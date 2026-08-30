#ifndef EMBEDDING_MODEL_HPP
#define EMBEDDING_MODEL_HPP

#include "Embedding.hpp"

#include <memory>
#include <string>
#include <vector>

#include "tokenizers_cpp.h"
#include <onnxruntime_cxx_api.h>

class EmbeddingModel {

private:
    // Converts text into token IDs for the model.
    std::unique_ptr<tokenizers::Tokenizer> tokenizer;

    // Manages the ONNX Runtime environment.
    Ort::Env env;

    // Holds the loaded ONNX model.
    std::unique_ptr<Ort::Session> session;

public:
    // Loads the tokenizer and embedding model.
    EmbeddingModel(const std::string& tokenizer_path);

    // Converts one text into an embedding.
    Embedding embed(const std::string& text);

    // Converts multiple texts into embeddings.
    std::vector<Embedding> embed_batch(
        const std::vector<std::string>& texts
    );
};

#endif
