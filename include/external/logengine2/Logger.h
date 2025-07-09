/*
 * Logger.h
 *
 * Copyright 2025, LogEngine2 Project. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#pragma once

#if defined(WIN32) && _HAS_CXX20==1 && !defined(__BORLANDC__)
#include <format>
#endif

#include <stdarg.h>
#include <string>
#include <memory>
#include "Common.h"
#include "Sink.h"
#include "LogEvent.h"
#include "DynamicArrays.h"
#include "SynchronizedQueue.h"
#include "Properties.h"

LOGENGINE_NS_BEGIN

typedef SafeQueue<LogEvent*> LoggerQueue;
/*struct LoggerThreadInfo
{
	LoggerQueue* queue;
	Logger* logger;
};*/

typedef Sink* SinkPtr;
typedef THArray<std::shared_ptr<Sink>> SinkList;

class Logger
{
private:
	std::string FName;
	LoggerQueue FQueue;
	std::thread FThread;
	Levels::LogLevel FLogLevel;
	SinkList FSinks;
	bool FAsync = false;
	Properties FProperties;
	void InternalLog(const LogEvent& le) { SendToAllSinks(le); }

public:
	//TODO shall we add FQueue capacity size to constructor parameters?
	Logger(const std::string& name, Levels::LogLevel ll = LL_DEFAULT) : FName(name), FQueue(10), FLogLevel(ll) {}
	
	Logger(const std::string& name, std::initializer_list<std::shared_ptr<Sink>> list, Levels::LogLevel ll = LL_DEFAULT) : Logger(name, ll) //FName(name), FQueue(10), FLogLevel(ll) 
	{
		for (auto& item : list) AddSink(item);
	}

	virtual ~Logger()
	{
		SetAsyncMode(false); // send stop to async thread and wait till it finishes.  
		ClearSinks();
	}

	void ClearSinks()
	{
		//for (uint i = 0; i < FSinks.Count(); i++) delete FSinks[i].get();
		FSinks.Clear();
	}

	bool ShouldLog(const Levels::LogLevel ll) const { return FLogLevel >= ll; }

	Levels::LogLevel GetLogLevel() { return FLogLevel; }
	void SetLogLevel(const Levels::LogLevel ll, bool propagate = true)
	{ 
		FLogLevel = ll;
		if (propagate)
			for (auto& sk : FSinks) sk->SetLogLevel(ll);
	}
	
	std::string GetName() { return FName; }

	bool GetAsyncMode() { return FAsync; }
	void SetAsyncMode(bool amode)
	{
		if (FAsync == amode) return; // mode hasn't changed

		if (amode == true)
		{
			//LoggerThreadInfo* info = new LoggerThreadInfo;
			//info->queue = &FQueue;
			//info->logger = this;

			std::thread thr(ThreadProc, this, &FQueue);
			FThread.swap(thr);
			FAsync = amode;
		}
		else
		{
			FAsync = amode; // stop adding new log messages
			FQueue.PushElement(nullptr);
			FThread.join(); // waiting till thread finishes
		}
	}

	// Waits till async thread queue BECOMES EMPTY
	// Empty means that all log messages are successfully sent into log file and other destinations depending on logger sinks.
	// It DOES NOT wait till async thread finishes.
	void WaitEmptyQueue()
	{
		FQueue.WaitEmptyQueue();
	}

	// Waits till async thread FINISHES
	// Async thread is always running until it stopped by sending null message (FQueue.PushElement(nullptr)) or calling SetAsyncMode(false)
	// Be carefull calling WaitFor() on async thread since it may never return
	void WaitFor()
	{
		FThread.join();
	}

	// sets log line pattern for the specified by parameter 'll' log line
	void SetPattern(const std::string& pattern, Levels::LogLevel ll)
	{
		for (auto& sk: FSinks)
		{
			sk->SetPattern(pattern, ll);
		}
	}

	// sets log line pattern for all log lines
	void SetPattern(const std::string& pattern)
	{
		for (auto& sk : FSinks)
		{
			sk->SetPattern(pattern);
		}
	}

	// sets log line pattern for all log lines
	void SetDefaultPattern(Levels::LogLevel ll)
	{
		for (auto& sk : FSinks)
		{
			sk->SetDefaultPattern(ll);
		}
	}
	// sets log line pattern for all log lines
	void SetDefaultPattern()
	{
		for (auto& sk : FSinks)
		{
			sk->SetDefaultPattern();
		}
	}

	void SetProperty(const std::string& name, const std::string& value)
	{
		FProperties.SetValue(name, value);
	}

	std::string GetProperty(const std::string& name, const std::string& defaultValue = "")
	{
		std::string* pval = FProperties.GetValuePointer(name);
		if (pval)
			return *pval;
		else
			return defaultValue;
	}

	bool PropertyExist(const std::string& name)
	{
		return FProperties.IfExists(name);
	}

	// copy into Properties all logger parameters which are read from .lfg file 
	using Section = IniReader::StorageType::ValuesHash;
	virtual void FillProperties(Section& section)
	{
		for (auto it = section.begin(); it != section.end(); ++it)
		{
			Section::iterator::value_type item = *it;
			FProperties.SetValue(item.first, item.second[0]);
		}
	}

#if defined(WIN32) && _HAS_CXX20==1 && !defined(__BORLANDC__)
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

	template<class... Args>
	void LogFmt(Levels::LogLevel ll, const std::format_string<Args...> fmt, Args&&... args)
	{
		if (!ShouldLog(ll)) return;

		// TODO think how to pass all args into SendToAllSinks and create final string there
		if (FAsync)
		{
			LogEvent* event = new LogEvent(this, std::vformat(fmt.get(), std::make_format_args(args...)), ll, GetThreadID(), GetCurrDateTime());
			FQueue.PushElement(event);
		}
		else
		{
			LogEvent ev(this, std::vformat(fmt.get(), std::make_format_args(args...)), ll, GetThreadID(), GetCurrDateTime());
			InternalLog(ev);
		}
	}
#else
 private:
	std::string vformat(const char* format, va_list args)
	{
		size_t size = 1024; // should be enough for format string
		char* buffer = new char[size];

		while (1)
		{
			va_list args_copy;

#if defined(_MSC_VER) || defined(__BORLANDC__)
			args_copy = args;
#else
			va_copy(args_copy, args);
#endif

			int n = vsnprintf(buffer, size, format, args_copy);

			va_end(args_copy);

			// If that worked, return a string.
			if ((n > -1) && (static_cast<size_t>(n) < size))
			{
				std::string s(buffer);
				delete[] buffer;
				return s;
			}

			// Else try again with more space.
			size = (n > -1) ?
				static_cast<size_t>(n + 1) :   // ISO/IEC 9899:1999
				size * 2; // twice the old size

			delete[] buffer;
			buffer = new char[size];
		}
	}

public:	
	void CritFmt(const char* fmt, ...)
	{
		if (!ShouldLog(Levels::llCritical)) return;

		va_list va;
		va_start(va, fmt);
		LogFmt(Levels::llCritical, fmt, va);
		va_end(va);
	}

	void ErrorFmt(const char* fmt, ...)
	{
		if (!ShouldLog(Levels::llError)) return;

		va_list va;
		va_start(va, fmt);
		LogFmt(Levels::llError, fmt, va);
		va_end(va);
	}

	void WarnFmt(const char* fmt, ...)
	{
		if (!ShouldLog(Levels::llWarning)) return;

		va_list va;
		va_start(va, fmt);
		LogFmt(Levels::llWarning, fmt, va);
		va_end(va);
	}

	void InfoFmt(const char* fmt, ...)
	{
		if (!ShouldLog(Levels::llInfo)) return;

		va_list va;
		va_start(va, fmt);
		LogFmt(Levels::llInfo, fmt, va);
		va_end(va);
	}

	void DebugFmt(const char* fmt, ...)
	{ 
		if (!ShouldLog(Levels::llDebug)) return;

		va_list va;
		va_start(va, fmt);
		LogFmt(Levels::llDebug, fmt, va);
		va_end(va);
	}

	void TraceFmt(const char* fmt, ...)
	{
		if (!ShouldLog(Levels::llTrace)) return;

		va_list va;
		va_start(va, fmt);
		LogFmt(Levels::llTrace, fmt, va);
		va_end(va);
	}

	void LogFmt(Levels::LogLevel ll, const char* fmt,  ...)
	{
		if (!ShouldLog(ll)) return;
		
		va_list va;
		va_start(va, fmt);

		// TODO think how to pass all args into SendToAllSinks and create final string there
		if (FAsync)
		{
			LogEvent* event = new LogEvent(this, vformat(fmt, va), ll, GetThreadID(), GetCurrDateTime());
			FQueue.PushElement(event);
		}
		else
		{
			LogEvent ev(this, vformat(fmt, va), ll, GetThreadID(), GetCurrDateTime());
			InternalLog(ev);
		}
		//LogEvent ev(vformat(fmt, va), ll, GetThreadID(), GetCurrDateTime());
		va_end(va);
	}
#endif

	void Crit (const std::string& msg) { Log(msg, Levels::llCritical); }
	void Error(const std::string& msg) { Log(msg, Levels::llError);    }
	void Warn (const std::string& msg) { Log(msg, Levels::llWarning);  }
	void Info (const std::string& msg) { Log(msg, Levels::llInfo);     }
	void Debug(const std::string& msg) { Log(msg, Levels::llDebug);    }
	void Trace(const std::string& msg) { Log(msg, Levels::llTrace);    }

	void Log(const std::string& msg, const Levels::LogLevel ll)
	{
		if (!ShouldLog(ll)) return;

		if (FAsync)
		{
			LogEvent* event = new LogEvent(this, msg, ll, GetThreadID(), GetCurrDateTime());
			FQueue.PushElement(event);
		}
		else
		{
			LogEvent ev(this, msg, ll, GetThreadID(), GetCurrDateTime());
			InternalLog(ev);
		}
	}

	void SendToAllSinks(const LogEvent& le)
	{
		for (auto& si : FSinks)
		{
			si->PubSendMsg(le);
		}
	}

	void AddSink(const std::shared_ptr<Sink>& sink)
	{
		if (FSinks.IndexOf(sink) >= 0) return; // already added
		FSinks.AddValue(sink);
	}

	void RemoveSink(std::string& sinkName)
	{
		for (uint i = 0; i < FSinks.Count(); i++)
		{
			if (EqualNCase(FSinks[i]->GetName(), sinkName)) FSinks.DeleteValue(i);
		}
	}

	uint SinkCount()
	{
		return FSinks.Count();
	}

	// return by value (not by reference) is done intentionally here
	// to be able to return nullptr value when Sink not found
	std::shared_ptr<Sink> GetSink(const std::string& sinkName)
	{
		for(auto& si : FSinks)
		{
			if (EqualNCase(si->GetName(), sinkName)) return si;
		}

		//for (uint i = 0; i < sinks.Count(); i++)
		//{
		//	if (sinks[i]->GetName() == sinkName) return sinks[i];
		//}

		return nullptr;
	}

/*	static int ThreadProc(LoggerThreadInfo* parameter)
	{
		LoggerThreadInfo* info = parameter; //reinterpret_cast<LoggerThreadInfo*>(parameter);

		LogEvent* current_msg;
		do
		{
			current_msg = info->queue->WaitForElement();
			if (current_msg)
			{
				LogEvent* event = current_msg;
				info->logger->InternalLog(*event);
				delete event;
			}

		} while (current_msg); // null as msg means that we need to stop this thread

		delete info;

		return 0;
	}
	*/
	static int ThreadProc(Logger* logger, LoggerQueue* queue)
	{
		LogEvent* current_msg;
		do
		{
			current_msg = queue->WaitForElement();
			if (current_msg)
			{
				LogEvent* event = current_msg;
				logger->InternalLog(*event);
				delete event;
			}

		} while (current_msg); // null as msg means that we need to stop this thread

		return 0;
	}

};

LOGENGINE_NS_END


