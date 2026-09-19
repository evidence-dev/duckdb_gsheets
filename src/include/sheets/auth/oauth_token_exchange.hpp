#pragma once

#include <string>

#include "sheets/transport/http_client.hpp"

namespace duckdb {
namespace sheets {

// The token_endpoint response fields both OAuth flows need: OAuthAuth's
// refresh_token -> access_token exchange, and the authorization-code flow's
// one-time code -> access_token/refresh_token exchange. `refresh_token` is
// empty when the response didn't include one (expected for a refresh_token
// grant; callers of an authorization_code grant should treat that as an
// error - see PostToTokenEndpoint's comment).
struct OAuthTokenResponse {
	std::string access_token;
	std::string refresh_token;
	int expires_in;
};

// POSTs `body` (an already-encoded application/x-www-form-urlencoded payload)
// to Google's OAuth2 token endpoint and parses the result, throwing
// IOException - prefixed with `context_label` (e.g. "OAuth token refresh" or
// "OAuth token exchange") so both callers get on-brand error messages - on
// any failure: a non-200 response, a body that isn't valid JSON, a field
// that's present but the wrong type (e.g. access_token: null), or a response
// missing access_token entirely. A response with no refresh_token is not an
// error here - refresh_token grants don't return one, and it's each caller's
// job to decide whether its own flow requires one.
OAuthTokenResponse PostToTokenEndpoint(IHttpClient &http, const std::string &body, const std::string &context_label);

} // namespace sheets
} // namespace duckdb
