#pragma once

#include <functional>
#include <optional>
#include <windows.h>
#include <winioctl.h>

#include "strutils/include/ci_string.h"
#include "logengine2/LogEngine.h"
#include "logengine2/DynamicArrays.h"
#include "NTFS.h"
#include "BitField.h"


#define CH_ERR(_res_) do { \
   TErrorCode __ = (_res_); \
    if ((__) != TErrorCode::Success) \
        return (__); \
    } while(0)

constexpr uint32_t F_WIDTH = 22;
const size_t MFT_LINE_LEN = 57;
const size_t ATTR_LINE_LEN = MFT_LINE_LEN - 6;

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
#define ATTR_TYPE_NAMES { "ZERO", "STANDARD_INFO", "ATTR_LIST", "FILENAME", "OBJECT_ID", "SECURITY_INFO", "LABEL", \
                          "VOLUME_INFO", "DATA", "INDEX_ROOT", "ALLOCATION", "BITMAP", "REPARSE", "EA_INFORMATION", \
                          "EA", "PROPERTYSHEET", "LOGGED_UTIL_STREAM", "USER_ATTRIBUTE" }

#define GetResidenceName(_attr_) ((_attr_->NonResidentFlag == ATTR_FLAG_NONRESIDENT) ? "NON-RESIDENT" : "RESIDENT")

// Attr types have numbers 0x10, 0x20, 0x30, etc. - convert them into consecutive indexes in the array
// used for indexing ATTR_TYPE_NAMES array and in some other places
#define MakeAttrTypeIndex(_) ((_)>>4) 
#define MATI(_) ((_)>>4) 
#define AttrName(_) (AttrTypeNames[(_)>>4])
#define MakeAttrBitmask(_) (1<<((_)>>4))
#define MakeCollationRuleIndex(_) ((_)==0?0:(_)==1?1:(_)==0x10?2:(_)==0x11?3:(_)==0x12?4:(_)==0x13?5:6) 
#define CollRuleName(_) (CollationRuleNames[MakeCollationRuleIndex(_)])

static const char* AttrTypeNames[ATTR_TYPE_CNT] ATTR_TYPE_NAMES;
static const char* FileNameTypes[]{ "POSIX", "UNICODE", "DOS", "UNICODE_AND_DOS" };
static const char* CollationRuleNames[]{ "BINARY",  "FILENAME", "UINT", "SID",  "SECURITY_HASH", "UINTS", "UNKNOWN"};

struct IFILE_NAME
{
    ci_string ciName;
    struct ATTR_FILE_NAME Attr { 0 };
    struct MFT_REF MFTRecID{0}; // MFT Rec ID of this file

    // we need these two operators for THArray<> storage and for std::lower_bound in MFTRecIdByPath method.
    bool operator<(const IFILE_NAME& other) const { return ciName < other.ciName; }
    bool operator==(const IFILE_NAME& other) const { return ciName == other.ciName; }

    bool IsDir() const { return (Attr.dup.FileAttrib & (uint32_t)FILE_ATTR_FLAGS::DIRECTORY) > 0; }
    //bool IsMetaFile() const { return (Attr.ParentDir.sId.low == MFT_ROOT_REC_ID) && (ciName[0] == L'$'); } // assumes ciName is not empty
    //bool IsDotDir() const { return (ciName.size() == 1) && (ciName[0] == L'.'); } 
    bool IsReparse() const { return (Attr.dup.FileAttrib & (uint32_t)FILE_ATTR_FLAGS::REPARSE_POINT) > 0; };
    //bool NtfsInternal() const { return IsMetaFile() || IsDotDir(); }
};

#if _DEBUG
static_assert(sizeof(IFILE_NAME) == 120); 
#else
static_assert(sizeof(IFILE_NAME) == 112);
#endif

typedef THArray<IFILE_NAME> TFileList;
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
    MFT_REF MFTRecID{ 0 };
    MFT_REF ParentDir{ 0 };
    std::wstring MainName;
    uint32_t FileAttrib{ 0 };
    uint32_t FilesCount{ 0 }; // valid for directoriy records only. number of dirs/files in a directory.
    uint16_t HardLinksCount{ 0 };
    uint64_t DataLCNsCount{ 0 }; // how many LCNs the file uses. filled for non-resident DATA attributes only
    uint16_t AttrsCount{ 0 };
    uint16_t AttrCounters[ATTR_TYPE_CNT]{ 0 };

    std::optional<bool> NonResidentAttrList = std::nullopt; // rare case. has an ATTR_LIST attribute that is non-resident
    std::optional<bool> NonResidentBitmap = std::nullopt;   // rare case. has an BITMAP attribute that is non-resident  
    bool HasResidentDataAttr{ false };      // Has resident DATA attribute. Not so rare case.
    bool HasNonResidentDataAttr{ false }; // Has non-resident DATA attribute

    THArray<IFILE_NAME> FileNames; // contains filenames of all types - DOS, WIN and POSIX
    THash<std::wstring, std::optional<TDataRuns>> DataStreamNames; // data stream name counts groupped by stream name
  
    DIR_NODE Node;

    //operator == is required for storing this structure in THArray<>
    bool operator==(const ITEM_INFO& other) const { return MFTRecID.Id == other.MFTRecID.Id; }

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
static_assert(sizeof(ITEM_INFO) == 344);
#else
static_assert(sizeof(ITEM_INFO) == 336);
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

// this is for Google Tests to proper print error codes during running tests
inline void PrintTo(const TErrorCode& code, std::ostream* os)
{
    *os << ErrorCodeNames[(uint8_t)code];
}

typedef THArray<ITEM_INFO> TItemInfoList;

typedef std::function<void(const ATTR_FILE_NAME*, const MFT_REF&)> AddFileAttrPred;
typedef std::function<TErrorCode(const MFT_REF&)> AttrListPred;
typedef std::function<void(uint8_t* dataBuf, uint64_t VCN, uint64_t LCN)> ProcessiBlocksPred;

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

typedef THArray<MFT_ATTR_HEADER*> TAttrHeaderList;

class TAttrCollection
{
private:
    THArray<TAttrHeaderList> FAttrList;
    THashUnordered<MFT_ATTR_HEADER*, MFTRecIndex> FReadFrom;
public:
    TAttrCollection()
    {
        FAttrList.SetCount(ATTR_TYPE_CNT);
    }

    //auto begin() { return FAttrList.begin(); }
    //auto end()   { return FAttrList.end(); }

    // readFrom - MFT Rec ID (eaither BASE or CHILD) where attribute attr is located
    void Set(MFT_ATTR_HEADER* attr, MFTRecIndex readFrom)
    {
        assert(attr->AttrType <= ATTR_LOGGED_UTILITY_STREAM); // this is last attribute in list of attr types
        assert(attr->AttrType != ATTR_LIST_ATTR);
        assert(attr->AttrType != ATTR_ZERO);

        // check that single attributes are single
        if (SingleAttributes[MATI(attr->AttrType)]) 
            assert(FAttrList[MATI(attr->AttrType)].Count() == 0);

        FAttrList[MATI(attr->AttrType)].AddValue(attr);
        FReadFrom.SetValue(attr, readFrom);
    }

    TAttrHeaderList& Get(ATTR_TYPE attrType)
    {
        assert(attrType <= ATTR_LOGGED_UTILITY_STREAM); // this is last attribute in list of attr types
        assert(attrType != ATTR_LIST_ATTR);
        assert(attrType != ATTR_ZERO);

        return FAttrList[MATI(attrType)];
    }

    // returns location of attribute attr 
    // location this is a MFT Rec ID where attribute attr is actually located
    // it may be CHILD MFT record when its BASE record contains ATT_LIST attribute
    MFTRecIndex GetLoc(MFT_ATTR_HEADER* attr)
    {
        return FReadFrom[attr];
    }

};

LogEngine::Logger& GetLoggerFunc();
string_t FileDateToString(uint64_t dateTime);
std::string FormatFileAttributes(uint32_t a);
MFTRecIndex StringToMFTRecID(const string_t& strMFTRecID);
string_t GetVolumeName(const string_t& path);
string_t AttrFlagToString(uint32_t attrFlag); 

// removes all leading and trailing \n\t\r and space symbols from string
//std::wstring TrimSPCRLF(std::wstring str);


enum class TProgressResult
{
    OK,
    ABORT,
    PAUSE
};

class IProgress
{
protected:
    uint32_t FP100 = 100;
public:
    virtual ~IProgress() {};
    virtual void Start(uint32_t p100) { FP100 = p100; };
    virtual void Finish() {};
    /** Return values:
    * 0 - user aborted the process. need to stop/abort current operation
    * 1 - everything is ok, continue operation
    * 2 - pause operation (call method progess() with the last parameter every 1 second until any other value than 2 returned)
    */
    virtual TProgressResult Progress(uint32_t prgrs, string_t data) { UNREFERENCED_PARAMETER(data); UNREFERENCED_PARAMETER(prgrs); return TProgressResult::OK; };

};

/**
* This class used for showing ONLY progress on the console
* It shows one line without '\n' at the end and updates info on this line each time when progress() method is called
*/
class ConsoleProgress : public IProgress
{
protected:
    ostream_t& FCout;
public:
    ConsoleProgress(ostream_t& console) : FCout(console) {}

    void Start(uint32_t p100) override { IProgress::Start(p100); }
    void Finish() override 
    {
        Progress(FP100, _T("DONE"));
        std::cout << std::endl << std::endl; 
    }

    TProgressResult Progress(uint32_t prgrs, string_t data) override
    {
        const uint32_t LINE_LEN = 100; //100 symbols in console
        const uint32_t FOR_FILE_NM = 20;

        uint32_t realProgress = prgrs * LINE_LEN / FP100;
        uint32_t figureProgress = prgrs * 100 / FP100;

        //string_t str(_T(''\033[32m'));
        string_t str;
        str.reserve(LINE_LEN);
        str.append(realProgress, L'\u2588');
        str.append(abs((int32_t)LINE_LEN - (int32_t)str.size()), L'\u2591');
        //str.append(_T("\033[0m"));

        cout_t << _T("\r ") << str << std::format(_T(" {}% ({:.{}}){:<{}}"), figureProgress, data, FOR_FILE_NM, _T(""), (FOR_FILE_NM > data.size() ? FOR_FILE_NM - data.size() : 0));

        return TProgressResult::OK;  // not used at the moment
    }
};