#pragma once

#include "duckdb/main/client_context.hpp"

#include "sheets/auth/auth_provider.hpp"
#include "sheets/transport/http_client.hpp"

namespace duckdb {
namespace sheets {

// Builds the IAuthProvider for the `gsheet` secret matching ctx, or nullptr
// if there is none.
//
// `allow_interactive_reauth` controls whether an `oauth` secret with a
// refresh_token gets a reauth callback wired up (see OAuthAuth) that reopens
// the browser to recover from a revoked/expired refresh_token. A secret
// created with `interactive_reauth` false (one given a refresh_token
// directly) never gets one, whatever this is set to. Only pass
// true from a call site where the returned IAuthProvider's
// GetAuthorizationHeader is guaranteed to run on one thread at a time across
// its whole lifetime - e.g. built fresh per query bind. Pass false for a
// provider that will be shared across a query's parallel sink threads (e.g.
// COPY's GlobalFunctionData, read by every PER_THREAD_OUTPUT sink call): a
// reauth there could block a worker thread on an interactive login for
// minutes, and the callback also touches ctx (SecretManager/CatalogTransaction
// lookups), which - unlike this function's own one-time use of ctx at bind
// time - has no established safe access pattern from a sink thread.
std::unique_ptr<IAuthProvider> CreateAuthFromSecret(ClientContext &ctx, IHttpClient &http,
                                                    bool allow_interactive_reauth = true);

} // namespace sheets
} // namespace duckdb
