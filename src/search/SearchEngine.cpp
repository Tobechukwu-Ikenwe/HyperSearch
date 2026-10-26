#include "SearchEngine.h"
#include "QueryPreparation.h"

std::string SearchEngine::prepare_search(
    const std::string& query,
    std::size_t limit
) const {
    if (limit == 0) {
        return {};
    }

    return prepare_query(query);
}
