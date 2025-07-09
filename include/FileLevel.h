#pragma once

#include <cstdint>
#include <cassert>
#include <string>
#include <stdexcept>
#include <malloc.h>

#include "strutils/include/ci_string.h"
#include "logengine2/FileStream.h"
#include "Utils.h"
#include "NTFS.h"

// structure fields aligment set to 1 byte.
// by default aligment is 16 bytes but here we need 1
#pragma pack(push, 1)

struct CACHE_ITEM
{
	uint32_t FParent;
	uint32_t FLevel;
	MFT_REF FMFTRecID; // MFT Id of this file
	ATTR_FILE_NAME FileAttr; // 66 bytes. must be last field in FILELIST_ITEM because it has variable size

	//uint32_t FileAttrSize() const { return sizeof(ATTR_FILE_NAME) + FileAttr.FileNameLen * sizeof(wchar_t); } // size in bytes of current item instance 
	uint32_t Size() const {	return sizeof(CACHE_ITEM) + FileAttr.FileNameLen * sizeof(wchar_t); } // size in bytes of current item instance 
	
	wchar_t* Name() const { return (wchar_t*)((uint8_t*)this + sizeof(CACHE_ITEM)); }
	bool IsMetaFile() const { return (FileAttr.ParentDir.sId.low == MFT_ROOT_REC_ID) && (Name()[0] == L'$'); }
	bool IsDotDir() const { return (FileAttr.FileNameLen == 1) && (Name()[0] == L'.'); }
	bool IsDir() const { return (FileAttr.dup.FileAttrib & (uint32_t)FILE_ATTR_FLAGS::DIRECTORY) > 0;	}
	bool IsReparse() const { return (FileAttr.dup.FileAttrib & (uint32_t)FILE_ATTR_FLAGS::REPARSE_POINT) > 0; };
	bool NtfsInternal() const { return IsMetaFile() || IsDotDir(); }
};

class TFileLevelList
{
public:
	uint32_t FCount; // number of items
	uint8_t* FStart;
	uint8_t* FEnd;
	uint8_t* FHead;
	uint32_t FLevel;

	uint32_t CalcFileAttrSize(const ATTR_FILE_NAME* fn) const { return sizeof(ATTR_FILE_NAME) + fn->FileNameLen * sizeof(wchar_t); } // size in bytes of file attr and file name
	uint32_t CalcItemSize(const ATTR_FILE_NAME* fn) const { return sizeof(CACHE_ITEM) + fn->FileNameLen * sizeof(wchar_t); } // size in bytes of CACHE_ITEM structure and filename

	void Error(const uint32_t Value, const uint32_t vmax) const 
	{ 
		if (Value >= vmax) throw std::out_of_range("Element with index " + std::to_string(Value) + " not found!"); 
	}

	// checks if there is anough allocated memoty to add structure if addBytes size 
    // in case not enough memory EnsureCapacity allocates additional memory, and copies content there  
	void EnsureCapacity(uint32_t addBytes)
	{
		if (FHead + addBytes >= FEnd)
		{
			uint32_t newSize = (uint32_t)((FEnd - FStart) + (FEnd - FStart) / 4); // increase by 25%
			uint32_t headRel = (uint32_t)(FHead - FStart);
			FStart = (uint8_t*)realloc(FStart, newSize);
			FEnd = FStart + newSize;
			FHead = FStart + headRel;
			assert(FStart);
		}
	}

public:
	TFileLevelList(uint32_t level, uint32_t initialCapacity) // initialCapacity is in bytes
	{
		FStart = (uint8_t*)malloc(initialCapacity);
		FHead = FStart;
		FEnd = FStart + initialCapacity;
		FCount = 0;
		FLevel = level; 
	}

	TFileLevelList(const TFileLevelList& a) = delete; // copy constructor
	TFileLevelList& operator=(const TFileLevelList& a) = delete;
	
	~TFileLevelList()
	{
		free(FStart);
	}

	CACHE_ITEM* GetValue(const uint32_t Index) const
	{
		Error(Index, FCount);

		CACHE_ITEM* it = First();
		for (uint32_t i = 0; i < Index; i++) 
			it = Next(it);

		return it;
	}

	/*
	// each structure has different size because of file name string
	uint32_t AddValue(const CACHE_ITEM* item)
	{
		uint32_t size = item->Size();
		assert(size < 260*sizeof(wchar_t) + sizeof(CACHE_ITEM) + 2); // 260 max file name length. 2 extra bytes just in case

		EnsureCapacity(size);
		memcpy_s(FHead, size, item, size);
		FHead += size;
		FCount++;
	}
	*/

	// each structure has different size because of file name string
	uint32_t AddValue(const uint32_t parent, const uint32_t itemLevel, const MFT_REF MFTRecID, const ATTR_FILE_NAME* data)
	{
		assert(itemLevel == FLevel); //TODO remove itemLevel from parameters since it is not needed

		uint32_t itemSize = CalcItemSize(data); // this is total item size: ITEM + filename size
		assert(itemSize < 260 * sizeof(wchar_t) + sizeof(CACHE_ITEM) + 2); // 260 max file name length. 2 extra bytes just in case

		EnsureCapacity(itemSize);

		((CACHE_ITEM*)FHead)->FParent = parent;
		((CACHE_ITEM*)FHead)->FLevel = itemLevel;
		((CACHE_ITEM*)FHead)->FMFTRecID = MFTRecID;

		uint32_t size = CalcFileAttrSize(data); // this is size of ATTR_FILE_NAME + filename size, needed only for memcpy
		memcpy_s(FHead + offsetof(CACHE_ITEM, FileAttr), size, data, size);// TODO add error check?
		
		FHead += itemSize; // move head to total ITEM + filename size bytes.
		FCount++;

		return FCount - 1; // index of added item
	}

	uint32_t Level() const { return FLevel; }
	uint32_t Count() const { return FCount; }
	CACHE_ITEM* First() const { return (CACHE_ITEM*)FStart;}
	CACHE_ITEM* Last() const  { return (CACHE_ITEM*)FHead; } // this is not really pointer to the last existing item. this is pointer to next item that will be added.
	bool IsEnd(CACHE_ITEM* item) const { return (uint8_t*)item >= FHead; }
	bool Belongs(CACHE_ITEM* item) const { return ((uint8_t*)item >= FStart) && ((uint8_t*)item < FHead); }
	CACHE_ITEM* Next(CACHE_ITEM* item) const 
	{ 
		assert((uint8_t*)item < FHead); 
		assert((uint8_t*)item >= FStart); 
		auto res = (CACHE_ITEM*)((uint8_t*)item + item->Size()); 
		return res;
	}

};

#pragma pack(pop)

