#ifndef RESULT_FUSION_HPP
#define RESULT_FUSION_HPP

#include "VectorStore.hpp"

#include <cstddef>
#include <vector>

// Combines lexical and semantic rankings without comparing their raw scores.
class ResultFusion {
public:
    explicit ResultFusion(
        std::size_t rank_constant = 60,
        std::size_t max_results_per_file = 5
    );

    std::vector<SearchResult> fuse(
        const std::vector<SearchResult>& lexical_results,
        const std::vector<SearchResult>& semantic_results,
        std::size_t limit = 20
    ) const;

private:
    std::size_t rank_constant_;
    std::size_t max_results_per_file_;
};

#endif
