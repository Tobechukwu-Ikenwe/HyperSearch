#include "QueryAnalyzer.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

bool is_word_char(unsigned char c) {
    return c >= 128 || std::isalnum(c) || c == '_';
}

std::string lowercase_ascii(std::string text) {
    for (char& c : text) {
        auto byte = static_cast<unsigned char>(c);
        if (byte < 128) {
            c = static_cast<char>(std::tolower(byte));
        }
    }
    return text;
}

std::string trim_query(std::string_view input) {
    std::size_t first = 0;
    std::size_t last = input.size();

    while (first < last &&
           std::isspace(static_cast<unsigned char>(input[first]))) {
        ++first;
    }
    while (last > first &&
           std::isspace(static_cast<unsigned char>(input[last - 1]))) {
        --last;
    }

    return std::string(input.substr(first, last - first));
}

const std::unordered_set<std::string>& stopwords() {
    static const std::unordered_set<std::string> words = {
        "a", "an", "the", "is", "are", "be", "do", "does",
        "of", "to", "in", "for", "by", "with", "from",
        "that", "this", "it", "and", "or", "how", "where",
        "what", "which", "when", "why", "who", "find", "show"
    };
    return words;
}

std::vector<std::string> split_parts(std::string_view text) {
    std::vector<std::string> parts;
    std::string part;

    for (unsigned char c : text) {
        if (c == '_' || !is_word_char(c)) {
            if (!part.empty()) {
                parts.push_back(lowercase_ascii(std::move(part)));
                part.clear();
            }
        } else {
            part.push_back(static_cast<char>(c));
        }
    }

    if (!part.empty()) {
        parts.push_back(lowercase_ascii(std::move(part)));
    }

    return parts;
}

bool contains_uppercase_ascii(std::string_view text) {
    return std::any_of(text.begin(), text.end(), [](unsigned char c) {
        return c >= 'A' && c <= 'Z';
    });
}

QueryTerm make_term(std::string text) {
    const bool qualified =
        text.find('.') != std::string::npos ||
        text.find("::") != std::string::npos;

    const bool identifier =
        text.find('_') != std::string::npos ||
        contains_uppercase_ascii(text);

    TermKind kind = TermKind::Word;
    if (qualified) {
        kind = TermKind::QualifiedSymbol;
    } else if (identifier) {
        kind = TermKind::Identifier;
    }

    auto parts = split_parts(text);
    return QueryTerm{std::move(text), kind, std::move(parts)};
}

std::string quote_fts_phrase(const std::string& phrase) {
    std::string quoted;
    quoted.reserve(phrase.size() + 2);
    quoted.push_back('"');

    for (char c : phrase) {
        if (c == '"') {
            quoted.push_back('"');
        }
        quoted.push_back(c);
    }

    quoted.push_back('"');
    return quoted;
}

bool is_natural_language(std::string_view query) {
    std::size_t words = 0;
    bool in_word = false;

    for (unsigned char c : query) {
        const bool whitespace = std::isspace(c);
        if (!whitespace && !in_word) {
            ++words;
        }
        in_word = !whitespace;
    }

    if (words >= 4 || (!query.empty() && query.back() == '?')) {
        return true;
    }

    const auto end = query.find_first_of(" \t\r\n");
    const std::string first =
        lowercase_ascii(std::string(query.substr(0, end)));

    static const std::unordered_set<std::string> starters = {
        "how", "where", "what", "which", "when",
        "why", "who", "find", "show"
    };

    return starters.contains(first);
}

} // namespace

SearchQuery analyze_query(std::string_view input) {
    SearchQuery result;
    result.original_text = std::string(input);
    result.embedding_text = trim_query(input);

    if (result.embedding_text.empty()) {
        return result;
    }

    result.is_natural_language = is_natural_language(result.embedding_text);

    std::string run;
    auto flush_run = [&]() {
        if (!run.empty()) {
            result.terms.push_back(make_term(std::move(run)));
            run.clear();
        }
    };

    for (unsigned char c : result.embedding_text) {
        if (std::isspace(c)) {
            flush_run();
        } else if (is_word_char(c) || c == ':' || c == '.') {
            run.push_back(static_cast<char>(c));
        } else {
            flush_run();
        }
    }
    flush_run();

    std::vector<std::string> phrases;
    std::unordered_set<std::string> seen;

    for (const QueryTerm& term : result.terms) {
        if (term.parts.empty()) {
            continue;
        }

        if (term.kind == TermKind::Word &&
            stopwords().contains(lowercase_ascii(term.text))) {
            continue;
        }

        std::string phrase;
        for (const auto& part : term.parts) {
            if (!phrase.empty()) {
                phrase.push_back(' ');
            }
            phrase += part;
        }

        if (seen.insert(phrase).second) {
            phrases.push_back(quote_fts_phrase(phrase));
        }
    }

    if (phrases.empty()) {
        for (const QueryTerm& term : result.terms) {
            std::string phrase;
            for (const auto& part : term.parts) {
                if (!phrase.empty()) {
                    phrase.push_back(' ');
                }
                phrase += part;
            }

            if (!phrase.empty() && seen.insert(phrase).second) {
                phrases.push_back(quote_fts_phrase(phrase));
            }
        }
    }

    const char* joiner = result.is_natural_language ? " OR " : " AND ";
    for (const auto& phrase : phrases) {
        if (!result.lexical_match.empty()) {
            result.lexical_match += joiner;
        }
        result.lexical_match += phrase;
    }

    return result;
}
