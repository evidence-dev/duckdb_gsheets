#pragma once

#include <string>
#include "duckdb.hpp"

namespace duckdb {

std::string read_token_from_file(const std::string& file_path);

std::string InitiateOAuthFlow();

struct CreateGsheetSecretFunctions {
public:
	//! Register all CreateSecretFunctions
#ifdef DUCKDB_CPP_EXTENSION_ENTRY
	static void Register(ExtensionLoader &loader);
#else
	static void Register(DatabaseInstance &db);
#endif
};

}  // namespace duckdb
