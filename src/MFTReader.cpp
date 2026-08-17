
// this is to remove defines min, max in windows headers because they conflict with std::min std::max 
#define NOMINMAX

#include "Debug.h"
#include <iostream>
#include <cassert>
#include <string>
#include <algorithm>
#include <iterator>
#include <shlwapi.h>
#include <malloc.h>
#include <tchar.h>

#include "strutils/include/string_utils.h"
#include "strutils/include/Ticks.h"
#include "logengine2/DynamicArrays.h"
#include "logengine2/LogEngine.h"
#include "cli/OptionsList.h"
#include "cli/HelpFormatter.h"
#include "cli/DefaultParser.h"
#include "Readers.h"
#include "MFTReader.h"


#define DEFAULT_VOLUME "C:"

int _tmain(int argc, TCHAR* argv[])
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    _CrtMemState s1, s2, s3;
    _CrtMemCheckpoint(&s1); // Take a snapshot at the start of main()

    //allows russian text printed in console
    std::locale::global(std::locale("ru_RU.UTF-8"));
    std::wcout.imbue(std::locale());
    //std::cout.imbue(std::locale());

    std::wcout << std::endl << "MFTReader shows various information about your NTFS file system." << std::endl << std::endl;

    InitLogger();
    LogEngine::Logger& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);
    assert(logger.SinkCount() > 0); // make sure that we've got properly configured logger

    logger.Debug("--------------- START --------------");
    logger.DebugFmt("LogEngine version {}.{}.{}", LOGENGINE_VER_MAJOR, LOGENGINE_VER_MINOR, LOGENGINE_VER_PATCH);

    CDefaultParser defaultParser;
    CCommandLine cmd;
    COptionsList options;

    DefineOptions(options);

    if (argc < 2) 
    {
        logger.Info("No command line arguments specified. MFTReader needs command line arguments, see description below.\n");
        PrintUsage(options);
        return 1;
    }

    if (!defaultParser.Parse(&options, &cmd, argv, argc))
    {
        logger.Error(wtos(defaultParser.GetLastError()));
        PrintUsage(options);
        return 1;
    }
    

    try 
    {
        //TODO re-do this error handling to proper way
        if (!cmd.HasOption(OPT_T))
            assert((cmd.HasOption(OPT_M) && !cmd.HasOption(OPT_P) && !cmd.HasOption(OPT_S) && !cmd.HasOption(OPT_C)) ||
                   (cmd.HasOption(OPT_P) && !cmd.HasOption(OPT_M) && !cmd.HasOption(OPT_S)) && !cmd.HasOption(OPT_C) ||
                   (cmd.HasOption(OPT_S) && !cmd.HasOption(OPT_C) && !cmd.HasOption(OPT_M) && !cmd.HasOption(OPT_P)) ||
                   (cmd.HasOption(OPT_C) && !cmd.HasOption(OPT_S) && !cmd.HasOption(OPT_M)) && !cmd.HasOption(OPT_P) );
                

        if (cmd.HasOption(OPT_M)) // info about one MFT record requested
        {
            string_t volume = cmd.GetOptionValue(OPT_M, 1, _T(DEFAULT_VOLUME));
            auto mftRecID = StringToMFTRecID(cmd.GetOptionValue(OPT_M, 0));

            auto absPath = IRecordsLoader::AbsPath(volume);

            IRecordsLoader* ldr{ nullptr };
            if(IRecordsLoader::IsPath(absPath)) //TODO shall we check here that path refered by absPath really exist?
                ldr = new TFileImageRecordsLoader(absPath);
            else
                ldr = new TWinAPIRecordsLoader(absPath);

            TMFTStatCollector rdr(*ldr);
            
            MFT_REF MFTRef{ mftRecID };
            THArray<std::wstring> paths;
            
            auto res = rdr.PathByMFTRecID(MFTRef, paths);
            if (res != TErrorCode::Success)
                logger.Warn("Error getting path by specified MFT record ID");

            cout_t << _T("Showing information about MFT record ID: ") << mftRecID << std::endl;

            for (auto& pth: paths)
            {
                cout_t << "Full path: " << convert_string<char_t>(pth) << std::endl;
            }
            
            std::wcout << std::endl;

            logger.SetLogLevel(LogEngine::Levels::llDebug);

            ITEM_INFO info{0};
            res = rdr.ReadMftItemInfo(MFTRef, nullptr, info);
            if (res != TErrorCode::Success)
                logger.Error("Error reading info about specified MFT record ID");

            delete ldr;

        }
        else if (cmd.HasOption(OPT_P)) // info about one item (file or dir) specified by path is requested
        {         
            string_t path = cmd.GetOptionValue(OPT_P, 0);
            TWinAPIRecordsLoader ldr(path);
            TMFTStatCollector rdr(ldr);
            
            auto MFTRecID = rdr.MFTRecIdByPath(path.c_str());
            if (MFTRecID)
            {
                logger.SetLogLevel(LogEngine::Levels::llDebug);

                cout_t << "Path: " << path << std::endl;

                MFT_REF MFTRef{ MFTRecID.value()};
                ITEM_INFO info{ 0 };

                auto res = rdr.ReadMftItemInfo(MFTRef, nullptr, info);
                if (res != TErrorCode::Success)
                    logger.Error("Error reading info about MFT record specified by path.");
            }
            else
            {
                cout_t << "Specified path is incorrect." << std::endl;
            }
        }
        else if (cmd.HasOption(OPT_S)) // volume statistics requested.
        {
            Ticks::Start(_T("FSReadingTime"));
            string_t volume = cmd.GetOptionValue(OPT_S, 0, _T(DEFAULT_VOLUME));

            auto absPath = IRecordsLoader::AbsPath(volume);
            
            IRecordsLoader* ldr{ nullptr };
            if (IRecordsLoader::IsPath(absPath)) //TODO shall we check here that path refered by volume really exist?
                ldr = new TFileImageRecordsLoader(absPath);
            else
                ldr = new TWinAPIRecordsLoader(absPath); // TWinAPICacheRecordsLoader ldr(absPath);

            TMFTStatCollector srdr(*ldr);

            auto res = srdr.CollectVolumeStat();
            if (res != TErrorCode::Success)
                logger.Error("Error reading volume files statistics.");

            srdr.ShowVolumeStat();

            //ReadDirsV2(vol);

            logger.InfoFmt("File System reading time : {}", MillisecToStr<std::string>(Ticks::Finish(_T("FSReadingTime"))));

            delete ldr;
        }
        else if (cmd.HasOption(OPT_C)) // volume statistics requested.
        {
            Ticks::Start(_T("FSReadingTime"));
            string_t volume = cmd.GetOptionValue(OPT_C, 0, _T(DEFAULT_VOLUME));

            auto absPath = IRecordsLoader::AbsPath(volume);

            IRecordsLoader* ldr{ nullptr };
            if (IRecordsLoader::IsPath(absPath)) //TODO shall we check here that path refered by volume really exist?
                ldr = new TFileImageRecordsLoader(absPath);
            else
                ldr = new TWinAPIRecordsLoader(absPath); // TWinAPICacheRecordsLoader ldr(absPath);

            TMFTSearchReader srchrdr(*ldr);
            srchrdr.ReadDirsV1();

            logger.InfoFmt("File System reading time : {}", MillisecToStr<std::string>(Ticks::Finish(_T("FSReadingTime"))));

            delete ldr;
        }
        else if (cmd.HasOption(OPT_T))
        {
            TWinAPIRecordsLoader ldr(_T("C"));
            TMFTBaseReader rdr(ldr);

            //rdr.FillAttrCollection()
        }
        else
        {
            cout_t << "Incorrect command line argument specified." << std::endl;
        }

        //logger.Debug("-----------------FINISH-------------------");
        LogEngine::ShutdownLoggers();

        _CrtMemCheckpoint(&s2); // Take a snapshot at the end of main()
        _CrtMemCheckpoint(&s2); // Take a snapshot at the end of main()
        if (_CrtMemDifference(&s3, &s1, &s2)) _CrtMemDumpStatistics(&s3); // Dump memory statistics excluding global variables
    }
    catch (std::runtime_error& ex)
    {
        logger.ErrorFmt("MFTReader Error: {}", ex.what());
    }
    catch (std::exception& ex)
    {
        logger.ErrorFmt("MFTReader Error: {}", ex.what());
    }
    catch (...)
    {
        logger.Error("MFTReader error: UNKNOWN.");
    }
}


void PrintUsage(COptionsList& options)
{
    cout_t << CHelpFormatter::Format(_T("MFTReader"), &options);
}

void DefineOptions(COptionsList& options)
{
    COption mm;
    mm.ShortName(OPT_M).LongName(_T("mft")).Descr(_T("Display information about specified MFT record.")).Required(false).NumArgs(2).RequiredArgs(1);
    options.AddOption(mm);

    //COption vv;
    //vv.ShortName(OPT_V).LongName(_T("volume")).Descr(_T("What disk/volume to use.")).Required(false).NumArgs(1).RequiredArgs(1);
    //options.AddOption(vv);

    COption pp;
    pp.ShortName(OPT_P).LongName(_T("path")).Descr(_T("Display information about file/directory by specified path.")).Required(false).NumArgs(1).RequiredArgs(1);
    options.AddOption(pp);

    COption ss;
    ss.ShortName(OPT_S).LongName(_T("stat")).Descr(_T("Show interesting volume/disk statistics.")).Required(false).NumArgs(1).RequiredArgs(0);
    options.AddOption(ss);

    COption cc;
    cc.ShortName(OPT_C).LongName(_T("cache")).Descr(_T("Build cache for file search and show some statistics.")).Required(false).NumArgs(1).RequiredArgs(0);
    options.AddOption(cc);

    //options.AddOption(OPT_S, _T("stat"), _T("Show interesting volume/disk statistics."), 1, false);
    //options.AddOption(OPT_C, _T("cache"), _T("Build cache for file search and show some statistics."), 1, false);
    options.AddOption(OPT_T, _T("test"), _T("For testing purposes."), 0, false);
}

#define MFT_LOG_CFG_FILENAME "MFTReader.lfg"
#define MFT_LOG_FILENAME "LogMFTReader.log"

void InitLogger()
{
    LogEngine::Levels::LogLevel llevel = LogEngine::Levels::llInfo;

    // if MFTReader.lfg exists load loggers from that file
    if (std::filesystem::exists(MFT_LOG_CFG_FILENAME))
    {
        LogEngine::InitFromFile(MFT_LOG_CFG_FILENAME);
    }
    else // otherwise configure loggers in code 
    {
        std::shared_ptr<LogEngine::Sink> consoleSink(DBG_NEW LogEngine::StdoutSinkST("consolesink"));
        consoleSink->SetPattern("%MSG%");
        consoleSink->SetLogLevel(llevel);

        std::shared_ptr<LogEngine::Sink> fileSink(DBG_NEW LogEngine::FileSinkST("file_sink", MFT_LOG_FILENAME));
        fileSink->SetLogLevel(llevel);

        LogEngine::Logger& logger = LogEngine::GetMultiLogger(MFT_LOGGER_NAME, { fileSink, consoleSink });
        //logger.SetAsyncMode(true);//TODO uncomment to increase performance, also change sinks from ST to MT
        logger.SetLogLevel(llevel, false); // do not overwrite sink's log levels.
    }
}
