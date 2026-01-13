#include "sheets/transport/duckdb_http_client.hpp"
#include "duckdb/common/exception.hpp"

namespace duckdb {
namespace sheets {

DuckDBHttpClient::DuckDBHttpClient(DatabaseInstance &db) {
	throw NotImplementedException("DuckDBHttpClient: Waiting for HTTPUtil POST support");
}

DuckDBHttpClient::DuckDBHttpClient(ClientContext &context) {
	throw NotImplementedException("DuckDBHttpClient: Waiting for HTTPUtil POST support");
}

HttpResponse DuckDBHttpClient::Execute(const HttpRequest &request) {
	throw NotImplementedException("DuckDBHttpClient: Waiting for HTTPUtil POST support");
}

} // namespace sheets
} // namespace duckdb
