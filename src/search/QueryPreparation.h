#pragma once

#include <string>

// Trim outer whitespace and return an empty string if no query remains.
std::string prepare_query(const std::string& query);
