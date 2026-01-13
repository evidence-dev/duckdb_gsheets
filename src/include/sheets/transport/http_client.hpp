#pragma once

#include "sheets/transport/http_type.hpp"

namespace duckdb {
namespace sheets {

class IHttpClient {
public:
	virtual ~IHttpClient() = default;
	virtual HttpResponse Execute(const HttpRequest &request) = 0;

	HttpRequest BuildRequest(const HttpMethod method, const std::string &url, const HttpHeaders &headers);
	HttpRequest BuildRequest(const HttpMethod method, const std::string &url, const HttpHeaders &headers,
	                         const std::string &body);

	HttpResponse Get(const std::string &url, const HttpHeaders &headers);
	HttpResponse Post(const std::string &url, const HttpHeaders &headers, const std::string &body);
	HttpResponse Put(const std::string &url, const HttpHeaders &headers, const std::string &body);
	HttpResponse Delete(const std::string &url, const HttpHeaders &headers);
};
} // namespace sheets
} // namespace duckdb
