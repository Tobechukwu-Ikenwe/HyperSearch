#include "ResultFusion.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

struct FusedCandidate {
    SearchResult result;
    double fused_score = 0.0;
};

std::string result_key(const SearchResult& result)
{
    return result.file_path + ":" +
        std::to_string(result.start_line) + ":" +
        std::to_string(result.end_line);
}

void accumulate_ranks(
    const std::vector<SearchResult>& results,
    std::size_t rank_constant,
    std::vector<FusedCandidate>& candidates,
    std::unordered_map<std::string, std::size_t>& positions
)
{
    std::unordered_set<std::string> seen_in_list;
    std::size_t rank = 0;

    for (const auto& result : results) {
        const std::string key = result_key(result);

        // A duplicate within one input list contributes only once.
        if (!seen_in_list.insert(key).second) {
            continue;
        }

        ++rank;
        const double contribution =
            1.0 / (static_cast<double>(rank_constant) +
                   static_cast<double>(rank));

        const auto found = positions.find(key);
        if (found != positions.end()) {
            candidates[found->second].fused_score += contribution;
        } else {
            const std::size_t index = candidates.size();
            candidates.push_back(FusedCandidate{result, contribution});
            positions.emplace(key, index);
        }
    }
}

} // namespace

ResultFusion::ResultFusion(
    std::size_t rank_constant,
    std::size_t max_results_per_file
)
    : rank_constant_(rank_constant),
      max_results_per_file_(max_results_per_file)
{}

std::vector<SearchResult> ResultFusion::fuse(
    const std::vector<SearchResult>& lexical_results,
    const std::vector<SearchResult>& semantic_results,
    std::size_t limit
) const
{
    if (limit == 0 || max_results_per_file_ == 0) {
        return {};
    }

    std::vector<FusedCandidate> candidates;
    std::unordered_map<std::string, std::size_t> positions;

    candidates.reserve(lexical_results.size() + semantic_results.size());
    positions.reserve(lexical_results.size() + semantic_results.size());

    accumulate_ranks(
        lexical_results, rank_constant_, candidates, positions);
    accumulate_ranks(
        semantic_results, rank_constant_, candidates, positions);

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const FusedCandidate& a, const FusedCandidate& b) {
            if (a.fused_score != b.fused_score) {
                return a.fused_score > b.fused_score;
            }
            return result_key(a.result) < result_key(b.result);
        });

    std::vector<SearchResult> fused;
    fused.reserve(std::min(limit, candidates.size()));
    std::unordered_map<std::string, std::size_t> per_file;

    for (const auto& candidate : candidates) {
        const std::string& path = candidate.result.file_path;
        auto& count = per_file[path];

        if (count >= max_results_per_file_) {
            continue;
        }

        // Store the fused score so callers can rank the combined results.
        SearchResult result = candidate.result;
        result.score = static_cast<float>(candidate.fused_score);
        fused.push_back(std::move(result));
        ++count;

        if (fused.size() >= limit) {
            break;
        }
    }

    return fused;
}
