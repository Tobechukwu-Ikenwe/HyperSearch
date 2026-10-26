#include "indexer/CodebaseScanner.h"
#include "indexer/CodeChunker.h"
#include "indexer/ChunkStore.h"
#include "embeddings/EmbeddingStore.h"
#include "embeddings/EmbeddingPipeline.h"
#include "query/QueryAnalyzer.h"
#include "query/LexicalSearch.hpp"
#include "query/SemanticSearch.hpp"
#include "query/ResultFusion.hpp"
#include "vectorstore/VectorStore.hpp"
#include "output/SearchResultFormatter.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string read_file(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open file: " + path.string());
    }

    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

void print_usage(const char* program)
{
    std::cerr
        << "Usage: " << program << " <repository-path> <search query>\n"
        << "Example: " << program
        << " . \"where are SQL queries executed\"\n";
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        if (argc < 3) {
            print_usage(argv[0]);
            return 2;
        }

        const fs::path repository = fs::canonical(argv[1]);
        if (!fs::is_directory(repository)) {
            throw std::runtime_error("Repository path is not a directory");
        }

        std::string query_text;
        for (int i = 2; i < argc; ++i) {
            if (!query_text.empty()) {
                query_text += ' ';
            }
            query_text += argv[i];
        }

        if (query_text.find_first_not_of(" \t\r\n") == std::string::npos) {
            throw std::invalid_argument("Search query cannot be empty");
        }

        // Keep persistent chunks and embeddings together so cache foreign
        // keys and chunk lookups refer to the same SQLite database.
        const fs::path database_directory = repository / ".hypersearch";
        fs::create_directories(database_directory);
        const fs::path database_path = database_directory / "index.sqlite3";

        ChunkStore chunk_store(database_path);
        chunk_store.initialize();

        EmbeddingStore embedding_store(database_path);
        embedding_store.initialize();

        CodebaseScanner scanner;
        CodeChunker chunker;

        const auto files = scanner.scan(repository);

        // Finish reading and chunking before changing the persistent index.
        struct PendingFile {
            fs::path relative_path;
            std::vector<CodeChunk> chunks;
        };

        std::vector<PendingFile> pending_files;
        std::vector<fs::path> current_file_paths;
        std::vector<CodeChunk> chunks;

        pending_files.reserve(files.size());
        current_file_paths.reserve(files.size());

        for (const ScannedFile& file : files) {
            const fs::path absolute_path = repository / file.relative_path;
            const std::string content = read_file(absolute_path);
            auto file_chunks = chunker.chunk_file(file, content);

            current_file_paths.push_back(file.relative_path);
            pending_files.push_back(
                PendingFile{file.relative_path, std::move(file_chunks)}
            );
        }

        // Remove records for files no longer found by the completed scan.
        chunk_store.remove_missing_files(current_file_paths);

        // Replace each file's records while retaining valid cached embeddings.
        for (const PendingFile& file : pending_files) {
            chunk_store.replace_file_chunks(file.relative_path, file.chunks);
            chunks.insert(chunks.end(), file.chunks.begin(), file.chunks.end());
        }

        EmbeddingPipeline embedding_pipeline(embedding_store);
        embedding_pipeline.initialize();

        VectorStore vector_store;
        embedding_pipeline.index_chunks(chunks, vector_store);

        const SearchQuery analyzed_query = analyze_query(query_text);

        LexicalSearch lexical_search(chunk_store);
        SemanticSearch semantic_search(embedding_pipeline, vector_store);
        ResultFusion fusion;
        SearchResultFormatter formatter;

        const auto lexical_results = lexical_search.search(analyzed_query, 50);
        const auto semantic_results = semantic_search.search(analyzed_query, 50);
        const auto results = fusion.fuse(
            lexical_results, semantic_results, 100
        );

        std::cout << formatter.format(results, analyzed_query, repository);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "HyperSearch error: " << error.what() << '\n';
        return 1;
    }
}
