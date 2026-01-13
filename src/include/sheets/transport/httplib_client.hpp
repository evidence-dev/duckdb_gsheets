#pragma once

#include "sheets/transport/http_client.hpp"

namespace duckdb {
namespace sheets {

class HttpLibClient : public IHttpClient {
public:
	HttpResponse Execute(const HttpRequest &request) override;

private:
	void ParseUrl(const std::string &url, std::string &baseUrl, std::string &path);
};
} // namespace sheets
} // namespace duckdb
