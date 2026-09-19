#include "sheets/auth/oauth_auth.hpp"

#include <mutex>

#include "gsheets_utils.hpp"
#include "sheets/auth/oauth_token_exchange.hpp"

namespace duckdb {
namespace sheets {

std::string OAuthAuth::GetAuthorizationHeader() {
	std::unique_lock<std::mutex> lock(cacheMutex);
	if (!IsExpired()) {
		return "Bearer " + cachedToken;
	}

	// Some other thread is already refreshing - wait for it instead of
	// starting a second, redundant refresh, then re-check: it may have
	// refreshed to a token that's since expired again.
	while (refreshing) {
		refreshCv.wait(lock);
		if (!IsExpired()) {
			return "Bearer " + cachedToken;
		}
	}

	// Released for the HTTP call itself (see cacheMutex's declaration) - a
	// thread that finds `refreshing` true above only blocks on refreshCv,
	// never on this mutex, so it isn't stuck behind a network round trip it
	// doesn't need to wait on until it actually needs the result.
	refreshing = true;
	lock.unlock();
	try {
		Refresh();
	} catch (...) {
		lock.lock();
		refreshing = false;
		refreshCv.notify_all();
		throw;
	}
	lock.lock();
	refreshing = false;
	refreshCv.notify_all();
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
