#pragma once

#include "Functions.h"
#include "Caches.h"

#define STREAM_NONAME "<noname>"
#define STREAM_NONAME_W L"<noname>"

class IRecordLoader
{
protected:
	VOLUME_DATA FVolumeData;
	TMFTRecCache FMFTRecCache; // = Singleton<TMFTRecCache>::getInstance();

public:
	static string_t ParseVolume(const string_t& vol);
	virtual const VOLUME_DATA& GetVolumeData() const { return FVolumeData; }
	virtual void OpenVolume(const string_t& vol) = 0;
	virtual bool LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData) = 0;
	virtual bool ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf) = 0;

	virtual uint8_t* LoadMFTRecordCache(MFT_REF mftRecRef) // returns NULL if error occurred during loading MFT record
	{
		uint8_t** result = FMFTRecCache.GetValuePointer(mftRecRef.sId.low);
		if (result == nullptr) // no value in cache, load MFT record from disk
		{
			uint8_t* mftRecBuf = DBG_NEW uint8_t[FVolumeData.BytesPerMFTRec];
			if (!LoadMFTRecord(mftRecRef, mftRecBuf))
				return nullptr; // error loading MFT record

			//we use mftRecRef.sId.low here because high part of mftRecRef.Id may change when MFT record is modified
			FMFTRecCache.SetValue(mftRecRef.sId.low, mftRecBuf); // update cache

			return mftRecBuf;
		}

		return *result; // return MFT record from cache
	}

	virtual void CloseVolume()
	{
		// closes volume handle opened by OpenVolume
		// clears data about volume, clears caches

		GET_LOGGER;
		logger.DebugFmt("Closing volume: {}", wtos(FVolumeData.Name));

		CloseHandle(FVolumeData.hVolume);
		auto& volDataBuf = (NTFS_VOLUME_DATA_BUFFER&)FVolumeData;
		ZeroMemory(&volDataBuf, sizeof(NTFS_VOLUME_DATA_BUFFER));
		FVolumeData.hVolume = INVALID_HANDLE_VALUE;
		FVolumeData.Name.clear();
		FMFTRecCache.Clear();
	}
};

class TMFTRecordLoader : public IRecordLoader
{
public:
	TMFTRecordLoader() { }
	TMFTRecordLoader(const string_t& vol) { OpenVolume(vol); }
	void OpenVolume(const string_t& vol) override;
	//void CloseVolume() override;
	bool ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf) override;
	bool LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData) override;
	//uint8_t* LoadMFTRecordCache(MFT_REF mftRecRef) override;
};


class TMFTParserBase
{
protected:
	IRecordLoader& FLoader;
	const VOLUME_DATA& getVolData() const { return FLoader.GetVolumeData(); }
public:
	TMFTParserBase(IRecordLoader& loader) : FLoader(loader) {};

	bool FixupUSA(uint8_t* dataBuf, CLST startLCN, uint64_t iblocksCount, uint32_t indexBlockSize);
	//void FillAttrValues(MFT_FILE_RECORD* mftRec, PMFT_ATTR_HEADER* attrValues);
	void FillAttrCollection(MFT_FILE_RECORD* mftRec, TAttrCollection& collection);
	void FillAttrCollection(MFT_FILE_RECORD* mftRec, uint32_t attrFilter, TAttrCollection& collection);
	//bool FillCollectionFromAttrList(MFTRecIndex indexMFTRec, uint32_t attrFilter, ATTR_LIST_ENTRY* startListItem, uint8_t* attrListEnd1, uint8_t* attrListEnd2, TAttrCollection& collection);

	//void GetAttr(ATTR_TYPE attrType, const PMFT_ATTR_HEADER* const attrValues, PMFT_ATTR_HEADER* result);
	void GetFileListFromNode(INDEX_HDR* ihdr, TLCNRecs& lcns, TFileList& fnames);
	void GetFileList(INDEX_HDR* ihdr, AddFileAttrPred pred);

	//bool ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf);
	bool ParseNonresAttrList(MFTRecIndex indexMFTRec, MFT_ATTR_HEADER* attrListAttr, AttrListPred processChildMFTRecPred);
	bool ParseNonresAttrList(MFTRecIndex indexMFTRec, uint32_t attrFilter, MFT_ATTR_HEADER* attrListAttr, AttrListPred processChildMFTRecPred);
	//bool GetAttrFromAttrList(ATTR_LIST_ENTRY* startListItem, ATTR_TYPE attrType, uint8_t* attrListEnd1, uint8_t* attrListEnd2, PMFT_ATTR_HEADER* result);
	bool ParseNonresBitmap(MFT_ATTR_HEADER* attr, TBitField& bitmap);
	bool ParseBitmap(MFT_ATTR_HEADER* attr, TBitField& bitmap);
	void ParseIndexRoot(MFT_ATTR_HEADER* attr, TLCNRecs& lcns, TFileList& fileList);
	bool ParseAlloc(MFT_ATTR_HEADER* attr, TDataRuns& dataRuns);
	void ParseAttrList(MFTRecIndex indexMFTRec, uint32_t attrFilter, ATTR_LIST_ENTRY* startListItem, uint8_t* attrListEnd, uint64_t realSize, uint64_t& processedAttrSize, AttrListPred processChildMFTRecPred);
	void ParseAttrList(MFTRecIndex indexMFTRec, ATTR_LIST_ENTRY* startListItem, uint8_t* attrListEnd, uint64_t realSize, uint64_t& processedAttrSize, AttrListPred processChildMFTRecPred);
	bool ProcessDataRuns(DIR_NODE& node, ProcessiBlocksPred processIndexBlockPred);
	bool DecodeDataRuns(MFT_ATTR_HEADER* attr, TDataRuns& runs);
	
	ATTR_FILE_NAME* GetFileNameAttr(MFT_FILE_RECORD* mftRec);
	std::wstring GetPathByAttrFileName(ATTR_FILE_NAME* attrFileName);
	void GetFileNameAttrPointers(MFT_FILE_RECORD* mftRec, THArray<ATTR_FILE_NAME*>& attrFileNames);
	
	int32_t GetFileListFromMFTRec(MFT_FILE_RECORD* mftRec, DIR_NODE& node);
	bool GetPathByMFTRecID(MFT_REF mftRecRef, THArray<std::wstring>& paths);
    MFTRecIndex GetMFTRecIdByPath(const ci_string& path); // ci_string is for case INsensitive search here
};

class TMFTStatCollector : public TMFTParserBase
{
private:
	TItemInfoList FItemsList;
public:
	TMFTStatCollector(IRecordLoader& loader) : TMFTParserBase(loader) {};
	
	//functions for reading ALL MFT records info and build statistics
	bool ReadMftItems(MFT_REF startMftRecRef, uint32_t dirLevel);
	bool ReadMftItemInfo(MFT_REF mftRecRef, ITEM_INFO& itemInfo);
	bool ReadMftItemInfoBuf(MFT_FILE_RECORD* mftRec, ITEM_INFO& itemInfo);
	void ShowVolumeStat();

};

class TMFTSearchReader: public TMFTParserBase
{
public:
	TFileCache FFileList;

	TMFTSearchReader(IRecordLoader& loader) : TMFTParserBase(loader) { }
	bool ParseMFTRecord(uint8_t* mftRecData, DIR_NODE& node, uint32_t parentIdx, TFileCache::TLevel* level);

	bool ReadDirectoryV1(uint32_t parentIdx, CACHE_ITEM* parentItem, uint64_t& dirSize, ProgressCallbackPtr callback);
	void ReadDirsV1();
};

class TMFTSearchReaderV2 : public TMFTParserBase
{
private:
	TFileList FDirList;
public:
	TMFTSearchReaderV2(IRecordLoader& loader) : TMFTParserBase(loader) { } 

	bool ReadDirectoryV2(MFT_REF parentMftRecID, uint32_t dirLevel);
	void ReadDirsV2();
};
