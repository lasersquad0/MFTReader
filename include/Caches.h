#pragma once

#include "Debug.h"
#include <cstdint>
#include "logengine2/DynamicArrays.h"
#include "NTFS.h"
#include "Functions.h" // for TErrorCode



/**
* @brief This class stores MFT records in memory and gets them by MFT Rec ID
* @details It is used by LoadMFTRecordCache to prevent loading the same record from disk several times.
* The difference from standard THash is that TMFTRecCache frees MFT records memory in its destructor.
* MFT record is defined as uint8_t* type 
* MFT rec ID is defined as MFTRecIndex type which is equivalent to uint32_t
**/
class TMFTRecCache : public THash<MFTRecIndex, uint8_t*>
{
public:
    ~TMFTRecCache() override
    {
        for (uint32_t i = 0; i < FAValues.Count(); i++) delete[] FAValues[i];
    }
};

/*
class TMFTRecs
{
public:
    THArrayRaw FRecs; // storage for MFT records (pieces of memory)
    THArraySorted<MFTRecIndex, CompareReverse<MFTRecIndex>> FKeys;
    THArray<uint8_t*> FValues;
public:
    using rec_type = decltype(FValues)::item_type;

    TMFTRecs() {}
    TMFTRecs(uint32_t recSize, uint32_t capacity) : FRecs(recSize) { SetCapacity(capacity); }

    void SetCapacity(uint32_t capacity) 
    { 
        FRecs.SetCapacity(capacity); 
        FKeys.SetCapacity(capacity);
        FValues.SetCapacity(capacity);

        assert(FRecs.Count() == FKeys.Count());
        assert(FRecs.Count() == FValues.Count());
        assert(FRecs.Capacity() == FKeys.Capacity());
        assert(FRecs.Capacity() == FValues.Capacity());
    }
    void SetRecordSize(uint32_t recSize) 
    { 
        FRecs.Clear();
        FKeys.Clear(); 
        FValues.Clear();
        FRecs.SetItemSize(recSize);

        assert(FRecs.GetItemSize() == recSize);
        assert(FRecs.Count() == 0);
        assert(FKeys.Count() == 0);
        assert(FValues.Count() == 0);
    }
    uint Count() { assert(FRecs.Count() == FKeys.Count()); assert(FRecs.Count() == FValues.Count()); return FRecs.Count(); }
    void Clear() { FRecs.Clear(); FKeys.Clear(); FValues.Clear(); }

    // copies to the internal storeage MFT record pointed by mftRecData 
    // returns pointer to the MFT record from FRecs storage, it differs from mftRecData pointer
    // mftRecData pointer and data left unchanged
    //TODO contains a problem, if we want to delete one record from cache, all pointers in FHash become "invalid"
    // solution - delete record from FHash but DO NOT delete anything from FRecs
    uint8_t* AddRec(MFTRecIndex mftRecID, uint8_t* mftRecData)
    {
        uint32_t index = FRecs.Add(mftRecData);
        uint8_t* p = FRecs.GetPointer(index);

        //TODO before adding new value FHash searches for mftRecID in its internal Keys array, that takes a time. 
        // If we know that mnftRecID is unique, we should avoid searching Keys each time
        //FHash.SetValue(mftRecID, p);
        FKeys.AddValue(mftRecID);
        //FValues.AddValue(p);

        assert(FRecs.Count() == FKeys.Count());
        //assert(FRecs.Count() == FValues.Count());

        return p;
    }

    rec_type GetRec(MFTRecIndex mftRecID)
    {
        int n = FKeys.IndexOf(mftRecID);
        if (n == -1)
            return nullptr;
        
        return FValues.GetValue(n);
    }

    TErrorCode GetRec(MFTRecIndex mftRecID, rec_type pValue)
    {
        int n = FKeys.IndexOf(mftRecID);
        if (n == -1)
            return TErrorCode::NotFound;

        auto res = memcpy(pValue, FValues[n], FRecs.GetItemSize());
        assert(res == pValue);

        return TErrorCode::Success;
    }
};*/

/**
* @brief Memory storage for LCN records.
* @details LCNs are either part of ALLOC/ATTR_LIST attributes and contain lists of files/list of ATTR_LIST entries or part of DATA attribute and contain file data
* This memory storage contains all LCN records loaded into memory for ATTR_LIST/ALLOC/DATA attributes
* Possibility to get LCN record by LCN number of by VCN number
**/
class TLCNRecs
{
private:
    THArrayRaw FRecs; // storage for LCN records (pieces of memory)
    THash2<CLST, CLST, uint8_t*> FHash; // mapping mft records to VCNs and LCNs
public:
    using rec_type = decltype(FHash)::ValuesHash::iterator::value_type;
    TLCNRecs(uint32_t recSize, uint32_t capacity) : FRecs(recSize) { SetCapacity(capacity); }

    void SetCapacity(uint32_t capacity)  { FRecs.SetCapacity(capacity); FHash.SetCapacity(capacity); }
    void SetRecordSize(uint32_t recSize) { FRecs.SetItemSize(recSize); FHash.Clear(); }
    uint Count() { assert(FRecs.Count() == FHash.Count()); return FRecs.Count(); }

    // copies to the internal hash LCN record pointed by lcnRecData 
    // returns pointer to the LCN record from FRecs storage, it differs from lcnRecData pointer
    // lcnRecData pointer and data become unchanged
    //TODO contains a problem, if we want to delete one record from cache, all pointers in FHash become "invalid"
    // solution - delete record from FHash but DO NOT delete anything from FRecs
    uint8_t* AddRec(uint8_t* lcnRecData, CLST VCN, CLST LCN)
    {
        uint32_t index = FRecs.AddValue(lcnRecData);
        uint8_t* p = (uint8_t*)FRecs.GetValuePointer(index);
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

    /** 
    * @brief Loads LCN records into memory from Data Runs located in 'node' parameter
    * @details Node.Bitmap defines what LCNs need to be loaded and what are not
    * @param node Contains both Data Runs to be loaded into memory and Bitmap bitfield that defines 
    * which VCNs from Data Runs contains valid info and need to be loaded
    * @return Number of VCN's actually loaded into memory (may be zero if Bitmap contains only zeroes) or -1  in case of error.
    */
    /*
    int64_t LoadDataRuns(const VOLUME_DATA& volData, DIR_NODE& node)
    {
        assert(FRecs.GetItemSize() == volData.BytesPerCluster);

        GET_LOGGER_FUNC;

        int64_t lastBit = node.Bitmap.LastBit();

        if (lastBit == -1)
        {
            logger.InfoFmt("[TMFTRecCache.LoadDataRuns] Bitmap contains all zeroes, so no LCNs loaded. Data Runs Count: {}", node.DataRuns.Count());
            return 0; // here bitmap tells us that no LCNs need to be loaded
        }

        int64_t LCNCounter = 0;
        uint8_t* dataBuf = nullptr;
        uint64_t dataBufLen = 0; // dataBuf buffer size in clusters, how many clusters is allocated in dataBuf
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
                return -1;
            }

            if (!FixupUSA(volData, dataBuf, rli, rlilen))
            {
                logger.Error("FixupUSA returned error.");
                delete[] dataBuf;
                return -1;
            }

            for (size_t i = 0; i < rlilen; i++)
            {
                if (node.Bitmap.Test(LCNCounter++)) // add only LCNs which are marked in bitmap bitfield
                {
                    //TODO shall we bypass LCN records which DO NOT have 'INDX' signature ( if (ntfs_is_indx_recp(allocIndex->RecHeader.Signature)) .....)
                    AddRec(dataBuf + i * volData.BytesPerCluster, rli.vcn + i, rli.lcn + i);
                }
                else
                {
                    logger.InfoFmt("[TMFTRecCache.LoadDataRuns] Bypassing this LCN because of Bitmap: VCN: {}, LCN : {}", rli.vcn + i, rli.lcn + i);
                }
            }

            currRun++;
        }

        delete[] dataBuf;

        return LCNCounter;
    }*/
};



