#include "gsheets_requests.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/http_util.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/database.hpp"
#include <chrono>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <thread>

namespace duckdb {


std::string perform_https_request(ClientContext &context, const std::string &host, const std::string &path,
                                  const std::string &token, HttpMethod method, const std::string &body,
                                  const std::string &content_type) {
      auto &db = DatabaseInstance::GetDatabase(context);

       HTTPHeaders headers(db);
       headers.Insert("Host", StringUtil::Format("%s", "http://127.0.0.1:8080/"));
      headers.Insert("Authorization", StringUtil::Format("Bearer %s", token));
       // headers.Insert("Connection", StringUtil::Format("close"));

       if (!body.empty()) {
               headers.Insert("Content-Type", StringUtil::Format("%s", content_type));
               headers.Insert("Content-Length", StringUtil::Format("%s", std::to_string(body.length())));
       }
       auto &http_util = HTTPUtil::Get(db);
      unique_ptr<HTTPParams> params;
       std::string x = string("https://") + host + path;
       params = http_util.InitializeParameters(context, x);
       // Unclear what's peculiar about extension install flow, but those two parameters are needed
       // to avoid lengthy retry on 304
       params->follow_location = false;
       params->keep_alive = false;

      switch (method) {
       case HttpMethod::GET: {

               GetRequestInfo get_request(x, headers, *params, nullptr, nullptr);
               get_request.try_request = true;

               auto response = http_util.Request(get_request);

               return response->body;

              break;
       }
       case HttpMethod::POST:
       case HttpMethod::PUT:
               throw IOException("POST or PUT not implemented");
              break;
       }

       // TODO: refactor HTTPS to catch more than just rate limit exceeded or else
       //       this could give false positives.
       throw duckdb::IOException("Google Sheets API rate limit exceeded");
}

std::string call_sheets_api(ClientContext &context, const std::string &spreadsheet_id, const std::string &token, const std::string &sheet_name,
                            const std::string &sheet_range, HttpMethod method, const std::string &body) {
	std::string host = "sheets.googleapis.com";
	std::string path = "/v4/spreadsheets/" + spreadsheet_id + "/values/" + sheet_name;

	if (!sheet_range.empty()) {
		path += "!" + sheet_range;
	}

	if (method == HttpMethod::POST) {
		path += ":append";
		path += "?valueInputOption=USER_ENTERED";
	}

	return perform_https_request(context, host, path, token, method, body);
}

std::string delete_sheet_data(ClientContext &context, const std::string &spreadsheet_id, const std::string &token,
                              const std::string &sheet_name, const std::string &sheet_range) {
	std::string host = "sheets.googleapis.com";
	std::string sheet_and_range = sheet_range.empty() ? sheet_name : sheet_name + "!" + sheet_range;
	std::string path = "/v4/spreadsheets/" + spreadsheet_id + "/values/" + sheet_and_range + ":clear";

	return perform_https_request(context, host, path, token, HttpMethod::POST, "{}");
}

std::string get_spreadsheet_metadata(ClientContext &context, const std::string &spreadsheet_id, const std::string &token) {
	std::string host = "sheets.googleapis.com";
	std::string path = "/v4/spreadsheets/" + spreadsheet_id + "?&fields=sheets.properties";
	return perform_https_request(context, host, path, token, HttpMethod::GET, "");
}
} // namespace duckdb
