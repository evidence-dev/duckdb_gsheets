#include "utils/version.hpp"

std::string duckdb::getVersion() {
#ifdef EXT_VERSION_GSHEETS
	return EXT_VERSION_GSHEETS;
#else
	return "";
#endif
}
