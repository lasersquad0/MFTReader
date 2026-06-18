
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
#include "Functions.h"
#include "Caches.h"
#include "cli/OptionsList.h"
#include "cli/HelpFormatter.h"
#include "cli/DefaultParser.h"
#include "Utils.h"


static void PrintUsage(COptionsList& options)
{
    std::cout << wtos(CHelpFormatter::Format(_T("MFTReader"), &options));
}

#define OPT_M _T("m") // "MFT Record ID"
#define OPT_V _T("v") // "Volume"
#define OPT_P _T("p") // "Path"
#define OPT_S _T("s") // "Statistic"

static void DefineOptions(COptionsList& options)
{
    COption mm;
    mm.ShortName(OPT_M).LongName(_T("mft")).Descr(_T("Display information about specified MFT record.")).Required(false).NumArgs(1).RequiredArgs(1);
    options.AddOption(mm);

    COption vv;
    vv.ShortName(OPT_V).LongName(_T("volume")).Descr(_T("What disk/volume to use.")).Required(false).NumArgs(1).RequiredArgs(1);
    options.AddOption(vv);

    COption pp;
    pp.ShortName(OPT_P).LongName(_T("path")).Descr(_T("Display information about file/directory by specified path.")).Required(false).NumArgs(1).RequiredArgs(1);
    options.AddOption(pp);

    options.AddOption(OPT_S, _T("stat"), _T("Show overall volume/disk statistics."), 0, false);    
}


static void ResetCache()
{
    Singleton<TMFTRecCache>::Release();
}

static void InitLogger()
{
    LogEngine::Levels::LogLevel llevel = LogEngine::Levels::llInfo;

    // if MFTReader.lfg exists load loggers from that file
    if (std::filesystem::exists("MFTReader.lfg"))
    {
        LogEngine::InitFromFile("MFTReader.lfg");
    }
    else // otherwise configure loggers in code 
    {
        std::shared_ptr<LogEngine::Sink> consoleSink(DBG_NEW LogEngine::StdoutSinkST("consolesink"));
        consoleSink->SetLogLevel(llevel);

        std::shared_ptr<LogEngine::Sink> fileSink(DBG_NEW LogEngine::FileSinkST("file_sink", "LogMFTReader.log"));
        fileSink->SetLogLevel(llevel);

        LogEngine::Logger& logger = LogEngine::GetMultiLogger(MFT_LOGGER_NAME, {fileSink, consoleSink});
        //logger.SetAsyncMode(true);//TODO uncomment to increase performance, also change sinks from ST to MT
        logger.SetLogLevel(llevel, false); // do not overwrite sink's log levels.

    }
}


//#define TRYCATCH(_,__) try {(_);}catch(...){logger.Warn(__);}

int _tmain(int argc, TCHAR* argv[])
//int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    _CrtMemState s1, s2, s3;
    _CrtMemCheckpoint(&s1); // Take a snapshot at the start of main()

    //allows russian text printed in console
    std::locale::global(std::locale("ru_RU.UTF-8"));
    std::wcout.imbue(std::locale());

    InitLogger();
    LogEngine::Logger& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);
    assert(logger.SinkCount() > 0); // make sure that we've got properly configured logger

    logger.Debug("--------------- START --------------");

    CDefaultParser defaultParser;
    CCommandLine cmd;
    COptionsList options;

    DefineOptions(options);

    if (argc < 2) 
    {
        logger.Info("No command line arguments specified.");
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
        assert( (cmd.HasOption(OPT_M) && !cmd.HasOption(OPT_S) && !cmd.HasOption(OPT_P)) ||
                (cmd.HasOption(OPT_S) && !cmd.HasOption(OPT_M) && !cmd.HasOption(OPT_P)) ||
                (cmd.HasOption(OPT_P) && !cmd.HasOption(OPT_M) && !cmd.HasOption(OPT_S)) );


        cli_string volume = ParseVolume(cmd.GetOptionValue(OPT_V, 0, _T("C:")));
        VOLUME_DATA vol;
        ReadVolumeData(volume, vol);


        if (cmd.HasOption(OPT_M)) // info about one MFT record requested
        { 
            logger.SetLogLevel(LogEngine::Levels::llDebug);

            // we support both 10based mftRecID and hex format. 
            auto recIdStr = TrimSPCRLF(cmd.GetOptionValue(OPT_M, 0));
            MFTRecIndex mftRecID;
            if (recIdStr.size() > 1 && recIdStr[0] == '0' && (recIdStr[1] == 'x' || recIdStr[1] == 'X'))
                mftRecID = std::stoul(recIdStr, nullptr, 16); // exception will be thrown if option value cannot be converted into uint
            else
                mftRecID = std::stoul(recIdStr);

            THArray<std::wstring> paths;
            MFT_REF MFTRef{ 0 };
            MFTRef.sId.low = mftRecID;
            GetPathByMFTRecID(vol, MFTRef, paths);

            std::wcout << "Showing information about MFT record." << std::endl;
            std::wcout << "MFT Record ID: " << mftRecID << std::endl;
            for (auto& pth: paths)
            {
                std::wcout << "Full Path: " << pth << std::endl;
            }
            
            //uint8_t* mftRecBuf = (uint8_t*)alloca(vol.BytesPerMFTRec);

            ITEM_INFO info{0};
            
            ReadMftItemInfo(vol, MFTRef, info);

        }
        else if (cmd.HasOption(OPT_S)) // volume statistics requested.
        {
            auto start1 = std::chrono::high_resolution_clock::now();
            Ticks::Start(_T("FSReadingTime"));
            ResetCache();

            ReadDirsV1(vol);
            //ReadDirsV2(vol);
            //ShowVolumeStat(vol);

            //auto stop = std::chrono::high_resolution_clock::now();
            logger.WarnFmt("File System reading time : {}", MillisecToStr<std::string>(Ticks::Finish(_T("FSReadingTime")))); //std::chrono::duration_cast<std::chrono::milliseconds>(stop - start1).count()));

            ResetCache();
        }
        else if (cmd.HasOption(OPT_P))
        {
            cli_string path = cmd.GetOptionValue(OPT_P, 0);
            MFTRecIndex MFTRecID = GetMFTRecIdByPath(vol, path.c_str());
            if (MFTRecID > 0)
            {
                logger.SetLogLevel(LogEngine::Levels::llDebug);

                std::wcout << "Path: " << path << std::endl;

                MFT_REF MFTRef{ 0 };
                ITEM_INFO info{ 0 };

                MFTRef.sId.low = MFTRecID;

                ReadMftItemInfo(vol, MFTRef, info);
            }
            else
            {
                std::wcout << "Specified path is incorrect." << std::endl;
            }
        }
        else
        {
            std::wcout << "Incorrect command line argument specified." << std::endl;
        }
  
        CloseHandle(vol.hVolume);

        //uint32_t recID = MFTRecIdByPath(volumeData, L"C:\\Windows\\WinSxS\\FileMaps\\");
        //std::cout << "MFT Record ID: " << recID << std::endl;
        //return 1;

        // std::string filename = "LogMFTFile_Items.log";
       // LogEngine::TFileStream ff(filename);

       /* std::cout << "Looking for duplicates..." << std::endl;

        auto iterEnd = gItemsList.end();
        auto iterBeg = gItemsList.begin();
        auto start2 = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < gItemsList.Count(); i++)
        {
            ITEM_INFO& item = gItemsList[i];
            //   ff << toStringSepA(item.RecID.sId.low);

            auto iter = std::find_if(iterBeg + i + 1, iterEnd, [&](const ITEM_INFO& a) {return item.RecID.sId.low == a.RecID.sId.low; });
            if (iter != iterEnd) std::cout << "Duplicate item found: " << item.RecID.sId.low << std::endl;
        }

        stop = std::chrono::high_resolution_clock::now();
        std::cout << std::endl << "find_if() time 1: " << MillisecToStr<std::string>(std::chrono::duration_cast<std::chrono::milliseconds>(stop - start2).count()) << std::endl;

        */

        logger.Debug("-----------------FINISH-------------------");

        //gItemsList.ClearMem();

        LogEngine::ShutdownLoggers();

        _CrtMemCheckpoint(&s2); // Take a snapshot at the end of main()
        if (_CrtMemDifference(&s3, &s1, &s2)) _CrtMemDumpStatistics(&s3); // Dump memory statistics excluding global variables
    }
    catch (std::runtime_error& ex)
    {
        logger.ErrorFmt("MFTReader runtime_error. {}", ex.what());
    }
    catch (std::exception& ex)
    {
        logger.ErrorFmt("MFTReader std::exception. {}", ex.what());
    }
    catch (...)
    {
        logger.Error("MFTReader error: UNKNOWN.");
    }

}


