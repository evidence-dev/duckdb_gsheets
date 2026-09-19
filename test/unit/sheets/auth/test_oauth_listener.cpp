#include "catch.hpp"

#include <atomic>
#include <chrono>
#include <exception>
#include <string>
#include <thread>

#include "duckdb/common/exception.hpp"
#include "sheets/auth/oauth_listener.hpp"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.hpp"

using namespace duckdb::sheets;

// =============================================================================
// BuildAuthorizationUrl Tests
// =============================================================================

TEST_CASE("BuildAuthorizationUrl assembles the expected query string", "[oauth_listener]") {
	std::string url =
	    BuildAuthorizationUrl("https://accounts.google.com/o/oauth2/v2/auth", "my-client-id", "http://localhost:8765",
	                          "https://www.googleapis.com/auth/spreadsheets", "my-state");

	// redirect_uri and scope are themselves URIs, so their ':' and '/' must
	// come out percent-encoded - Google's endpoint parses this URL's query
	// string, not the raw concatenation, so an unescaped nested URI would be
	// misread as more top-level query params.
	REQUIRE(url == "https://accounts.google.com/o/oauth2/v2/auth"
	               "?client_id=my-client-id"
	               "&redirect_uri=http%3A%2F%2Flocalhost%3A8765"
	               "&response_type=token"
	               "&scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fspreadsheets"
	               "&state=my-state");
}

TEST_CASE("BuildAuthorizationUrl orders parameters client_id, redirect_uri, ..., state", "[oauth_listener]") {
	std::string url = BuildAuthorizationUrl("AUTH", "CID", "REDIR", "SCOPE", "STATE");
	REQUIRE(url.find("client_id=CID") < url.find("redirect_uri=REDIR"));
	REQUIRE(url.find("redirect_uri=REDIR") < url.find("state=STATE"));
}

// =============================================================================
// ParseTokenPayload Tests
// =============================================================================

TEST_CASE("ParseTokenPayload returns the token when state matches", "[oauth_listener]") {
	std::string token = ParseTokenPayload("state=abc123&access_token=ya29.realistic-token-value", "abc123");
	REQUIRE(token == "ya29.realistic-token-value");
}

TEST_CASE("ParseTokenPayload throws on state mismatch", "[oauth_listener]") {
	REQUIRE_THROWS_AS(ParseTokenPayload("state=attacker-guess&access_token=forged-token", "abc123"),
	                  duckdb::IOException);
}

TEST_CASE("ParseTokenPayload throws when state is missing entirely", "[oauth_listener]") {
	REQUIRE_THROWS_AS(ParseTokenPayload("access_token=no-state-at-all", "abc123"), duckdb::IOException);
}

TEST_CASE("ParseTokenPayload throws on malformed payload", "[oauth_listener]") {
	REQUIRE_THROWS_AS(ParseTokenPayload("not a valid payload", "abc123"), duckdb::IOException);
	REQUIRE_THROWS_AS(ParseTokenPayload("", "abc123"), duckdb::IOException);
}

TEST_CASE("ParseTokenPayload throws when the token is empty", "[oauth_listener]") {
	REQUIRE_THROWS_AS(ParseTokenPayload("state=abc123&access_token=", "abc123"), duckdb::IOException);
}

TEST_CASE("ParseTokenPayload percent-decodes the access_token", "[oauth_listener]") {
	std::string token = ParseTokenPayload("state=abc123&access_token=ya29.has%20a%20space", "abc123");
	REQUIRE(token == "ya29.has a space");
}

TEST_CASE("ParseTokenPayload percent-decodes state before comparing against expected_state", "[oauth_listener]") {
	std::string token = ParseTokenPayload("state=weird%20state&access_token=tok", "weird state");
	REQUIRE(token == "tok");
}

// =============================================================================
// ExtractPastedToken Tests
// =============================================================================

TEST_CASE("ExtractPastedToken accepts a bare token with no query string", "[oauth_listener]") {
	REQUIRE(ExtractPastedToken("ya29.bare-token-value", "abc123") == "ya29.bare-token-value");
}

TEST_CASE("ExtractPastedToken trims surrounding whitespace from a bare token", "[oauth_listener]") {
	REQUIRE(ExtractPastedToken("  ya29.bare-token-value \r\n", "abc123") == "ya29.bare-token-value");
}

TEST_CASE("ExtractPastedToken extracts the token from a full redirect URL", "[oauth_listener]") {
	std::string pasted = "http://localhost:8765/#access_token=ya29.from-url&token_type=Bearer&expires_in=3599&"
	                     "scope=https://www.googleapis.com/auth/spreadsheets&state=abc123";
	REQUIRE(ExtractPastedToken(pasted, "abc123") == "ya29.from-url");
}

TEST_CASE("ExtractPastedToken extracts the token when it's the only query param", "[oauth_listener]") {
	REQUIRE(ExtractPastedToken("access_token=ya29.only-param&state=abc123", "abc123") == "ya29.only-param");
}

TEST_CASE("ExtractPastedToken throws on state mismatch", "[oauth_listener]") {
	REQUIRE_THROWS_AS(ExtractPastedToken("access_token=forged&state=wrong-state", "abc123"), duckdb::IOException);
}

TEST_CASE("ExtractPastedToken throws when a query string has no state param", "[oauth_listener]") {
	// A genuine redirect always echoes back the state we generated, so a
	// URL/query-shaped paste with no state at all must be rejected the same
	// as one with the wrong state - otherwise a crafted link omitting state
	// entirely would bypass the CSRF check.
	REQUIRE_THROWS_AS(ExtractPastedToken("access_token=ya29.no-state", "abc123"), duckdb::IOException);
}

TEST_CASE("ExtractPastedToken throws when access_token has no value", "[oauth_listener]") {
	REQUIRE_THROWS_AS(ExtractPastedToken("access_token=&state=abc123", "abc123"), duckdb::IOException);
}

TEST_CASE("ExtractPastedToken throws on empty input", "[oauth_listener]") {
	REQUIRE_THROWS_AS(ExtractPastedToken("", "abc123"), duckdb::IOException);
	REQUIRE_THROWS_AS(ExtractPastedToken("   \r\n", "abc123"), duckdb::IOException);
}

TEST_CASE("ExtractPastedToken ignores a param name that only ends with access_token", "[oauth_listener]") {
	// "some_access_token=" doesn't start at a param boundary, so it must not
	// be mistaken for the real "access_token=" param.
	std::string pasted = "some_access_token=decoy&access_token=ya29.real&state=abc123";
	REQUIRE(ExtractPastedToken(pasted, "abc123") == "ya29.real");
}

TEST_CASE("ExtractPastedToken percent-decodes the token from a full redirect URL", "[oauth_listener]") {
	std::string pasted = "http://localhost:8765/#access_token=ya29.has%2Fslash&state=abc123";
	REQUIRE(ExtractPastedToken(pasted, "abc123") == "ya29.has/slash");
}

// =============================================================================
// RunLocalOAuthListener / RunLocalOAuthCodeListener Integration Tests
// =============================================================================
// Plays the role of the browser: connects over a real loopback TCP socket
// (via httplib::Client, the same library the listener itself is now built
// on) and performs the handshake the OAuth redirect page does. This
// exercises the real listen/accept/read/write path on whatever platform CI
// runs on, not just whether it compiles.

namespace {

// High port range, distinct from the extension's real default (8765), so a
// local dev instance of the extension can't collide with the test.
constexpr int TEST_PORT_BASE = 18765;

// Joins the wrapped thread on destruction if it hasn't been joined already.
// Without this, a REQUIRE failing between spawning server_thread and its
// explicit join() (e.g. WaitUntil(listening) timing out on a slow/contended
// CI runner) would unwind the stack with a still-joinable std::thread and
// call std::terminate, crashing the whole test binary instead of just
// failing the one test case.
class AutoJoinThread {
public:
	template <typename Func>
	explicit AutoJoinThread(Func &&func) : thread_(std::forward<Func>(func)) {
	}
	~AutoJoinThread() {
		join();
	}
	AutoJoinThread(const AutoJoinThread &) = delete;
	AutoJoinThread &operator=(const AutoJoinThread &) = delete;

	void join() {
		if (thread_.joinable()) {
			thread_.join();
		}
	}

private:
	std::thread thread_;
};

bool WaitUntil(std::atomic<bool> &flag) {
	for (int i = 0; i < 100 && !flag; i++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	return flag;
}

// Sends a GET (simulating the OAuth redirect landing) then a POST with the
// given body (simulating the redirect page's JS posting back what it parsed
// out of the URL fragment). `host` is "127.0.0.1" or "::1" - the latter
// exercises the IPv6 listener the same way macOS Safari's preference for
// resolving "localhost" to ::1 does in production.
void SendGetThenPost(int port, const std::string &post_body, const std::string &host = "127.0.0.1") {
	duckdb_httplib_openssl::Client cli(host, port);
	cli.set_connection_timeout(2, 0);

	// on_listening fires right after the server is confirmed running, so
	// this should connect on the first try; retry briefly to absorb
	// scheduling jitter.
	duckdb_httplib_openssl::Result get_result;
	for (int attempt = 0; attempt < 50; attempt++) {
		get_result = cli.Get("/");
		if (get_result) {
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	REQUIRE(get_result);

	auto post_result = cli.Post("/", post_body, "application/x-www-form-urlencoded");
	REQUIRE(post_result);
}

// Sends a single GET with the given query string (simulating the OAuth
// redirect carrying the authorization code).
void SendCodeCallbackGet(int port, const std::string &query, const std::string &host = "127.0.0.1") {
	duckdb_httplib_openssl::Client cli(host, port);
	cli.set_connection_timeout(2, 0);

	duckdb_httplib_openssl::Result result;
	for (int attempt = 0; attempt < 50; attempt++) {
		result = cli.Get("/?" + query);
		if (result) {
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	REQUIRE(result);
}

} // namespace

TEST_CASE("RunLocalOAuthListener returns the token posted with the matching state", "[oauth_listener][integration]") {
	const int port = TEST_PORT_BASE;
	const std::string state = "integration-test-state";

	std::atomic<bool> listening {false};
	std::string result;
	std::exception_ptr thread_exception;

	AutoJoinThread server_thread([&]() {
		try {
			result = RunLocalOAuthListener(port, state, [&]() { listening = true; });
		} catch (...) {
			thread_exception = std::current_exception();
		}
	});

	REQUIRE(WaitUntil(listening));

	const std::string fake_token = "ya29.fake-integration-test-token";
	SendGetThenPost(port, "state=" + state + "&access_token=" + fake_token);

	server_thread.join();

	if (thread_exception) {
		std::rethrow_exception(thread_exception);
	}
	REQUIRE(result == fake_token);
}

TEST_CASE("RunLocalOAuthListener accepts the callback over IPv6 (::1)", "[oauth_listener][integration]") {
	// macOS Safari resolves "localhost" to the IPv6 loopback address (::1)
	// ahead of 127.0.0.1; the listener must accept connections there too or
	// the login flow silently fails in Safari even though it works in
	// browsers that fall back to IPv4.
	const int port = TEST_PORT_BASE + 3;
	const std::string state = "ipv6-test-state";

	std::atomic<bool> listening {false};
	std::string result;
	std::exception_ptr thread_exception;

	AutoJoinThread server_thread([&]() {
		try {
			result = RunLocalOAuthListener(port, state, [&]() { listening = true; });
		} catch (...) {
			thread_exception = std::current_exception();
		}
	});

	REQUIRE(WaitUntil(listening));

	const std::string fake_token = "ya29.fake-ipv6-token";
	SendGetThenPost(port, "state=" + state + "&access_token=" + fake_token, "::1");

	server_thread.join();

	if (thread_exception) {
		std::rethrow_exception(thread_exception);
	}
	REQUIRE(result == fake_token);
}

TEST_CASE("RunLocalOAuthListener ignores a callback with the wrong state and waits for the real one",
          "[oauth_listener][integration]") {
	const int port = TEST_PORT_BASE + 1;
	const std::string state = "correct-state";

	std::atomic<bool> listening {false};
	std::string result;
	std::exception_ptr thread_exception;

	AutoJoinThread server_thread([&]() {
		try {
			result = RunLocalOAuthListener(port, state, [&]() { listening = true; });
		} catch (...) {
			thread_exception = std::current_exception();
		}
	});

	REQUIRE(WaitUntil(listening));

	// An attacker (or an unrelated stray request) racing the real flow with
	// a forged/mismatched state must not be accepted.
	SendGetThenPost(port, "state=wrong-state&access_token=attacker-forged-token");

	// The real redirect follows shortly after, with the correct state.
	const std::string real_token = "ya29.the-real-token";
	SendGetThenPost(port, "state=" + state + "&access_token=" + real_token);

	server_thread.join();

	if (thread_exception) {
		std::rethrow_exception(thread_exception);
	}
	REQUIRE(result == real_token);
}

TEST_CASE("RunLocalOAuthListener throws after exhausting its attempt budget", "[oauth_listener][integration]") {
	const int port = TEST_PORT_BASE + 2;
	const std::string state = "correct-state";

	std::atomic<bool> listening {false};
	bool threw = false;

	AutoJoinThread server_thread([&]() {
		try {
			RunLocalOAuthListener(
			    port, state, [&]() { listening = true; }, /*max_attempts=*/2);
		} catch (const std::exception &) {
			threw = true;
		}
	});

	REQUIRE(WaitUntil(listening));

	// Each POST carrying the wrong state counts as one failed attempt;
	// max_attempts=2 means the second one exhausts the budget and the
	// listener should give up rather than hang indefinitely.
	SendGetThenPost(port, "state=wrong&access_token=nope");
	SendGetThenPost(port, "state=wrong&access_token=nope");

	server_thread.join();

	REQUIRE(threw);
}

TEST_CASE("RunLocalOAuthListener returns a token supplied via try_read_pasted_input", "[oauth_listener][integration]") {
	// Simulates the remote-host scenario: the browser's redirect never
	// reaches the listener, but the user pastes the token back manually.
	const int port = TEST_PORT_BASE + 6;
	const std::string state = "paste-test-state";

	std::atomic<bool> listening {false};
	std::atomic<bool> paste_offered {false};
	std::string result;
	std::exception_ptr thread_exception;

	auto try_read_pasted_input = [&](std::string &line) {
		if (paste_offered.exchange(true)) {
			return false; // Only offer the paste once.
		}
		line = "http://localhost:1/#access_token=ya29.pasted-token&state=" + state;
		return true;
	};

	AutoJoinThread server_thread([&]() {
		try {
			result = RunLocalOAuthListener(
			    port, state, [&]() { listening = true; }, /*max_attempts=*/20,
			    /*is_interrupted=*/nullptr, try_read_pasted_input);
		} catch (...) {
			thread_exception = std::current_exception();
		}
	});

	REQUIRE(WaitUntil(listening));
	server_thread.join();

	if (thread_exception) {
		std::rethrow_exception(thread_exception);
	}
	REQUIRE(result == "ya29.pasted-token");
}

TEST_CASE("RunLocalOAuthListener ignores an invalid paste and still accepts the real browser callback",
          "[oauth_listener][integration]") {
	const int port = TEST_PORT_BASE + 7;
	const std::string state = "paste-fallback-state";

	std::atomic<bool> listening {false};
	std::atomic<bool> paste_offered {false};
	std::string result;
	std::exception_ptr thread_exception;

	auto try_read_pasted_input = [&](std::string &line) {
		if (paste_offered.exchange(true)) {
			return false; // Only offer the (bad) paste once.
		}
		line = "access_token=forged&state=wrong-state";
		return true;
	};

	AutoJoinThread server_thread([&]() {
		try {
			result = RunLocalOAuthListener(
			    port, state, [&]() { listening = true; }, /*max_attempts=*/20,
			    /*is_interrupted=*/nullptr, try_read_pasted_input);
		} catch (...) {
			thread_exception = std::current_exception();
		}
	});

	REQUIRE(WaitUntil(listening));
	REQUIRE(WaitUntil(paste_offered));

	const std::string real_token = "ya29.real-after-bad-paste";
	SendGetThenPost(port, "state=" + state + "&access_token=" + real_token);

	server_thread.join();

	if (thread_exception) {
		std::rethrow_exception(thread_exception);
	}
	REQUIRE(result == real_token);
}

// =============================================================================
// PKCE Tests
// =============================================================================

TEST_CASE("GeneratePkceCodeVerifier returns a verifier within the RFC 7636 length range", "[oauth_listener][pkce]") {
	std::string verifier = GeneratePkceCodeVerifier();
	REQUIRE(verifier.length() >= 43);
	REQUIRE(verifier.length() <= 128);
}

TEST_CASE("GeneratePkceCodeVerifier returns different verifiers each call", "[oauth_listener][pkce]") {
	REQUIRE(GeneratePkceCodeVerifier() != GeneratePkceCodeVerifier());
}

TEST_CASE("GeneratePkceCodeChallenge is deterministic for a given verifier", "[oauth_listener][pkce]") {
	std::string verifier = "fixed-test-verifier-value";
	REQUIRE(GeneratePkceCodeChallenge(verifier) == GeneratePkceCodeChallenge(verifier));
}

TEST_CASE("GeneratePkceCodeChallenge differs for different verifiers", "[oauth_listener][pkce]") {
	REQUIRE(GeneratePkceCodeChallenge("verifier-one") != GeneratePkceCodeChallenge("verifier-two"));
}

TEST_CASE("GeneratePkceCodeChallenge matches the known S256 vector from RFC 7636", "[oauth_listener][pkce]") {
	// The example verifier/challenge pair from RFC 7636 Appendix B.
	std::string verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
	REQUIRE(GeneratePkceCodeChallenge(verifier) == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}

// =============================================================================
// BuildAuthorizationCodeUrl Tests
// =============================================================================

TEST_CASE("BuildAuthorizationCodeUrl assembles the expected query string", "[oauth_listener]") {
	std::string url = BuildAuthorizationCodeUrl("https://accounts.google.com/o/oauth2/v2/auth", "my-client-id",
	                                            "http://localhost:8765", "https://www.googleapis.com/auth/spreadsheets",
	                                            "my-state", "my-code-challenge");

	REQUIRE(url == "https://accounts.google.com/o/oauth2/v2/auth"
	               "?client_id=my-client-id"
	               "&redirect_uri=http://localhost:8765"
	               "&response_type=code"
	               "&access_type=offline&prompt=consent"
	               "&scope=https://www.googleapis.com/auth/spreadsheets"
	               "&state=my-state"
	               "&code_challenge=my-code-challenge"
	               "&code_challenge_method=S256");
}

TEST_CASE("BuildAuthorizationCodeUrl does not touch BuildAuthorizationUrl's response_type", "[oauth_listener]") {
	// Regression guard: the default (implicit-grant) flow's URL builder must
	// remain untouched by adding the authorization-code variant.
	std::string url = BuildAuthorizationUrl("AUTH", "CID", "REDIR", "SCOPE", "STATE");
	REQUIRE(url.find("response_type=token") != std::string::npos);
}

// =============================================================================
// ParseAuthorizationCodeCallback Tests
// =============================================================================

TEST_CASE("ParseAuthorizationCodeCallback returns the code when state matches", "[oauth_listener]") {
	std::string code = ParseAuthorizationCodeCallback("/?state=abc123&code=4/0Areal-code-value", "abc123");
	REQUIRE(code == "4/0Areal-code-value");
}

TEST_CASE("ParseAuthorizationCodeCallback throws on state mismatch", "[oauth_listener]") {
	REQUIRE_THROWS_AS(ParseAuthorizationCodeCallback("/?state=attacker-guess&code=forged-code", "abc123"),
	                  duckdb::IOException);
}

TEST_CASE("ParseAuthorizationCodeCallback throws when state is missing entirely", "[oauth_listener]") {
	REQUIRE_THROWS_AS(ParseAuthorizationCodeCallback("/?code=no-state-at-all", "abc123"), duckdb::IOException);
}

TEST_CASE("ParseAuthorizationCodeCallback throws when the code is missing", "[oauth_listener]") {
	REQUIRE_THROWS_AS(ParseAuthorizationCodeCallback("/?state=abc123", "abc123"), duckdb::IOException);
}

// =============================================================================
// ExtractPastedAuthorizationCode Tests
// =============================================================================

TEST_CASE("ExtractPastedAuthorizationCode accepts a bare code with no query string", "[oauth_listener]") {
	REQUIRE(ExtractPastedAuthorizationCode("4/0Abare-code-value", "abc123") == "4/0Abare-code-value");
}

TEST_CASE("ExtractPastedAuthorizationCode extracts the code from a full redirect URL", "[oauth_listener]") {
	std::string pasted = "http://localhost:8765/?code=4/0Afrom-url&scope=https://www.googleapis.com/auth/"
	                     "spreadsheets&state=abc123";
	REQUIRE(ExtractPastedAuthorizationCode(pasted, "abc123") == "4/0Afrom-url");
}

TEST_CASE("ExtractPastedAuthorizationCode throws on state mismatch", "[oauth_listener]") {
	REQUIRE_THROWS_AS(ExtractPastedAuthorizationCode("code=forged&state=wrong-state", "abc123"), duckdb::IOException);
}

TEST_CASE("ExtractPastedAuthorizationCode throws when a query string has no state param", "[oauth_listener]") {
	REQUIRE_THROWS_AS(ExtractPastedAuthorizationCode("code=4/0Ano-state", "abc123"), duckdb::IOException);
}

TEST_CASE("ExtractPastedAuthorizationCode throws on empty input", "[oauth_listener]") {
	REQUIRE_THROWS_AS(ExtractPastedAuthorizationCode("", "abc123"), duckdb::IOException);
}

// =============================================================================
// RunLocalOAuthCodeListener Integration Tests
// =============================================================================

TEST_CASE("RunLocalOAuthCodeListener returns the code from a GET with the matching state",
          "[oauth_listener][integration]") {
	const int port = TEST_PORT_BASE + 8;
	const std::string state = "code-integration-test-state";

	std::atomic<bool> listening {false};
	std::string result;
	std::exception_ptr thread_exception;

	AutoJoinThread server_thread([&]() {
		try {
			result = RunLocalOAuthCodeListener(port, state, [&]() { listening = true; });
		} catch (...) {
			thread_exception = std::current_exception();
		}
	});

	REQUIRE(WaitUntil(listening));

	const std::string fake_code = "4/0Afake-integration-test-code";
	SendCodeCallbackGet(port, "state=" + state + "&code=" + fake_code);

	server_thread.join();

	if (thread_exception) {
		std::rethrow_exception(thread_exception);
	}
	REQUIRE(result == fake_code);
}

TEST_CASE("RunLocalOAuthCodeListener ignores a callback with the wrong state and waits for the real one",
          "[oauth_listener][integration]") {
	const int port = TEST_PORT_BASE + 9;
	const std::string state = "code-correct-state";

	std::atomic<bool> listening {false};
	std::string result;
	std::exception_ptr thread_exception;

	AutoJoinThread server_thread([&]() {
		try {
			result = RunLocalOAuthCodeListener(port, state, [&]() { listening = true; });
		} catch (...) {
			thread_exception = std::current_exception();
		}
	});

	REQUIRE(WaitUntil(listening));

	SendCodeCallbackGet(port, "state=wrong-state&code=attacker-forged-code");

	const std::string real_code = "4/0Athe-real-code";
	SendCodeCallbackGet(port, "state=" + state + "&code=" + real_code);

	server_thread.join();

	if (thread_exception) {
		std::rethrow_exception(thread_exception);
	}
	REQUIRE(result == real_code);
}

TEST_CASE("RunLocalOAuthListener throws InterruptException promptly when is_interrupted becomes true",
          "[oauth_listener][integration]") {
	// Production wires this to ClientContext::IsInterrupted() so Ctrl+C can
	// cancel a pending login - without checking it, the accept loop only
	// gives up after the full 5-minute timeout, which looks like DuckDB
	// hanging (Ctrl+C, and Ctrl+D since the CLI never gets back to its own
	// read loop, both appear to do nothing).
	const int port = TEST_PORT_BASE + 5;
	const std::string state = "interrupt-test-state";

	std::atomic<bool> listening {false};
	std::atomic<bool> interrupted {false};
	bool threw_interrupt = false;

	AutoJoinThread server_thread([&]() {
		try {
			RunLocalOAuthListener(
			    port, state, [&]() { listening = true; }, /*max_attempts=*/20, [&]() { return interrupted.load(); });
		} catch (const duckdb::InterruptException &) {
			threw_interrupt = true;
		} catch (const std::exception &) {
			// Wrong exception type - leave threw_interrupt false so the
			// REQUIRE below reports the mismatch.
		}
	});

	REQUIRE(WaitUntil(listening));
	interrupted = true;

	server_thread.join();

	REQUIRE(threw_interrupt);
}
