#include "sheets/transport/http_client.hpp"
#include "sheets/transport/http_type.hpp"

namespace duckdb {
namespace sheets {

HttpRequest IHttpClient::BuildRequest(const HttpMethod method, const std::string &url, const HttpHeaders &headers) {
	return HttpRequest {method, url, headers, ""};
}

HttpRequest IHttpClient::BuildRequest(const HttpMethod method, const std::string &url, const HttpHeaders &headers,
                                      const std::string &body) {
	return HttpRequest {method, url, headers, body};
}

HttpResponse IHttpClient::Get(const std::string &url, const HttpHeaders &headers) {
	return Execute(BuildRequest(HttpMethod::GET, url, headers));
}

HttpResponse IHttpClient::Post(const std::string &url, const HttpHeaders &headers, const std::string &body) {
	return Execute(BuildRequest(HttpMethod::POST, url, headers, body));
}

HttpResponse IHttpClient::Put(const std::string &url, const HttpHeaders &headers, const std::string &body) {
	return Execute(BuildRequest(HttpMethod::PUT, url, headers, body));
}

HttpResponse IHttpClient::Delete(const std::string &url, const HttpHeaders &headers) {
	return Execute(BuildRequest(HttpMethod::DELETE, url, headers));
}
} // namespace sheets
} // namespace duckdb
