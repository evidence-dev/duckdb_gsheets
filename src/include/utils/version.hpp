#pragma once

#include <string>

namespace duckdb {

/**
 * Retrieves version from macro if present or empty string if not
 */
std::string getVersion();

} // namespace duckdb
