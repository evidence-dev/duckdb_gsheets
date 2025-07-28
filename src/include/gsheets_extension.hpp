#pragma once

#include "duckdb.hpp"

namespace duckdb {

class GsheetsExtension : public Extension {
public:
#ifdef DUCKDB_CPP_EXTENSION_ENTRY
	void Load(ExtensionLoader &loader) override;
#else
	void Load(DuckDB &db) override;
#endif
	std::string Name() override;
	std::string Version() const override;
};

} // namespace duckdb
