#include "SemanticSearch.hpp"
#include "Embedding.hpp"

#include <utility>

SemanticSearch::SemanticSearch(
    EmbeddingPipeline& embedding_pipeline,
    VectorStore& vector_store
)
    : embedding_pipeline_(embedding_pipeline),
      vector_store_(vector_store)
{}

std::vector<SearchResult> SemanticSearch::search(
    const SearchQuery& query,
    std::size_t limit
)
{
    if (limit == 0 || query.embedding_text.empty()) {
        return {};
    }

    // The pipeline handles the configured embedding backend and validation.
    std::vector<float> values =
        embedding_pipeline_.embed_query(query.embedding_text);

    if (values.empty()) {
        return {};
    }

    Embedding query_embedding(std::move(values));

    // VectorStore returns the highest-similarity matches first.
    return vector_store_.search_top_k(query_embedding, limit);
}
