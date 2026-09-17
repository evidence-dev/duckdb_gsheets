#include "sheets/auth/oauth_listener.hpp"

#include <cstdint>
#include <cstring>

#include "duckdb/common/exception.hpp"

#include "sheets/auth/socket_compat.hpp"

namespace duckdb {
namespace sheets {

namespace {

constexpr int BUFFER_SIZE = 8192;

bool StartsWith(const std::string &s, const std::string &prefix) {
	return s.compare(0, prefix.size(), prefix) == 0;
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

std::string RunLocalOAuthListener(int port, const std::string &expected_state,
                                   const std::function<void()> &on_listening, int max_attempts) {
	if (!InitSockets()) {
		throw IOException("Failed to initialize sockets");
	}

	socket_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == INVALID_SOCKET_VALUE) {
		CleanupSockets();
		throw IOException("Failed to create socket");
	}

	int opt = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&opt), sizeof(opt)) < 0) {
		CloseSocket(server_fd);
		CleanupSockets();
		throw IOException("Failed to set socket options");
	}

	// Bind to loopback only (127.0.0.1), not all interfaces - this listener
	// must not be reachable from other machines on the network.
	struct sockaddr_in address;
	std::memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = htons(static_cast<uint16_t>(port));

	if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		CloseSocket(server_fd);
		CleanupSockets();
		throw IOException("Failed to bind to port " + std::to_string(port));
	}

	if (listen(server_fd, 1) < 0) {
		CloseSocket(server_fd);
		CleanupSockets();
		throw IOException("Failed to listen on socket");
	}

	if (on_listening) {
		on_listening();
	}

	// Any local process or open browser tab can connect to this listener, so
	// a single connection can't be trusted to be the real OAuth redirect.
	// Serve the redirect page to GETs and keep accepting connections until a
	// POST carrying the correct `state` arrives (see ParseTokenPayload),
	// ignoring anything else, up to a bounded number of attempts.
	for (int attempt = 0; attempt < max_attempts; attempt++) {
		socket_t client_socket = accept(server_fd, nullptr, nullptr);
		if (client_socket == INVALID_SOCKET_VALUE) {
			continue;
		}

		char buffer[BUFFER_SIZE];
		int bytes_read = SocketRecv(client_socket, buffer, static_cast<int>(sizeof(buffer)));
		std::string request(buffer, bytes_read > 0 ? static_cast<size_t>(bytes_read) : 0);

		bool is_post = StartsWith(request, "POST ");
		SendResponse(client_socket, is_post ? BuildAckResponse() : BuildRedirectPageResponse());
		CloseSocket(client_socket);

		if (!is_post) {
			continue;
		}

		try {
			std::string token = ParseTokenPayload(ExtractHttpBody(request), expected_state);
			CloseSocket(server_fd);
			CleanupSockets();
			return token;
		} catch (const Exception &) {
			// Not our callback (wrong/missing state, malformed body, or a
			// stray request) - keep waiting for the real one.
			continue;
		}
	}

	CloseSocket(server_fd);
	CleanupSockets();
	throw IOException("Timed out waiting for a valid OAuth callback");
}

} // namespace sheets
} // namespace duckdb
