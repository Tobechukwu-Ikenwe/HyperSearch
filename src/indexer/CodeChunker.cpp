#include "CodeChunker.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

CodeChunker::CodeChunker(std::size_t chunk_size,
                         std::size_t overlap)
    : chunk_size_(chunk_size),
      overlap_(overlap) {
    if (chunk_size_ == 0 || overlap_ >= chunk_size_) {
        throw std::invalid_argument(
            "Chunk size must be positive and overlap must be smaller "
            "than chunk size"
        );
    }
}

std::vector<CodeChunk> CodeChunker::chunk_file(
    const ScannedFile& source,
    const std::string& content
) const {
    std::vector<CodeChunk> chunks;

    if (content.empty()) {
        return chunks;
    }

    std::vector<std::size_t> line_starts{0};

    for (std::size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n' && i + 1 < content.size()) {
            line_starts.push_back(i + 1);
        }
    }

    const std::size_t step = chunk_size_ - overlap_;

    for (std::size_t start = 0; start < line_starts.size(); start += step) {
        const std::size_t end =
            std::min(start + chunk_size_, line_starts.size());

        const std::size_t byte_start = line_starts[start];
        const std::size_t byte_end =
            end < line_starts.size() ? line_starts[end] : content.size();

        chunks.push_back({
            source,
            start + 1,
            end,
            content.substr(byte_start, byte_end - byte_start)
        });

        if (end == line_starts.size()) {
            break;
        }
    }

    return chunks;
}
