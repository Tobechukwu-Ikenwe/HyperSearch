#pragma once

#include <string>
#include <cstdint>
#include <chrono>
#include <variant>
#include <vector>
#include "Embedding.hpp"
#include "VectorStore.hpp"


// the state in the cycle a given request can be in 
enum class RequestState {
    queued,
    scheduled,
    running,
    completed,
    cancelled,
    failed
};

// the execution a request requires
enum class Backend {
    any,
    cpu,
    gpu
};



// A result produced by one supported request operation.
using ResultValue = std::variant<
    Embedding,
    std::string,
    std::vector<SearchResult>
>;

struct RequestSuccess {
    ResultValue value;
};

struct RequestFailure {
    std::string error_message;
};

using RequestOutcome = std::variant<RequestSuccess, RequestFailure>;

struct RequestResult {
    std::uint64_t request_id;
    RequestOutcome outcome;
};

struct Request {

	std::uint64_t request_id;
	std::string text;
	std::chrono::steady_clock::time_point arrival_time;
	std::uint32_t priority;
	std::chrono::steady_clock::time_point deadline;
	Backend backend;
	// when related to higher level operation
	std::uint64_t correlation_id;


};


