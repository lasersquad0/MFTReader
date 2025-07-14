#pragma once

#include <functional>

#include "strutils/include/ci_string.h"
#include "logengine2/LogEngine.h"
#include "logengine2/DynamicArrays.h"
#include "NTFS.h"
#include "FileCache.h"
#include "BitField.h"


#define SAME_ATTR_CNT 5
#define MFT_LOGGER_NAME "mftlog"
#define MFT_LOGGER_NAME_FUNC "mftlogfunc"
#define MFT_LOGGER_NAME_LIST "mftlist"

#define GET_LOGGER auto& logger = LogEngine::GetLogger(MFT_LOGGER_NAME)
#define GET_LOGGER_FUNC auto& logger = GetLoggerFunc()

#define Diff2Ptr(ptr1, ptr2) ((ULONG)((uint8_t*)(ptr2) - (uint8_t*)(ptr1)))
#define Add2Ptr(P, I)		((uint8_t*)(P) + (I))

#define GetAttrName(pRec, field)  ( (wchar_t*)((uint8_t*)(pRec) + (pRec->field)) )
#define GetFName(pRec) ( (wchar_t*)((uint8_t*)(pRec) + sizeof(ATTR_FILE_NAME)) )
#define AttrMetaFile(_) ( ((_)->ParentDir.sId.low == MFT_ROOT_REC_ID) && (GetFName(_)[0] == L'$') )
#define AttrDotDir(_) ( ((_)->FileNameLen == 1) && (GetFName(_)[0] == L'.')  )
#define AttrNTfsInternal(_) (AttrMetaFile(_) || AttrDotDir(_))

// ANSI chaset for logging purposes only
#define ATTR_TYPE_NAMES { "ZERO", "STANDARD INFO", "ATTR LIST", "FILENAME", "OBJECT ID", "secure_info", "LABEL", \
                          "VOLUME INFO", "DATA", "INDEX ROOT", "ALLOCATION", "BITMAP", "REPARSE", "EA INFORMATION", \
                          "EA", "PROPERTYSHEET", "LOGGED UTILITY STREAM", "USER ATTRIBUTE" }

// Attr types have numbers 0x10, 0x20, 0x30, etc. - convert them into consecutive indexes in the array
// used for indexing ATTR_TYPE_NAMES array and in some other places
#define MakeAttrTypeIndex(_) ((_)>>4) 
#define AttrName(_) (AttrTypeNames[MakeAttrTypeIndex(_)])

static const char* AttrTypeNames[ATTR_TYPE_CNT] ATTR_TYPE_NAMES;
static const char* FileNameTypes[]{ "POSIX", "UNICODE", "DOS", "UNICODE_AND_DOS" };

struct VOLUME_DATA
{
    HANDLE hVolume;
    LARGE_INTEGER TotalClusters;
    DWORD BytesPerCluster;
    DWORD BytesPerMFTRec;
    DWORD BytesPerSector;
    DWORD MaxMFTIndex;
    std::wstring Name;
};

struct FILE_NAME
{
    ci_string ciName;
    struct ATTR_FILE_NAME Attr;
    struct MFT_REF MFTRef; // MFT Id of this file

    //bool operator>(const FILE_NAME& other) const { return ciName > other.ciName; }
    bool operator<(const FILE_NAME& other) const { return ciName < other.ciName; }
    bool operator==(const FILE_NAME& other) const { return ciName == other.ciName; }

    bool IsDir() const { return (Attr.dup.FileAttrib & (uint32_t)FILE_ATTR_FLAGS::DIRECTORY) > 0; }
    bool IsMetaFile() const { return (Attr.ParentDir.sId.low == MFT_ROOT_REC_ID) && (ciName[0] == L'$'); }
    bool IsDotDir() const { return (ciName.size() == 1) && (ciName[0] == L'.'); }
    bool IsReparse() const { return (Attr.dup.FileAttrib & (uint32_t)FILE_ATTR_FLAGS::REPARSE_POINT) > 0; };
    bool NtfsInternal() const { return IsMetaFile() || IsDotDir(); }
};

typedef THArray<FILE_NAME> TFileList;
typedef THArray<DATA_RUN_ITEM> TDataRuns;

struct DIR_NODE
{
    TFileList FileList; // filled from INDEX_ROOT attribute and then from ALLOCATE after processing data runs 
    TDataRuns DataRuns; // from ALLOCATE attribute
    TBitField Bitmap;   // tells us which LCNs from data runs are valid ones
    uint64_t DirSize{0};

    void Clear() 
    {
        FileList.ClearMem();
        DataRuns.ClearMem();
        Bitmap.Clear();
        DirSize = 0;
    }
};

struct ITEM_INFO
{
    MFT_REF RecID;
    uint16_t AttrCounters[ATTR_TYPE_CNT];
    bool NonResidentAttrList; // rare case. has an ATTR_LIST attribute that is non-resident
    bool NonResidentBitmap;   // rare case. has an BITMAP attribute that is non-resident
    //bool NonResidentAlloc;    
    bool ResidentData;   // Has an DATA attribute that is resident. Not so rare case.
    uint32_t FileAttrib;

    uint16_t HardLinksCount;
    uint16_t AttrsCount;
    ci_string MainName; // here is ci_string for quicker sorting array of ITEM_INFO
    THArray<std::wstring> FileNames; // contains filenames of all types - DOS, WIN and POSIX
    THash<std::wstring, uint16_t> DataStreamNames; // data stream name counts groupped by stream name
    
    DIR_NODE Node;

    bool operator<(const ITEM_INFO& other) const 
    { 
        /*ci_string n1 = MainName.c_str();
        ci_string n2 = other.MainName.c_str();
        return n1 < n2;*/
        return MainName < other.MainName;
    }

    bool operator==(const ITEM_INFO& other) const { return RecID.Id == other.RecID.Id; }

    bool IsDir() const { return (FileAttrib & (uint32_t)FILE_ATTR_FLAGS::DIRECTORY) > 0; }
//    bool IsMetaFile() const { return (Attr.ParentDir.sId.low == MFT_ROOT_REC_ID) && (ciName[0] == L'$'); }
//    bool IsDotDir() const { return (ciName.size() == 1) && (ciName[0] == L'.'); }
//    bool NtfsInternal() const { return IsMetaFile() || IsDotDir(); }

};

typedef THArray<ITEM_INFO> TItemInfoList;
typedef std::function<void(const ATTR_FILE_NAME*, const MFT_REF&)> FileListPred;

typedef int32_t (__stdcall *ProgressCallbackPtr)(int32_t progress);

LogEngine::Logger& GetLoggerFunc();
std::wstring ParseVolume(const std::wstring& vol);
void ReadVolumeData(const std::wstring& volume, VOLUME_DATA& volumeData);
bool ParseNonresBitmap(const VOLUME_DATA& volData, MFT_ATTR_HEADER* attr, TBitField& bitmap);
bool ParseNonresAttrList(const VOLUME_DATA& volData, MFT_ATTR_HEADER* attrAttrList, ATTR_TYPE attrType, PMFT_ATTR_HEADER* result);
bool ReadDirectoryV1(VOLUME_DATA& volData, uint32_t parentIdx, CACHE_ITEM* parentItem , uint64_t& dirSize, TFileCache& gFileList, ProgressCallbackPtr callback);
bool ReadDirectoryV2(VOLUME_DATA& volData, MFT_REF parentMftRecID, uint32_t dirLevel, TFileList& gDirList);
bool DataRunsDecode(MFT_ATTR_HEADER* attr, TDataRuns& runs);
bool ReadClusters(const VOLUME_DATA& volData, CLST lcnStart, CLST lcnCnt, PBYTE dataBuf);
bool FixupUSA(const VOLUME_DATA& volData, PBYTE dataBuf, DATA_RUN_ITEM& rli, uint64_t rlilen);
bool LoadMFTRecord(const VOLUME_DATA& volData, MFT_REF recID, uint8_t* mftRec);
uint8_t* LoadMFTRecordCache(const VOLUME_DATA& volData, MFT_REF recID);
void GetAttr(const VOLUME_DATA& volData, ATTR_TYPE attrType, const PMFT_ATTR_HEADER* const attrValues, PMFT_ATTR_HEADER* result);
void FillAttrValues(MFT_FILE_RECORD* mftRec, PMFT_ATTR_HEADER* attrValues);
//int32_t GetFileListFromMFTRec(const VOLUME_DATA& volData, MFT_FILE_RECORD* mftRec, DIR_NODE& node);
void GetFileList(INDEX_HDR* ihdr, FileListPred pred);
bool ProcessAllocDataRuns(VOLUME_DATA& volData, DIR_NODE& node, FileListPred pred);
void ReadDirsV2(VOLUME_DATA& volData);
void ReadDirsV1(VOLUME_DATA& volData);
void ReadItems(VOLUME_DATA& volData);
uint32_t MFTRecIdByPath(VOLUME_DATA& volData, const ci_string& path);
bool ReadMftItemInfo(VOLUME_DATA& volData, MFT_REF mftRecRef, ITEM_INFO& itemInfo);


//void GetFileListFromNode(INDEX_HDR* ihdr, TLCNRecs& lcns, TFileList& fnames);