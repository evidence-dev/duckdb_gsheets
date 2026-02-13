#pragma once

#include <memory>

#include "duckdb/main/client_context.hpp"

#include "sheets/transport/http_client.hpp"

namespace duckdb {
namespace sheets {

std::unique_ptr<IHttpClient> CreateHttpClient(ClientContext &ctx);

} // namespace sheets
} // namespace duckdb
