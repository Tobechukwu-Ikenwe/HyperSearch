#include "Executor.hpp"

#include <exception>
#include <utility>
#include <vector>

EmbeddingExecutor::EmbeddingExecutor(EmbeddingModel& model)
    : model(model)
{
}

std::vector<RequestResult> EmbeddingExecutor::execute(const Batch& batch)
{
    std::vector<RequestResult> results;
    results.reserve(batch.size());

    for (const Request& request : batch.requests) {
        try {
            Embedding embedding = model.embed(request.text);

            results.push_back(RequestResult{
                request.request_id,
                RequestSuccess{ResultValue{std::move(embedding)}}
            });
        }
        catch (const std::exception& error) {
            results.push_back(RequestResult{
                request.request_id,
                RequestFailure{error.what()}
            });
        }
    }

    return results;
}
