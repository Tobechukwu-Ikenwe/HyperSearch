#include "SearchEngine.h"

#include <string>

namespace {

// Treat user input as an FTS5 phrase, not as FTS5 query syntax.
std::string make_fts_phrase(const std::string& query) {
    std::string phrase;
    phrase.reserve(query.size() + 2);
    phrase.push_back('"');

    for (char character : query) {
        if (character == '"') {
            phrase.push_back('"');
        }
        phrase.push_back(character);
    }

    phrase.push_back('"');
    return phrase;
}

}  // namespace

SearchEngine::SearchEngine(ChunkStore& store)
    : store_(store) {}

std::vector<CodeChunk> SearchEngine::search(
    const std::string& query,
    std::size_t limit
) {
    if (query.empty() || limit == 0) {
        return {};
    }

    return store_.search_candidates(make_fts_phrase(query), limit);
}
