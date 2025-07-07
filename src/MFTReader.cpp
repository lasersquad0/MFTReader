
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
#include "logengine2/DynamicArrays.h"
#include "logengine2/LogEngine.h"
#include "Functions.h"
#include "Caches.h"
#include "cli/OptionsList.h"
#include "cli/HelpFormatter.h"
#include "cli/DefaultParser.h"
#include "Utils.h"


/* Идеи для анализа файловой системы
1. MFT записи с редкими флагами   
IS_4          = 0x0004, // it is called RECORD_FLAG_SYSTEM in another source
    IS_VIEW_INDEX = 0x0008, // it is called RECORD_FLAG_UNKNOWN in another source
    SPACE_FILLER 
2. Файлы с альтернативными потоками - названия этих потоков и размеры
3. Папки с кол-во файлов больше чем 1000 (например)
3. Файлы-папки имеющие 3 и более имен.
4. файлы-папки имеющие 2 и больше hard links
5. Файлы-папки с атрибутом sparse
6. Файлы- папки имеющие в MFT записи кол-во атрибутов > 5 (например)
7. файлы-папки имеющие редкие атрибуты EA, EA_INFO, SECURE_ID, ATTR_LOGGED_UTILITY_STREAM
8. файлы-папки имеющие nonresident атрибуты кроме DATA
9. Файлы-папки имеющие длинные data run list (3 и более items в data runs list)
10. 
*/

/* Идеи для commandline tool
*  вывод всех Attributes по recID
*  вывод всех attributes по file path 
*     + опция выводить ли все file names либо только unicode
* Опция задать диск с которым работаем C: или D: и т.п.
* 
* MFTReader.exe -d C -m 32435   // вывод информации по MFT record ID. 
* MFTReader.exe -p "c:\Program Files\Git\bin\git.exe" // здесь указывать диск опцией -d необязательно но можно
* MFTReader.exe -d D -p "Git\bin\git.exe" // relative path. здесь указывать диск опцией -d нужно
* 
Вывод различной статистики по файловой системе.
*   + нужна ли опция вывода статистики по папке (с подпапками)??
* здесь подумать как правильно задавать КАКУЮ статистику хочу видеть и какие фильтры (сумматоры) хочу применить да сырые данные.
* 
* 
*/

static void PrintUsage(COptionsList& options)
{
    std::cout << wtos(CHelpFormatter::Format(_T("MFTReader"), &options));
}

#define OPT_M _T("m") // "mft record ID"
#define OPT_D _T("d") // "disk"
#define OPT_P _T("p") // "path"
#define OPT_S _T("s") // "statistic"
/*#define OPT_H _T("h")
#define OPT_L _T("l")
#define OPT_M _T("m")
#define OPT_O _T("o")
#define OPT_SM _T("sm")
#define OPT_T _T("t")
#define OPT_V _T("v")
#define OPT_X _T("x")
*/

static void DefineOptions(COptionsList& options)
{
    COption mm;
    mm.ShortName(OPT_M).LongName(_T("mftrec")).Descr(_T("Display information about specified MFT record.")).Required(false).NumArgs(1).RequiredArgs(1);
    options.AddOption(mm);

    COption dd;
    dd.ShortName(OPT_D).LongName(_T("disk")).Descr(_T("What disk/volume to use for reading info.")).Required(false).NumArgs(1).RequiredArgs(1);
    options.AddOption(dd);

    COption pp;
    pp.ShortName(OPT_P).LongName(_T("path")).Descr(_T("Show information about file/directory by specified path.")).Required(false).NumArgs(1).RequiredArgs(1);
    options.AddOption(pp);

    options.AddOption(OPT_S, _T("stat"), _T("Show overall volume/disk statistics."), 1);
    
    /*options.AddOption(OPT_L, _T("list"), _T("List content of archive"), 1);
    options.AddOption(OPT_T, _T("threads"), _T("Use specified number of threads during operation"), 1);
    options.AddOption(OPT_H, _T("help"), _T("Show help"), 0);
    options.AddOption(OPT_M, _T("model-type"), _T("Use model of specified order. Valid model types: O0, O1, O2, O3, O0FIX, O0SORT, O0PAIR, O3MIX, O1FPAQ."), 1);
    options.AddOption(OPT_C, _T("coder-type"), _T("Use specified coder. Valid coders: huf, ahuf, ari, ari32, ari64, bitari."), 1);
    options.AddOption(OPT_V, _T("verbose"), _T("Print more detailed (verbose) information to screen."), 0);
    options.AddOption(OPT_SM, _T("stream-mode"), _T("Use stream mode (oposite to block mode). No BWT, no MTB in this mode."), 0);
    options.AddOption(OPT_O, _T("output-dir"), _T("Specifies directory where uncompressed files will be placed. Valid with -x option only."), 1);
    */
}


static void ResetCache()
{
    Singleton<TMFTRecCache>::Release();
}

static void InitLogger()
{
    // if MFTReader.lfg exists load loggers from that file
    if (std::filesystem::exists("MFTReader.lfg"))
    {
        LogEngine::InitFromFile("MFTReader.lfg");
    }
    else // otherwise configure loggers in code 
    {
        std::shared_ptr<LogEngine::Sink> consoleSink(DBG_NEW LogEngine::StdoutSinkST("consolesink"));
        consoleSink->SetLogLevel(LogEngine::Levels::llInfo);

        std::shared_ptr<LogEngine::Sink> fileSink(DBG_NEW LogEngine::FileSinkMT(MFT_LOGGER_NAME, "LogMFTReader.log"));
        fileSink->SetLogLevel(LogEngine::Levels::llInfo);

        LogEngine::Logger& logger = LogEngine::GetMultiLogger(MFT_LOGGER_NAME, {fileSink, consoleSink});
        logger.SetAsyncMode(true);
        logger.SetLogLevel(LogEngine::Levels::llInfo, false); // do not overwrite sink's log levels.

        //std::shared_ptr<LogEngine::Sink> sink(DBG_NEW LogEngine::StdoutSinkST(MFT_LOGGER_NAME));
        //sink->SetLogLevel(LogEngine::Levels::llError);
        //logger.AddSink(sink);
    }
}


#define TRYCATCH(_,__) try {(_);}catch(...){logger.Warn(__);}

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
    logger.Info("--------------- START --------------");

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
        cli_string volume = ParseVolume(cmd.GetOptionValue(OPT_D, 0, _T("C:")));
        
        VOLUME_DATA vol{0};
        ReadVolumeData(volume, vol);

        if (cmd.HasOption(OPT_M))
        {            
            int MFTRecID = std::stoi(cmd.GetOptionValue(OPT_M, 0)); // exception will be thrown if option value cannot be converted into int

            std::wcout << "here will be shown information about record: " << MFTRecID << "." << std::endl;

        }
        else if (cmd.HasOption(OPT_S)) // volume statistics requested.
        {
            //std::wcout << "Here will be shown statistics about volume " << volume << "." << std::endl;

           // MFT_REF startId;
           // startId.Id = MFT_ROOT_REC_ID;

            auto start1 = std::chrono::high_resolution_clock::now();

            std::wcout << _T("Reading volume: ") << volume << std::endl;

            ResetCache();

            ReadDirsV1(vol);
            //ReadDirsV2(vol);
           // ReadItems(vol);

            auto stop = std::chrono::high_resolution_clock::now();
            logger.WarnFmt("Reading time : {}", MillisecToStr<std::string>(std::chrono::duration_cast<std::chrono::milliseconds>(stop - start1).count()));
            //std::cout << std::endl << "Reading time: " << MillisecToStr<std::string>(std::chrono::duration_cast<std::chrono::milliseconds>(stop - start1).count()) << std::endl;

            ResetCache();
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

        logger.Info("-----------------FINISH-------------------");

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


