#pragma once

#include <string>
#include <ctime>

#include "sheets/auth/auth_provider.hpp"
#include "sheets/transport/http_client.hpp"

namespace duckdb {
namespace sheets {

class ServiceAccountAuth : public IAuthProvider {
public:
	ServiceAccountAuth(IHttpClient &http, const std::string &email, const std::string &privateKey)
	    : http(http), email(email), privateKey(privateKey) {
	}

	std::string GetAuthorizationHeader() override;

private:
	IHttpClient &http;
	std::string email;
	std::string privateKey;
	std::string cachedToken;
	std::time_t expirationTime = 0;

	std::string CreateJwt();
	void ExchangeJwtForToken();
	bool IsExpired();
	void Refresh();
};
} // namespace sheets
} // namespace duckdb
