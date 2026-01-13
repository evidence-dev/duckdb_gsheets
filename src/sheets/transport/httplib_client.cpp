#include "duckdb/common/exception.hpp"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.hpp"

#include "sheets/transport/httplib_client.hpp"
#include "sheets/transport/http_type.hpp"

namespace duckdb {
namespace sheets {

void HttpLibClient::ParseUrl(const std::string &url, std::string &baseUrl, std::string &path) {
	const std::string schemaSep = "://";
	size_t schemeEnd = url.find(schemaSep);
	if (schemeEnd == std::string::npos) {
		throw duckdb::IOException("Invalid URL: " + url);
	}

	size_t pathStart = url.find('/', schemeEnd + schemaSep.size());
	if (pathStart == std::string::npos) {
		baseUrl = url;
		path = "/";
	} else {
		baseUrl = url.substr(0, pathStart);
		path = url.substr(pathStart);
	}
}

HttpResponse HttpLibClient::Execute(const HttpRequest &request) {
	std::string baseUrl;
	std::string path;
	ParseUrl(request.url, baseUrl, path);

	duckdb_httplib_openssl::Client client(baseUrl);

	// Extract content-type from request headers (default to application/json)
	std::string contentType = "application/json";
	duckdb_httplib_openssl::Headers headers;
	for (const auto &h : request.headers) {
		if (h.first == "Content-Type") {
			contentType = h.second;
		} else {
			headers.insert(h);
		}
	}

	duckdb_httplib_openssl::Result result;

	switch (request.method) {

	case HttpMethod::GET:
		result = client.Get(path, headers);
		break;
	case HttpMethod::POST:
		result = client.Post(path, headers, request.body, contentType);
		break;
	case HttpMethod::PUT:
		result = client.Put(path, headers, request.body, contentType);
		break;
	case HttpMethod::DELETE:
		result = client.Delete(path, headers);
		break;
	}

	HttpResponse response;

	if (!result) {
		throw duckdb::IOException("HTTP request failed: " + duckdb_httplib_openssl::to_string(result.error()));
	}

	response.statusCode = result->status;
	response.body = result->body;
	for (const auto &h : result->headers) {
		response.headers[h.first] = h.second;
	}
	return response;
}

} // namespace sheets
} // namespace duckdb
