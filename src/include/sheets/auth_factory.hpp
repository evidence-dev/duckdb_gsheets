#pragma once

#include "duckdb/main/client_context.hpp"

#include "sheets/auth/auth_provider.hpp"
#include "sheets/transport/http_client.hpp"

namespace duckdb {
namespace sheets {

std::unique_ptr<IAuthProvider> CreateAuthFromSecret(ClientContext &ctx, IHttpClient &http);

} // namespace sheets
} // namespace duckdb
