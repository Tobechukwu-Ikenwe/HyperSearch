#include "Scheduler.hpp"
#include <stdexcept>
#include <exception>
#include <unordered_set>
#include <utility>

Scheduler::Scheduler(
    RequestQueue& requests,
    std::size_t max_batch_size,
    std::size_t max_queue_size
)
    : requests(requests),
      max_batch_size(max_batch_size),
      max_queue_size(max_queue_size) {
    // Reject configurations that cannot process or accept requests.
    if (max_batch_size == 0 || max_queue_size == 0) {
        throw std::invalid_argument(
            "Batch size and queue capacity must both be greater than zero"
        );
    }
}

bool Scheduler::submit(const Request& request) {
    std::lock_guard<std::mutex> lock(scheduler_mutex);

    // Reserve zero as an invalid request ID.
    if (request.request_id == 0) {
        return false;
    }

    // Reject duplicate IDs before changing the queue or registry.
    if (request_states.find(request.request_id) != request_states.end()) {
        return false;
    }

    if (requests.size() >= max_queue_size) {
        return false;
    }

    requests.push(request);
    request_states[request.request_id] = RequestState::queued;

    return true;
}

bool Scheduler::update_state(std::uint64_t request_id, RequestState state) {
    std::lock_guard<std::mutex> lock(scheduler_mutex);

    auto it = request_states.find(request_id);
    if (it == request_states.end()) {
        return false;
    }

    RequestState current = it->second;
    bool valid = false;

    // Permit only explicitly supported lifecycle transitions.
    switch (current) {
        case RequestState::queued:
            valid = state == RequestState::scheduled ||
                    state == RequestState::cancelled;
            break;
        case RequestState::scheduled:
            valid = state == RequestState::running ||
                    state == RequestState::failed;
            break;
        case RequestState::running:
            valid = state == RequestState::completed ||
                    state == RequestState::failed;
            break;
        case RequestState::completed:
        case RequestState::cancelled:
        case RequestState::failed:
            valid = false;
            break;
    }

    if (!valid) {
        return false;
    }

    it->second = state;
    return true;
}


std::optional<RequestState> Scheduler::get_state(
    std::uint64_t request_id
) const {
    // Lock the registry before reading it.
    std::lock_guard<std::mutex> lock(scheduler_mutex);

    auto it = request_states.find(request_id);

    if (it == request_states.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::optional<RequestResult> Scheduler::get_result(
    std::uint64_t request_id
) const {
    // Protect the result registry while looking up the request.
    std::lock_guard<std::mutex> lock(scheduler_mutex);

    auto it = request_results.find(request_id);

    if (it == request_results.end()) {
        return std::nullopt;
    }

    return it->second;
}

Batch Scheduler::form_batch() {
    std::lock_guard<std::mutex> lock(scheduler_mutex);

    Batch batch;

    while (!requests.empty() && batch.size() < max_batch_size) {
        Request request = requests.pop();

        // Schedule only requests still registered as queued.
        auto it = request_states.find(request.request_id);
        if (it == request_states.end() ||
            it->second != RequestState::queued) {
            continue;
        }

        it->second = RequestState::scheduled;
        batch.add(request);
    }

    return batch;
}

DispatchResult Scheduler::dispatch(
    const Batch& batch,
    Executor& executor
) {
    if (batch.empty()) {
        return {true, {}, ""};
    }

    // Fail only requests that have not already reached a terminal state.
    auto fail_batch = [&batch, this]() {
        for (const Request& request : batch.requests) {
            update_state(request.request_id, RequestState::failed);
        }
    };

    for (const Request& request : batch.requests) {
        if (!update_state(request.request_id, RequestState::running)) {
            fail_batch();
            return {false, {}, "Could not transition request to running"};
        }
    }

    try {
        std::vector<RequestResult> results = executor.execute(batch);

        if (results.size() != batch.size()) {
            fail_batch();
            return {false, {}, "Executor returned an incomplete result set"};
        }

        // Validate every ID before changing any request's terminal state.
        std::unordered_set<std::uint64_t> result_ids;

        for (const RequestResult& result : results) {
            bool found = false;

            for (const Request& request : batch.requests) {
                if (request.request_id == result.request_id) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                fail_batch();
                return {false, {}, "Executor returned an unknown request ID"};
            }

            if (!result_ids.insert(result.request_id).second) {
                fail_batch();
                return {false, {}, "Executor returned a duplicate request ID"};
            }
        }

        if (result_ids.size() != batch.size()) {
            fail_batch();
            return {false, {}, "Executor omitted one or more request results"};
        }

        {
            std::lock_guard<std::mutex> lock(scheduler_mutex);

            // Validate every request before modifying either registry.
            for (const RequestResult& result : results) {
                auto it = request_states.find(result.request_id);

                if (it == request_states.end() ||
                    it->second != RequestState::running) {
                    return {
                        false,
                        results,
                        "Could not transition request to its final state"
                    };
                }
            }

            // Store only this batch's outcomes.
            for (const RequestResult& result : results) {
                request_results.insert_or_assign(result.request_id, result);
            }

            // Record each request's final lifecycle state.
            for (const RequestResult& result : results) {
                request_states.at(result.request_id) =
                    std::holds_alternative<RequestSuccess>(result.outcome)
                        ? RequestState::completed
                        : RequestState::failed;
            }
        }

        // Dispatch succeeded even if individual requests failed.
        return {true, std::move(results), ""};
    } catch (const std::exception& error) {
        fail_batch();
        return {false, {}, error.what()};
    }
}

bool Scheduler::cancel(std::uint64_t request_id) {
    std::lock_guard<std::mutex> lock(scheduler_mutex);

    // Only queued requests can be removed from the queue.
    auto it = request_states.find(request_id);
    if (it == request_states.end() ||
        it->second != RequestState::queued) {
        return false;
    }

    if (!requests.cancel(request_id)) {
        return false;
    }

    // Keep the state registry consistent with the queue.
    it->second = RequestState::cancelled;
    return true;
}
