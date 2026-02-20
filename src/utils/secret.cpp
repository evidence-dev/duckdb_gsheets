#include "utils/secret.hpp"

#include "duckdb/main/secret/secret.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"

namespace duckdb {
namespace sheets {

const SecretMatch GetSecretMatch(ClientContext &ctx, const std::string &path, const std::string &type) {
	auto &manager = SecretManager::Get(ctx);
	auto transaction = CatalogTransaction::GetSystemCatalogTransaction(ctx);
	return manager.LookupSecret(transaction, path, type);
}

const KeyValueSecret *GetGSheetSecret(ClientContext &ctx) {
	auto match = GetSecretMatch(ctx, "gsheet", "gsheet");
	if (!match.HasMatch()) {
		return nullptr;
	}
	auto &secret = match.GetSecret();
	return static_cast<const KeyValueSecret *>(&secret);
}

} // namespace sheets
} // namespace duckdb
