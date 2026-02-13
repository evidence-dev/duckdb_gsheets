#pragma once

#include <utility>

#include "sheets/transport/http_client.hpp"
#include "sheets/transport/http_type.hpp"

namespace duckdb {
namespace sheets {

class HttpLibClient : public IHttpClient {
public:
	explicit HttpLibClient(HttpProxyConfig proxy_config) : proxy_config(std::move(proxy_config)) {
	}

	HttpResponse Execute(const HttpRequest &request) override;

private:
	void ParseUrl(const std::string &url, std::string &baseUrl, std::string &path);

	HttpProxyConfig proxy_config;
};
} // namespace sheets
} // namespace duckdb
