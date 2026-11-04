#include "sheets/auth_factory.hpp"

#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/main/secret/secret_manager.hpp"

#include "gsheets_auth.hpp"
#include "utils/secret.hpp"
#include "sheets/auth/bearer_token_auth.hpp"
#include "sheets/auth/oauth_auth.hpp"
#include "sheets/auth/service_account_auth.hpp"

namespace duckdb {
namespace sheets {

namespace {

// Builds the callback OAuthAuth invokes when its refresh_token comes back
// invalid_grant (revoked/expired): reruns the authorization-code + PKCE
// login for the same client_id/client_secret, then persists the resulting
// tokens back over the matched secret so later queries (which each look the
// secret up and build a fresh OAuthAuth - see CreateAuthFromSecret) pick up
// the replacement refresh_token instead of immediately hitting the same dead
// one again.
//
// ctx and http are captured by reference, not copied: that's only safe
// because, in this codebase, the OAuthAuth this callback is handed to never
// outlives the single bind/InitializeGlobal call that built it via
// CreateAuthFromSecret (see that function's allow_interactive_reauth doc for
// why - this callback also isn't wired up at all for call sites where that
// doesn't hold). A future caller that caches an OAuthAuth across statements
// or connections would need to give this callback its own owned
// ClientContext/IHttpClient instead.
OAuthReauthCallback BuildReauthCallback(ClientContext &ctx, IHttpClient &http, const SecretEntry &matched_entry,
                                        const std::string &client_id, const std::string &client_secret) {
	auto persist_type = matched_entry.persist_type;
	std::string storage_mode = matched_entry.storage_mode;
	// Copy - not reference - the matched secret's key/value contents: the
	// SecretEntry this came from is a snapshot owned by the SecretMatch in
	// CreateAuthFromSecret's stack frame and won't outlive this call, but the
	// callback itself may run much later (whenever the token actually
	// expires).
	KeyValueSecret secret_template(matched_entry.secret->Cast<KeyValueSecret>());

	return
	    [&ctx, &http, client_id, client_secret, persist_type, storage_mode, secret_template]() -> OAuthTokenResponse {
		    // RunOAuthCodeFlow itself serializes with any other concurrent login
		    // attempt across the process (see oauth_local_listener_mutex in
		    // gsheets_auth.cpp, which every flow that binds the local redirect
		    // listener goes through) - needed because two OAuthAuth instances
		    // built off the same now-dead refresh_token secret (e.g. two
		    // concurrent connections) could otherwise hit invalid_grant and try
		    // to reauthenticate at the same time.
		    OAuthTokenResponse flow = RunOAuthCodeFlow(ctx, http, client_id, client_secret);

		    auto &manager = SecretManager::Get(ctx);
		    auto transaction = CatalogTransaction::GetSystemCatalogTransaction(ctx);

		    // Only persist over the secret if it's still there under its original
		    // name: the interactive login above can take minutes, and the user
		    // may have dropped (or replaced) this secret in the meantime -
		    // REPLACE_ON_CONFLICT would otherwise silently resurrect it.
		    auto still_exists =
		        manager.GetSecretByName(transaction, secret_template.GetName().GetIdentifierName(), storage_mode);
		    if (still_exists) {
			    auto updated = make_uniq<KeyValueSecret>(secret_template);
			    updated->secret_map["token"] = flow.access_token;
			    updated->secret_map["refresh_token"] = flow.refresh_token;
			    manager.RegisterSecret(transaction, std::move(updated), OnCreateConflict::REPLACE_ON_CONFLICT,
			                           persist_type, storage_mode);
		    }

		    // The freshly-obtained tokens are still good for the current query
		    // either way - only the persistence for *future* queries is skipped.
		    return flow;
	    };
}

} // namespace

std::unique_ptr<IAuthProvider> CreateAuthFromSecret(ClientContext &ctx, IHttpClient &http,
                                                    bool allow_interactive_reauth) {
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
			// Only `oauth` secrets created with both client_id and
			// client_secret carry a refresh_token - either captured by the
			// authorization-code + PKCE login, or passed in directly via
			// refresh_token (in which case there's no token at all until
			// OAuthAuth mints one). Everything else (access_token secrets,
			// and oauth secrets created via the default/implicit-grant flow)
			// keeps using the static BearerTokenAuth, exactly as before.
			Value refreshValue;
			if (provider == "oauth" && gsheet_secret->TryGetValue("refresh_token", refreshValue)) {
				Value clientIdValue, clientSecretValue;
				if (!gsheet_secret->TryGetValue("client_id", clientIdValue)) {
					throw InvalidInputException("'client_id' not found in gsheet secret with a refresh_token");
				}
				if (!gsheet_secret->TryGetValue("client_secret", clientSecretValue)) {
					throw InvalidInputException("'client_secret' not found in gsheet secret with a refresh_token");
				}
				// Secrets created from a passed-in refresh_token opt out of
				// the browser reauth: they're almost always used headless,
				// where a login would just hang until it times out.
				Value interactiveValue;
				bool secret_allows_reauth = !gsheet_secret->TryGetValue("interactive_reauth", interactiveValue) ||
				                            interactiveValue.IsNull() ||
				                            interactiveValue.DefaultCastAs(LogicalType::BOOLEAN).GetValue<bool>();
				OAuthReauthCallback reauth = nullptr;
				if (allow_interactive_reauth && secret_allows_reauth) {
					reauth = BuildReauthCallback(ctx, http, *match.secret_entry, clientIdValue.ToString(),
					                             clientSecretValue.ToString());
				}
				return make_uniq<OAuthAuth>(http, refreshValue.ToString(), clientIdValue.ToString(),
				                            clientSecretValue.ToString(), std::move(reauth));
			}

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
