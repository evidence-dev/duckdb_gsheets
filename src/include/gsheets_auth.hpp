#pragma once

#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

std::string InitiateOAuthFlow();

struct CreateGsheetSecretFunctions {
public:
	static void Register(ExtensionLoader &loader);
};

} // namespace duckdb
