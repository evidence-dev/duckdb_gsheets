#pragma once
#include <string>

#include "sheets/transport/http_client.hpp"
#include "sheets/transport/http_type.hpp"

namespace duckdb {
namespace sheets {

class BaseResource {
protected:
	BaseResource(IHttpClient &http, const HttpHeaders &headers, const std::string &baseUrl)
	    : http(http), headers(headers), baseUrl(baseUrl) {};

	IHttpClient &http;
	const HttpHeaders &headers;
	std::string baseUrl;

	HttpResponse DoGet(const std::string &path);
	HttpResponse DoPost(const std::string &path, const std::string &body);
	HttpResponse DoPut(const std::string &path, const std::string &body);
};

} // namespace sheets
} // namespace duckdb
