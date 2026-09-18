#include <fstream>
#include <iostream>
#include <cstdlib>
#include <json.hpp>

#include "duckdb/common/exception/binder_exception.hpp"

#include "gsheets_auth.hpp"
#include "gsheets_utils.hpp"
#include "sheets/auth/oauth_listener.hpp"
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
	string token = InitiateOAuthFlow(context);

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

std::string InitiateOAuthFlow(ClientContext &context) {
	// Runs a short-lived local HTTP listener so the OAuth redirect can hand back
	// the access token automatically, without the user having to copy/paste it.
	const int PORT = 8765;
	const std::string client_id = "793766532675-rehqgocfn88h0nl88322ht6d1i12kl4e.apps.googleusercontent.com";
	const std::string redirect_uri = "http://localhost:" + std::to_string(PORT);
	const std::string auth_url = "https://accounts.google.com/o/oauth2/v2/auth";
	const std::string scope = "https://www.googleapis.com/auth/spreadsheets";

	// Generate state for CSRF protection
	std::string state = generate_random_string(10);
	std::string auth_request_url = sheets::BuildAuthorizationUrl(auth_url, client_id, redirect_uri, scope, state);

	// Open the browser only once the listener is actually ready to receive the
	// redirect (RunLocalOAuthListener invokes this after it starts listening).
	auto open_browser = [&auth_request_url]() {
		bool should_open_browser = true;

#ifdef __linux__
		// On Linux, check for a headless environment to avoid xdg-open erroring out.
		const char *display = std::getenv("DISPLAY");
		const char *wayland_display = std::getenv("WAYLAND_DISPLAY");
		if (!display && !wayland_display) {
			should_open_browser = false;
		}
#endif

		if (should_open_browser) {
#ifdef _WIN32
			system(("start \"\" \"" + auth_request_url + "\"").c_str());
#elif __APPLE__
			system(("open \"" + auth_request_url + "\"").c_str());
#elif __linux__
			system(("xdg-open \"" + auth_request_url + "\"").c_str());
#endif
		}
		std::cout << '\n' << "Waiting for Login via Browser..." << '\n' << '\n';
		std::cout << auth_request_url << '\n';
		std::cout << "(This will time out after " << (sheets::kOAuthListenerTimeoutSeconds / 60)
		           << " minutes if login isn't completed.)" << '\n';
		std::cout << '\n'
		           << "Alternatively, after logging in, paste the redirect URL (or just its access_token) "
		           << "here and press Enter:" << '\n';
	};

	// Lets Ctrl+C cancel a pending login instead of blocking the CLI until
	// the 5-minute timeout: without this, RunLocalOAuthListener's blocking
	// accept loop never yields back to DuckDB's own interrupt/EOF handling.
	auto is_interrupted = [&context]() { return context.IsInterrupted(); };

	// Lets a token be pasted in as an alternative to the local listener
	// actually receiving the browser's redirect - the only way to complete
	// this flow when DuckDB runs on a remote/headless host, since the
	// redirect URI is always a loopback address the user's own browser can't
	// reach back into over the network. TryReadPastedLine is non-blocking,
	// so this is polled from the same loop that services the listener
	// socket(s) instead of needing a separate thread that could otherwise
	// linger reading stdin - and race the DuckDB CLI's own prompt for it -
	// after this call returns.
	auto try_read_pasted_input = [](std::string &line) { return sheets::TryReadPastedLine(line); };

	return sheets::RunLocalOAuthListener(PORT, state, open_browser, /*max_attempts=*/20, is_interrupted,
	                                      try_read_pasted_input);
}

} // namespace duckdb
