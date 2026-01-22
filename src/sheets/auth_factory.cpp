#include "sheets/auth_factory.hpp"

#include "utils/secret.hpp"
#include "sheets/auth/bearer_token_auth.hpp"
#include "sheets/auth/service_account_auth.hpp"

namespace duckdb {
namespace sheets {

std::unique_ptr<IAuthProvider> CreateAuthFromSecret(ClientContext &ctx, IHttpClient &http,
                                                    const std::string &secretName) {
	auto *secret = GetGSheetSecret(ctx, secretName);
	if (!secret) {
		return nullptr;
	}

	auto provider = secret->GetProvider();
	if (provider == "key_file") {
		Value emailValue, keyValue;
		if (!secret->TryGetValue("email", emailValue)) {
			throw InvalidInputException("'email' not found in gsheet secret");
		}
		if (!secret->TryGetValue("secret", keyValue)) {
			throw InvalidInputException("'secret' not found in gsheet secret");
		}
		return make_uniq<ServiceAccountAuth>(http, emailValue.ToString(), keyValue.ToString());
	} else {
		Value tokenValue;
		if (!secret->TryGetValue("token", tokenValue)) {
			throw InvalidInputException("'token' not found in gsheet secret");
		}
		return make_uniq<BearerTokenAuth>(tokenValue.ToString());
	}
}

} // namespace sheets
} // namespace duckdb
