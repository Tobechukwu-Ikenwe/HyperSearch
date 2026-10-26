#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class EmbeddingWorker {
public:
    struct Metadata {
        std::string model_id;
        std::size_t dimension;
    };

    explicit EmbeddingWorker(
        std::filesystem::path python_executable = ".venv/bin/python",
        std::filesystem::path worker_script = "python/embedding_worker.py"
    );

    ~EmbeddingWorker();

    EmbeddingWorker(const EmbeddingWorker&) = delete;
    EmbeddingWorker& operator=(const EmbeddingWorker&) = delete;

    Metadata metadata();
    std::vector<float> embed_query(const std::string& text);
    std::vector<std::vector<float>> embed_documents(
        const std::vector<std::string>& texts
    );

private:
    void start();
    void stop() noexcept;
    std::string request(const std::string& json);
    std::string make_request_id();

    std::filesystem::path python_executable_;
    std::filesystem::path worker_script_;
    int input_fd_ = -1;
    int output_fd_ = -1;
    int process_id_ = -1;
    std::uint64_t next_request_id_ = 1;
};
