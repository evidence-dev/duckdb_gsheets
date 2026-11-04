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
	OAuthTokenResponse tokenResponse;
	try {
		try {
			tokenResponse = Refresh();
		} catch (const OAuthInvalidGrantException &) {
			// The refresh_token itself is dead (revoked/expired), not just a
			// transient failure - Refresh() with the same refresh_token would
			// only fail the same way again. Only a fresh interactive login can
			// recover from this, so fall back to that if the caller wired one
			// up; otherwise this is the same terminal failure it always was.
			if (!reauthCallback) {
				throw;
			}
			tokenResponse = reauthCallback();
		}
	} catch (...) {
		lock.lock();
		refreshing = false;
		refreshCv.notify_all();
		throw;
	}
	// Re-acquired before touching cachedToken/expirationTime/refreshToken:
	// Refresh()/reauthCallback() themselves only perform the (lock-free) HTTP
	// call / interactive login, so these writes - and every read of the same
	// fields in IsExpired()/GetAuthorizationHeader()/Refresh() - stay
	// synchronized on cacheMutex.
	lock.lock();
	cachedToken = tokenResponse.access_token;
	expirationTime = std::time(nullptr) + tokenResponse.expires_in - 60; // refresh 1 min early
	if (!tokenResponse.refresh_token.empty()) {
		refreshToken = tokenResponse.refresh_token;
	}
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

OAuthTokenResponse OAuthAuth::Refresh() {
	std::string body = "grant_type=refresh_token" + ("&refresh_token=" + url_encode(refreshToken)) +
	                   ("&client_id=" + url_encode(clientId)) + ("&client_secret=" + url_encode(clientSecret));

	return PostToTokenEndpoint(http, body, "OAuth token refresh");
}

} // namespace sheets
} // namespace duckdb
