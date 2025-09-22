#define DUCKDB_EXTENSION_MAIN

#include "duckdb.hpp"

#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"

// Standard library
#include <string>

// GSheets extension
#include "gsheets_extension.hpp"
#include "gsheets_auth.hpp"
#include "gsheets_copy.hpp"
#include "gsheets_read.hpp"

// OpenSSL linked through vcpkg
#include <openssl/opensslv.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>

namespace duckdb {

#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

unique_ptr<TableRef> ReadSheetReplacement(ClientContext &context, ReplacementScanInput &input,
                                          optional_ptr<ReplacementScanData> data) {
	auto table_name = ReplacementScan::GetFullPath(input);
	if (!StringUtil::StartsWith(table_name, "https://docs.google.com/spreadsheets/d/")) {
		return nullptr;
	}
	auto table_function = make_uniq<TableFunctionRef>();
	vector<unique_ptr<ParsedExpression>> children;
	children.push_back(make_uniq<ConstantExpression>(Value(table_name)));
	table_function->function = make_uniq<FunctionExpression>("read_gsheet", std::move(children));

	if (!FileSystem::HasGlob(table_name)) {
		auto &fs = FileSystem::GetFileSystem(context);
		table_function->alias = fs.ExtractBaseName(table_name);
	}

	return std::move(table_function);
}

static void LoadInternal(ExtensionLoader &loader) {
#ifndef EMSCRIPTEN
	// Initialize OpenSSL
	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
#endif

	// Register read_gsheet table function
	TableFunction read_gsheet_function("read_gsheet", {LogicalType::VARCHAR}, ReadSheetFunction, ReadSheetBind);
	read_gsheet_function.named_parameters["header"] = LogicalType::BOOLEAN;
	read_gsheet_function.named_parameters["sheet"] = LogicalType::VARCHAR;
	read_gsheet_function.named_parameters["range"] = LogicalType::VARCHAR;
	read_gsheet_function.named_parameters["all_varchar"] = LogicalType::BOOLEAN;

	GSheetCopyFunction gsheet_copy_function;

	loader.RegisterFunction(read_gsheet_function);
	loader.RegisterFunction(gsheet_copy_function);
	CreateGsheetSecretFunctions::Register(loader);
	auto &config = DBConfig::GetConfig(loader.GetDatabaseInstance());

	config.AddExtensionOption("gsheets_endpoint", "Alternative endpoint for the sheets.googleapis.com API",
	                          LogicalType::VARCHAR, Value("sheets.googleapis.com"));
	config.replacement_scans.emplace_back(ReadSheetReplacement);
}

void GsheetsExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string GsheetsExtension::Name() {
	return "gsheets";
}

std::string GsheetsExtension::Version() const {
#ifdef EXT_VERSION_GSHEETS
	return EXT_VERSION_GSHEETS;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(gsheets, loader) {
	duckdb::LoadInternal(loader);
}
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
