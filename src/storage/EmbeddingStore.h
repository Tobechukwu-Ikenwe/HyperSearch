#pragma once

#include "CodeChunk.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

class EmbeddingStore {
public:
    explicit EmbeddingStore(const std::filesystem::path& database_path);
    ~EmbeddingStore();

    EmbeddingStore(const EmbeddingStore&) = delete;
    EmbeddingStore& operator=(const EmbeddingStore&) = delete;

    void initialize();

    void save(
        const CodeChunk& chunk,
        const std::string& model_id,
        const std::vector<float>& embedding
    );

    std::optional<std::vector<float>> load(
        const CodeChunk& chunk,
        const std::string& model_id
    );

private:
    sqlite3* db_ = nullptr;
};
