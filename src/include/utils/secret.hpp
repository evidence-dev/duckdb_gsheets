#pragma once

#include "duckdb/main/client_context.hpp"
#include "duckdb/main/secret/secret.hpp"

namespace duckdb {
namespace sheets {

const KeyValueSecret *GetGSheetSecret(ClientContext &ctx, const std::string &secretName = "");

} // namespace sheets
} // namespace duckdb
