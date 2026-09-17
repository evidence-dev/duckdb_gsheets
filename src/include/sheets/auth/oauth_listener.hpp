#pragma once

#include <functional>
#include <string>

namespace duckdb {
namespace sheets {

// Builds the Google OAuth2 authorization URL. Pure/side-effect-free so it can
// be unit tested without any network or browser involved.
std::string BuildAuthorizationUrl(const std::string &auth_url, const std::string &client_id,
                                   const std::string &redirect_uri, const std::string &scope,
                                   const std::string &state);

// Extracts the access token from the body of a raw HTTP request (everything
// after the blank line separating headers from body). Returns an empty
// string if the request has no body. Pure/side-effect-free.
std::string ExtractAccessTokenFromHttpRequest(const std::string &raw_request);

// Runs a short-lived local HTTP listener on `port` so the OAuth redirect
// (see BuildAuthorizationUrl's redirect_uri) can hand back the access token
// automatically, without the user having to copy/paste it. Blocks until a
// token is received (or an error occurs). `on_listening`, if set, is invoked
// once the socket is bound and listening, before the call blocks on accept()
// - production code uses it to open the browser only once the server is
// actually ready; tests use it to know when it's safe to connect.
std::string RunLocalOAuthListener(int port, const std::function<void()> &on_listening = nullptr);

} // namespace sheets
} // namespace duckdb
