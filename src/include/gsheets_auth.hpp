#pragma once

#include "duckdb.hpp"

#include <string>

namespace duckdb {

std::string read_token_from_file(const std::string &file_path);

std::string InitiateOAuthFlow();

struct CreateGsheetSecretFunctions {
public:
	static void Register(ExtensionLoader &loader);
};

} // namespace duckdb
