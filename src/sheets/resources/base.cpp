#include "sheets/resources/base.hpp"
#include "sheets/transport/http_type.hpp"

namespace duckdb {
namespace sheets {

HttpResponse BaseResource::DoGet(const std::string &path) {
	HttpRequest req;
	req.url = baseUrl + path;
	req.method = HttpMethod::GET;
	req.headers = headers;
	return http.Execute(req);
}

HttpResponse BaseResource::DoPost(const std::string &path, const std::string &body) {
	HttpRequest req;
	req.url = baseUrl + path;
	req.method = HttpMethod::POST;
	req.headers = headers;
	req.body = body;
	return http.Execute(req);
}

HttpResponse BaseResource::DoPut(const std::string &path, const std::string &body) {
	HttpRequest req;
	req.url = baseUrl + path;
	req.method = HttpMethod::PUT;
	req.headers = headers;
	req.body = body;
	return http.Execute(req);
}

} // namespace sheets
} // namespace duckdb
