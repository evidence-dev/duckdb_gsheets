#include <fstream>
#include <cstdlib>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif
#include <json.hpp>

#include "duckdb/common/exception/binder_exception.hpp"

#include "gsheets_auth.hpp"
#include "gsheets_utils.hpp"
#include "utils/options.hpp"

using json = nlohmann::json;

namespace duckdb {

// This code is copied, with minor modifications from
// https://github.com/duckdb/duckdb_azure/blob/main/src/azure_secret.cpp
static void CopySecret(const std::string &key, const CreateSecretInput &input, KeyValueSecret &result) {
	auto val = input.options.find(key);

	if (val != input.options.end()) {
		result.secret_map[key] = val->second;
	}
}

static void RegisterCommonSecretParameters(CreateSecretFunction &function) {
	// Register google sheets common parameters
	function.named_parameters["token"] = LogicalType::VARCHAR;
}

static void RedactCommonKeys(KeyValueSecret &result) {
	result.redact_keys.insert("proxy_password");
}

static unique_ptr<BaseSecret> CreateGsheetSecretFromAccessToken(ClientContext &context, CreateSecretInput &input) {
	auto scope = input.scope;

	auto result = make_uniq<KeyValueSecret>(scope, input.type, input.provider, input.name);

	// Manage specific secret option
	CopySecret("token", input, *result);

	// Redact sensible keys
	RedactCommonKeys(*result);
	result->redact_keys.insert("token");

	return std::move(result);
}

static unique_ptr<BaseSecret> CreateGsheetSecretFromOAuth(ClientContext &context, CreateSecretInput &input) {
	auto scope = input.scope;

	auto result = make_uniq<KeyValueSecret>(scope, input.type, input.provider, input.name);

	// Initiate OAuth flow
	string token = InitiateOAuthFlow();

	result->secret_map["token"] = token;

	// Redact sensible keys
	RedactCommonKeys(*result);
	result->redact_keys.insert("token");

	return std::move(result);
}

static unique_ptr<BaseSecret> CreateGsheetSecretFromKeyFile(ClientContext &context, CreateSecretInput &input) {
	auto scope = input.scope;

	auto result = make_uniq<KeyValueSecret>(scope, input.type, input.provider, input.name);

	std::string email, secret;
	auto filepath = duckdb::sheets::GetStringOption(input.options, "filepath");
	if (filepath.empty()) {
		email = duckdb::sheets::GetStringOption(input.options, "email");
		if (email.empty()) {
			throw BinderException("Must provide email if not using filepath");
		}
		secret = duckdb::sheets::GetStringOption(input.options, "secret");
		if (email.empty()) {
			throw BinderException("Must provide secret value if not using filepath");
		}
	} else {
		std::ifstream ifs(filepath);
		if (!ifs.is_open()) {
			throw IOException("Could not open JSON key file at: " + filepath);
		}
		json credentials_file = json::parse(ifs);
		email = credentials_file["client_email"].get<std::string>();
		secret = credentials_file["private_key"].get<std::string>();
	}

	// Manage specific secret option
	(*result).secret_map["email"] = Value(email);
	(*result).secret_map["secret"] = Value(secret);
	CopySecret("filepath", input, *result); // Store the filepath anyway

	const auto result_const = *result;

	// Redact sensible keys
	RedactCommonKeys(*result);
	result->redact_keys.insert("secret");
	result->redact_keys.insert("filepath");
	result->redact_keys.insert("token");

	return std::move(result);
}

void CreateGsheetSecretFunctions::Register(ExtensionLoader &loader) {
	string type = "gsheet";

	// Register the new type
	SecretType secret_type;
	secret_type.name = type;
	secret_type.deserializer = KeyValueSecret::Deserialize<KeyValueSecret>;
	secret_type.default_provider = "oauth";

	// Register the access_token secret provider
	CreateSecretFunction access_token_function = {type, "access_token", CreateGsheetSecretFromAccessToken, {}};
	access_token_function.named_parameters["access_token"] = LogicalType::VARCHAR;
	RegisterCommonSecretParameters(access_token_function);

	// Register the oauth secret provider
	CreateSecretFunction oauth_function = {type, "oauth", CreateGsheetSecretFromOAuth, {}};
	oauth_function.named_parameters["use_oauth"] = LogicalType::BOOLEAN;
	RegisterCommonSecretParameters(oauth_function);

	// Register the key_file secret provider
	CreateSecretFunction key_file_function = {type, "key_file", CreateGsheetSecretFromKeyFile, {}};
	key_file_function.named_parameters["filepath"] = LogicalType::VARCHAR;
	key_file_function.named_parameters["email"] = LogicalType::VARCHAR;
	key_file_function.named_parameters["secret"] = LogicalType::VARCHAR;
	RegisterCommonSecretParameters(key_file_function);

	loader.RegisterSecretType(secret_type);
	loader.RegisterFunction(access_token_function);
	loader.RegisterFunction(oauth_function);
	loader.RegisterFunction(key_file_function);
}

std::string InitiateOAuthFlow() {
	// Runs a short-lived local HTTP listener so the OAuth redirect can hand back
	// the access token automatically, without the user having to copy/paste it.
	const int PORT = 8765;
	const std::string client_id = "793766532675-rehqgocfn88h0nl88322ht6d1i12kl4e.apps.googleusercontent.com";
	const std::string redirect_uri = "http://localhost:" + std::to_string(PORT);
	const std::string auth_url = "https://accounts.google.com/o/oauth2/v2/auth";
	std::string access_token;

#ifdef _WIN32
	WSADATA wsa_data;
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
		throw IOException("Failed to initialize Winsock");
	}
#endif

	// Create socket
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) {
		throw IOException("Failed to create socket");
	}

	// Set socket options to allow reuse
	int opt = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		close(server_fd);
		throw IOException("Failed to set socket options");
	}

	// Bind to localhost:PORT
	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(PORT);

	if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		close(server_fd);
		throw IOException("Failed to bind to port " + std::to_string(PORT));
	}

	if (listen(server_fd, 1) < 0) {
		close(server_fd);
		throw IOException("Failed to listen on socket");
	}

	// Generate state for CSRF protection
	std::string state = generate_random_string(10);

	std::string auth_request_url = auth_url + "?client_id=" + client_id + "&redirect_uri=" + redirect_uri +
	                               "&response_type=token" + "&scope=https://www.googleapis.com/auth/spreadsheets" +
	                               "&state=" + state;

	// Open browser
#ifdef _WIN32
	system(("start \"\" \"" + auth_request_url + "\"").c_str());
#elif __APPLE__
	system(("open \"" + auth_request_url + "\"").c_str());
#elif __linux__
	system(("xdg-open \"" + auth_request_url + "\"").c_str());
#endif

	std::cout << '\n' << "Waiting for Login via Browser..." << '\n' << '\n';
	std::cout << auth_request_url << '\n';

	// Accept first connection (GET request)
	int client_socket;
	if ((client_socket = accept(server_fd, nullptr, nullptr)) < 0) {
		close(server_fd);
		throw IOException("Failed to accept connection");
	}

	// Read initial request
	char buffer[4096] = {0};
	ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer));
	(void)bytes_read;

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
	write(client_socket, response.c_str(), response.length());
	close(client_socket);

	// Accept second connection (POST request)
	if ((client_socket = accept(server_fd, nullptr, nullptr)) < 0) {
		close(server_fd);
		throw IOException("Failed to accept second connection");
	}

	// Read the POST request
	memset(buffer, 0, sizeof(buffer));
	bytes_read = read(client_socket, buffer, sizeof(buffer));
	std::string token_request(buffer);

	// Send response to POST request
	std::string post_response = "HTTP/1.1 200 OK\r\n"
	                             "Access-Control-Allow-Origin: *\r\n"
	                             "Content-Length: 0\r\n\r\n";
	write(client_socket, post_response.c_str(), post_response.length());

	// Extract token from POST body
	size_t body_start = token_request.find("\r\n\r\n");
	if (body_start != std::string::npos) {
		access_token = token_request.substr(body_start + 4);
	}

	// Clean up
	close(client_socket);
	close(server_fd);

#ifdef _WIN32
	WSACleanup();
#endif

	if (access_token.empty()) {
		throw IOException("Failed to obtain access token");
	}

	return access_token;
}
} // namespace duckdb
