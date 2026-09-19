#include "sheets/auth/oauth_token_exchange.hpp"

#include "json.hpp"
#include "duckdb/common/exception.hpp"

#include "sheets/transport/http_type.hpp"

using json = nlohmann::json;

namespace duckdb {
namespace sheets {

constexpr int DEFAULT_TOKEN_TTL = 3600;
constexpr const char *TOKEN_ENDPOINT = "https://oauth2.googleapis.com/token";

OAuthTokenResponse PostToTokenEndpoint(IHttpClient &http, const std::string &body, const std::string &context_label) {
	HttpHeaders headers;
	headers["Content-Type"] = "application/x-www-form-urlencoded";
	HttpResponse response = http.Post(TOKEN_ENDPOINT, headers, body);

	if (response.statusCode != 200) {
		throw IOException(context_label + " failed: " + response.body);
	}

	// access_token/expires_in/refresh_token are all extracted inside this one
	// try block - not just the json::parse call - so a field that's present
	// but the wrong type (e.g. access_token: null) surfaces as the same clean
	// IOException as a parse failure, instead of an uncaught json::type_error
	// escaping to the caller.
	try {
		json responseJson = json::parse(response.body);

		if (!responseJson.contains("access_token")) {
			throw IOException(context_label + " response missing 'access_token': " + response.body);
		}

		OAuthTokenResponse result;
		result.access_token = responseJson.at("access_token").get<std::string>();
		if (responseJson.contains("refresh_token") && !responseJson.at("refresh_token").is_null()) {
			result.refresh_token = responseJson.at("refresh_token").get<std::string>();
		}
		result.expires_in = responseJson.value("expires_in", DEFAULT_TOKEN_TTL);
		return result;
	} catch (const json::exception &) {
		throw IOException("Failed to parse " + context_label + " response: " + response.body);
	}
}

} // namespace sheets
} // namespace duckdb
