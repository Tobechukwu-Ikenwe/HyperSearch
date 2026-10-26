#pragma once

#include "Request.hpp"
#include <cstddef>
#include <vector>

struct Batch {
    void add(const Request& request);
    std::size_t size() const;
    bool empty() const;

    std::vector<Request> requests;
};
