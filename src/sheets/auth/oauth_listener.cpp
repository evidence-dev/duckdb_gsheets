#include "sheets/auth/oauth_listener.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "duckdb/common/exception.hpp"

#include "sheets/auth/socket_compat.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace duckdb {
namespace sheets {

namespace {

constexpr int BUFFER_SIZE = 8192;
// accept() has no built-in timeout, so a browser that never completes the
// redirect (headless environment, user abandons the flow, etc.) would
// otherwise block this call forever regardless of max_attempts. Poll with
// select() instead and bound the whole wait by a wall-clock deadline.
constexpr int ACCEPT_POLL_SECONDS = 1;

bool StartsWith(const std::string &s, const std::string &prefix) {
	return s.compare(0, prefix.size(), prefix) == 0;
}

// A single recv() isn't guaranteed to return a whole HTTP request: some
// clients (observed with Safari's fetch()) write the headers and body as
// separate TCP writes, so the headers can arrive first with the body still
// in flight. This keeps reading - first until the header/body separator is
// found, then until at least as many body bytes as Content-Length declares
// have arrived - instead of silently treating a not-yet-arrived body as
// empty. Bounded by MAX_REQUEST_SIZE so a connection that never completes a
// request can't grow this buffer without limit.
std::string ReadHttpRequest(socket_t client_socket) {
	constexpr size_t MAX_REQUEST_SIZE = 65536;
	std::string request;
	char buffer[BUFFER_SIZE];

	size_t header_end = std::string::npos;
	size_t content_length = 0;

	while (request.size() < MAX_REQUEST_SIZE) {
		int bytes_read = SocketRecv(client_socket, buffer, static_cast<int>(sizeof(buffer)));
		if (bytes_read <= 0) {
			break;
		}
		request.append(buffer, static_cast<size_t>(bytes_read));

		if (header_end == std::string::npos) {
			header_end = request.find("\r\n\r\n");
			if (header_end == std::string::npos) {
				continue;
			}
			std::string headers = request.substr(0, header_end);
			std::transform(headers.begin(), headers.end(), headers.begin(),
			                [](unsigned char c) { return std::tolower(c); });
			size_t cl_pos = headers.find("content-length:");
			if (cl_pos != std::string::npos) {
				try {
					content_length = static_cast<size_t>(std::stoul(headers.substr(cl_pos + 15)));
				} catch (const std::exception &) {
					// Malformed Content-Length value - treat as no body left
					// to wait for; ParseTokenPayload will reject the
					// (already malformed) request downstream as usual.
				}
			}
		}

		size_t body_so_far = request.size() - (header_end + 4);
		if (body_so_far >= content_length) {
			break;
		}
	}

	return request;
}

// Waits up to `timeout_seconds` for either `fd_a` or `fd_b` (either may be
// INVALID_SOCKET_VALUE if that socket wasn't created) to have an incoming
// connection ready to accept(). Returns whichever fd is ready, or
// INVALID_SOCKET_VALUE on timeout, so the caller can re-check the overall
// deadline instead of blocking indefinitely.
socket_t WaitForConnection(socket_t fd_a, socket_t fd_b, int timeout_seconds) {
	fd_set read_fds;
	FD_ZERO(&read_fds);

	int max_fd = -1;
	if (fd_a != INVALID_SOCKET_VALUE) {
		FD_SET(fd_a, &read_fds);
		max_fd = std::max(max_fd, static_cast<int>(fd_a));
	}
	if (fd_b != INVALID_SOCKET_VALUE) {
		FD_SET(fd_b, &read_fds);
		max_fd = std::max(max_fd, static_cast<int>(fd_b));
	}
	if (max_fd < 0) {
		return INVALID_SOCKET_VALUE;
	}

	struct timeval tv;
	tv.tv_sec = timeout_seconds;
	tv.tv_usec = 0;

	if (select(max_fd + 1, &read_fds, nullptr, nullptr, &tv) <= 0) {
		return INVALID_SOCKET_VALUE;
	}
	if (fd_a != INVALID_SOCKET_VALUE && FD_ISSET(fd_a, &read_fds)) {
		return fd_a;
	}
	if (fd_b != INVALID_SOCKET_VALUE && FD_ISSET(fd_b, &read_fds)) {
		return fd_b;
	}
	return INVALID_SOCKET_VALUE;
}

// Creates, binds (to the loopback address for `family`) and listens on a
// socket for the OAuth callback. `family` is AF_INET or AF_INET6.
//
// macOS Safari resolves "localhost" to the IPv6 loopback address (::1)
// ahead of 127.0.0.1 and, unlike Chromium/Firefox, doesn't reliably fall
// back to IPv4 when nothing answers there - so an IPv4-only listener made
// the whole login flow silently fail in Safari even though the redirect
// URI ("http://localhost:<port>") worked fine in other browsers. Listening
// on both families sidesteps that without touching the redirect URI (which
// must stay in sync with what's registered for the OAuth client).
//
// IPv6 support is best-effort: if creating/binding the IPv6 socket fails
// (disabled at the OS level, sandboxed environment, etc.) this returns
// INVALID_SOCKET_VALUE rather than throwing, and the caller proceeds with
// IPv4 only, matching prior behavior.
socket_t CreateLoopbackListener(int family, int port, bool required) {
	socket_t fd = socket(family, SOCK_STREAM, 0);
	if (fd == INVALID_SOCKET_VALUE) {
		if (required) {
			throw IOException("Failed to create socket");
		}
		return INVALID_SOCKET_VALUE;
	}

	int opt = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&opt), sizeof(opt));
	if (family == AF_INET6) {
		// Without this, some platforms (Linux) let the IPv6 socket also
		// accept IPv4 connections, which would collide with the separate
		// IPv4 socket bound to the same port.
		setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char *>(&opt), sizeof(opt));
	}

	bool bound = false;
	if (family == AF_INET) {
		struct sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(static_cast<uint16_t>(port));
		bound = bind(fd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) == 0;
	} else {
		struct sockaddr_in6 address;
		std::memset(&address, 0, sizeof(address));
		address.sin6_family = AF_INET6;
		address.sin6_addr = in6addr_loopback;
		address.sin6_port = htons(static_cast<uint16_t>(port));
		bound = bind(fd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) == 0;
	}

	// Backlog > 1: browsers (Safari in particular) fire off several extra
	// same-origin requests right after loading the redirect page - favicon
	// and Apple touch-icon probes were observed racing the real POST here -
	// so a backlog of 1 risked the kernel refusing the real callback's
	// connection outright if it arrived while one of those was still queued.
	if (!bound || listen(fd, 8) < 0) {
		CloseSocket(fd);
		if (required) {
			throw IOException("Failed to bind to port " + std::to_string(port));
		}
		return INVALID_SOCKET_VALUE;
	}
	return fd;
}

void SendResponse(socket_t client_socket, const std::string &response) {
	SocketSend(client_socket, response.c_str(), static_cast<int>(response.length()));
}

// The page served for the browser's GET after the OAuth redirect. Runs
// client-side, in the browser, at the http://localhost:<port> origin: reads
// the access_token/state out of the URL fragment (fragments are never sent
// to any server) and POSTs them back to us, same-origin - so no CORS
// headers are needed, or served, on either response.
std::string BuildRedirectPageResponse() {
	std::string body =
	    "<script>"
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
	return "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: " +
	       std::to_string(body.length()) + "\r\n\r\n" + body;
}

std::string BuildAckResponse() {
	return "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
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
	return WaitForSingleObject(handle, 0) == WAIT_OBJECT_0;
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
                                   const std::string &redirect_uri, const std::string &scope,
                                   const std::string &state) {
	return auth_url + "?client_id=" + client_id + "&redirect_uri=" + redirect_uri + "&response_type=token" +
	       "&scope=" + scope + "&state=" + state;
}

std::string ExtractHttpBody(const std::string &raw_request) {
	size_t body_start = raw_request.find("\r\n\r\n");
	if (body_start == std::string::npos) {
		return "";
	}
	return raw_request.substr(body_start + 4);
}

std::string ParseTokenPayload(const std::string &body, const std::string &expected_state) {
	const std::string state_prefix = "state=";
	const std::string token_marker = "&access_token=";

	if (!StartsWith(body, state_prefix)) {
		throw IOException("Malformed OAuth callback payload");
	}
	size_t token_marker_pos = body.find(token_marker);
	if (token_marker_pos == std::string::npos) {
		throw IOException("Malformed OAuth callback payload");
	}

	std::string received_state = body.substr(state_prefix.size(), token_marker_pos - state_prefix.size());
	std::string token = body.substr(token_marker_pos + token_marker.size());

	// This is the CSRF check: only a payload that echoes back the random
	// state we generated for this specific flow is accepted, so a stray
	// local process or browser tab that also reaches this listener can't
	// inject an arbitrary access token.
	if (received_state.empty() || received_state != expected_state) {
		throw IOException("OAuth state mismatch - rejecting callback");
	}
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

	std::string state = ExtractQueryParam(trimmed, "state");
	if (!state.empty() && state != expected_state) {
		throw IOException("OAuth state mismatch - rejecting pasted token");
	}

	return token;
}

bool TryReadPastedLine(std::string &line) {
	if (!StdinHasPendingData()) {
		return false;
	}
	return static_cast<bool>(std::getline(std::cin, line));
}

std::string RunLocalOAuthListener(int port, const std::string &expected_state,
                                   const std::function<void()> &on_listening, int max_attempts,
                                   const std::function<bool()> &is_interrupted,
                                   const std::function<bool(std::string &)> &try_read_pasted_input) {
	if (!InitSockets()) {
		throw IOException("Failed to initialize sockets");
	}

	// Bind to loopback only - 127.0.0.1 and, best-effort, ::1 - not all
	// interfaces, so this listener is never reachable from other machines on
	// the network. IPv4 is required; see CreateLoopbackListener for why IPv6
	// is also attempted.
	socket_t server_fd_v4;
	try {
		server_fd_v4 = CreateLoopbackListener(AF_INET, port, /*required=*/true);
	} catch (...) {
		CleanupSockets();
		throw;
	}
	socket_t server_fd_v6 = CreateLoopbackListener(AF_INET6, port, /*required=*/false);

	if (on_listening) {
		on_listening();
	}

	// Any local process or open browser tab can connect to this listener, so
	// a single connection can't be trusted to be the real OAuth redirect.
	// Serve the redirect page to GETs and keep accepting connections until a
	// POST carrying the correct `state` arrives (see ParseTokenPayload),
	// ignoring anything else, up to a bounded number of attempts - and never
	// longer than kOAuthListenerTimeoutSeconds wall-clock, even if no one ever
	// connects at all.
	auto close_listeners = [&]() {
		CloseSocket(server_fd_v4);
		if (server_fd_v6 != INVALID_SOCKET_VALUE) {
			CloseSocket(server_fd_v6);
		}
	};

	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kOAuthListenerTimeoutSeconds);
	for (int attempt = 0; attempt < max_attempts;) {
		if (std::chrono::steady_clock::now() >= deadline) {
			break;
		}
		if (is_interrupted && is_interrupted()) {
			close_listeners();
			CleanupSockets();
			throw InterruptException();
		}
		if (try_read_pasted_input) {
			std::string pasted_line;
			if (try_read_pasted_input(pasted_line)) {
				try {
					std::string token = ExtractPastedToken(pasted_line, expected_state);
					close_listeners();
					CleanupSockets();
					return token;
				} catch (const Exception &e) {
					// Not a usable paste (wrong/missing state, no token
					// found) - keep waiting for either a corrected paste or
					// the real browser redirect, same as an HTTP callback
					// that fails ParseTokenPayload below.
					std::cerr << "Ignoring pasted input: " << e.what() << '\n';
					continue;
				}
			}
		}
		socket_t ready_fd = WaitForConnection(server_fd_v4, server_fd_v6, ACCEPT_POLL_SECONDS);
		if (ready_fd == INVALID_SOCKET_VALUE) {
			continue;
		}

		socket_t client_socket = accept(ready_fd, nullptr, nullptr);
		if (client_socket == INVALID_SOCKET_VALUE) {
			attempt++;
			continue;
		}
		attempt++;

		std::string request = ReadHttpRequest(client_socket);

		bool is_post = StartsWith(request, "POST ");
		SendResponse(client_socket, is_post ? BuildAckResponse() : BuildRedirectPageResponse());
		CloseSocket(client_socket);

		if (!is_post) {
			continue;
		}

		try {
			std::string token = ParseTokenPayload(ExtractHttpBody(request), expected_state);
			close_listeners();
			CleanupSockets();
			return token;
		} catch (const Exception &) {
			// Not our callback (wrong/missing state, malformed body, or a
			// stray request) - keep waiting for the real one.
			continue;
		}
	}

	close_listeners();
	CleanupSockets();
	throw IOException("Timed out waiting for a valid OAuth callback");
}

} // namespace sheets
} // namespace duckdb
