
#include <iostream>
#include <cassert>
#include <string>
#include <algorithm>
#include <shlwapi.h>
#include <malloc.h>

#include "string_utils.h"
#include "DynamicArrays.h"
#include "LogEngine.h"
#include "Functions.h"
#include "MTFReader.h"


#pragma pack(show)

bool ParseNonresBitmap(const VOLUME_DATA& volData, MFT_ATTR_HEADER* attr, TBitField& bitmap)
{
    assert(attr->NonResidentFlag == 1);

    TDataRuns dataRuns;
    if (!DataRunsDecode(attr, dataRuns)) // DataRunsDecode writes a message into log file in case of an error
        return false;

    // GET_LOGGER;

    uint8_t* dataBuf = nullptr;
    uint32_t dataBufLen = 0; // memory size in clusters, how many clusters is allocated in dataBuf
    uint32_t r = 0;
    THArrayRaw bmpRecs(volData.BytesPerCluster);

    assert(dataRuns.Count() > 0);

    while (r < dataRuns.Count())
    {
        DATA_RUN_ITEM& rli = dataRuns[r];

        if (rli.len > dataBufLen)
        {
            delete[] dataBuf;
            dataBuf = DBG_NEW uint8_t[rli.len * volData.BytesPerCluster];
            dataBufLen = rli.len;
        }

        if (!ReadClusters(volData, rli.lcn, rli.len, dataBuf)) // ReadCluster writes error meesage to log file in case of an error
        {
            delete[] dataBuf;
            return false;
        }

        // in most cases nonresident Bitmap will occupy one data run
        // therefore we have special handling of this case
        if (dataRuns.Count() > 1)
            bmpRecs.AddMany(dataBuf, rli.len);

        r++;
    }

    if (dataRuns.Count() > 1)
    {
        uint32_t bmpLenBytes = bmpRecs.Count() * bmpRecs.GetItemSize();
        assert((bmpLenBytes & 0x07) == 0); // bitmap data size always multiple of 8
        bitmap.SetData((uint64_t*)bmpRecs.Memory(), bmpLenBytes >> 3);
    }
    else
    {
        uint32_t bmpLenBytes = dataBufLen * volData.BytesPerCluster; //TODO shall we use nonres.RealSize here?
        assert((bmpLenBytes & 0x07) == 0); // bitmap data size always multiple of 8
        bitmap.SetData((uint64_t*)dataBuf, bmpLenBytes >> 3);
    }

    delete[] dataBuf;
    return true;
}

bool ParseBitmap(const VOLUME_DATA& volData, MFT_ATTR_HEADER* attr, TBitField& bitmap)
{
    assert(bitmap.Count() == 0);
    if (!attr) return false; // attr==nullptr means that no LCNs need to be parsed, usually Bitmap is null for empty directories

    if (attr->NonResidentFlag == 0)
    {
        assert((attr->res.DataSize & 0x07) == 0); // bitmap data size always multiple of 8

        ATTR_BITMAP_ATTR* bmp = (ATTR_BITMAP_ATTR*)Add2Ptr(attr, attr->res.DataOffset);
        bitmap.SetData((uint64_t*)bmp->bitmap, attr->res.DataSize >> 3);
        return true;
    }
    else
    {
        return ParseNonresBitmap(volData, attr, bitmap);
    }
}

bool ParseAlloc(MFT_ATTR_HEADER* attr, TDataRuns& dataRuns)
{
    // sometimes one ATTR_LIST list may contain two ATTR_ALLOC attributes for some reason
    // it means we come here two times during parsing one MFT record with such ATTR_LIST 
    //assert(dataRuns.Count() == 0);

    if (!attr) return true; // attr==NULL means that ALLOC attribute is not present in MFT rec. Usully ALLOC is not needed in MFT rec for empty directories

    assert(attr->NonResidentFlag == 1);

    if (!DataRunsDecode(attr, dataRuns))
    {
        return false;
       // logger.Error("DataRunsDecode finished with error.");
    }
    return true;
}

bool ParseIndexRoot(MFT_ATTR_HEADER* attr, TLCNRecs& lcns, TFileList& fileList)
{
    assert(fileList.Count() == 0);
    assert(attr->NonResidentFlag == 0); // always resident

    ATTR_INDEX_ROOT* indexR = (ATTR_INDEX_ROOT*)Add2Ptr(attr, attr->res.DataOffset);
    auto pihdr = &(indexR->ihdr);

    //logger.DebugFmt("Stored attr type: {:#x} ({})", (uint32_t)indexR->AttrType, wtos(AttrTypeNames[indexR->AttrType >> 4]));
    //logger.DebugFmt("Collation rule: {}", (uint32_t)indexR->Rule);
    //logger.DebugFmt("Dir type: {} {}", indexR->ihdr.Flags, (indexR->ihdr.Flags == 0 ? " (small dir)" : " (big dir)"));

    GetFileListFromNode(pihdr, lcns, fileList);

    return true;
}

uint32_t GetFileListFromMFTRec(const VOLUME_DATA& volData, MFT_FILE_RECORD* mftRec, DIR_NODE& node)
{
    assert(mftRec->Flags == 0x03); // only "directory" record should go here

    PMFT_ATTR_HEADER attrValues[ATTR_TYPE_CNT];
    FillAttrValues(mftRec, attrValues);

    PMFT_ATTR_HEADER multValues[SAME_ATTR_CNT];
    GetAttr(volData, ATTR_BITMAP, attrValues, multValues);
    auto bitmap = multValues[0];
    assert(multValues[1] == nullptr); // only single value can be returned
    ParseBitmap(volData, bitmap, node.Bitmap); // copy bitmap into TBitField class for easier access

    GetAttr(volData, ATTR_ALLOC, attrValues, multValues); // multiple values can be returned
    uint i = 0;
    while(multValues[i] != nullptr)
        ParseAlloc(multValues[i++], node.DataRuns); // decode data runs and store them in node.DataRuns
    
    uint32_t lcnTotalCount = 0;
    for (auto& run : node.DataRuns) lcnTotalCount += run.len;

    TLCNRecs lcns(volData.BytesPerCluster, lcnTotalCount);
    lcns.LoadDataRuns(volData, node);  //TODO add error handling 

    GetAttr(volData, ATTR_ROOT, attrValues, multValues);
    auto root = multValues[0];
    assert(multValues[1] == nullptr); // only single value can be returned
    ParseIndexRoot(root, lcns, node.FileList);

    return node.FileList.Count();
}

uint32_t MFTRecIdByPath(VOLUME_DATA& volData, const ci_string& path)
{
    if (path.size() == 0) return 0;

    GET_LOGGER;
  
    std::vector<ci_string> arr;
    StringToArray(path, arr, L'\\');
    if (arr.size() == 0) return 0;

    assert(arr[0][0] == 'C' || arr[0][0] == 'c'); // we support only C: disks for now

  
    MFT_REF mftRecID;
    mftRecID.Id = MFT_ROOT_RECID; // root record
    uint8_t* mftRecBuf = (uint8_t*)alloca(volData.BytesPerMFTRec);

    DIR_NODE node;
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;

    for (size_t i = 1; i < arr.size(); i++) // bypass drive letter for now
    {
        if (!LoadMFTRecord(volData, mftRecID, mftRecBuf))
        {
            logger.Error("LoadMFTRecord finished with error.");
            //free(mftRecBuf);
            return false;
        }

        node.Clear();
        GetFileListFromMFTRec(volData, mftRec, node); //TODO add error handling here
        FILE_NAME fn;
        fn.ciName = arr[i];
        auto iter = std::lower_bound(node.FileList.begin(), node.FileList.end(), fn);
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

    //free(mftRecBuf);

    return mftRecID.sId.low;
}

bool ReadDirectory2(VOLUME_DATA& volData, MFT_REF parentMftRecID, uint32_t dirLevel, THArray<FILE_NAME>& gDirList)
{
    if (dirLevel > 30) throw std::runtime_error("dirLevel > 30 !!!!!!!");

    LogEngine::Logger& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);

    uint8_t* mftRecBuf = (uint8_t*)alloca(volData.BytesPerMFTRec);
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;

    if (!LoadMFTRecord(volData, parentMftRecID, mftRecBuf))
    {
        logger.Error("LoadMFTRecord finished with error.");
        //delete[] mftRecBuf;
        return false;
    }

    DIR_NODE node;

    GetFileListFromMFTRec(volData, mftRec, node);

    //delete[] mftRecBuf;

    for (auto& item : node.FileList)
    {
        if (!IsMetaFile(item) && !IsDotDir(item.ciName)) // do not add hidden metafiles into file list
        {
            if (dirLevel == 0) std::wcout << item.ciName.c_str() << std::endl;

            if(IsDir(item.Attr))
                gDirList.AddValue(item);
            else
                gDirList.AddValue(item);
        }

        //if (IsReparse(item.Attr)) { logger.InfoFmt("REPARSE detected: {}", wtos(item.Name)); continue; }

        // item.Attr.ParentDir actually is ref to item's MFT Id, not to the parent dir
        if (IsDir(item.Attr) && !IsMetaFile(item) && !IsDotDir(item.ciName)) // bypass hidden mft metafiles
            ReadDirectory2(volData, item.MFTRef, dirLevel + 1, gDirList);
    }

    return true;
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


int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    _CrtMemState s1, s2, s3;
    _CrtMemCheckpoint(&s1); // Take a snapshot at the start of main()

    LogEngine::Logger& logger1 = LogEngine::GetFileLogger(MFT_LOGGER_NAME, "LogMFTReader.log");
    logger1.SetAsyncMode(true);
    logger1.SetLogLevel(LogEngine::Levels::llInfo);
    //logger1.GetSink(MFT_LOGGER_NAME)->SetLogLevel(LogEngine::Levels::llDebug);

    std::shared_ptr<LogEngine::Sink> sink(DBG_NEW LogEngine::StdoutSinkST(MFT_LOGGER_NAME));
    sink->SetLogLevel(LogEngine::Levels::llError);
    logger1.AddSink(sink);
    
    logger1.Info("--------------- START --------------");
    
    PCWSTR volume = L"\\\\.\\C:";
    

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


        MFT_REF recId;
        //183207 cppunit11.12.1/src/wind32/debug;
        recId.Id = 5; // 70496 WinSyS; //60408 windows/install; //64501 system32; // 58193 prog data; // 114082; // 61; // 58663 windows; //57651 pf; // 58065 pf(86);//  195302 child MFT record; // 170497 cppunit11.12.1; // 91401 cppunit11.6;
        THArray<FILE_NAME> dirList;
        dirList.SetCapacity(1'000'000);

        auto start1 = std::chrono::high_resolution_clock::now();

        std::wcout << L"Reading volume: " << volume << std::endl;
        
        ClearCaches();
        ReadDirectory2(volumeData, recId, 0, dirList);
        //ReadDirectoryX(volumeData, recId, 0, dirList);

        auto stop = std::chrono::high_resolution_clock::now();
        logger1.WarnFmt("Reading time : {}", MillisecToStr<std::string>(std::chrono::duration_cast<std::chrono::milliseconds>(stop - start1).count()));
        std::cout << std::endl <<"Reading time: " << MillisecToStr<std::string>(std::chrono::duration_cast<std::chrono::milliseconds>(stop - start1).count()) << std::endl;
        
        //std::cout << "Sorting... " << std::endl;
        //std::sort(dirList.begin(), dirList.end(), compare);

        //uint32_t recID = MFTRecIdByPath(volumeData, L"C:\\Program Files\\Common Files\\Microsoft");

        std::string filename = "ListMFTFile_sorted.log";
        LogEngine::TFileStream ff(filename);
        auto dirCount = std::count_if(dirList.begin(), dirList.end(), [](FILE_NAME& a) { return IsDir(a.Attr); });
        
        std::cout << toStringSepA(dirList.Count()) + " - total" << std::endl;
        std::cout << toStringSepA(dirList.Count() - dirCount) + " - files" << std::endl; // only files 
        std::cout << toStringSepA(dirCount) + " - dirs" << std::endl; // only dirs 
       
        ff << toStringSepW(dirList.Count()) + L" - total";
        ff << toStringSepW(dirList.Count() - dirCount) + L" - files"; // only files 
        ff << toStringSepW(dirCount) + L" - dirs"; // only dirs 
        
        /*std::cout << "Saving list of files to " << filename << std::endl;

        for (auto& item : dirList)
        {
            if (IsDir(item.Attr))
                item.ciName += L'\\';
            
            ff << item.ciName;
        }*/

        dirList.ClearMem();
        CloseHandle(hVolume);

        ClearCaches();
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

    logger1.Info("-----------------FINISH-------------------");

}


