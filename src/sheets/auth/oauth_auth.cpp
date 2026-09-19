#include "sheets/auth/oauth_auth.hpp"

#include <mutex>

#include "gsheets_utils.hpp"
#include "sheets/auth/oauth_token_exchange.hpp"

namespace duckdb {
namespace sheets {

std::string OAuthAuth::GetAuthorizationHeader() {
	// Held across Refresh()'s HTTP call - see cacheMutex's declaration for why.
	std::lock_guard<std::mutex> lock(cacheMutex);
	if (IsExpired()) {
		Refresh();
	}
	return "Bearer " + cachedToken;
}

bool OAuthAuth::IsExpired() {
	if (cachedToken.empty()) {
		return true;
	}
	std::time_t now = std::time(nullptr);
	return now >= expirationTime;
}

void OAuthAuth::Refresh() {
	std::string body = "grant_type=refresh_token" + ("&refresh_token=" + url_encode(refreshToken)) +
	                   ("&client_id=" + url_encode(clientId)) + ("&client_secret=" + url_encode(clientSecret));

	OAuthTokenResponse tokenResponse = PostToTokenEndpoint(http, body, "OAuth token refresh");

	cachedToken = tokenResponse.access_token;
	expirationTime = std::time(nullptr) + tokenResponse.expires_in - 60; // refresh 1 min early
}

} // namespace sheets
} // namespace duckdb
