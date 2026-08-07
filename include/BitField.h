#pragma once

#include "Debug.h"
#include <cassert>
#include <cstdlib>
#include <stdexcept>

class TBitField
{
public:
    static const uint64_t DWORD_2POWER = 6ull; // 2^6==sizeof(uint64_t)*8 = 64;
    static const uint64_t BITS_IN_DWORD = 1ull << DWORD_2POWER; //==sizeof(uint64_t)*8 = 64;
    static const uint64_t DWORD_MASK = BITS_IN_DWORD - 1ull; // =63=0x3F
private:
    uint64_t* FBits;
    uint32_t FCount;  // number of 64bit dwords
    uint64_t FBitsCount;
public:
    TBitField()
    {
        FBits = nullptr;
        FCount = 0;
        FBitsCount = 0;
    }

    TBitField(const uint64_t* bits, const uint32_t dwordsCount) : TBitField() // count is in uint64_t words here
    {
        SetData(bits, dwordsCount);
    }

    ~TBitField() { free(FBits); FBits = nullptr; }

    void SetData(const uint64_t* bits, const uint32_t dwordsCount) // count is in uint64_t words here
    {
        free(FBits); // free previously allocated memory if any
        FBits = (uint64_t*)malloc(dwordsCount*sizeof(uint64_t));
        FCount = dwordsCount;
        FBitsCount = dwordsCount * 64ull;
        auto res = memcpy_s(FBits, dwordsCount * sizeof(uint64_t), bits, dwordsCount * sizeof(uint64_t));
        UNREFERENCED_PARAMETER(res);
        assert(!res);
    }

    void SetData(const uint32_t dwordsCount, bool setAllTo) // count is in uint64_t words here
    {
        free(FBits); // free previously allocated memory if any
        FBits = (uint64_t*)malloc(dwordsCount * sizeof(uint64_t));
        FCount = dwordsCount;
        FBitsCount = dwordsCount * 64ull;
        if(setAllTo)
            memset(FBits, 0xFF, dwordsCount * sizeof(uint64_t));
        else
            memset(FBits, 0x00, dwordsCount * sizeof(uint64_t));
    }

    void AddData(const uint64_t* bits, const uint32_t addWordsCount) // count is in uint64_t words here
    {
        //delete[] FBits; // free previously allocated memory if any
        FBits = (uint64_t*)realloc(FBits, (FCount + addWordsCount)*sizeof(uint64_t) );
        auto res = memcpy_s(FBits + FCount, addWordsCount * sizeof(uint64_t), bits, addWordsCount * sizeof(uint64_t));
        UNREFERENCED_PARAMETER(res);
        assert(!res);
        FCount += addWordsCount;
        FBitsCount += addWordsCount * 64ull;
    }

    /*TBitField& operator=(const TBitField& a)
    {
        SetData(a.FBits, a.FCount);
        return *this;
    }*/

    uint8_t* GetData()
    {
        return (uint8_t*)FBits;
    }

    uint32_t Count() const { return FCount; }
    uint64_t BitsCount() const { return FBitsCount; }

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

        uint64_t wordIndex = bitIndex >> DWORD_2POWER; // divide by 64 = 2^6
        if (((FBits[wordIndex] >> (bitIndex & DWORD_MASK)) & 1ull) == 1ull) return true;
        return false;
    }

    // sets bitIndex bit into 1
    void SetTrue(uint64_t bitIndex)
    {
        if (bitIndex >= FBitsCount) throw std::runtime_error("Index out of bounds");

        uint64_t wordIndex = bitIndex >> DWORD_2POWER; // divide by 64 = 2^6

        FBits[wordIndex] |= (1ull << (bitIndex & DWORD_MASK));
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
                if (bitWord >> i) return bitIndex; //we've met 1
                bitIndex--;
            }
        }

        assert(bitIndex + 1 == 0);
        return bitIndex;
    }
};

