#include "gsheets_copy.hpp"
#include "gsheets_requests.hpp"
#include "gsheets_auth.hpp"
#include "gsheets_utils.hpp"

#include "duckdb/common/serializer/buffered_file_writer.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include <json.hpp>
#include <regex>
#include "gsheets_get_token.hpp"

#include <iostream>

using json = nlohmann::json;

namespace duckdb
{

    GSheetCopyFunction::GSheetCopyFunction() : CopyFunction("gsheet")
    {
        copy_to_bind = GSheetWriteBind;
        copy_to_initialize_global = GSheetWriteInitializeGlobal;
        copy_to_initialize_local = GSheetWriteInitializeLocal;
        copy_to_sink = GSheetWriteSink;
    }


    unique_ptr<FunctionData> GSheetCopyFunction::GSheetWriteBind(ClientContext &context, CopyFunctionBindInput &input, const vector<string> &names, const vector<LogicalType> &sql_types)
    {
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
            if (!overwrite_sheet_opt->second.back().DefaultTryCastAs(LogicalType::BOOLEAN, overwrite_sheet_bool_val, &error_msg)) {
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
            if (!overwrite_range_opt->second.back().DefaultTryCastAs(LogicalType::BOOLEAN, overwrite_range_bool_val, &error_msg)) {
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

        return make_uniq<GSheetWriteBindData>(file_path, sql_types, names, sheet, range, overwrite_sheet, overwrite_range, header);
    }

    unique_ptr<GlobalFunctionData> GSheetCopyFunction::GSheetWriteInitializeGlobal(ClientContext &context, FunctionData &bind_data, const string &file_path)
    {
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


        auto options = bind_data.Cast<GSheetWriteBindData>().options;

        if (secret.GetProvider() == "private_key") {
            // If using a private key, retrieve the private key from the secret, but convert it 
            // into a token before use. This is an extra request per Google Sheet read or copy.
            // The secret is the JSON file that is extracted from Google as per the README
            

            Value client_email_value;
            if (!kv_secret->TryGetValue("client_email", client_email_value)) {
                throw InvalidInputException("'client_email' not found in 'gsheet' secret");
            }
            std::string client_email_string = client_email_value.ToString();

            Value sheets_private_key_value;
            if (!kv_secret->TryGetValue("sheets_private_key", sheets_private_key_value)) {
                throw InvalidInputException("'sheets_private_key' not found in 'gsheet' secret");
            }
            std::string sheets_private_key_string = sheets_private_key_value.ToString();
            
            token = get_token(client_email_string, sheets_private_key_string);
        } else {
            Value token_value;
            if (!kv_secret->TryGetValue("token", token_value)) {
                throw InvalidInputException("'token' not found in 'gsheet' secret");
            }

            token = token_value.ToString();
        }

        std::string spreadsheet_id = extract_spreadsheet_id(file_path);
        std::string sheet_id = extract_sheet_id(file_path);
        std::string sheet_name; 
        std::string sheet_range;

        // Prefer a sheet or range that is specified as a parameter over one on the query string
        if (!options.sheet.empty()) {
            sheet_name = options.sheet;
        } else {
            sheet_name = get_sheet_name_from_id(spreadsheet_id, sheet_id, token);
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
            delete_sheet_data(spreadsheet_id, token, encoded_sheet_name, sheet_range);
        } else if (options.overwrite_sheet) {
            std::string empty_string = ""; // An empty string will clear the entire sheet
            delete_sheet_data(spreadsheet_id, token, encoded_sheet_name, empty_string);    
        } 
        
        // Write out the headers to the file here in the Initialize so they are only written once
        // Create object ready to write to Google Sheet
        json sheet_data;

        sheet_data["range"] = sheet_range.empty() ? sheet_name : sheet_name + "!" + sheet_range;
        sheet_data["majorDimension"] = "ROWS";

        vector<string> headers = options.name_list;        

        vector<vector<string>> values;
        values.push_back(headers);
        sheet_data["values"] = values;

        // Convert the JSON object to a string
        std::string request_body = sheet_data.dump();

        // Make the API call to write headers to the Google Sheet
        // If we are appending, header defaults to false
        if (options.header) {
            std::string response = call_sheets_api(spreadsheet_id, token, encoded_sheet_name, sheet_range, HttpMethod::POST, request_body);
        
            // Check for errors in the response
            json response_json = parseJson(response);
            if (response_json.contains("error")) {
                throw duckdb::IOException("Error writing to Google Sheet: " + response_json["error"]["message"].get<std::string>());
            }
        }
        return make_uniq<GSheetCopyGlobalState>(context, spreadsheet_id, token, encoded_sheet_name);
    }

    unique_ptr<LocalFunctionData> GSheetCopyFunction::GSheetWriteInitializeLocal(ExecutionContext &context, FunctionData &bind_data_p)
    {
        return make_uniq<LocalFunctionData>();
    }

    void GSheetCopyFunction::GSheetWriteSink(ExecutionContext &context, FunctionData &bind_data_p, GlobalFunctionData &gstate_p, LocalFunctionData &lstate, DataChunk &input)
    {
        input.Flatten();
        auto &gstate = gstate_p.Cast<GSheetCopyGlobalState>();


        std::string file = bind_data_p.Cast<GSheetWriteBindData>().files[0];
        auto options = bind_data_p.Cast<GSheetWriteBindData>().options;
        std::string sheet_id = extract_sheet_id(file);
        std::string sheet_name; 
        std::string sheet_range;

        // Prefer a sheet or range that is specified as a parameter over one on the query string
        if (!options.sheet.empty()) {
            sheet_name = options.sheet;
        } else {
            sheet_name = get_sheet_name_from_id(gstate.spreadsheet_id, sheet_id, gstate.token);
        }
        
        if (!options.range.empty()) {
            sheet_range = options.range;
        } else {
            sheet_range = extract_sheet_range(file);
        }

        std::string encoded_sheet_name = url_encode(sheet_name);
        // Create object ready to write to Google Sheet
        json sheet_data;

        sheet_data["range"] = sheet_range.empty() ? sheet_name : sheet_name + "!" + sheet_range;
        sheet_data["majorDimension"] = "ROWS";

        vector<vector<string>> values;

        for (idx_t r = 0; r < input.size(); r++)
        {
            vector<string> row;
            for (idx_t c = 0; c < input.ColumnCount(); c++)
            {
                auto &col = input.data[c];
                Value val = col.GetValue(r);
                if (val.IsNull()) {
                    row.push_back("");
                } else {
                    row.push_back(val.ToString());
                }
            }
            values.push_back(row);
        }
        sheet_data["values"] = values;

        // Convert the JSON object to a string
        std::string request_body = sheet_data.dump();

        // Make the API call to write data to the Google Sheet
        std::string response = call_sheets_api(gstate.spreadsheet_id, gstate.token, encoded_sheet_name, sheet_range, HttpMethod::POST, request_body);

        // Check for errors in the response
        json response_json = parseJson(response);
        if (response_json.contains("error")) {
            throw duckdb::IOException("Error writing to Google Sheet: " + response_json["error"]["message"].get<std::string>());
        }
    }
} // namespace duckdb
