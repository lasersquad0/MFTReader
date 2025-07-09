
/*
 * Common.h
 *
 * Copyright 2025, LogEngine2 Project. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#pragma once

//#include <ctype.h>
#include <time.h>
#include <tchar.h>
#include <string>
#include <chrono>
#include <mutex>

#ifdef LOGENGINE_HEADER_ONLY
#define LOGENGINE_INLINE inline
#else
#define LOGENGINE_INLINE
#endif

typedef unsigned char uchar;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned long long ullong;

#define LOGENGINE_NS_BEGIN namespace LogEngine {
#define LOGENGINE_NS_END }
#define LOGENGINE_NS LogEngine

#define CRChar '\r'
#define LFChar '\n'

#ifdef WIN32 
#define EndLine "\r\n"
#define EndLineChar '\n'
// this macro works with both string and wstring, looks like CRChar/LFChar is automatically extended to wchar_t for string
#define BUILD_ENDL(_) (_) += CRChar; (_) += LFChar
#else
#define EndLine "\n"
#define EndLineChar '\n'
#define BUILD_ENDL(_) ((_) += LFChar)
#endif

typedef std::lock_guard<std::recursive_mutex> mutexguard;
typedef std::chrono::time_point<std::chrono::system_clock> tm_point;

// truncates Value to Precision digits after point
double round(const double Value, const int Precision);

// functions to convert varous ints and longs to string
std::string IntToStr(int Value, int FieldSize);
std::string IntToStr(uint Value, int FieldSize);
std::string IntToStr(int Value);
std::string IntToStr(uint Value);
std::string IntToStr(ulong Value);

// function to convert double to string
std::string FloatToStr(double Value);

// function to convert bool to string
std::string BoolToStr(bool Value);

// function to convert string value (1,0,yes,no,true,false) to the bool
bool StrToBool(const std::string& Value);

// extracts filename from path with FileName
std::string ExtractFileName(const std::string& FileName);

// extracts directory name from FileName. FileName can be either file name or path with file name.
std::string ExtractFileDir(const std::string& FileName);

// extracts file extention. returns empty string if file does not have the extention
std::string ExtractFileExt(const std::string& FileName);

// excludes file extention from FileName
std::string StripFileExt(const std::string& FileName);

// replaces in string S all occurrences of OldPattern by NewPattern
std::string StringReplace(const std::string& S, const std::string& OldPattern, const std::string& NewPattern);

tm_point GetCurrTimePoint();

struct tm GetCurrDateTime();

// converts date to string representation
//std::string DateToString(int Date);

// retrieves current date as std::string
std::string GetCurrDateAsString(void);

// retrieves current time as std::string
std::string GetCurrTimeAsString(void);

// retrieves current datetime as std::string
std::string GetCurrDateTimeAsString(void);

// gets formatted current datetime
std::string FormatCurrDateTime(const std::string& FormatStr);

// converts native datetime value into std::string
std::string DateTimeToStr(time_t t);
std::string DateTimeToStr(struct tm const& t);

// deletes all leading and trailing \n \r symbols from string
std::string DelCRLF(const std::string str);

// removes all leading and trailing \n \r and space symbols from string
std::string TrimSPCRLF(std::string str);

// removes all leading and trailing space and tab symbols from string
std::string Trim(std::string str);

// removes any leading space and tab symbols from string
std::string TrimLeft(std::string str);

// removes any trailing space and tab symbols from string
std::string TrimRight(std::string str);

// compares two strings case insensitive
template <typename STRING>
bool EqualNCase(const STRING& str1, const STRING& str2)
{
	// make sure that STRING is one of instantiations of std::string
	static_assert(std::is_base_of<std::basic_string<typename STRING::value_type, typename STRING::traits_type>, STRING>::value);

	if (str1.length() == 0 && str2.length() == 0)
		return true;
	if ((str1.length() == 0) && (str2.length() > 0))
		return false;
	if ((str1.length() > 0) && (str2.length() == 0))
		return false;

	//#ifdef HAVE_STRCASECMP
	//	return strcasecmp(s1, s2) == 0;
	//#else

		//for (; *s1 != '\0' && *s2 != '\0'; s1++, s2++)
	for (uint i = 0; i < str1.length() && i < str2.length(); i++)
		if (_totupper((str1[i])) != _totupper((str2[i])))
			return false;

	if (str1.length() == str2.length())
		return true;
	else
		return false;
	//#endif
}

template<class STRING>
int CompareNCase(const STRING& str1, const STRING& str2)
{
	// make sure that STRING is one of instantiations of std::string
	static_assert(std::is_base_of<std::basic_string<typename STRING::value_type, typename STRING::traits_type>, STRING>::value);

	if ((str1.length() == 0) && (str2.length() == 0)) // two empty strings are equal
		return 0;

	if ((str1.length() == 0)) //empty string is less any non empty 
		return -1;

	if (str2.length() == 0) // non-empty string is larger any empty one
		return 1;

	//#ifdef HAVE_STRCASECMP
	//	return strcasecmp(s1, s2);
	//#else
	for (uint i = 0; i < str1.length() && str2.length(); i++)
	{
		int upper1 = _totupper((str1[i]));
		int upper2 = _totupper((str2[i]));

		if (upper1 > upper2)
			return 1;
		else if (upper1 < upper2)
			return -1;
	}

	if (str1.length() > str2.length())
		return 1;
	else if (str1.length() < str2.length())
		return -1;
	else
		return 0;
	//#endif
}

// checks if string contains unsigned integer or not
bool isUInt(std::string& value);

uint GetThreadID();
uint GetThreadID(const std::thread::id& id);

std::string StrToLower(std::string str);

std::string DisplaySystemVersion();


#ifdef LOGENGINE_HEADER_ONLY
#include "Common-hdr.h"
#endif
