#include <utility>

#include "duckdb/common/case_insensitive_map.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/exception/binder_exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/types/value.hpp"

#include "gsheets_copy.hpp"
#include "gsheets_utils.hpp"

#include "sheets/auth_factory.hpp"
#include "sheets/client.hpp"
#include "sheets/exception.hpp"
#include "sheets/range.hpp"
#include "sheets/transport/client_factory.hpp"
#include "sheets/types.hpp"

namespace duckdb {

GSheetCopyFunction::GSheetCopyFunction() : CopyFunction("gsheet") {
	copy_to_bind = GSheetWriteBind;
	copy_to_initialize_global = GSheetWriteInitializeGlobal;
	copy_to_initialize_local = GSheetWriteInitializeLocal;
	copy_to_sink = GSheetWriteSink;
}

static std::string GetStringOption(const case_insensitive_map_t<vector<Value>> &options, const std::string &name,
                                   const std::string &default_value = "") {
	const auto it = options.find(name);
	if (it == options.end()) {
		return default_value;
	}
	std::string err;
	Value val;
	if (!it->second.back().DefaultTryCastAs(LogicalType::VARCHAR, val, &err)) {
		throw BinderException(name + " option must be VARCHAR");
	}
	if (val.IsNull()) {
		throw BinderException(name + " option must not be NULL");
	}
	return StringValue::Get(val);
}

// NOTE: the second value in pair is a flag indicating if the value was set by the user
static std::pair<bool, bool> GetBoolOption(const case_insensitive_map_t<vector<Value>> &options,
                                           const std::string &name, bool default_value = false) {
	const auto it = options.find(name);
	if (it == options.end()) {
		return std::make_pair(default_value, false);
	}
	if (it->second.size() != 1) {
		throw BinderException(name + " option must be a single boolean value");
	}
	std::string err;
	Value val;
	if (!it->second.back().DefaultTryCastAs(LogicalType::BOOLEAN, val, &err)) {
		throw BinderException(name + " option must be a single boolean value");
	}
	if (val.IsNull()) {
		throw BinderException(name + " option must be a single boolean value");
	}
	return std::make_pair(BooleanValue::Get(val), true);
}

unique_ptr<FunctionData> GSheetCopyFunction::GSheetWriteBind(ClientContext &context, CopyFunctionBindInput &input,
                                                             const vector<string> &names,
                                                             const vector<LogicalType> &sql_types) {

	string file_path = input.info.file_path;
	auto options = input.info.options;

	auto sheet = GetStringOption(options, "sheet");
	auto range = GetStringOption(options, "range");
	bool overwrite_sheet = GetBoolOption(options, "overwrite_sheet", true).first;
	bool overwrite_range = GetBoolOption(options, "overwrite_range", false).first;
	bool create_if_not_exists = GetBoolOption(options, "create_if_not_exists", false).first;

	auto header_result = GetBoolOption(options, "header", true);
	bool header = header_result.second ? header_result.first : (overwrite_range || overwrite_sheet);

	if (create_if_not_exists && sheet.empty()) {
		throw BinderException("Must provide sheet name");
	}

	return make_uniq<GSheetWriteBindData>(file_path, sql_types, names, sheet, range, overwrite_sheet, overwrite_range,
	                                      create_if_not_exists, header);
}

unique_ptr<GlobalFunctionData> GSheetCopyFunction::GSheetWriteInitializeGlobal(ClientContext &context,
                                                                               FunctionData &bind_data,
                                                                               const string &file_path) {
	auto options = bind_data.Cast<GSheetWriteBindData>().options;

	std::string spreadsheet_id = extract_spreadsheet_id(file_path);
	std::string sheet_id = extract_sheet_id(file_path);
	std::string sheet_name;
	std::string sheet_range;

	// Initialize client
	auto http = sheets::CreateHttpClient(context);
	auto auth = sheets::CreateAuthFromSecret(context, *http);
	if (!auth) {
		throw InvalidInputException("No 'gsheet' secret found...");
	}
	auto client = sheets::GoogleSheetsClient(*http, *auth);

	// Prefer a sheet or range that is specified as a parameter over one on the query string
	if (!options.sheet.empty()) {
		sheet_name = options.sheet;
	} else {
		auto sheet = client.Spreadsheets(spreadsheet_id).GetSheetById(sheet_id);
		sheet_name = sheet.properties.title;
	}

	// Create sheet if not exist (if enabled)
	if (options.create_if_not_exists) {
		try {
			auto sheet = client.Spreadsheets(spreadsheet_id).GetSheetByName(sheet_name);
			// sheet already exists so we need to ignore this instruction from the user
		} catch (sheets::SheetNotFoundException &_) {
			client.Spreadsheets(spreadsheet_id).CreateSheet(sheet_name);
		}
	}

	if (!options.range.empty()) {
		sheet_range = options.range;
	} else {
		sheet_range = extract_sheet_range(file_path);
	}

	std::string encoded_sheet_name = url_encode(sheet_name);

	// Do this here in the initialization so that it only happens once
	// OVERWRITE_RANGE takes precedence since it defaults to false and is less destructive
	if (options.overwrite_range) {
		client.Spreadsheets(spreadsheet_id).Values().Clear(sheets::A1Range(encoded_sheet_name + "!" + sheet_range));
	} else if (options.overwrite_sheet) {
		client.Spreadsheets(spreadsheet_id).Values().Clear(sheets::A1Range(encoded_sheet_name));
	}

	// Make the API call to write headers to the Google Sheet
	// If we are appending, header defaults to false
	if (options.header) {
		sheets::ValueRange header_values;
		header_values.range = sheet_range.empty() ? sheet_name : sheet_name + "!" + sheet_range;
		header_values.majorDimension = sheets::ROWS;
		header_values.values.push_back(options.name_list);

		auto range_str = encoded_sheet_name;
		if (!sheet_range.empty()) {
			range_str += "!" + sheet_range;
		}
		client.Spreadsheets(spreadsheet_id).Values().Append(sheets::A1Range(range_str), header_values);
	}
	return make_uniq<GSheetCopyGlobalState>(context, std::move(http), std::move(auth), spreadsheet_id,
	                                        encoded_sheet_name);
}

unique_ptr<LocalFunctionData> GSheetCopyFunction::GSheetWriteInitializeLocal(ExecutionContext &context,
                                                                             FunctionData &bind_data_p) {
	return make_uniq<LocalFunctionData>();
}

void GSheetCopyFunction::GSheetWriteSink(ExecutionContext &context, FunctionData &bind_data_p,
                                         GlobalFunctionData &gstate_p, LocalFunctionData &lstate, DataChunk &input) {
	input.Flatten();
	auto &gstate = gstate_p.Cast<GSheetCopyGlobalState>();

	std::string file = bind_data_p.Cast<GSheetWriteBindData>().files[0];
	auto options = bind_data_p.Cast<GSheetWriteBindData>().options;
	std::string sheet_id = extract_sheet_id(file);
	std::string sheet_name;
	std::string sheet_range;

	auto client = sheets::GoogleSheetsClient(*gstate.http, *gstate.auth);

	// Prefer a sheet or range that is specified as a parameter over one on the query string
	if (!options.sheet.empty()) {
		sheet_name = options.sheet;
	} else {
		auto sheet = client.Spreadsheets(gstate.spreadsheet_id).GetSheetById(sheet_id);
		sheet_name = sheet.properties.title;
	}

	if (!options.range.empty()) {
		sheet_range = options.range;
	} else {
		sheet_range = extract_sheet_range(file);
	}

	std::string encoded_sheet_name = url_encode(sheet_name);

	// Build ValueRange from input chunk
	sheets::ValueRange data;
	data.range = sheet_range.empty() ? sheet_name : sheet_name + "!" + sheet_range;
	data.majorDimension = sheets::ROWS;

	for (idx_t row_ix = 0; row_ix < input.size(); row_ix++) {
		std::vector<std::string> row;
		for (idx_t col_ix = 0; col_ix < input.ColumnCount(); col_ix++) {
			auto &col = input.data[col_ix];
			Value val = col.GetValue(row_ix);
			if (val.IsNull()) {
				row.emplace_back("");
			} else {
				row.emplace_back(val.ToString());
			}
		}
		data.values.emplace_back(std::move(row));
	}
	auto range_str = encoded_sheet_name;
	if (!sheet_range.empty()) {
		range_str += "!" + sheet_range;
	}
	client.Spreadsheets(gstate.spreadsheet_id).Values().Append(sheets::A1Range(range_str), data);
}
} // namespace duckdb
