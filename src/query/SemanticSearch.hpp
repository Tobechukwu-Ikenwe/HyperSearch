#ifndef SEMANTIC_SEARCH_HPP
#define SEMANTIC_SEARCH_HPP

#include "EmbeddingPipeline.h"
#include "QueryAnalyzer.h"
#include "VectorStore.hpp"

#include <cstddef>
#include <vector>

// Retrieves code chunks by semantic similarity to an analyzed query.
class SemanticSearch {
public:
    SemanticSearch(
        EmbeddingPipeline& embedding_pipeline,
        VectorStore& vector_store
    );

    std::vector<SearchResult> search(
        const SearchQuery& query,
        std::size_t limit = 20
    );

private:
    EmbeddingPipeline& embedding_pipeline_;
    VectorStore& vector_store_;
};

#endif
