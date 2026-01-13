#pragma once

#include <string>
#include <map>

namespace duckdb {
namespace sheets {

enum class HttpMethod { GET, POST, PUT, DELETE };

using HttpHeaders = std::map<std::string, std::string>;

struct HttpRequest {
	HttpMethod method;
	std::string url;
	HttpHeaders headers;
	std::string body;
};

struct HttpResponse {
	int statusCode;
	HttpHeaders headers;
	std::string body;
};
} // namespace sheets
} // namespace duckdb
