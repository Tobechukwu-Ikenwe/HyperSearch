#include "EmbeddingModel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr std::size_t max_sequence_length = 256;
constexpr std::size_t embedding_size = 384;
constexpr std::int32_t cls_token_id = 101;
constexpr std::int32_t sep_token_id = 102;
constexpr std::int64_t pad_token_id = 0;
}

EmbeddingModel::EmbeddingModel(
    const std::string& tokenizer_path,
    const std::string& model_path
)
    : env(ORT_LOGGING_LEVEL_WARNING, "HyperSearch")
{
    std::ifstream file(tokenizer_path);
    if (!file.is_open()) {
        throw std::runtime_error(
            "Failed to open tokenizer file: " + tokenizer_path
        );
    }

    std::ostringstream output;
    output << file.rdbuf();

    tokenizer = tokenizers::Tokenizer::FromBlobJSON(output.str());
    if (!tokenizer) {
        throw std::runtime_error(
            "Failed to create tokenizer from: " + tokenizer_path
        );
    }

    Ort::SessionOptions options;
    session = std::make_unique<Ort::Session>(
        env, model_path.c_str(), options
    );
}

Embedding EmbeddingModel::embed(const std::string& text)
{
    auto results = embed_batch({text});
    return std::move(results.front());
}

std::vector<Embedding> EmbeddingModel::embed_batch(
    const std::vector<std::string>& texts
)
{
    if (texts.empty()) {
        return {};
    }

    const std::size_t batch_size = texts.size();
    const std::size_t max_text_tokens = max_sequence_length - 2;

    // Tokenize each text and add the model's classification and separator tokens.
    std::vector<std::vector<std::int64_t>> encoded;
    encoded.reserve(batch_size);

    std::size_t sequence_length = 0;

    for (const auto& text : texts) {
        auto token_ids = tokenizer->Encode(text);

        if (token_ids.size() > max_text_tokens) {
            token_ids.resize(max_text_tokens);
        }

        std::vector<std::int64_t> ids;
        ids.reserve(token_ids.size() + 2);
        ids.push_back(cls_token_id);

        for (const auto id : token_ids) {
            ids.push_back(static_cast<std::int64_t>(id));
        }

        ids.push_back(sep_token_id);
        sequence_length = std::max(sequence_length, ids.size());
        encoded.push_back(std::move(ids));
    }

    // Pad all sequences to the longest sequence in this batch.
    // The attention mask marks real tokens as 1 and padding as 0.
    const std::size_t tensor_size = batch_size * sequence_length;

    std::vector<std::int64_t> input_ids(
        tensor_size, pad_token_id
    );
    std::vector<std::int64_t> attention_mask(
        tensor_size, 0
    );
    std::vector<std::int64_t> token_type_ids(
        tensor_size, 0
    );

    for (std::size_t row = 0; row < batch_size; ++row) {
        const std::size_t offset = row * sequence_length;

        for (std::size_t col = 0; col < encoded[row].size(); ++col) {
            input_ids[offset + col] = encoded[row][col];
            attention_mask[offset + col] = 1;
        }
    }

    const std::array<std::int64_t, 2> shape = {
        static_cast<std::int64_t>(batch_size),
        static_cast<std::int64_t>(sequence_length)
    };

    auto memory_info = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault
    );

    auto ids_tensor = Ort::Value::CreateTensor<std::int64_t>(
        memory_info, input_ids.data(), input_ids.size(),
        shape.data(), shape.size()
    );

    auto mask_tensor = Ort::Value::CreateTensor<std::int64_t>(
        memory_info, attention_mask.data(), attention_mask.size(),
        shape.data(), shape.size()
    );

    auto types_tensor = Ort::Value::CreateTensor<std::int64_t>(
        memory_info, token_type_ids.data(), token_type_ids.size(),
        shape.data(), shape.size()
    );

    const char* input_names[] = {
        "input_ids", "attention_mask", "token_type_ids"
    };
    const char* output_names[] = {"last_hidden_state"};

    Ort::Value inputs[] = {
        std::move(ids_tensor),
        std::move(mask_tensor),
        std::move(types_tensor)
    };

    auto outputs = session->Run(
        Ort::RunOptions{nullptr},
        input_names, inputs, 3,
        output_names, 1
    );

    if (outputs.empty() || !outputs[0].IsTensor()) {
        throw std::runtime_error(
            "ONNX model did not return a hidden-state tensor"
        );
    }

    const auto output_info = outputs[0].GetTensorTypeAndShapeInfo();
    const auto output_shape = output_info.GetShape();

    if (output_shape.size() != 3 ||
        output_shape[0] != static_cast<std::int64_t>(batch_size) ||
        output_shape[1] != static_cast<std::int64_t>(sequence_length) ||
        output_shape[2] != static_cast<std::int64_t>(embedding_size)) {
        throw std::runtime_error(
            "Unexpected ONNX output shape; expected [batch, sequence, 384]"
        );
    }

    const float* hidden = outputs[0].GetTensorData<float>();
    std::vector<Embedding> results;
    results.reserve(batch_size);

    // Mean-pool only real tokens, then normalize each sentence vector.
    for (std::size_t row = 0; row < batch_size; ++row) {
        std::vector<float> embedding(embedding_size, 0.0f);
        std::size_t token_count = 0;
        const std::size_t row_offset = row * sequence_length;

        for (std::size_t token = 0; token < sequence_length; ++token) {
            if (attention_mask[row_offset + token] == 0) {
                continue;
            }

            const std::size_t hidden_offset =
                (row * sequence_length + token) * embedding_size;

            for (std::size_t dim = 0; dim < embedding_size; ++dim) {
                embedding[dim] += hidden[hidden_offset + dim];
            }
            ++token_count;
        }

        if (token_count == 0) {
            throw std::runtime_error(
                "Document produced zero tokens"
            );
        }

        const float divisor = static_cast<float>(token_count);
        float norm_squared = 0.0f;

        for (float& value : embedding) {
            value /= divisor;
            norm_squared += value * value;
        }

        const float norm = std::sqrt(norm_squared);
        if (!std::isfinite(norm) || norm <= 0.0f) {
            throw std::runtime_error(
                "Generated embedding has invalid magnitude"
            );
        }

        for (float& value : embedding) {
            value /= norm;
        }

        results.emplace_back(std::move(embedding));
    }

    return results;
}
