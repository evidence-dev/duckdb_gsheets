#pragma once

#include <condition_variable>
#include <ctime>
#include <mutex>
#include <string>

#include "sheets/auth/auth_provider.hpp"
#include "sheets/transport/http_client.hpp"

namespace duckdb {
namespace sheets {

// Auto-refreshing OAuth provider for `oauth` secrets that went through the
// authorization-code + PKCE flow (i.e. were created with both client_id and
// client_secret - see CreateGsheetSecretFromOAuth). Holds the long-lived
// refresh_token and mints short-lived access tokens from it on demand,
// mirroring how ServiceAccountAuth turns a service account's private key
// into access tokens - see ServiceAccountAuth for the equivalent JWT-bearer
// flow.
class OAuthAuth : public IAuthProvider {
public:
	OAuthAuth(IHttpClient &http, const std::string &refreshToken, const std::string &clientId,
	          const std::string &clientSecret)
	    : http(http), refreshToken(refreshToken), clientId(clientId), clientSecret(clientSecret) {
	}

	// Thread-safe: safe to call concurrently (see cacheMutex).
	std::string GetAuthorizationHeader() override;

private:
	IHttpClient &http;
	std::string refreshToken;
	std::string clientId;
	std::string clientSecret;

	// Guards cachedToken/expirationTime/refreshing. GetAuthorizationHeader can
	// be called concurrently from multiple sink threads (e.g. a COPY with
	// PER_THREAD_OUTPUT); without synchronization a near-expiry token could
	// let two threads both decide to refresh at once and race to write the
	// cache. Unlike a plain lock_guard held for the whole call, this is
	// released before Refresh()'s blocking HTTP call (see
	// GetAuthorizationHeader) so a thread that already has a valid cached
	// token isn't stuck waiting on a network round trip some other thread is
	// making; `refreshing`/`refreshCv` instead let any threads that do need
	// the new token wait for the one in-flight refresh instead of each
	// starting their own.
	std::mutex cacheMutex;
	std::condition_variable refreshCv;
	bool refreshing = false;
	std::string cachedToken;
	std::time_t expirationTime = 0;

	void Refresh();
	bool IsExpired();
};

} // namespace sheets
} // namespace duckdb
