#include "catch.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <string>
#include <thread>

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
// ExtractAccessTokenFromHttpRequest Tests
// =============================================================================

TEST_CASE("ExtractAccessTokenFromHttpRequest returns the request body", "[oauth_listener]") {
	std::string request = "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\ntoken";
	REQUIRE(ExtractAccessTokenFromHttpRequest(request) == "token");
}

TEST_CASE("ExtractAccessTokenFromHttpRequest handles a realistic ya29 token", "[oauth_listener]") {
	std::string token = "ya29.a0AfH6SMB_test_token_value";
	std::string request =
	    "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: " + std::to_string(token.length()) + "\r\n\r\n" + token;
	REQUIRE(ExtractAccessTokenFromHttpRequest(request) == token);
}

TEST_CASE("ExtractAccessTokenFromHttpRequest returns empty string when there is no body separator",
          "[oauth_listener]") {
	std::string request = "POST / HTTP/1.1\r\nHost: localhost";
	REQUIRE(ExtractAccessTokenFromHttpRequest(request).empty());
}

TEST_CASE("ExtractAccessTokenFromHttpRequest returns empty string for an empty body", "[oauth_listener]") {
	std::string request = "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n";
	REQUIRE(ExtractAccessTokenFromHttpRequest(request).empty());
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
constexpr int TEST_PORT_TOKEN = 18765;
constexpr int TEST_PORT_NO_TOKEN = 18766;

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

} // namespace

TEST_CASE("RunLocalOAuthListener returns the token posted by the redirect page", "[oauth_listener][integration]") {
	InitSockets();

	std::atomic<bool> listening {false};
	std::string result;
	std::exception_ptr thread_exception;

	std::thread server_thread([&]() {
		try {
			result = RunLocalOAuthListener(TEST_PORT_TOKEN, [&]() { listening = true; });
		} catch (...) {
			thread_exception = std::current_exception();
		}
	});

	REQUIRE(WaitUntil(listening));

	const std::string fake_token = "ya29.fake-integration-test-token";

	// Step 1: simulate the browser's initial GET after the OAuth redirect.
	socket_t get_socket;
	REQUIRE(ConnectToLoopback(TEST_PORT_TOKEN, get_socket));
	std::string get_request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
	SocketSend(get_socket, get_request.c_str(), static_cast<int>(get_request.length()));
	char get_response[4096] = {0};
	SocketRecv(get_socket, get_response, sizeof(get_response) - 1);
	CloseSocket(get_socket);

	// Step 2: simulate the page's JS posting the extracted token back to us.
	socket_t post_socket;
	REQUIRE(ConnectToLoopback(TEST_PORT_TOKEN, post_socket));
	std::string post_request = "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: " +
	                            std::to_string(fake_token.length()) + "\r\n\r\n" + fake_token;
	SocketSend(post_socket, post_request.c_str(), static_cast<int>(post_request.length()));
	char post_response[4096] = {0};
	SocketRecv(post_socket, post_response, sizeof(post_response) - 1);
	CloseSocket(post_socket);

	server_thread.join();
	CleanupSockets();

	if (thread_exception) {
		std::rethrow_exception(thread_exception);
	}
	REQUIRE(result == fake_token);
}

TEST_CASE("RunLocalOAuthListener throws when no token is posted", "[oauth_listener][integration]") {
	InitSockets();

	std::atomic<bool> listening {false};
	bool threw = false;

	std::thread server_thread([&]() {
		try {
			RunLocalOAuthListener(TEST_PORT_NO_TOKEN, [&]() { listening = true; });
		} catch (const std::exception &) {
			threw = true;
		}
	});

	REQUIRE(WaitUntil(listening));

	socket_t get_socket;
	REQUIRE(ConnectToLoopback(TEST_PORT_NO_TOKEN, get_socket));
	std::string get_request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
	SocketSend(get_socket, get_request.c_str(), static_cast<int>(get_request.length()));
	char get_response[4096] = {0};
	SocketRecv(get_socket, get_response, sizeof(get_response) - 1);
	CloseSocket(get_socket);

	// Post an empty body, as if the redirect page never received a token.
	socket_t post_socket;
	REQUIRE(ConnectToLoopback(TEST_PORT_NO_TOKEN, post_socket));
	std::string post_request = "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n";
	SocketSend(post_socket, post_request.c_str(), static_cast<int>(post_request.length()));
	char post_response[4096] = {0};
	SocketRecv(post_socket, post_response, sizeof(post_response) - 1);
	CloseSocket(post_socket);

	server_thread.join();
	CleanupSockets();

	REQUIRE(threw);
}
