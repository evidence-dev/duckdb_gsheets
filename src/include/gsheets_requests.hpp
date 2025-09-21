#pragma once

#include <string>

namespace duckdb {

class ClientContext;

enum class HttpMethod { GET, POST, PUT };

std::string perform_https_request(ClientContext &context, const std::string &host, const std::string &path, const std::string &token,
                                  HttpMethod method = HttpMethod::GET, const std::string &body = "",
                                  const std::string &content_type = "application/json");

std::string call_sheets_api(ClientContext &context, const std::string &spreadsheet_id, const std::string &token, const std::string &sheet_name,
                            const std::string &sheet_range, HttpMethod method = HttpMethod::GET,
                            const std::string &body = "");

std::string delete_sheet_data(ClientContext &context, const std::string &spreadsheet_id, const std::string &token,
                              const std::string &sheet_name, const std::string &sheet_range);

std::string get_spreadsheet_metadata(const std::string &spreadsheet_id, const std::string &token);
} // namespace duckdb
