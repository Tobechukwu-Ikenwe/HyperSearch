#include "CodebaseScanner.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <set>
#include <string>
#include <system_error>

namespace {

namespace fs = std::filesystem;

// Normalize filename spelling before matching extensions and filenames.
std::string lowercase(std::string text) {
    std::transform(
        text.begin(), text.end(), text.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );

    return text;
}

// Explicit allowlist of file extensions supported by the scanner.
const std::set<std::string>& supported_extensions() {
    static const std::set<std::string> extensions = {
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
        ".py", ".pyi", ".rs", ".go", ".java",
        ".js", ".jsx", ".ts", ".tsx",
        ".yaml", ".yml", ".json", ".toml", ".ini", ".xml",
        ".sh", ".bash", ".zsh", ".ps1",
        ".md", ".rst", ".txt", ".cmake", ".tf", ".hcl"
    };

    return extensions;
}

// Some important build files are recognized by name rather than extension.
const std::set<std::string>& supported_filenames() {
    static const std::set<std::string> filenames = {
        "cmakelists.txt", "makefile", "gnumakefile",
        "dockerfile", "containerfile", "build", "build.bazel",
        "workspace", "workspace.bazel", "justfile", "taskfile"
    };

    return filenames;
}

// Prune these directory trees instead of traversing their contents.
const std::set<std::string>& ignored_directories() {
    static const std::set<std::string> directories = {
        ".git", ".hg", ".svn", ".hypersearch", "build",
        "build-debug", "build-release", "build-relwithdebinfo",
        "build-minsizerel", "cmakefiles",
        "third_party", "vendor", "node_modules", "bower_components",
        "eval", "experiments", "test-repos",
        ".venv", "venv", "__pycache__", ".pytest_cache",
        ".mypy_cache", ".tox", ".nox", "site-packages",
        ".cache", "dist", "out", "target"
    };

    return directories;
}

bool is_ignored_directory(
    const fs::directory_entry& entry,
    const std::string& filename
) {
    if (ignored_directories().contains(filename) ||
        filename.starts_with("cmake-build-")) {
        return true;
    }

    // CMake build trees may have arbitrary names, including out-of-source
    // directories that do not follow a build-* naming convention.
    std::error_code ec;
    return fs::exists(entry.path() / "CMakeCache.txt", ec) ||
           fs::is_directory(entry.path() / "CMakeFiles", ec);
}

// Exclude common credential and secret filenames.
// This is a denylist, not a guarantee that all secrets are detected.
const std::set<std::string>& ignored_filenames() {
    static const std::set<std::string> filenames = {
        ".env", "id_rsa", "id_ed25519",
        "credentials", "secrets.yaml", "secrets.yml",
        "secrets.json", "service-account.json",
        "project_snapshot.txt", "cmakecache.txt",
        "cmake_install.cmake", "ctesttestfile.cmake",
        "cpackconfig.cmake", "cpacksourceconfig.cmake",
        "compile_commands.json", "install_manifest.txt"
    };

    return filenames;
}

// Decide whether a regular file is eligible for indexing.
bool is_supported_file(
    const fs::directory_entry& entry,
    const std::string& filename,
    const std::string& extension,
    std::error_code& ec
) {
    if (!entry.is_regular_file(ec) || ec) {
        ec.clear();
        return false;
    }

    // The allowlist also rejects compiled binaries, object files, and
    // static/shared libraries by their extensions.
    if (!supported_filenames().contains(filename) &&
        !supported_extensions().contains(extension)) {
        return false;
    }

    if (ignored_filenames().contains(filename) ||
        filename.ends_with(".pem") ||
        filename.ends_with(".key") ||
        filename.ends_with(".p12") ||
        filename.ends_with(".pfx")) {
        return false;
    }

    // Reject files whose size cannot be determined or exceeds 1 MiB.
    constexpr std::uintmax_t max_file_size = 1024 * 1024;
    const std::uintmax_t size = entry.file_size(ec);

    if (ec || size > max_file_size) {
        ec.clear();
        return false;
    }

    return true;
}

// Classify an eligible file after the filtering policy accepts it.
FileCategory classify_file(
    const fs::path& relative_path,
    const std::string& filename,
    const std::string& extension
) {
    // Directory-based test classification takes precedence over extension.
    for (auto component = relative_path.begin();
         component != relative_path.end(); ++component) {
        const bool is_filename =
            std::next(component) == relative_path.end();

        if (is_filename) {
            break;
        }

        const std::string directory = lowercase(component->string());

        if (directory == "test" || directory == "tests") {
            return FileCategory::Test;
        }
    }

    // Recognize common test filename conventions.
    if (filename.ends_with("_test.cpp") ||
        filename.ends_with("_test.py") ||
        filename.starts_with("test_")) {
        return FileCategory::Test;
    }

    if (extension == ".h" || extension == ".hh" ||
        extension == ".hpp" || extension == ".hxx") {
        return FileCategory::Header;
    }

    if (extension == ".c" || extension == ".cc" ||
        extension == ".cpp" || extension == ".cxx" ||
        extension == ".py" || extension == ".pyi" ||
        extension == ".rs" || extension == ".go" ||
        extension == ".java" || extension == ".js" ||
        extension == ".jsx" || extension == ".ts" ||
        extension == ".tsx" || extension == ".sh" ||
        extension == ".bash" || extension == ".zsh" ||
        extension == ".ps1") {
        return FileCategory::Source;
    }

    if (extension == ".md" || extension == ".rst" ||
        extension == ".txt") {
        return FileCategory::Documentation;
    }

    if (filename == "cmakelists.txt" ||
        filename == "makefile" || filename == "gnumakefile" ||
        filename == "dockerfile" || filename == "containerfile" ||
        filename == "build" || filename == "build.bazel" ||
        filename == "workspace" || filename == "workspace.bazel" ||
        filename == "justfile" || filename == "taskfile" ||
        extension == ".cmake" || extension == ".toml" ||
        extension == ".yaml" || extension == ".yml" ||
        extension == ".ini" || extension == ".json" ||
        extension == ".tf" || extension == ".hcl") {
        return FileCategory::BuildConfig;
    }

    return FileCategory::Other;
}

} // namespace

std::vector<ScannedFile> CodebaseScanner::scan(
    const std::filesystem::path& root_directory
) const {
    std::vector<ScannedFile> files;
    std::error_code ec;

    // Normalize the root so discovered paths and relative-path calculations
    // use the same path representation.
    const fs::path root =
        fs::absolute(root_directory, ec).lexically_normal();

    if (ec || !fs::is_directory(root, ec) || ec) {
        return files;
    }

    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec
    );
    const fs::recursive_directory_iterator end;

    if (ec) {
        return files;
    }

    for (; it != end; it.increment(ec)) {
        // Clear recoverable traversal errors and continue where possible.
        if (ec) {
            ec.clear();
            continue;
        }

        const fs::directory_entry& entry = *it;
        const std::string filename =
            lowercase(entry.path().filename().string());

        // Do not index content outside the requested tree through a file
        // symlink. Directory symlinks are not followed by the iterator either.
        if (entry.is_symlink(ec)) {
            ec.clear();
            continue;
        }
        if (ec) {
            ec.clear();
            continue;
        }

        // Prune ignored directories before visiting their descendants.
        if (entry.is_directory(ec)) {
            if (is_ignored_directory(entry, filename)) {
                it.disable_recursion_pending();
            }

            ec.clear();
            continue;
        }

        if (ec) {
            ec.clear();
            continue;
        }

        const std::string extension =
            lowercase(entry.path().extension().string());

        if (!is_supported_file(entry, filename, extension, ec)) {
            continue;
        }

        // Store paths relative to the repository, not the host machine.
        const fs::path relative_path =
            entry.path().lexically_relative(root);

        files.push_back({
            relative_path,
            classify_file(relative_path, filename, extension)
        });
    }

    // Filesystem traversal order is unspecified, so sort for reproducibility.
    std::sort(
        files.begin(), files.end(),
        [](const ScannedFile& left, const ScannedFile& right) {
            return left.relative_path.generic_string() <
                   right.relative_path.generic_string();
        }
    );

    return files;
}
