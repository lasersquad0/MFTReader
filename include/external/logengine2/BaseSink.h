/*
 * BaseSink.h
 *
 * Copyright 2025, LogEngine2 Project. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#pragma once

#include <string>
#include "Sink.h"
#include "PatternLayout.h"

LOGENGINE_NS_BEGIN

//special struct that emulates mutex but does noting, for single threaded Sinks
struct NullMutex
{
	void lock() {}
	void unlock() {}
};

template<class Mutex>
class BaseSink : public Sink
{
protected:
	//std::string FName;
	//Levels::LogLevel FLogLevel{ LL_DEFAULT };
	Mutex FMutex;
	Layout* FLayout{ nullptr };
	ullong FMessageCounts[static_cast<int>(Levels::n_LogLevels)]{ 0,0,0,0,0,0 };
	ullong FBytesWritten = 0; //TODO may be move to Sink class?

	//bool shouldLog(const Levels::LogLevel ll) const { return FLogLevel >= ll; }
	//virtual void sendMsg(const LogEvent& e) = 0;

public:
	BaseSink(const std::string& name, const Levels::LogLevel ll = LL_DEFAULT) : Sink(name, ll), FLayout{ new PatternLayout() }
	{
		//for (uint i = 0; i < n_LogLevels; i++)
		//	FMessageCounts[i] = 0;
	}

	//TODO add here constructor with PatternLayout parameter. Decide who should delete PatternLayout in this case.

	~BaseSink() override
	{
		delete FLayout;
		FLayout = nullptr;
	}

	virtual void Flush() { /* does nothing here*/ }

	void PubSendMsg(const LogEvent& e) override
	{
		std::lock_guard<Mutex> lock(FMutex);

		if (!shouldLog(e.msgLevel)) return;

		sendMsg(e);
	}

	std::string FormatString(const LogEvent& e) override
	{
		std::string str = FLayout->Format(e, FProperties);
		FMessageCounts[e.msgLevel]++;

		return str;
	}

	Layout* GetLayout() const override { return FLayout; }
	void SetLayout(Layout* layout) override
	{
		std::lock_guard<Mutex> lock(FMutex);

		if (layout == nullptr) return; // if layout is null do nothing
		if (FLayout != nullptr) delete FLayout;
		FLayout = layout;
	}

	// sets log line pattern for the specified by parameter 'll' log line
	void SetPattern(const std::string& pattern, Levels::LogLevel ll) override
	{
		std::lock_guard<Mutex> lock(FMutex);
		FLayout->SetPattern(pattern, ll);
	}

	// sets log line pattern for all log lines
	void SetPattern(const std::string& pattern) override
	{
		std::lock_guard<Mutex> lock(FMutex);
		FLayout->SetPattern(pattern);
	}

	// sets log line pattern for the specified by parameter 'll' log line
	void SetDefaultPattern(Levels::LogLevel ll) override
	{
		std::lock_guard<Mutex> lock(FMutex);
		FLayout->SetDefaultPattern(ll);
	}

	// sets log line pattern for all log lines
	void SetDefaultPattern() override
	{
		std::lock_guard<Mutex> lock(FMutex);
		FLayout->SetDefaultPattern();
	}

	/**
	* Returns bytes written since creating the sink.
	* @returns bytes written as value of size_t type.
	**/
	ullong GetBytesWritten() const override
	{
		return FBytesWritten;
	}
	/**
	* Returns message statistic. Message statistic it is number of written messages per log level.
	* @returns array of size_t valuesm one value for each log level.
	**/
	ullong* GetMessageCounts() override
	{
		return FMessageCounts;
	}

	/*Levels::LogLevel GetLogLevel() const { return FLogLevel; }
	void SetLogLevel(Levels::LogLevel ll)
	{
		std::lock_guard<Mutex> lock(FMutex);
		FLogLevel = ll;
	}

	/**
	 * Get the name of this sink. The name identifies the sink.
	 * @returns the name of the sink.
	 **/
	 //const std::string& GetName() const { return FName; }

};


LOGENGINE_NS_END
