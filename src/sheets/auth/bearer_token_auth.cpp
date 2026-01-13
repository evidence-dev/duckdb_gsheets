#include "sheets/auth/bearer_token_auth.hpp"

namespace duckdb {
namespace sheets {

std::string BearerTokenAuth::GetAuthorizationHeader() {
	return "Bearer " + token;
}

} // namespace sheets
} // namespace duckdb
