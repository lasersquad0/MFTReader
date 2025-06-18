
// this is to remove defines min, max in windows headers because they conflict with std::min std::max 
#define NOMINMAX

#include <iostream>
#include <cassert>
#include <string>
#include <algorithm>
#include <shlwapi.h>
#include <malloc.h>
#include <iterator>

#include "string_utils.h"
#include "DynamicArrays.h"
#include "LogEngine.h"
#include "Functions.h"
#include "MTFReader.h"


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
* MFTReader.exe -p "c:\Program Files\Git\bin\git.exe" // здесь указывать диск опцией -d необязательно
* 
*/

uint32_t MFTRecIdByPath(VOLUME_DATA& volData, const ci_string& path)
{
    if (path.size() == 0) return 0;

    GET_LOGGER;
  
    std::vector<ci_string> arr;
    StringToArray(path, arr, L'\\');
    if (arr.size() == 0) return 0;

    assert(arr[0][0] == 'C' || arr[0][0] == 'c'); // we support only C: disks for now
  
    MFT_REF mftRecID;
    mftRecID.Id = MFT_ROOT_REC_ID; // starting MFT record
    uint8_t* mftRecBuf = (uint8_t*)alloca(volData.BytesPerMFTRec);

    DIR_NODE node;
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;

    for (size_t i = 1; i < arr.size(); i++) // bypass drive letter for now
    {
        if (!LoadMFTRecord(volData, mftRecID, mftRecBuf))
        {
            logger.Error("LoadMFTRecord finished with error.");
            return false;
        }

        node.Clear();
        GetFileListFromMFTRec(volData, mftRec, node); //TODO add error handling here
        FILE_NAME fn;
        fn.ciName = arr[i];
        auto iter = std::lower_bound(node.FileList.begin(), node.FileList.end(), fn); // fn has overrided operators < and ==
        if (iter != node.FileList.end() && (*iter).ciName == fn.ciName)
        {
            mftRecID = iter->MFTRef;
        }
        else
        {
            mftRecID.Id = 0; // signal that file not found
            break; // file not found in directory
        }
    }

    return mftRecID.sId.low;
}

void ClearCaches()
{
    Singleton<TMFTRecCache>::Release();
}

bool compare(const FILE_NAME& a, const FILE_NAME& b)
{
    if (IsDir(a.Attr) && !IsDir(b.Attr)) return true; // folders on top during sorting
    if (!IsDir(a.Attr) && IsDir(b.Attr)) return false; 
    return a.ciName < b.ciName;
}

bool compare2(const FILE_NAME& a, const FILE_NAME& b)
{
    return a.ciName < b.ciName;
}

bool VerifyArraySorted(THArray<FILE_NAME>& dirList)
{
    for (uint i = 1; i < dirList.Count(); i++)
    {
        if (dirList[i] < dirList[i-1]) return false;
    }
    return true;
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
        LogEngine::Logger& logger = LogEngine::GetFileLogger(MFT_LOGGER_NAME, "LogMFTReader.log");
        logger.SetAsyncMode(true);
        logger.SetLogLevel(LogEngine::Levels::llInfo);

        std::shared_ptr<LogEngine::Sink> sink(DBG_NEW LogEngine::StdoutSinkST(MFT_LOGGER_NAME));
        sink->SetLogLevel(LogEngine::Levels::llError);
        logger.AddSink(sink);
    }
}

static void ReadDirsV1(VOLUME_DATA& volData, MFT_REF startMftRecID)
{
    THArray<FILE_NAME> dirList;
    dirList.SetCapacity(1'000'000);

    ReadDirectoryV1(volData, startMftRecID, 0, dirList);

    //std::cout << "Sorting... " << std::endl;
    //std::sort(dirList.begin(), dirList.end(), compare);

    //std::string filename = "ListMFTFile_sorted.log";
    //LogEngine::TFileStream ff(filename);
    auto dirCount = std::count_if(dirList.begin(), dirList.end(), [](FILE_NAME& a) { return IsDir(a.Attr); });

    std::cout << toStringSepA(dirList.Count()) + " - total" << std::endl;
    std::cout << toStringSepA(dirList.Count() - dirCount) + " - files" << std::endl; // only files 
    std::cout << toStringSepA(dirCount) + " - dirs" << std::endl; // only dirs 

    /*ff << toStringSepW(dirList.Count()) + L" - total";
    ff << toStringSepW(dirList.Count() - dirCount) + L" - files"; // only files
    ff << toStringSepW(dirCount) + L" - dirs"; // only dirs

    std::cout << "Saving list of files to " << filename << std::endl;

    for (auto& item : dirList)
    {
        if (IsDir(item.Attr))
            item.ciName += L'\\';

        ff << item.ciName;
    }
*/
    dirList.ClearMem();

}

static void ReadDirsV2(VOLUME_DATA& volData, MFT_REF startMftRecID)
{
    THArray<FILE_NAME> dirList;
    dirList.SetCapacity(1'000'000);

    ReadDirectoryV2(volData, startMftRecID, 0, dirList);

    //std::cout << "Sorting... " << std::endl;
    //std::sort(dirList.begin(), dirList.end(), compare);

    //std::string filename = "ListMFTFile_sorted.log";
    //LogEngine::TFileStream ff(filename);
    auto dirCount = std::count_if(dirList.begin(), dirList.end(), [](FILE_NAME& a) { return IsDir(a.Attr); });

    std::cout << toStringSepA(dirList.Count()) + " - total" << std::endl;
    std::cout << toStringSepA(dirList.Count() - dirCount) + " - files" << std::endl; // only files 
    std::cout << toStringSepA(dirCount) + " - dirs" << std::endl; // only dirs 

    /*ff << toStringSepW(dirList.Count()) + L" - total";
    ff << toStringSepW(dirList.Count() - dirCount) + L" - files"; // only files
    ff << toStringSepW(dirCount) + L" - dirs"; // only dirs

    std::cout << "Saving list of files to " << filename << std::endl;

    for (auto& item : dirList)
    {
        if (IsDir(item.Attr))
            item.ciName += L'\\';

        ff << item.ciName;
    }
*/
    dirList.ClearMem();
}

static void ReadItems(VOLUME_DATA& volData, MFT_REF startMftRecID)
{
    GET_LOGGER;

    extern TItemInfoList gItemsList;
    gItemsList.SetCapacity(1'000'000); // Expect 1M files and dirs

    ITEM_INFO iInfo{0};
    if(!ReadMftItemInfo(volData, startMftRecID, 0, iInfo))
    {
        logger.Error("ReadMftItemInfo() returned false!");
    }

  
    auto AttrCountGreater7 = std::count_if(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a) { return a.AttrsCount > 7; });
    auto HardLinksGreater7 = std::count_if(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a) { return a.HardLinksCount > 7; });
    auto DataStreamsCountGreater2 = std::count_if(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a) { return a.DataStreamsCount > 2; });
    auto FilenamesCountGreater9 = std::count_if(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a) { return a.FileNamesCount > 9; });
    auto FilenamesCountGreater5 = std::count_if(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a) { return a.FileNamesCount > 5; });
    auto HasNonresAttrList = std::count_if(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a) { return a.NonResidentAttrList; });
    auto HasNonresBitmap = std::count_if(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a) { return a.NonResidentBitmap; });
    auto HasResidentData = std::count_if(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a) { return a.ResidentData; });

    auto maxHardLinks = std::max_element(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) {return a.HardLinksCount < b.HardLinksCount; });
    auto maxAttrs = std::max_element(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) {return a.AttrsCount < b.AttrsCount; });
    auto maxFilenames = std::max_element(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) {return a.FileNamesCount < b.FileNamesCount; });
    auto maxDataStreams = std::max_element(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) {return a.DataStreamsCount < b.DataStreamsCount; });

    std::cout << "Total items count: " << gItemsList.Count() << std::endl;
    std::cout << "Attrs count > 7: " << AttrCountGreater7 << std::endl;
    std::cout << "Hard Links count > 7: " << HardLinksGreater7 << std::endl;
    std::cout << "Data streams count > 2: " << DataStreamsCountGreater2 << std::endl;
    std::cout << "Filenames count > 9: " << FilenamesCountGreater9 << std::endl;
    std::cout << "Filenames count > 5: " << FilenamesCountGreater5 << std::endl;
    std::cout << "Have non-resident attr_list: " << HasNonresAttrList << std::endl;
    std::cout << "Have non-resident bitmap: " << HasNonresBitmap << std::endl;
    std::cout << "Have resident data: " << HasResidentData<< std::endl;
    std::cout << "ITEM_INFO size:" << sizeof(ITEM_INFO) << std::endl;
    std::cout << "DIR_NODE size:" << sizeof(DIR_NODE) << std::endl;
    std::cout << std::format("Max hard links count: {}, file name: {}", (*maxHardLinks).HardLinksCount, wtos((*maxHardLinks).MainName)) << std::endl;
    std::cout << std::format("Max attrs count: {}, file name: {}", (*maxAttrs).AttrsCount, wtos((*maxAttrs).MainName)) << std::endl;
    std::cout << std::format("Max file names count:{}, file name: {}", (*maxFilenames).FileNamesCount, wtos((*maxFilenames).MainName)) << std::endl;
    std::cout << std::format("Max data streams count:{}, filename:{}", (*maxDataStreams).DataStreamsCount, wtos((*maxDataStreams).MainName)) << std::endl;

    std::cout << "Datastream names:" << std::endl;
    for (auto& ds : (*maxDataStreams).DataStreamNames)
    {
        std::cout << wtos(ds) << std::endl;
    }

    std::cout << "File names:" << std::endl;
    for (auto& fn : (*maxFilenames).FileNames)
    {
        std::cout << wtos(fn) << std::endl;
    }

}

int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    _CrtMemState s1, s2, s3;
    _CrtMemCheckpoint(&s1); // Take a snapshot at the start of main()

    //allows russian text printed in console
    std::locale::global(std::locale("ru_RU.UTF-8"));
    std::wcout.imbue(std::locale());

    InitLogger();

    LogEngine::Logger& logger1 = LogEngine::GetLogger(MFT_LOGGER_NAME);
    assert(logger1.SinkCount() > 0); // make sure that we've got properly configured logger


    logger1.Info("--------------- START --------------");
    
    const wchar_t* volume = L"\\\\.\\C:";
    
    try
    {
        logger1.InfoFmt("Opening volume: {}", wtos(volume));

        HANDLE hVolume = CreateFile(volume, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);

        if (hVolume == INVALID_HANDLE_VALUE)
        {
            logger1.ErrorFmt("[CreateFile] Error opening volume: {} error: {}", wtos(volume), GetLastError());
            return 1;
        }

        NTFS_VOLUME_DATA_BUFFER nvdb;

        if (!DeviceIoControl(hVolume, FSCTL_GET_NTFS_VOLUME_DATA, 0, 0, &nvdb, sizeof(nvdb), 0, nullptr))
        {
            logger1.ErrorFmt("[DeviceIoControl] Error reading volume data: {} error: {}", wtos(volume), GetLastError());
            return 1;
        }

        VOLUME_DATA volumeData;
        volumeData.hVolume = hVolume;
        volumeData.BytesPerCluster = nvdb.BytesPerCluster;
        volumeData.BytesPerMFTRec = nvdb.BytesPerFileRecordSegment;
        volumeData.TotalClusters = nvdb.TotalClusters;
        volumeData.BytesPerSector = nvdb.BytesPerSector;

        //uint32_t recID = MFTRecIdByPath(volumeData, L"C:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-merge-index.exe");
        //std::cout << "Record ID: " << recID << std::endl;
        //return 1;

        MFT_REF startId;
        startId.Id = MFT_ROOT_REC_ID;

        auto start1 = std::chrono::high_resolution_clock::now();

        std::wcout << L"Reading volume: " << volume << std::endl;
        
        ClearCaches();
        
        //ReadDirsV1(volumeData, startId);
        //ReadDirsV2(volumeData, startId);
        ReadItems(volumeData, startId);

        auto stop = std::chrono::high_resolution_clock::now();
        logger1.WarnFmt("Reading time : {}", MillisecToStr<std::string>(std::chrono::duration_cast<std::chrono::milliseconds>(stop - start1).count()));
        std::cout << std::endl << "Reading time: " << MillisecToStr<std::string>(std::chrono::duration_cast<std::chrono::milliseconds>(stop - start1).count()) << std::endl;

    
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

        CloseHandle(hVolume);
        ClearCaches();

        logger1.Info("-----------------FINISH-------------------");

        //gItemsList.ClearMem();

        LogEngine::ShutdownLoggers();

        _CrtMemCheckpoint(&s2); // Take a snapshot at the end of main()
        if (_CrtMemDifference(&s3, &s1, &s2)) _CrtMemDumpStatistics(&s3); // Dump memory statistics excluding global variables
    }
    catch (std::runtime_error& ex)
    {
        logger1.ErrorFmt("MFTReader runtime_error. {}", ex.what());
    }
    catch (std::exception& ex)
    {
        logger1.ErrorFmt("MFTReader runtime_error. {}", ex.what());
    }
    catch (...)
    {
        logger1.Error("MFTReader error: UNKNOWN.");
    }

}


