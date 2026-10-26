#ifndef EMBEDDING_MODEL_HPP
#define EMBEDDING_MODEL_HPP

#include "Embedding.hpp"
#include "tokenizers_cpp.h"
#include <onnxruntime_cxx_api.h>

#include <memory>
#include <string>
#include <vector>

class EmbeddingModel {
private:
    std::unique_ptr<tokenizers::Tokenizer> tokenizer;
    Ort::Env env;
    std::unique_ptr<Ort::Session> session;

public:
    // Load a tokenizer and its corresponding ONNX model.
    explicit EmbeddingModel(
        const std::string& tokenizer_path,
        const std::string& model_path =
            "third_party/models/all-MiniLM-L6-v2/onnx/model.onnx"
    );

    // Embed one text using the same implementation as batch inference.
    Embedding embed(const std::string& text);

    // Embed multiple texts in one ONNX Runtime inference call.
    std::vector<Embedding> embed_batch(
        const std::vector<std::string>& texts
    );
};

#endif
