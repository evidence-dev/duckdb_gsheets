#include "sheets/auth/oauth_listener.hpp"

#include <cstring>

#include "duckdb/common/exception.hpp"

#include "sheets/auth/socket_compat.hpp"

namespace duckdb {
namespace sheets {

std::string BuildAuthorizationUrl(const std::string &auth_url, const std::string &client_id,
                                   const std::string &redirect_uri, const std::string &scope,
                                   const std::string &state) {
	return auth_url + "?client_id=" + client_id + "&redirect_uri=" + redirect_uri + "&response_type=token" +
	       "&scope=" + scope + "&state=" + state;
}

std::string ExtractAccessTokenFromHttpRequest(const std::string &raw_request) {
	size_t body_start = raw_request.find("\r\n\r\n");
	if (body_start == std::string::npos) {
		return "";
	}
	return raw_request.substr(body_start + 4);
}

std::string RunLocalOAuthListener(int port, const std::function<void()> &on_listening) {
	if (!InitSockets()) {
		throw IOException("Failed to initialize sockets");
	}

	// Create socket
	socket_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == INVALID_SOCKET_VALUE) {
		CleanupSockets();
		throw IOException("Failed to create socket");
	}

	// Set socket options to allow reuse
	int opt = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&opt), sizeof(opt)) < 0) {
		CloseSocket(server_fd);
		CleanupSockets();
		throw IOException("Failed to set socket options");
	}

	// Bind to localhost:port
	struct sockaddr_in address;
	std::memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
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

	// Accept first connection (GET request)
	socket_t client_socket = accept(server_fd, nullptr, nullptr);
	if (client_socket == INVALID_SOCKET_VALUE) {
		CloseSocket(server_fd);
		CleanupSockets();
		throw IOException("Failed to accept connection");
	}

	// Read initial request (unused, just drains the socket)
	char buffer[4096] = {0};
	SocketRecv(client_socket, buffer, static_cast<int>(sizeof(buffer)));

	// Send response to browser: extract the token from the URL fragment client-side
	// (fragments are never sent to the server) and post it back to us.
	std::string response = "HTTP/1.1 200 OK\r\n"
	                        "Access-Control-Allow-Origin: *\r\n"
	                        "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
	                        "Access-Control-Allow-Headers: Content-Type\r\n"
	                        "Content-Type: text/html\r\n\r\n"
	                        "<script>"
	                        "const hash = window.location.hash.substring(1);"
	                        "const params = new URLSearchParams(hash);"
	                        "const token = params.get('access_token');"
	                        "if (token) {"
	                        "  fetch('/', {"
	                        "    method: 'POST',"
	                        "    body: token"
	                        "  }).then(() => {"
	                        "    window.location.href = 'https://duckdb-gsheets.com/oauth#ready=1&access_token=success';"
	                        "  });"
	                        "}"
	                        "</script></body></html>";
	SocketSend(client_socket, response.c_str(), static_cast<int>(response.length()));
	CloseSocket(client_socket);

	// Accept second connection (POST request)
	client_socket = accept(server_fd, nullptr, nullptr);
	if (client_socket == INVALID_SOCKET_VALUE) {
		CloseSocket(server_fd);
		CleanupSockets();
		throw IOException("Failed to accept second connection");
	}

	// Read the POST request
	std::memset(buffer, 0, sizeof(buffer));
	SocketRecv(client_socket, buffer, static_cast<int>(sizeof(buffer)));
	std::string token_request(buffer);

	// Send response to POST request
	std::string post_response = "HTTP/1.1 200 OK\r\n"
	                             "Access-Control-Allow-Origin: *\r\n"
	                             "Content-Length: 0\r\n\r\n";
	SocketSend(client_socket, post_response.c_str(), static_cast<int>(post_response.length()));

	std::string access_token = ExtractAccessTokenFromHttpRequest(token_request);

	// Clean up
	CloseSocket(client_socket);
	CloseSocket(server_fd);
	CleanupSockets();

	if (access_token.empty()) {
		throw IOException("Failed to obtain access token");
	}

	return access_token;
}

} // namespace sheets
} // namespace duckdb
