cmake_minimum_required(VERSION 3.20)
project(HyperSearch LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HYPERSEARCH_STRICT_WARNINGS "Treat warnings in project code as errors" ON)
function(hypersearch_warnings target)
    if(HYPERSEARCH_STRICT_WARNINGS)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

include(CTest)
option(HYPERSEARCH_CORE_ONLY "Build model-free search tests only" OFF)

if(HYPERSEARCH_CORE_ONLY)
    find_package(SQLite3 REQUIRED)
    find_package(OpenMP REQUIRED)
    add_executable(search_selection_tests
        tests/unit/search_selection_test.cpp
        src/indexer/CodebaseScanner.cpp
        src/indexer/CodeChunker.cpp
        src/storage/ChunkStore.cpp
        src/storage/EmbeddingStore.cpp
        src/search/QueryAnalyzer.cpp
        src/search/ResultFusion.cpp
        src/output/SearchResultFormatter.cpp
    )
    target_include_directories(search_selection_tests PRIVATE
        src/indexer src/storage src/embedding src/search src/output
    )
    target_link_libraries(search_selection_tests PRIVATE SQLite::SQLite3)
    hypersearch_warnings(search_selection_tests)
    add_test(NAME search_selection COMMAND search_selection_tests)
    add_executable(vector_store_tests
        tests/unit/vector_store_test.cpp
        src/embedding/Embedding.cpp
        src/embedding/VectorStore.cpp
    )
    target_include_directories(vector_store_tests PRIVATE
        src/embedding
    )
    target_link_libraries(vector_store_tests PRIVATE OpenMP::OpenMP_CXX)
    hypersearch_warnings(vector_store_tests)
    add_test(NAME vector_store COMMAND vector_store_tests)
    add_executable(search_selection_bench
        benchmarks/search_bench.cpp
        src/search/QueryAnalyzer.cpp
        src/search/ResultFusion.cpp
        src/output/SearchResultFormatter.cpp
    )
    target_include_directories(search_selection_bench PRIVATE
        src/search src/output src/embedding
    )
    hypersearch_warnings(search_selection_bench)
    return()
endif()

find_package(SQLite3 REQUIRED)
find_package(OpenMP REQUIRED)

set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime")
# The bundled package config expects an include/onnxruntime directory, while
# this copy places its headers directly in include/.
find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime
    PATHS "${ONNXRUNTIME_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)

add_subdirectory(third_party/tokenizers-cpp EXCLUDE_FROM_ALL)

add_library(hypersearch_lib STATIC
    src/output/SearchResultFormatter.cpp
    src/indexer/CodebaseScanner.cpp
    src/indexer/CodeChunker.cpp
    src/storage/ChunkStore.cpp
    src/embedding/Embedding.cpp
    src/embedding/EmbeddingModel.cpp
    src/embedding/VectorStore.cpp
    src/storage/EmbeddingStore.cpp
    src/embedding/EmbeddingWorker.cpp
    src/embedding/EmbeddingPipeline.cpp
    src/search/ResultFusion.cpp
    src/search/LexicalSearch.cpp
    src/search/SemanticSearch.cpp
    src/search/QueryAnalyzer.cpp
)

target_include_directories(hypersearch_lib
    PUBLIC
        src/output
        src/indexer
        src/storage
        src/embedding
        src/search
        third_party/tokenizers-cpp/include
        ${ONNXRUNTIME_ROOT}/include
)

target_link_libraries(hypersearch_lib
    PUBLIC
        SQLite::SQLite3
        ${ONNXRUNTIME_LIBRARY}
        tokenizers_cpp
        OpenMP::OpenMP_CXX
)
hypersearch_warnings(hypersearch_lib)

add_executable(hypersearch src/main.cpp)
target_link_libraries(hypersearch PRIVATE hypersearch_lib)
hypersearch_warnings(hypersearch)
set_target_properties(hypersearch PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
install(TARGETS hypersearch RUNTIME DESTINATION bin)
install(DIRECTORY "${ONNXRUNTIME_ROOT}/lib/" DESTINATION lib
    FILES_MATCHING PATTERN "libonnxruntime*.so*")

set(MODEL_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/models/all-MiniLM-L6-v2")
set(MODEL_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/models/all-MiniLM-L6-v2")
file(MAKE_DIRECTORY "${MODEL_OUTPUT}/onnx")
configure_file("${MODEL_SOURCE}/tokenizer.json" "${MODEL_OUTPUT}/tokenizer.json" COPYONLY)
configure_file("${MODEL_SOURCE}/onnx/model.onnx" "${MODEL_OUTPUT}/onnx/model.onnx" COPYONLY)
install(FILES "${MODEL_SOURCE}/tokenizer.json"
    DESTINATION bin/models/all-MiniLM-L6-v2)
install(FILES "${MODEL_SOURCE}/onnx/model.onnx"
    DESTINATION bin/models/all-MiniLM-L6-v2/onnx)
install(FILES "${ONNXRUNTIME_ROOT}/LICENSE"
    "${ONNXRUNTIME_ROOT}/ThirdPartyNotices.txt"
    DESTINATION share/doc/hypersearch/third_party/onnxruntime)
install(FILES "${MODEL_SOURCE}/README.md"
    DESTINATION share/doc/hypersearch/third_party/all-MiniLM-L6-v2)
# The model card declares Apache-2.0. The tokenizer submodule carries its text.
install(FILES third_party/tokenizers-cpp/LICENSE
    DESTINATION share/doc/hypersearch/third_party/all-MiniLM-L6-v2)
install(FILES third_party/tokenizers-cpp/LICENSE
    DESTINATION share/doc/hypersearch/third_party/tokenizers-cpp)

add_executable(query_analyzer_tests tests/unit/query_analyzer_test.cpp)
target_link_libraries(query_analyzer_tests PRIVATE hypersearch_lib)
hypersearch_warnings(query_analyzer_tests)
add_test(NAME query_analyzer COMMAND query_analyzer_tests)

add_executable(search_selection_tests tests/unit/search_selection_test.cpp)
target_link_libraries(search_selection_tests PRIVATE hypersearch_lib)
hypersearch_warnings(search_selection_tests)
add_test(NAME search_selection COMMAND search_selection_tests)

add_executable(vector_store_tests tests/unit/vector_store_test.cpp)
target_link_libraries(vector_store_tests PRIVATE hypersearch_lib)
hypersearch_warnings(vector_store_tests)
add_test(NAME vector_store COMMAND vector_store_tests)

add_executable(search_selection_bench benchmarks/search_bench.cpp)
target_link_libraries(search_selection_bench PRIVATE hypersearch_lib)
hypersearch_warnings(search_selection_bench)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME cli_smoke COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/cli_smoke.py
        $<TARGET_FILE:hypersearch>
    )
    add_test(NAME cli_batch_smoke COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/cli_batch_smoke.py
        $<TARGET_FILE:hypersearch>
    )
endif()
