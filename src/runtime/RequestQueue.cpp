#include "RequestQueue.hpp"

void RequestQueue::push(const Request& request) {
    requests.push(request);
}

Request RequestQueue::pop() {
    Request request = requests.top();
    requests.pop();
    return request;
}

bool RequestQueue::empty() const {
    return requests.empty();
}

std::size_t RequestQueue::size() const {
    return requests.size();
}


bool RequestQueue::cancel(std::uint64_t request_id) {
    std::vector<Request> remaining;
    bool cancelled = false;

    while (!requests.empty()) {
        Request request = requests.top();
        requests.pop();

        if (!cancelled && request.request_id == request_id) {
            cancelled = true;
            continue;
        }

        remaining.push_back(request);
    }

    for (const auto& request : remaining) {
        requests.push(request);
    }

    return cancelled;
}
