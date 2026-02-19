#pragma once

#include <string>
#include <utility>

#include "duckdb/common/types/value.hpp"

namespace duckdb {
namespace sheets {

std::string GetStringOption(const case_insensitive_map_t<vector<Value>> &options, const std::string &name,
                            const std::string &default_value = "");

std::string GetStringOption(const case_insensitive_map_t<Value> &options, const std::string &name,
                            const std::string &default_value = "");

std::pair<bool, bool> GetBoolOption(const case_insensitive_map_t<vector<Value>> &options, const std::string &name,
                                    bool default_value = false);

std::pair<bool, bool> GetBoolOption(const case_insensitive_map_t<Value> &options, const std::string &name,
                                    bool default_value = false);

} // namespace sheets
} // namespace duckdb
