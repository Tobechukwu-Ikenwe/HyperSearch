#include "VectorStore.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        VectorStore store;
        store.add("x axis", Embedding({1.0f, 0.0f, 0.0f}), "x.cpp", 2, 4);
        store.add("y axis", Embedding({0.0f, 1.0f, 0.0f}), "y.cpp", 8, 9);
        store.add("opposite y", Embedding({0.0f, -1.0f, 0.0f}), "z.cpp", 1, 1);

        const auto ranked = store.search_top_k(
            Embedding({0.0f, 1.0f, 0.0f}), 2);
        require(ranked.size() == 2, "top-K count");
        require(ranked[0].file_path == "y.cpp" &&
                ranked[0].start_line == 8 && ranked[0].end_line == 9,
                "best cosine match and source metadata");
        require(ranked[0].score > ranked[1].score,
                "cosine ranking order");

        store.replace_file("y.cpp", {
            StoredDocument{"updated", Embedding({0.0f, 0.0f, 1.0f}),
                           "y.cpp", 10, 11}
        });
        const auto updated = store.search_top_k(
            Embedding({0.0f, 0.0f, 1.0f}), 1);
        require(updated[0].file_path == "y.cpp" &&
                updated[0].start_line == 10,
                "file replacement must refresh vector and location");

        store.remove_file("y.cpp");
        require(store.size() == 2, "file removal");
        store.clear();
        require(store.size() == 0, "clear");
        std::cout << "Vector store tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Vector store test failed: " << error.what() << '\n';
        return 1;
    }
}
