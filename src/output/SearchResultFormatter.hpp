#pragma once

#include "../embedding/VectorStore.hpp"
#include "../search/SearchQuery.h"

#include <filesystem>
#include <string>
#include <vector>

class SearchResultFormatter {
public:
    // Convert ranked search results into readable terminal output.
    std::string format(
        const std::vector<SearchResult>& results,
        const SearchQuery& query,
        const std::filesystem::path& repository
    ) const;
};
