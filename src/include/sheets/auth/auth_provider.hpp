#pragma once

#include <string>

namespace duckdb {
namespace sheets {

class IAuthProvider {
public:
	virtual ~IAuthProvider() = default;
	virtual std::string GetAuthorizationHeader() = 0;
};

} // namespace sheets
} // namespace duckdb
