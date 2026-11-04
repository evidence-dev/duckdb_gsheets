#pragma once

#include <string>

#include "duckdb/main/extension/extension_loader.hpp"

#include "sheets/auth/oauth_token_exchange.hpp"
#include "sheets/transport/http_client.hpp"

namespace duckdb {

class ClientContext;

std::string InitiateOAuthFlow(ClientContext &context, const std::string &client_id);

// Runs the authorization-code + PKCE flow (browser login, then a server-side
// token exchange) for a caller-supplied OAuth client_id/client_secret,
// returning a fresh access_token + refresh_token pair (refresh_token is
// always set - see ExchangeAuthorizationCodeForTokens). Used both to create
// an `oauth` secret initially (see CreateGsheetSecretFromOAuth) and to
// re-authenticate when a stored refresh_token has been revoked or expired
// (see auth_factory.cpp's reauth callback).
sheets::OAuthTokenResponse RunOAuthCodeFlow(ClientContext &context, sheets::IHttpClient &http,
                                            const std::string &client_id, const std::string &client_secret);

struct CreateGsheetSecretFunctions {
public:
	static void Register(ExtensionLoader &loader);
};

} // namespace duckdb
