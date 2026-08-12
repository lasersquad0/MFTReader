#pragma once

//#include "gtest/gtest.h"
#include "Readers.h"
#include "Functions.h"

int64_t TFileImageRecordsLoader::MFTRecIdToOffset(MFTRecIndex MFTRecID)
{
    return MFTRecIdToOffset(MFTRecID, FMFTDataRuns, FVolumeData.BytesPerCluster, FVolumeData.BytesPerMFTRec);
}

// MFT table may be fragmented. Fragments can be found in Data Runs in the first MFT record (file with name '$MFT', MFT rec #0)
// This function calculates offset of MFT record MFTRecID taking into account MFT table fragmentation. 
// This offset starts from first byte of NTFS partition. 
int64_t TFileImageRecordsLoader::MFTRecIdToOffset(MFTRecIndex MFTRecID, TDataRuns& runs, uint32_t BytesPerCluster, uint32_t BytesPerMFTRec)
{
    if (runs.Count() == 0) return -1;

    uint64_t MFTRecIDBytes = MFTRecID * (uint64_t)BytesPerMFTRec;
    uint64_t sumBytes = 0;
    DATA_RUN_ITEM rli{ 0 };

    for (uint i = 0; i < runs.Count(); i++)
    {
        rli = runs[i];
        assert(rli.len > 0ul); // len>0

        if (((rli.len * BytesPerCluster) % BytesPerMFTRec) != 0) // we've met len that is not divisible by BytesPerMFTRec
            return -1;

        sumBytes += rli.len * BytesPerCluster;
        if (sumBytes > MFTRecIDBytes) break;
    }

    if (sumBytes <= MFTRecIDBytes) return -1;

    return (rli.lcn + rli.len) * BytesPerCluster - (sumBytes - MFTRecIDBytes);
}

#define throw_winapi_exception(_where_) {\
    DWORD err = GetLastError(); \
    auto errMsg = GetErrorMessageTextA(err, (_where_)); \
    throw std::system_error(std::error_code(err, std::system_category()), errMsg); }


// finds first NTFS partition in the file, then finds first MFT record in this partition.
// results are stored in FPartitionOffset, FMFTDataRuns, FMFTRecordsCount fields 
void TFileImageRecordsLoader::Open(const string_t& imgFileName)
{
    assert(!IsOpened());
    assert(INVALID_HANDLE_VALUE == FHFile);

    FHFile = CreateFile(imgFileName.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    assert(INVALID_HANDLE_VALUE != FHFile); // << "Error opening file '" << imgFileName << "'";;

    // try to find "NTFS   " in the beginning of the file (offset 3).
    // if found - file contain only NTFS partition without MBR
    DWORD bytesRead = 0;
    NTFS_BOOT_SECTOR partNTFS{ 0 };

    if (!(ReadFile(FHFile, &partNTFS, sizeof(partNTFS), &bytesRead, nullptr) && (bytesRead == sizeof(partNTFS))))
        throw_winapi_exception("ReadFile");
        //FAIL() << std::format(_T("Error reading file '{}', Error code: {}"), imgFileName, GetLastError());

    FPartitionOffset = 0;

    if (memcmp(partNTFS.OemId, NTFS_LABEL, 8) != 0)
    {
        // looks like this file contains MBR and list of partitions
        // go through partitions unless find NTFS partition

        //TODO extended partitions are not supported yet, need to add support.

        MBR_PARTITION_ENTRY mbr{ 0 };
        DWORD mbrOff = 0x1BE; // fixed offset to first partition entry

        // look at list of partitions and find NTFS partition. 4 is max number of standard partitions.
        for (size_t i = 0; i < 4; i++)
        {
            if (SetFilePointer(FHFile, mbrOff, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
                throw_winapi_exception("SetFilePointer");
                //FAIL() << std::format(_T("Error setting file pointer for file '{}', Error code: {}"), imgFileName, GetLastError());

            bytesRead = 0;

            if (!(ReadFile(FHFile, &mbr, sizeof(mbr), &bytesRead, nullptr) && (bytesRead == sizeof(mbr))))
                throw_winapi_exception("ReadFile");
                //FAIL() << std::format(_T("Error reading file '{}', Error code: {}"), imgFileName, GetLastError());

            if (SetFilePointer(FHFile, mbr.FirstLBA * DEFAULT_SECTOR_SIZE, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
                throw_winapi_exception("SetFilePointer");
                 //FAIL() << std::format(_T("Error setting file pointer for file '{}', Error code: {}"), imgFileName, GetLastError());

            if (!(ReadFile(FHFile, &partNTFS, sizeof(partNTFS), &bytesRead, nullptr) && (bytesRead == sizeof(partNTFS))))
                throw_winapi_exception("ReadFile");
                //FAIL() << std::format(_T("Error reading file '{}', Error code: {}"), imgFileName, GetLastError());

            assert(DEFAULT_SECTOR_SIZE == partNTFS.BytesPerSector);
            assert(mbr.SectorCount == partNTFS.TotalSectors + 1);

            if (memcmp(partNTFS.OemId, NTFS_LABEL, 8) == 0)
            {
                assert(0x07 == mbr.Type); // additional check for NTFS volume type
                FPartitionOffset = mbr.FirstLBA * (uint64_t)DEFAULT_SECTOR_SIZE;
                break;
            }

            mbrOff += sizeof(NTFS_BOOT_SECTOR);
        }

        assert(0 != FPartitionOffset); // check that we've found NTFS partition
    }

    uint16_t RealSectorsPerCluster = (((signed char)partNTFS.SectorsPerCluster) >= 0) ? partNTFS.SectorsPerCluster : 1u << (256 - partNTFS.SectorsPerCluster);
    // EXPECT_EQ(0, partNTFS.TotalSectors % partNTFS.SectorsPerCluster);
    FVolumeData.BytesPerSector = partNTFS.BytesPerSector;
    FVolumeData.TotalClusters.QuadPart = partNTFS.TotalSectors / RealSectorsPerCluster;
    FVolumeData.NumberSectors.QuadPart = partNTFS.TotalSectors;
    FVolumeData.BytesPerCluster = partNTFS.BytesPerSector * RealSectorsPerCluster;
    FVolumeData.BytesPerMFTRec = (partNTFS.ClustersPerFileRecord >= 0) ? partNTFS.ClustersPerFileRecord * FVolumeData.BytesPerCluster : 1u << (-partNTFS.ClustersPerFileRecord);
    FVolumeData.ClustersPerFileRecordSegment = FVolumeData.BytesPerMFTRec / FVolumeData.BytesPerCluster;
    FVolumeData.MftStartLcn.QuadPart = partNTFS.MftStartLcn;
    FVolumeData.MftZoneStart.QuadPart = partNTFS.MftStartLcn;
    FVolumeData.Mft2StartLcn.QuadPart = partNTFS.MftMirrorStartLcn;
    FVolumeData.hVolume = INVALID_HANDLE_VALUE;// FHFile;
    FVolumeData.Name = convert_string<wchar_t>(GetVolumeName(imgFileName));

    assert(DEFAULT_SECTOR_SIZE == FVolumeData.BytesPerSector);
    assert(DEFAULT_BYTES_PER_MFT_REC == FVolumeData.BytesPerMFTRec);

    SetOpened(true); // needs to be before LoadMFTRecord

    // reading MFT record #0, getting $MFT LCNs
    uint8_t* mftRecData = (uint8_t*)alloca(FVolumeData.BytesPerMFTRec);
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecData;
    MFT_REF mftRef{ 0 };

    // these two temporary values needed for proper work of LoadMFTRecord
    FRecordsCount = 1;
    FMFTDataRuns.AddValue({ 2, 0, partNTFS.MftStartLcn });
    TErrorCode res = LoadMFTRecord(mftRef, mftRecData); // loading MFT record #0 which is $MFT file
    assert(TErrorCode::Success == res); // << "Error loading MFT record " << mftRef.sId.low;

    if (!ntfs_is_file_recp(mftRec->RecHeader.Signature))
        throw std::runtime_error(std::string("MFT record with incorrect signature found ") + (char*)mftRec->RecHeader.Signature[0]+(char*)mftRec->RecHeader.Signature[1]+(char*)mftRec->RecHeader.Signature[2]+(char*)mftRec->RecHeader.Signature[3] + " (expected 'FILE')");
        //FAIL() << "MFT record with incorrect signature found " << mftRec->RecHeader.Signature << " (expected 'FILE')";

    // check that record #0 is in use
    assert((mftRec->Flags & MFT_FLAG_IN_USE) == MFT_FLAG_IN_USE);

    TMFTParserBase prsr(*this);
    TAttrCollection collection;
    res = prsr.FillAttrCollection(mftRec, MakeAttrBitmask(ATTR_DATA), collection);
    assert(TErrorCode::Success == res);// << "Error parsing attributes in MFT record buffer " << mftRef.sId.low;

    auto& adata = collection.Get(ATTR_DATA);
    assert(1ul == adata.Count()); // $MFT file contain only one ATR_DATA attribute

    FMFTDataRuns.Clear();
    res = prsr.DecodeDataRuns(adata[0], FMFTDataRuns);
    assert(TErrorCode::Success == res);
    assert(FMFTDataRuns.Count() > 0ul);

    FRecordsCount = 0;
    for (auto& rli : FMFTDataRuns) FRecordsCount += rli.len;

    // FMFTRecordsCount is in clusters here
    FVolumeData.MftZoneEnd.QuadPart = FVolumeData.MftStartLcn.QuadPart + FRecordsCount;
    FVolumeData.MftValidDataLength.QuadPart = FRecordsCount * FVolumeData.BytesPerCluster;

    //TODO alternate way to calculate MFT total records count is
    // totalMFTRecCount = FVolumeData.MftValidDataLength.QuadPart / FVolumeData.BytesPerMFTRec;

    // recalc LCNs into MFT records, FMFTRecordsCount is in MFT records here
    assert(0 == (FRecordsCount * FVolumeData.BytesPerCluster) % FVolumeData.BytesPerMFTRec);
    FRecordsCount = (FRecordsCount * FVolumeData.BytesPerCluster) / FVolumeData.BytesPerMFTRec;

    auto expct = ReadMetaFilesCount(prsr);
    assert(expct);
    FMetaFilesCount = expct.value();
}

void TFileImageRecordsLoader::Close()
{
    IRecordsLoader::Close();

    CloseHandle(FHFile);
    FHFile = INVALID_HANDLE_VALUE;
    FMFTDataRuns.Clear();
    FPartitionOffset = 0;

    SetOpened(false);
}

    // returns error code WrongMFTRecID when: 
    //   - incorrect record ID specified (out of range) 
    // IOError when
    //   - attemt to read outside of a file
    //   - cannot read needed BytesPerMFTRec bytes from a file
    // MFTRecordNotInUse when 
    //   - requested rec ID is inside of range but record does not contain 'FILE' signature
    // CorruptedData when
    //   - FixupUSA call failed
TErrorCode TFileImageRecordsLoader::LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData)
{
    assert(IsOpened());
    assert(FRecordsCount > 0);

    // check that MFT Rec ID is less than MFT table size
    if (mftRecRef.sId.low >= FRecordsCount)
        return TErrorCode::WrongMFTRecID;

    auto offset = MFTRecIdToOffset(mftRecRef.sId.low);
    if (offset == -1) return TErrorCode::WrongMFTRecID; // MFT rec ID is out of MFT bounds

    assert(offset > 0); // offset>=0 must be

    if (!SetFilePointer(FHFile, (uint32_t)FPartitionOffset + (uint32_t)offset, 0, FILE_BEGIN))
        return TErrorCode::IOError;

    DWORD bytesRead = 0;
    if (!(ReadFile(FHFile, mftRecData, FVolumeData.BytesPerMFTRec, &bytesRead, nullptr) && (bytesRead == FVolumeData.BytesPerMFTRec)))
        return TErrorCode::IOError;

    // check that we've read record with proper signature
    NTFS_RECORD_HEADER* mftRec = (NTFS_RECORD_HEADER*)mftRecData;
    if (!ntfs_is_file_recp(mftRec->Signature)) // MFT rec must contain 'FILE' signature
    {
        GET_LOGGER;
        uint8_t* sign = mftRec->Signature;
        logger.WarnFmt("[LoadMFTRecord] Signature 'FILE' has not been found in MFT record {}. Signature found: {}{}{}{}",
            mftRecRef.sId.low, sign[0], sign[1], sign[2], sign[3]);

        //record is inside MFT table but it does not contain 'FILE' signature - consider it as Not In Use
        return TErrorCode::MFTRecordNotInUse;
    }

    // because we read MFT records directly from a file, we need to fixup USA in them.
    // when we read via DeviceIoControl WINAPI call then we do NOT need to fixup USA.
    auto res = FixupUsaMFTRec(mftRec);
    if (res != TErrorCode::Success) return res;

    return TErrorCode::Success;
}

TErrorCode TFileImageRecordsLoader::ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf)
{
    assert(IsOpened());

    LARGE_INTEGER offset{ 0 };
    DWORD bytesToRead, bytesRead;

    offset.QuadPart = FPartitionOffset + lcnStart * FVolumeData.BytesPerCluster;

    BOOL res = SetFilePointerEx(FHFile, offset, nullptr, FILE_BEGIN);
    if (!res)
    {
        GET_LOGGER;
        logger.ErrorFmt("ReadClusters.SetFilePointerEx() has failed with error: {}", GetLastError());
        return TErrorCode::IOError;
    }

    // read lcnCnt clusters
    bytesToRead = (DWORD)(lcnCnt * FVolumeData.BytesPerCluster);
    res = ReadFile(FHFile, dataBuf, bytesToRead, &bytesRead, nullptr);
    if (res)
    {
        assert(bytesToRead == bytesRead);
        return TErrorCode::Success;
    }
    else
    {
        GET_LOGGER;
        logger.ErrorFmt("ReadClusters.ReadFile() has failed with error: {}", GetLastError());
        return TErrorCode::IOError;
    }
}

// Applies Update Sequence Array (USA) to MFT record refered by dataBuf
TErrorCode TFileImageRecordsLoader::FixupUsaMFTRec(NTFS_RECORD_HEADER* mftRec)
{
    return FixupUSA1(mftRec, FVolumeData.BytesPerMFTRec, FVolumeData.BytesPerSector);
}

