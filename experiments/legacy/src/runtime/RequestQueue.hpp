#pragma once

#include "Request.hpp"

#include <cstddef>
#include <queue>
#include <vector>

struct RequestCompare {
    bool operator()(const Request& a, const Request& b) const {
        return a.priority > b.priority;
    }
};

class RequestQueue {
private:
    std::priority_queue<Request, std::vector<Request>, RequestCompare> requests;

public:
    void push(const Request& request);
    Request pop();
    bool cancel(std::uint64_t request_id);
    bool empty() const;
    std::size_t size() const;
};
