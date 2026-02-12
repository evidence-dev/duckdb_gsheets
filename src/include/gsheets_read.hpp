#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/main/client_context.hpp"

namespace duckdb {

struct ReadSheetBindData : public TableFunctionData {
	mutable bool finished;
	mutable idx_t row_index;
	bool header;
	std::vector<std::vector<std::string>> values;
	vector<LogicalType> return_types;
	vector<string> names;

	ReadSheetBindData(bool header, std::vector<std::vector<std::string>> values)
	    : finished(false), row_index(0), header(header), values(std::move(values)) {
	}
};

void ReadSheetFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);

unique_ptr<FunctionData> ReadSheetBind(ClientContext &context, TableFunctionBindInput &input,
                                       vector<LogicalType> &return_types, vector<string> &names);

} // namespace duckdb
