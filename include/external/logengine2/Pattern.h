/*
 * Pattern.h
 *
 * Copyright 2025, LogEngine2 Project. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#pragma once

#include <cassert>
#include <vector>
#include <sstream>
#include "DynamicArrays.h"
#include "LogEvent.h"
#include "Logger.h"
#include "Properties.h"

#ifdef WIN32
#include <windows.h>
#endif


LOGENGINE_NS_BEGIN

#define APPNAME_PROPERTY  "AppName"
#define APPVERSION_PROPERTY "AppVersion"

#define LoggerNameMacro   "%LOGGERNAME%"
#define LogLevelMacro	  "%LOGLEVEL%"
#define MessageMacro      "%MSG%"
#define DateMacro         "%DATE%"
#define TimeMacro         "%TIME%"
#define DateTimeMacro     "%DATETIME%"
#define ThreadMacro		  "%THREAD%"
#define AppNameMacro      "%APPNAME%"
#define AppVersionMacro   "%APPVERSION%"
#define OSMacro		      "%OS%"
#define OSVersionMacro	  "%OSVERSION%"


static std::string GetPropertyValue(Properties* props, Logger* logger, const std::string& paramName, const std::string& defValue)
{
	std::string* valuePtr = nullptr;

	if (props != nullptr)
		valuePtr = props->GetValuePointer(paramName);

	if (valuePtr == nullptr) // if parameter is not found in props then look in logger properties
		if (logger == nullptr)
			return defValue;
		else
			return logger->GetProperty(paramName, defValue);
	else
		return (*valuePtr);
}

class Holder
{
public:
	virtual std::string Format(const LogEvent& event, Properties& props) = 0;
	virtual ~Holder() {}
};

#define SFT_SIZE 100

class LoggerNameHolder : public Holder
{
public:
	std::string Format(const LogEvent& event, Properties&) override { return event.logger->GetName(); }
};

class LogLevelHolder : public Holder
{
public:
	std::string Format(const LogEvent& event, Properties&) override { return LLtoCapsString(event.msgLevel); }
};

class DateHolder : public Holder
{
public:
	std::string Format(const LogEvent& event, Properties& props) override
	{
		//std::string format;
		//auto formatPtr = props.GetValuePointer(DateMacro);
		//format = (formatPtr == nullptr) ? "%x"/*"%d-%b-%Y"*/ : (*formatPtr);

		auto format = GetPropertyValue(&props, event.logger, DateMacro, "%x");

		char datestr[SFT_SIZE];
		size_t res = std::strftime(datestr, SFT_SIZE, format.c_str(), &event.tmtime);
		
		if (res == 0) // error during formatting, use default system formatting
			std::strftime(datestr, SFT_SIZE, "%x", &event.tmtime);

		return datestr;
	}
};

class TimeHolder : public Holder
{
public:
	std::string Format(const LogEvent& event, Properties& props) override
	{
		//std::string format;
		//auto formatPtr = props.GetValuePointer(TimeMacro);
		//format = (formatPtr == nullptr) ? "%X"/*"%d-%b-%Y"*/ : (*formatPtr);
		
		auto format = GetPropertyValue(&props, event.logger, TimeMacro, "%X");

		char timestr[SFT_SIZE];
		size_t res = std::strftime(timestr, SFT_SIZE, format.c_str(), &event.tmtime);
		
		if (res == 0) // error during formatting, use default system formatting
			std::strftime(timestr, SFT_SIZE, "%X", &event.tmtime);

		return timestr;
	}
};

class DateTimeHolder : public Holder
{
public:
	std::string Format(const LogEvent& event, Properties& props) override
	{
		//std::string format;
		//auto formatPtr = props.GetValuePointer(DateTimeMacro);
		//format = (formatPtr == nullptr) ? "%x %X"/*"%d-%b-%Y %X"*/ : (*formatPtr);

		auto format = GetPropertyValue(&props, event.logger, DateTimeMacro, "%x %X");

		char dtstr[SFT_SIZE];
		size_t res = std::strftime(dtstr, SFT_SIZE, format.c_str(), &event.tmtime);

		if (res == 0) // error during formatting, use default system formatting
			std::strftime(dtstr, SFT_SIZE, "%x %X", &event.tmtime);

		return dtstr;
	}
};

class MessageHolder : public Holder
{
public:
	std::string Format(const LogEvent& event, Properties&) override { return event.message; }
};

class ThreadHolder : public Holder
{
public:
	std::string Format(const LogEvent& event, Properties&) override { return IntToStr(event.threadID); }
};

class PropertyHolder : public Holder
{
private:
	std::string FPropName;
	std::string FDefValue;
public:
	PropertyHolder(const std::string& propName, const std::string& defValue) { FPropName = propName; FDefValue = defValue; }

	std::string Format(const LogEvent& event, Properties& props) override 
	{
		return GetPropertyValue(&props, event.logger, FPropName, FDefValue);

/*		auto valuePtr = props.GetValuePointer(FPropName);
		if (valuePtr == nullptr) // if parameter is not found in props then look in logger properties
			if (event.logger == nullptr) 
				return FDefValue;
			else
				return event.logger->GetProperty(FPropName, FDefValue);
		else
			return (*valuePtr);*/
	}
};

/*
class AppNameHolder : public Holder
{
private:
	std::string FAppName;
public:
	AppNameHolder(const std::string& aname) { FAppName = aname; }
	std::string Format(const LogEvent&, Properties&) override { return FAppName; }
};

class AppVersionHolder : public Holder
{
private:
	std::string version;
public:
	AppVersionHolder(const std::string& ver) { this->version = ver; }

	std::string Format(const LogEvent&, Properties&) override { return version; }
};
*/
class LiteralHolder : public Holder
{
private:
	std::string FValue;
public:
	LiteralHolder(const std::string& value) { FValue = value; }
	std::string Format(const LogEvent&, Properties&) override { return FValue; }
};

class OSHolder : public Holder
{
public:
	std::string Format(const LogEvent&, Properties&) override
	{
#ifdef WIN32
		DWORD OSMajorVer = 6, OSMinorVer = 3, SPMajorVer = 0, SPMinorVer = 0;
		DWORD result;

		if (0 == GetProductInfo(OSMajorVer, OSMinorVer, SPMajorVer, SPMinorVer, &result))
			return "Error getting OS info";
		else
			return IntToStr(OSMajorVer) + "." + IntToStr(OSMinorVer) + " SP " + IntToStr(SPMajorVer) + "." + IntToStr(SPMinorVer) + " " + IntToStr(result);
#else
		return "OSHolder";
#endif	
	}
};

class OSVersionHolder : public Holder
{
private:
	std::string FVersionCache;
public:
	std::string Format(const LogEvent&, Properties&) override
	{
		//return cached value if it is exists.
		if (FVersionCache.size() > 0) return FVersionCache;

#ifdef WIN32
		const auto system = L"kernel32.dll";
		DWORD dummy = 0;
		const auto cbInfo = ::GetFileVersionInfoSizeExW(FILE_VER_GET_NEUTRAL, system, &dummy);
		std::vector<char> buffer(cbInfo);
		GetFileVersionInfoExW(FILE_VER_GET_NEUTRAL, system, dummy, static_cast<DWORD>(buffer.size()), &buffer[0]);
		void* p = nullptr;
		UINT size = 0;
		::VerQueryValueW(buffer.data(), L"\\", &p, &size);
		assert(size >= sizeof(VS_FIXEDFILEINFO));
		assert(p != nullptr);
		auto pFixed = static_cast<const VS_FIXEDFILEINFO*>(p);

		std::ostringstream o;
		o << HIWORD(pFixed->dwFileVersionMS) << '.'
			<< LOWORD(pFixed->dwFileVersionMS) << '.'
			<< HIWORD(pFixed->dwFileVersionLS) << '.'
			<< LOWORD(pFixed->dwFileVersionLS);

		FVersionCache = o.str();
		return FVersionCache;
#else
		FVersionCache = "<OSVERSION>";
		return FVersionCache;
#endif
	}
};


#define SecondMacro       "%SEC%"
#define MinuteMacro       "%MIN%"
#define HourMacro         "%HOUR%"
#define DayMacro          "%DAY%"
#define MonthMacro        "%MONTH%"
#define YearMacro         "%YEAR%"

#define ExceptionMacro    "%E%"
#define ClassNameMacro	  "%NAME%"
#define ClassClassMacro	  "%CLASS%"

#define DefaultAppName        "nonameapp"
#define DefaultAppVersion     "0.0.0.0"
#define DefaultLinePattern    " " DateTimeMacro " #" ThreadMacro ": " MessageMacro
#define DefaultCritPattern    "*!*" DefaultLinePattern
#define DefaultErrorPattern   "E!" DefaultLinePattern
#define DefaultWarningPattern "W#" DefaultLinePattern
#define DefaultInfoPattern    "I" DefaultLinePattern
#define DefaultDebugPattern   "D" DefaultLinePattern
#define DefaultTracePattern   "T" DefaultLinePattern
#define DefaultStartAppLine   "\n%APPNAME% %APPVERSION% startup\nLog is started at %DATETIME%."
#define DefaultStopAppLine    "%APPNAME% %APPVERSION% normal shutdown \nLog stopped at %DATETIME%.\n"
#define DefaultSeparatorLine  "----------------------------------------------------------------"


class Pattern
{
protected:
	inline static const THash<std::string, Holder*, CompareStringNCase> FDefHolders
	{ {LoggerNameMacro, new LoggerNameHolder()}, {LogLevelMacro, new LogLevelHolder()}, {MessageMacro, new MessageHolder()},
	  {DateMacro, new DateHolder()}, {TimeMacro, new TimeHolder()}, {DateTimeMacro, new DateTimeHolder()},
	  {ThreadMacro, new ThreadHolder()}, {OSMacro, new OSHolder()}, {OSVersionMacro, new OSVersionHolder()},
	  {AppNameMacro, new PropertyHolder(APPNAME_PROPERTY, DefaultAppName)},
	  {AppVersionMacro, new PropertyHolder(APPVERSION_PROPERTY, DefaultAppVersion)},
	};

	THArray<Holder*> FHolders; // container of pointers is required here to support proper virtual "->Format()" calls 
	std::string FPattern;
	void parsePattern(const std::string& pattern); // compiles string pattern into list of appropriate Holder classes
	//void parsePattern2(const std::string& pattern); // compiles string pattern into list of appropriate Holder classes
	void clearHolders();
public:
	Pattern(const std::string& pattern) { parsePattern(pattern); }
	virtual ~Pattern() { clearHolders(); }
	virtual std::string Format(const LogEvent& event);
	virtual std::string Format(const LogEvent& event, Properties& props);
	void SetPattern(const std::string& pattern) { parsePattern(pattern); }
	std::string GetPattern() { return FPattern; }
};

LOGENGINE_NS_END

#ifdef LOGENGINE_HEADER_ONLY
#include "Pattern-hdr.h"
#endif
