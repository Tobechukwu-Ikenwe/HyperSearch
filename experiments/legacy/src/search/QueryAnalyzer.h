#pragma once

#include <string>
#include <vector>

enum class QueryTermKind {
    Word,
    Identifier
};

struct QueryTerm {
    std::string text;
    QueryTermKind kind;
};

struct QueryAnalysis {
    // Exact text supplied by the user.
    std::string original_query;

    // FTS5 expression prepared for keyword-based retrieval.
    std::string lexical_query;

    // Natural-language text intended for semantic retrieval.
    std::string semantic_query;

    // Extracted terms, classified as words or code identifiers.
    std::vector<QueryTerm> terms;

    // True when at least one term looks like a code identifier.
    bool has_identifiers = false;

    // True when the original input contains only whitespace.
    bool empty = true;
};

// Analyzes user input without accessing the database or an AI model.
QueryAnalysis analyze_query(const std::string& query);

// Returns descriptive words for candidate scoring, excluding identifiers.
std::vector<std::string> descriptive_terms(const QueryAnalysis& analysis);
