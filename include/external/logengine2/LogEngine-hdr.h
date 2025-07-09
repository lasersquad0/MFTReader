/*
 * LogEngine-hdr.h
 *
 * Copyright 2025, LogEngine2 Project. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#include <string>
#include "DynamicArrays.h"
#include "FileSink.h"
#include "RotatingFileSink.h"
#include "IniReader.h"


LOGENGINE_NS_BEGIN

class Registry
{
private:
	// logger names are case INsensitive
	THash<std::string, Logger*, CompareStringNCase> FLoggers;

	Registry() 
	{
		// adding default logger
		Logger* logger = new Logger("");
		FLoggers.SetValue("", logger);
		// by default we use single threaded sink here
		std::shared_ptr<StdoutSinkST> sink(new StdoutSinkST("_stdout_"));
		logger->AddSink(sink);
	}

	~Registry() { Shutdown(); }
public:
	Registry(const Registry&) = delete;
	Registry& operator=(const Registry&) = delete;

	void Shutdown()
	{
		for (uint i = 0; i < FLoggers.Count(); i++)
			delete FLoggers.GetValues()[i];

		FLoggers.ClearMem();
	}

	uint Count() { return FLoggers.Count(); }
	bool IfExists(const std::string& loggerName) { return FLoggers.IfExists(loggerName); }

	Logger& GetDefaultLogger()
	{
		return *FLoggers.GetValue("");
	}

	void SetDefaultLogger(Logger& logger)
	{
		//TODO memory leak? need to delete previous default logger before setting new one
		FLoggers.SetValue("", &logger);
	}

	Logger& RegisterLogger(const std::string& loggerName)
	{
		// Logger name can be empty for default logger only
		// other loggers must have non-empty name
		if (loggerName.size() == 0) throw LogException("Logger name cannot be empty.");

		Logger** loggerPtr = FLoggers.GetValuePointer(loggerName);
		if (loggerPtr == nullptr)
		{
			Logger* logger = new Logger(loggerName);
			FLoggers.SetValue(loggerName, logger);
			return *logger;
		}
		else
		{
			return **loggerPtr;
		}
	}

	//void InitializeLogger(Logger& newLogger);

	static Registry& Instance()
	{
		static Registry FInstance;
		return FInstance;
	}
};

static Properties properties;
//static Destructor destructor; // variable destructor must be located BELOW variable loggers because loggers should exist when destructor is being destroyed.

LOGENGINE_INLINE void SetProperty(const std::string& name, const std::string& value)
{
	properties.SetValue(name, value);
}

LOGENGINE_INLINE std::string GetProperty(const std::string& name, const std::string& defaultValue)
{
	return properties.GetString(name, defaultValue);
}

LOGENGINE_INLINE bool PropertyExist(const std::string& name)
{
	return properties.IfExists(name);
}

LOGENGINE_INLINE void ShutdownLoggers()
{
	Registry::Instance().Shutdown();
}

LOGENGINE_INLINE uint LoggersCount()
{
	return Registry::Instance().Count();
}

LOGENGINE_INLINE bool LoggerExist(const std::string& loggerName)
{
	return Registry::Instance().IfExists(loggerName);
}

LOGENGINE_INLINE Logger& GetDefaultLogger()
{
	return Registry::Instance().GetDefaultLogger();
}

LOGENGINE_INLINE void SetDefaultLogger(Logger& logger)
{
	return Registry::Instance().SetDefaultLogger(logger);
}

// returns reference to the logger with name specified in loggerName parameter
// creates new "empty" logger in case if logger with specified name does not yet exists
// "empty" means that the logger does not contain any Sinks.
LOGENGINE_INLINE Logger& GetLogger(const std::string& loggerName)
{
	return Registry::Instance().RegisterLogger(loggerName);
}

// returns reference to the file logger with name specified in loggerName parameter
// if logger with specified name does not exist new logger is created and one FileSink is added to thie logger
LOGENGINE_INLINE Logger& GetFileLoggerST(const std::string& loggerName, const std::string& fileName)
{
	Logger& logger = GetLogger(loggerName); // TODO what if second logger has the same logger name, but different file name???
	if (logger.SinkCount() > 0) return logger; // this is pre-existed logger

	auto sink = std::make_shared<FileSinkST>(loggerName, fileName); //TODO may be use file name as Sink name instead of logger name?
	logger.AddSink(sink);

	return logger;
}

LOGENGINE_INLINE Logger& GetFileLoggerMT(const std::string& loggerName, const std::string& fileName)
{
	Logger& logger = GetLogger(loggerName); // TODO what if second logger has the same logger name, but different file name???
	if (logger.SinkCount() > 0) return logger; // this is pre-existed logger

	auto sink = std::make_shared<FileSinkMT>(loggerName, fileName); //TODO may be use file name as Sink name instead of logger name?
	logger.AddSink(sink);

	return logger;
}

LOGENGINE_INLINE Logger& GetRotatingFileLoggerST(const std::string& loggerName, const std::string& fileName, ullong maxLogSize, LogRotatingStrategy strategy, uint maxBackupIndex)
{
	Logger& logger = GetLogger(loggerName); // TODO what if second logger has the same logger name, but different file name???
	if (logger.SinkCount() > 0) return logger; // this is pre-existed logger

	auto sink = std::make_shared<RotatingFileSinkST>(loggerName, fileName, maxLogSize, strategy, maxBackupIndex); //TODO may be use file name as Sink name instead of logger name?
	logger.AddSink(sink);

	return logger;
}

LOGENGINE_INLINE Logger& GetRotatingFileLoggerMT(const std::string& loggerName, const std::string& fileName, ullong maxLogSize, LogRotatingStrategy strategy, uint maxBackupIndex)
{
	Logger& logger = GetLogger(loggerName); // TODO what if second logger has the same logger name, but different file name???
	if (logger.SinkCount() > 0) return logger; // this is pre-existed logger

	auto sink = std::make_shared<RotatingFileSinkMT>(loggerName, fileName, maxLogSize, strategy, maxBackupIndex); //TODO may be use file name as Sink name instead of logger name?
	logger.AddSink(sink);

	return logger;
}

LOGENGINE_INLINE Logger& GetStdoutLoggerST(const std::string& loggerName)
{
	Logger& logger = GetLogger(loggerName); //TODO what to do when logger with the same name but another type (e.g. FileLogger) already exists. 
	if (logger.SinkCount() > 0) return logger; // this is existed logger

	auto sink = std::make_shared<StdoutSinkST>(loggerName);
	logger.AddSink(sink);
	return logger;
}

LOGENGINE_INLINE Logger& GetStdoutLoggerMT(const std::string& loggerName)
{
	Logger& logger = GetLogger(loggerName); //TODO what to do when logger with the same name but another type (e.g. FileLogger) already exists. 
	if (logger.SinkCount() > 0) return logger; // this is existed logger

	auto sink = std::make_shared<StdoutSinkMT>(loggerName);
	logger.AddSink(sink);
	return logger;
}

LOGENGINE_INLINE Logger& GetStderrLoggerST(const std::string& loggerName)
{
	Logger& logger = GetLogger(loggerName);
	if (logger.SinkCount() > 0) return logger; // this is pre-existed logger

	auto sink = std::make_shared<StderrSinkST>(loggerName);
	logger.AddSink(sink);
	return logger;
}

LOGENGINE_INLINE Logger& GetStderrLoggerMT(const std::string& loggerName)
{
	Logger& logger = GetLogger(loggerName);
	if (logger.SinkCount() > 0) return logger; // this is pre-existed logger

	auto sink = std::make_shared<StderrSinkST>(loggerName);
	logger.AddSink(sink);
	return logger;
}

LOGENGINE_INLINE Logger& GetMultiLogger(const std::string& loggerName, SinkList& sinks)
{
	Logger& logger = GetLogger(loggerName);
	logger.ClearSinks();
	for (uint i = 0; i < sinks.Count(); i++)
		logger.AddSink(sinks[i]);

	return logger;
}

LOGENGINE_INLINE Logger& GetMultiLogger(const std::string& loggerName, std::initializer_list<std::shared_ptr<Sink>> list)
{
	Logger& logger = GetLogger(loggerName);
	logger.ClearSinks(); // if existing logger is returned - re-initialise it with new Sinks
	for (auto sink: list)
		logger.AddSink(sink);

	return logger;
}

LOGENGINE_INLINE Logger& GetCallbackLoggerST(const std::string& loggerName, const CustomLogCallback& callback)
{
	Logger& logger = GetLogger(loggerName);
	if (logger.SinkCount() > 0) return logger; // this is pre-existed logger

	auto sink = std::make_shared<CallbackSinkST>(loggerName, callback);
	logger.AddSink(sink);
	return logger;
}

LOGENGINE_INLINE Logger& GetCallbackLoggerMT(const std::string& loggerName, const CustomLogCallback& callback)
{
	Logger& logger = GetLogger(loggerName);
	if (logger.SinkCount() > 0) return logger; // this is pre-existed logger

	auto sink = std::make_shared<CallbackSinkMT>(loggerName, callback);
	logger.AddSink(sink);
	return logger;
}

#ifdef WIN32
LOGENGINE_INLINE Logger& GetODebugLoggerST(const std::string& loggerName)
{
	Logger& logger = GetLogger(loggerName);
	if (logger.SinkCount() > 0) return logger; // this is pre-existed logger

	auto sink = std::make_shared<OutputDebugSinkST>(loggerName);
	logger.AddSink(sink);
	return logger;
}

LOGENGINE_INLINE Logger& GetODebugLoggerMT(const std::string& loggerName)
{
	Logger& logger = GetLogger(loggerName);
	if (logger.SinkCount() > 0) return logger; // this is pre-existed logger

	auto sink = std::make_shared<OutputDebugSinkMT>(loggerName);
	logger.AddSink(sink);
	return logger;
}
#endif

LOGENGINE_INLINE void Log(const std::string& msg, const Levels::LogLevel ll)
{
	Registry::Instance().GetDefaultLogger().Log(msg, ll);
}

LOGENGINE_INLINE void Crit(const std::string& msg) { Log(msg, Levels::llCritical); }
LOGENGINE_INLINE void Error(const std::string& msg) { Log(msg, Levels::llError); }
LOGENGINE_INLINE void Warn(const std::string& msg) { Log(msg, Levels::llWarning); }
LOGENGINE_INLINE void Info(const std::string& msg) { Log(msg, Levels::llInfo); }
LOGENGINE_INLINE void Debug(const std::string& msg) { Log(msg, Levels::llDebug); }
LOGENGINE_INLINE void Trace(const std::string& msg) { Log(msg, Levels::llTrace); }


static uint ParseInt(std::string s, uint defaultValue = 0)
{
	if (s.empty()) return defaultValue;

	int factor = 1;
	std::string sl = StrToLower(s);

	if (sl.find_last_of('k') != std::string::npos)
	{
		sl = sl.substr(0, sl.length() - 1);
		factor = 1024;
	}
	else if (sl.find_last_of('m') != std::string::npos)
	{
		sl = sl.substr(0, sl.length() - 1);
		factor = 1024 * 1024;
	}
	else if (sl.find_last_of('g') != std::string::npos)
	{
		sl = sl.substr(0, sl.length() - 1);
		factor = 1024 * 1024 * 1024;
	}

	try
	{
		uint result = static_cast<uint>(stoi(sl) * factor);
		return result;
	}
	catch (...)
	{
		return defaultValue;
	}
}

LOGENGINE_INLINE void InitFromFile(const std::string& fileName)
{
	IniReader reader;
	reader.LoadIniFile(fileName);
	THash<std::string, std::shared_ptr<Sink>> sinkCache; // used when several loggers refer to the same sink

	for (auto it = reader.begin(); it != reader.end(); ++it)
	{
		std::string sectName = *it;
		
		size_t n = sectName.find('.');
		if (/*(n != std::string::npos) &&*/ EqualNCase<std::string>(sectName.substr(0, n), LOGGER_PREFIX) )
		{
			auto& section = reader.GetSection(sectName);

			std::string loggerName = (n == std::string::npos) ? "" : sectName.substr(n + 1); // loggerName should be ampty if '.' is missing in the section name

			if (loggerName.empty()) 
				throw LogException("Logger name cannot be empty. See section '" + sectName + "' in " + fileName +" file.");

			Logger& logger = GetLogger(loggerName);
			logger.SetLogLevel(LLfromString(reader.GetValue(sectName, LOGLEVEL_PARAM, LL_DEFAULT_NAME, 0)));
			logger.SetAsyncMode(StrToBool(reader.GetValue(sectName, ASYNMODE_PARAM, "false", 0)));
			logger.FillProperties(section);

			if (!section.IfExists(SINK_PARAM)) continue; // logger does not have any sinks, it's ok

			auto& sinks = section.GetValue(SINK_PARAM);
			for (auto sn : sinks)
			{
				if (sn.empty()) continue; // ignore sinks with empty names for current logger

				Sink* sink = nullptr;
				if (!sinkCache.IfExists(sn)) // we never met this sink before, create and configure new sink instance
				{
					std::string sinkSectName = SINK_PREFIX;
					sinkSectName.append(sn);

					if (!reader.HasSection(sinkSectName))
						throw LogException("Sink section '" + sinkSectName + "' refered by logger '" + loggerName + "' not found in file '" + fileName + "'.");

					std::string value = reader.GetValue(sinkSectName, TYPE_PARAM, ST_DEFAULT_NAME, 0);
					LogSinkType stype = STfromString(value);
					LogSinkThreaded threaded = STHfromString(reader.GetValue(sinkSectName, THREAD_PARAM, STH_DEFAULT_NAME, 0));

					switch (stype)
					{
					case stStdout: if (threaded == sthSingle) sink = new StdoutSinkST(sn); else sink = new StdoutSinkMT(sn); break;
					case stStderr: if (threaded == sthSingle) sink = new StderrSinkST(sn); else sink = new StderrSinkMT(sn); break;
					case stString: if (threaded == sthSingle) sink = new StringSinkST(sn); else sink = new StringSinkMT(sn); break;
					case stFile:
					{
						std::string sinkFileName = reader.GetValue(sinkSectName, FILENAME_PARAM, "");
						if (sinkFileName.empty())
							throw LogException("File sink '" + sn + "' missing FileName parameter.");

						if (threaded == sthSingle)
							sink = new FileSinkST(sn, sinkFileName);
						else
							sink = new FileSinkMT(sn, sinkFileName);

						break;
					}
					case stRotatingFile:
					{
						std::string sinkFileName = reader.GetValue(sinkSectName, FILENAME_PARAM, "");
						if (sinkFileName.empty())
							throw LogException("File sink '" + sn + "' missing FileName parameter.");

						LogRotatingStrategy strategy = RSfromString(reader.GetValue(sinkSectName, STRATEGY_PARAM, RS_DEFAULT_NAME, 0));
						uint maxlogsize = ParseInt(reader.GetValue(sinkSectName, MAXLOGSIZE_PARAM, DefaultMaxLogSizeStr, 0), DefaultMaxLogSize);
						uint maxIndex = ParseInt(reader.GetValue(sinkSectName, MAXBACKUPINDEX_PARAM, DefaultMaxBackupIndexStr, 0), DefaultMaxBackupIndex);

						if (threaded == sthSingle)
							sink = new RotatingFileSinkST(sn, sinkFileName, maxlogsize, strategy, maxIndex);
						else
							sink = new RotatingFileSinkMT(sn, sinkFileName, maxlogsize, strategy, maxIndex);

						break;
					}
					default:
						break;
					}

					sink->SetLogLevel(LLfromString(reader.GetValue(sinkSectName, LOGLEVEL_PARAM, LL_DEFAULT_NAME, 0)));
					PatternLayout* lay = new PatternLayout();
					lay->SetPattern(reader.GetValue(sinkSectName, PATTERNALL_PARAM, DefaultLinePattern, 0));
					if (reader.HasValue(sinkSectName, CRITPATTERN_PARAM))  lay->SetCritPattern(reader.GetValue(sinkSectName, CRITPATTERN_PARAM));
					if (reader.HasValue(sinkSectName, ERRORPATTERN_PARAM)) lay->SetErrorPattern(reader.GetValue(sinkSectName, ERRORPATTERN_PARAM));
					if (reader.HasValue(sinkSectName, WARNPATTERN_PARAM))  lay->SetWarnPattern(reader.GetValue(sinkSectName, WARNPATTERN_PARAM));
					if (reader.HasValue(sinkSectName, INFOPATTERN_PARAM))  lay->SetInfoPattern(reader.GetValue(sinkSectName, INFOPATTERN_PARAM));
					if (reader.HasValue(sinkSectName, DEBUGPATTERN_PARAM)) lay->SetDebugPattern(reader.GetValue(sinkSectName, DEBUGPATTERN_PARAM));
					if (reader.HasValue(sinkSectName, TRACEPATTERN_PARAM)) lay->SetTracePattern(reader.GetValue(sinkSectName, TRACEPATTERN_PARAM));

					sink->SetLayout(lay);
					sink->FillProperties(reader.GetSection(sinkSectName));
					auto ssink = std::shared_ptr<Sink>(sink);
					sinkCache.SetValue(sn, ssink);
				}

				logger.AddSink(sinkCache.GetValue(sn));
				
			}
		}
	}
}


LOGENGINE_NS_END
