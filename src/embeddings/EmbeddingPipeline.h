#pragma once

#include "CodeChunk.h"
#include "EmbeddingModel.hpp"
#include "EmbeddingStore.h"
#include "EmbeddingWorker.h"
#include "VectorStore.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// Coordinates embedding generation, validation, and persistent caching.
// Native ONNX is the default; Python remains available as an alternative.
class EmbeddingPipeline {
public:
    explicit EmbeddingPipeline(EmbeddingStore& store);
    EmbeddingPipeline(EmbeddingStore& store, EmbeddingModel& model);
    EmbeddingPipeline(EmbeddingStore& store, EmbeddingWorker& worker);

    void initialize();

    std::vector<std::vector<float>> embed_chunks(
        const std::vector<CodeChunk>& chunks
    );

    // Cache embeddings as needed and populate the in-memory search index.
    void index_chunks(
        const std::vector<CodeChunk>& chunks,
        VectorStore& vector_store
    );

    std::vector<float> embed_query(const std::string& query);

private:
    enum class Backend {
        Onnx,
        Python
    };

    void validate_embedding(const std::vector<float>& embedding) const;

    EmbeddingStore& store_;
    Backend backend_;
    std::unique_ptr<EmbeddingModel> owned_model_;
    EmbeddingModel* model_ = nullptr;
    EmbeddingWorker* worker_ = nullptr;

    std::string model_id_;
    std::size_t dimension_ = 0;
    bool initialized_ = false;
};
