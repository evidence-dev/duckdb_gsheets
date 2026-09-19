#include "sheets/auth/oauth_listener.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

#include "duckdb/common/exception.hpp"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/select.h>
#include <unistd.h>
#endif

namespace duckdb {
namespace sheets {

namespace {

using HttpServer = duckdb_httplib_openssl::Server;
using HttpRequest = duckdb_httplib_openssl::Request;
using HttpResponse = duckdb_httplib_openssl::Response;

// Bounds how long a single connection's read/write may take. httplib enforces
// this with a plain socket-level recv/send timeout, so a connection that's
// accepted but then stalls (an idle probe, a client that writes a partial
// request and goes quiet) can't block a worker thread - and therefore this
// listener - forever. Generous relative to a real loopback round trip.
constexpr int CONNECTION_TIMEOUT_SECONDS = 10;

// The CSRF check shared by every place a state parameter is validated
// (ParseTokenPayload, ExtractPastedToken below): only a payload/URL that
// echoes back the exact state generated for this specific flow is accepted,
// so a stray local process, browser tab, or crafted link can't inject an
// arbitrary token. Centralized so a future hardening of this check only has
// to change one place instead of drifting across both.
void RequireMatchingState(const std::string &received_state, const std::string &expected_state,
                          const std::string &what) {
	if (received_state.empty() || received_state != expected_state) {
		throw IOException("OAuth state mismatch - rejecting " + what);
	}
}

std::string TrimWhitespace(const std::string &s) {
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) {
		return "";
	}
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

// Finds `key=` in `text` at a param boundary (start of string, or right
// after '&', '#' or '?') and returns the value up to the next '&' (or end of
// string). Returns "" if `key` never appears at a boundary - e.g. it's only
// present as a suffix of a longer param name.
std::string ExtractQueryParam(const std::string &text, const std::string &key) {
	std::string marker = key + "=";
	size_t search_from = 0;
	while (true) {
		size_t pos = text.find(marker, search_from);
		if (pos == std::string::npos) {
			return "";
		}
		bool at_boundary = pos == 0 || text[pos - 1] == '&' || text[pos - 1] == '#' || text[pos - 1] == '?';
		if (at_boundary) {
			size_t value_start = pos + marker.size();
			size_t value_end = text.find('&', value_start);
			size_t value_len = value_end == std::string::npos ? std::string::npos : value_end - value_start;
			return text.substr(value_start, value_len);
		}
		search_from = pos + 1;
	}
}

// Non-blocking check for whether stdin has unread input. Used to decide
// whether it's safe to call the (line-buffered, otherwise blocking)
// std::getline in TryReadPastedLine without stalling the caller's poll loop.
#ifdef _WIN32
bool StdinHasPendingData() {
	HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
	if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
		return false;
	}

	// GetNumberOfConsoleInputEvents/WaitForSingleObject signal "ready" on any
	// queued input record, not just typed text: a mouse move, window resize,
	// or focus-change event (all routine while a terminal just sits waiting
	// for the browser redirect) counts too. Treating any of those as "a line
	// is ready" let TryReadPastedLine call the blocking std::getline with
	// nothing actually typed yet, stalling this poll loop - along with Ctrl+C
	// handling and the real browser callback - until the user happened to
	// type something. Drain and discard every non-keystroke event first, so
	// only a genuine keydown with a character is reported as pending.
	INPUT_RECORD record;
	DWORD events_read;
	while (true) {
		DWORD pending = 0;
		if (!GetNumberOfConsoleInputEvents(handle, &pending) || pending == 0) {
			return false;
		}
		if (!PeekConsoleInputW(handle, &record, 1, &events_read) || events_read == 0) {
			return false;
		}
		if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown &&
		    record.Event.KeyEvent.uChar.UnicodeChar != 0) {
			return true;
		}
		// Not a keystroke we care about (key-up, a modifier-only key, mouse,
		// resize, focus, ...) - consume it so it doesn't spin this loop
		// forever on the same stale event, then keep looking.
		ReadConsoleInputW(handle, &record, 1, &events_read);
	}
}
#else
bool StdinHasPendingData() {
	fd_set read_fds;
	FD_ZERO(&read_fds);
	FD_SET(STDIN_FILENO, &read_fds);
	struct timeval tv = {0, 0};
	return select(STDIN_FILENO + 1, &read_fds, nullptr, nullptr, &tv) > 0;
}
#endif

} // namespace

std::string BuildAuthorizationUrl(const std::string &auth_url, const std::string &client_id,
                                  const std::string &redirect_uri, const std::string &scope, const std::string &state) {
	return auth_url + "?client_id=" + client_id + "&redirect_uri=" + redirect_uri + "&response_type=token" +
	       "&scope=" + scope + "&state=" + state;
}

std::string ParseTokenPayload(const std::string &body, const std::string &expected_state) {
	const std::string state_prefix = "state=";
	const std::string token_marker = "&access_token=";

	if (body.compare(0, state_prefix.size(), state_prefix) != 0) {
		throw IOException("Malformed OAuth callback payload");
	}
	size_t token_marker_pos = body.find(token_marker);
	if (token_marker_pos == std::string::npos) {
		throw IOException("Malformed OAuth callback payload");
	}

	std::string received_state = body.substr(state_prefix.size(), token_marker_pos - state_prefix.size());
	std::string token = body.substr(token_marker_pos + token_marker.size());

	RequireMatchingState(received_state, expected_state, "callback");
	if (token.empty()) {
		throw IOException("Failed to obtain access token");
	}

	return token;
}

std::string ExtractPastedToken(const std::string &pasted, const std::string &expected_state) {
	std::string trimmed = TrimWhitespace(pasted);
	if (trimmed.empty()) {
		throw IOException("Pasted input was empty");
	}

	if (trimmed.find("access_token=") == std::string::npos) {
		// No query string to parse - treat the whole line as the bare
		// token. No state to check here, unlike ParseTokenPayload: this
		// came from the user directly pasting into their own terminal, not
		// an unsolicited request reaching the listener, so there's nothing
		// for the CSRF check to protect against.
		return trimmed;
	}

	std::string token = ExtractQueryParam(trimmed, "access_token");
	if (token.empty()) {
		throw IOException("Could not find access_token in pasted input");
	}

	// Unlike the bare-token case above, this input is URL/query-shaped, so it
	// claims to be an actual redirect from our flow - and a genuine redirect
	// always echoes back the state we generated. Require it to be present
	// and match rather than only checking it when present: silently
	// accepting a missing state here would let an attacker hand a victim a
	// crafted "access_token=...&state=" (or state-less) link to paste in,
	// defeating the CSRF protection this same check applies to the HTTP
	// callback path in ParseTokenPayload.
	std::string state = ExtractQueryParam(trimmed, "state");
	RequireMatchingState(state, expected_state, "pasted token");

	return token;
}

bool TryReadPastedLine(std::string &line) {
	if (!StdinHasPendingData()) {
		return false;
	}
	return static_cast<bool>(std::getline(std::cin, line));
}

namespace {

// Outcome box shared between the httplib worker thread that receives a valid
// (or invalid) callback and the calling thread's poll loop in
// RunLoginListenerLoop below - takes the place of the old accept-loop's
// direct return value now that receiving happens on a background thread
// instead of inline.
struct ListenerOutcome {
	std::mutex mtx;
	std::condition_variable cv;
	bool done = false;
	bool gave_up = false;
	std::string value;
	int failed_attempts = 0;
};

void SignalSuccess(ListenerOutcome &outcome, const std::string &value) {
	std::lock_guard<std::mutex> lock(outcome.mtx);
	if (!outcome.done) {
		outcome.done = true;
		outcome.value = value;
	}
	outcome.cv.notify_all();
}

// Records a callback that was received but failed validation (wrong/missing
// state, malformed payload). Once `max_attempts` of these have been seen,
// gives up rather than waiting out the full wall-clock deadline - mainly so
// tests don't have to wait through the production timeout to exercise this
// path.
void SignalFailedAttempt(ListenerOutcome &outcome, int max_attempts) {
	std::lock_guard<std::mutex> lock(outcome.mtx);
	if (outcome.done) {
		return;
	}
	outcome.failed_attempts++;
	if (outcome.failed_attempts >= max_attempts) {
		outcome.done = true;
		outcome.gave_up = true;
	}
	outcome.cv.notify_all();
}

// One HTTP server bound to a single loopback address ("127.0.0.1" or "::1").
// Binding is best-effort for both families: IsBound() reports whether it
// succeeded, and RunLoginListenerLoop below decides what to do if neither
// one did (see its class comment for why both families are attempted, and
// what happens if both fail to bind).
class LoopbackHttpServer {
public:
	LoopbackHttpServer(const std::string &host, int port) {
		server_.set_read_timeout(CONNECTION_TIMEOUT_SECONDS, 0);
		server_.set_write_timeout(CONNECTION_TIMEOUT_SECONDS, 0);
		if (host.find(':') != std::string::npos) {
			// Without this, some platforms (Linux) let the IPv6 socket also
			// accept IPv4 connections, which would collide with the separate
			// IPv4 socket bound to the same port.
			server_.set_ipv6_v6only(true);
		}
		bound_ = server_.bind_to_port(host, port);
	}

	~LoopbackHttpServer() {
		Stop();
		if (thread_.joinable()) {
			thread_.join();
		}
	}

	bool IsBound() const {
		return bound_;
	}

	HttpServer &Handle() {
		return server_;
	}

	// Starts accepting connections on a background thread and waits for the
	// accept loop to actually be running before returning, so a caller that
	// immediately calls Stop() (e.g. an early deadline/interrupt) can't race
	// a thread that hasn't started listening yet.
	void Start() {
		if (!bound_) {
			return;
		}
		thread_ = std::thread([this]() { server_.listen_after_bind(); });
		server_.wait_until_ready();
	}

	void Stop() {
		if (bound_) {
			server_.stop();
		}
	}

private:
	HttpServer server_;
	std::thread thread_;
	bool bound_ = false;
};

// Drives the accept/poll/deadline/interrupt/paste-poll loop behind
// RunLocalOAuthListener. Factored out from it (rather than inlined) so a
// second flow can reuse this same driver later, supplying its own request
// handling via `register_handlers`/`handle_pasted` instead of duplicating
// the loop.
//
// Binds two servers - one on 127.0.0.1 (IPv4), one on ::1 (IPv6) - and
// listens on whichever bind. macOS Safari resolves "localhost" to the IPv6
// loopback address ahead of 127.0.0.1 and, unlike Chromium/Firefox, doesn't
// reliably fall back to IPv4 when nothing answers there - so an IPv4-only
// listener made the whole login flow silently fail in Safari even though the
// redirect URI ("http://localhost:<port>") worked fine in other browsers.
// Listening on both families sidesteps that without touching the redirect
// URI (which must stay in sync with what's registered for the OAuth client).
//
// If neither family can bind at all (e.g. the port is already in use by
// another concurrent login), this falls back to a listener-less, paste-only
// mode rather than failing the whole flow outright - as long as a paste
// fallback is actually available (stdin is an interactive terminal); if it
// isn't, there would be no way to ever complete the login, so this throws
// instead.
std::string RunLoginListenerLoop(int port, const std::function<void()> &on_listening, int max_attempts,
                                 const std::function<bool()> &is_interrupted,
                                 const std::function<bool(std::string &)> &try_read_pasted_input,
                                 const std::function<void(HttpServer &, ListenerOutcome &, int)> &register_handlers,
                                 const std::function<bool(const std::string &, std::string &)> &handle_pasted) {
	// Declared before v4/v6 (and therefore destroyed after them, since local
	// variables are destroyed in reverse declaration order): both servers'
	// handlers hold a reference to `outcome`, and run on background threads
	// that LoopbackHttpServer's destructor stops and joins. If `outcome`
	// were destroyed first, a handler still in flight during shutdown could
	// touch it after it's gone.
	ListenerOutcome outcome;
	LoopbackHttpServer v4("127.0.0.1", port);
	LoopbackHttpServer v6("::1", port);

	if (!v4.IsBound() && !v6.IsBound()) {
		if (!try_read_pasted_input) {
			throw IOException("Failed to bind to port " + std::to_string(port) +
			                  " (already in use?) and no paste fallback is available "
			                  "(stdin isn't an interactive terminal)");
		}
		std::cerr << "Warning: could not bind to port " << port
		          << " (already in use by another process?) - falling back to pasting the redirect URL manually.\n";
	}

	if (v4.IsBound()) {
		register_handlers(v4.Handle(), outcome, max_attempts);
	}
	if (v6.IsBound()) {
		register_handlers(v6.Handle(), outcome, max_attempts);
	}

	v4.Start();
	v6.Start();

	if (on_listening) {
		on_listening();
	}

	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kOAuthListenerTimeoutSeconds);
	while (true) {
		{
			std::unique_lock<std::mutex> lock(outcome.mtx);
			bool signaled = outcome.cv.wait_for(lock, std::chrono::seconds(1), [&] { return outcome.done; });
			if (signaled) {
				if (outcome.gave_up) {
					throw IOException("Timed out waiting for a valid OAuth callback");
				}
				return outcome.value;
			}
		}

		if (std::chrono::steady_clock::now() >= deadline) {
			throw IOException("Timed out waiting for a valid OAuth callback");
		}
		if (is_interrupted && is_interrupted()) {
			throw InterruptException();
		}
		if (try_read_pasted_input) {
			std::string pasted_line;
			if (try_read_pasted_input(pasted_line)) {
				std::string result;
				if (handle_pasted(pasted_line, result)) {
					return result;
				}
			}
		}
	}
}

// The page served for the browser's GET after the OAuth redirect. Runs
// client-side, in the browser, at the http://localhost:<port> origin: reads
// the access_token/state out of the URL fragment (fragments are never sent
// to any server) and POSTs them back to us, same-origin - so no CORS
// headers are needed, or served, on either response.
std::string BuildRedirectPageBody() {
	return "<script>"
	       "const hash = window.location.hash.substring(1);"
	       "const params = new URLSearchParams(hash);"
	       "const token = params.get('access_token');"
	       "const state = params.get('state') || '';"
	       "if (token) {"
	       "  fetch('/', {"
	       "    method: 'POST',"
	       "    body: 'state=' + encodeURIComponent(state) + '&access_token=' + encodeURIComponent(token)"
	       "  }).then(() => {"
	       "    window.location.href = 'https://duckdb-gsheets.com/oauth#ready=1&access_token=success';"
	       "  });"
	       "}"
	       "</script></body></html>";
}

void RegisterImplicitGrantHandlers(HttpServer &server, ListenerOutcome &outcome, int max_attempts,
                                   const std::string &expected_state) {
	// Any GET (including a browser's favicon probe alongside the real
	// redirect) gets served the same page; it's harmless, and the real
	// redirect always is a GET too, since the token lives in the fragment
	// (never sent to a server) until the page's own JS posts it below.
	server.Get(".*",
	           [](const HttpRequest &, HttpResponse &res) { res.set_content(BuildRedirectPageBody(), "text/html"); });

	server.Post(".*", [&outcome, max_attempts, expected_state](const HttpRequest &req, HttpResponse &res) {
		res.status = 200;
		try {
			std::string token = ParseTokenPayload(req.body, expected_state);
			SignalSuccess(outcome, token);
		} catch (const Exception &) {
			// Not our callback (wrong/missing state, malformed body, or a
			// stray request) - keep waiting for the real one.
			SignalFailedAttempt(outcome, max_attempts);
		}
	});
}

bool HandlePastedImplicitGrantToken(const std::string &pasted_line, const std::string &expected_state,
                                    std::string &out_token) {
	try {
		out_token = ExtractPastedToken(pasted_line, expected_state);
		return true;
	} catch (const Exception &e) {
		// Not a usable paste (wrong/missing state, no token found) - keep
		// waiting for either a corrected paste or the real browser redirect,
		// same as an HTTP callback that fails ParseTokenPayload above.
		std::cerr << "Ignoring pasted input: " << e.what() << '\n';
		return false;
	}
}

} // namespace

std::string RunLocalOAuthListener(int port, const std::string &expected_state,
                                  const std::function<void()> &on_listening, int max_attempts,
                                  const std::function<bool()> &is_interrupted,
                                  const std::function<bool(std::string &)> &try_read_pasted_input) {
	return RunLoginListenerLoop(
	    port, on_listening, max_attempts, is_interrupted, try_read_pasted_input,
	    [&expected_state](HttpServer &server, ListenerOutcome &outcome, int attempts) {
		    RegisterImplicitGrantHandlers(server, outcome, attempts, expected_state);
	    },
	    [&](const std::string &pasted_line, std::string &out_token) {
		    return HandlePastedImplicitGrantToken(pasted_line, expected_state, out_token);
	    });
}

} // namespace sheets
} // namespace duckdb
