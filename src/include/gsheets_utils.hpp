#pragma once

#include <string>

namespace duckdb {

std::string extract_spreadsheet_id(const std::string &input);
std::string extract_sheet_id(const std::string &input);
std::string extract_sheet_range(const std::string &input);
std::string generate_random_string(size_t length);
std::string url_encode(const std::string &str);

} // namespace duckdb
