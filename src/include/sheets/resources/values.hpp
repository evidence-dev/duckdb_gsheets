#pragma once

#include "sheets/range.hpp"
#include "sheets/resources/base.hpp"
#include "sheets/transport/http_client.hpp"
#include "sheets/transport/http_type.hpp"
#include "sheets/types.hpp"

namespace duckdb {
namespace sheets {

class ValuesResource : protected BaseResource {
public:
	ValuesResource(IHttpClient &http, const HttpHeaders &headers, const std::string &baseUrl,
	               const std::string &spreadsheetId)
	    : BaseResource(http, headers, baseUrl), spreadsheetId(spreadsheetId) {};

	ValueRange Get(const A1Range &range);
	UpdateValuesResponse Update(const A1Range &range, const ValueRange &values);
	AppendValuesResponse Append(const A1Range &range, const ValueRange &values);
	ClearValuesResponse Clear(const A1Range &range);

private:
	std::string spreadsheetId;
};

} // namespace sheets
} // namespace duckdb
