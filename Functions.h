#pragma once

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#ifdef _DEBUG
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
// Replace _NORMAL_BLOCK with _CLIENT_BLOCK if you want the allocations to be of _CLIENT_BLOCK type
#else
#define DBG_NEW new
#endif

#include "Logger.h"
#include "MTFReader.h"
#include "DynamicArrays.h"
#include "ci_string.h"

#define SAME_ATTR_CNT 5
#define MFT_LOGGER_NAME "mftlog"
#define MFT_LOGGER_NAME_FUNC "mftlogfunc"
#define MFT_LOGGER_NAME_LIST "mftlist"

#define GET_LOGGER LogEngine::Logger& logger = LogEngine::GetLogger(MFT_LOGGER_NAME)
#define GET_LOGGER_FUNC LogEngine::Logger& logger = GetLoggerFunc()

#define IsDir(_) (((_).dup.FileAttrib & (uint32_t)FILE_ATTR_FLAGS::DIRECTORY) > 0)
#define IsReparse(_) (((_).dup.FileAttrib & (uint32_t)FILE_ATTR_FLAGS::REPARSE_POINT) > 0)
#define IsMetaFile(_) (((_).Attr.ParentDir.sId.low == 5) && ((_).ciName[0] == L'$'))
#define IsDotDir(_) ( ((_)[0] == L'.') && ((_).size() == 1) )

#define ATTR_TYPE_NAMES { L"ZERO", L"STANDARD INFO", L"ATTR LIST", L"FILENAME", L"OBJECT ID", L"secure_info", L"LABEL", \
                          L"VOLUME INFO", L"DATA", L"INDEX ROOT", L"ALLOCATION", L"BITMAP", L"REPARSE", L"EA_INFORMATION", \
                          L"EA", L"PROPERTYSHEET", L"UTILITY STREAM" }

// Attr types have numbers 0x10 0x20 0x100, etc. - convert them into consecutive indexes in the array
// used for indexing ATTR_TYPE_NAMES array and in some other places
#define MakeAttrTypeIndex(_) ((_)>>4) 
#define MATI MakeAttrTypeIndex

static const wchar_t* AttrTypeNames[] ATTR_TYPE_NAMES;

static const wchar_t* FileNameTypes[]{ L"POSIX", L"UNICODE", L"DOS", L"UNICODE_AND_DOS" };

struct VOLUME_DATA
{
    HANDLE hVolume;
    LARGE_INTEGER TotalClusters;
    DWORD BytesPerCluster;
    DWORD BytesPerMFTRec;
    DWORD BytesPerSector;
};

class TBitField
{
private:
    uint64_t* FBits;
    uint64_t FCount;
    uint64_t FBitsCount;
public:
    TBitField()
    {
        FBits = nullptr;
        FCount = 0;
        FBitsCount = 0;
    }

    TBitField(const uint64_t* bits, const uint64_t wordsCount): TBitField() // count is in uint64_t words here
    {
        SetData(bits, wordsCount);
    }

    ~TBitField() { delete[] FBits; FBits = nullptr; }

    void SetData(const uint64_t* bits, const uint64_t wordsCount) // count is in uint64_t words here
    {
        delete[] FBits; // free previously allocated memory if any
        FBits = DBG_NEW uint64_t[wordsCount];
        FCount = wordsCount;
        FBitsCount = wordsCount * 64; 
        assert(!memcpy_s(FBits, wordsCount * sizeof(uint64_t), bits, wordsCount * sizeof(uint64_t)));
    }

    TBitField& operator=(const TBitField& a) // copy constructor
    {
        SetData(a.FBits, a.FCount);
        return *this;
    }

    uint8_t* GetData()
    {
        return (uint8_t*)FBits;
    }

    uint64_t Count() const { return FCount; }

    void Clear() 
    {
        delete[] FBits;
        FBits = nullptr;
        FCount = 0;
        FBitsCount = 0;
    }

    // true if bit=1, false if bit=0
    bool Test(uint64_t bitIndex)
    {
        if (bitIndex >= FBitsCount) throw std::runtime_error("Index out of bounds");

        uint64_t wordIndex = bitIndex >> 6; // divide to 64 = 2^6
        if ( ((FBits[wordIndex] >> (bitIndex % 64)) & 1ull) == 1ull) return true;
        return false;
    }

    int64_t LastBit()
    {
        if (FBitsCount == 0) return -1;

        int64_t bitIndex = FBitsCount - 1;
        for (auto word = FBits + FCount - 1; word >= FBits; --word)
        {
            uint64_t bitWord = *word;
            if (bitWord == 0) { bitIndex -= 64; continue; }

            for (int i = 63; i >= 0; --i)
            {
                if(bitWord >> i) return bitIndex; //we've met 1
                bitIndex--;
            }
        }

        assert(bitIndex + 1 == 0);
        return bitIndex;
    }
};

typedef MFT_ATTR_HEADER* PMFT_ATTR_HEADER;

struct FILE_NAME
{
    ci_string ciName;
    struct ATTR_FILE_NAME Attr;
    struct MFT_REF MFTRef; // MFT Id of this file

    //bool operator>(const FILE_NAME& other) const { return ciName > other.ciName; }
    bool operator<(const FILE_NAME& other) const { return ciName < other.ciName; }
    bool operator==(const FILE_NAME& other) const { return ciName == other.ciName; }
};

typedef THArray<FILE_NAME> TFileList;
typedef THArray<DATA_RUN_ITEM> TDataRuns;

struct DIR_NODE
{
    TFileList FileList; // filled from INDEX_ROOT attribute and then from ALLOCATE after processing data runs 
    TDataRuns DataRuns; // from ALLOCATE attribute
    TBitField Bitmap;   // tells us which LCNs from data runs are valid ones

    void Clear() 
    {
        FileList.Clear();
        DataRuns.Clear();
        Bitmap.Clear();
    }
};

struct ITEM_INFO
{
    MFT_REF RecID;
    bool HasAttr[ATTR_TYPE_CNT];
    bool NonResidentAttrList;
    bool NonResidentBitmap;
    bool NonResidentAlloc;
    bool ResidentData;

    uint AttrsCount;
    uint NamesCount; // DOS, WIN, POSIX
    uint DataStreamsCount;
    std::wstring FileName;
    std::wstring DataStreamNames[5]; // unlikely that file will have more than 5 file streams
    
    DIR_NODE Node;

    bool operator==(const ITEM_INFO& other) const { return RecID.Id == other.RecID.Id; }
};

typedef THArray<ITEM_INFO> TItemInfoList;


LogEngine::Logger& GetLoggerFunc();
bool ParseNonresBitmap(const VOLUME_DATA& volData, MFT_ATTR_HEADER* attr, TBitField& bitmap);
void ParseNonresAttrList(const VOLUME_DATA& volData, MFT_ATTR_HEADER* attrAttrList, ATTR_TYPE attrType, PMFT_ATTR_HEADER* result);
bool ReadDirectoryX(VOLUME_DATA& volData, MFT_REF mftRecID, uint32_t dirLevel, THArray<FILE_NAME>& gDirList);
bool DataRunsDecode(MFT_ATTR_HEADER* attr, THArray<DATA_RUN_ITEM>& runs);
bool ReadClusters(const VOLUME_DATA& volData, uint64_t lcnStart, uint32_t lcnCnt, PBYTE dataBuf);
bool FixupUSA(const VOLUME_DATA& volData, PBYTE dataBuf, DATA_RUN_ITEM& rli, uint32_t rlilen);
bool LoadMFTRecord(const VOLUME_DATA& volData, MFT_REF recID, uint8_t* mftRec);
uint8_t* LoadMFTRecordCache(const VOLUME_DATA& volData, MFT_REF recID);
void GetAttr(const VOLUME_DATA& volData, ATTR_TYPE attrType, const PMFT_ATTR_HEADER* const attrValues, PMFT_ATTR_HEADER* result);
void FillAttrValues(MFT_FILE_RECORD* mftRec, PMFT_ATTR_HEADER* attrValues);

// This class stores MFT records in memory and gets them by MFT rec ID
// MFT record is defined as uint8_t* type 
// MFT rec ID is uint32_t type
// the difference from standard THash is that TMFTRecCache frees MFT records memory in its destructor.
class TMFTRecCache : public THash<uint64_t, uint8_t*>
{
public:
    ~TMFTRecCache() override
    {
        for (uint i = 0; i < FAValues.Count(); i++)
        {
            delete[] FAValues[i];
        }
    }
};

// Memory storage for LCN records that contains list of files
// contains all LCN records loaded into memory defined by "data runs" of ALLOC attribute
// get LCN record by LCN number of by VCN number
class TLCNRecs
{
private:
    THArrayRaw FRecs; // storage for LCN records 
    THash2<CLST, CLST, uint8_t*> FHash; // mapping mft records to VCNs and LCNs
public:
    using rec_type = decltype(FHash)::ValuesHash::iterator::value_type;
    TLCNRecs(uint recSize, uint capacity) : FRecs(recSize) { SetCapacity(capacity); }

    void SetCapacity(uint capacity) { FRecs.SetCapacity(capacity); FHash.SetCapacity(capacity); }
    void SetRecordSize(uint recSize) { FRecs.SetItemSize(recSize); }

    // adds LCN record to the hash
    // returns pointer to the LCN record from FRecs storage, it differs from mftRecData pointer
    uint8_t* AddRec(uint8_t* lcnRecData, CLST VCN, CLST LCN)
    {
        uint index = FRecs.Add(lcnRecData);
        uint8_t* p = (uint8_t*)FRecs.GetPointer(index);
        FHash.SetValue(VCN, LCN, p);
        return p;
    }

    rec_type GetRecByVCN(CLST VCN) 
    { 
        auto& LCNHash = FHash.GetValue(VCN); 
        
        assert(LCNHash.Count() == 1);

        return *LCNHash.begin();
    }

    /*CLST GetLCNByVCN(CLST VCN)
    {
        auto& LCNHash = FHash.GetValue(VCN);

        assert(LCNHash.Count() == 1);

        return (*LCNHash.begin()).first;
    }*/

    //loads LCN records into memory from data runs located in node parameter
    bool LoadDataRuns(const VOLUME_DATA& volData, DIR_NODE& node)
    {
        assert(FRecs.GetItemSize() == volData.BytesPerCluster);

        GET_LOGGER_FUNC;

        int64_t lastBit = node.Bitmap.LastBit();

        if (lastBit == -1) return true; // here bitmap tells us that no LCNs need to be loaded

        int64_t LCNCounter = 0;
        uint8_t* dataBuf = nullptr;
        uint32_t dataBufLen = 0; // memory size in clusters, how many clusters is allocated in dataBuf
        uint32_t currRun = 0;
        while (currRun < node.DataRuns.Count())
        {
            if (LCNCounter > lastBit) // stop loading by bitmap field
                  break;

            DATA_RUN_ITEM& rli = node.DataRuns[currRun];
            //logger.DebugFmt("Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

            uint32_t rlilen = valuemin((uint32_t)(lastBit + 1 - LCNCounter), rli.len);

            if (rlilen > dataBufLen)
            {
                delete[] dataBuf;
                dataBuf = DBG_NEW uint8_t[rlilen * volData.BytesPerCluster];
                dataBufLen = rlilen;
            }

            if (!ReadClusters(volData, rli.lcn, rlilen, dataBuf)) // ReadCluster writes error meesage to log file in case of an error
            {
                delete[] dataBuf;
                return false;
            }

            if (!FixupUSA(volData, dataBuf, rli, rlilen))
            {
                logger.Error("FixupUSA returned error.");
                delete[] dataBuf;
                return false;
            }

            for (size_t i = 0; i < rlilen; i++)
            {
                if (node.Bitmap.Test(LCNCounter++)) // add only LCNs which are marked in bitmap bit field
                {
                    AddRec(dataBuf + i * volData.BytesPerCluster, rli.vcn + i, rli.lcn + i);
                }
            }
            
            currRun++;
        }

        delete[] dataBuf;

        return true;
    }
};

void GetFileListFromNode(INDEX_HDR* ihdr, TLCNRecs& lcns, THArray<FILE_NAME>& fnames);
