#pragma once

#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

class ClientContext;

std::string InitiateOAuthFlow(ClientContext &context, const std::string &client_id);

struct CreateGsheetSecretFunctions {
public:
	static void Register(ExtensionLoader &loader);
};

} // namespace duckdb
