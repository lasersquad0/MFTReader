#pragma once

#include <expected>
#include "Functions.h" //for TErrorCode
#include "Caches.h"
#include "FileCache.h"
#include "Loaders.h"

#define STREAM_NONAME "<noname>"
#define STREAM_NONAME_W L"<noname>"

class TMFTParserBase
{
protected:
	IRecordsLoader& FLoader;
	const VOLUME_DATA& getVolData() const { return FLoader.GetVolumeData(); }
public:
	TMFTParserBase(IRecordsLoader& loader) : FLoader(loader) {};

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
	TMFTStatCollector(IRecordsLoader& loader, bool processNonRes = true) : TMFTParserBase(loader), FProcessNonResAttr(processNonRes) {};
	
	TItemInfoList& GetItemsList() { return FItemsList; }

	TErrorCode ReadMftItems(FILE_NAME fileItem,/*MFT_REF mftRecRef, MFT_REF parentMFTRecRef,*/ uint32_t dirLevel, ReadMftItemsCallback callback);
	TErrorCode ReadMftItems(MFT_REF mftRecRef, uint32_t dirLevel, ReadMftItemsCallback callback);
	TErrorCode ReadMftItemInfo(FILE_NAME fileItem, /*MFT_REF mftRecRef, MFT_REF parentMFTRecRef,*/ ITEM_INFO& itemInfo);
	TErrorCode ReadMftItemInfo(MFT_REF mftRecRef, ITEM_INFO& itemInfo);
	TErrorCode ReadMftItemInfoBuf(MFT_FILE_RECORD* mftRec, FILE_NAME fileItem, /*MFT_REF parentMFTRecRef,*/ ITEM_INFO& itemInfo);
	TErrorCode ReadMftItemInfoBuf(MFT_FILE_RECORD* mftRec, ITEM_INFO& itemInfo);
	TErrorCode CollectVolumeStat();
	void ShowVolumeStat();
	void SaveToFile(string_t fileName);

};

class TMFTSearchReader: public TMFTParserBase
{
public:
	TFileCache FFileList;

	TMFTSearchReader(IRecordsLoader& loader) : TMFTParserBase(loader) { }
	TErrorCode ParseMFTRecord(uint8_t* mftRecData, DIR_NODE& node, AddFileAttrPred addToFileListPred /*uint32_t parentIdx, TFileCache::TLevel* level*/);

	TErrorCode ReadDirectoryV1(uint32_t parentIdx, CACHE_ITEM* parentItem, uint64_t& dirSize, ProgressCallbackPtr callback);
	void ReadDirsV1();
	void SaveToFile(string_t fileName);
};

class TMFTSearchReaderV2 : public TMFTParserBase
{
private:
	TFileList FDirList;
public:
	TMFTSearchReaderV2(IRecordsLoader& loader) : TMFTParserBase(loader) { } 

	TErrorCode ReadDirectoryV2(MFT_REF parentMftRecID, uint32_t dirLevel);
	void ReadDirsV2();
};
