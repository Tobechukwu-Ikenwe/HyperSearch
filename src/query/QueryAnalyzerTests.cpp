#include "QueryAnalyzer.h"

#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    {
        const auto query = analyze_query("sqlite3_open");
        require(query.embedding_text == "sqlite3_open", "embedding text");
        require(query.lexical_match == "\"sqlite3 open\"", "underscore split");
    }

    {
        const auto query = analyze_query("ChunkStore::search_candidates");
        require(query.lexical_match ==
                "\"chunkstore search candidates\"", "qualified symbol");
    }

    {
        const auto query = analyze_query("database connection");
        require(query.lexical_match ==
                "\"database\" AND \"connection\"", "keyword AND");
    }

    {
        const auto query =
            analyze_query("How does the database find a connection?");
        require(query.is_natural_language, "natural language classification");
        require(query.lexical_match.find(" OR ") != std::string::npos,
                "natural language OR");
    }

    {
        const auto query = analyze_query("   ");
        require(query.embedding_text.empty(), "blank embedding text");
        require(query.lexical_match.empty(), "blank lexical text");
    }

    {
        const auto query =
            analyze_query("\"; DROP TABLE chunks; --");
        require(query.lexical_match ==
                "\"drop\" OR \"table\" OR \"chunks\"",
                "query punctuation must not become FTS syntax");
    }

    std::cout << "QueryAnalyzer checks passed\n";
}
