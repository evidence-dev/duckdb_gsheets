#include "utils/secret.hpp"

#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"

namespace duckdb {
namespace sheets {

const KeyValueSecret *GetGSheetSecret(ClientContext &ctx, const std::string &secretName) {
	auto &manager = SecretManager::Get(ctx);
	auto transaction = CatalogTransaction::GetSystemCatalogTransaction(ctx);
	auto match = manager.LookupSecret(transaction, "gsheet", "gsheet");
	if (!match.HasMatch()) {
		return nullptr;
	}
	auto &secret = match.GetSecret();
	return dynamic_cast<const KeyValueSecret *>(&secret);
}

} // namespace sheets
} // namespace duckdb
