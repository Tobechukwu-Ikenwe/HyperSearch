#pragma once

#include "CodeChunk.h"

#include <cstddef>
#include <string>
#include <vector>

class CodeChunker {
public:
    CodeChunker(std::size_t chunk_size = 40,
                std::size_t overlap = 5);

    std::vector<CodeChunk> chunk_file(
        const ScannedFile& source,
        const std::string& content
    ) const;

private:
    std::size_t chunk_size_;
    std::size_t overlap_;
};
