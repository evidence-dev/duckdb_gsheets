#include "duckdb/common/exception/binder_exception.hpp"
#include "duckdb/common/types/value.hpp"

#include "utils/options.hpp"

std::string duckdb::sheets::GetStringOption(const identifier_map_t<vector<Value>> &options,
                                            const std::string &name, const std::string &default_value) {
	const auto it = options.find(Identifier(name));
	if (it == options.end()) {
		return default_value;
	}
	std::string err;
	auto val = it->second.back().DefaultTryCastAs(LogicalType::VARCHAR, &err);
	if (!val) {
		throw BinderException(name + " option must be VARCHAR");
	}
	if (val->IsNull()) {
		throw BinderException(name + " option must not be NULL");
	}
	return StringValue::Get(*val);
}

std::string duckdb::sheets::GetStringOption(const case_insensitive_map_t<Value> &options, const std::string &name,
                                            const std::string &default_value) {
	const auto it = options.find(name);
	if (it == options.end()) {
		return default_value;
	}
	std::string err;
	auto val = it->second.DefaultTryCastAs(LogicalType::VARCHAR, &err);
	if (!val) {
		throw BinderException(name + " option must be VARCHAR");
	}
	if (val->IsNull()) {
		throw BinderException(name + " option must not be NULL");
	}
	return StringValue::Get(*val);
}

std::pair<bool, bool> duckdb::sheets::GetBoolOption(const identifier_map_t<vector<Value>> &options,
                                                    const std::string &name, bool default_value) {
	const auto it = options.find(Identifier(name));
	if (it == options.end()) {
		return std::make_pair(default_value, false);
	}
	if (it->second.size() != 1) {
		throw BinderException(name + " option must be a single boolean value");
	}
	std::string err;
	auto val = it->second.back().DefaultTryCastAs(LogicalType::BOOLEAN, &err);
	if (!val) {
		throw BinderException(name + " option must be a single boolean value");
	}
	if (val->IsNull()) {
		throw BinderException(name + " option must be a single boolean value");
	}
	return std::make_pair(BooleanValue::Get(*val), true);
}

std::pair<bool, bool> duckdb::sheets::GetBoolOption(const case_insensitive_map_t<Value> &options,
                                                    const std::string &name, bool default_value) {
	const auto it = options.find(name);
	if (it == options.end()) {
		return std::make_pair(default_value, false);
	}
	std::string err;
	auto val = it->second.DefaultTryCastAs(LogicalType::BOOLEAN, &err);
	if (!val) {
		throw BinderException(name + " option must be a single boolean value");
	}
	if (val->IsNull()) {
		throw BinderException(name + " option must be a single boolean value");
	}
	return std::make_pair(BooleanValue::Get(*val), true);
}
