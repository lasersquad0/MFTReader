/*
 * Common.cpp
 *
 * Copyright 2025, LogEngine2 Project. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#include <string.h> // need for strstr under Linux
#include <sstream>
#include <string>
#include <cassert>
#include <algorithm>
#include <sys/timeb.h>
#include "Common.h"

// truncates Value to Precision digits after point
LOGENGINE_INLINE double round(const double Value, const int Precision)
{
	int i = Precision;
	double ret;
	double p = 1;

	if(Precision >= 0)
		while(i--)p *= 10;
	else 
		while(i++)p /= 10;

	double temp = Value * p;

	double ttt = temp - static_cast<int>(temp);
	if (ttt == 0) //TODO floating point should not be compared to 0 by ==
		return temp/p;
	
	if (ttt >= 0.5 && Value > 0)
	{
		ret = int(temp + 0.5);
		return ret/p;
	}
	if (ttt <= -0.5 && Value < 0)
	{
		ret = int(temp - 0.5);
		return ret/p;
	}
	ret = int(temp);
	return ret/p;
}

#define CONV_BUF 20

#ifdef WIN32

// function for convert int Value to string
LOGENGINE_INLINE std::string IntToStr(int Value, int FieldSize)
{
	char buf[CONV_BUF];
	char buf2[CONV_BUF];
    sprintf_s(buf2, CONV_BUF,"%%-%dd", FieldSize);
    sprintf_s(buf, CONV_BUF, buf2, Value);

	return buf;
}

// function for convert uint Value to string
LOGENGINE_INLINE std::string IntToStr(uint Value, int FieldSize)
{
	char buf[CONV_BUF];
	char buf2[CONV_BUF];
	sprintf_s(buf2, CONV_BUF, "%%-%du", FieldSize);
	sprintf_s(buf, CONV_BUF, buf2, Value);

	return buf;
}
	
// function for convert int Value to string
LOGENGINE_INLINE std::string IntToStr(int Value)
{
	char buf[CONV_BUF];
	sprintf_s(buf, CONV_BUF, "%d", Value);
	return buf;
}

// function for convert uint Value to string
LOGENGINE_INLINE std::string IntToStr(uint Value)
{
	char buf[CONV_BUF];
	sprintf_s(buf, CONV_BUF, "%u", Value);
	return buf;
}

// function for convert ulong Value to string
LOGENGINE_INLINE std::string IntToStr(ulong Value)
{
	char buf[CONV_BUF];
	sprintf_s(buf, CONV_BUF, "%lu", Value);
	return buf;
}

// function for convert double Value to string
LOGENGINE_INLINE std::string FloatToStr(double Value)
{
	char buf[50];
	sprintf_s(buf, 50, "%f", Value);

	return buf;
}

#else //WIN32


// function for convert int Value to string
LOGENGINE_INLINE std::string IntToStr(int Value, int FieldSize)
{
	char buf[CONV_BUF];
	char buf2[CONV_BUF];
	sprintf(buf2, "%%-%dd", FieldSize);
	sprintf(buf, buf2, Value);

	return buf;
}

// function for convert uint Value to string
LOGENGINE_INLINE std::string IntToStr(uint Value, int FieldSize)
{
	char buf[CONV_BUF];
	char buf2[CONV_BUF];
	sprintf(buf2, "%%-%du", FieldSize);
	sprintf(buf, buf2, Value);

	return buf;
}

// function for convert int Value to string
LOGENGINE_INLINE std::string IntToStr(int Value)
{
	char buf[CONV_BUF];
	sprintf(buf, "%d", Value);
	return buf;
}

// function for convert uint Value to string
LOGENGINE_INLINE std::string IntToStr(uint Value)
{
	char buf[CONV_BUF];
	sprintf(buf, "%u", Value);
	return buf;
}

// function for convert ulong Value to string
LOGENGINE_INLINE std::string IntToStr(ulong Value)
{
	char buf[CONV_BUF];
	sprintf(buf, "%lu", Value);
	return buf;
}


// function for convert double Value to string
LOGENGINE_INLINE std::string FloatToStr(double Value)
{
	char buf[50];
	sprintf(buf, "%f", Value);

	return buf;
}


#endif //WIN32

// function for convert bool Value to string
LOGENGINE_INLINE std::string BoolToStr(bool Value)
{
	if(Value)
		return "1";
	else
		return "0";

}
LOGENGINE_INLINE bool StrToBool(const std::string& Value)
{
	return EqualNCase<std::string>(Value, "1") || EqualNCase<std::string>(Value, "yes") || EqualNCase<std::string>(Value, "true");
}

// extracts filename from path with filename
LOGENGINE_INLINE std::string ExtractFileName(const std::string& FileName)
{
	size_t i = FileName.length();
	if (i == 0) return "";

	do
	{
		i--;
		if ((FileName[i] == '\\') || (FileName[i] == '/'))
		{
			i++;
			break;
		}
	} while (i > 0);

	return FileName.substr(i);
}

// extracts file dir from path with filename
LOGENGINE_INLINE std::string ExtractFileDir(const std::string& FileName)
{
	size_t i = FileName.length();
	if (i == 0) return "";

	do
	{
		i--;
		if ((FileName[i] == '\\') || (FileName[i] == '/'))
		{
			i++;
			break;
		}
	} while (i > 0);

	return FileName.substr(0, i);
}

LOGENGINE_INLINE std::string ExtractFileExt(const std::string& FileName)
{
	size_t i = FileName.length();
	if (i == 0) return "";

	bool f = false;
	do
	{
		i--;
		if (FileName[i] == '.')
		{
			f = true; // we found a dot
			break;
		}

		if (FileName[i] == '\\' || FileName[i] == '/') // stop is folder delimiter found
			break;

	} while (i > 0);

	if (f)
		return FileName.substr(i);
	else
		return "";
}

LOGENGINE_INLINE std::string StripFileExt(const std::string& FileName)
{
	size_t i = FileName.length();
	if (i == 0) return "";

	bool f = false;
	do
	{
		i--;
		if (FileName[i] == '.')
		{
			f = true; // we found a dot
			break;
		}

		if (FileName[i] == '\\' || FileName[i] == '/') // stop is folder delimiter found
			break;

	} while (i > 0);

	if (f)
		return FileName.substr(0, i);
	else
		return FileName;
}

LOGENGINE_INLINE std::string StringReplace(const std::string& S, const std::string& SearchPattern, const std::string& ReplacePattern)
{
	if(SearchPattern.size() == 0 || S.size() == 0)
		return "";

	const char *temp1;
	char *temp2;
	std::string SearchStr(S);
	std::string Result;
	SearchStr[0] = S[0];

	temp1 = SearchStr.c_str();
	while(true)
	{
		temp2 = (char*)strstr(temp1, SearchPattern.c_str()); // explicit cast requred by C++ Builder 6
		if(temp2 == nullptr)
		{
			Result += temp1;
			break;
		}
		(*temp2) = '\0';
		Result += temp1;
		Result += ReplacePattern;
		temp1   = temp2 + SearchPattern.length();
	}
	return Result;
}

#define DATETIME_BUF 100

LOGENGINE_INLINE tm_point GetCurrTimePoint()
{
	return std::chrono::system_clock::now();
}

LOGENGINE_INLINE struct tm time_t_to_tm(const time_t t)
{
#ifdef WIN32
	struct tm ttm;
	localtime_s(&ttm, &t);
	return ttm;
#else
	return *localtime(&t);
#endif
}

LOGENGINE_INLINE struct tm GetCurrDateTime()
{
	const std::time_t tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	//struct tm t;
	//localtime_s(&t, &tt);
	return time_t_to_tm(tt);
}

// retrieves current time as std::string
LOGENGINE_INLINE std::string GetCurrTimeAsString(void)
{ 
	std::chrono::system_clock::time_point stime = GetCurrTimePoint();
	std::chrono::system_clock::time_point sstime = std::chrono::time_point_cast<std::chrono::seconds>(stime); // round to seconds
	long long millis = std::chrono::duration_cast<std::chrono::milliseconds>(stime - sstime).count();

	const std::time_t tt = std::chrono::system_clock::to_time_t(stime);

    char ss[DATETIME_BUF];
	struct tm t = time_t_to_tm(tt);
	std::strftime(ss, DATETIME_BUF, "%X", &t);

	char sss[20];
#ifdef WIN32       
	sprintf_s(sss, 20, ".%03lld", millis);
#else
	sprintf(sss, ".%03lld", millis);
#endif

	std::string s = DelCRLF(ss);
	return s + sss;
}

// retrieves current date as std::string
LOGENGINE_INLINE std::string GetCurrDateAsString(void)
{  	
	struct tm t = GetCurrDateTime();
    char ss[DATETIME_BUF];
    std::strftime(ss, DATETIME_BUF, "%d-%m-%Y", &t);

	//std::string s = DelCRLF(ss);
	return ss;
}

// retrieves current datetime as string
LOGENGINE_INLINE std::string GetCurrDateTimeAsString(void)
{
	struct tm t = GetCurrDateTime();
	char ss[DATETIME_BUF];
	std::strftime(ss, DATETIME_BUF, "%c", &t);

	return ss;
}

// converts native datetime value into string
LOGENGINE_INLINE std::string DateTimeToStr(const time_t t)
{
	struct tm ttm = time_t_to_tm(t);
	char ss[DATETIME_BUF];
	strftime(ss, DATETIME_BUF, "%F %T", &ttm);

	return ss;
}

// converts native datetime value into string
LOGENGINE_INLINE std::string DateTimeToStr(struct tm const& t)
{
	char ss[DATETIME_BUF];
	strftime(ss, DATETIME_BUF, "%F %T", &t);

	return ss;
}

// gets formatted current datetime
LOGENGINE_INLINE std::string FormatCurrDateTime(const std::string& FormatStr)
{   	
	struct tm t = GetCurrDateTime();
	char ss[DATETIME_BUF];
	std::strftime(ss, DATETIME_BUF, FormatStr.c_str(), &t);

	return ss;
}

LOGENGINE_INLINE std::string DelCRLF(std::string str) // str passed by value here intentionally
{
	// remove any leading and traling \n and \r, just in case.
	size_t strBegin = str.find_first_not_of("\r\n");
	if (strBegin == std::string::npos) return "";
	
	size_t strEnd = str.find_last_not_of("\r\n");
	assert(strEnd != std::string::npos);
	
	str.erase(strEnd + 1 /*, s.size() - strEnd*/);
	str.erase(0, strBegin);

	return str;

/*	int j = 0;
	std::string res;
	res.resize(S.length());
	for(uint i = 0; i < S.length(); i++)
	{
		if(S[i] != '\n' && S[i] != '\r')
			res[j++] = S[i];
		res[j] = '\0';
	}
	//res = res.erase(strlen(res.c_str()));
	return res; */
}

LOGENGINE_INLINE std::string TrimSPCRLF(std::string str) // str passed by value here intentionally
{
	// remove any leading and traling spaces, tabs and \n, \r.
	size_t strBegin = str.find_first_not_of(" \t\r\n");
	if (strBegin == std::string::npos) return "";

	size_t strEnd = str.find_last_not_of(" \t\r\n");
	assert(strEnd != std::string::npos);

	str.erase(strEnd + 1 /*, S.size() - strEnd*/); // erase till end of string
	str.erase(0, strBegin);
	
	return str;
}

LOGENGINE_INLINE std::string Trim(std::string str) // str passed by value here intentionally
{
	// remove any leading and traling spaces tabs.
	size_t strBegin = str.find_first_not_of(" \t");
	if (strBegin == std::string::npos) return "";
	
	size_t strEnd = str.find_last_not_of(" \t");
	assert(strEnd != std::string::npos);
	
	str.erase(strEnd + 1 /*, str.size() - strEnd*/);
	str.erase(0, strBegin);

	return str;
}

LOGENGINE_INLINE std::string TrimLeft(std::string str) // str passed by value here intentionally
{
	// remove any leading spaces and tabs
	size_t strBegin = str.find_first_not_of(" \t");
	str.erase(0, strBegin);

	return str;
}

LOGENGINE_INLINE std::string TrimRight(std::string str) // str passed by value here intentionally
{
	// remove any trailing spaces and tabs
	size_t strEnd = str.find_last_not_of(" \t");
	str.erase(strEnd + 1/*, str.size() - strEnd*/);

	return str;
}

/*
int CompareNCase(const std::string& str1, const std::string& str2)
{
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
		int upper1 = toupper(static_cast<uchar>(str1[i]));
		int upper2 = toupper(static_cast<uchar>(str2[i]));

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
} */

LOGENGINE_INLINE bool isUInt(std::string & value)
{
	if (value.size() == 0) return false;

	size_t start = 0;
	if (value[0] == '+') start = 1; // we need only positive numbers here

	return value.find_first_not_of("0123456789", start) == std::string::npos;
}

LOGENGINE_INLINE uint GetThreadID()
{
	std::stringstream ss;
	ss << std::this_thread::get_id();
	uint thrID;
	ss >> thrID;
	return thrID;
}

LOGENGINE_INLINE uint GetThreadID(const std::thread::id& id)
{
	std::stringstream ss;
	ss << id;
	uint thrID;
	ss >> thrID;
	return thrID;
}

static char mytolower(int c) // to eliminate compile warning "warning C4244: '=': conversion from 'int' to 'char', possible loss of data"
{
	return static_cast<char>(tolower(c));
}
LOGENGINE_INLINE std::string StrToLower(std::string str) // needs to be passed by value
{
	std::transform(str.begin(), str.end(), str.begin(), mytolower);
	return str;
}


