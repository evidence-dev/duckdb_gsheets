#include "gsheets_read.hpp"
#include "gsheets_utils.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "gsheets_requests.hpp"
#include <json.hpp>
#include <string>
#include <regex>
#include "gsheets_get_token.hpp"
#include <iostream>

namespace duckdb {

using json = nlohmann::json;

ReadSheetBindData::ReadSheetBindData(string spreadsheet_id, string token, bool header, string sheet_name, string sheet_range) 
    : spreadsheet_id(spreadsheet_id), token(token), finished(false), row_index(0), header(header), sheet_name(sheet_name), sheet_range(sheet_range) {
    response = call_sheets_api(spreadsheet_id, token, sheet_name, sheet_range, HttpMethod::GET);
}

bool IsValidNumber(const string& value) {
    // Skip empty strings
    if (value.empty()) {
        return false;
    }
    
    try {
        // Try to parse as double
        size_t processed;
        std::stod(value, &processed);
        // Ensure the entire string was processed
        return processed == value.length();
    } catch (...) {
        return false;
    }
}

bool IsValidA1Range(const std::string& range) {
    // Matches things like A1, $A$1, A1:B2, $A1:$B2, etc.
    static const std::regex pattern("^\\$?[A-Za-z]+\\$?[0-9]+(:\\$?[A-Za-z]+\\$?[0-9]+)?$");
    return std::regex_match(range, pattern);
}

void ReadSheetFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
    auto &bind_data = const_cast<ReadSheetBindData&>(data_p.bind_data->Cast<ReadSheetBindData>());

    if (bind_data.finished) {
        return;
    }
    
    json cleanJson = parseJson(bind_data.response);
    SheetData sheet_data = getSheetData(cleanJson);

    idx_t row_count = 0;
    idx_t column_count = output.ColumnCount();

    // Adjust starting index based on whether we're using the header
    idx_t start_index = bind_data.header ? bind_data.row_index + 1 : bind_data.row_index;

    for (idx_t i = start_index; i < sheet_data.values.size() && row_count < STANDARD_VECTOR_SIZE; i++) {
        const auto& row = sheet_data.values[i];
        for (idx_t col = 0; col < column_count; col++) {
            if (col < row.size()) {
                const string& value = row[col];
                switch (bind_data.return_types[col].id()) {
                    case LogicalTypeId::BOOLEAN:
                        if (value.empty()) {
                            output.SetValue(col, row_count, Value(LogicalType::BOOLEAN));
                        } else {
                            output.SetValue(col, row_count, Value(value).DefaultCastAs(LogicalType::BOOLEAN));
                        }
                        break;
                    case LogicalTypeId::DOUBLE:
                        if (value.empty()) {
                            output.SetValue(col, row_count, Value(LogicalType::DOUBLE));
                        } else {
                            output.SetValue(col, row_count, Value(value).DefaultCastAs(LogicalType::DOUBLE));
                        }
                        break;
                    default:
                        // Empty strings should be converted to NULL
                        if (value.empty()) {
                            output.SetValue(col, row_count, Value(LogicalType::VARCHAR));
                        } else {
                            output.SetValue(col, row_count, Value(value));
                        }
                        break;
                }
            } else {
                output.SetValue(col, row_count, Value(nullptr));
            }
        }
        row_count++;
    }

    bind_data.row_index += row_count;
    bind_data.finished = (bind_data.row_index >= (sheet_data.values.size() - (bind_data.header ? 1 : 0)));

    output.SetCardinality(row_count);
}

unique_ptr<FunctionData> ReadSheetBind(ClientContext &context, TableFunctionBindInput &input,
                                              vector<LogicalType> &return_types, vector<string> &names) {
    auto sheet_input = input.inputs[0].GetValue<string>();

    // Flags
    bool header = true;
    bool use_explicit_sheet_name = false;
    bool use_all_varchars = false;

    // Default values
    string sheet_name = "";
    string sheet_id = "";

    // Extract the spreadsheet ID from the input (URL or ID)
    std::string spreadsheet_id = extract_spreadsheet_id(sheet_input);

    // Try to extract the range from the input (URL or ID)
    std::string sheet_range = extract_sheet_range(sheet_input);

    // Use the SecretManager to get the token
    auto &secret_manager = SecretManager::Get(context);
    auto transaction = CatalogTransaction::GetSystemCatalogTransaction(context);
    auto secret_match = secret_manager.LookupSecret(transaction, "gsheet", "gsheet");
    
    if (!secret_match.HasMatch()) {
        throw InvalidInputException("No 'gsheet' secret found. Please create a secret with 'CREATE SECRET' first.");
    }

    auto &secret = secret_match.GetSecret();
    if (secret.GetType() != "gsheet") {
        throw InvalidInputException("Invalid secret type. Expected 'gsheet', got '%s'", secret.GetType());
    }

    const auto *kv_secret = dynamic_cast<const KeyValueSecret*>(&secret);
    if (!kv_secret) {
        throw InvalidInputException("Invalid secret format for 'gsheet' secret");
    }

    std::string token;

    if (secret.GetProvider() == "key_file") {
        // If using a private key, retrieve the private key from the secret, but convert it 
        // into a token before use. This is an extra request per 30 minutes.
        // The secret is the JSON file that is extracted from Google as per the README
        token = get_token_and_cache(context, transaction, kv_secret);

    } else {
        Value token_value;
        if (!kv_secret->TryGetValue("token", token_value)) {
            throw InvalidInputException("'token' not found in 'gsheet' secret");
        }

        token = token_value.ToString();
    }

    // Parse named parameters
    for (auto &kv : input.named_parameters) {
        if (kv.first == "header") {
            try {
                header = kv.second.GetValue<bool>();
            } catch (const std::exception& e) {
                throw InvalidInputException("Invalid value for 'header' parameter. Expected a boolean value.");
            }
        } else if (kv.first == "all_varchar") {
            try {
                use_all_varchars = kv.second.GetValue<bool>();
            } catch (const std::exception& e) {
                throw InvalidInputException("Invalid value for 'use_varchar' parameter. Expected a boolean value.");
            }
        } else if (kv.first == "sheet") {
            // TODO: maybe factor this out to clean up this space
            use_explicit_sheet_name = true;
            sheet_name = kv.second.GetValue<string>();

            // Check if sheet name is quoted and therefore might contain a `!` char that doesn't indicate A1 notation
            if (!sheet_name.empty() && sheet_name[0] == '\'') {
                size_t closing_quote_pos = sheet_name.find('\'', 1);
                if (closing_quote_pos != std::string::npos) {
                    // Check if there is a `!` char after the closing quote which would indicate A1 notation
                    if (closing_quote_pos + 1 < sheet_name.size() && sheet_name[closing_quote_pos + 1] == '!') {
                        sheet_range = sheet_name.substr(closing_quote_pos + 2);
                    }
                    // keep only unquoted part of name
                    sheet_name = sheet_name.substr(1, closing_quote_pos - 1);
                }
            } else {
                // No quotes means any `!` char indicates A1 notation
                size_t pos = sheet_name.find("!");
                if (pos != std::string::npos) {
                    sheet_range = sheet_name.substr(pos + 1);
                    sheet_name = sheet_name.substr(0, pos);
                }
            }

            // Validate that sheet with name exists for better error messaging
            sheet_id = get_sheet_id_from_name(spreadsheet_id, sheet_name, token);
        } else if (kv.first == "range") {
            sheet_range = kv.second.GetValue<string>();
        }
    }

    // Get sheet name from URL if not provided as input
    if (!use_explicit_sheet_name) {
        sheet_id = extract_sheet_id(sheet_input);
        if (sheet_id.empty()) {
            // Fallback to first sheet by index
            sheet_name = get_sheet_name_from_index(spreadsheet_id, "0", token);
        } else {
            sheet_name = get_sheet_name_from_id(spreadsheet_id, sheet_id, token);
        }
    }

    std::string encoded_sheet_name = url_encode(sheet_name);
    
    auto bind_data = make_uniq<ReadSheetBindData>(spreadsheet_id, token, header, encoded_sheet_name, sheet_range);

    json cleanJson = parseJson(bind_data->response);
    SheetData sheet_data = getSheetData(cleanJson);

    // Throw error ourselves to give user a better error message
    if (sheet_data.values.empty()) {
        throw duckdb::InvalidInputException("Range %s is empty", sheet_data.range);
    }

    idx_t start_index = header ? 1 : 0;

    // Use empty row for first row if results are header-only
    const std::vector<string> empty_row = {};
    const auto& first_data_row = start_index >= sheet_data.values.size() ? empty_row : sheet_data.values[start_index];

    // If we have a header, we want the width of the result to be the max of:
    //      the width of the header row
    //      or the width of the first row of data
    int result_width = first_data_row.size();
    if (header) {
        int header_width = sheet_data.values[0].size();
        if (header_width > result_width) {
            result_width = header_width;
        }
    }
    
    for (size_t i = 0; i < result_width; i++) {
        // Assign default column_name, but rename to header value if using a header and header cell exists
        string column_name = "column" + std::to_string(i + 1);
        if (header && (i < sheet_data.values[0].size())) {
            column_name = sheet_data.values[0][i];
        }
        names.push_back(column_name);
        
        // If the first row has blanks, assume varchar for now
        if (i >= first_data_row.size() || use_all_varchars) {
            return_types.push_back(LogicalType::VARCHAR);
            continue;
        } 
        const string& value = first_data_row[i];
        if (value == "TRUE" || value == "FALSE") {
            return_types.push_back(LogicalType::BOOLEAN);
        } else if (IsValidNumber(value)) {
            return_types.push_back(LogicalType::DOUBLE);
        } else {
            return_types.push_back(LogicalType::VARCHAR);
        }
    }

    bind_data->names = names;
    bind_data->return_types = return_types;

    return bind_data;
}

} // namespace duckdb

