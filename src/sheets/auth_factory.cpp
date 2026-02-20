#include "sheets/auth_factory.hpp"

#include "utils/secret.hpp"
#include "sheets/auth/bearer_token_auth.hpp"
#include "sheets/auth/service_account_auth.hpp"

namespace duckdb {
namespace sheets {

std::unique_ptr<IAuthProvider> CreateAuthFromSecret(ClientContext &ctx, IHttpClient &http) {
	auto match = GetSecretMatch(ctx, "gsheet", "gsheet");
	if (match.HasMatch()) {
		auto &secret = match.GetSecret();
		auto gsheet_secret = dynamic_cast<const KeyValueSecret *>(&secret);
		auto provider = gsheet_secret->GetProvider();
		if (provider == "key_file") {
			Value emailValue, keyValue;
			if (!gsheet_secret->TryGetValue("email", emailValue)) {
				throw InvalidInputException("'email' not found in gsheet secret");
			}
			if (!gsheet_secret->TryGetValue("secret", keyValue)) {
				throw InvalidInputException("'secret' not found in gsheet secret");
			}
			return make_uniq<ServiceAccountAuth>(http, emailValue.ToString(), keyValue.ToString());
		} else {
			Value tokenValue;
			if (!gsheet_secret->TryGetValue("token", tokenValue)) {
				throw InvalidInputException("'token' not found in gsheet secret");
			}
			return make_uniq<BearerTokenAuth>(tokenValue.ToString());
		}
	}
	return nullptr;
}

} // namespace sheets
} // namespace duckdb
