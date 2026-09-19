#include "sheets/auth/oauth_auth.hpp"

#include "json.hpp"
#include "duckdb/common/exception.hpp"

#include "gsheets_utils.hpp"
#include "sheets/transport/http_type.hpp"

using json = nlohmann::json;

namespace duckdb {
namespace sheets {

constexpr int DEFAULT_TOKEN_TTL = 3600;
constexpr const char *TOKEN_ENDPOINT = "https://oauth2.googleapis.com/token";

std::string OAuthAuth::GetAuthorizationHeader() {
	if (IsExpired()) {
		Refresh();
	}
	return "Bearer " + cachedToken;
}

bool OAuthAuth::IsExpired() {
	if (cachedToken.empty()) {
		return true;
	}
	std::time_t now = std::time(nullptr);
	return now >= expirationTime;
}

void OAuthAuth::Refresh() {
	std::string body = "grant_type=refresh_token" + ("&refresh_token=" + url_encode(refreshToken)) +
	                   ("&client_id=" + url_encode(clientId)) + ("&client_secret=" + url_encode(clientSecret));

	HttpHeaders headers;
	headers["Content-Type"] = "application/x-www-form-urlencoded";
	HttpResponse response = http.Post(TOKEN_ENDPOINT, headers, body);

	if (response.statusCode != 200) {
		throw duckdb::IOException("OAuth token refresh failed: " + response.body);
	}

	json responseJson;
	try {
		responseJson = json::parse(response.body);
	} catch (const json::exception &) {
		throw duckdb::IOException("Failed to parse OAuth token refresh response: " + response.body);
	}

	if (!responseJson.contains("access_token")) {
		throw duckdb::IOException("OAuth token refresh response missing 'access_token': " + response.body);
	}
	cachedToken = responseJson["access_token"].get<std::string>();

	int expiresIn = responseJson.value("expires_in", DEFAULT_TOKEN_TTL);
	expirationTime = std::time(nullptr) + expiresIn - 60; // refresh 1 min early
}

} // namespace sheets
} // namespace duckdb
