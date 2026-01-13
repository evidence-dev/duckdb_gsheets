#pragma once

#include <vector>

#include "sheets/transport/http_client.hpp"
#include "sheets/transport/http_type.hpp"

namespace duckdb {
namespace sheets {

class MockHttpClient : public IHttpClient {
public:
	HttpResponse Execute(const HttpRequest &request) override;
	void AddResponse(HttpResponse response);
	const std::vector<HttpRequest> &GetRecordedRequests() const;

private:
	size_t responseIndex = 0;
	std::vector<HttpResponse> responses;
	std::vector<HttpRequest> recordedRequests;
};
} // namespace sheets
} // namespace duckdb
