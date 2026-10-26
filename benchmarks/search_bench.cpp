#include "QueryAnalyzer.h"
#include "ResultFusion.hpp"
#include "SearchResultFormatter.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

double percentile(const std::vector<double>& sorted, double fraction) {
    const auto index = static_cast<std::size_t>(
        fraction * static_cast<double>(sorted.size() - 1));
    return sorted[index];
}

int main(int argc, char** argv) {
    const int iterations = argc > 1 ? std::stoi(argv[1]) : 200;
    if (iterations <= 0) throw std::invalid_argument("iterations must be positive");

    const fs::path root = fs::temp_directory_path() /
        ("hypersearch-selection-bench-" +
         std::to_string(Clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        std::vector<SearchResult> lexical;
        std::vector<SearchResult> semantic;
        lexical.reserve(50);
        semantic.reserve(50);
        for (int i = 0; i < 75; ++i) {
            const std::string path = "candidate_" + std::to_string(i) + ".cpp";
            const std::string source =
                "auto embeddings = model.embed_batch(chunks);\n"
                "store.save(embeddings);\n"
                "auto cached = store.load(embedding_id);\n";
            std::ofstream(root / path) << source;
            SearchResult candidate{source, 0.0f, path, 1, 3};
            if (i < 50) lexical.push_back(candidate);
            if (i >= 25) semantic.push_back(candidate);
        }

        ResultFusion fusion;
        SearchResultFormatter formatter;
        std::size_t checksum = 0;
        for (const std::string question : {
                 "where is embedding saved",
                 "where are embeddings generated, stored, and loaded"}) {
            const auto query = analyze_query(question);
            std::vector<double> latencies;
            latencies.reserve(iterations);
            for (int i = 0; i < iterations + 10; ++i) {
                const auto start = Clock::now();
                const auto fused = fusion.fuse(lexical, semantic, 100);
                const std::string output = formatter.format(fused, query, root);
                const auto end = Clock::now();
                if (fused.size() != 75 || output.empty()) {
                    throw std::runtime_error("benchmark selection failed");
                }
                checksum += output.size();
                if (i >= 10) {
                    latencies.push_back(std::chrono::duration<double, std::milli>(
                        end - start).count());
                }
            }
            std::sort(latencies.begin(), latencies.end());
            std::cout << question << " (50 lexical + 50 semantic; "
                      << "75 unique candidates)\n"
                      << "Iterations: " << iterations << "\n"
                      << std::fixed << std::setprecision(3)
                      << "p50: " << percentile(latencies, 0.50) << " ms\n"
                      << "p95: " << percentile(latencies, 0.95) << " ms\n"
                      << "min: " << latencies.front() << " ms\n"
                      << "max: " << latencies.back() << " ms\n";
        }
        std::cout << "checksum: " << checksum << '\n';
    } catch (...) {
        fs::remove_all(root);
        throw;
    }
    fs::remove_all(root);
}
