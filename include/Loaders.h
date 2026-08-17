#pragma once

#include <expected>
#include "Functions.h" //for TErrorCode
//#include "Caches.h"
//#include "FileCache.h"


typedef uint8_t* puint8_t;
typedef std::expected<puint8_t, TErrorCode> expected_uintptr;
typedef std::expected<uint32_t, TErrorCode> expected_uint32;

class TMFTBaseReader;

class IRecordsLoader
{
protected:
	bool FOpened{ false };
	uint64_t FRecordsCount{ 0 }; // total number of MFT records in $MFT file
	uint32_t FMetaFilesCount{ 0 }; // number of first "system" hidden meta files till first non-system file met
	VOLUME_DATA FVolumeData;
	TMFTRecCache FMFTRecCache;

	virtual TErrorCode InternalLoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData, bool internalCall) = 0;
	virtual expected_uint32 ReadMetaFilesCount(TMFTBaseReader& parser);
public:
	virtual ~IRecordsLoader() { Close(); }
	static string_t NormalizeVolume(const string_t& vol);
	static string_t PreNormalize(const string_t& str);
	static string_t AbsPath(const string_t& str);
	static bool IsPath(const string_t& vol);

	const VOLUME_DATA& GetVolumeData() const { return FVolumeData; }
	//uint32_t GetMetaFilesCount() const { return FMetaFilesCount; };
	virtual void Open(const string_t& vol) = 0;
	virtual bool IsOpened() { return FOpened; };
	virtual void SetOpened(bool opened) { FOpened = opened; }
	virtual uint32_t GetMetaFilesCount() { return FMetaFilesCount; }
	virtual bool IsMetaFile(MFTRecIndex mftRecID) { assert(FMetaFilesCount > 0); return mftRecID < FMetaFilesCount; }

	virtual TErrorCode LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData);
	virtual TErrorCode ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf) = 0;
	static TErrorCode FixupUSA1(NTFS_RECORD_HEADER* record, uint32_t BytesPerBlock, uint32_t BytesPerSector);
	virtual expected_uintptr LoadMFTRecordCache(MFT_REF mftRecRef); // returns NULL if error occurred during loading MFT record
	virtual void Close();

};

class TWinAPIRecordsLoader : public IRecordsLoader
{
protected:
	void InternalOpen(const string_t& vol);
	TErrorCode InternalLoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData, bool internalCall) override;
public:
	TWinAPIRecordsLoader() {}
	TWinAPIRecordsLoader(const string_t& vol) { Open(vol); }
	void Open(const string_t& vol) override;
	void Close() override;
	TErrorCode ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf) override;
	//TErrorCode LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData) override;
};

class TWinAPICacheRecordsLoader : public TWinAPIRecordsLoader
{
protected:
	THArrayRaw FRecs;
	TBitField FBitmap;

	TErrorCode InternalLoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData, bool internalCall) override;
	TErrorCode ReadAllMftRecords();
public:
	TWinAPICacheRecordsLoader() {}
	TWinAPICacheRecordsLoader(const string_t& vol) { Open(vol); }
	void Open(const string_t& vol) override;
	//TErrorCode LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData) override;
	expected_uintptr LoadMFTRecordCache(MFT_REF mftRecRef) override
	{
		UNREFERENCED_PARAMETER(mftRecRef);
		throw std::exception("LoadMFTRecordCache methhod is not supported.");
	}
};


class TFileImageRecordsLoader : public IRecordsLoader
{
private:
	HANDLE FHFile = INVALID_HANDLE_VALUE;
	uint64_t FPartitionOffset{ 0 }; // offset from beginning of the file where NTFS partition starts 
	TDataRuns FMFTDataRuns; // Data Runs of $MFT file, used to properly calc offsets for MFT records
public:
	TFileImageRecordsLoader() {}
	TFileImageRecordsLoader(const string_t& imgFileName) { Open(imgFileName); }

	bool Eof(MFTRecIndex id) const { return id >= FRecordsCount; }

	int64_t MFTRecIdToOffset(MFTRecIndex MFTRecID);

	// MFT table may be fragmented. Fragments can be found in Data Runs in the first MFT record (file with name '$MFT', MFT rec #0)
	// This function calculates offset of MFT record MFTRecID taking into account MFT table fragmentation. 
	// This offset starts from first byte of NTFS partition. 
	static int64_t MFTRecIdToOffset(MFTRecIndex MFTRecID, TDataRuns& runs, uint32_t BytesPerCluster, uint32_t BytesPerMFTRec);

	// finds first NTFS partition in the file, then finds first MFT record in this partition.
	// results are stored in FPartitionOffset, FMFTDataRuns, FMFTRecordsCount fields 
	void Open(const string_t& imgFileName) override;

	void Close() override;

	// returns error code WrongMFTRecID when: 
	//   - incorrect record ID specified (out of range) 
	// IOError when
	//   - attemt to read outside of a file
	//   - cannot read needed BytesPerMFTRec bytes from a file
	// MFTRecordNotInUse when 
	//   - requested rec ID is inside of range but record does not contain 'FILE' signature
	// CorruptedData when
	//   - FixupUSA call failed
	TErrorCode InternalLoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData, bool internalCall) override;

	TErrorCode ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf) override;

	// Applies Update Sequence Array (USA) to MFT record refered by dataBuf
	TErrorCode FixupUsaMFTRec(NTFS_RECORD_HEADER* mftRec);
};

