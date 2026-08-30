#include "VectorStore.hpp"
#include "similarity.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <omp.h>


void VectorStore::add(
    const std::string& text,
    const Embedding& embedding)
{
    documents.push_back({text, embedding});
}


std::size_t VectorStore::size() const
{
    return documents.size();
}


// Find the single document with the highest similarity.
SearchResult VectorStore::search(
    const Embedding& query)
{
    if (documents.empty()) {
        throw std::runtime_error(
            "Vector store is empty"
        );
    }

    // Number of threads OpenMP is configured to use.
    const int thread_count =
        omp_get_max_threads();

    // Each thread gets its own slot for its best score.
    //
    // Thread 0 → best_scores[0]
    // Thread 1 → best_scores[1]
    // ...
    // Thread 11 → best_scores[11]
    std::vector<float> best_scores(
        thread_count,
        std::numeric_limits<float>::lowest()
    );

    // Stores the document index associated with
    // each thread's best score.
    std::vector<std::size_t> best_indices(
        thread_count,
        0
    );

    // OpenMP creates the available thread team and
    // distributes the document iterations between them.
    #pragma omp parallel
    {
        // Identify the current thread.
        const int thread_id =
            omp_get_thread_num();

        // Best result found by this particular thread.
        float local_best_score =
            std::numeric_limits<float>::lowest();

        std::size_t local_best_index = 0;

        // Divide the document loop among the threads.
        #pragma omp for
        for (std::size_t i = 0;
             i < documents.size();
             ++i)
        {
            // Calculate similarity between the query
            // and the current document.
            const float score =
                cosine_similarity(
                    documents[i].embedding,
                    query
                );

            // Update this thread's local winner.
            if (score > local_best_score) {
                local_best_score = score;
                local_best_index = i;
            }
        }

        // Store this thread's winner in its own shared slot.
        //
        // No two threads write to the same element.
        best_scores[thread_id] =
            local_best_score;

        best_indices[thread_id] =
            local_best_index;
    }

    // The parallel region has finished.
    //
    // Now compare the winners from every thread.
    float highest_similarity =
        std::numeric_limits<float>::lowest();

    std::size_t best_index = 0;

    for (int thread = 0;
         thread < thread_count;
         ++thread)
    {
        if (best_scores[thread] > highest_similarity) {
            highest_similarity =
                best_scores[thread];

            best_index =
                best_indices[thread];
        }
    }

    // Return the globally best document.
    return {
        documents[best_index].text,
        highest_similarity
    };
}


// Find the K documents with the highest similarity.
std::vector<SearchResult> VectorStore::search_top_k(
    const Embedding& query,
    std::size_t k)
{
    if (documents.empty()) {
        throw std::runtime_error(
            "Vector store is empty"
        );
    }

    // Never request more results than documents available.
    k = std::min(k, documents.size());

    // Allocate one result slot for every document.
    //
    // This allows different OpenMP threads to write to
    // different elements safely.
    std::vector<SearchResult> results(
        documents.size()
    );

    // ---------------------------------------------------------
    // PARALLEL SIMILARITY CALCULATION
    // ---------------------------------------------------------
    //
    // Each document can be processed independently.
    //
    // OpenMP divides these iterations across the available
    // CPU threads.
    #pragma omp parallel for
    for (std::size_t i = 0;
         i < documents.size();
         ++i)
    {
        // Calculate similarity between this document
        // and the query.
        const float score =
            cosine_similarity(
                documents[i].embedding,
                query
            );

        // Each iteration owns a unique results[i].
        //
        // Therefore different threads do not modify
        // the same vector element.
        results[i] = {
            documents[i].text,
            score
        };
    }

    // ---------------------------------------------------------
    // PARTIAL SORT
    // ---------------------------------------------------------
    //
    // We only need the best K results.
    //
    // partial_sort places the highest-scoring K results
    // at the beginning of the vector without completely
    // sorting every remaining result.
    std::partial_sort(
        results.begin(),
        results.begin() + k,
        results.end(),
        [](const SearchResult& a,
           const SearchResult& b)
        {
            return a.score > b.score;
        }
    );

    // Remove all results after the top K.
    results.resize(k);

    return results;
	}
