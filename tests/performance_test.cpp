#include "VectorStore.hpp"
#include "Embedding.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;


// Generate deterministic synthetic embeddings.
std::vector<Embedding> generate_embeddings(std::size_t count)
{
    constexpr std::size_t dimensions = 384;

    // Fixed seed makes every benchmark run reproducible.
    std::mt19937 generator(42);
    std::uniform_real_distribution<float> distribution(-1.0f, 1.0f);

    std::vector<Embedding> embeddings;
    embeddings.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {

        std::vector<float> values;
        values.reserve(dimensions);

        for (std::size_t d = 0; d < dimensions; ++d) {
            values.push_back(distribution(generator));
        }

        embeddings.emplace_back(
            std::move(values)
        );
    }

    return embeddings;
}


// Measure VectorStore search at a specific index size.
void benchmark_search(
    const std::vector<Embedding>& embeddings,
    std::size_t count)
{
    VectorStore store;

    // Build the index.
    for (std::size_t i = 0; i < count; ++i) {
        store.add(
            std::to_string(i),
            embeddings[i]
        );
    }

    // Use an existing vector as the query.
    const Embedding& query = embeddings[0];

    // Run once before measuring.
    store.search(query);

    constexpr std::size_t iterations = 100;

    std::vector<double> times;
    times.reserve(iterations);

    // Measure multiple searches.
    for (std::size_t i = 0; i < iterations; ++i) {

        auto start = Clock::now();

        auto result = store.search(query);

        auto finish = Clock::now();

        // Keep the compiler from removing the search.
        volatile float score = result.score;
        (void)score;

        double microseconds =
            std::chrono::duration<double, std::micro>(
                finish - start
            ).count();

        times.push_back(microseconds);
    }


    // Sort timings so we can calculate percentiles.
    std::sort(
        times.begin(),
        times.end()
    );

    double p50 =
        times[times.size() / 2];

    double p95 =
        times[
            static_cast<std::size_t>(
                times.size() * 0.95
            )
        ];

    double p99 =
        times[
            static_cast<std::size_t>(
                times.size() * 0.99
            )
        ];

    // Queries per second based on median latency.
    double qps =
        1'000'000.0 / p50;


    std::cout
        << std::left
        << std::setw(12)
        << count
        << std::setw(14)
        << std::fixed
        << std::setprecision(2)
        << p50
        << std::setw(14)
        << p95
        << std::setw(14)
        << p99
        << qps
        << '\n';
}


int main()
{
    constexpr std::size_t maximum_vectors = 100000;

    std::cout
        << "========================================\n"
        << "HyperSearch Search Benchmark\n"
        << "========================================\n\n";


    std::cout
        << "Generating "
        << maximum_vectors
        << " synthetic 384-dimensional embeddings...\n";

    auto embeddings =
        generate_embeddings(maximum_vectors);

    std::cout
        << "Generated "
        << embeddings.size()
        << " vectors\n\n";


    std::cout
        << "Search scaling\n"
        << "--------------------------------------------------\n"
        << std::left
        << std::setw(12) << "Vectors"
        << std::setw(14) << "p50 us"
        << std::setw(14) << "p95 us"
        << std::setw(14) << "p99 us"
        << "QPS\n";

    std::cout
        << "--------------------------------------------------\n";


    // Test how search scales as the index grows.
    const std::vector<std::size_t> sizes = {
        1000,
        10000,
        100000
    };

    for (std::size_t count : sizes) {

        benchmark_search(
            embeddings,
            count
        );
    }


    std::cout
        << "\nBenchmark complete.\n";
}
