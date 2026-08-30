#ifndef EMBEDDING_HPP
#define EMBEDDING_HPP

#include <cstddef>
#include <vector>

class Embedding {

private:
    std::vector<float> values;

public:
    Embedding(std::vector<float> values);

    const std::vector<float>& data() const;

    std::size_t size() const;

    float operator[](std::size_t index) const;
};

#endif
