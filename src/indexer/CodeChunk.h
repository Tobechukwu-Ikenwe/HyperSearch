#pragma once

#include "CodebaseScanner.h"

#include <cstddef>
#include <string>

struct CodeChunk {
    ScannedFile source;
    std::size_t start_line;
    std::size_t end_line;
    std::string content;
};
