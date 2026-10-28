#include "CodebaseScanner.h"
#include "CodeChunker.h"
#include "ChunkStore.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

const char* category_name(FileCategory category) {
    switch (category) {
        case FileCategory::Source:         return "Source";
        case FileCategory::Header:         return "Header";
        case FileCategory::Test:           return "Test";
        case FileCategory::Documentation:  return "Documentation";
        case FileCategory::BuildConfig:    return "BuildConfig";
        case FileCategory::Other:          return "Other";
    }

    return "Unknown";
}

int main() {
    const std::filesystem::path root = "test-repos/SQLiteCpp";

    CodebaseScanner scanner;
    CodeChunker chunker;
    ChunkStore store("hypersearch.db");
    store.initialize();

    const std::vector<ScannedFile> files = scanner.scan(root);

    std::size_t total_chunks = 0;
    std::size_t processed_files = 0;
    std::size_t unreadable_files = 0;
    std::vector<CodeChunk> examples;

    for (const ScannedFile& file : files) {
        if (file.category == FileCategory::Other) {
            continue;
        }

        const std::filesystem::path full_path = root / file.relative_path;
        std::ifstream input(full_path, std::ios::binary);

        if (!input) {
            ++unreadable_files;
            continue;
        }

        const std::string content{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };

        std::vector<CodeChunk> chunks =
            chunker.chunk_file(file, content);
        store.save_chunks(chunks);

        ++processed_files;
        total_chunks += chunks.size();

        for (CodeChunk& chunk : chunks) {
            if (examples.size() < 3) {
                examples.push_back(std::move(chunk));
            }
        }
    }

    std::cout << "Database: hypersearch.db\n";
    std::cout << "Files discovered: " << files.size() << '\n';
    std::cout << "Files processed: " << processed_files << '\n';
    std::cout << "Chunks produced: " << total_chunks << '\n';
    std::cout << "Unreadable files: " << unreadable_files << "\n\n";

    for (const CodeChunk& chunk : examples) {
        std::cout << "File: " << chunk.source.relative_path.string() << '\n';
        std::cout << "Category: "
                  << category_name(chunk.source.category) << '\n';
        std::cout << "Lines: " << chunk.start_line
                  << '-' << chunk.end_line << '\n';
        std::cout << "Content preview:\n"
                  << chunk.content.substr(0, 300)
                  << "\n\n---\n\n";
    }

    return 0;
}
