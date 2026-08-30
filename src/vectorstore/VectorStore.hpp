#ifndef VECTOR_STORE_HPP
#define VECTOR_STORE_HPP

#include "Embedding.hpp"

#include <cstddef>
#include <string>
#include <vector>


// A document and its embedding stored together.
struct StoredDocument {
    std::string text;
    Embedding embedding;
};


// A document returned from a similarity search.
struct SearchResult {
    std::string text;
    float score;
};


class VectorStore {

private:

    // All documents stored in the vector database.
    std::vector<StoredDocument> documents;


public:

    // Add a document and its embedding.
    void add(
        const std::string& text,
        const Embedding& embedding
    );

    // Return the number of stored documents.
    std::size_t size() const;

    // Find the single most similar document.
    SearchResult search(
        const Embedding& query
    );

    // Find the k most similar documents.
    std::vector<SearchResult> search_top_k(
        const Embedding& query,
        std::size_t k
    );
};

#endif
