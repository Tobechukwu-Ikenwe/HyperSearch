#include "QueryAnalyzer.h"

#include <cassert>
#include <string>

int main() {
    // Whitespace-only input should not trigger a search.
    const QueryAnalysis blank = analyze_query(" \t\n ");
    assert(blank.empty);
    assert(blank.lexical_query.empty());
    assert(blank.original_query == " \t\n ");

    // Leading and trailing whitespace should be removed.
    const QueryAnalysis trimmed = analyze_query("  ChunkStore  ");
    assert(!trimmed.empty);
    assert(trimmed.lexical_query == "ChunkStore");
    assert(trimmed.original_query == "  ChunkStore  ");

    // Ordinary queries should retain their words and spacing.
    const QueryAnalysis ordinary = analyze_query("open database");
    assert(!ordinary.empty);
    assert(ordinary.lexical_query == "open database");

    return 0;
}
