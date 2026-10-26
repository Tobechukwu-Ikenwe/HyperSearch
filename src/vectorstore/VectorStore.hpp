#ifndef VECTOR_STORE_HPP
#define VECTOR_STORE_HPP

#include "Embedding.hpp"

#include <cstddef>
#include <string>
#include <vector>

// A document, its embedding, and its original source location.
struct StoredDocument {
    std::string text;
    Embedding embedding;
    std::string file_path;
    std::size_t start_line = 0;
    std::size_t end_line = 0;
};

// A document returned from a similarity search,
// including the location needed to find it in the codebase.
struct SearchResult {
    std::string text;
    float score;
    std::string file_path;
    std::size_t start_line = 0;
    std::size_t end_line = 0;
};

class VectorStore {
private:
    // Each stored embedding remains associated with its source metadata.
    std::vector<StoredDocument> documents;

public:
    // Add a document and its embedding.
    void add(
        const std::string& text,
        const Embedding& embedding
    );

    // Add a document with its original source location.
    void add(
        const std::string& text,
        const Embedding& embedding,
        const std::string& file_path,
        std::size_t start_line,
        std::size_t end_line
    );

    // Remove all in-memory documents before rebuilding the search index.
    void clear();

    // Remove all indexed chunks belonging to one repository-relative file path.
    void remove_file(const std::string& file_path);

    // Replace one file's indexed chunks while preserving every other file.
    void replace_file(
        const std::string& file_path,
        const std::vector<StoredDocument>& replacements
    );

    // Return the number of stored documents.
    std::size_t size() const;

    // Find the single most similar document.
    SearchResult search(const Embedding& query);

    // Find the k most similar documents.
    std::vector<SearchResult> search_top_k(
        const Embedding& query,
        std::size_t k
    );
};

#endif
