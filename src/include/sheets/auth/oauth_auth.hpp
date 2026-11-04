#pragma once

#include <condition_variable>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>

#include "sheets/auth/auth_provider.hpp"
#include "sheets/auth/oauth_token_exchange.hpp"
#include "sheets/transport/http_client.hpp"

namespace duckdb {
namespace sheets {

// Invoked by OAuthAuth when the stored refresh_token is rejected with
// invalid_grant (revoked or expired). Must run a full interactive
// authorization-code + PKCE login - including reopening the browser - and
// return the new tokens (the same OAuthTokenResponse shape a plain refresh
// returns - refresh_token must be set), or throw. Left unset (nullptr) for
// callers that can't or don't want to reauthenticate automatically, in which
// case invalid_grant just surfaces as an OAuthInvalidGrantException like any
// other refresh failure.
//
// Only ever wire this up for an OAuthAuth instance whose GetAuthorizationHeader
// is exclusively called from one thread at a time across its whole lifetime
// (e.g. built fresh per query bind, like ReadSheetBind does) - it typically
// blocks on interactive browser/stdin input for minutes, which is only
// acceptable on a thread that's allowed to block like that; a COPY sink
// thread running under PER_THREAD_OUTPUT is not, so CreateAuthFromSecret's
// `allow_interactive_reauth` must be false for that call site.
using OAuthReauthCallback = std::function<OAuthTokenResponse()>;

// Auto-refreshing OAuth provider for `oauth` secrets that carry a
// refresh_token (i.e. were created with both client_id and client_secret,
// either through the authorization-code + PKCE flow or with a refresh_token
// passed in directly - see CreateGsheetSecretFromOAuth). Holds the long-lived
// refresh_token and mints short-lived access tokens from it on demand,
// mirroring how ServiceAccountAuth turns a service account's private key
// into access tokens - see ServiceAccountAuth for the equivalent JWT-bearer
// flow.
class OAuthAuth : public IAuthProvider {
public:
	OAuthAuth(IHttpClient &http, const std::string &refreshToken, const std::string &clientId,
	          const std::string &clientSecret, OAuthReauthCallback reauthCallback = nullptr)
	    : http(http), refreshToken(refreshToken), clientId(clientId), clientSecret(clientSecret),
	      reauthCallback(std::move(reauthCallback)) {
	}

	// Thread-safe: safe to call concurrently (see cacheMutex).
	std::string GetAuthorizationHeader() override;

private:
	IHttpClient &http;
	std::string refreshToken;
	std::string clientId;
	std::string clientSecret;
	OAuthReauthCallback reauthCallback;

	// Guards cachedToken/expirationTime/refreshing/refreshToken.
	// GetAuthorizationHeader can be called concurrently from multiple sink
	// threads (e.g. a COPY with PER_THREAD_OUTPUT); without synchronization a
	// near-expiry token could let two threads both decide to refresh at once
	// and race to write the cache. Unlike a plain lock_guard held for the
	// whole call, this is released before Refresh()'s blocking HTTP call (and
	// before reauthCallback's, which additionally blocks on interactive login
	// - see GetAuthorizationHeader and OAuthReauthCallback's single-threaded
	// requirement) so a thread that already has a valid cached token isn't
	// stuck waiting on a network round trip some other thread is making;
	// `refreshing`/`refreshCv` instead let any threads that do need the new
	// token wait for the one in-flight refresh instead of each starting their
	// own. refreshToken is only ever written by the single thread currently
	// holding `refreshing`, and only read (unlocked, in Refresh()) by that
	// same thread, so the lock around its write below is what makes a
	// subsequent Refresh() call's read observe it correctly rather than being
	// required for mutual exclusion.
	std::mutex cacheMutex;
	std::condition_variable refreshCv;
	bool refreshing = false;
	std::string cachedToken;
	std::time_t expirationTime = 0;

	OAuthTokenResponse Refresh();
	bool IsExpired();
};

} // namespace sheets
} // namespace duckdb
