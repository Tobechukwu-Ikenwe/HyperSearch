#include "EmbeddingStore.h"

#include <bit>
#include <cstdint>
#include <limits>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

static_assert(sizeof(float) == 4);
static_assert(std::numeric_limits<float>::is_iec559);

void check_sqlite(int result, sqlite3* db, const char* operation) {
    if (result != SQLITE_OK) {
        throw std::runtime_error(
            std::string(operation) + ": " + sqlite3_errmsg(db)
        );
    }
}

std::string chunk_path(const CodeChunk& chunk) {
    return chunk.source.relative_path.generic_string();
}

// Store IEEE-754 float32 values in a defined little-endian representation.
std::vector<unsigned char> encode_embedding(
    const std::vector<float>& embedding
) {
    std::vector<unsigned char> bytes;
    bytes.reserve(embedding.size() * 4);

    for (float value : embedding) {
        if (!std::numeric_limits<float>::is_iec559) {
            throw std::runtime_error("IEEE-754 floats are required");
        }

        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);

        bytes.push_back(static_cast<unsigned char>(bits));
        bytes.push_back(static_cast<unsigned char>(bits >> 8));
        bytes.push_back(static_cast<unsigned char>(bits >> 16));
        bytes.push_back(static_cast<unsigned char>(bits >> 24));
    }

    return bytes;
}

std::vector<float> decode_embedding(
    const unsigned char* bytes,
    std::size_t byte_count,
    std::size_t dimension
) {
    if (dimension > std::numeric_limits<std::size_t>::max() / 4 ||
        byte_count != dimension * 4 ||
        (byte_count != 0 && bytes == nullptr)) {
        throw std::runtime_error("Invalid cached embedding size");
    }

    std::vector<float> embedding;
    embedding.reserve(dimension);

    for (std::size_t i = 0; i < dimension; ++i) {
        const std::size_t offset = i * 4;

        const std::uint32_t bits =
            static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);

        embedding.push_back(std::bit_cast<float>(bits));
    }

    return embedding;
}

} // namespace

EmbeddingStore::EmbeddingStore(
    const std::filesystem::path& database_path
) {
    const int result = sqlite3_open(database_path.c_str(), &db_);

    if (result != SQLITE_OK) {
        const std::string message =
            db_ ? sqlite3_errmsg(db_) : "Could not allocate SQLite connection";

        if (db_) {
            sqlite3_close(db_);
        }

        db_ = nullptr;
        throw std::runtime_error(
            "Opening embedding database failed: " + message
        );
    }

    char* pragma_error = nullptr;
    const int pragma_result = sqlite3_exec(
        db_, "PRAGMA foreign_keys = ON", nullptr, nullptr, &pragma_error
    );
    if (pragma_result != SQLITE_OK) {
        const std::string message =
            pragma_error ? pragma_error : sqlite3_errmsg(db_);
        sqlite3_free(pragma_error);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error(
            "Enabling SQLite foreign keys failed: " + message
        );
    }
}

EmbeddingStore::~EmbeddingStore() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void EmbeddingStore::initialize() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS chunk_embeddings (
            chunk_id INTEGER NOT NULL,
            model_id TEXT NOT NULL,
            dimension INTEGER NOT NULL CHECK (dimension > 0),
            embedding BLOB NOT NULL,
            PRIMARY KEY (chunk_id, model_id),
            FOREIGN KEY (chunk_id) REFERENCES chunks(id)
        );

        CREATE TRIGGER IF NOT EXISTS chunks_embedding_invalidate
        AFTER UPDATE OF content ON chunks
        WHEN OLD.content IS NOT NEW.content
        BEGIN
            DELETE FROM chunk_embeddings
            WHERE chunk_id = OLD.id;
        END;

        CREATE TRIGGER IF NOT EXISTS chunks_embedding_delete
        AFTER DELETE ON chunks
        BEGIN
            DELETE FROM chunk_embeddings
            WHERE chunk_id = OLD.id;
        END;
    )";

    char* error_message = nullptr;
    const int result = sqlite3_exec(
        db_, sql, nullptr, nullptr, &error_message
    );

    if (result != SQLITE_OK) {
        const std::string message =
            error_message ? error_message : sqlite3_errmsg(db_);
        sqlite3_free(error_message);
        throw std::runtime_error(
            "Initializing embedding cache failed: " + message
        );
    }
}

void EmbeddingStore::save(
    const CodeChunk& chunk,
    const std::string& model_id,
    const std::vector<float>& embedding
) {
    if (model_id.empty()) {
        throw std::invalid_argument("Embedding model ID cannot be empty");
    }

    if (embedding.empty()) {
        throw std::invalid_argument("Embedding cannot be empty");
    }

    if (embedding.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("Embedding dimension is too large");
    }

    const auto bytes = encode_embedding(embedding);
    const std::string path = chunk_path(chunk);

    // Resolve the stable database chunk ID from its source location.
    sqlite3_stmt* lookup = nullptr;
    check_sqlite(
        sqlite3_prepare_v2(
            db_,
            "SELECT id FROM chunks "
            "WHERE file_path = ? AND start_line = ? AND end_line = ?",
            -1, &lookup, nullptr
        ),
        db_, "Preparing chunk lookup"
    );

    sqlite3_int64 chunk_id = 0;

    try {
        check_sqlite(
            sqlite3_bind_text(
                lookup, 1, path.c_str(), -1, SQLITE_TRANSIENT
            ),
            db_, "Binding chunk path"
        );
        check_sqlite(
            sqlite3_bind_int64(
                lookup, 2, static_cast<sqlite3_int64>(chunk.start_line)
            ),
            db_, "Binding chunk start line"
        );
        check_sqlite(
            sqlite3_bind_int64(
                lookup, 3, static_cast<sqlite3_int64>(chunk.end_line)
            ),
            db_, "Binding chunk end line"
        );

        const int result = sqlite3_step(lookup);

        if (result == SQLITE_ROW) {
            chunk_id = sqlite3_column_int64(lookup, 0);
        } else if (result == SQLITE_DONE) {
            throw std::runtime_error(
                "Cannot cache embedding: chunk is not stored in SQLite"
            );
        } else {
            throw std::runtime_error(
                std::string("Chunk lookup failed: ") + sqlite3_errmsg(db_)
            );
        }
    } catch (...) {
        sqlite3_finalize(lookup);
        throw;
    }

    sqlite3_finalize(lookup);

    const char* sql = R"(
        INSERT INTO chunk_embeddings
            (chunk_id, model_id, dimension, embedding)
        VALUES (?, ?, ?, ?)
        ON CONFLICT(chunk_id, model_id) DO UPDATE SET
            dimension = excluded.dimension,
            embedding = excluded.embedding
    )";

    sqlite3_stmt* statement = nullptr;
    check_sqlite(
        sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr),
        db_, "Preparing embedding insert"
    );

    try {
        check_sqlite(
            sqlite3_bind_int64(statement, 1, chunk_id),
            db_, "Binding chunk ID"
        );
        check_sqlite(
            sqlite3_bind_text(
                statement, 2, model_id.c_str(), -1, SQLITE_TRANSIENT
            ),
            db_, "Binding model ID"
        );
        check_sqlite(
            sqlite3_bind_int64(
                statement, 3, static_cast<sqlite3_int64>(embedding.size())
            ),
            db_, "Binding embedding dimension"
        );
        check_sqlite(
            sqlite3_bind_blob(
                statement, 4, bytes.data(),
                static_cast<int>(bytes.size()), SQLITE_TRANSIENT
            ),
            db_, "Binding embedding data"
        );

        const int result = sqlite3_step(statement);
        if (result != SQLITE_DONE) {
            throw std::runtime_error(
                std::string("Saving embedding failed: ") + sqlite3_errmsg(db_)
            );
        }
    } catch (...) {
        sqlite3_finalize(statement);
        throw;
    }

    sqlite3_finalize(statement);
}

std::optional<std::vector<float>> EmbeddingStore::load(
    const CodeChunk& chunk,
    const std::string& model_id
) {
    if (model_id.empty()) {
        throw std::invalid_argument("Embedding model ID cannot be empty");
    }

    const std::string path = chunk_path(chunk);

    const char* sql = R"(
        SELECT e.dimension, e.embedding
        FROM chunk_embeddings AS e
        JOIN chunks AS c ON c.id = e.chunk_id
        WHERE c.file_path = ?
          AND c.start_line = ?
          AND c.end_line = ?
          AND e.model_id = ?
    )";

    sqlite3_stmt* statement = nullptr;
    check_sqlite(
        sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr),
        db_, "Preparing embedding lookup"
    );

    std::optional<std::vector<float>> result_embedding;

    try {
        check_sqlite(
            sqlite3_bind_text(
                statement, 1, path.c_str(), -1, SQLITE_TRANSIENT
            ),
            db_, "Binding chunk path"
        );
        check_sqlite(
            sqlite3_bind_int64(
                statement, 2, static_cast<sqlite3_int64>(chunk.start_line)
            ),
            db_, "Binding chunk start line"
        );
        check_sqlite(
            sqlite3_bind_int64(
                statement, 3, static_cast<sqlite3_int64>(chunk.end_line)
            ),
            db_, "Binding chunk end line"
        );
        check_sqlite(
            sqlite3_bind_text(
                statement, 4, model_id.c_str(), -1, SQLITE_TRANSIENT
            ),
            db_, "Binding model ID"
        );

        const int step_result = sqlite3_step(statement);

        if (step_result == SQLITE_ROW) {
            const sqlite3_int64 stored_dimension =
                sqlite3_column_int64(statement, 0);

            if (stored_dimension <= 0) {
                throw std::runtime_error(
                    "Cached embedding has an invalid dimension"
                );
            }

            const int blob_size = sqlite3_column_bytes(statement, 1);
            const auto* blob = static_cast<const unsigned char*>(
                sqlite3_column_blob(statement, 1)
            );

            result_embedding = decode_embedding(
                blob,
                static_cast<std::size_t>(blob_size),
                static_cast<std::size_t>(stored_dimension)
            );
        } else if (step_result != SQLITE_DONE) {
            throw std::runtime_error(
                std::string("Loading embedding failed: ") + sqlite3_errmsg(db_)
            );
        }
    } catch (...) {
        sqlite3_finalize(statement);
        throw;
    }

    sqlite3_finalize(statement);
    return result_embedding;
}
