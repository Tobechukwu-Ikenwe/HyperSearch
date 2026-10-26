#pragma once

#include <string>
#include <vector>

enum class TermKind {
    Word,
    Identifier,
    QualifiedSymbol
};

struct QueryTerm {
    std::string text;
    TermKind kind;
    std::vector<std::string> parts;
};

struct SearchQuery {
    std::string original_text;
    std::string embedding_text;
    std::vector<QueryTerm> terms;
    std::string lexical_match;
    bool is_natural_language = false;
};
