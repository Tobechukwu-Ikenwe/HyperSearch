#pragma once

#include "Batch.hpp"
#include "Embedding.hpp"
#include "EmbeddingModel.hpp"

#include <vector>


class Executor{
	
	public:
		virtual std::vector<RequestResult> execute(const Batch& batch) = 0;
		virtual ~Executor() = default;

};

class EmbeddingExecutor : public Executor{
	private:
		EmbeddingModel& model;
	public:
		EmbeddingExecutor(EmbeddingModel& model);

		std::vector<RequestResult> execute(const Batch& batch) override; 
};

