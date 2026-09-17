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

bool ConnectToLoopback(int port, socket_t &out_socket) {
	socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET_VALUE) {
		return false;
	}

	struct sockaddr_in address;
	std::memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(static_cast<uint16_t>(port));
	inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

	// on_listening fires right after listen() succeeds, so this should
	// connect on the first try; retry briefly to absorb scheduling jitter.
	for (int attempt = 0; attempt < 50; attempt++) {
		if (connect(sock, (struct sockaddr *)&address, sizeof(address)) == 0) {
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

// Sends a GET (simulating the OAuth redirect landing) then a POST with the
// given body (simulating the redirect page's JS posting back what it parsed
// out of the URL fragment), draining each response.
void SendGetThenPost(int port, const std::string &post_body) {
	socket_t get_socket;
	REQUIRE(ConnectToLoopback(port, get_socket));
	std::string get_request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
	SocketSend(get_socket, get_request.c_str(), static_cast<int>(get_request.length()));
	char get_response[4096] = {0};
	SocketRecv(get_socket, get_response, sizeof(get_response) - 1);
	CloseSocket(get_socket);

	socket_t post_socket;
	REQUIRE(ConnectToLoopback(port, post_socket));
	std::string post_request = "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: " +
	                            std::to_string(post_body.length()) + "\r\n\r\n" + post_body;
	SocketSend(post_socket, post_request.c_str(), static_cast<int>(post_request.length()));
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

	std::thread server_thread([&]() {
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

TEST_CASE("RunLocalOAuthListener ignores a callback with the wrong state and waits for the real one",
          "[oauth_listener][integration]") {
	InitSockets();
	const int port = TEST_PORT_BASE + 1;
	const std::string state = "correct-state";

	std::atomic<bool> listening {false};
	std::string result;
	std::exception_ptr thread_exception;

	std::thread server_thread([&]() {
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

	std::thread server_thread([&]() {
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
