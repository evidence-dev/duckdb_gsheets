#include "json.hpp"

#include "sheets/types.hpp"
#include "sheets/resources/values.hpp"
#include "sheets/util/response.hpp"

using json = nlohmann::json;

namespace duckdb {
namespace sheets {

ValueRange ValuesResource::Get(const A1Range &range) {
	std::string path = "/spreadsheets/" + spreadsheetId + "/values/" + range.ToString();
	return ParseResponse<ValueRange>(DoGet(path));
}

UpdateValuesResponse ValuesResource::Update(const A1Range &range, const ValueRange &values) {
	std::string path =
	    "/spreadsheets/" + spreadsheetId + "/values/" + range.ToString() + "?valueInputOption=USER_ENTERED";
	std::string body = json(values).dump();
	return ParseResponse<UpdateValuesResponse>(DoPut(path, body));
}

AppendValuesResponse ValuesResource::Append(const A1Range &range, const ValueRange &values) {
	std::string path =
	    "/spreadsheets/" + spreadsheetId + "/values/" + range.ToString() + ":append" + "?valueInputOption=USER_ENTERED";
	std::string body = json(values).dump();
	return ParseResponse<AppendValuesResponse>(DoPost(path, body));
}

ClearValuesResponse ValuesResource::Clear(const A1Range &range) {
	std::string path = "/spreadsheets/" + spreadsheetId + "/values/" + range.ToString() + ":clear";
	return ParseResponse<ClearValuesResponse>(DoPost(path, "{}"));
}

} // namespace sheets
} // namespace duckdb
