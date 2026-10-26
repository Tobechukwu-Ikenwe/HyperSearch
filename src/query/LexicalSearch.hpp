#ifndef LEXICAL_SEARCH_HPP
#define LEXICAL_SEARCH_HPP

#include "ChunkStore.h"
#include "QueryAnalyzer.h"
#include "VectorStore.hpp"

#include <cstddef>
#include <vector>

// Executes the lexical expression produced by QueryAnalyzer
// and returns matching chunks in the order ranked by SQLite FTS5.
class LexicalSearch {
public:
    explicit LexicalSearch(ChunkStore& chunk_store);

    std::vector<SearchResult> search(
        const SearchQuery& query,
        std::size_t limit = 20
    );

private:
    ChunkStore& chunk_store_;
};

#endif
