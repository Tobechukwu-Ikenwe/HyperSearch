#pragma once

#include <cstddef>
#include <string>

class SearchEngine {
public:
    // Prepare a query before retrieval; empty output means no search is needed.
    std::string prepare_search(
        const std::string& query,
        std::size_t limit
    ) const;
};
