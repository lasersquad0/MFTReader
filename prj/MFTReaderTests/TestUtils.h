#pragma once

#include "windows.h"
#include "strutils/include/string_utils.h"

#define MFT_TESTS_LOG_FILE "LogMFTReaderTests.log"
//#define MFT_TESTS_LOGGER_NAME "mft_tests_logger"

#define TEST_DATA_DIR "../../TestData/"

template<typename STRING, typename ARRAY>
bool EXPECT_ONE_OF(STRING expected, ARRAY arr)
{
	// make sure that STRING is one of instantiations of strings
	static_assert(std::is_base_of<std::basic_string<typename STRING::value_type, typename STRING::traits_type>, STRING>::value);
	// make sure that wstr is either std::string (no conversion required) or std::wstring
	static_assert(std::is_same_v<typename STRING::value_type, wchar_t> || std::is_same_v<typename STRING::value_type, char>);
	static_assert(std::is_same_v<typename ARRAY::item_type::value_type, wchar_t> || std::is_same_v<typename ARRAY::item_type::value_type, char>);
	//static_assert(std::is_same_v<typename STRING::value_type, typename ARRAY::item_type::value_type>);
	//static_assert(std::is_convertible_v<typename ARRAY::item_type, STRING>);
	
	for (auto& item : arr)
	{
		auto str = convert_string<typename STRING::value_type>(item);
		if (expected == str.c_str()) return true; //==STRING(str.c_str())
	}
	return false;
}

