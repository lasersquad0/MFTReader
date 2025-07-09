#pragma once

#include "Debug.h"
#include <cstdint>
#include "logengine2/DynamicArrays.h"
#include "NTFS.h"
#include "Functions.h"



// This class stores MFT records in memory and gets them by MFT Rec ID
// It used as singleton in LoadMFTRecordCache to prevent loading the same record several times.
// The difference from standard THash is that TMFTRecCache frees MFT records memory in its destructor.
// MFT record is defined as uint8_t* type 
// MFT Rec ID is uint32_t type
class TMFTRecCache : public THash<uint64_t, uint8_t*>
{
public:
    ~TMFTRecCache() override
    {
        for (uint32_t i = 0; i < FAValues.Count(); i++) delete[] FAValues[i];
    }
};

// Memory storage for LCN records. 
// LCNs are part of ALLOC attribute and contain lists of files
// This memory storage contains all LCN records loaded into memory and defined by "data runs" of ALLOC attribute
// Possible to get LCN record by LCN number of by VCN number
class TLCNRecs
{
private:
    THArrayRaw FRecs; // storage for LCN records (pieces of memory)
    THash2<CLST, CLST, uint8_t*> FHash; // mapping mft records to VCNs and LCNs
public:
    using rec_type = decltype(FHash)::ValuesHash::iterator::value_type;
    TLCNRecs(uint32_t recSize, uint32_t capacity) : FRecs(recSize) { SetCapacity(capacity); }

    void SetCapacity(uint32_t capacity)  { FRecs.SetCapacity(capacity); FHash.SetCapacity(capacity); }
    void SetRecordSize(uint32_t recSize) { FRecs.SetItemSize(recSize); }

    // adds LCN record to the hash
    // returns pointer to the LCN record from FRecs storage, it differs from mftRecData pointer
    uint8_t* AddRec(uint8_t* lcnRecData, CLST VCN, CLST LCN)
    {
        uint32_t index = FRecs.Add(lcnRecData);
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
        uint64_t dataBufLen = 0; // memory size in clusters, how many clusters is allocated in dataBuf
        uint32_t currRun = 0;
        while (currRun < node.DataRuns.Count())
        {
            if (LCNCounter > lastBit) // stop loading by bitmap field
                break;

            DATA_RUN_ITEM& rli = node.DataRuns[currRun];
            //logger.DebugFmt("Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

            CLST rlilen = valuemin((CLST)(lastBit + 1 - LCNCounter), rli.len);

            if (rlilen > dataBufLen)
            {
                delete[] dataBuf;
                dataBuf = DBG_NEW uint8_t[rlilen * volData.BytesPerCluster];
                dataBufLen = rlilen;
            }

            if (!ReadClusters(volData, rli.lcn, rlilen, dataBuf)) // ReadClusters writes error meesage to log file in case of an error
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
                if (node.Bitmap.Test(LCNCounter++)) // add only LCNs which are marked in bitmap bitfield
                {
                    //TODO shall we bypass LCN records which DO NOT have 'INDX' signature ( if (ntfs_is_indx_recp(allocIndex->RecHeader.Signature)) .....)
                    AddRec(dataBuf + i * volData.BytesPerCluster, rli.vcn + i, rli.lcn + i); //TODO oprtimization - avoid of multiply operator in first parameter of AddRec
                }
            }

            currRun++;
        }

        delete[] dataBuf;

        return true;
    }
};



