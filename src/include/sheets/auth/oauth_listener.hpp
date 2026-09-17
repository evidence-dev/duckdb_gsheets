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

// Extracts the body of a raw HTTP request (everything after the blank line
// separating headers from body). Returns an empty string if the request has
// no body. Pure/side-effect-free.
std::string ExtractHttpBody(const std::string &raw_request);

// Parses the "state=<state>&access_token=<token>" payload the redirect
// page's JS posts back, and validates `state` against `expected_state` -
// this is the CSRF protection: only a payload that echoes back the state we
// generated for this specific flow is accepted, so an unrelated local
// process or browser tab that also happens to connect to the listener can't
// inject an arbitrary token. Throws IOException if the payload is malformed
// or the state doesn't match. Pure/side-effect-free.
std::string ParseTokenPayload(const std::string &body, const std::string &expected_state);

// Runs a short-lived local HTTP listener on `port`, bound to loopback only,
// so the OAuth redirect (see BuildAuthorizationUrl's redirect_uri) can hand
// back the access token automatically, without the user having to
// copy/paste it. Blocks until a payload passing ParseTokenPayload is
// received (or the attempt budget is exhausted). `on_listening`, if set, is
// invoked once the socket is bound and listening, before the call blocks on
// accept() - production code uses it to open the browser only once the
// server is actually ready; tests use it to know when it's safe to connect.
// `max_attempts` bounds how many connections it will accept/inspect before
// giving up (exposed mainly so tests don't have to wait through the full
// production budget to exercise the timeout path). It also gives up after a
// fixed wall-clock deadline regardless of `max_attempts`, so a browser that
// never completes the redirect (headless environment, abandoned flow, etc.)
// can't leave the caller blocked forever.
std::string RunLocalOAuthListener(int port, const std::string &expected_state,
                                   const std::function<void()> &on_listening = nullptr, int max_attempts = 20);

} // namespace sheets
} // namespace duckdb
