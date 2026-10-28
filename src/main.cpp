#include "indexer/CodebaseScanner.h"
#include "indexer/CodeChunker.h"
#include "storage/ChunkStore.h"
#include "storage/EmbeddingStore.h"
#include "embedding/EmbeddingPipeline.h"
#include "search/QueryAnalyzer.h"
#include "search/LexicalSearch.hpp"
#include "search/SemanticSearch.hpp"
#include "search/ResultFusion.hpp"
#include "embedding/VectorStore.hpp"
#include "output/SearchResultFormatter.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open file: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

std::string fingerprint(const std::string& content) {
    std::uint64_t value = 14695981039346656037ULL;
    for (unsigned char byte : content) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
    return std::to_string(content.size()) + ":" + std::to_string(value);
}

void print_usage(std::ostream& output, const char* program) {
    output << "Usage:\n"
           << "  " << program << " index <repository-path>\n"
           << "  " << program << " query [--json] <repository-path> <search query>\n"
           << "  " << program << " search [--json] <repository-path> <search query>\n"
           << "  " << program << " <repository-path> <search query>  (search alias)\n";
}

std::string json_string(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char byte : value) {
        switch (byte) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (byte < 0x20) {
                    out << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0') << static_cast<int>(byte)
                        << std::dec;
                } else {
                    out << static_cast<char>(byte);
                }
        }
    }
    out << '"';
    return out.str();
}

void print_json_results(const std::vector<SearchResult>& results) {
    std::cout << "{\"schema_version\":1,\"results\":[";
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        if (i) std::cout << ',';
        std::cout << "{\"file\":" << json_string(result.file_path)
                  << ",\"start_line\":" << result.start_line
                  << ",\"end_line\":" << result.end_line
                  << ",\"score\":" << result.score << '}';
    }
    std::cout << "]}\n";
}

struct IndexStats {
    std::size_t files = 0;
    std::size_t changed_files = 0;
    std::size_t chunks = 0;
};

IndexStats index_repository(const fs::path& repository,
                            ChunkStore& chunk_store,
                            EmbeddingStore& embedding_store) {
    CodebaseScanner scanner;
    CodeChunker chunker;
    const auto files = scanner.scan(repository);

    struct PendingFile {
        fs::path relative_path;
        std::string fingerprint;
        std::vector<CodeChunk> chunks;
    };
    std::vector<PendingFile> pending_files;
    std::vector<fs::path> current_file_paths;
    std::vector<CodeChunk> chunks;
    pending_files.reserve(files.size());
    current_file_paths.reserve(files.size());

    // Complete the scan before changing the index. A read error leaves the
    // previous index intact instead of removing files from an incomplete scan.
    for (const ScannedFile& file : files) {
        const std::string content = read_file(repository / file.relative_path);
        const std::string digest = fingerprint(content);
        current_file_paths.push_back(file.relative_path);
        if (chunk_store.file_is_current(file.relative_path, digest)) {
            continue;
        }
        auto file_chunks = chunker.chunk_file(file, content);
        pending_files.push_back(
            PendingFile{file.relative_path, digest, std::move(file_chunks)});
    }

    chunk_store.remove_missing_files(current_file_paths);
    for (const PendingFile& file : pending_files) {
        chunk_store.replace_file_chunks(file.relative_path, file.chunks);
        chunk_store.mark_file_current(file.relative_path, file.fingerprint);
    }

    EmbeddingPipeline embedding_pipeline(embedding_store);
    embedding_pipeline.initialize();
    chunks = chunk_store.all_chunks();
    embedding_pipeline.embed_chunks(chunks);
    return IndexStats{files.size(), pending_files.size(), chunks.size()};
}

void query_repository(const fs::path& repository, ChunkStore& chunk_store,
                      EmbeddingStore& embedding_store,
                      const std::string& query_text, bool json_output) {
    if (query_text.find_first_not_of(" \t\r\n") == std::string::npos) {
        throw std::invalid_argument("Search query cannot be empty");
    }

    const auto chunks = chunk_store.all_chunks();
    if (chunks.empty()) {
        throw std::runtime_error("Index is empty; run 'hypersearch index' first");
    }

    EmbeddingPipeline embedding_pipeline(embedding_store);
    embedding_pipeline.initialize();
    VectorStore vector_store;
    embedding_pipeline.index_chunks(chunks, vector_store);

    const SearchQuery analyzed_query = analyze_query(query_text);
    LexicalSearch lexical_search(chunk_store);
    SemanticSearch semantic_search(embedding_pipeline, vector_store);
    ResultFusion fusion;
    SearchResultFormatter formatter;

    const auto lexical_results = lexical_search.search(analyzed_query, 50);
    const auto semantic_results = semantic_search.search(analyzed_query, 50);
    const auto results = fusion.fuse(lexical_results, semantic_results, 100);
    if (json_output) {
        print_json_results(results);
    } else {
        std::cout << formatter.format(results, analyzed_query, repository);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc == 2 &&
            (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
            print_usage(std::cout, argv[0]);
            return 0;
        }
        if (argc < 3) {
            print_usage(std::cerr, argv[0]);
            return 2;
        }

        const std::string first = argv[1];
        const bool explicit_command =
            first == "index" || first == "query" || first == "search";
        const std::string command = explicit_command ? first : "search";
        const bool json_output = explicit_command && argc > 2 &&
            std::string(argv[2]) == "--json";
        if (json_output && command == "index") {
            print_usage(std::cerr, argv[0]);
            return 2;
        }
        const int path_arg = explicit_command ? (json_output ? 3 : 2) : 1;
        const int query_arg = path_arg + 1;
        if (argc <= path_arg ||
            (command == "index" ? argc != query_arg : argc <= query_arg)) {
            print_usage(std::cerr, argv[0]);
            return 2;
        }

        const fs::path repository = fs::canonical(argv[path_arg]);
        if (!fs::is_directory(repository)) {
            throw std::runtime_error("Repository path is not a directory");
        }

        std::string query_text;
        for (int i = query_arg; i < argc; ++i) {
            if (!query_text.empty()) query_text += ' ';
            query_text += argv[i];
        }

        const fs::path database_path =
            repository / ".hypersearch" / "index.sqlite3";
        if (command == "query" && !fs::exists(database_path)) {
            throw std::runtime_error(
                "Index does not exist; run 'hypersearch index' first");
        }
        if (command != "query") {
            fs::create_directories(database_path.parent_path());
        }

        ChunkStore chunk_store(database_path);
        chunk_store.initialize();
        EmbeddingStore embedding_store(database_path);
        embedding_store.initialize();

        if (command != "query") {
            const IndexStats stats =
                index_repository(repository, chunk_store, embedding_store);
            if (command == "index") {
                std::cout << "Indexed " << stats.files << " files ("
                          << stats.changed_files << " changed), "
                          << stats.chunks << " chunks.\n";
            }
        }
        if (command != "index") {
            query_repository(repository, chunk_store, embedding_store,
                             query_text, json_output);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "HyperSearch error: " << error.what() << '\n';
        return 1;
    }
}
