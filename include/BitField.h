#pragma once

#include "Debug.h"
#include <cassert>
#include <cstdint>
#include <stdexcept>

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

    TBitField(const uint64_t* bits, const uint64_t wordsCount) : TBitField() // count is in uint64_t words here
    {
        SetData(bits, wordsCount);
    }

    TBitField(const TBitField& a) : TBitField()
    {
        SetData(a.FBits, a.FCount);
    }

    ~TBitField() { delete[] FBits; FBits = nullptr; }

    void SetData(const uint64_t* bits, const uint64_t wordsCount) // count is in uint64_t words here
    {
        delete[] FBits; // free previously allocated memory if any
        FBits = DBG_NEW uint64_t[wordsCount];
        FCount = wordsCount;
        FBitsCount = wordsCount * 64;
        auto res = memcpy_s(FBits, wordsCount * sizeof(uint64_t), bits, wordsCount * sizeof(uint64_t));
        UNREFERENCED_PARAMETER(res);
        assert(!res);
    }

    TBitField& operator=(const TBitField& a)
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
        if (((FBits[wordIndex] >> (bitIndex % 64)) & 1ull) == 1ull) return true;
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
                if (bitWord >> i) return bitIndex; //we've met 1
                bitIndex--;
            }
        }

        assert(bitIndex + 1 == 0);
        return bitIndex;
    }
};

