cmake_minimum_required(VERSION 3.20)
project(HyperSearch LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
option(HYPERSEARCH_CORE_ONLY "Build model-free search tests only" OFF)

if(HYPERSEARCH_CORE_ONLY)
    find_package(SQLite3 REQUIRED)
    find_package(OpenMP REQUIRED)
    add_executable(search_selection_tests
        tests/search_selection_test.cpp
        src/indexer/CodeChunker.cpp
        src/indexer/ChunkStore.cpp
        src/embeddings/EmbeddingStore.cpp
        src/query/QueryAnalyzer.cpp
        src/query/ResultFusion.cpp
        src/output/SearchResultFormatter.cpp
    )
    target_include_directories(search_selection_tests PRIVATE
        src/indexer src/embeddings src/query src/output
        src/vectorstore src/embedding
    )
    target_link_libraries(search_selection_tests PRIVATE SQLite::SQLite3)
    add_test(NAME search_selection COMMAND search_selection_tests)
    add_executable(vector_store_tests
        tests/vector_store_test.cpp
        src/embedding/Embedding.cpp
        src/vectorstore/VectorStore.cpp
    )
    target_include_directories(vector_store_tests PRIVATE
        src/embedding src/vectorstore src/similarity
    )
    target_link_libraries(vector_store_tests PRIVATE OpenMP::OpenMP_CXX)
    add_test(NAME vector_store COMMAND vector_store_tests)
    add_executable(search_selection_bench
        benchmarks/search_bench.cpp
        src/query/QueryAnalyzer.cpp
        src/query/ResultFusion.cpp
        src/output/SearchResultFormatter.cpp
    )
    target_include_directories(search_selection_bench PRIVATE
        src/query src/output src/vectorstore src/embedding
    )
    return()
endif()

find_package(SQLite3 REQUIRED)
find_package(OpenMP REQUIRED)

set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime")
# The bundled package config expects an include/onnxruntime directory, while
# this copy places its headers directly in include/.
find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime
    PATHS "${ONNXRUNTIME_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)

# This checkout contains the built tokenizer archives, but not the Rust
# sources required by tokenizers-cpp's CMake project. Use the archives
# directly so a normal build does not try to run Cargo in a missing directory.
set(TOKENIZERS_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tokenizers-cpp/build")
foreach(TOKENIZERS_ARCHIVE libtokenizers_cpp.a libtokenizers_c.a)
    if(NOT EXISTS "${TOKENIZERS_BUILD_DIR}/${TOKENIZERS_ARCHIVE}")
        message(FATAL_ERROR "Missing ${TOKENIZERS_BUILD_DIR}/${TOKENIZERS_ARCHIVE}")
    endif()
endforeach()
add_library(tokenizers_cpp STATIC IMPORTED)
set_target_properties(tokenizers_cpp PROPERTIES
    IMPORTED_LOCATION "${TOKENIZERS_BUILD_DIR}/libtokenizers_cpp.a"
    INTERFACE_LINK_LIBRARIES "${TOKENIZERS_BUILD_DIR}/libtokenizers_c.a;${CMAKE_DL_LIBS}"
)

add_library(hypersearch_lib STATIC
    src/output/SearchResultFormatter.cpp
    src/indexer/CodebaseScanner.cpp
    src/indexer/CodeChunker.cpp
    src/indexer/ChunkStore.cpp
    src/embedding/Embedding.cpp
    src/embeddingmodel/EmbeddingModel.cpp
    src/vectorstore/VectorStore.cpp
    src/embeddings/EmbeddingStore.cpp
    src/embeddings/EmbeddingWorker.cpp
    src/embeddings/EmbeddingPipeline.cpp
    src/query/ResultFusion.cpp
    src/query/LexicalSearch.cpp
    src/query/SemanticSearch.cpp
    src/query/QueryAnalyzer.cpp
)

target_include_directories(hypersearch_lib
    PUBLIC
        src/output
        src/indexer
        src/embedding
        src/embeddingmodel
        src/embeddings
        src/query
        src/similarity
        src/vectorstore
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

add_executable(hypersearch src/main.cpp)
target_link_libraries(hypersearch PRIVATE hypersearch_lib)

add_executable(scanner_demo src/indexer/scanner_demo.cpp)
target_link_libraries(scanner_demo PRIVATE hypersearch_lib)

add_executable(query_analyzer_tests src/query/QueryAnalyzerTests.cpp)
target_link_libraries(query_analyzer_tests PRIVATE hypersearch_lib)
add_test(NAME query_analyzer COMMAND query_analyzer_tests)

add_executable(search_selection_tests tests/search_selection_test.cpp)
target_link_libraries(search_selection_tests PRIVATE hypersearch_lib)
add_test(NAME search_selection COMMAND search_selection_tests)

add_executable(vector_store_tests tests/vector_store_test.cpp)
target_link_libraries(vector_store_tests PRIVATE hypersearch_lib)
add_test(NAME vector_store COMMAND vector_store_tests)

add_executable(search_selection_bench benchmarks/search_bench.cpp)
target_link_libraries(search_selection_bench PRIVATE hypersearch_lib)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME cli_smoke COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.py
        $<TARGET_FILE:hypersearch>
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
