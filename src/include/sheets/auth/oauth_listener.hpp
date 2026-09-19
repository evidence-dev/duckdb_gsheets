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
                                  const std::string &redirect_uri, const std::string &scope, const std::string &state);

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
//     string, and its "state=" parameter must be present and validated
//     against `expected_state` (same CSRF check as ParseTokenPayload). A
//     genuine redirect always echoes the state we generated, so a
//     URL/query-shaped paste with no state (or the wrong one) is rejected
//     rather than silently accepted - otherwise an attacker could hand a
//     victim a crafted "http://localhost:<port>/#access_token=..." link
//     with the state omitted and have it accepted as if it were the real
//     redirect.
//   - Otherwise (no "access_token=" substring at all) the trimmed input is
//     treated as the raw token itself, with no state to check - that's fine
//     here, unlike the HTTP listener, since the user is deliberately
//     supplying it themselves rather than an unsolicited request reaching
//     the listener.
// Throws IOException if no token can be found, or if a URL/query-shaped
// paste's state is missing or mismatches. Pure/side-effect-free.
std::string ExtractPastedToken(const std::string &pasted, const std::string &expected_state);

// Non-blocking check for a line of input waiting on stdin: returns false
// immediately if nothing has been typed/pasted yet, otherwise reads and
// returns one line. Used to build the `try_read_pasted_input` callback
// RunLocalOAuthListener polls in production, so pasting a token can be
// offered alongside the local listener without ever blocking on
// std::cin - which matters because that polling happens on the same loop
// that also services the listener, with no separate thread left behind to
// race the DuckDB CLI's own prompt for stdin once this call returns.
//
// Callers should only wire this up when stdin is a real interactive
// terminal (isatty). Any line here is treated as a paste attempt, and an
// input that doesn't parse as a valid token/URL is silently ignored and
// waited past (see RunLocalOAuthListener) - fine for a human correcting a
// bad paste, but on a piped/scripted stdin (e.g. `duckdb < script.sql`)
// this would instead consume and discard the next line of the script.
bool TryReadPastedLine(std::string &line);

// Runs a short-lived local HTTP listener on `port`, bound to loopback only
// (127.0.0.1 and, best-effort, ::1 - see the .cpp for why both), so the
// OAuth redirect (see BuildAuthorizationUrl's redirect_uri) can hand back the
// access token automatically, without the user having to copy/paste it.
// Blocks until a payload passing ParseTokenPayload is received (or the
// attempt budget is exhausted). `on_listening`, if set, is invoked once the
// server is bound and ready to accept connections (or, if binding failed on
// every loopback family and a paste fallback is available, once that
// degraded paste-only mode has been entered instead) - production code uses
// it to open the browser only once the server is actually ready; tests use
// it to know when it's safe to connect.
//
// `max_attempts` bounds how many callbacks that fail validation (wrong/
// missing state, malformed payload) this will tolerate before giving up -
// exposed mainly so tests don't have to wait through the full production
// budget to exercise the timeout path. A stray request that isn't even a
// callback attempt (e.g. a GET before the POST in the implicit-grant flow)
// doesn't count against this. It also gives up after a fixed wall-clock
// deadline regardless of `max_attempts`, so a browser that never completes
// the redirect (headless environment, abandoned flow, etc.) can't leave the
// caller blocked forever.
//
// `is_interrupted`, if set, is polled about once a second. Production code
// wires this to ClientContext::IsInterrupted() so Ctrl+C can actually cancel
// a pending login instead of the whole CLI appearing to hang until the
// 5-minute timeout, since this call otherwise blocks the query-execution
// thread without ever yielding back to DuckDB's normal cancellation/EOF
// handling. Throws InterruptException as soon as it's observed set.
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
// remote host the browser can't reach back into, or when the local port
// couldn't be bound at all (see above).
std::string RunLocalOAuthListener(int port, const std::string &expected_state,
                                  const std::function<void()> &on_listening = nullptr, int max_attempts = 20,
                                  const std::function<bool()> &is_interrupted = nullptr,
                                  const std::function<bool(std::string &)> &try_read_pasted_input = nullptr);

// ---------------------------------------------------------------------------
// Authorization-code + PKCE flow support.
//
// Used only when a caller supplies its own OAuth client_secret alongside a
// client_id (see CreateGsheetSecretFromOAuth) - the flow above (implicit
// grant, response_type=token) remains the default and is untouched by any
// of the below.
// ---------------------------------------------------------------------------

// Generates a PKCE code_verifier: a random string (length 64, within the
// RFC 7636-required 43-128 range) drawn from the PKCE-unreserved character
// set. Pure/randomized, no network or browser involved.
std::string GeneratePkceCodeVerifier();

// Derives the S256 PKCE code_challenge for a given code_verifier:
// Base64UrlEncode(SHA256(code_verifier)). Pure/side-effect-free.
std::string GeneratePkceCodeChallenge(const std::string &code_verifier);

// Builds the Google OAuth2 authorization URL for the authorization-code +
// PKCE flow (response_type=code, access_type=offline, prompt=consent so a
// refresh_token is (re)issued every time, plus the PKCE challenge).
// Pure/side-effect-free so it can be unit tested without any network or
// browser involved.
std::string BuildAuthorizationCodeUrl(const std::string &auth_url, const std::string &client_id,
                                      const std::string &redirect_uri, const std::string &scope,
                                      const std::string &state, const std::string &code_challenge);

// Parses the "code"/"state" query parameters off a redirect callback's
// request-target (e.g. "/?code=abc&state=xyz") and validates `state` against
// `expected_state` - the same CSRF protection ParseTokenPayload applies to
// the implicit-grant flow. Throws IOException if no code is found, or the
// state doesn't match. Pure/side-effect-free.
std::string ParseAuthorizationCodeCallback(const std::string &request_target, const std::string &expected_state);

// Paste-fallback counterpart to ParseAuthorizationCodeCallback, mirroring
// ExtractPastedToken's rules but for "code=" instead of "access_token=":
// accepts either a full redirect URL/query string (state required and
// validated) or a bare authorization code (no state to check, since the
// user is deliberately supplying it themselves). Throws IOException if no
// code can be found, or a URL/query-shaped paste's state is missing or
// mismatches. Pure/side-effect-free.
std::string ExtractPastedAuthorizationCode(const std::string &pasted, const std::string &expected_state);

// Authorization-code-flow counterpart to RunLocalOAuthListener: the
// redirect carries "code"/"state" as normal GET query parameters (safe to
// read server-side, unlike an implicit-grant access token), so this waits
// for a single valid GET instead of RunLocalOAuthListener's GET-then-POST
// handshake. Shares the same listener/deadline/interrupt/paste-poll
// machinery and parameters otherwise - see RunLocalOAuthListener for what
// each one does.
std::string RunLocalOAuthCodeListener(int port, const std::string &expected_state,
                                      const std::function<void()> &on_listening = nullptr, int max_attempts = 20,
                                      const std::function<bool()> &is_interrupted = nullptr,
                                      const std::function<bool(std::string &)> &try_read_pasted_input = nullptr);

} // namespace sheets
} // namespace duckdb
