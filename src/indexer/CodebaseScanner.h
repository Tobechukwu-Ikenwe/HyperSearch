#pragma once

#include <filesystem>
#include <vector>

// Broad roles a discovered repository file can serve.
enum class FileCategory {
    Source,
    Header,
    Test,
    Documentation,
    BuildConfig,
    Other
};

// A repository file and its classification.
struct ScannedFile {
    std::filesystem::path relative_path;
    FileCategory category;
};

class CodebaseScanner {
public:
    std::vector<ScannedFile> scan(
        const std::filesystem::path& root_directory
    ) const;
};
