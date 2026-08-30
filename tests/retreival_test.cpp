#include "EmbeddingModel.hpp"
#include "VectorStore.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


// A document from the test corpus.
struct TestDocument {
    std::string id;
    std::string text;
};


// A query and the document we expect it to retrieve.
struct TestQuery {
    std::string id;
    std::string text;
    std::string expected_id;
};


// Load documents from the corpus file.
std::vector<TestDocument> load_corpus(
    const std::string& path)
{
    std::ifstream file(path);

    if (!file)
        throw std::runtime_error(
            "Could not open corpus"
        );

    std::vector<TestDocument> documents;

    std::string line;
    std::string id;
    std::string text;

    while (std::getline(file, line)) {

        // A new [DOC_x] marker starts a document.
        if (line.rfind("[DOC_", 0) == 0) {

            // Save the previous document.
            if (!id.empty()) {
                documents.push_back({
                    id,
                    text
                });

                text.clear();
            }

            // Remove '[' and ']' from the ID.
            id = line.substr(
                1,
                line.size() - 2
            );

            continue;
        }

        // Append non-empty lines to the document.
        if (!line.empty())
            text += line;
    }


    // Save the final document.
    if (!id.empty()) {
        documents.push_back({
            id,
            text
        });
    }

    return documents;
}


// Load queries in: ID|query|expected document format.
std::vector<TestQuery> load_queries(
    const std::string& path)
{
    std::ifstream file(path);

    if (!file)
        throw std::runtime_error(
            "Could not open queries"
        );

    std::vector<TestQuery> queries;
    std::string line;

    while (std::getline(file, line)) {

        if (line.empty())
            continue;

        std::stringstream stream(line);

        TestQuery query;

        std::getline(
            stream,
            query.id,
            '|'
        );

        std::getline(
            stream,
            query.text,
            '|'
        );

        std::getline(
            stream,
            query.expected_id,
            '|'
        );

        queries.push_back(query);
    }

    return queries;
}


int main()
{
    // Load the embedding model.
    EmbeddingModel model(
        "third_party/models/all-MiniLM-L6-v2/tokenizer.json"
    );

    VectorStore store;


    // Load the test corpus.
    auto documents =
        load_corpus("tests/corpus.txt");

    std::cout
        << "Documents: "
        << documents.size()
        << '\n';


    // Keep the original document ID associated
    // with the text stored in VectorStore.
    std::vector<
        std::pair<std::string, std::string>
    > id_lookup;


    // Embed every document and add it to the store.
    for (const auto& document : documents) {

        Embedding embedding =
            model.embed(document.text);

        store.add(
            document.text,
            embedding
        );

        id_lookup.push_back({
            document.text,
            document.id
        });
    }


    // Load evaluation queries.
    auto queries =
        load_queries("tests/queries.txt");

    std::cout
        << "Queries: "
        << queries.size()
        << "\n\n";


    int recall_at_1 = 0;
    int recall_at_5 = 0;

    double reciprocal_rank_sum = 0.0;


    // Evaluate every query.
    for (const auto& query : queries) {

        // Convert the query into an embedding.
        Embedding query_embedding =
            model.embed(query.text);

        // Retrieve the five highest-scoring documents.
        auto results =
            store.search_top_k(
                query_embedding,
                5
            );


        int rank = 0;


        // Find the expected document in the results.
        for (const auto& result : results) {

            ++rank;

            auto it = std::find_if(
                id_lookup.begin(),
                id_lookup.end(),
                [&](const auto& entry)
                {
                    return entry.first == result.text;
                }
            );


            if (it != id_lookup.end() &&
                it->second == query.expected_id) {

                // Expected document was ranked first.
                if (rank == 1)
                    ++recall_at_1;

                // Expected document appeared in top 5.
                ++recall_at_5;

                // Reciprocal rank = 1 / position.
                reciprocal_rank_sum +=
                    1.0 / rank;

                break;
            }
        }


        // Print the result for this query.
        std::cout
            << query.id
            << " -> ";

        if (rank == 0)
            std::cout << "MISS";
        else
            std::cout
                << "rank "
                << rank;

        std::cout << '\n';
    }


    // Convert counts into retrieval metrics.
    double total =
        static_cast<double>(queries.size());

    double recall1 =
        recall_at_1 / total;

    double recall5 =
        recall_at_5 / total;

    double mrr =
        reciprocal_rank_sum / total;


    std::cout
        << "\n"
        << "============================\n"
        << "HyperSearch Retrieval Test\n"
        << "============================\n";


    std::cout
        << "Recall@1: "
        << recall1 * 100
        << "%\n";

    std::cout
        << "Recall@5: "
        << recall5 * 100
        << "%\n";

    std::cout
        << "MRR: "
        << mrr
        << '\n';
}
