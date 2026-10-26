#include "QueryAnalyzer.h"

#include <algorithm>
#include <cctype>

QueryAnalysis analyze_query(const std::string& query) {
    QueryAnalysis result;
    result.original_query = query;

    const auto is_not_space = [](unsigned char character) {
        return std::isspace(character) == 0;
    };

    const auto first = std::find_if(
        query.begin(),
        query.end(),
        is_not_space
    );

    const auto last = std::find_if(
        query.rbegin(),
        query.rend(),
        is_not_space
    ).base();

    if (first >= last) {
        return result;
    }

    result.lexical_query = std::string(first, last);
    result.empty = false;

    return result;
}

std::vector<std::string> descriptive_terms(const QueryAnalysis& analysis) {
    std::vector<std::string> result;

    for (const QueryTerm& term : analysis.terms) {
        if (term.kind == QueryTermKind::Word) {
            result.push_back(term.text);
        }
    }

    return result;
}
