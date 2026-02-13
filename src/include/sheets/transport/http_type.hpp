#pragma once

#include <cstdint>
#include <string>
#include <map>

namespace duckdb {
namespace sheets {

enum class HttpMethod { GET, POST, PUT, DEL };

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

struct HttpProxyConfig {
	std::string host;
	uint16_t port = 0;
	std::string username;
	std::string password;
};

} // namespace sheets
} // namespace duckdb
