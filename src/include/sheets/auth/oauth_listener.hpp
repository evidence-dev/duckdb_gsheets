#pragma once

#include <functional>
#include <string>

namespace duckdb {
namespace sheets {

// Wall-clock budget RunLocalOAuthListener will wait for a valid callback
// before giving up, regardless of max_attempts. Exposed so callers (e.g. the
// message shown while waiting for the browser) can surface it to the user
// instead of duplicating the number.
constexpr int kOAuthListenerTimeoutSeconds = 300;

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

// Parses what a user pastes back after completing the OAuth login in a
// browser that can't reach this machine's local listener - e.g. DuckDB
// running on a remote/headless host, where the redirect to
// "http://localhost:<port>" fails to load in the user's own browser but the
// address bar still shows the full URL, fragment included. Accepts either
// that whole URL (or just its "#..." fragment/query string) or a bare access
// token:
//   - If the input contains "access_token=", it's parsed like a query
//     string; when a "state=" parameter is also present it's validated
//     against `expected_state` (same CSRF check as ParseTokenPayload).
//   - Otherwise the trimmed input is treated as the raw token itself, with
//     no state to check - that's fine here, unlike the HTTP listener, since
//     the user is deliberately supplying it themselves rather than an
//     unsolicited request reaching the listener.
// Throws IOException if no token can be found, or if a present state
// mismatches. Pure/side-effect-free.
std::string ExtractPastedToken(const std::string &pasted, const std::string &expected_state);

// Non-blocking check for a line of input waiting on stdin: returns false
// immediately if nothing has been typed/pasted yet, otherwise reads and
// returns one line. Used to build the `try_read_pasted_input` callback
// RunLocalOAuthListener polls in production, so pasting a token can be
// offered alongside the local listener without ever blocking on
// std::cin - which matters because that polling happens on the same loop
// that also services the listener socket(s), with no separate thread left
// behind to race the DuckDB CLI's own prompt for stdin once this call
// returns.
bool TryReadPastedLine(std::string &line);

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
//
// `is_interrupted`, if set, is polled about once a second (whenever the
// accept-loop's connection wait times out) - production code wires this to
// ClientContext::IsInterrupted() so Ctrl+C can actually cancel a pending
// login instead of the whole CLI appearing to hang until the 5-minute
// timeout, since this call otherwise blocks the query-execution thread
// without ever yielding back to DuckDB's normal cancellation/EOF handling.
// Throws InterruptException as soon as it's observed set.
//
// `try_read_pasted_input`, if set, is also polled about once a second: it
// should return false immediately if nothing is available (production code
// wires this to TryReadPastedLine, a non-blocking stdin check), or true with
// a line of text otherwise. Each line is run through ExtractPastedToken; a
// valid one is returned immediately (same as a valid HTTP callback), while
// an invalid one (bad state, no token found) is logged and ignored so the
// listener keeps waiting for either a corrected paste or the real browser
// redirect - this is what lets pasting a token work as an alternative to the
// local listener actually receiving the redirect, e.g. when DuckDB runs on a
// remote host the browser can't reach back into.
std::string RunLocalOAuthListener(int port, const std::string &expected_state,
                                   const std::function<void()> &on_listening = nullptr, int max_attempts = 20,
                                   const std::function<bool()> &is_interrupted = nullptr,
                                   const std::function<bool(std::string &)> &try_read_pasted_input = nullptr);

} // namespace sheets
} // namespace duckdb
