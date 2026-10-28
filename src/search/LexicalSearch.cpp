#include "LexicalSearch.hpp"

#include <utility>

LexicalSearch::LexicalSearch(ChunkStore& chunk_store)
    : chunk_store_(chunk_store)
{}

std::vector<SearchResult> LexicalSearch::search(
    const SearchQuery& query,
    std::size_t limit)
{
    if (limit == 0 || query.lexical_match.empty()) {
        return {};
    }

    const auto chunks = chunk_store_.search_candidates(
        query.lexical_match,
        limit
    );

    std::vector<SearchResult> results;
    results.reserve(chunks.size());

    for (const auto& match : chunks) {
        const auto& chunk = match.chunk;

        results.push_back(SearchResult{
            chunk.content,
            static_cast<float>(-match.bm25_score),
            chunk.source.relative_path.generic_string(),
            chunk.start_line,
            chunk.end_line
        });
    }

    return results;
}
