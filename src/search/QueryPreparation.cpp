#include "QueryPreparation.h"

#include <string>

namespace {

// Remove whitespace from the beginning and end of the query.
std::string trim_query(const std::string& query) {
    const auto first = query.find_first_not_of(" \t\n\r");

    if (first == std::string::npos) {
        return {};
    }

    const auto last = query.find_last_not_of(" \t\n\r");
    return query.substr(first, last - first + 1);
}

}  // namespace

std::string prepare_query(const std::string& query) {
    std::string normalized = trim_query(query);

    if (normalized.empty()) {
        return {};
    }

    return normalized;
}
