#include "Readers.h"


string_t TMFTReaderBase::ParseVolume(const string_t& vol)
{
	return ::ParseVolume(vol);
}

void TMFTReaderBase::ReadVolumeData(const string_t& vol)  // vol should be in format \\.\c:
{
	::ReadVolumeData(vol, FVolumeData);
}


TMFTStatReader::TMFTStatReader()
{
	
}
TMFTStatReader::TMFTStatReader(string_t& vol)
{
	ReadVolumeData(ParseVolume(vol));
}
bool TMFTStatReader::ReadMftItems(MFT_REF startMmftRec, uint32_t dirLevel)
{
	return ::ReadMftItems(FVolumeData, startMmftRec, dirLevel, FItemsList);
}
bool TMFTStatReader::ReadMftItemInfo(MFT_REF mftRecRef, ITEM_INFO& itemInfo)
{
	return ::ReadMftItemInfo(FVolumeData, mftRecRef, itemInfo);
}
void TMFTStatReader::ShowVolumeStat()
{
	::ShowVolumeStat(FVolumeData);
}

TMFTSearchReader::TMFTSearchReader()
{

}
TMFTSearchReader::TMFTSearchReader(string_t& vol)
{
	ReadVolumeData(ParseVolume(vol));
}
bool TMFTSearchReader::ReadDirectoryV1(uint32_t parentIdx, CACHE_ITEM* parentItem, uint64_t& dirSize, TFileCache& gFileList, ProgressCallbackPtr callback)
{
	return ::ReadDirectoryV1(FVolumeData, parentIdx, parentItem, dirSize, gFileList, callback);
}

void TMFTSearchReader::ReadDirsV1()
{
	::ReadDirsV1(FVolumeData);
}