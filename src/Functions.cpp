
// this is to remove defines min, max in windows headers because they conflict with std::min std::max 
#define NOMINMAX

#include "Debug.h"
#include <iostream>
#include <string>
#include <cstdint>
#include <cassert>
#include <vector>
#include <stdexcept>
#include <system_error>

#include "strutils/include/string_utils.h"
#include "strutils/include/ci_string.h"
#include "logengine2/LogEngine.h"
//#include "logengine2/FileStream.h"
#include "NTFS.h"
#include "Functions.h"
#include "Caches.h"
#include "Readers.h"


LogEngine::Logger& GetLoggerFunc()
{
    LogEngine::Logger& logger = LogEngine::GetFileLogger(MFT_LOGGER_NAME_FUNC, "LogMFTReaderFUNC.log");
    //logger.SetAsyncMode(true);
    logger.SetLogLevel(LogEngine::Levels::llDebug);
    return logger;
}

// Supports both 10based mftRecID and hex format. 
MFTRecIndex StringToMFTRecID(const string_t& strMFTRecID)
{
    auto recIdStr = TrimSPCRLF(strMFTRecID);
    
    if (recIdStr.size() > 1 && recIdStr[0] == '0' && (recIdStr[1] == 'x' || recIdStr[1] == 'X'))
        return std::stoul(recIdStr, nullptr, 16); // exception will be thrown if option value cannot be converted into uint
    else
        return std::stoul(recIdStr);
}

//This function is not fully compatible with FILE_ATTR_FLAGS enum
std::string FormatFileAttributes(uint32_t a)
{
    std::string s = "-----------------"; // 17 chars

    if (a & FILE_ATTRIBUTE_READONLY)               s[0] = 'R'; // READONLY
    if (a & FILE_ATTRIBUTE_HIDDEN)                 s[1] = 'H'; // HIDDEN
    if (a & FILE_ATTRIBUTE_SYSTEM)                 s[2] = 'S'; // SYSTEM
    
    if ((a & FILE_ATTRIBUTE_DIRECTORY) ||
       (a & (uint32_t)FILE_ATTR_FLAGS::DIRECTORY)) s[3] = 'D'; // DIRECTORY

    if (a & FILE_ATTRIBUTE_ARCHIVE)                s[4] = 'A'; // ARCHIVE
    if (a & FILE_ATTRIBUTE_NORMAL)                 s[5] = 'N'; // NORMAL
    if (a & FILE_ATTRIBUTE_TEMPORARY)              s[6] = 'T'; // TEMPORARY
    if (a & FILE_ATTRIBUTE_SPARSE_FILE)            s[7] = 's'; // SPARSE (lowercase for diff)
    if (a & FILE_ATTRIBUTE_REPARSE_POINT)          s[8] = 'r'; // REPARSE (lowercase for diff)
    if (a & FILE_ATTRIBUTE_COMPRESSED)             s[9] = 'C'; // COMPRESSED
    if (a & FILE_ATTRIBUTE_OFFLINE)                s[10] = 'O'; // OFFLINE
    if (a & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED)    s[11] = 'I'; // NOT INDEXED
    if (a & FILE_ATTRIBUTE_ENCRYPTED)              s[12] = 'E'; // ENCRYPTED
    if (a & FILE_ATTRIBUTE_INTEGRITY_STREAM)       s[13] = 'P'; // INTEGRITY The directory or user data stream is configured with integrity (only supported on ReFS volumes)
    if (a & FILE_ATTRIBUTE_NO_SCRUB_DATA)          s[14] = 'U'; // NO SCRUB
    if (a & FILE_ATTRIBUTE_EA)                     s[15] = 'L'; // EA
    if (a & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)  s[16] = 'X'; // RECALL ON ACCESS
    /* FILE_ATTRIBUTE_DEVICE
         FILE_ATTRIBUTE_PINNED
         FILE_ATTRIBUTE_UNPINNED
         FILE_ATTRIBUTE_VIRTUAL
         FILE_ATTRIBUTE_RECALL_ON_OPEN
         FILE_ATTRIBUTE_STRICTLY_SEQUENTIAL*/
    return s;
}





// reads list of files in already sorted order
bool TMFTSearchReaderV2::ReadDirectoryV2(MFT_REF parentMftRecID, uint32_t dirLevel)
{
    if (dirLevel > 30) throw std::runtime_error("dirLevel > 30 !!!!!!!");

    GET_LOGGER;

    uint8_t* mftRecBuf = (uint8_t*)alloca(getVolData().BytesPerMFTRec);
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;

    if (!FLoader.LoadMFTRecord(parentMftRecID, mftRecBuf))
    {
        logger.Error("LoadMFTRecord finished with error.");
        return false;
    }

    DIR_NODE node;
    int32_t cnt = GetFileListFromMFTRec(mftRec, node); // writes error to log file in case of error
    if (cnt == -1) return false;

    for (auto& item : node.FileList)
    {
        if (!item.NtfsInternal()) // do not add hidden metafiles into file list
        {
            if (dirLevel == 0) std::wcout << item.ciName.c_str() << std::endl;
            //if (dirLevel == 1) std::wcout << "      " << item.ciName.c_str() << std::endl;

            FDirList.AddValue(item);

            if (item.IsDir())
            {
                if (!item.IsReparse())
                {
                    if (!ReadDirectoryV2(item.MFTRef, dirLevel + 1))
                        logger.ErrorFmt("ReadDirectoryV2 finished with error for MFT rec: {}", item.MFTRef.sId.low);
                }
            }
        }
    }

    return true;
}

/*static bool compare(const FILE_NAME& a, const FILE_NAME& b)
{
    if (IsDir(a.Attr) && !IsDir(b.Attr)) return true; // folders on top during sorting
    if (!IsDir(a.Attr) && IsDir(b.Attr)) return false;
    return a.ciName < b.ciName;
}*/

void TMFTSearchReaderV2::ReadDirsV2()
{
    //TFileList dirList;
    FDirList.Clear();
    FDirList.SetCapacity(1'000'000);

    MFT_REF startId{0};
    startId.Id = MFT_ROOT_REC_ID;
    ReadDirectoryV2(startId, 0);

    auto dirCount = std::count_if(FDirList.begin(), FDirList.end(), [](FILE_NAME& a) { return a.IsDir(); });

    std::cout << toStringSepA(FDirList.Count()) + " - total" << std::endl;
    std::cout << toStringSepA(FDirList.Count() - dirCount) + " - files" << std::endl; // only files 
    std::cout << toStringSepA(dirCount) + " - dirs" << std::endl; // only dirs 

    /* std::string filename = "ListMFTFile_sortedV2.log";
     LogEngine::TFileStream ff(filename);

     string_t endl;
     BUILD_ENDL(endl);

     ff << toStringSepW(dirList.Count()) + L" - total" << endl;
     ff << toStringSepW(dirList.Count() - dirCount) + L" - files" << endl; // only files
     ff << toStringSepW(dirCount) + L" - dirs" << endl; // only dirs

     std::cout << "Sorting... " << std::endl;
     std::sort(dirList.begin(), dirList.end());

     std::cout << "Saving list of files to " << filename << std::endl;

     for (auto& item : dirList)
     {
         ff << item.ciName;
         if (item.IsDir()) ff << L'\\';
         ff << endl;
     }*/

    FDirList.ClearMem();
}

