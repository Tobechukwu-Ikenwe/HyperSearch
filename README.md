
## Roadmap

HyperSearch is being developed incrementally, with each stage focused on improving search performance, scalability, and systems-level efficiency.

### v1 — Brute-Force Vector Search
- 384-dimensional transformer embeddings
- Vector storage and retrieval
- Cosine similarity search
- Top-K retrieval
- Retrieval correctness tests
- Performance benchmarking across 1K–100K vectors

### v2 — Parallel Search
- OpenMP parallel similarity search
- Thread-scaling experiments
- Latency and throughput analysis
- CPU utilization and parallel scaling benchmarks

### v3 — SIMD Optimization
- SIMD-optimized vector similarity
- Compare scalar vs SIMD implementations
- Benchmark AVX/SSE implementations where supported
- Analyze compute throughput and memory behavior

### v4 — Approximate Nearest Neighbor Search
- Implement an HNSW-based index from first principles
- Compare brute-force and approximate search
- Measure latency, throughput, and recall
- Study search-quality vs performance tradeoffs

### v5 — Persistent Vector Index
- Persistent on-disk index format
- Index serialization and loading
- Memory-mapped vector storage
- Investigate page faults, memory locality, and access patterns

### v6 — Vector Quantization
- FP32 baseline
- FP16 representation
- INT8 quantization
- Measure memory reduction, search performance, and recall impact

### v7 — Batch Search & Performance Engineering
- Batched query processing
- Multi-query parallelism
- CPU cache and memory-bandwidth analysis
- Performance profiling with Linux tooling
- Scaling experiments across available CPU threads

### Long-Term Goal

Transform HyperSearch from a brute-force vector search implementation into a high-performance vector search engine that explores the interaction between:

**Algorithms → CPU computation → parallelism → memory → storage → search quality**

The project will prioritize measurable performance improvements and reproducible benchmarks over feature count.


## Roadmap

HyperSearch is being developed incrementally, with each stage focused on improving search performance, scalability, and systems-level efficiency.

### v1 — Brute-Force Vector Search
- 384-dimensional transformer embeddings
- Vector storage and retrieval
- Cosine similarity search
- Top-K retrieval
- Retrieval correctness tests
- Performance benchmarking across 1K–100K vectors

### v2 — Parallel Search
- OpenMP parallel similarity search
- Thread-scaling experiments
- Latency and throughput analysis
- CPU utilization and parallel scaling benchmarks

### v3 — SIMD Optimization
- SIMD-optimized vector similarity
- Compare scalar vs SIMD implementations
- Benchmark AVX/SSE implementations where supported
- Analyze compute throughput and memory behavior

### v4 — Approximate Nearest Neighbor Search
- Implement an HNSW-based index from first principles
- Compare brute-force and approximate search
- Measure latency, throughput, and recall
- Study search-quality vs performance tradeoffs

### v5 — Persistent Vector Index
- Persistent on-disk index format
- Index serialization and loading
- Memory-mapped vector storage
- Investigate page faults, memory locality, and access patterns

### v6 — Vector Quantization
- FP32 baseline
- FP16 representation
- INT8 quantization
- Measure memory reduction, search performance, and recall impact

### v7 — Batch Search & Performance Engineering
- Batched query processing
- Multi-query parallelism
- CPU cache and memory-bandwidth analysis
- Performance profiling with Linux tooling
- Scaling experiments across available CPU threads

### Long-Term Goal

Transform HyperSearch from a brute-force vector search implementation into a high-performance vector search engine that explores the interaction between:

**Algorithms → CPU computation → parallelism → memory → storage → search quality**

The project will prioritize measurable performance improvements and reproducible benchmarks over feature count.

