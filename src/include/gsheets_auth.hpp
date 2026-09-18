#pragma once

#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

class ClientContext;

std::string InitiateOAuthFlow(ClientContext &context);

struct CreateGsheetSecretFunctions {
public:
	static void Register(ExtensionLoader &loader);
};

} // namespace duckdb
