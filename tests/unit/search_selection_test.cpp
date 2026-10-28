#include "ChunkStore.h"
#include "CodeChunker.h"
#include "CodebaseScanner.h"
#include "EmbeddingStore.h"
#include "QueryAnalyzer.h"
#include "ResultFusion.hpp"
#include "SearchResultFormatter.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct TemporaryDirectory {
    fs::path path;

    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now()
            .time_since_epoch().count();
        path = fs::temp_directory_path() /
            ("hypersearch-selection-test-" + std::to_string(stamp));
        fs::create_directories(path);
    }

    ~TemporaryDirectory() { fs::remove_all(path); }
};

SearchResult result(std::string path, std::string text,
                    std::size_t first = 1, std::size_t last = 1) {
    return {std::move(text), 0.0f, std::move(path), first, last};
}

void test_scanner(const fs::path& root) {
    const auto fixture = root / "scanner-fixture";
    fs::create_directories(fixture / "src");
    fs::create_directories(fixture / "test-repos" / "nested");
    fs::create_directories(fixture / "experiments");
    std::ofstream(fixture / "src" / "answer.cpp") << "int answer = 42;\n";
    std::ofstream(fixture / "test-repos" / "nested" / "noise.cpp") << "noise\n";
    std::ofstream(fixture / "experiments" / "old.cpp") << "old\n";
    std::ofstream(fixture / "src" / "secrets.json") << "{}\n";
    std::ofstream(root / "outside.cpp") << "outside\n";
    fs::create_symlink(root / "outside.cpp", fixture / "src" / "external.cpp");
    const auto files = CodebaseScanner{}.scan(fixture);
    require(files.size() == 1 &&
            files[0].relative_path == fs::path("src/answer.cpp"),
            "scanner should skip fixture, secret, and symlinked files");
}

void test_query_analysis() {
    const auto question = analyze_query(
        "Where are embeddings generated and stored?");
    require(question.is_natural_language, "question classification");
    require(question.lexical_match.find(" OR ") != std::string::npos,
            "natural language should broaden FTS terms");
    require(question.embedding_text ==
            "Where are embeddings generated and stored?",
            "semantic branch should retain the whole question");

    const auto symbol = analyze_query("ChunkStore::search_candidates");
    require(symbol.lexical_match ==
            "\"chunkstore search candidates\"",
            "qualified symbol should stay one searchable phrase: " +
            symbol.lexical_match);
}

void test_fusion() {
    ResultFusion fusion;
    const auto a = result("a.cpp", "A");
    const auto b = result("b.cpp", "B");
    const auto c = result("c.cpp", "C");
    const auto fused = fusion.fuse({a, b, a}, {b, a, c}, 10);
    require(fused.size() == 3, "fusion should deduplicate chunks");
    require(fused[0].file_path == "a.cpp" &&
            fused[1].file_path == "b.cpp",
            "shared high ranks should beat a one-list result");
    require(fused[0].score > fused[2].score,
            "fused score should reflect evidence from both lists");
    require(fusion.fuse({a}, {b}, 0).empty(), "zero limit");

    ResultFusion one_per_file(60, 1);
    const auto limited = one_per_file.fuse(
        {a, result("a.cpp", "A2", 2, 2), b}, {}, 10);
    require(limited.size() == 2, "per-file fusion cap");
}

void test_persistent_index_and_cache(const fs::path& root) {
    const fs::path db = root / "index.sqlite3";
    ChunkStore chunks(db);
    chunks.initialize();
    EmbeddingStore embeddings(db);
    embeddings.initialize();

    const ScannedFile file{"sample.cpp", FileCategory::Source};
    CodeChunker chunker;
    auto old_chunks = chunker.chunk_file(file, "alpha token\nsecond line\n");
    require(old_chunks.size() == 1 && old_chunks[0].start_line == 1 &&
            old_chunks[0].end_line == 2, "line-numbered chunking");
    chunks.replace_file_chunks(file.relative_path, old_chunks);
    require(chunks.all_chunks().size() == 1 &&
            chunks.all_chunks()[0].content == old_chunks[0].content,
            "persisted chunks must load with their content");
    require(!chunks.file_is_current(file.relative_path, "digest-a"),
            "new file must need indexing");
    chunks.mark_file_current(file.relative_path, "digest-a");
    require(chunks.file_is_current(file.relative_path, "digest-a") &&
            !chunks.file_is_current(file.relative_path, "digest-b"),
            "fingerprint must identify unchanged content");
    require(chunks.search_candidates("\"alpha\"", 10).size() == 1,
            "FTS should find inserted chunk");

    embeddings.save(old_chunks[0], "test-model", {0.5f, 0.25f});
    auto cached = embeddings.load(old_chunks[0], "test-model");
    require(cached && *cached == std::vector<float>({0.5f, 0.25f}),
            "embedding cache round trip");
    require(!embeddings.load(old_chunks[0], "another-model"),
            "model IDs must partition the cache");

    auto changed = chunker.chunk_file(file, "beta token\nsecond line\n");
    chunks.replace_file_chunks(file.relative_path, changed);
    require(!embeddings.load(changed[0], "test-model"),
            "content update must invalidate cached vector");
    require(chunks.search_candidates("\"alpha\"", 10).empty() &&
            chunks.search_candidates("\"beta\"", 10).size() == 1,
            "FTS triggers must update the text index");

    chunks.remove_missing_files({});
    require(chunks.search_candidates("\"beta\"", 10).empty(),
            "deleted file must leave no FTS result");
    require(!chunks.file_is_current(file.relative_path, "digest-a"),
            "deleted file must leave no fingerprint");
}

void test_formatter(const fs::path& root) {
    {
        std::ofstream file(root / "answer.cpp");
        file << "void parseWidget() {\n"
                "    parse_widget(input);\n"
                "}\n";
    }
    SearchResultFormatter formatter;
    const auto query = analyze_query("where is widget parsed");
    const std::vector<SearchResult> candidates = {
        result("noise.md", "widget", 1, 1),
        result("answer.cpp", "void parseWidget() {\n    parse_widget(input);\n}\n", 1, 3),
        result("answer.cpp", "    parse_widget(input);", 2, 2),
        result("other.cpp", "widget", 1, 1),
        result("third.cpp", "widget", 1, 1),
        result("fourth.cpp", "widget", 1, 1)
    };
    const std::string output = formatter.format(candidates, query, root);
    require(output.starts_with("Most likely match\nanswer.cpp:1-3"),
            "source term coverage should outrank weaker matches");
    require(output.find("2 |     parse_widget(input);") != std::string::npos,
            "excerpt must include exact source line numbers");
    require(output.find("Likely alternative 3") != std::string::npos &&
            output.find("Likely alternative 4") == std::string::npos,
            "formatter should show at most three alternatives");
    require(output.find("answer.cpp:", output.find("answer.cpp:") + 1) ==
            std::string::npos, "formatter should show one span per file");

    {
        std::ofstream file(root / "pipeline.cpp");
        file << "auto batch = model_->embed_batch(missing_texts);\n";
    }
    {
        std::ofstream file(root / "store.cpp");
        file << "const char* sql = R\"(\n"
                "INSERT INTO chunk_embeddings\n"
                "(chunk_id, embedding) VALUES (?, ?)\n"
                ")\";\n";
    }
    {
        std::ofstream file(root / "decoy.cpp");
        file << "code.find(\"embed_batch(\");\n";
    }
    const auto lifecycle_query =
        analyze_query("where are embeddings generated and stored");
    const std::string lifecycle = formatter.format({
        result("decoy.cpp", "embed_batch", 1, 1),
        result("pipeline.cpp", "embed_batch", 1, 1),
        result("store.cpp", "INSERT INTO chunk_embeddings", 2, 2)
    }, lifecycle_query, root);
    require(lifecycle.find("Generated here\npipeline.cpp:1") == 0,
            "generated section must use executable code, not quoted text: " + lifecycle);
    require(lifecycle.find("Stored here\nstore.cpp:1-4") != std::string::npos,
            "stored section must show the persistent write");
    require(lifecycle.find("decoy.cpp") == std::string::npos,
            "quoted search patterns must not become answers");

    {
        std::ofstream file(root / "reader.cpp");
        file << "auto vector = store.load(embedding_id);\n";
    }
    const std::string three_steps = formatter.format({
        result("pipeline.cpp", "embed_batch(missing_texts)", 1, 1),
        result("store.cpp", "INSERT INTO chunk_embeddings", 2, 2),
        result("reader.cpp", "store.load(embedding_id)", 1, 1)
    }, analyze_query("where are embeddings generated, stored, and loaded"), root);
    require(three_steps.find("Generated here\npipeline.cpp:1") == 0 &&
            three_steps.find("Stored here\nstore.cpp:1-4") != std::string::npos &&
            three_steps.find("Loaded here\nreader.cpp:1") != std::string::npos,
            "three requested actions should yield three source sections: " + three_steps);

    {
        std::ofstream file(root / "files.cpp");
        file << "auto files = scanner.scan(root);\n"
                "auto chunks = chunker.chunk_files(files);\n"
                "index.insert(chunks);\n";
    }
    const std::string other_subject = formatter.format({
        result("files.cpp", "scanner.scan(root);\nchunker.chunk_files(files);\nindex.insert(chunks);", 1, 3)
    }, analyze_query("how are files scanned, chunked, and indexed"), root);
    require(other_subject.find("Scanned here\nfiles.cpp:1") == 0 &&
            other_subject.find("Chunked here\nfiles.cpp:2") != std::string::npos &&
            other_subject.find("Indexed here\nfiles.cpp:3") != std::string::npos,
            "coordinated actions should work for another subject: " + other_subject);

    const std::string distinct_subjects = formatter.format({
        result("pipeline.cpp", "embed_batch(missing_texts)", 1, 1),
        result("files.cpp", "scanner.scan(root);", 1, 1)
    }, analyze_query("where are embeddings generated and files scanned"), root);
    require(distinct_subjects.find("Generated here\npipeline.cpp:1") == 0 &&
            distinct_subjects.find("Scanned here\nfiles.cpp:1") != std::string::npos,
            "each action can have its own subject: " + distinct_subjects);

    const std::string missing_step = formatter.format({
        result("pipeline.cpp", "embed_batch(missing_texts)", 1, 1)
    }, analyze_query("where are embeddings generated and uploaded"), root);
    require(missing_step.find("Generated here\npipeline.cpp:1") == 0 &&
            missing_step.find("Uploaded here\nNo confident source match") != std::string::npos,
            "missing evidence should be explicit without dropping a requested step");
}

int main() {
    try {
        TemporaryDirectory directory;
        test_query_analysis();
        test_fusion();
        test_scanner(directory.path);
        test_persistent_index_and_cache(directory.path);
        test_formatter(directory.path);
        std::cout << "Search selection tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Search selection test failed: " << error.what() << '\n';
        return 1;
    }
}
