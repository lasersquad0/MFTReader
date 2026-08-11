#pragma once

#include <expected>
#include "Functions.h" //for TErrorCode
#include "Caches.h"
#include "FileCache.h"

#define STREAM_NONAME "<noname>"
#define STREAM_NONAME_W L"<noname>"

class TMFTParserBase;

class IRecordLoader
{
protected:
	bool FOpened{ false };
	uint64_t FRecordsCount{ 0 }; // total number of MFT records in $MFT file
	uint32_t FMetaFilesCount{ 0 }; // number of first "system" hidden meta files till first non-system file met
	VOLUME_DATA FVolumeData;
	TMFTRecCache FMFTRecCache;

	virtual std::expected<uint32_t, TErrorCode> ReadMetaFilesCount(TMFTParserBase& parser);
public:
	static string_t NormalizeVolume(const string_t& vol);
	const VOLUME_DATA& GetVolumeData() const { return FVolumeData; }
	virtual void Open(const string_t& vol) = 0;
	virtual bool IsOpened() { return FOpened; };
	virtual void SetOpened(bool opened) { FOpened = opened; }
	virtual uint32_t GetMetaFilesCount() { return FMetaFilesCount; }
	virtual bool IsMetaFile(MFTRecIndex mftRecID) { assert(FMetaFilesCount > 0); return mftRecID < FMetaFilesCount; }

	virtual TErrorCode LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData) = 0;
	virtual TErrorCode ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf) = 0;
	static TErrorCode FixupUSA1(NTFS_RECORD_HEADER* record, uint32_t BytesPerBlock, uint32_t BytesPerSector);

	typedef uint8_t* puint8_t;
	typedef std::expected<puint8_t, TErrorCode> ret_expected;

	virtual ret_expected LoadMFTRecordCache(MFT_REF mftRecRef) // returns NULL if error occurred during loading MFT record
	{
		assert(IsOpened());

		uint8_t** result = FMFTRecCache.GetValuePointer(mftRecRef.sId.low);
	
		if (result == nullptr) // no value in cache, load MFT record from disk
		{
			uint8_t* mftRecBuf = DBG_NEW uint8_t[FVolumeData.BytesPerMFTRec];
			TErrorCode res = LoadMFTRecord(mftRecRef, mftRecBuf);
			if (res != TErrorCode::Success) 
				return std::unexpected(res); // error loading MFT record

			//we use mftRecRef.sId.low here because high part of mftRecRef.Id may change when MFT record is modified
			FMFTRecCache.SetValue(mftRecRef.sId.low, mftRecBuf); // update cache

			return mftRecBuf;
		}

		GET_LOGGER;
		logger.Warn("[LoadMFTRecordCache] Record is loaded from cache!");

		return *result; // return MFT record from cache
	}

	virtual void Close()
	{
		if (!IsOpened()) return;

		GET_LOGGER;
		logger.DebugFmt("Closing volume: {}", wtos(FVolumeData.Name));

		// clears data about volume, clears caches

		//CloseHandle(FVolumeData.hVolume);
		auto& volDataBuf = (NTFS_VOLUME_DATA_BUFFER&)FVolumeData;
		ZeroMemory(&volDataBuf, sizeof(NTFS_VOLUME_DATA_BUFFER));
		//FVolumeData.hVolume = INVALID_HANDLE_VALUE;
		FVolumeData.Name.clear();
		FMFTRecCache.Clear();
		FRecordsCount = 0;
		FMetaFilesCount = 0;
	}

};

class TMFTRecordLoader : public IRecordLoader
{
protected:
	void InternalOpen(const string_t& vol);
public:
	TMFTRecordLoader() { }
	TMFTRecordLoader(const string_t& vol) { Open(vol); }
	void Open(const string_t& vol) override;
	void Close() override;
	TErrorCode ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf) override;
	TErrorCode LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData) override;
};

class TMFTAllRecordsLoader : public TMFTRecordLoader
{
protected:
	THArrayRaw FRecs;
	TBitField FBitmap;

	TErrorCode ReadAllMftRecords();
public:
	TMFTAllRecordsLoader() {}
	TMFTAllRecordsLoader(const string_t& vol) { Open(vol); }
	void Open(const string_t& vol) override;
	TErrorCode LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData) override;
	ret_expected LoadMFTRecordCache(MFT_REF mftRecRef) override 
	{ 
		UNREFERENCED_PARAMETER(mftRecRef); 
		throw std::exception("LoadMFTRecordCache methhod is not supported."); 
	}
};


class TMFTParserBase
{
protected:
	IRecordLoader& FLoader;
	const VOLUME_DATA& getVolData() const { return FLoader.GetVolumeData(); }
public:
	TMFTParserBase(IRecordLoader& loader) : FLoader(loader) {};

	//bool FixupUSA(uint8_t* dataBuf, CLST startLCN, uint64_t iblocksCount, uint32_t indexBlockSize);
	//void FillAttrValues(MFT_FILE_RECORD* mftRec, PMFT_ATTR_HEADER* attrValues);
	TErrorCode FillAttrCollection(MFT_FILE_RECORD* mftRec, TAttrCollection& collection);
	TErrorCode FillAttrCollection(MFT_FILE_RECORD* mftRec, uint32_t attrFilter, TAttrCollection& collection);
	//bool FillCollectionFromAttrList(MFTRecIndex indexMFTRec, uint32_t attrFilter, ATTR_LIST_ENTRY* startListItem, uint8_t* attrListEnd1, uint8_t* attrListEnd2, TAttrCollection& collection);

	//void GetAttr(ATTR_TYPE attrType, const PMFT_ATTR_HEADER* const attrValues, PMFT_ATTR_HEADER* result);
	void GetFileListFromNode(INDEX_HDR* ihdr, TLCNRecs& lcns, TFileList& fnames);
	void GetFileList(INDEX_HDR* ihdr, AddFileAttrPred pred);

	//bool ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf);
	TErrorCode ParseNonresAttrList(MFTRecIndex indexMFTRec, MFT_ATTR_HEADER* attrListAttr, AttrListPred processChildMFTRecPred);
	TErrorCode ParseNonresAttrList(MFTRecIndex indexMFTRec, uint32_t attrFilter, MFT_ATTR_HEADER* attrListAttr, AttrListPred processChildMFTRecPred);
	//bool GetAttrFromAttrList(ATTR_LIST_ENTRY* startListItem, ATTR_TYPE attrType, uint8_t* attrListEnd1, uint8_t* attrListEnd2, PMFT_ATTR_HEADER* result);
	TErrorCode ParseNonresBitmap(MFT_ATTR_HEADER* attr, TBitField& bitmap);
	TErrorCode ParseBitmap(MFT_ATTR_HEADER* attr, TBitField& bitmap);
	void ParseIndexRoot(MFT_ATTR_HEADER* attr, TLCNRecs& lcns, TFileList& fileList);
	//bool ParseAlloc(MFT_ATTR_HEADER* attr, TDataRuns& dataRuns);
	TErrorCode ParseAttrList(MFTRecIndex indexMFTRec, uint32_t attrFilter, ATTR_LIST_ENTRY* startListItem, uint8_t* attrListEnd, uint64_t realSize, uint64_t& processedAttrSize, AttrListPred processChildMFTRecPred);
	TErrorCode ParseAttrList(MFTRecIndex indexMFTRec, ATTR_LIST_ENTRY* startListItem, uint8_t* attrListEnd, uint64_t realSize, uint64_t& processedAttrSize, AttrListPred processChildMFTRecPred);
	TErrorCode ProcessAllocDataRuns(DIR_NODE& node, ProcessiBlocksPred processIndexBlockPred);
	TErrorCode DecodeDataRuns(MFT_ATTR_HEADER* attr, TDataRuns& runs);
	
	ATTR_FILE_NAME* GetFileNameAttr(MFT_FILE_RECORD* mftRec);
	std::wstring GetPathByAttrFileName(ATTR_FILE_NAME* attrFileName);
	TErrorCode GetFileNameAttrPointers(MFT_FILE_RECORD* mftRec, THArray<ATTR_FILE_NAME*>& attrFileNames);
	
	TErrorCode GetFileListFromMFTRec(MFT_FILE_RECORD* mftRec, DIR_NODE& node);
	TErrorCode PathByMFTRecID(MFT_REF mftRecRef, THArray<std::wstring>& paths);
	std::expected<MFTRecIndex, TErrorCode> MFTRecIdByPath(const ci_string& path); // ci_string is for case INsensitive search here
};

typedef int32_t(*ReadMftItemsCallback)(const string_t& data);

class TMFTStatCollector : public TMFTParserBase
{
private:
	TItemInfoList FItemsList;
	THashUnordered<std::wstring, std::wstring> FStatistics;
	bool FProcessNonResAttr; // whether to process non-resident attrs. for some tests it is not needed to process non-res attrs
public:
	TMFTStatCollector(IRecordLoader& loader, bool processNonRes = true) : TMFTParserBase(loader), FProcessNonResAttr(processNonRes) {};
	
	TItemInfoList& GetItemsList() { return FItemsList; }

	TErrorCode ReadMftItems(MFT_REF startMftRecRef, uint32_t dirLevel, ReadMftItemsCallback callback);
	TErrorCode ReadMftItemInfo(MFT_REF mftRecRef, ITEM_INFO& itemInfo);
	TErrorCode ReadMftItemInfoBuf(MFT_FILE_RECORD* mftRec, ITEM_INFO& itemInfo);
	TErrorCode CollectVolumeStat();
	void ShowVolumeStat();
	void SaveToFile(string_t fileName);

};

class TMFTSearchReader: public TMFTParserBase
{
public:
	TFileCache FFileList;

	TMFTSearchReader(IRecordLoader& loader) : TMFTParserBase(loader) { }
	TErrorCode ParseMFTRecord(uint8_t* mftRecData, DIR_NODE& node, AddFileAttrPred addToFileListPred /*uint32_t parentIdx, TFileCache::TLevel* level*/);

	TErrorCode ReadDirectoryV1(uint32_t parentIdx, CACHE_ITEM* parentItem, uint64_t& dirSize, ProgressCallbackPtr callback);
	void ReadDirsV1();
};

class TMFTSearchReaderV2 : public TMFTParserBase
{
private:
	TFileList FDirList;
public:
	TMFTSearchReaderV2(IRecordLoader& loader) : TMFTParserBase(loader) { } 

	TErrorCode ReadDirectoryV2(MFT_REF parentMftRecID, uint32_t dirLevel);
	void ReadDirsV2();
};
