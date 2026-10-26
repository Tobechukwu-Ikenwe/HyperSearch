#pragma once
#include "RequestQueue.hpp"
#include "Batch.hpp"
#include "Executor.hpp"
#include <cstddef>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>
#include <string>

// Reports dispatch status, results, and a diagnostic message.
struct DispatchResult {
    bool success;
    std::vector<RequestResult> results;
    std::string error_message;
};

class Scheduler {
	private:
	//needs direct access to the request queeu
	RequestQueue& requests;
	// max batch size
	std::size_t max_batch_size;
	// max queue size
	std::size_t max_queue_size;
	// create a lock on the queue
	mutable std::mutex scheduler_mutex;
	// state of a request after leaving the queue
	std::unordered_map<std::uint64_t, RequestState> request_states;
	// Preserve outcomes so callers can retrieve them after dispatch.
	std::unordered_map<std::uint64_t, RequestResult> request_results;

	public:
	//connect scheduler to queue via constructor
	Scheduler(RequestQueue& requests,std::size_t max_batch_size,
        std::size_t max_queue_size);
	
	
	bool submit(const Request& request);
	bool update_state(std::uint64_t request_id, RequestState state);
	std::optional<RequestState> get_state(std::uint64_t request_id) const;
std::optional<RequestResult> get_result(std::uint64_t request_id) const;
	bool cancel(std::uint64_t request_id);
	Batch form_batch();
	DispatchResult dispatch(const Batch& batch, Executor& executor); 
}; 
