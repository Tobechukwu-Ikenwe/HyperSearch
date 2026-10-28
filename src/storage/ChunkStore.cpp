#include "ChunkStore.h"

#include <sqlite3.h>
#include <stdexcept>
#include <string>

namespace {

void check_sqlite(int result, sqlite3* db, const char* operation) {
    if (result != SQLITE_OK) {
        throw std::runtime_error(
            std::string(operation) + ": " + sqlite3_errmsg(db)
        );
    }
}

const char* category_name(FileCategory category) {
    switch (category) {
        case FileCategory::Source: return "Source";
        case FileCategory::Header: return "Header";
        case FileCategory::Test: return "Test";
        case FileCategory::Documentation: return "Documentation";
        case FileCategory::BuildConfig: return "BuildConfig";
        case FileCategory::Other: return "Other";
    }
    return "Unknown";
}

} // namespace

ChunkStore::ChunkStore(const std::filesystem::path& database_path) {
    const int result = sqlite3_open(database_path.c_str(), &db_);
    if (result != SQLITE_OK) {
        const std::string message =
            db_ ? sqlite3_errmsg(db_) : "Could not allocate SQLite connection";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("Opening chunk database failed: " + message);
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

ChunkStore::~ChunkStore() {
    if (db_) sqlite3_close(db_);
}

void ChunkStore::initialize() {
    const char* schema_sql = R"(
        CREATE TABLE IF NOT EXISTS chunks (
            id INTEGER PRIMARY KEY,
            file_path TEXT NOT NULL,
            category TEXT NOT NULL,
            start_line INTEGER NOT NULL CHECK (start_line >= 1),
            end_line INTEGER NOT NULL CHECK (end_line >= start_line),
            content TEXT NOT NULL,
            UNIQUE(file_path, start_line, end_line)
        );

        CREATE INDEX IF NOT EXISTS idx_chunks_file_path
            ON chunks(file_path);

        CREATE TABLE IF NOT EXISTS file_fingerprints (
            file_path TEXT PRIMARY KEY,
            fingerprint TEXT NOT NULL
        );
    )";

    char* error_message = nullptr;
    int result = sqlite3_exec(
        db_, schema_sql, nullptr, nullptr, &error_message
    );
    if (result != SQLITE_OK) {
        const std::string message =
            error_message ? error_message : sqlite3_errmsg(db_);
        sqlite3_free(error_message);
        throw std::runtime_error("Initializing chunk table failed: " + message);
    }

    // Detect a new FTS index so existing chunks are indexed only once.
    sqlite3_stmt* check_statement = nullptr;
    check_sqlite(
        sqlite3_prepare_v2(
            db_,
            "SELECT 1 FROM sqlite_master WHERE type = 'table' "
            "AND name = 'chunks_fts'",
            -1, &check_statement, nullptr
        ),
        db_, "Checking FTS index"
    );

    const bool fts_exists =
        sqlite3_step(check_statement) == SQLITE_ROW;
    sqlite3_finalize(check_statement);

    const char* fts_sql = R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5(
            content,
            content='chunks',
            content_rowid='id'
        );

        CREATE TRIGGER IF NOT EXISTS chunks_ai AFTER INSERT ON chunks BEGIN
            INSERT INTO chunks_fts(rowid, content)
            VALUES (new.id, new.content);
        END;

        CREATE TRIGGER IF NOT EXISTS chunks_ad AFTER DELETE ON chunks BEGIN
            INSERT INTO chunks_fts(chunks_fts, rowid, content)
            VALUES ('delete', old.id, old.content);
        END;

        CREATE TRIGGER IF NOT EXISTS chunks_au AFTER UPDATE ON chunks BEGIN
            INSERT INTO chunks_fts(chunks_fts, rowid, content)
            VALUES ('delete', old.id, old.content);
            INSERT INTO chunks_fts(rowid, content)
            VALUES (new.id, new.content);
        END;
    )";

    error_message = nullptr;
    result = sqlite3_exec(db_, fts_sql, nullptr, nullptr, &error_message);
    if (result != SQLITE_OK) {
        const std::string message =
            error_message ? error_message : sqlite3_errmsg(db_);
        sqlite3_free(error_message);
        throw std::runtime_error("Initializing FTS index failed: " + message);
    }

    if (!fts_exists) {
        error_message = nullptr;
        result = sqlite3_exec(
            db_, "INSERT INTO chunks_fts(chunks_fts) VALUES ('rebuild')",
            nullptr, nullptr, &error_message
        );
        if (result != SQLITE_OK) {
            const std::string message =
                error_message ? error_message : sqlite3_errmsg(db_);
            sqlite3_free(error_message);
            throw std::runtime_error("Building FTS index failed: " + message);
        }
    }
}

std::vector<CodeChunk> ChunkStore::all_chunks() {
    const char* sql =
        "SELECT file_path, category, start_line, end_line, content "
        "FROM chunks ORDER BY file_path, start_line";
    sqlite3_stmt* statement = nullptr;
    check_sqlite(sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr),
                 db_, "Preparing chunk load");

    std::vector<CodeChunk> chunks;
    try {
        int result;
        while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
            const auto* path = sqlite3_column_text(statement, 0);
            const auto* category = sqlite3_column_text(statement, 1);
            const auto* content = sqlite3_column_text(statement, 4);
            const int content_size = sqlite3_column_bytes(statement, 4);
            const std::string category_text = category
                ? reinterpret_cast<const char*>(category) : "";
            FileCategory file_category = FileCategory::Other;
            if (category_text == "Source") file_category = FileCategory::Source;
            else if (category_text == "Header") file_category = FileCategory::Header;
            else if (category_text == "Test") file_category = FileCategory::Test;
            else if (category_text == "Documentation") file_category = FileCategory::Documentation;
            else if (category_text == "BuildConfig") file_category = FileCategory::BuildConfig;
            chunks.push_back(CodeChunk{
                ScannedFile{
                    std::filesystem::path(path ? reinterpret_cast<const char*>(path) : ""),
                    file_category
                },
                static_cast<std::size_t>(sqlite3_column_int64(statement, 2)),
                static_cast<std::size_t>(sqlite3_column_int64(statement, 3)),
                content && content_size > 0
                    ? std::string(reinterpret_cast<const char*>(content),
                                  static_cast<std::size_t>(content_size))
                    : std::string{}
            });
        }
        if (result != SQLITE_DONE) {
            throw std::runtime_error(std::string("Loading chunks failed: ") +
                                     sqlite3_errmsg(db_));
        }
    } catch (...) {
        sqlite3_finalize(statement);
        throw;
    }
    sqlite3_finalize(statement);
    return chunks;
}

bool ChunkStore::file_is_current(
    const std::filesystem::path& relative_path,
    const std::string& fingerprint
) {
    sqlite3_stmt* statement = nullptr;
    check_sqlite(sqlite3_prepare_v2(db_,
        "SELECT fingerprint FROM file_fingerprints WHERE file_path = ?",
        -1, &statement, nullptr), db_, "Preparing file fingerprint lookup");
    const std::string path = relative_path.generic_string();
    sqlite3_bind_text(statement, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    const int result = sqlite3_step(statement);
    bool current = false;
    if (result == SQLITE_ROW) {
        const auto* stored = sqlite3_column_text(statement, 0);
        current = stored && fingerprint == reinterpret_cast<const char*>(stored);
    } else if (result != SQLITE_DONE) {
        const std::string error = sqlite3_errmsg(db_);
        sqlite3_finalize(statement);
        throw std::runtime_error("Looking up file fingerprint failed: " + error);
    }
    sqlite3_finalize(statement);
    return current;
}

void ChunkStore::mark_file_current(
    const std::filesystem::path& relative_path,
    const std::string& fingerprint
) {
    sqlite3_stmt* statement = nullptr;
    check_sqlite(sqlite3_prepare_v2(db_,
        "INSERT INTO file_fingerprints(file_path, fingerprint) VALUES (?, ?) "
        "ON CONFLICT(file_path) DO UPDATE SET fingerprint = excluded.fingerprint",
        -1, &statement, nullptr), db_, "Preparing file fingerprint update");
    const std::string path = relative_path.generic_string();
    sqlite3_bind_text(statement, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, fingerprint.c_str(), -1, SQLITE_TRANSIENT);
    const int result = sqlite3_step(statement);
    if (result != SQLITE_DONE) {
        const std::string error = sqlite3_errmsg(db_);
        sqlite3_finalize(statement);
        throw std::runtime_error("Updating file fingerprint failed: " + error);
    }
    sqlite3_finalize(statement);
}

void ChunkStore::save_chunks(const std::vector<CodeChunk>& chunks) {
    if (chunks.empty()) return;

    const char* sql = R"(
        INSERT INTO chunks (file_path, category, start_line, end_line, content)
        VALUES (?, ?, ?, ?, ?)
        ON CONFLICT(file_path, start_line, end_line)
        DO UPDATE SET category = excluded.category, content = excluded.content
    )";

    sqlite3_stmt* statement = nullptr;
    check_sqlite(
        sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr),
        db_, "Preparing chunk insert"
    );

    try {
        char* error_message = nullptr;
        const int begin_result =
            sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, &error_message);
        if (begin_result != SQLITE_OK) {
            const std::string message =
                error_message ? error_message : sqlite3_errmsg(db_);
            sqlite3_free(error_message);
            throw std::runtime_error("Starting transaction failed: " + message);
        }

        for (const CodeChunk& chunk : chunks) {
            const std::string path = chunk.source.relative_path.generic_string();

            check_sqlite(
                sqlite3_bind_text(statement, 1, path.c_str(), -1, SQLITE_TRANSIENT),
                db_, "Binding file path"
            );
            check_sqlite(
                sqlite3_bind_text(statement, 2, category_name(chunk.source.category),
                                   -1, SQLITE_TRANSIENT),
                db_, "Binding category"
            );
            check_sqlite(
                sqlite3_bind_int64(statement, 3,
                    static_cast<sqlite3_int64>(chunk.start_line)),
                db_, "Binding start line"
            );
            check_sqlite(
                sqlite3_bind_int64(statement, 4,
                    static_cast<sqlite3_int64>(chunk.end_line)),
                db_, "Binding end line"
            );
            check_sqlite(
                sqlite3_bind_text(statement, 5, chunk.content.c_str(),
                    static_cast<int>(chunk.content.size()), SQLITE_TRANSIENT),
                db_, "Binding content"
            );

            const int step_result = sqlite3_step(statement);
            if (step_result != SQLITE_DONE) {
                throw std::runtime_error(
                    std::string("Saving chunk failed: ") + sqlite3_errmsg(db_)
                );
            }

            sqlite3_reset(statement);
            sqlite3_clear_bindings(statement);
        }

        char* commit_error_message = nullptr;
        const int commit_result =
            sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &commit_error_message);
        if (commit_result != SQLITE_OK) {
            const std::string message = commit_error_message
                ? commit_error_message : sqlite3_errmsg(db_);
            sqlite3_free(commit_error_message);
            throw std::runtime_error("Committing chunks failed: " + message);
        }
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        sqlite3_finalize(statement);
        throw;
    }

    sqlite3_finalize(statement);
}


void ChunkStore::remove_missing_files(
    const std::vector<std::filesystem::path>& current_files
) {
    // Build a temporary table so SQLite can compare all scanned paths
    // against indexed paths without constructing a huge SQL statement.
    char* error_message = nullptr;
    int result = sqlite3_exec(
        db_,
        "BEGIN TRANSACTION;"
        "CREATE TEMP TABLE IF NOT EXISTS current_scan_files ("
        "file_path TEXT PRIMARY KEY);"
        "DELETE FROM current_scan_files;",
        nullptr, nullptr, &error_message
    );

    if (result != SQLITE_OK) {
        const std::string message =
            error_message ? error_message : sqlite3_errmsg(db_);
        sqlite3_free(error_message);
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw std::runtime_error(
            "Starting file reconciliation failed: " + message
        );
    }

    sqlite3_stmt* insert_statement = nullptr;

    try {
        check_sqlite(
            sqlite3_prepare_v2(
                db_,
                "INSERT OR IGNORE INTO current_scan_files(file_path) VALUES (?)",
                -1, &insert_statement, nullptr
            ),
            db_, "Preparing scanned file paths"
        );

        for (const auto& file_path : current_files) {
            const std::string path = file_path.generic_string();

            check_sqlite(
                sqlite3_bind_text(
                    insert_statement, 1, path.c_str(), -1, SQLITE_TRANSIENT
                ),
                db_, "Binding scanned file path"
            );

            result = sqlite3_step(insert_statement);
            if (result != SQLITE_DONE) {
                throw std::runtime_error(
                    std::string("Recording scanned file failed: ")
                    + sqlite3_errmsg(db_)
                );
            }

            sqlite3_reset(insert_statement);
            sqlite3_clear_bindings(insert_statement);
        }

        sqlite3_finalize(insert_statement);
        insert_statement = nullptr;

        error_message = nullptr;
        result = sqlite3_exec(
            db_,
            "DELETE FROM chunks "
            "WHERE file_path NOT IN ("
            "SELECT file_path FROM current_scan_files"
            ");"
            "DELETE FROM file_fingerprints "
            "WHERE file_path NOT IN ("
            "SELECT file_path FROM current_scan_files"
            ");",
            nullptr, nullptr, &error_message
        );

        if (result != SQLITE_OK) {
            const std::string message =
                error_message ? error_message : sqlite3_errmsg(db_);
            sqlite3_free(error_message);
            error_message = nullptr;
            throw std::runtime_error(
                "Removing deleted-file chunks failed: " + message
            );
        }

        error_message = nullptr;
        result = sqlite3_exec(
            db_, "COMMIT", nullptr, nullptr, &error_message
        );

        if (result != SQLITE_OK) {
            const std::string message =
                error_message ? error_message : sqlite3_errmsg(db_);
            sqlite3_free(error_message);
            error_message = nullptr;
            throw std::runtime_error(
                "Committing file reconciliation failed: " + message
            );
        }
    } catch (...) {
        if (insert_statement) sqlite3_finalize(insert_statement);
        sqlite3_free(error_message);
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}


void ChunkStore::replace_file_chunks(
    const std::filesystem::path& relative_path,
    const std::vector<CodeChunk>& chunks
) {
    const std::string path = relative_path.generic_string();

    for (const CodeChunk& chunk : chunks) {
        if (chunk.source.relative_path.generic_string() != path) {
            throw std::invalid_argument(
                "replace_file_chunks received a chunk from another file"
            );
        }
    }

    char* error = nullptr;
    int rc = sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(db_);
        sqlite3_free(error);
        throw std::runtime_error("Beginning file replacement failed: " + message);
    }

    sqlite3_stmt* position_stmt = nullptr;
    sqlite3_stmt* upsert_stmt = nullptr;
    sqlite3_stmt* delete_stmt = nullptr;

    try {
        const char* temp_sql = R"(
            CREATE TEMP TABLE IF NOT EXISTS current_file_chunks (
                start_line INTEGER NOT NULL,
                end_line INTEGER NOT NULL,
                PRIMARY KEY(start_line, end_line)
            );
            DELETE FROM current_file_chunks;
        )";
        error = nullptr;
        rc = sqlite3_exec(db_, temp_sql, nullptr, nullptr, &error);
        if (rc != SQLITE_OK) {
            const std::string message = error ? error : sqlite3_errmsg(db_);
            sqlite3_free(error);
            error = nullptr;
            throw std::runtime_error(
                "Preparing file replacement failed: " + message
            );
        }

        check_sqlite(
            sqlite3_prepare_v2(
                db_,
                "INSERT OR IGNORE INTO current_file_chunks "
                "(start_line, end_line) VALUES (?, ?)",
                -1, &position_stmt, nullptr
            ),
            db_, "Preparing current chunk positions"
        );

        const char* upsert_sql = R"(
            INSERT INTO chunks
                (file_path, category, start_line, end_line, content)
            VALUES (?, ?, ?, ?, ?)
            ON CONFLICT(file_path, start_line, end_line)
            DO UPDATE SET
                category = excluded.category,
                content = excluded.content
        )";
        check_sqlite(
            sqlite3_prepare_v2(
                db_, upsert_sql, -1, &upsert_stmt, nullptr
            ),
            db_, "Preparing chunk upsert"
        );

        for (const CodeChunk& chunk : chunks) {
            // Record the positions that belong in the new version of this file.
            check_sqlite(
                sqlite3_bind_int64(
                    position_stmt, 1,
                    static_cast<sqlite3_int64>(chunk.start_line)
                ),
                db_, "Binding current start line"
            );
            check_sqlite(
                sqlite3_bind_int64(
                    position_stmt, 2,
                    static_cast<sqlite3_int64>(chunk.end_line)
                ),
                db_, "Binding current end line"
            );

            rc = sqlite3_step(position_stmt);
            if (rc != SQLITE_DONE) {
                throw std::runtime_error(
                    std::string("Recording chunk position failed: ")
                    + sqlite3_errmsg(db_)
                );
            }
            sqlite3_reset(position_stmt);
            sqlite3_clear_bindings(position_stmt);

            check_sqlite(
                sqlite3_bind_text(
                    upsert_stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT
                ),
                db_, "Binding file path"
            );
            check_sqlite(
                sqlite3_bind_text(
                    upsert_stmt, 2, category_name(chunk.source.category),
                    -1, SQLITE_TRANSIENT
                ),
                db_, "Binding category"
            );
            check_sqlite(
                sqlite3_bind_int64(
                    upsert_stmt, 3,
                    static_cast<sqlite3_int64>(chunk.start_line)
                ),
                db_, "Binding start line"
            );
            check_sqlite(
                sqlite3_bind_int64(
                    upsert_stmt, 4,
                    static_cast<sqlite3_int64>(chunk.end_line)
                ),
                db_, "Binding end line"
            );
            check_sqlite(
                sqlite3_bind_text(
                    upsert_stmt, 5, chunk.content.c_str(),
                    static_cast<int>(chunk.content.size()), SQLITE_TRANSIENT
                ),
                db_, "Binding content"
            );

            rc = sqlite3_step(upsert_stmt);
            if (rc != SQLITE_DONE) {
                throw std::runtime_error(
                    std::string("Saving current chunk failed: ")
                    + sqlite3_errmsg(db_)
                );
            }
            sqlite3_reset(upsert_stmt);
            sqlite3_clear_bindings(upsert_stmt);
        }

        sqlite3_finalize(position_stmt);
        position_stmt = nullptr;
        sqlite3_finalize(upsert_stmt);
        upsert_stmt = nullptr;

        // Remove only old positions absent from this file's new chunk list.
        check_sqlite(
            sqlite3_prepare_v2(
                db_,
                "DELETE FROM chunks AS c "
                "WHERE c.file_path = ? "
                "AND NOT EXISTS ("
                " SELECT 1 FROM current_file_chunks AS n "
                " WHERE n.start_line = c.start_line "
                " AND n.end_line = c.end_line"
                ")",
                -1, &delete_stmt, nullptr
            ),
            db_, "Preparing obsolete chunk deletion"
        );
        check_sqlite(
            sqlite3_bind_text(
                delete_stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT
            ),
            db_, "Binding obsolete chunk file path"
        );

        rc = sqlite3_step(delete_stmt);
        if (rc != SQLITE_DONE) {
            throw std::runtime_error(
                std::string("Removing obsolete chunks failed: ")
                + sqlite3_errmsg(db_)
            );
        }
        sqlite3_finalize(delete_stmt);
        delete_stmt = nullptr;

        error = nullptr;
        rc = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &error);
        if (rc != SQLITE_OK) {
            const std::string message = error ? error : sqlite3_errmsg(db_);
            sqlite3_free(error);
            error = nullptr;
            throw std::runtime_error(
                "Committing file replacement failed: " + message
            );
        }
    } catch (...) {
        if (position_stmt) sqlite3_finalize(position_stmt);
        if (upsert_stmt) sqlite3_finalize(upsert_stmt);
        if (delete_stmt) sqlite3_finalize(delete_stmt);
        sqlite3_free(error);
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

std::vector<ChunkSearchResult> ChunkStore::search_candidates(
    const std::string& query, std::size_t limit
) {
    if (query.empty() || limit == 0) {
        return {};
    }

    const char* sql = R"(
        SELECT c.file_path, c.category, c.start_line, c.end_line,
               c.content, bm25(chunks_fts)
        FROM chunks_fts
        JOIN chunks AS c ON c.id = chunks_fts.rowid
        WHERE chunks_fts MATCH ?
        ORDER BY bm25(chunks_fts)
        LIMIT ?
    )";

    sqlite3_stmt* statement = nullptr;
    check_sqlite(
        sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr),
        db_, "Preparing FTS candidate search"
    );

    std::vector<ChunkSearchResult> results;

    try {
        check_sqlite(
            sqlite3_bind_text(statement, 1, query.c_str(), -1, SQLITE_TRANSIENT),
            db_, "Binding FTS query"
        );
        check_sqlite(
            sqlite3_bind_int64(
                statement, 2, static_cast<sqlite3_int64>(limit)),
            db_, "Binding result limit"
        );

        int result;
        while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
            const auto* path_text = sqlite3_column_text(statement, 0);
            const auto* category_text = sqlite3_column_text(statement, 1);
            const auto* content_text = sqlite3_column_text(statement, 4);
            const int content_size = sqlite3_column_bytes(statement, 4);

            const std::string path = path_text
                ? reinterpret_cast<const char*>(path_text) : "";
            const std::string category = category_text
                ? reinterpret_cast<const char*>(category_text) : "";
            const std::string content = content_text && content_size > 0
                ? std::string(
                    reinterpret_cast<const char*>(content_text),
                    static_cast<std::size_t>(content_size))
                : "";

            FileCategory file_category = FileCategory::Other;
            if (category == "Source") file_category = FileCategory::Source;
            else if (category == "Header") file_category = FileCategory::Header;
            else if (category == "Test") file_category = FileCategory::Test;
            else if (category == "Documentation") file_category = FileCategory::Documentation;
            else if (category == "BuildConfig") file_category = FileCategory::BuildConfig;

            ScannedFile source_file{
                std::filesystem::path(path), file_category
            };

            CodeChunk chunk{
                std::move(source_file),
                static_cast<std::size_t>(sqlite3_column_int64(statement, 2)),
                static_cast<std::size_t>(sqlite3_column_int64(statement, 3)),
                content
            };

            results.push_back(ChunkSearchResult{
                std::move(chunk),
                sqlite3_column_double(statement, 5)
            });
        }

        if (result != SQLITE_DONE) {
            throw std::runtime_error(
                std::string("FTS candidate search failed: ") + sqlite3_errmsg(db_)
            );
        }
    } catch (...) {
        sqlite3_finalize(statement);
        throw;
    }

    sqlite3_finalize(statement);
    return results;
}
