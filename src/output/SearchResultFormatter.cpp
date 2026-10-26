#include "SearchResultFormatter.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace {

struct Excerpt {
    std::size_t first_line;
    std::size_t last_line;
    std::vector<std::string> lines;
    bool approximate;
};

std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> result;
    std::string word;
    unsigned char previous = 0;

    for (unsigned char character : text) {
        if (!std::isalnum(character)) {
            if (!word.empty()) {
                result.push_back(std::move(word));
                word.clear();
            }
            previous = 0;
            continue;
        }

        // Split camel-case identifiers as well as snake_case identifiers.
        if (std::isupper(character) && !word.empty() &&
            (std::islower(previous) || std::isdigit(previous))) {
            result.push_back(std::move(word));
            word.clear();
        }
        word.push_back(static_cast<char>(std::tolower(character)));
        previous = character;
    }

    if (!word.empty()) {
        result.push_back(std::move(word));
    }
    return result;
}

std::vector<std::string> query_words(const SearchQuery& query) {
    static const std::unordered_set<std::string> stopwords = {
        "a", "an", "the", "is", "are", "be", "do", "does",
        "of", "to", "in", "for", "by", "with", "from",
        "that", "this", "it", "and", "or", "how", "where",
        "what", "which", "when", "why", "who", "find", "show"
    };

    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    for (const QueryTerm& term : query.terms) {
        for (auto& word : words(term.text)) {
            if (word.size() >= 3 && !stopwords.contains(word) &&
                seen.insert(word).second) {
                result.push_back(std::move(word));
            }
        }
    }
    return result;
}

int word_match(const std::string& query_word, const std::string& source_word) {
    if (query_word == source_word) {
        return 3;
    }

    // A shared stem catches simple variants such as generate/generated and
    // embedding/embeddings without changing the search ranking.
    std::size_t common = 0;
    while (common < query_word.size() && common < source_word.size() &&
           query_word[common] == source_word[common]) {
        ++common;
    }
    return common >= 4 ? 1 : 0;
}

int line_score(
    const std::string& line,
    const std::vector<std::string>& terms
) {
    const auto source_words = words(line);
    int score = 0;
    for (const auto& term : terms) {
        int best = 0;
        for (const auto& source_word : source_words) {
            best = std::max(best, word_match(term, source_word));
        }
        score += best;
    }

    const auto first = line.find_first_not_of(" \t");
    if (score > 0 && first != std::string::npos &&
        (line.compare(first, 2, "//") == 0 ||
         line[first] == '#' || line[first] == '*')) {
        score = std::max(1, score / 2);
    }
    return score;
}

int result_relevance(
    const SearchResult& result,
    const std::vector<std::string>& terms
) {
    if (terms.empty()) {
        return 0;
    }

    const auto extension = std::filesystem::path(result.file_path).extension();
    const bool code_file = extension == ".cpp" || extension == ".cc" ||
        extension == ".c" || extension == ".h" || extension == ".hpp" ||
        extension == ".py" || extension == ".rs" || extension == ".go" ||
        extension == ".java" || extension == ".js" || extension == ".ts";
    const auto source_words = words(result.text);
    std::vector<std::string> code_words;
    if (code_file) {
        std::istringstream source(result.text);
        std::string line;
        while (std::getline(source, line)) {
            const auto first = line.find_first_not_of(" \t");
            if (first == std::string::npos ||
                line.compare(first, 2, "//") == 0 || line[first] == '#' ||
                line[first] == '*') {
                continue;
            }
            auto line_words = words(line);
            code_words.insert(
                code_words.end(), line_words.begin(), line_words.end());
        }
    }
    int matched_terms = 0;
    int code_matched_terms = 0;
    int strength = 0;
    for (const auto& term : terms) {
        int best = 0;
        for (const auto& source_word : source_words) {
            best = std::max(best, word_match(term, source_word));
        }
        if (best > 0) {
            ++matched_terms;
            strength += best;
        }
        if (std::any_of(code_words.begin(), code_words.end(),
                        [&](const auto& word) {
                            return word_match(term, word) > 0;
                        })) {
            ++code_matched_terms;
        }
    }

    // Covering more parts of a question matters more than repeating one word.
    // Prefer implementation files when coverage is equal.
    return code_matched_terms * 1000 + matched_terms * 100 +
        strength * 10 + (code_file ? 1 : 0);
}

struct ActionLocation {
    std::string path;
    std::size_t first_line = 0;
    std::size_t last_line = 0;
    std::vector<std::string> lines;
    int score = 0;
    std::string explanation;
};

struct QueryFacet {
    std::string action;
    std::string subject;
    std::vector<std::string> terms;
};

bool is_action(std::string_view word) {
    static const std::unordered_set<std::string> common = {
        "build", "cache", "create", "delete", "embed", "execute", "fetch",
        "find", "generate", "index", "load", "parse", "read", "rank",
        "retrieve", "save", "scan", "search", "store", "update", "write"
    };
    return (word.size() > 4 && (word.ends_with("ed") ||
            word.ends_with("ing"))) || common.contains(std::string(word));
}

std::vector<QueryFacet> query_facets(const SearchQuery& query) {
    if (!query.is_natural_language) return {};
    std::string with_conjunctions;
    for (char character : query.original_text) {
        if (character == ',') with_conjunctions += " and ";
        else with_conjunctions += character;
    }
    const auto tokens = words(with_conjunctions);
    const auto first_action = std::find_if(tokens.begin(), tokens.end(), is_action);
    if (first_action == tokens.end()) return {};

    std::vector<std::string> subject_terms;
    for (auto it = tokens.begin(); it != first_action; ++it) {
        if (it->size() >= 3 && *it != "are" && *it != "was" &&
            *it != "were" && *it != "how" && *it != "where" &&
            *it != "what" && *it != "which" && *it != "does" &&
            *it != "the" && *it != "and") {
            subject_terms.push_back(*it);
        }
    }
    if (subject_terms.empty()) return {};

    std::vector<QueryFacet> facets;
    auto previous_action = first_action;
    for (auto it = first_action; it != tokens.end(); ++it) {
        if (!is_action(*it)) continue;
        std::vector<std::string> current_subject = subject_terms;
        if (!facets.empty()) {
            const auto boundary = std::find_if(previous_action + 1, it,
                [](const auto& token) { return token == "and" || token == "or"; });
            if (boundary == it) continue;
            std::vector<std::string> local_subject;
            for (auto token = boundary + 1; token != it; ++token) {
                if (token->size() >= 3 && *token != "are" && *token != "was" &&
                    *token != "were" && *token != "how" && *token != "where" &&
                    *token != "what" && *token != "the" && *token != "and" &&
                    *token != "or")
                    local_subject.push_back(*token);
            }
            if (!local_subject.empty()) current_subject = std::move(local_subject);
        }
        QueryFacet facet;
        facet.action = *it;
        facet.terms = current_subject;
        facet.terms.push_back(*it);
        for (std::size_t i = 0; i < current_subject.size(); ++i) {
            if (i) facet.subject += ' ';
            facet.subject += current_subject[i];
        }
        facets.push_back(std::move(facet));
        subject_terms = std::move(current_subject);
        previous_action = it;
    }
    return facets.size() >= 2 ? facets : std::vector<QueryFacet>{};
}

std::vector<std::string> action_words(std::string_view action) {
    // General code-operation vocabulary bridges question verbs to common
    // implementation verbs. All choices still need source evidence.
    if (action.starts_with("generat") || action.starts_with("creat") ||
        action.starts_with("build"))
        return {std::string(action), "generate", "create", "build", "embed", "encode"};
    if (action.starts_with("stor") || action.starts_with("sav") ||
        action.starts_with("cach") || action.starts_with("writ"))
        return {std::string(action), "store", "save", "cache", "insert", "write", "persist"};
    if (action.starts_with("load") || action.starts_with("read") ||
        action.starts_with("retriev") || action.starts_with("fetch"))
        return {std::string(action), "load", "read", "retrieve", "fetch", "select"};
    if (action.starts_with("rank") || action.starts_with("sort"))
        return {std::string(action), "rank", "sort", "score", "order"};
    if (action.starts_with("scann") || action.starts_with("scan"))
        return {std::string(action), "scan", "walk", "traverse", "iterate"};
    if (action.starts_with("index"))
        return {std::string(action), "index", "insert", "add"};
    return {std::string(action)};
}

std::string without_quoted_text(std::string_view line) {
    std::string result;
    char quote = '\0';
    bool escaped = false;
    for (char character : line) {
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == quote) {
                quote = '\0';
            }
        } else if (character == '"' || character == '\'') {
            quote = character;
        } else {
            result.push_back(character);
        }
    }
    return result;
}

std::string called_symbol(std::string_view code) {
    const auto open = code.find('(');
    if (open == std::string_view::npos) return {};
    std::size_t end = open;
    while (end > 0 && std::isspace(static_cast<unsigned char>(code[end - 1]))) --end;
    std::size_t begin = end;
    while (begin > 0 && (std::isalnum(static_cast<unsigned char>(code[begin - 1])) ||
                         code[begin - 1] == '_')) --begin;
    if (begin == end) return {};
    return std::string(code.substr(begin, end - begin));
}

void append_location(std::ostringstream& output, const ActionLocation& location) {
    output << location.path << ':' << location.first_line;
    if (location.last_line != location.first_line) {
        output << '-' << location.last_line;
    }
    output << '\n';
    const int width =
        static_cast<int>(std::to_string(location.last_line).size());
    for (std::size_t i = 0; i < location.lines.size(); ++i) {
        output << std::setw(width) << location.first_line + i
               << " | " << location.lines[i] << '\n';
    }
    output << "  " << location.explanation << "\n\n";
}

std::optional<std::string> format_facets(
    const std::vector<SearchResult>& results,
    const SearchQuery& query,
    const std::filesystem::path& repository
) {
    const auto facets = query_facets(query);
    if (facets.empty()) return std::nullopt;
    std::vector<ActionLocation> answers(facets.size());
    std::vector<std::vector<std::string>> operations;
    operations.reserve(facets.size());
    for (const auto& facet : facets) operations.push_back(action_words(facet.action));
    std::unordered_set<std::string> visited_files;

    for (const SearchResult& result : results) {
        if (!visited_files.insert(result.file_path).second) {
            continue;
        }
        const auto extension =
            std::filesystem::path(result.file_path).extension();
        if (extension != ".cpp" && extension != ".cc" &&
            extension != ".c" && extension != ".py" &&
            extension != ".rs" && extension != ".go" &&
            extension != ".java" && extension != ".js" &&
            extension != ".ts") {
            continue;
        }

        std::ifstream source(repository / result.file_path);
        if (!source) {
            continue;
        }
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(source, line)) {
            lines.push_back(std::move(line));
        }
        const auto context_words = words(result.file_path + " " + result.text);

        bool in_raw_string = false;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const std::string& code = lines[i];
            const bool sql_literal_line = in_raw_string ||
                code.find("R\"(") != std::string::npos;
            if (code.find("R\"(") != std::string::npos) in_raw_string = true;
            if (code.find(")\"") != std::string::npos) in_raw_string = false;
            const auto first = code.find_first_not_of(" \t");
            if (first == std::string::npos ||
                code.compare(first, 2, "//") == 0 || code[first] == '#') {
                continue;
            }
            const bool sql = sql_literal_line &&
                (code.find("INSERT INTO ") != std::string::npos ||
                 code.find("SELECT ") != std::string::npos ||
                 code.find("UPDATE ") != std::string::npos);
            const std::string executable = sql ? code : without_quoted_text(code);
            const auto code_words = words(executable);

            for (std::size_t facet_index = 0; facet_index < facets.size(); ++facet_index) {
                const auto& facet = facets[facet_index];
                const auto& synonyms = operations[facet_index];
                int action_score = 0;
                for (const auto& word : code_words) {
                    for (const auto& synonym : synonyms) {
                        action_score = std::max(action_score, word_match(synonym, word));
                    }
                }
                if (!action_score) continue;
                int subject_score = 0;
                for (const auto& term : facet.terms) {
                    if (term == facet.action) continue;
                    for (const auto& word : code_words) {
                        subject_score = std::max(subject_score, word_match(term, word));
                    }
                }
                int context_score = 0;
                for (const auto& term : facet.terms) {
                    if (term == facet.action) continue;
                    for (const auto& word : context_words) {
                        context_score = std::max(context_score, word_match(term, word));
                    }
                }
                if (!subject_score && !context_score) continue;
                int score = action_score * 20 + subject_score * 10 + context_score * 3;
                if (executable.find('(') != std::string::npos) score += 12;
                if (executable.find("->") != std::string::npos ||
                    executable.find('.') != std::string::npos) score += 30;
                if (result.file_path.starts_with("src/")) score += 50;
                if (result.file_path.starts_with("tests/") ||
                    result.file_path.starts_with("benchmarks/")) score -= 50;
                if (sql && code.find("INSERT INTO ") != std::string::npos &&
                    (facet.action.starts_with("stor") || facet.action.starts_with("sav")))
                    score += 30;
                if (executable.find("::") != std::string::npos &&
                    executable.find('{') != std::string::npos) score -= 15;
                if (score <= answers[facet_index].score) continue;

                std::size_t begin = i;
                std::size_t end = i;
                if (sql && i > 0 && lines[i - 1].find("R\"(") != std::string::npos) {
                    begin = i - 1;
                    while (end + 1 < lines.size() && end - i < 12 &&
                           lines[end].find(")\"") == std::string::npos &&
                           lines[end].find(';') == std::string::npos) ++end;
                }
                const std::string symbol = called_symbol(executable);
                const std::string explanation = sql
                    ? (code.find("INSERT INTO ") != std::string::npos
                       ? "This INSERT writes matching data to the table."
                       : code.find("SELECT ") != std::string::npos
                         ? "This SELECT reads matching data from the table."
                         : "This UPDATE changes matching rows in the table.")
                    : !symbol.empty()
                      ? "Calls `" + symbol + "` while handling " +
                        facet.subject + "."
                      : "This source line handles " + facet.subject +
                        " for the " + facet.action + " step.";
                answers[facet_index] = {result.file_path, begin + 1, end + 1,
                    {lines.begin() + begin, lines.begin() + end + 1}, score,
                    explanation};
            }
        }
    }
    std::ostringstream output;
    for (std::size_t i = 0; i < facets.size(); ++i) {
        std::string label = facets[i].action;
        label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        output << label << " here\n";
        if (answers[i].score > 0) append_location(output, answers[i]);
        else output << "No confident source match found for this step.\n\n";
    }
    return output.str();
}

Excerpt select_excerpt(
    const SearchResult& result,
    const std::vector<std::string>& terms,
    const std::filesystem::path& repository
) {
    std::vector<std::string> lines;
    std::ifstream source(repository / result.file_path);
    const bool source_available = static_cast<bool>(source);
    std::istringstream cached_chunk(result.text);
    std::istream& input = source_available
        ? static_cast<std::istream&>(source)
        : static_cast<std::istream&>(cached_chunk);
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(std::move(line));
    }

    if (lines.empty()) {
        return {result.start_line, result.start_line, {}, true};
    }

    const std::size_t base_line = source_available ? 1 : result.start_line;
    const std::size_t candidate_first = result.start_line > base_line
        ? std::min(result.start_line - base_line, lines.size() - 1) : 0;
    const std::size_t candidate_last = std::min(
        result.end_line >= base_line
            ? result.end_line - base_line + 1 : lines.size(),
        lines.size()
    );

    std::size_t anchor = candidate_first;
    int best_score = 0;
    for (std::size_t i = candidate_first; i < candidate_last; ++i) {
        const int score = line_score(lines[i], terms);
        if (score > best_score) {
            best_score = score;
            anchor = i;
        }
    }

    if (best_score == 0) {
        // Semantic retrieval may find a relevant chunk with no literal query
        // words. Show its center, but mark the location as approximate.
        anchor = candidate_first + (candidate_last - candidate_first) / 2;
        for (std::size_t offset = 0; offset < candidate_last - candidate_first;
             ++offset) {
            const std::size_t i = candidate_first +
                (anchor - candidate_first + offset) %
                    (candidate_last - candidate_first);
            if (lines[i].find_first_not_of(" \t\r") != std::string::npos) {
                anchor = i;
                break;
            }
        }
    }

    std::size_t second_anchor = anchor;
    if (best_score > 0 && terms.size() > 1) {
        const auto anchor_words = words(lines[anchor]);
        int most_new_terms = 0;
        for (std::size_t i = candidate_first; i < candidate_last; ++i) {
            const std::size_t distance = i > anchor ? i - anchor : anchor - i;
            if (i == anchor || distance > 24) {
                continue;
            }
            const auto other_words = words(lines[i]);
            int new_terms = 0;
            for (const auto& term : terms) {
                const auto matches = [&](const auto& word) {
                    return word_match(term, word) > 0;
                };
                if (std::none_of(anchor_words.begin(), anchor_words.end(), matches) &&
                    std::any_of(other_words.begin(), other_words.end(), matches)) {
                    ++new_terms;
                }
            }
            if (new_terms > most_new_terms) {
                most_new_terms = new_terms;
                second_anchor = i;
            }
        }
    }

    // A blank line marks a natural paragraph or code block boundary. Read
    // from the source file so the selected span may cross indexed chunks.
    auto blank = [&lines](std::size_t i) {
        return lines[i].find_first_not_of(" \t\r") == std::string::npos;
    };
    std::size_t first = anchor;
    while (first > 0 && !blank(first - 1)) {
        --first;
    }
    std::size_t last = anchor + 1;
    while (last < lines.size() && !blank(last)) {
        ++last;
    }
    if (second_anchor != anchor) {
        first = std::min(first, second_anchor);
        last = std::max(last, second_anchor + 1);
        while (first > 0 && !blank(first - 1)) {
            --first;
        }
        while (last < lines.size() && !blank(last)) {
            ++last;
        }
    }

    return {
        base_line + first,
        base_line + last - 1,
        {lines.begin() + first, lines.begin() + last},
        best_score == 0
    };
}

} // namespace

std::string SearchResultFormatter::format(
    const std::vector<SearchResult>& results,
    const SearchQuery& query,
    const std::filesystem::path& repository
) const {
    if (results.empty()) {
        return "No results found.\n";
    }

    std::ostringstream output;
    const auto terms = query_words(query);
    if (auto facets = format_facets(results, query, repository)) {
        return *facets;
    }
    std::vector<const SearchResult*> ranked;
    ranked.reserve(results.size());
    for (const auto& result : results) {
        ranked.push_back(&result);
    }
    std::stable_sort(ranked.begin(), ranked.end(), [&](const auto* left,
                                                       const auto* right) {
        return result_relevance(*left, terms) >
               result_relevance(*right, terms);
    });
    std::unordered_set<std::string> shown_excerpts;
    std::unordered_set<std::string> shown_files;
    std::size_t shown_count = 0;
    constexpr std::size_t max_likely_alternatives = 3;

    for (const SearchResult* candidate : ranked) {
        const SearchResult& result = *candidate;
        if (shown_files.contains(result.file_path)) {
            continue;
        }
        const Excerpt excerpt = select_excerpt(result, terms, repository);
        const std::string location = result.file_path + ':' +
            std::to_string(excerpt.first_line) + '-' +
            std::to_string(excerpt.last_line);
        if (!shown_excerpts.insert(location).second) {
            continue;
        }
        shown_files.insert(result.file_path);

        if (shown_count == 0) {
            output << "Most likely match";
        } else {
            output << "Likely alternative " << shown_count;
        }
        ++shown_count;
        if (excerpt.approximate) {
            output << " (approximate excerpt)";
        }
        output << '\n' << result.file_path << ':' << excerpt.first_line;
        if (excerpt.last_line != excerpt.first_line) {
            output << '-' << excerpt.last_line;
        }
        output << '\n';

        const int width =
            static_cast<int>(std::to_string(excerpt.last_line).size());
        for (std::size_t j = 0; j < excerpt.lines.size(); ++j) {
            output << std::setw(width) << excerpt.first_line + j
                   << " | " << excerpt.lines[j] << '\n';
        }
        output << '\n';

        if (shown_count == 1 + max_likely_alternatives) {
            break;
        }
    }

    return output.str();
}
