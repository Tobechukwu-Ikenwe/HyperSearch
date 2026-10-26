#pragma once

#include "CodeChunk.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct sqlite3;

// A code chunk returned by lexical retrieval, with its SQLite BM25 score.
struct ChunkSearchResult {
    CodeChunk chunk;
    double bm25_score;
};

class ChunkStore {
public:
    explicit ChunkStore(const std::filesystem::path& database_path);
    ~ChunkStore();

    ChunkStore(const ChunkStore&) = delete;
    ChunkStore& operator=(const ChunkStore&) = delete;

    void initialize();
    void remove_missing_files(const std::vector<std::filesystem::path>& current_files);
    void save_chunks(const std::vector<CodeChunk>& chunks);
    void replace_file_chunks(
        const std::filesystem::path& relative_path,
        const std::vector<CodeChunk>& chunks
    );
    std::vector<ChunkSearchResult> search_candidates(const std::string& query, std::size_t limit = 20);

private:
    sqlite3* db_ = nullptr;
};
