#pragma once

#include <string>

#include "sheets/auth/auth_provider.hpp"

namespace duckdb {
namespace sheets {

class BearerTokenAuth : public IAuthProvider {
public:
	explicit BearerTokenAuth(const std::string &token) : token(token) {
	}

	std::string GetAuthorizationHeader() override;

private:
	std::string token;
};

} // namespace sheets
} // namespace duckdb
