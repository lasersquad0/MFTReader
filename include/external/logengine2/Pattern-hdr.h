/*
 * Pattern.cpp
 *
 * Copyright 2025, LogEngine2 Project. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#include <algorithm>
#include "Pattern.h"
#include "LogEvent.h"
#include "Properties.h"

LOGENGINE_NS_BEGIN

LOGENGINE_INLINE std::string Pattern::Format(const LogEvent& event)
{
	std::string result;
	Properties props; // empty class just as mock

	for (uint i = 0; i < FHolders.Count(); i++)
	{
		result.append(FHolders[i]->Format(event, props));
	}

	return result;
}

LOGENGINE_INLINE std::string Pattern::Format(const LogEvent& event, Properties& props)
{
	std::string result;

	for (uint i = 0; i < FHolders.Count(); i++)
	{
		result.append(FHolders[i]->Format(event, props));
	}

	return result;
}

// forward declaration of one global function from LogEngine.h instead of including entire LogEngine.h
std::string GetProperty(const std::string& name, const std::string& defaultValue = "");

#define MACRO_SEP '%'
#define MACRO_SEP_STR "%"

/*
LOGENGINE_INLINE void Pattern::parsePattern2(const std::string& pattern)
{
	static const THArray<std::string> placeHolders = { "", DateMacro, TimeMacro, DateTimeMacro, MessageMacro, ThreadMacro, AppNameMacro, AppVersionMacro, OSMacro, OSVersionMacro, LogLevelMacro };

	FPattern = pattern;

	clearHolders();

	uint i = 0;
	while (i < pattern.size())
	{
		if (pattern[i] == MACRO_SEP)
		{
			std::string macro = MACRO_SEP_STR;
			i++;

			while ((i < pattern.size()) && (pattern[i] != MACRO_SEP))
				macro += pattern[i++];

			if (i < pattern.size()) // do not forget to add second % to placeholder
				macro += pattern[i++];

			int j = placeHolders.IndexOf<CompareStringNCase>(macro);

			switch (j)
			{
			case 1:
				FHolders.AddValue(new DateHolder());
				break;
			case 2:
				FHolders.AddValue(new TimeHolder());
				break;
			case 3:
				FHolders.AddValue(new DateTimeHolder());
				break;
			case 4:
				FHolders.AddValue(new MessageHolder());
				break;
			case 5:
				FHolders.AddValue(new ThreadHolder());
				break;
			case 6:
				FHolders.AddValue(new AppNameHolder(GetProperty(APPNAME_PROPERTY, DefaultAppName)));
				break;
			case 7:
				FHolders.AddValue(new AppVersionHolder(GetProperty(APPVERSION_PROPERTY, DefaultAppVersion))); 
				break;
			case 8:
				FHolders.AddValue(new OSHolder());
				break;
			case 9:
				FHolders.AddValue(new OSVersionHolder());
				break;
			case 10:
				FHolders.AddValue(new LogLevelHolder());
				break;

			default:
				// replace double % by single % ("%%" => "%")
				if(macro.size() == 2 && macro[0] == MACRO_SEP && macro[1] == MACRO_SEP)
					FHolders.AddValue(new LiteralHolder(MACRO_SEP_STR));
				else
					FHolders.AddValue(new LiteralHolder(macro));
				break;
			}
		}
		else // consider text between placeholders as LiteralHolder 
		{
			std::string lit;
			while ((i < pattern.size()) && (pattern[i] != MACRO_SEP))
				lit += pattern[i++];

			Holder* a = new LiteralHolder(lit);
			FHolders.AddValue(a);
		}
	}
}
*/
LOGENGINE_INLINE void Pattern::parsePattern(const std::string& pattern)
{
	clearHolders();

	FPattern = pattern;

	uint i = 0;
	while (i < pattern.size())
	{
		if (pattern[i] == MACRO_SEP)
		{
			std::string macro = MACRO_SEP_STR;
			i++;

			while ((i < pattern.size()) && (pattern[i] != MACRO_SEP))
				macro += pattern[i++];

			if (i < pattern.size()) // do not forget to add second % to placeholder
				macro += pattern[i++];


			Holder** holderPtr = FDefHolders.GetValuePointer(macro);
			if (holderPtr == nullptr)
			{
				// replace double % by single % ("%%" => "%")
				if (macro.size() == 2 && macro[0] == MACRO_SEP && macro[1] == MACRO_SEP)
					FHolders.AddValue(new LiteralHolder(MACRO_SEP_STR));
				else
					FHolders.AddValue(new LiteralHolder(macro));
			}
			else
			{
				FHolders.AddValue(*holderPtr);
			}

		}
		else // consider text between placeholders as LiteralHolder 
		{
			std::string lit;
			while ((i < pattern.size()) && (pattern[i] != MACRO_SEP))
				lit += pattern[i++];

			Holder* a = new LiteralHolder(lit);
			FHolders.AddValue(a);
		}
	}
}

LOGENGINE_INLINE void Pattern::clearHolders()
{
	// delete only LiteralHolder objects
	// other type are shared between Pattern instances
	std::transform(FHolders.begin(), FHolders.end(), FHolders.begin(), [](Holder* x) 
		{
			assert(x != nullptr);
			if (dynamic_cast<LiteralHolder*>(x) == x) delete x;
			return x; // it's ok that we return pointer to deleted object here, array will be cleared below
		});

	/*for (auto hld: FHolders)
	{
		delete hld;
	}*/

	/*for (uint i = 0; i < FHolders.Count(); i++)
	{
		Holder* h = FHolders[i];
		delete h;
	}*/

	FHolders.Clear();
}


LOGENGINE_NS_END
