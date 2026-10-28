#include "EmbeddingPipeline.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

// Keep cache identities distinct across backends and preprocessing versions.
constexpr const char* onnx_model_id =
    "all-MiniLM-L6-v2:onnx:mean-pooling-v1";
constexpr const char* python_model_id =
    "all-MiniLM-L6-v2:python:worker-defined";

constexpr std::size_t onnx_embedding_dimension = 384;

std::filesystem::path model_directory() {
    return std::filesystem::read_symlink("/proc/self/exe").parent_path() /
           "models" / "all-MiniLM-L6-v2";
}

} // namespace

EmbeddingPipeline::EmbeddingPipeline(EmbeddingStore& store)
    : store_(store),
      backend_(Backend::Onnx)
{
    const auto directory = model_directory();
    owned_model_ = std::make_unique<EmbeddingModel>(
        (directory / "tokenizer.json").string(),
        (directory / "onnx" / "model.onnx").string()
    );
    model_ = owned_model_.get();
}

EmbeddingPipeline::EmbeddingPipeline(
    EmbeddingStore& store,
    EmbeddingModel& model
)
    : store_(store),
      backend_(Backend::Onnx),
      model_(&model)
{}

EmbeddingPipeline::EmbeddingPipeline(
    EmbeddingStore& store,
    EmbeddingWorker& worker
)
    : store_(store),
      backend_(Backend::Python),
      worker_(&worker)
{}

void EmbeddingPipeline::initialize()
{
    if (backend_ == Backend::Onnx) {
        if (model_ == nullptr) {
            throw std::logic_error("ONNX embedding model is not configured");
        }

        model_id_ = onnx_model_id;
        dimension_ = onnx_embedding_dimension;
    } else {
        if (worker_ == nullptr) {
            throw std::logic_error("Python embedding worker is not configured");
        }

        const auto metadata = worker_->metadata();

        if (metadata.model_id.empty()) {
            throw std::runtime_error(
                "Embedding worker returned an empty model ID"
            );
        }

        if (metadata.dimension == 0) {
            throw std::runtime_error(
                "Embedding worker returned an invalid vector dimension"
            );
        }

        // Include the worker's actual model identity in the cache key.
        model_id_ = std::string(python_model_id) + ":" + metadata.model_id;
        dimension_ = metadata.dimension;
    }

    initialized_ = true;
}

void EmbeddingPipeline::validate_embedding(
    const std::vector<float>& embedding
) const
{
    if (embedding.size() != dimension_) {
        throw std::runtime_error(
            "Embedding dimension mismatch: expected " +
            std::to_string(dimension_) + ", received " +
            std::to_string(embedding.size())
        );
    }

    for (const float value : embedding) {
        if (!std::isfinite(value)) {
            throw std::runtime_error(
                "Embedding contains a non-finite value"
            );
        }
    }
}

std::vector<std::vector<float>> EmbeddingPipeline::embed_chunks(
    const std::vector<CodeChunk>& chunks
)
{
    if (!initialized_) {
        throw std::logic_error(
            "EmbeddingPipeline::initialize() must be called first"
        );
    }

    std::vector<std::vector<float>> results(chunks.size());
    std::vector<std::size_t> missing_indices;
    std::vector<std::string> missing_texts;

    for (std::size_t i = 0; i < chunks.size(); ++i) {
        auto cached = store_.load(chunks[i], model_id_);

        if (cached) {
            validate_embedding(*cached);
            results[i] = std::move(*cached);
        } else {
            missing_indices.push_back(i);
            missing_texts.push_back(chunks[i].content);
        }
    }

    if (missing_texts.empty()) {
        return results;
    }

    std::vector<std::vector<float>> generated;
    generated.reserve(missing_texts.size());

    if (backend_ == Backend::Onnx) {
        constexpr std::size_t batch_size = 32;
        for (std::size_t start = 0; start < missing_texts.size();
             start += batch_size) {
            const std::size_t end =
                std::min(start + batch_size, missing_texts.size());
            const std::vector<std::string> inputs(
                missing_texts.begin() + start, missing_texts.begin() + end);
            auto batch = model_->embed_batch(inputs);
            if (batch.size() != inputs.size()) {
                throw std::runtime_error(
                    "ONNX model returned a different number of vectors "
                    "than requested"
                );
            }
            for (const auto& embedding : batch) {
                generated.push_back(embedding.data());
            }
        }
    } else {
        generated = worker_->embed_documents(missing_texts);
    }

    if (generated.size() != missing_indices.size()) {
        throw std::runtime_error(
            "Embedding backend returned a different number of vectors "
            "than requested"
        );
    }

    for (std::size_t i = 0; i < generated.size(); ++i) {
        validate_embedding(generated[i]);

        const std::size_t chunk_index = missing_indices[i];
        store_.save(chunks[chunk_index], model_id_, generated[i]);
        results[chunk_index] = std::move(generated[i]);
    }

    return results;
}


void EmbeddingPipeline::index_chunks(
    const std::vector<CodeChunk>& chunks,
    VectorStore& vector_store
)
{
    // Reuse cached vectors and generate only missing embeddings.
    const auto embeddings = embed_chunks(chunks);

    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const CodeChunk& chunk = chunks[i];
        const Embedding embedding(embeddings[i]);

        vector_store.add(
            chunk.content,
            embedding,
            chunk.source.relative_path.generic_string(),
            chunk.start_line,
            chunk.end_line
        );
    }
}

std::vector<float> EmbeddingPipeline::embed_query(
    const std::string& query
)
{
    if (!initialized_) {
        throw std::logic_error(
            "EmbeddingPipeline::initialize() must be called first"
        );
    }

    std::vector<float> embedding;

    if (backend_ == Backend::Onnx) {
        embedding = model_->embed(query).data();
    } else {
        embedding = worker_->embed_query(query);
    }

    validate_embedding(embedding);
    return embedding;
}
