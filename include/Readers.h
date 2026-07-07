#pragma once

#include "Functions.h"
#include "Caches.h"

class IRecordLoader
{
protected:
	VOLUME_DATA FVolumeData;
	TMFTRecCache FMFTRecCache; // = Singleton<TMFTRecCache>::getInstance();

public:
	static string_t ParseVolume(const string_t& vol);
	virtual const VOLUME_DATA& GetVolumeData() const { return FVolumeData; }
	virtual void OpenVolume(const string_t& vol) = 0;
	virtual void CloseVolume() = 0;
	virtual bool LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData) = 0;
	virtual uint8_t* LoadMFTRecordCache(MFT_REF mftRecRef) = 0;
};

class TMFTRecordLoader : public IRecordLoader
{
public:
	TMFTRecordLoader() { }
	TMFTRecordLoader(const string_t& vol) { OpenVolume(vol); }
	void OpenVolume(const string_t& vol) override;
	void CloseVolume() override;
	bool LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData) override;
	uint8_t* LoadMFTRecordCache(MFT_REF mftRecRef) override;
};


class TMFTParserBase
{
protected:
	IRecordLoader& FLoader;
	const VOLUME_DATA& getVolData() const { return FLoader.GetVolumeData(); }
public:
	TMFTParserBase(IRecordLoader& loader) : FLoader(loader) {};

	bool FixupUSA(uint8_t* dataBuf, DATA_RUN_ITEM& rli, uint64_t rlilen);
	void FillAttrValues(MFT_FILE_RECORD* mftRec, PMFT_ATTR_HEADER* attrValues);
	void GetAttr(ATTR_TYPE attrType, const PMFT_ATTR_HEADER* const attrValues, PMFT_ATTR_HEADER* result);
	void GetFileListFromNode(INDEX_HDR* ihdr, TLCNRecs& lcns, TFileList& fnames);
	void GetFileList(INDEX_HDR* ihdr, AddFileAttrPred pred);

	bool ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf);
	bool ParseNonresAttrList(MFT_ATTR_HEADER* attrListAttr, ATTR_TYPE attrType, PMFT_ATTR_HEADER* result);
	bool ParseAttrList(ATTR_LIST_ENTRY* startListItem, ATTR_TYPE attrType, uint8_t* attrListEnd1, uint8_t* attrListEnd2, PMFT_ATTR_HEADER* result);
	bool ParseNonresBitmap(MFT_ATTR_HEADER* attr, TBitField& bitmap);
	bool ParseBitmap(MFT_ATTR_HEADER* attr, TBitField& bitmap);
	void ParseIndexRoot(MFT_ATTR_HEADER* attr, TLCNRecs& lcns, TFileList& fileList);
	bool ParseAlloc(MFT_ATTR_HEADER* attr, TDataRuns& dataRuns);
	bool ProcessDataRuns(DIR_NODE& node, ProcessLCNsPred pred);
	bool DecodeDataRuns(MFT_ATTR_HEADER* attr, TDataRuns& runs);
	
	ATTR_FILE_NAME* GetFirstFileNameAttr(MFT_FILE_RECORD* mftRec);
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
