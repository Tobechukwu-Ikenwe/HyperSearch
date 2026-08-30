#include "EmbeddingModel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>


// Load the tokenizer and ONNX model.
EmbeddingModel::EmbeddingModel(
    const std::string& tokenizer_path
)
    : env(ORT_LOGGING_LEVEL_WARNING, "HyperSearch")
{
    // Read the tokenizer JSON file.
    std::ifstream file(tokenizer_path);

    if (!file.is_open()) {
        throw std::runtime_error(
            "Failed to open tokenizer file: " + tokenizer_path
        );
    }

    std::ostringstream output;
    output << file.rdbuf();

    std::string json_blob = output.str();

    // Create the tokenizer from the JSON data.
    tokenizer =
        tokenizers::Tokenizer::FromBlobJSON(json_blob);


    // Load the ONNX model into an inference session.
    Ort::SessionOptions session_options;

    session = std::make_unique<Ort::Session>(
        env,
        "third_party/models/all-MiniLM-L6-v2/onnx/model.onnx",
        session_options
    );
}


// Convert one text into a normalized embedding.
Embedding EmbeddingModel::embed(
    const std::string& text
)
{
    // Convert text into integer token IDs.
    std::vector<int32_t> token_ids =
        tokenizer->Encode(text);

    // ONNX Runtime expects 64-bit token IDs.
    std::vector<int64_t> input_ids(
        token_ids.begin(),
        token_ids.end()
    );


    // Mark real tokens as 1 and padding as 0.
    std::vector<int64_t> attention_mask;

    for (int64_t id : input_ids) {
        attention_mask.push_back(
            id == 0 ? 0 : 1
        );
    }


    // Single sentence → every token belongs to segment 0.
    std::vector<int64_t> token_type_ids(
        input_ids.size(),
        0
    );


    // Tensor shape: [batch_size, sequence_length].
    std::array<int64_t, 2> input_shape = {
        1,
        static_cast<int64_t>(input_ids.size())
    };


    // Describe the CPU memory holding our input vectors.
    Ort::MemoryInfo memory_info =
        Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator,
            OrtMemTypeDefault
        );


    // Wrap the vectors as ONNX Runtime tensors.
    auto input_ids_tensor =
        Ort::Value::CreateTensor<int64_t>(
            memory_info,
            input_ids.data(),
            input_ids.size(),
            input_shape.data(),
            input_shape.size()
        );

    auto attention_mask_tensor =
        Ort::Value::CreateTensor<int64_t>(
            memory_info,
            attention_mask.data(),
            attention_mask.size(),
            input_shape.data(),
            input_shape.size()
        );

    auto token_type_ids_tensor =
        Ort::Value::CreateTensor<int64_t>(
            memory_info,
            token_type_ids.data(),
            token_type_ids.size(),
            input_shape.data(),
            input_shape.size()
        );


    // These names must match the ONNX model inputs.
    const char* input_names[] = {
        "input_ids",
        "attention_mask",
        "token_type_ids"
    };

    Ort::Value input_tensors[] = {
        std::move(input_ids_tensor),
        std::move(attention_mask_tensor),
        std::move(token_type_ids_tensor)
    };


    // Request the transformer's final hidden states.
    const char* output_names[] = {
        "last_hidden_state"
    };


    // Run the model.
    auto output_tensors =
        session->Run(
            Ort::RunOptions{nullptr},
            input_names,
            input_tensors,
            3,
            output_names,
            1
        );


    // Access the model's output tensor.
    const auto& output_tensor =
        output_tensors[0];

    const float* output_data =
        output_tensor.GetTensorData<float>();


    // MiniLM-L6-v2 produces 384 values per token.
    constexpr std::size_t embedding_size = 384;

    std::vector<float> embedding(
        embedding_size,
        0.0f
    );


    // Mean-pool the embeddings of real tokens.
    std::size_t token_count = 0;

    for (std::size_t token = 0;
         token < input_ids.size();
         ++token)
    {
        if (attention_mask[token] == 0) {
            continue;
        }

        for (std::size_t dim = 0;
             dim < embedding_size;
             ++dim)
        {
            embedding[dim] +=
                output_data[
                    token * embedding_size + dim
                ];
        }

        ++token_count;
    }


    if (token_count == 0) {
        throw std::runtime_error(
            "Document produced zero tokens"
        );
    }


    // Divide by the number of real tokens.
    for (float& value : embedding) {
        value /=
            static_cast<float>(token_count);
    }


    // Calculate the L2 magnitude of the embedding.
    float norm = 0.0f;

    for (float value : embedding) {
        norm += value * value;
    }

    norm = std::sqrt(norm);


    if (norm == 0.0f) {
        throw std::runtime_error(
            "Generated embedding has zero magnitude"
        );
    }


    // Normalize the vector to unit length.
    for (float& value : embedding) {
        value /= norm;
    }


    // Transfer the vector into the Embedding object.
    return Embedding(
        std::move(embedding)
    );
}
