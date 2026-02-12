#pragma once

#include "duckdb/main/client_context.hpp"
#include "duckdb/main/secret/secret.hpp"
#include "duckdb/main/secret/secret_manager.hpp"

namespace duckdb {
namespace sheets {

const SecretMatch GetSecretMatch(ClientContext &ctx, const std::string &path, const std::string &type);
const KeyValueSecret *GetGSheetSecret(ClientContext &ctx);

} // namespace sheets
} // namespace duckdb
