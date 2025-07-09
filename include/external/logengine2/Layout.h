/*
 * Logger.h
 *
 * Copyright 2024, LogEngine Project. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#pragma once

#include <string>
#include <sstream>
#include "LogEvent.h"
#include "Common.h"
#include "Properties.h"

LOGENGINE_NS_BEGIN


class Layout
{
public:
	virtual ~Layout() { }
	virtual std::string Format(const LogEvent& event, Properties& props) = 0;

	virtual void SetPattern(const std::string&, Levels::LogLevel) = 0;
	virtual void SetPattern(const std::string&) = 0;
	
	virtual std::string GetPattern(Levels::LogLevel level) = 0;
	virtual std::string GetPattern() = 0;

	virtual void SetCritPattern (const std::string& pattern) { SetPattern(pattern, Levels::llCritical); }
	virtual void SetErrorPattern(const std::string& pattern) { SetPattern(pattern, Levels::llError);    }
	virtual void SetWarnPattern (const std::string& pattern) { SetPattern(pattern, Levels::llWarning);  }
	virtual void SetInfoPattern (const std::string& pattern) { SetPattern(pattern, Levels::llInfo);     }
	virtual void SetDebugPattern(const std::string& pattern) { SetPattern(pattern, Levels::llDebug);    }
	virtual void SetTracePattern(const std::string& pattern) { SetPattern(pattern, Levels::llTrace);    }

	virtual void SetDefaultPattern(Levels::LogLevel level) = 0;
	virtual void SetDefaultPattern() = 0;

};

/*
class FixedLayout : public Layout
{
public:
	std::string Format(const LogEvent& lv) override
	{
		std::string msgTypeName = LLtoString(lv.msgLevel);

		std::ostringstream o;
		o << DateTimeToStr(lv.tmtime) << " [" << msgTypeName << "] " << lv.message;

		return o.str();
	}
};
*/

LOGENGINE_NS_END
