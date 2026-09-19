#pragma once

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

	// Guards cachedToken/expirationTime, and is held across Refresh()'s HTTP
	// call: GetAuthorizationHeader can be called concurrently from multiple
	// sink threads (e.g. a COPY with PER_THREAD_OUTPUT), and without this a
	// near-expiry token could let two threads both decide to refresh at once
	// and race to write the cache. Serializing the (infrequent, ~hourly)
	// refresh is a small price for avoiding that.
	std::mutex cacheMutex;
	std::string cachedToken;
	std::time_t expirationTime = 0;

	void Refresh();
	bool IsExpired();
};

} // namespace sheets
} // namespace duckdb
