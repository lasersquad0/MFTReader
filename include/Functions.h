#pragma once

#include <functional>
#include <optional>
#include <windows.h>
#include <winioctl.h>

#include "strutils/include/ci_string.h"
#include "logengine2/LogEngine.h"
#include "logengine2/DynamicArrays.h"
#include "NTFS.h"
//#include "FileCache.h"
#include "BitField.h"


#define MFT_LOGGER_NAME "mftlog"
#define MFT_LOGGER_NAME_FUNC "mftlogfunc"
//#define MFT_LOGGER_NAME_LIST "mftlist"

constexpr uint32_t DEFAULT_BYTES_PER_MFT_REC = 1024;

#define GET_LOGGER auto& logger = LogEngine::GetLogger(MFT_LOGGER_NAME)
#define GET_LOGGER_FUNC auto& logger = GetLoggerFunc()

#define Diff2Ptr(ptr1, ptr2) ((ULONG)((uint8_t*)(ptr2) - (uint8_t*)(ptr1)))
#define Add2Ptr(P, I)		 ((uint8_t*)(P) + (I))

#define GetAttrName(pRec, field) ( (wchar_t*)((uint8_t*)(pRec) + (pRec->field)) )
#define GetFName(pRec)  ( (wchar_t*)((uint8_t*)(pRec) + sizeof(ATTR_FILE_NAME)) )
//#define AttrIsMetaFile(_) ( ((_)->ParentDir.sId.low == MFT_ROOT_REC_ID) && (GetFName(_)[0] == L'$') )
//#define AttrIsDotDir(_)   ( ((_)->FileNameLen == 1) && (GetFName(_)[0] == L'.')  )
//#define AttrIsNtfsInt(_) (AttrIsMetaFile(_) || AttrIsDotDir(_))

// ANSI charset, for logging purposes only
#define ATTR_TYPE_NAMES { "ZERO", "STANDARD INFO", "ATTR LIST", "FILENAME", "OBJECT ID", "secure_info", "LABEL", \
                          "VOLUME INFO", "DATA", "INDEX ROOT", "ALLOCATION", "BITMAP", "REPARSE", "EA INFORMATION", \
                          "EA", "PROPERTYSHEET", "LOGGED UTILITY STREAM", "USER ATTRIBUTE" }


// Attr types have numbers 0x10, 0x20, 0x30, etc. - convert them into consecutive indexes in the array
// used for indexing ATTR_TYPE_NAMES array and in some other places
#define MakeAttrTypeIndex(_) ((_)>>4) 
#define MATI(_) ((_)>>4) 
#define AttrName(_) (AttrTypeNames[(_)>>4])
#define MakeAttrBitmask(_) (1<<((_)>>4))

static const char* AttrTypeNames[ATTR_TYPE_CNT] ATTR_TYPE_NAMES;
static const char* FileNameTypes[]{ "POSIX", "UNICODE", "DOS", "UNICODE_AND_DOS" };
static const char* CollationRuleNames[]{ "BINARY",  "FILENAME", "UINT", "SID",  "SECURITY_HASH", "UINTS", "UNKNOWN"};

#define MakeCollationRuleIndex(_) ((_)==0?0:(_)==1?1:(_)==0x10?2:(_)==0x11?3:(_)==0x12?4:(_)==0x13?5:6) 
#define CollRuleName(_) (CollationRuleNames[MakeCollationRuleIndex(_)])

struct FILE_NAME
{
    ci_string ciName;
    struct ATTR_FILE_NAME Attr{0};
    struct MFT_REF MFTRef{0}; // MFT Rec ID of this file

    //bool operator>(const FILE_NAME& other) const { return ciName > other.ciName; }
    bool operator<(const FILE_NAME& other) const { return ciName < other.ciName; }
    bool operator==(const FILE_NAME& other) const { return ciName == other.ciName; }

    bool IsDir() const { return (Attr.dup.FileAttrib & (uint32_t)FILE_ATTR_FLAGS::DIRECTORY) > 0; }
    //bool IsMetaFile() const { return (Attr.ParentDir.sId.low == MFT_ROOT_REC_ID) && (ciName[0] == L'$'); } // assumes ciName is not empty
    //bool IsDotDir() const { return (ciName.size() == 1) && (ciName[0] == L'.'); } 
    bool IsReparse() const { return (Attr.dup.FileAttrib & (uint32_t)FILE_ATTR_FLAGS::REPARSE_POINT) > 0; };
    //bool NtfsInternal() const { return IsMetaFile() || IsDotDir(); }
};

#if _DEBUG
static_assert(sizeof(FILE_NAME) == 120); 
#else
static_assert(sizeof(FILE_NAME) == 112);
#endif

typedef THArray<FILE_NAME> TFileList;
typedef THArray<DATA_RUN_ITEM> TDataRuns;

#define FILE_LIST_DEF_SIZE 100
#define DATA_RUNS_DEF_SIZE 100

struct DIR_NODE
{
    uint32_t IndexBlockSize; // got from INDEX_ROOT attr, required for processing ALLOC data runs
    TFileList FileList; // filled from INDEX_ROOT attribute and then from ALLOCATE after processing data runs 
    TDataRuns DataRuns; // from ALLOCATE attribute
    TBitField Bitmap;   // tells us which LCNs from data runs are valid ones
    uint64_t DirSize = 0;

    uint64_t GetDataRunsLCNsCount()
    {
        uint64_t result = 0;
        for (auto& rli : DataRuns)
        {
            result += rli.len;
        }
        return result;
    }

    void Clear() 
    {
        // leave memory allocated for the next iterations
        FileList.Clear(); 
        DataRuns.Clear();
        Bitmap.Clear();
        DirSize = 0;
    }
};

struct ITEM_INFO
{
    MFT_REF RecID{ 0 };
    uint16_t AttrCounters[ATTR_TYPE_CNT]{ 0 };
    std::optional<bool> NonResidentAttrList = std::nullopt; // rare case. has an ATTR_LIST attribute that is non-resident
    std::optional<bool> NonResidentBitmap = std::nullopt;   // rare case. has an BITMAP attribute that is non-resident  
    bool HasResidentDataAttr{ false };      // Has resident DATA attribute. Not so rare case.
    bool HasNonResidentDataAttr{ false }; // Has non-resident DATA attribute
    uint32_t FileAttrib{ 0 };

    uint16_t HardLinksCount{ 0 };
    uint16_t AttrsCount{ 0 };
    uint64_t DataLCNsCount{ 0 }; // how many LCNs the file uses. filled for non-resident data attributes only
    uint32_t FilesCount{ 0 }; // valid for directoriy records only. number of dirs/files in a directory.
    std::wstring MainName; 
    THArray<std::wstring> FileNames; // contains filenames of all types - DOS, WIN and POSIX
    THash<std::wstring, TDataRuns> DataStreamNames; // data stream name counts groupped by stream name
  
    DIR_NODE Node;

    /*bool operator<(const ITEM_INFO& other) const
    { 
        return MainName < other.MainName;
    }*/

    bool operator==(const ITEM_INFO& other) const { return RecID.Id == other.RecID.Id; }

    bool IsDir() const { return (FileAttrib & (uint32_t)FILE_ATTR_FLAGS::DIRECTORY) > 0; }
    //bool IsMetaFile() const { return (RecID.sId.low == MFT_ROOT_REC_ID) && (ciName[0] == L'$'); }
    //bool IsDotDir() const { return (ciName.size() == 1) && (ciName[0] == L'.'); }
    //bool NtfsInternal() const { return IsMetaFile() || IsDotDir(); }

    //ITEM_INFO() {};
    //ITEM_INFO(const ITEM_INFO&) = delete;
    //ITEM_INFO& operator=(const ITEM_INFO&) = delete;

};

// assert below fails and this is the reason why sorting of array of ITEM_INFO takes too much time
// sort function swaps items and for such big structure as ITEM_INFO this is costly operation
//static_assert(std::is_nothrow_move_constructible_v<ITEM_INFO>);

#if _DEBUG
static_assert(sizeof(ITEM_INFO) == 336);
#else
static_assert(sizeof(ITEM_INFO) == 328);
#endif

// FixupUSA1 - corrupted data
// MFT record is not in use
// incorrect parameters in function call
// incorrect data in data runs bytes
// LoadMFTRecordCache returned nullptr
// ReadClusters failed
// LoadMFTRecord failed
// DeviceIoControl failed
// DeviceIoControl returned another MFT record than requested
// 
enum class TErrorCode
{
    Success,
    MFTRecordNotInUse,
    WrongMFTRecID,
    InvalidArgument,
    CorruptedData, // mostly generated by FixupUSA and DecodeDataRuns
    IOError, // generated mostly by ReadClusters, LoadMFTRecord and DeviceIoControl
    NotFound,

    TErrorCodeCount // defines count of items in this enum
};

#define ERROR_CODE_NAMES { "Success", "MFTRecordNotInUse", "WrongMFTRecID", "InvalidArgument", "CorruptedData", "IOError", "NotFound" }
static const char* ErrorCodeNames[(uint8_t)TErrorCode::TErrorCodeCount] ERROR_CODE_NAMES;

inline void PrintTo(const TErrorCode& code, std::ostream* os)
{
    *os << ErrorCodeNames[(uint8_t)code];
}

typedef THArray<ITEM_INFO> TItemInfoList;

typedef std::function<void(const ATTR_FILE_NAME*, const MFT_REF&)> AddFileAttrPred;
typedef std::function<TErrorCode(const MFT_REF&)> AttrListPred;
typedef std::function<void(uint8_t* dataBuf, CLST VCN, CLST LCN)> ProcessiBlocksPred;

typedef int32_t (__stdcall *ProgressCallbackPtr)(int32_t progress);

struct VOLUME_DATA : public NTFS_VOLUME_DATA_BUFFER
{
    DWORD& BytesPerMFTRec; // alias to field = NTFS_VOLUME_DATA_BUFFER::BytesPerFileRecordSegment
    HANDLE hVolume;
    std::wstring Name;

    VOLUME_DATA(const VOLUME_DATA&) = delete;
    VOLUME_DATA& operator=(const VOLUME_DATA&) = delete;

    VOLUME_DATA() : NTFS_VOLUME_DATA_BUFFER{0}, BytesPerMFTRec(this->BytesPerFileRecordSegment), hVolume(INVALID_HANDLE_VALUE)
    {
    }

};

class TAttrCollection
{
private:
    typedef THArray<MFT_ATTR_HEADER*> THArrayAttrHeader;
    THArray<THArrayAttrHeader> FAttrList;
public:
    TAttrCollection()
    {
        FAttrList.SetCount(ATTR_TYPE_CNT);
        FAttrList.Zero(); // needed to check which attrs are not present (will be nulls in FAttrList)
    }
    
    void Set(MFT_ATTR_HEADER* attr)
    {
        assert(attr->AttrType <= ATTR_LOGGED_UTILITY_STREAM); // this is last attribute in list of attr types

        // check that single attributes are single
        if (SingleAttributes[MATI(attr->AttrType)]) 
            assert(FAttrList[MATI(attr->AttrType)].Count() == 0);

        FAttrList[MATI(attr->AttrType)].AddValue(attr);
    }

    THArrayAttrHeader& Get(ATTR_TYPE attrType)
    {
        //assert((attrType != ATTR_FILENAME) && (attrType != ATTR_DATA) && (attrType != ATTR_LOGGED_UTILITY_STREAM));
        return FAttrList[MATI(attrType)];
    }

};

LogEngine::Logger& GetLoggerFunc();
string_t FileDateToString(const string_t& str, uint64_t dateTime);
std::string FormatFileAttributes(uint32_t a);
MFTRecIndex StringToMFTRecID(const string_t& strMFTRecID);
string_t GetVolumeName(const string_t& path);

// removes all leading and trailing \n\t\r and space symbols from string
//std::wstring TrimSPCRLF(std::wstring str);
