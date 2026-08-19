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
    THash2<uint64_t, uint64_t, uint8_t*> FHash; // mapping mft records to VCNs and LCNs
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
    uint8_t* AddRec(uint8_t* lcnRecData, uint64_t VCN, uint64_t LCN)
    {
        uint32_t index = FRecs.AddValue(lcnRecData);
        uint8_t* p = (uint8_t*)FRecs.GetValuePointer(index);
        FHash.SetValue(VCN, LCN, p);
        return p;
    }

    rec_type GetRecByVCN(uint64_t VCN)
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

};



