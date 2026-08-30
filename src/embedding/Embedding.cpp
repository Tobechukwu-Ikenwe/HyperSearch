#include "Embedding.hpp"

#include <utility>


// Store the vector of embedding values.
Embedding::Embedding(std::vector<float> values)
    : values(std::move(values))
{
}


// Return the underlying embedding vector.
const std::vector<float>& Embedding::data() const
{
    return values;
}


// Return the number of values in the embedding.
std::size_t Embedding::size() const
{
    return values.size();
}


// Access one value by its index.
float Embedding::operator[](std::size_t index) const
{
    return values[index];
}
