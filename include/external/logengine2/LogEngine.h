/*
 * LogEngine.h
 *
 * Copyright 2025, LogEngine2 Project. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#ifndef LOG_ENGINE_H
#define LOG_ENGINE_H

#include <string>
//#include <functional>

// all includes below need for header only mode
#include "DynamicArrays.h"
#include "Common.h"
#include "Compare.h"
#include "LogEvent.h"
#include "Properties.h"
#include "Logger.h"
#include "RotatingFileSink.h"
#include "IniReader.h"
#include "version.h"

LOGENGINE_NS_BEGIN

typedef FileSink<std::mutex> FileSinkMT;
typedef FileSink<NullMutex> FileSinkST;
typedef FileLockSink<std::mutex> FileLockSinkMT;
typedef FileLockSink<NullMutex> FileLockSinkST;
typedef StdoutSink<std::mutex> StdoutSinkMT;
typedef StdoutSink<NullMutex> StdoutSinkST;
typedef StderrSink<std::mutex> StderrSinkMT;
typedef StderrSink<NullMutex> StderrSinkST;
typedef StringSink<std::mutex> StringSinkMT;
typedef StringSink<NullMutex> StringSinkST;
typedef RotatingFileSink<std::mutex> RotatingFileSinkMT;
typedef RotatingFileSink<NullMutex> RotatingFileSinkST;
typedef CallbackSink<std::mutex> CallbackSinkMT;
typedef CallbackSink<NullMutex> CallbackSinkST;

#ifdef WIN32
typedef OutputDebugSink<std::mutex> OutputDebugSinkMT;
typedef OutputDebugSink<NullMutex> OutputDebugSinkST;
#endif

void SetProperty(const std::string& name, const std::string& value);
std::string GetProperty(const std::string& name, const std::string& defaultValue /*= ""*/);
bool PropertyExist(const std::string& name);

void ShutdownLoggers();
uint LoggersCount();
bool LoggerExist(const std::string& loggerName);

Logger& GetDefaultLogger();

#define GetFileLogger GetFileLoggerST
#define GetStdoutLogger GetStdoutLoggerST
#define GetStderrLogger GetStderrLoggerST
#define GetCallbackLogger GetCallbackLoggerST
#define GetRotatingFileLogger GetRotatingFileLoggerST

Logger& GetLogger(const std::string& loggerName);
Logger& GetFileLoggerST(const std::string& loggerName, const std::string& fileName);
Logger& GetFileLoggerMT(const std::string& loggerName, const std::string& fileName);
Logger& GetStdoutLoggerST(const std::string& loggerName);
Logger& GetStdoutLoggerMT(const std::string& loggerName);
Logger& GetStderrLoggerST(const std::string& loggerName);
Logger& GetStderrLoggerMT(const std::string& loggerName);
Logger& GetMultiLogger(const std::string& loggerName, SinkList& sinks);
Logger& GetMultiLogger(const std::string& loggerName, std::initializer_list<std::shared_ptr<Sink>> list);
Logger& GetCallbackLoggerST(const std::string& loggerName, const CustomLogCallback& callback);
Logger& GetCallbackLoggerMT(const std::string& loggerName, const CustomLogCallback& callback);
Logger& GetRotatingFileLoggerST(const std::string& loggerName, const std::string& fileName, ullong maxLogSize = DefaultMaxLogSize,
						LogRotatingStrategy strategy = DefaultRotatingStrategy, uint maxBackupIndex = DefaultMaxBackupIndex);
Logger& GetRotatingFileLoggerMT(const std::string& loggerName, const std::string& fileName, ullong maxLogSize = DefaultMaxLogSize,
	LogRotatingStrategy strategy = DefaultRotatingStrategy, uint maxBackupIndex = DefaultMaxBackupIndex);

#ifdef WIN32
Logger& GetODebugLoggerST(const std::string& loggerName);
Logger& GetODebugLoggerMT(const std::string& loggerName);
#endif

// default logger functions
void Log(const std::string& msg, const Levels::LogLevel ll);

void Crit(const std::string& msg);
void Error(const std::string& msg);
void Warn(const std::string& msg);
void Info(const std::string& msg);
void Debug(const std::string& msg);
void Trace(const std::string& msg);

#if defined(WIN32) && _HAS_CXX20==1 && !defined(__BORLANDC__)
template<class... Args>
void LogFmt(Levels::LogLevel ll, const std::format_string<Args...> fmt, Args&&... args)
{
	GetDefaultLogger().LogFmt(ll, fmt, std::forward<Args>(args)...);
}

template<class ... Args>
void CritFmt(const std::format_string<Args...> fmt, Args&& ...args)
{
	LogFmt(Levels::llCritical, fmt, std::forward<Args>(args)...);
}

template<class ... Args>
void ErrorFmt(const std::format_string<Args...> fmt, Args&& ...args)
{
	LogFmt(Levels::llError, fmt, std::forward<Args>(args)...);
}

template<class ... Args>
void WarnFmt(const std::format_string<Args...> fmt, Args&& ...args)
{
	LogFmt(Levels::llWarning, fmt, std::forward<Args>(args)...);
}

template<class ... Args>
void InfoFmt(const std::format_string<Args...> fmt, Args&& ...args)
{
	LogFmt(Levels::llInfo, fmt, std::forward<Args>(args)...);
}

template<class ... Args>
void DebugFmt(const std::format_string<Args...> fmt, Args&& ...args)
{
	LogFmt(Levels::llDebug, fmt, std::forward<Args>(args)...);
}

template<class ... Args>
void TraceFmt(const std::format_string<Args...> fmt, Args&& ...args)
{
	LogFmt(Levels::llTrace, fmt, std::forward<Args>(args)...);
}

#else

LOGENGINE_INLINE void LogFmt(Levels::LogLevel ll, const char* fmt,  ...)
{
	va_list va;
	va_start(va, fmt);
	GetDefaultLogger().LogFmt(ll, fmt, va);
	va_end(va);
}

LOGENGINE_INLINE void CritFmt(const char* fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	LogFmt(Levels::llCritical, fmt, va);
	va_end(va);
}

LOGENGINE_INLINE void WarnFmt(const char* fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	LogFmt(Levels::llWarning, fmt, va);
	va_end(va);
}

LOGENGINE_INLINE void InfoFmt(const char* fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	LogFmt(Levels::llInfo, fmt, va);
	va_end(va);
}

LOGENGINE_INLINE void DebugFmt(const char* fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	LogFmt(Levels::llDebug, fmt, va);
	va_end(va);
}

LOGENGINE_INLINE void TraceFmt(const char* fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	LogFmt(Levels::llTrace, fmt, va);
	va_end(va);
}

#endif


// parameters for loggers
#define LOGGER_PREFIX  "logger"
#define SINK_PREFIX	   "sink."
#define LOGLEVEL_PARAM "loglevel"
#define ASYNMODE_PARAM "asyncmode"
#define SINK_PARAM     "sink"

//parameters for Sinks (FileSink, RotatingFileSink, StdoutSink, StderrSink, StringSink, CallbackSink)
#define TYPE_PARAM         "type"
#define THREAD_PARAM       "threadsafety"
#define FILENAME_PARAM     "filename"
#define MAXLOGSIZE_PARAM   "maxlogfilesize"
#define MAXBACKUPINDEX_PARAM "maxbackupindex"
#define STRATEGY_PARAM     "strategy"
#define PATTERNALL_PARAM   "patternall"
#define CRITPATTERN_PARAM  "critpattern"
#define ERRORPATTERN_PARAM "errorpattern"
#define WARNPATTERN_PARAM  "warnpattern"
#define INFOPATTERN_PARAM  "infopattern"
#define DEBUGPATTERN_PARAM "debugpattern"
#define TRACEPATTERN_PARAM "tracepattern"

void InitFromFile(const std::string& fileName);

LOGENGINE_NS_END

#ifdef LOGENGINE_HEADER_ONLY
#include "LogEngine-hdr.h"
#endif

#endif //LOG_ENGINE_H

