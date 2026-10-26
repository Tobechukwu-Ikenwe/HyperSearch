#pragma once

#include <sqlite3.h>
#include <vector>

class CandidateRetriever {
public:
    explicit CandidateRetriever(sqlite3* db);

    // Return matching chunk IDs, ordered by lexical relevance.
    std::vector<int> retrieve(const char* fts_query, int limit);

private:
    sqlite3* db_;
};
