#include "EmbeddingWorker.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

// Escape a C++ string so it can safely appear inside a JSON string.
std::string json_escape(std::string_view text) {
    std::string result;
    result.reserve(text.size() + 2);

    for (unsigned char c : text) {
        switch (c) {
        case '"':  result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b";  break;
        case '\f': result += "\\f";  break;
        case '\n': result += "\\n";  break;
        case '\r': result += "\\r";  break;
        case '\t': result += "\\t";  break;
        default:
            if (c < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                result += "\\u00";
                result += hex[c >> 4];
                result += hex[c & 0x0f];
            } else {
                result += static_cast<char>(c);
            }
        }
    }

    return result;
}

std::size_t find_value(std::string_view json, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const auto key_pos = json.find(needle);

    if (key_pos == std::string_view::npos) {
        throw std::runtime_error("Worker response is missing field: " +
                                 std::string(key));
    }

    const auto colon = json.find(':', key_pos + needle.size());
    if (colon == std::string_view::npos) {
        throw std::runtime_error("Malformed JSON field: " +
                                 std::string(key));
    }

    auto value = colon + 1;
    while (value < json.size() &&
           (json[value] == ' ' || json[value] == '\t' ||
            json[value] == '\r' || json[value] == '\n')) {
        ++value;
    }

    return value;
}

std::string parse_json_string(std::string_view json, std::size_t& pos) {
    if (pos >= json.size() || json[pos] != '"') {
        throw std::runtime_error("Expected a JSON string");
    }

    ++pos;
    std::string result;

    while (pos < json.size()) {
        const char c = json[pos++];

        if (c == '"') {
            return result;
        }

        if (c != '\\') {
            result += c;
            continue;
        }

        if (pos >= json.size()) {
            throw std::runtime_error("Incomplete JSON escape");
        }

        const char escaped = json[pos++];

        switch (escaped) {
        case '"':  result += '"';  break;
        case '\\': result += '\\'; break;
        case '/':  result += '/';  break;
        case 'b':  result += '\b'; break;
        case 'f':  result += '\f'; break;
        case 'n':  result += '\n'; break;
        case 'r':  result += '\r'; break;
        case 't':  result += '\t'; break;
        case 'u': {
            // Worker-generated IDs, model names, and error messages are
            // expected to use ordinary UTF-8; handle ASCII \uXXXX escapes.
            if (pos + 4 > json.size()) {
                throw std::runtime_error("Incomplete Unicode escape");
            }

            unsigned code = 0;
            for (int i = 0; i < 4; ++i) {
                const char h = json[pos++];
                code <<= 4;

                if (h >= '0' && h <= '9') code |= h - '0';
                else if (h >= 'a' && h <= 'f') code |= h - 'a' + 10;
                else if (h >= 'A' && h <= 'F') code |= h - 'A' + 10;
                else throw std::runtime_error("Invalid Unicode escape");
            }

            if (code > 0x7f) {
                throw std::runtime_error(
                    "Non-ASCII Unicode escape in worker response"
                );
            }

            result += static_cast<char>(code);
            break;
        }
        default:
            throw std::runtime_error("Invalid JSON escape");
        }
    }

    throw std::runtime_error("Unterminated JSON string");
}

std::string json_string(std::string_view json, std::string_view key) {
    auto pos = find_value(json, key);
    return parse_json_string(json, pos);
}

std::size_t json_size(std::string_view json, std::string_view key) {
    const auto pos = find_value(json, key);
    std::size_t value = 0;

    const auto* first = json.data() + pos;
    const auto* last = json.data() + json.size();
    const auto parsed = std::from_chars(first, last, value);

    if (parsed.ec != std::errc{}) {
        throw std::runtime_error("Invalid numeric JSON field: " +
                                 std::string(key));
    }

    return value;
}

bool json_bool(std::string_view json, std::string_view key) {
    const auto pos = find_value(json, key);

    if (json.substr(pos, 4) == "true") return true;
    if (json.substr(pos, 5) == "false") return false;

    throw std::runtime_error("Invalid Boolean JSON field: " +
                             std::string(key));
}

std::size_t find_array_start(std::string_view json, std::string_view key) {
    const auto pos = find_value(json, key);

    if (pos >= json.size() || json[pos] != '[') {
        throw std::runtime_error("Expected JSON array: " +
                                 std::string(key));
    }

    return pos;
}

void skip_space(std::string_view json, std::size_t& pos) {
    while (pos < json.size() &&
           (json[pos] == ' ' || json[pos] == '\t' ||
            json[pos] == '\r' || json[pos] == '\n')) {
        ++pos;
    }
}

std::vector<float> parse_vector(std::string_view json, std::size_t& pos) {
    if (pos >= json.size() || json[pos] != '[') {
        throw std::runtime_error("Expected embedding vector");
    }

    ++pos;
    std::vector<float> values;
    skip_space(json, pos);

    if (pos < json.size() && json[pos] == ']') {
        ++pos;
        return values;
    }

    while (pos < json.size()) {
        skip_space(json, pos);

        char* end = nullptr;
        const char* begin = json.data() + pos;
        const float value = std::strtof(begin, &end);

        if (end == begin || !std::isfinite(value)) {
            throw std::runtime_error("Invalid number in embedding vector");
        }

        pos += static_cast<std::size_t>(end - begin);
        values.push_back(value);
        skip_space(json, pos);

        if (pos >= json.size()) break;
        if (json[pos] == ']') {
            ++pos;
            return values;
        }
        if (json[pos] != ',') {
            throw std::runtime_error("Malformed embedding vector");
        }

        ++pos;
    }

    throw std::runtime_error("Unterminated embedding vector");
}

std::vector<float> json_vector(std::string_view json,
                               std::string_view key) {
    auto pos = find_array_start(json, key);
    return parse_vector(json, pos);
}

std::vector<std::vector<float>> json_matrix(
    std::string_view json, std::string_view key
) {
    auto pos = find_array_start(json, key);
    ++pos;

    std::vector<std::vector<float>> matrix;
    skip_space(json, pos);

    if (pos < json.size() && json[pos] == ']') {
        return matrix;
    }

    while (pos < json.size()) {
        skip_space(json, pos);
        matrix.push_back(parse_vector(json, pos));
        skip_space(json, pos);

        if (pos >= json.size()) break;
        if (json[pos] == ']') return matrix;
        if (json[pos] != ',') {
            throw std::runtime_error("Malformed embedding matrix");
        }

        ++pos;
    }

    throw std::runtime_error("Unterminated embedding matrix");
}

void write_all(int fd, std::string_view data) {
    while (!data.empty()) {
        const auto written = ::write(fd, data.data(), data.size());

        if (written < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(),
                                    "Writing to Python worker");
        }

        if (written == 0) {
            throw std::runtime_error("Python worker closed its input");
        }

        data.remove_prefix(static_cast<std::size_t>(written));
    }
}

std::string read_line(int fd) {
    std::string line;
    char c;

    while (true) {
        const auto count = ::read(fd, &c, 1);

        if (count < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(),
                                    "Reading from Python worker");
        }

        if (count == 0) {
            throw std::runtime_error(
                "Python worker exited before returning a response"
            );
        }

        if (c == '\n') return line;
        line += c;
    }
}

} // namespace

EmbeddingWorker::EmbeddingWorker(
    std::filesystem::path python_executable,
    std::filesystem::path worker_script
)
    : python_executable_(std::move(python_executable)),
      worker_script_(std::move(worker_script)) {}

EmbeddingWorker::~EmbeddingWorker() {
    stop();
}

void EmbeddingWorker::start() {
    if (process_id_ > 0) return;

    int to_child[2];
    int from_child[2];

    if (::pipe(to_child) < 0) {
        throw std::system_error(errno, std::generic_category(),
                                "Creating worker input pipe");
    }

    if (::pipe(from_child) < 0) {
        const int saved_errno = errno;
        ::close(to_child[0]);
        ::close(to_child[1]);
        throw std::system_error(saved_errno, std::generic_category(),
                                "Creating worker output pipe");
    }

    const pid_t child = ::fork();

    if (child < 0) {
        const int saved_errno = errno;
        ::close(to_child[0]);
        ::close(to_child[1]);
        ::close(from_child[0]);
        ::close(from_child[1]);
        throw std::system_error(saved_errno, std::generic_category(),
                                "Starting Python worker");
    }

    if (child == 0) {
        ::close(to_child[1]);
        ::close(from_child[0]);

        if (::dup2(to_child[0], STDIN_FILENO) < 0 ||
            ::dup2(from_child[1], STDOUT_FILENO) < 0) {
            _exit(126);
        }

        ::close(to_child[0]);
        ::close(from_child[1]);

        const auto python = python_executable_.string();
        const auto script = worker_script_.string();

        ::execlp(python.c_str(), python.c_str(),
                 script.c_str(), static_cast<char*>(nullptr));

        _exit(127);
    }

    ::close(to_child[0]);
    ::close(from_child[1]);

    input_fd_ = to_child[1];
    output_fd_ = from_child[0];
    process_id_ = static_cast<int>(child);
}

void EmbeddingWorker::stop() noexcept {
    if (input_fd_ >= 0) {
        ::close(input_fd_);
        input_fd_ = -1;
    }

    if (output_fd_ >= 0) {
        ::close(output_fd_);
        output_fd_ = -1;
    }

    if (process_id_ > 0) {
        int status = 0;
        while (::waitpid(process_id_, &status, 0) < 0 && errno == EINTR) {}
        process_id_ = -1;
    }
}

std::string EmbeddingWorker::make_request_id() {
    return std::to_string(next_request_id_++);
}

std::string EmbeddingWorker::request(const std::string& json) {
    start();

    try {
        write_all(input_fd_, json);
        write_all(input_fd_, "\n");

        const auto response = read_line(output_fd_);
        const auto expected_id = json_string(
            "{\"id\":\"" + make_request_id() + "\"}", "id"
        );
        (void)expected_id;

        if (!json_bool(response, "ok")) {
            throw std::runtime_error(
                "Python embedding worker: " + json_string(response, "error")
            );
        }

        return response;
    } catch (...) {
        stop();
        throw;
    }
}

EmbeddingWorker::Metadata EmbeddingWorker::metadata() {
    const auto id = make_request_id();
    const auto response = request(
        "{\"id\":\"" + id + "\",\"op\":\"metadata\"}"
    );

    if (json_string(response, "id") != id) {
        stop();
        throw std::runtime_error("Python worker response ID mismatch");
    }

    return {
        json_string(response, "model_id"),
        json_size(response, "dimension")
    };
}

std::vector<float> EmbeddingWorker::embed_query(const std::string& text) {
    const auto id = make_request_id();
    const auto response = request(
        "{\"id\":\"" + id + "\",\"op\":\"embed_query\",\"text\":\"" +
        json_escape(text) + "\"}"
    );

    if (json_string(response, "id") != id) {
        stop();
        throw std::runtime_error("Python worker response ID mismatch");
    }

    return json_vector(response, "embedding");
}

std::vector<std::vector<float>> EmbeddingWorker::embed_documents(
    const std::vector<std::string>& texts
) {
    const auto id = make_request_id();
    std::string json =
        "{\"id\":\"" + id + "\",\"op\":\"embed_documents\",\"texts\":[";

    for (std::size_t i = 0; i < texts.size(); ++i) {
        if (i != 0) json += ',';
        json += "\"" + json_escape(texts[i]) + "\"";
    }

    json += "]}";

    const auto response = request(json);

    if (json_string(response, "id") != id) {
        stop();
        throw std::runtime_error("Python worker response ID mismatch");
    }

    return json_matrix(response, "embeddings");
}
