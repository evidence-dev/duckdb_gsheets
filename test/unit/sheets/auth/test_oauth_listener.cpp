#include "catch.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <string>
#include <thread>

#include "duckdb/common/exception.hpp"
#include "sheets/auth/oauth_listener.hpp"
#include "sheets/auth/socket_compat.hpp"

using namespace duckdb::sheets;

// =============================================================================
// BuildAuthorizationUrl Tests
// =============================================================================

TEST_CASE("BuildAuthorizationUrl assembles the expected query string", "[oauth_listener]") {
	std::string url = BuildAuthorizationUrl("https://accounts.google.com/o/oauth2/v2/auth", "my-client-id",
	                                         "http://localhost:8765", "https://www.googleapis.com/auth/spreadsheets",
	                                         "my-state");

	REQUIRE(url == "https://accounts.google.com/o/oauth2/v2/auth"
	               "?client_id=my-client-id"
	               "&redirect_uri=http://localhost:8765"
	               "&response_type=token"
	               "&scope=https://www.googleapis.com/auth/spreadsheets"
	               "&state=my-state");
}

TEST_CASE("BuildAuthorizationUrl orders parameters client_id, redirect_uri, ..., state", "[oauth_listener]") {
	std::string url = BuildAuthorizationUrl("AUTH", "CID", "REDIR", "SCOPE", "STATE");
	REQUIRE(url.find("client_id=CID") < url.find("redirect_uri=REDIR"));
	REQUIRE(url.find("redirect_uri=REDIR") < url.find("state=STATE"));
}

// =============================================================================
// ExtractHttpBody Tests
// =============================================================================

TEST_CASE("ExtractHttpBody returns the request body", "[oauth_listener]") {
	std::string request = "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\nhello";
	REQUIRE(ExtractHttpBody(request) == "hello");
}

TEST_CASE("ExtractHttpBody returns empty string when there is no body separator", "[oauth_listener]") {
	std::string request = "POST / HTTP/1.1\r\nHost: localhost";
	REQUIRE(ExtractHttpBody(request).empty());
}

TEST_CASE("ExtractHttpBody returns empty string for an empty body", "[oauth_listener]") {
	std::string request = "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n";
	REQUIRE(ExtractHttpBody(request).empty());
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

// =============================================================================
// RunLocalOAuthListener Integration Tests
// =============================================================================
// Plays the role of the browser: connects over a real loopback TCP socket and
// performs the same two-step GET-then-POST handshake the OAuth redirect page
// does. This exercises the real platform socket code - POSIX read/write/close
// on macOS/Linux, Winsock recv/send/closesocket when this test runs on
// Windows CI - not just whether it compiles.

namespace {

// High port range, distinct from the extension's real default (8765), so a
// local dev instance of the extension can't collide with the test.
constexpr int TEST_PORT_BASE = 18765;

// `family` is AF_INET (127.0.0.1) or AF_INET6 (::1) - the latter is used to
// exercise the IPv6 listener socket the same way macOS Safari's preference
// for resolving "localhost" to ::1 does in production.
bool ConnectToLoopback(int port, socket_t &out_socket, int family = AF_INET) {
	socket_t sock = socket(family, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET_VALUE) {
		return false;
	}

	struct sockaddr_storage address;
	std::memset(&address, 0, sizeof(address));
	socklen_t address_len;
	if (family == AF_INET) {
		auto *addr4 = reinterpret_cast<struct sockaddr_in *>(&address);
		addr4->sin_family = AF_INET;
		addr4->sin_port = htons(static_cast<uint16_t>(port));
		inet_pton(AF_INET, "127.0.0.1", &addr4->sin_addr);
		address_len = sizeof(struct sockaddr_in);
	} else {
		auto *addr6 = reinterpret_cast<struct sockaddr_in6 *>(&address);
		addr6->sin6_family = AF_INET6;
		addr6->sin6_port = htons(static_cast<uint16_t>(port));
		addr6->sin6_addr = in6addr_loopback;
		address_len = sizeof(struct sockaddr_in6);
	}

	// on_listening fires right after listen() succeeds, so this should
	// connect on the first try; retry briefly to absorb scheduling jitter.
	for (int attempt = 0; attempt < 50; attempt++) {
		if (connect(sock, reinterpret_cast<struct sockaddr *>(&address), address_len) == 0) {
			out_socket = sock;
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	CloseSocket(sock);
	return false;
}

bool WaitUntil(std::atomic<bool> &flag) {
	for (int i = 0; i < 100 && !flag; i++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	return flag;
}

// Joins the wrapped thread on destruction if it hasn't been joined already.
// Without this, a REQUIRE failing between spawning server_thread and its
// explicit join() (e.g. WaitUntil(listening) timing out on a slow/contended
// CI runner) would unwind the stack with a still-joinable std::thread and
// call std::terminate, crashing the whole test binary instead of just
// failing the one test case.
class AutoJoinThread {
public:
	template <typename Func> explicit AutoJoinThread(Func &&func) : thread_(std::forward<Func>(func)) {
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

// Sends a GET (simulating the OAuth redirect landing) then a POST with the
// given body (simulating the redirect page's JS posting back what it parsed
// out of the URL fragment), draining each response.
void SendGetThenPost(int port, const std::string &post_body, int family = AF_INET) {
	socket_t get_socket;
	REQUIRE(ConnectToLoopback(port, get_socket, family));
	std::string get_request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
	SocketSend(get_socket, get_request.c_str(), static_cast<int>(get_request.length()));
	char get_response[4096] = {0};
	SocketRecv(get_socket, get_response, sizeof(get_response) - 1);
	CloseSocket(get_socket);

	socket_t post_socket;
	REQUIRE(ConnectToLoopback(port, post_socket, family));
	std::string post_request = "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: " +
	                            std::to_string(post_body.length()) + "\r\n\r\n" + post_body;
	SocketSend(post_socket, post_request.c_str(), static_cast<int>(post_request.length()));
	char post_response[4096] = {0};
	SocketRecv(post_socket, post_response, sizeof(post_response) - 1);
	CloseSocket(post_socket);
}

// Like the POST half of SendGetThenPost, but writes the headers and body as
// two separate send() calls with a short pause in between - reproducing
// what Safari's fetch() was observed to do on the wire (see ReadHttpRequest
// in oauth_listener.cpp), where a single recv() on the server only picks up
// the headers and the body arrives a moment later on its own.
void SendPostSplitAcrossWrites(int port, const std::string &post_body, int family = AF_INET) {
	socket_t post_socket;
	REQUIRE(ConnectToLoopback(port, post_socket, family));
	std::string headers = "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: " +
	                       std::to_string(post_body.length()) + "\r\n\r\n";
	SocketSend(post_socket, headers.c_str(), static_cast<int>(headers.length()));
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	SocketSend(post_socket, post_body.c_str(), static_cast<int>(post_body.length()));
	char post_response[4096] = {0};
	SocketRecv(post_socket, post_response, sizeof(post_response) - 1);
	CloseSocket(post_socket);
}

} // namespace

TEST_CASE("RunLocalOAuthListener returns the token posted with the matching state",
          "[oauth_listener][integration]") {
	InitSockets();
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
	CleanupSockets();

	if (thread_exception) {
		std::rethrow_exception(thread_exception);
	}
	REQUIRE(result == fake_token);
}

TEST_CASE("RunLocalOAuthListener accepts the callback over IPv6 (::1)", "[oauth_listener][integration]") {
	// macOS Safari resolves "localhost" to the IPv6 loopback address (::1)
	// ahead of 127.0.0.1; the listener must accept connections there too or
	// the login flow silently fails in Safari even though it works in
	// browsers that fall back to IPv4 (see CreateLoopbackListener).
	InitSockets();
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
	SendGetThenPost(port, "state=" + state + "&access_token=" + fake_token, AF_INET6);

	server_thread.join();
	CleanupSockets();

	if (thread_exception) {
		std::rethrow_exception(thread_exception);
	}
	REQUIRE(result == fake_token);
}

TEST_CASE("RunLocalOAuthListener assembles a POST whose body arrives in a separate TCP write",
          "[oauth_listener][integration]") {
	// This is the actual root cause behind the real-world Safari bug: Safari's
	// fetch() was observed sending the POST's headers and body as two
	// separate writes, arriving as two separate recv()s on the server. A
	// naive single-recv() read (the previous implementation) would see a
	// complete set of headers ending in the blank line and treat that as the
	// whole request, silently getting an empty body and rejecting the real
	// token forever. ReadHttpRequest must keep reading per Content-Length
	// instead of assuming one recv() has everything.
	InitSockets();
	const int port = TEST_PORT_BASE + 4;
	const std::string state = "split-write-state";

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

	const std::string fake_token = "ya29.fake-split-write-token";
	SendPostSplitAcrossWrites(port, "state=" + state + "&access_token=" + fake_token);

	server_thread.join();
	CleanupSockets();

	if (thread_exception) {
		std::rethrow_exception(thread_exception);
	}
	REQUIRE(result == fake_token);
}

TEST_CASE("RunLocalOAuthListener ignores a callback with the wrong state and waits for the real one",
          "[oauth_listener][integration]") {
	InitSockets();
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
	CleanupSockets();

	if (thread_exception) {
		std::rethrow_exception(thread_exception);
	}
	REQUIRE(result == real_token);
}

TEST_CASE("RunLocalOAuthListener throws after exhausting its attempt budget", "[oauth_listener][integration]") {
	InitSockets();
	const int port = TEST_PORT_BASE + 2;
	const std::string state = "correct-state";

	std::atomic<bool> listening {false};
	bool threw = false;

	AutoJoinThread server_thread([&]() {
		try {
			RunLocalOAuthListener(port, state, [&]() { listening = true; }, /*max_attempts=*/2);
		} catch (const std::exception &) {
			threw = true;
		}
	});

	REQUIRE(WaitUntil(listening));

	// max_attempts=2 covers exactly one GET+POST round; since this POST
	// carries the wrong state, that exhausts the budget and the listener
	// should give up rather than hang indefinitely.
	SendGetThenPost(port, "state=wrong&access_token=nope");

	server_thread.join();
	CleanupSockets();

	REQUIRE(threw);
}

TEST_CASE("RunLocalOAuthListener returns a token supplied via try_read_pasted_input", "[oauth_listener][integration]") {
	// Simulates the remote-host scenario: the browser's redirect never
	// reaches the listener, but the user pastes the token back manually.
	InitSockets();
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
	CleanupSockets();

	if (thread_exception) {
		std::rethrow_exception(thread_exception);
	}
	REQUIRE(result == "ya29.pasted-token");
}

TEST_CASE("RunLocalOAuthListener ignores an invalid paste and still accepts the real browser callback",
          "[oauth_listener][integration]") {
	InitSockets();
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
	CleanupSockets();

	if (thread_exception) {
		std::rethrow_exception(thread_exception);
	}
	REQUIRE(result == real_token);
}

TEST_CASE("RunLocalOAuthListener throws InterruptException promptly when is_interrupted becomes true",
          "[oauth_listener][integration]") {
	// Production wires this to ClientContext::IsInterrupted() so Ctrl+C can
	// cancel a pending login - without checking it, the accept loop only
	// gives up after the full 5-minute timeout, which looks like DuckDB
	// hanging (Ctrl+C, and Ctrl+D since the CLI never gets back to its own
	// read loop, both appear to do nothing).
	InitSockets();
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
	CleanupSockets();

	REQUIRE(threw_interrupt);
}
