#pragma once

#include "utils/version.hpp"

#include "sheets/auth/auth_provider.hpp"
#include "sheets/resources/spreadsheet.hpp"
#include "sheets/transport/http_client.hpp"
#include "sheets/transport/http_type.hpp"

namespace duckdb {
namespace sheets {

constexpr const char *DEFAULT_SHEETS_API_URL = "https://sheets.googleapis.com/v4";

class GoogleSheetsClient {
public:
	GoogleSheetsClient(IHttpClient &http, IAuthProvider &auth, const std::string &baseUrl = DEFAULT_SHEETS_API_URL)
	    : http(http), headers(BuildHeaders(auth)), baseUrl(baseUrl) {
	}

	SpreadsheetResource Spreadsheets(const std::string &spreadsheetId) {
		return SpreadsheetResource(http, headers, baseUrl, spreadsheetId);
	}

private:
	IHttpClient &http;
	HttpHeaders headers;
	std::string baseUrl;

	static HttpHeaders BuildHeaders(IAuthProvider &auth) {
		HttpHeaders h;
		h["Authorization"] = auth.GetAuthorizationHeader();
		h["Content-Type"] = "application/json";
		h["Accept"] = "application/json";

		std::string version = getVersion();
		h["User-Agent"] = "duckdb-gsheets/" + (version.empty() ? "dev" : version);

		return h;
	}
};
} // namespace sheets
} // namespace duckdb
