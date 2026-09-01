/*
 * LogEvent.h
 *
 * Copyright 2025, LogEngine2 Project. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#if !defined(LOGEVENT_H)
#define LOGEVENT_H

//#include <string_view>
#include <sys/timeb.h>
#include <algorithm>
#include <ctime>
//#include "Common.h"
#include "Exceptions.h"


LOGENGINE_NS_BEGIN

enum LogLevel : uint { llOff, llCritical, llError, llWarning, llInfo, llDebug, llTrace, llnLogLevels };

#define LL_DEFAULT llInfo
#define LL_DEFAULT_NAME "llInfo"

#define LL_NAMES       { "off", "critical", "error", "warning", "info", "debug", "trace" }
#define LL_CAPS_NAMES  { "OFF", "CRITICAL", "ERROR", "WARNING", "INFO", "DEBUG", "TRACE" }
#define LL_SHORT_NAMES { "O", "C", "E", "W", "I", "D", "T" }

static const std::string LogLevelNames[] LL_NAMES;
static const std::string LogLevelCapsNames[] LL_CAPS_NAMES;
static const char* LogLevelShortNames[] LL_SHORT_NAMES;

inline const std::string& LLtoString(const LogLevel ll) { return LogLevelNames[ll]; }
inline const std::string& LLtoCapsString(const LogLevel ll) { return LogLevelCapsNames[ll]; }

inline const char* LLtoShortString(const LogLevel ll) { return LogLevelShortNames[ll]; }


inline LogLevel LLfromString(std::string name)
{
	//std::transform(name.begin(), name.end(), name.begin(), mytolower);

	name = StrToLower(name);
	auto it = std::find(std::begin(LogLevelNames), std::end(LogLevelNames), name);
	if (it != std::end(LogLevelNames))
		return static_cast<LogLevel>(std::distance(std::begin(LogLevelNames), it));

	// check also for "warn" and "err" before giving up..
	if (name == "warn") return llWarning;
	if (name == "err") return llError;

	return LL_DEFAULT;
}

enum LogSinkType { stStdout, stStderr, stFile, stRotatingFile, stString, stCallback, n_SinkTypes };

#define ST_NAMES { "stdout", "stderr", "file", "rotatingfile", "string", "callback" }
#define ST_DEFAULT stStdout
#define ST_DEFAULT_NAME "Stdout"

static const std::string SinkTypeNames[] ST_NAMES;

inline LogSinkType STfromString(std::string name) // parameter needs to be passed by value
{
	//std::transform(name.begin(), name.end(), name.begin(), name);

	auto it = std::find(std::begin(SinkTypeNames), std::end(SinkTypeNames), StrToLower(name));
	if (it != std::end(SinkTypeNames))
		return static_cast<LogSinkType>(std::distance(std::begin(SinkTypeNames), it));

	throw LogException(std::string("Sink Type '") + name + "' is not valid sink type name.");
	//return ST_DEFAULT;
}

enum LogSinkThreaded { sthSingle, sthMulti, n_SinkThreaded };

#define STH_NAMES { "single", "multi" }
#define STH_DEFAULT sthSingle
#define STH_DEFAULT_NAME "single"

static const std::string SinkThreadedNames[] STH_NAMES;

inline LogSinkThreaded STHfromString(std::string name) // parameter needs to be passed by value
{
	auto it = std::find(std::begin(SinkThreadedNames), std::end(SinkThreadedNames), StrToLower(name));
	if (it != std::end(SinkThreadedNames))
		return static_cast<LogSinkThreaded>(std::distance(std::begin(SinkThreadedNames), it));

	return STH_DEFAULT;
}

class Logger;

class LogEvent
{
public:
	Logger* logger; // pointer to logger instance that generated this log event
	struct tm tmtime;
	std::string message;
	LogLevel msgLevel;
	unsigned int threadID; // this is a thread that generated a log message. it may differ from thread that makes actual writing to the file (when Logger's AsyncMode set to true)

	LogEvent(Logger* lgr, const std::string& msg, LogLevel msgLv, unsigned int thrID, struct tm time)
		: logger(lgr), tmtime(time), message(msg),msgLevel(msgLv), threadID(thrID) {}

};

LOGENGINE_NS_END

#endif //LOGEVENT_H
