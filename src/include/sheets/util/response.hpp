#pragma once

#include "json.hpp"

#include "sheets/exception.hpp"
#include "sheets/transport/http_type.hpp"

namespace duckdb {
namespace sheets {

template <typename T>
T ParseResponse(const HttpResponse &response) {
	if (response.statusCode != 200) {
		throw SheetsApiException(response.statusCode, response.body);
	}
	try {
		return nlohmann::json::parse(response.body).get<T>();
	} catch (const nlohmann::json::exception &e) {
		throw SheetsParseException("Failed to parse response: " + std::string(e.what()));
	}
}

} // namespace sheets
} // namespace duckdb
