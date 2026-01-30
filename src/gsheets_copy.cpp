#include <vector>

#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/types/value.hpp"

#include "gsheets_copy.hpp"
#include "gsheets_utils.hpp"

#include "sheets/auth_factory.hpp"
#include "sheets/client.hpp"
#include "sheets/range.hpp"
#include "sheets/transport/httplib_client.hpp"
#include "sheets/types.hpp"

namespace duckdb {

GSheetCopyFunction::GSheetCopyFunction() : CopyFunction("gsheet") {
	copy_to_bind = GSheetWriteBind;
	copy_to_initialize_global = GSheetWriteInitializeGlobal;
	copy_to_initialize_local = GSheetWriteInitializeLocal;
	copy_to_sink = GSheetWriteSink;
}

unique_ptr<FunctionData> GSheetCopyFunction::GSheetWriteBind(ClientContext &context, CopyFunctionBindInput &input,
                                                             const vector<string> &names,
                                                             const vector<LogicalType> &sql_types) {
	string file_path = input.info.file_path;

	auto options = input.info.options;

	const auto sheet_opt = options.find("sheet");
	std::string sheet;
	if (sheet_opt != options.end()) {
		string error_msg;
		Value sheet_val;
		if (!sheet_opt->second.back().DefaultTryCastAs(LogicalType::VARCHAR, sheet_val, &error_msg)) {
			throw BinderException("sheet option must be a VARCHAR");
		}
		if (sheet_val.IsNull()) {
			throw BinderException("sheet option must be a non-null VARCHAR");
		}
		sheet = StringValue::Get(sheet_val);
	} else {
		sheet = "";
	}

	const auto range_opt = options.find("range");
	std::string range;
	if (range_opt != options.end()) {
		string error_msg;
		Value range_val;
		if (!range_opt->second.back().DefaultTryCastAs(LogicalType::VARCHAR, range_val, &error_msg)) {
			throw BinderException("range option must be a VARCHAR");
		}
		if (range_val.IsNull()) {
			throw BinderException("range option must be a non-null VARCHAR");
		}
		range = StringValue::Get(range_val);
	} else {
		range = "";
	}

	const auto overwrite_sheet_opt = options.find("overwrite_sheet");
	bool overwrite_sheet;
	if (overwrite_sheet_opt != options.end()) {
		if (overwrite_sheet_opt->second.size() != 1) {
			throw BinderException("overwrite_sheet option must be a single boolean value");
		}
		string error_msg;
		Value overwrite_sheet_bool_val;
		if (!overwrite_sheet_opt->second.back().DefaultTryCastAs(LogicalType::BOOLEAN, overwrite_sheet_bool_val,
		                                                         &error_msg)) {
			throw BinderException("overwrite_sheet option must be a single boolean value");
		}
		if (overwrite_sheet_bool_val.IsNull()) {
			throw BinderException("overwrite_sheet option must be a single boolean value");
		}
		overwrite_sheet = BooleanValue::Get(overwrite_sheet_bool_val);
	} else {
		overwrite_sheet = true; // Default to overwrite_sheet to maintain prior behavior
	}

	const auto overwrite_range_opt = options.find("overwrite_range");
	bool overwrite_range;
	if (overwrite_range_opt != options.end()) {
		if (overwrite_range_opt->second.size() != 1) {
			throw BinderException("overwrite_range option must be a single boolean value");
		}
		string error_msg;
		Value overwrite_range_bool_val;
		if (!overwrite_range_opt->second.back().DefaultTryCastAs(LogicalType::BOOLEAN, overwrite_range_bool_val,
		                                                         &error_msg)) {
			throw BinderException("overwrite_range option must be a single boolean value");
		}
		if (overwrite_range_bool_val.IsNull()) {
			throw BinderException("overwrite_range option must be a single boolean value");
		}
		overwrite_range = BooleanValue::Get(overwrite_range_bool_val);
	} else {
		overwrite_range = false;
	}

	const auto header_opt = options.find("header");
	bool header;
	if (header_opt != options.end()) {
		if (header_opt->second.size() != 1) {
			throw BinderException("header option must be a single boolean value");
		}
		string error_msg;
		Value header_bool_val;
		if (!header_opt->second.back().DefaultTryCastAs(LogicalType::BOOLEAN, header_bool_val, &error_msg)) {
			throw BinderException("header option must be a single boolean value");
		}
		if (header_bool_val.IsNull()) {
			throw BinderException("header option must be a single boolean value");
		}
		header = BooleanValue::Get(header_bool_val);
	} else {
		header = true;
		// If we are in the append case, default to no header instead.
		if (!overwrite_sheet && !overwrite_range) {
			header = false;
		}
	}

	return make_uniq<GSheetWriteBindData>(file_path, sql_types, names, sheet, range, overwrite_sheet, overwrite_range,
	                                      header);
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
	auto http = make_uniq<sheets::HttpLibClient>();
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
