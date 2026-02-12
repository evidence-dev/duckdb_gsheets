#include "sheets/auth/oauth_auth.hpp"

namespace duckdb {
namespace sheets {

std::string OAuthAuth::GetAuthorizationHeader() {
	return "Bearer " + token;
}

} // namespace sheets
} // namespace duckdb
