// Taken with minor modifications from
// https://github.com/duckdb/postgres_scanner/blob/main/src/include/postgres_binary_copy.hpp

#pragma once

#include "duckdb/function/copy_function.hpp"

namespace duckdb {
struct GSheetCopyGlobalState : public GlobalFunctionData {
	explicit GSheetCopyGlobalState(ClientContext &context, const string &spreadsheet_id, const string &token,
	                               const string &sheet_name)
	    : spreadsheet_id(spreadsheet_id), token(token), sheet_name(sheet_name) {
	}

public:
	string spreadsheet_id;
	string token;
	string sheet_name;
};

struct GSheetWriteOptions {
	vector<string> name_list;
	std::string sheet;
	std::string range;
	bool overwrite_sheet;
	bool overwrite_range;
	bool header;
};

struct GSheetWriteBindData : public TableFunctionData {
	vector<string> files;
	GSheetWriteOptions options;
	vector<LogicalType> sql_types;

	GSheetWriteBindData(string file_path, vector<LogicalType> sql_types, vector<string> names, std::string sheet,
	                    std::string range, bool overwrite_sheet, bool overwrite_range, bool header)
	    : sql_types(std::move(sql_types)) {
		files.push_back(std::move(file_path));
		options.name_list = std::move(names);
		options.sheet = std::move(sheet);
		options.range = std::move(range);
		options.overwrite_sheet = std::move(overwrite_sheet);
		options.overwrite_range = std::move(overwrite_range);
		options.header = std::move(header);
	}
};

class GSheetCopyFunction : public CopyFunction {
public:
	GSheetCopyFunction();

	static unique_ptr<FunctionData> GSheetWriteBind(ClientContext &context, CopyFunctionBindInput &input,
	                                                const vector<string> &names, const vector<LogicalType> &sql_types);

	static unique_ptr<GlobalFunctionData> GSheetWriteInitializeGlobal(ClientContext &context, FunctionData &bind_data,
	                                                                  const string &file_path);

	static unique_ptr<LocalFunctionData> GSheetWriteInitializeLocal(ExecutionContext &context,
	                                                                FunctionData &bind_data_p);

	static void GSheetWriteSink(ExecutionContext &context, FunctionData &bind_data_p, GlobalFunctionData &gstate,
	                            LocalFunctionData &lstate, DataChunk &input);
};

} // namespace duckdb