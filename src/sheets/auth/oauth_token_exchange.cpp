#include "sheets/auth/oauth_token_exchange.hpp"

#include "json.hpp"
#include "duckdb/common/exception.hpp"

#include "sheets/transport/http_type.hpp"

using json = nlohmann::json;

namespace duckdb {
namespace sheets {

constexpr int DEFAULT_TOKEN_TTL = 3600;
constexpr const char *TOKEN_ENDPOINT = "https://oauth2.googleapis.com/token";

namespace {

// The standard OAuth2 error shape (RFC 6749 5.2: "error"/"error_description")
// pulled out of a token-endpoint error response.
struct SafeError {
	// For use in exception messages. Deliberately never the raw response
	// body: that can carry sensitive details (e.g. token fields) that
	// shouldn't end up in logs/errors. "" if the body isn't JSON or doesn't
	// have these fields.
	std::string detail;
	// Whether "error" is exactly "invalid_grant". false whenever detail
	// extraction itself failed, so a malformed/non-JSON body never gets
	// misclassified as invalid_grant.
	bool is_invalid_grant = false;
};

// Single json::parse of `body` backing both fields of SafeError - the two
// were previously extracted by separate, independently-parsing helpers.
SafeError ParseSafeError(const std::string &body) {
	SafeError result;
	try {
		json j = json::parse(body);
		if (j.contains("error") && j.at("error").is_string()) {
			std::string error = j.at("error").get<std::string>();
			result.is_invalid_grant = (error == "invalid_grant");
			result.detail = error;
		}
		if (j.contains("error_description") && j.at("error_description").is_string()) {
			if (!result.detail.empty()) {
				result.detail += ": ";
			}
			result.detail += j.at("error_description").get<std::string>();
		}
	} catch (const json::exception &) {
		// Leave result at its defaults: detail "", is_invalid_grant false.
	}
	return result;
}

} // namespace

OAuthTokenResponse PostToTokenEndpoint(IHttpClient &http, const std::string &body, const std::string &context_label) {
	HttpHeaders headers;
	headers["Content-Type"] = "application/x-www-form-urlencoded";
	HttpResponse response = http.Post(TOKEN_ENDPOINT, headers, body);

	if (response.statusCode != 200) {
		SafeError error = ParseSafeError(response.body);
		std::string message = context_label + " failed with status " + std::to_string(response.statusCode) +
		                      (error.detail.empty() ? "" : ": " + error.detail);
		if (error.is_invalid_grant) {
			throw OAuthInvalidGrantException(message);
		}
		throw IOException(message);
	}

	// access_token/expires_in/refresh_token are all extracted inside this one
	// try block - not just the json::parse call - so a field that's present
	// but the wrong type (e.g. access_token: null) surfaces as the same clean
	// IOException as a parse failure, instead of an uncaught json::type_error
	// escaping to the caller.
	try {
		json responseJson = json::parse(response.body);

		if (!responseJson.contains("access_token")) {
			throw IOException(context_label + " response missing 'access_token'");
		}

		OAuthTokenResponse result;
		result.access_token = responseJson.at("access_token").get<std::string>();
		if (responseJson.contains("refresh_token") && !responseJson.at("refresh_token").is_null()) {
			result.refresh_token = responseJson.at("refresh_token").get<std::string>();
		}
		result.expires_in = responseJson.value("expires_in", DEFAULT_TOKEN_TTL);
		return result;
	} catch (const json::exception &) {
		throw IOException("Failed to parse " + context_label + " response");
	}
}

} // namespace sheets
} // namespace duckdb
