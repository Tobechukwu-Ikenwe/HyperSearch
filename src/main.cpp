#include "EmbeddingModel.hpp"

#include <iostream>
#include <string>


int main()
{
    // Load the tokenizer and ONNX embedding model.
    EmbeddingModel model(
        "third_party/models/all-MiniLM-L6-v2/tokenizer.json"
    );

    std::cout << "HyperSearch started.\n";

    return 0;
}
