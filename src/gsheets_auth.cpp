#include "duckdb.hpp"

#ifndef DUCKDB_CPP_EXTENSION_ENTRY
#include "duckdb/main/extension_util.hpp"
#endif

#include "gsheets_auth.hpp"
#include "gsheets_utils.hpp"
#include "gsheets_get_token.hpp"

#include <fstream>
#include <cstdlib>
#include <json.hpp>

using json = nlohmann::json;

namespace duckdb {

std::string read_token_from_file(const std::string &file_path) {
	std::ifstream file(file_path);
	if (!file.is_open()) {
		throw duckdb::IOException("Unable to open token file: " + file_path);
	}
	std::string token;
	std::getline(file, token);
	return token;
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

// TODO: Maybe this should be a KeyValueSecret
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

// TODO: Maybe this should be a KeyValueSecret
static unique_ptr<BaseSecret> CreateGsheetSecretFromKeyFile(ClientContext &context, CreateSecretInput &input) {
	auto scope = input.scope;

	auto result = make_uniq<KeyValueSecret>(scope, input.type, input.provider, input.name);

	// Want to store the private key and email in case the secret is persisted
	std::string filepath_key = "filepath";
	auto filepath = (input.options.find(filepath_key)->second).ToString();

	std::ifstream ifs(filepath);
	if (!ifs.is_open()) {
		throw IOException("Could not open JSON key file at: " + filepath);
	}
	json credentials_file = json::parse(ifs);
	std::string email = credentials_file["client_email"].get<std::string>();
	std::string secret = credentials_file["private_key"].get<std::string>();

	// Manage specific secret option
	(*result).secret_map["email"] = Value(email);
	(*result).secret_map["secret"] = Value(secret);
	CopySecret("filepath", input, *result); // Store the filepath anyway

	const auto result_const = *result;
	TokenDetails token_details = get_token(context, &result_const);
	std::string token = token_details.token;

	(*result).secret_map["token"] = Value(token);
	(*result).secret_map["token_expiration"] = Value(token_details.expiration_time);

	// Redact sensible keys
	RedactCommonKeys(*result);
	result->redact_keys.insert("secret");
	result->redact_keys.insert("filepath");
	result->redact_keys.insert("token");

	return std::move(result);
}

#ifdef DUCKDB_CPP_EXTENSION_ENTRY
void CreateGsheetSecretFunctions::Register(ExtensionLoader &loader)
#else
void CreateGsheetSecretFunctions::Register(DatabaseInstance &db)
#endif
{
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
	RegisterCommonSecretParameters(key_file_function);

#ifdef DUCKDB_CPP_EXTENSION_ENTRY
	loader.RegisterSecretType(secret_type);
	loader.RegisterFunction(access_token_function);
	loader.RegisterFunction(oauth_function);
	loader.RegisterFunction(key_file_function);
#else
	ExtensionUtil::RegisterSecretType(db, secret_type);
	ExtensionUtil::RegisterFunction(db, access_token_function);
	ExtensionUtil::RegisterFunction(db, oauth_function);
	ExtensionUtil::RegisterFunction(db, key_file_function);
#endif
}

std::string InitiateOAuthFlow() {
	// This is using the Web App OAuth flow, as I can't figure out desktop app flow.
	const std::string client_id = "793766532675-rehqgocfn88h0nl88322ht6d1i12kl4e.apps.googleusercontent.com";
	const std::string redirect_uri = "https://duckdb-gsheets.com/oauth";
	const std::string auth_url = "https://accounts.google.com/o/oauth2/v2/auth";

	// Generate a random state for CSRF protection
	std::string state = generate_random_string(10);

	std::string auth_request_url = auth_url + "?client_id=" + client_id + "&redirect_uri=" + redirect_uri +
	                               "&response_type=token" + "&scope=https://www.googleapis.com/auth/spreadsheets" +
	                               "&state=" + state;

	// Instruct the user to visit the URL and grant permission
	std::cout << "Visit the below URL to authorize DuckDB GSheets" << '\n';
	std::cout << auth_request_url << '\n';

// Open the URL in the user's default browser
#ifdef _WIN32
	system(("start \"\" \"" + auth_request_url + "\"").c_str());
#elif __APPLE__
	system(("open \"" + auth_request_url + "\"").c_str());
#elif __linux__
	system(("xdg-open \"" + auth_request_url + "\"").c_str());
#endif

	std::cout << "After granting permission, enter the token: ";
	std::string access_token;
	std::cin >> access_token;

	return access_token;
}

} // namespace duckdb
