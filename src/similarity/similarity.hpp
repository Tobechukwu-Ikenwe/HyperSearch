#ifndef SIMILARITY_HPP
#define SIMILARITY_HPP

#include "Embedding.hpp"

#include <cmath>
#include <stdexcept>


// Calculate the dot product of two embeddings.
float dot_product(
    const Embedding& A,
    const Embedding& B)
{
    if (A.size() != B.size()) {
        throw std::invalid_argument(
            "Embeddings must have the same size"
        );
    }

    float result = 0.0f;

    for (std::size_t i = 0;
         i < A.size();
         ++i)
    {
        result += A[i] * B[i];
    }

    return result;
}


// Calculate the magnitude (L2 norm) of an embedding.
float magnitude(
    const Embedding& A)
{
    float sum = 0.0f;

    for (std::size_t i = 0;
         i < A.size();
         ++i)
    {
        sum += A[i] * A[i];
    }

    return std::sqrt(sum);
}


// Calculate cosine similarity between two embeddings.
float cosine_similarity(
    const Embedding& A,
    const Embedding& B)
{
    float magA = magnitude(A);
    float magB = magnitude(B);

    if (magA == 0.0f || magB == 0.0f) {
        throw std::invalid_argument(
            "Cannot calculate similarity with a zero vector"
        );
    }

    return dot_product(A, B)
           / (magA * magB);
}

#endif
