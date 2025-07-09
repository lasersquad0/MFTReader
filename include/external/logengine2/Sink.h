/*
 * Sink.h
 *
 * Copyright 2025, LogEngine2 Project. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#pragma once

#include <string>
#include "Common.h"
#include "LogEvent.h"
#include "Layout.h"
#include "Properties.h"
#include "IniReader.h"

LOGENGINE_NS_BEGIN

class Sink
{
protected:
	std::string FName;
	std::atomic<Levels::LogLevel> FLogLevel;
	Properties FProperties;

	virtual bool shouldLog(const Levels::LogLevel ll) { return FLogLevel.load() >= ll; }
	virtual void sendMsg(const LogEvent& e) = 0;

public:
	Sink(const std::string& name, const Levels::LogLevel ll = LL_DEFAULT) : FName{ name }
	{
		FLogLevel.store(ll);

		//for (uint i = 0; i < n_LogLevels; i++)
		//	FMessageCounts[i] = 0;
	}

	virtual ~Sink() = default;

	virtual void Flush() { /* does nothing here*/ }
	
	virtual void PubSendMsg(const LogEvent& e) = 0;

	virtual Layout* GetLayout() const = 0;
	virtual void SetLayout(Layout* layout) = 0;

	/**
	* Sets log line pattern for the specified log level
	**/
	virtual void SetPattern(const std::string& pattern, Levels::LogLevel ll) = 0;

	/**
	* Sets one log line pattern for all log levels
	**/
	virtual void SetPattern(const std::string& pattern) = 0;

	/**
	* Preforms log line formatting according to appropriate log line pattern and data from LogEvent
	* @returns formatted log line as string
	**/
	virtual std::string FormatString(const LogEvent& e) = 0;

	/**
	* Sets default log line pattern for the specified log level
	**/
	virtual void SetDefaultPattern(Levels::LogLevel ll) = 0;

	// sets log line pattern for all log lines
	virtual void SetDefaultPattern() = 0;

	/**
	* Returns bytes written since creating the sink.
	* @returns bytes written as value of size_t type.
	**/
	virtual ullong GetBytesWritten() const = 0;

	/**
	* Returns message statistic. Message statistic it is number of written messages per log level.
	* @returns array of size_t valuesm one value for each log level.
	**/
	virtual ullong* GetMessageCounts() = 0;
	
	Levels::LogLevel GetLogLevel() const 
	{ 
		return FLogLevel.load(); 
	}
	
	void SetLogLevel(Levels::LogLevel ll) 
	{
		FLogLevel.store(ll); 
	}

	/**
	 * Get the name of this sink. The name identifies the sink.
	 * @returns the name of the sink.
	 **/
	const std::string& GetName() const { return FName; }

	// copy all sink parameters read from .lfg file into Properties
	using Section = IniReader::StorageType::ValuesHash;
	virtual void FillProperties(Section& section)
	{
		for (auto it = section.begin(); it != section.end(); ++it)
		{
			Section::iterator::value_type item = *it;
			FProperties.SetValue(item.first, item.second[0]);
		}
	}
};


LOGENGINE_NS_END
