#pragma once

#include "duckdb/main/client_context.hpp"

#include "sheets/transport/http_type.hpp"

namespace duckdb {
namespace sheets {

HttpProxyConfig GetHttpProxyConfig(ClientContext &ctx);

} // namespace sheets
} // namespace duckdb
