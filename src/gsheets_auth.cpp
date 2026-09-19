#include <fstream>
#include <functional>
#include <iostream>
#include <cstdlib>
#include <json.hpp>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "duckdb/common/exception/binder_exception.hpp"

#include "gsheets_auth.hpp"
#include "gsheets_utils.hpp"
#include "sheets/auth/oauth_listener.hpp"
#include "sheets/auth/oauth_token_exchange.hpp"
#include "sheets/transport/client_factory.hpp"
#include "sheets/transport/http_type.hpp"
#include "utils/options.hpp"

using json = nlohmann::json;

namespace duckdb {

// Whether stdin is a real interactive terminal rather than a pipe/redirect
// (e.g. `duckdb < script.sql`, or the extension embedded in some other
// process's stdin). The paste-a-token fallback below must only be offered
// when this is true: TryReadPastedLine treats any line it reads as a paste
// attempt, so on a non-interactive stdin it would instead consume and
// discard whatever the caller's own script/pipe was about to feed in next.
static bool IsStdinInteractive() {
#ifdef _WIN32
	return _isatty(_fileno(stdin)) != 0;
#else
	return isatty(fileno(stdin)) != 0;
#endif
}

// The extension's built-in OAuth client, used unless the caller supplies
// their own via the `client_id` secret parameter (see
// CreateGsheetSecretFromOAuth). No client_secret is embedded for it: it
// only ever drives the implicit-grant flow below, which needs none. Callers
// who want a refresh_token must bring their own client_id *and*
// client_secret.
static const std::string kDefaultOAuthClientId =
    "793766532675-rehqgocfn88h0nl88322ht6d1i12kl4e.apps.googleusercontent.com";

// The callbacks RunLocalOAuthListener/RunLocalOAuthCodeListener need to open
// the user's browser, let Ctrl+C cancel a pending login, and (on an
// interactive terminal) accept a pasted fallback - identical machinery for
// both the implicit-grant and authorization-code flows, which only differ
// in the URL opened and, for the paste hint, what's being pasted.
struct LoginCallbacks {
	std::function<void()> open_browser;
	std::function<bool()> is_interrupted;
	std::function<bool(std::string &)> try_read_pasted_input;
};

static LoginCallbacks PrepareLoginCallbacks(ClientContext &context, const std::string &auth_request_url,
                                            const std::string &paste_hint_suffix) {
	// Only offer (and later poll for) the paste fallback when stdin is a
	// real interactive terminal - see IsStdinInteractive.
	bool stdin_interactive = IsStdinInteractive();

	// Open the browser only once the listener is actually ready to receive
	// the redirect (RunLocalOAuthListener/RunLocalOAuthCodeListener invoke
	// this after they start listening).
	auto open_browser = [auth_request_url, stdin_interactive, paste_hint_suffix]() {
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
		if (stdin_interactive) {
			std::cout << '\n'
			          << "Alternatively, after logging in, paste the redirect URL " << paste_hint_suffix
			          << " here and press Enter:" << '\n';
		}
	};

	LoginCallbacks callbacks;
	callbacks.open_browser = open_browser;

	// Lets Ctrl+C cancel a pending login instead of blocking the CLI until
	// the 5-minute timeout: without this, the listener's blocking accept
	// loop never yields back to DuckDB's own interrupt/EOF handling.
	callbacks.is_interrupted = [&context]() {
		return context.IsInterrupted();
	};

	// Lets a value be pasted in as an alternative to the local listener
	// actually receiving the browser's redirect - the only way to complete
	// this flow when DuckDB runs on a remote/headless host. Left unset
	// (nullptr) unless stdin is an interactive terminal - see
	// TryReadPastedLine.
	if (stdin_interactive) {
		callbacks.try_read_pasted_input = [](std::string &line) {
			return sheets::TryReadPastedLine(line);
		};
	}
	return callbacks;
}

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

// Exchanges an authorization code for an access_token + refresh_token at
// Google's token endpoint. Throws IOException on any failure, including a
// response that (unexpectedly, given access_type=offline&prompt=consent)
// lacks a refresh_token - a secret that silently can't refresh would be a
// confusing dead end.
static std::string ExchangeAuthorizationCodeForTokens(sheets::IHttpClient &http, const std::string &code,
                                                      const std::string &client_id, const std::string &client_secret,
                                                      const std::string &redirect_uri, const std::string &code_verifier,
                                                      std::string &out_refresh_token) {
	std::string body = "grant_type=authorization_code" + ("&code=" + url_encode(code)) +
	                   ("&client_id=" + url_encode(client_id)) + ("&client_secret=" + url_encode(client_secret)) +
	                   ("&redirect_uri=" + url_encode(redirect_uri)) + ("&code_verifier=" + url_encode(code_verifier));

	sheets::OAuthTokenResponse tokenResponse = sheets::PostToTokenEndpoint(http, body, "OAuth token exchange");

	if (tokenResponse.refresh_token.empty()) {
		throw IOException("Google did not return a refresh_token for this client_id/client_secret. This usually "
		                  "means this OAuth app was already authorized without one - revoke its access at "
		                  "https://myaccount.google.com/permissions and try again.");
	}

	out_refresh_token = tokenResponse.refresh_token;
	return tokenResponse.access_token;
}

struct OAuthCodeFlowResult {
	std::string access_token;
	std::string refresh_token;
};

// Runs the authorization-code + PKCE flow (browser login, then a
// server-side token exchange) for a caller-supplied OAuth client_id and
// client_secret, returning both an access_token and a refresh_token. See
// InitiateOAuthFlow for the default implicit-grant flow this sits
// alongside.
static OAuthCodeFlowResult InitiateOAuthCodeFlow(ClientContext &context, sheets::IHttpClient &http,
                                                 const std::string &client_id, const std::string &client_secret) {
	const int PORT = 8765;
	const std::string redirect_uri = "http://localhost:" + std::to_string(PORT);
	const std::string auth_url = "https://accounts.google.com/o/oauth2/v2/auth";
	const std::string scope = "https://www.googleapis.com/auth/spreadsheets";

	std::string state = generate_random_string(10);
	std::string code_verifier = sheets::GeneratePkceCodeVerifier();
	std::string code_challenge = sheets::GeneratePkceCodeChallenge(code_verifier);
	std::string auth_request_url =
	    sheets::BuildAuthorizationCodeUrl(auth_url, client_id, redirect_uri, scope, state, code_challenge);

	LoginCallbacks callbacks = PrepareLoginCallbacks(context, auth_request_url, "(or just its authorization code)");

	std::string code = sheets::RunLocalOAuthCodeListener(PORT, state, callbacks.open_browser, /*max_attempts=*/20,
	                                                     callbacks.is_interrupted, callbacks.try_read_pasted_input);

	OAuthCodeFlowResult result;
	result.access_token = ExchangeAuthorizationCodeForTokens(http, code, client_id, client_secret, redirect_uri,
	                                                         code_verifier, result.refresh_token);
	return result;
}

static unique_ptr<BaseSecret> CreateGsheetSecretFromOAuth(ClientContext &context, CreateSecretInput &input) {
	auto scope = input.scope;

	auto result = make_uniq<KeyValueSecret>(scope, input.type, input.provider, input.name);

	std::string client_id = duckdb::sheets::GetStringOption(input.options, "client_id");
	std::string client_secret = duckdb::sheets::GetStringOption(input.options, "client_secret");

	if (!client_secret.empty() && client_id.empty()) {
		throw BinderException("client_secret requires client_id to also be provided");
	}

	std::string effective_client_id = client_id.empty() ? kDefaultOAuthClientId : client_id;

	if (client_secret.empty()) {
		// Default flow, optionally with a caller-supplied client_id (i.e.
		// "bring your own OAuth app"): implicit grant, no refresh_token -
		// unchanged from before these parameters existed.
		string token = InitiateOAuthFlow(context, effective_client_id);
		result->secret_map["token"] = token;
	} else {
		// Both client_id and client_secret supplied: authorization-code +
		// PKCE flow, capturing a refresh_token so future queries can
		// silently reauthenticate via OAuthAuth with no browser interaction.
		auto http = sheets::CreateHttpClient(context);
		OAuthCodeFlowResult flow_result = InitiateOAuthCodeFlow(context, *http, effective_client_id, client_secret);

		result->secret_map["token"] = flow_result.access_token;
		result->secret_map["refresh_token"] = flow_result.refresh_token;
		result->secret_map["client_id"] = effective_client_id;
		result->secret_map["client_secret"] = client_secret;
	}

	// Redact sensible keys
	RedactCommonKeys(*result);
	result->redact_keys.insert("token");
	result->redact_keys.insert("refresh_token");
	result->redact_keys.insert("client_secret");

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
	// Optional: bring your own OAuth app. client_id alone still uses the
	// implicit-grant flow (no refresh_token) with that app instead of the
	// built-in one; client_id + client_secret together switch to the
	// authorization-code + PKCE flow and capture a refresh_token - see
	// CreateGsheetSecretFromOAuth.
	oauth_function.named_parameters["client_id"] = LogicalType::VARCHAR;
	oauth_function.named_parameters["client_secret"] = LogicalType::VARCHAR;
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

std::string InitiateOAuthFlow(ClientContext &context, const std::string &client_id) {
	// Runs a short-lived local HTTP listener so the OAuth redirect can hand back
	// the access token automatically, without the user having to copy/paste it.
	const int PORT = 8765;
	const std::string redirect_uri = "http://localhost:" + std::to_string(PORT);
	const std::string auth_url = "https://accounts.google.com/o/oauth2/v2/auth";
	const std::string scope = "https://www.googleapis.com/auth/spreadsheets";

	// Generate state for CSRF protection
	std::string state = generate_random_string(10);
	std::string auth_request_url = sheets::BuildAuthorizationUrl(auth_url, client_id, redirect_uri, scope, state);

	LoginCallbacks callbacks = PrepareLoginCallbacks(context, auth_request_url, "(or just its access_token)");

	return sheets::RunLocalOAuthListener(PORT, state, callbacks.open_browser, /*max_attempts=*/20,
	                                     callbacks.is_interrupted, callbacks.try_read_pasted_input);
}

} // namespace duckdb
