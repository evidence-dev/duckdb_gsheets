#include "sheets/transport/mock_http_client.hpp"
#include <stdexcept>

namespace duckdb {
namespace sheets {

HttpResponse MockHttpClient::Execute(const HttpRequest &request) {
	recordedRequests.push_back(request);
	if (responseIndex < responses.size()) {
		return responses[responseIndex++];
	}
	throw std::runtime_error("MockHttpClient: No more responses queued");
}

void MockHttpClient::AddResponse(HttpResponse response) {
	responses.push_back(std::move(response));
}

const std::vector<HttpRequest> &MockHttpClient::GetRecordedRequests() const {
	return recordedRequests;
}
} // namespace sheets
} // namespace duckdb
