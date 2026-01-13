#pragma once

#include "sheets/transport/http_client.hpp"

// Forward declarations
namespace duckdb {
class DatabaseInstance;
class ClientContext;
} // namespace duckdb

namespace duckdb {
namespace sheets {

// This client will use DuckDB's built-in HTTPUtil once it supports POST/PUT.
// Currently HTTPUtil::Post throws NotImplementedException.

class DuckDBHttpClient : public IHttpClient {
public:
	explicit DuckDBHttpClient(DatabaseInstance &db);
	explicit DuckDBHttpClient(ClientContext &context);

	HttpResponse Execute(const HttpRequest &request) override;
};

} // namespace sheets
} // namespace duckdb
