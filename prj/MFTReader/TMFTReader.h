#pragma once

#include "Functions.h"

class TMFTReaderBase
{
protected:
	VOLUME_DATA FVolumeData;
public:
	string_t ParseVolume(const string_t& vol);
	void ReadVolumeData(const string_t& vol);  // volume should be in format \\.\c:

};

class TMFTStatReader : public TMFTReaderBase
{
private:
	TItemInfoList FItemsList;
public:
	TMFTStatReader(string_t& volume);

	//functions for reading ALL MFT records info and build statistics
	bool ReadMftItems(MFT_REF startMmftRec, uint32_t dirLevel);
	bool ReadMftItemInfo(MFT_REF mftRecRef, ITEM_INFO& itemInfo);
	void ShowVolumeStat();

};

class TMFTSearchReader: public TMFTReaderBase
{
public:
	bool ReadDirectoryV1(uint32_t parentIdx, CACHE_ITEM* parentItem, uint64_t& dirSize, TFileCache& gFileList, ProgressCallbackPtr callback);
	void ReadDirsV1();
};

