#include "Embedding.hpp"
#include "VectorStore.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

int main()
{
    constexpr std::size_t dimension = 384;
    constexpr std::size_t vector_count = 100'000;
    constexpr std::size_t query_count = 100;
    constexpr std::size_t top_k = 5;

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> distribution(-1.0, 1.0);

    VectorStore store;

    std::vector<Embedding> vectors;
    vectors.reserve(vector_count);

    // Generate deterministic vectors and build the index.
    for (std::size_t i = 0; i < vector_count; ++i) {

        std::vector<float> values(dimension);

        for (float& value : values)
            value = static_cast<float>(distribution(rng));

        Embedding embedding(std::move(values));

        vectors.push_back(embedding);
        store.add(std::to_string(i), embedding);
    }

    std::vector<double> latencies;
    latencies.reserve(query_count);

    // Generate and search 100 deterministic queries.
    for (std::size_t i = 0; i < query_count; ++i) {

        std::vector<float> values(dimension);

        for (float& value : values)
            value = static_cast<float>(distribution(rng));

        Embedding query(std::move(values));

        auto start = std::chrono::high_resolution_clock::now();

        auto results = store.search_top_k(query, top_k);

        auto end = std::chrono::high_resolution_clock::now();

        double latency =
            std::chrono::duration<double, std::micro>(end - start).count();

        latencies.push_back(latency);
    }

    std::sort(latencies.begin(), latencies.end());

    auto percentile = [&](double p) {
        std::size_t index =
            static_cast<std::size_t>(p * (latencies.size() - 1));

        return latencies[index];
    };

    double p50 = percentile(0.50);
    double p95 = percentile(0.95);
    double p99 = percentile(0.99);

    double qps = 1'000'000.0 / p50;

    std::cout << "\n========================================\n";
    std::cout << "HyperSearch Retrieval Benchmark\n";
    std::cout << "========================================\n\n";

    std::cout << "Vectors:       " << vector_count << '\n';
    std::cout << "Dimensions:    " << dimension << '\n';
    std::cout << "Queries:       " << query_count << '\n';
    std::cout << "Top-K:         " << top_k << "\n\n";

    std::cout << "Retrieval\n";
    std::cout << "---------\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "p50: " << p50 << " us\n";
    std::cout << "p95: " << p95 << " us\n";
    std::cout << "p99: " << p99 << " us\n";
    std::cout << "QPS: " << qps << '\n';

    std::cout << "\nBenchmark complete.\n";
}
