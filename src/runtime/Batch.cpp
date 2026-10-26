#include "Batch.hpp"

void Batch::add(const Request& request) {
    requests.push_back(request);
}

std::size_t Batch::size() const {
    return requests.size();
}

bool Batch::empty() const {
    return requests.empty();
}
