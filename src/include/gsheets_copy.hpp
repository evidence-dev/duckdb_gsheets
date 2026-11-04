// Taken with minor modifications from
// https://github.com/duckdb/postgres_scanner/blob/main/src/include/postgres_binary_copy.hpp

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "duckdb/function/copy_function.hpp"

#include "sheets/transport/http_client.hpp"
#include "sheets/auth/auth_provider.hpp"

namespace duckdb {
struct GSheetCopyGlobalState : public GlobalFunctionData {
	explicit GSheetCopyGlobalState(ClientContext &context, std::unique_ptr<sheets::IHttpClient> http,
	                               std::unique_ptr<sheets::IAuthProvider> auth, const string &spreadsheet_id,
	                               const string &sheet_name)
	    : http(std::move(http)), auth(std::move(auth)), spreadsheet_id(spreadsheet_id), sheet_name(sheet_name) {
	}

public:
	std::unique_ptr<sheets::IHttpClient> http;
	std::unique_ptr<sheets::IAuthProvider> auth;
	string spreadsheet_id;
	string sheet_name;

	//! Rows to write, header first. Sink only adds to this; Finalize sends it in as few requests as fit.
	std::vector<std::vector<std::string>> rows;
	//! Sink can run on several threads at once when insertion order is not preserved
	std::mutex rows_lock;
};

struct GSheetWriteOptions {
	vector<string> name_list;
	std::string sheet;
	std::string gid;
	std::string range;
	bool overwrite_sheet;
	bool overwrite_range;
	bool create_if_not_exists;
	bool header;
};

struct GSheetWriteBindData : public TableFunctionData {
	vector<string> files;
	GSheetWriteOptions options;
	vector<LogicalType> sql_types;

	GSheetWriteBindData(string file_path, vector<LogicalType> sql_types, vector<string> names, std::string sheet,
	                    std::string gid, std::string range, bool overwrite_sheet, bool overwrite_range,
	                    bool create_if_not_exists, bool header)
	    : sql_types(std::move(sql_types)) {
		files.push_back(std::move(file_path));
		options.name_list = std::move(names);
		options.sheet = std::move(sheet);
		options.gid = std::move(gid);
		options.range = std::move(range);
		options.overwrite_sheet = overwrite_sheet;
		options.overwrite_range = overwrite_range;
		options.create_if_not_exists = create_if_not_exists;
		options.header = header;
	}
};

class GSheetCopyFunction : public CopyFunction {
public:
	//! Name of the copy function that does the writing; "gsheet" rewrites COPY statements to it
	static constexpr const char *WRITE_FORMAT = "gsheet_write";

	GSheetCopyFunction();

	//! The user-facing "gsheet" copy function, which only rewrites the statement for the writer
	static CopyFunction GetPlanFunction();

	static BoundStatement GSheetCopyPlan(Binder &binder, CopyStatement &stmt);

	static unique_ptr<FunctionData> GSheetWriteBind(ClientContext &context, CopyFunctionBindInput &input,
	                                                const vector<Identifier> &names,
	                                                const vector<LogicalType> &sql_types);

	static unique_ptr<GlobalFunctionData> GSheetWriteInitializeGlobal(ClientContext &context, FunctionData &bind_data,
	                                                                  const string &file_path);

	static unique_ptr<LocalFunctionData> GSheetWriteInitializeLocal(ExecutionContext &context,
	                                                                FunctionData &bind_data_p);

	static void GSheetWriteSink(ExecutionContext &context, FunctionData &bind_data_p, GlobalFunctionData &gstate,
	                            LocalFunctionData &lstate, DataChunk &input);

	static void GSheetWriteCombine(ExecutionContext &context, FunctionData &bind_data, GlobalFunctionData &gstate,
	                               LocalFunctionData &lstate);

	static void GSheetWriteFinalize(ClientContext &context, FunctionData &bind_data, GlobalFunctionData &gstate);
};

} // namespace duckdb
