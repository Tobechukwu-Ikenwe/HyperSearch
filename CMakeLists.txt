cmake_minimum_required(VERSION 3.20)

project(HyperSearch LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------

find_package(OpenMP REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(
    ONNXRUNTIME
    REQUIRED
    IMPORTED_TARGET
    libonnxruntime
)


# ------------------------------------------------------------
# Core library
# ------------------------------------------------------------

add_library(hypersearch_lib STATIC

    src/embedding/Embedding.cpp
    src/embeddingmodel/EmbeddingModel.cpp
    src/vectorstore/VectorStore.cpp
)


# ------------------------------------------------------------
# Header locations
# ------------------------------------------------------------

target_include_directories(hypersearch_lib PUBLIC

    ${CMAKE_CURRENT_SOURCE_DIR}/src/embedding
    ${CMAKE_CURRENT_SOURCE_DIR}/src/embeddingmodel
    ${CMAKE_CURRENT_SOURCE_DIR}/src/similarity
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vectorstore

    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tokenizers-cpp/include
)


# ------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------

target_link_libraries(hypersearch_lib PUBLIC

    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tokenizers-cpp/build/libtokenizers_cpp.a

    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tokenizers-cpp/build/libtokenizers_c.a

    PkgConfig::ONNXRUNTIME

    OpenMP::OpenMP_CXX
)


# ------------------------------------------------------------
# Main program
# ------------------------------------------------------------

add_executable(
    hypersearch
    src/main.cpp
)

target_link_libraries(
    hypersearch
    PRIVATE
    hypersearch_lib
)


# ------------------------------------------------------------
# Retrieval test
# ------------------------------------------------------------

add_executable(
    retreival_test
    tests/retreival_test.cpp
)

target_link_libraries(
    retreival_test
    PRIVATE
    hypersearch_lib
)


# ------------------------------------------------------------
# Performance test
# ------------------------------------------------------------

add_executable(
    performance_test
    tests/performance_test.cpp
)

target_link_libraries(
    performance_test
    PRIVATE
    hypersearch_lib
)
