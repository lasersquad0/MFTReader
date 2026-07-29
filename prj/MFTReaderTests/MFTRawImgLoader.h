#pragma once

#include "gtest/gtest.h"
#include "Readers.h"
#include "TestUtils.h"

class TMFTRawImageLoader : public IRecordLoader
{
private:
    HANDLE FHFile = INVALID_HANDLE_VALUE;
    uint64_t FPartitionOffset{ 0 }; // offset from beginning of the file where NTFS partition starts 
    uint64_t FMFTRecordsCount{ 0 }; // total number of MFT records in $MFT file
    TDataRuns FMFTDataRuns; // Data Runs of $MFT file, used to properly calc offsets for MFT records

public:
    TMFTRawImageLoader() {}
    TMFTRawImageLoader(const string_t& imgFileName) { OpenVolume(imgFileName); }
    ~TMFTRawImageLoader() { CloseVolume(); }

    bool Eof(MFTRecIndex id) const { return id >= FMFTRecordsCount; }

    int64_t MFTRecIdToOffset(MFTRecIndex MFTRecID)
    {
        return MFTRecIdToOffset(MFTRecID, FMFTDataRuns, FVolumeData.BytesPerCluster, FVolumeData.BytesPerMFTRec);
    }

    // MFT table may be fragmented. Fragments can be found in Data Runs in the first MFT record (file with name '$MFT', MFT rec #0)
    // This function calculates offset of MFT record MFTRecID taking into account MFT table fragmentation. 
    // This offset starts from first byte of NTFS partition. 
    static int64_t MFTRecIdToOffset(MFTRecIndex MFTRecID, TDataRuns& runs, uint32_t BytesPerCluster, uint32_t BytesPerMFTRec)
    {
        if (runs.Count() == 0) return -1;

        uint32_t k = BytesPerCluster / BytesPerMFTRec;
        EXPECT_NE(k, 0ul);

        uint64_t sum = 0;
        DATA_RUN_ITEM rli{ 0 };

        for (uint i = 0; i < runs.Count(); i++)
        {
            rli = runs[i];
            EXPECT_GT(rli.len, 0ul); // len>0
            sum += rli.len * k;
            if (sum > MFTRecID) break;
        }

        if (sum <= MFTRecID) return -1;

        EXPECT_GT(rli.len, 0ul); // len>0
        return (rli.lcn + rli.len) * BytesPerCluster - (sum - MFTRecID) * BytesPerMFTRec;
    }

    // finds first NTFS partition in the file, then finds first MFT record in this partition.
    // results are stored in FPartitionOffset, FMFTDataRuns, FMFTRecordsCount fields 
    void OpenVolume(const string_t& imgFileName) override
    {
        ASSERT_EQ(INVALID_HANDLE_VALUE, FHFile);

        FHFile = CreateFile(imgFileName.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        
        ASSERT_NE(INVALID_HANDLE_VALUE, FHFile) << "Error opening file '" << imgFileName << "'";;

        // try to find "NTFS   " in the beginning of the file (offset 3).
        // if found - file contain only NTFS partition without MBR
        DWORD bytesRead = 0;
        NTFS_BOOT_SECTOR partNTFS{ 0 };

        //if (SetFilePointer(FHFile, 0, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
        //    FAIL() << std::format(L"Error setting file pointer for file '{}', Error code: {}", imgFileName, GetLastError());

        if (!(ReadFile(FHFile, &partNTFS, sizeof(partNTFS), &bytesRead, nullptr) && (bytesRead == sizeof(partNTFS))))
            FAIL() << std::format(L"Error reading file '{}', Error code: {}", imgFileName, GetLastError());

        FPartitionOffset = 0;

        if (memcmp(partNTFS.OemId, NTFS_LABEL, 8) != 0)
        {
            // looks like this file contains MBR and list of partitions
            // go through partitions unless find NTFS partition
            //TODO extended partitions are not supported yet, need to add support.

            MBR_PARTITION_ENTRY mbr{ 0 };
            DWORD off = 0x1BE; // fixed offset to first partition entry

            // look at list of partitions and find NTFS partition. 4 is max number of standard partitions.
            for (size_t i = 0; i < 4; i++)
            {
                if (SetFilePointer(FHFile, off, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
                    FAIL() << std::format(L"Error setting file pointer for file '{}', Error code: {}", imgFileName, GetLastError());

                bytesRead = 0;

                if (!(ReadFile(FHFile, &mbr, sizeof(mbr), &bytesRead, nullptr) && (bytesRead == sizeof(mbr))))
                    FAIL() << std::format(L"Error reading file '{}', Error code: {}", imgFileName, GetLastError());

                if (SetFilePointer(FHFile, mbr.FirstLBA * DEFAULT_SECTOR_SIZE, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
                    FAIL() << std::format(L"Error setting file pointer for file '{}', Error code: {}", imgFileName, GetLastError());

                if (!(ReadFile(FHFile, &partNTFS, sizeof(partNTFS), &bytesRead, nullptr) && (bytesRead == sizeof(partNTFS))))
                    FAIL() << std::format(L"Error reading file '{}', Error code: {}", imgFileName, GetLastError());

                if (memcmp(partNTFS.OemId, NTFS_LABEL, 8) == 0)
                {
                    FPartitionOffset = mbr.FirstLBA * (uint64_t)DEFAULT_SECTOR_SIZE;
                    break;
                }

                off += sizeof(NTFS_BOOT_SECTOR);
            }

            ASSERT_NE(0, FPartitionOffset); // check that we've found NTFS partition
        }
        
        FVolumeData.BytesPerSector = partNTFS.BytesPerSector;
        FVolumeData.TotalClusters.QuadPart = partNTFS.TotalSectors;
        FVolumeData.BytesPerCluster = partNTFS.BytesPerSector * partNTFS.SectorsPerCluster;
        FVolumeData.BytesPerMFTRec = (partNTFS.ClustersPerFileRecord >= 0) ? partNTFS.ClustersPerFileRecord * FVolumeData.BytesPerCluster : 1u << (-partNTFS.ClustersPerFileRecord);
        FVolumeData.ClustersPerFileRecordSegment = FVolumeData.BytesPerMFTRec / FVolumeData.BytesPerCluster;
        FVolumeData.MftStartLcn.QuadPart  = partNTFS.MftStartLcn;
        FVolumeData.MftZoneStart.QuadPart = partNTFS.MftStartLcn;
        FVolumeData.Mft2StartLcn.QuadPart = partNTFS.MftMirrorStartLcn;
        FVolumeData.hVolume = FHFile;
        FVolumeData.Name = imgFileName;

        EXPECT_EQ(DEFAULT_SECTOR_SIZE, FVolumeData.BytesPerSector);
        EXPECT_EQ(DEFAULT_BYTES_PER_MFT_REC, FVolumeData.BytesPerMFTRec);

        // reading MFT record #0, getting $MFT LCNs
        uint8_t* mftRecBuf = (uint8_t*)alloca(FVolumeData.BytesPerMFTRec);
        MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;
        MFT_REF mftRef{ 0 };

        // these two temporary values needed for proper work of LoadMFTRecord
        FMFTRecordsCount = 1;
        FMFTDataRuns.AddValue({ 1, 0, partNTFS.MftStartLcn });
        TErrorCode res = LoadMFTRecord(mftRef, mftRecBuf); // loading MFT record #0 which is $MFT file
        ASSERT_EQ(TErrorCode::Success, res) << "Error loading MFT record " << mftRef.sId.low;

        if (!ntfs_is_file_recp(mftRec->RecHeader.Signature))
            FAIL() << "MFT record with incorrect signature found " << mftRec->RecHeader.Signature << " (expected 'FILE')";

        // check that record #0 is in use
        ASSERT_TRUE((mftRec->Flags & MFT_FLAG_IN_USE) == MFT_FLAG_IN_USE);

        TMFTParserBase prsr(*this);
        TAttrCollection collection;
        res = prsr.FillAttrCollection(mftRec, collection);
        ASSERT_EQ(TErrorCode::Success, res) << "Error parsing attributes in MFT record buffer " << mftRef.sId.low;

        auto attr = collection.Get(ATTR_DATA);
        ASSERT_NE(nullptr, attr);

        FMFTDataRuns.Clear();
        res = prsr.DecodeDataRuns(attr, FMFTDataRuns);
        ASSERT_EQ(TErrorCode::Success, res);
        ASSERT_GT(FMFTDataRuns.Count(), 0ul);

        FMFTRecordsCount = 0;
        for (auto& rli : FMFTDataRuns) FMFTRecordsCount += rli.len;

        // FMFTRecordsCount is in clusters here
        FVolumeData.MftZoneEnd.QuadPart = FVolumeData.MftStartLcn.QuadPart + FMFTRecordsCount;
        FVolumeData.MftValidDataLength.QuadPart = FMFTRecordsCount * FVolumeData.BytesPerCluster;

        // recalc LCNs into MFT records, FMFTRecordsCount is in MFT records here
        FMFTRecordsCount *= FVolumeData.BytesPerCluster / FVolumeData.BytesPerMFTRec;
    }

    void CloseVolume() override
    {
        IRecordLoader::CloseVolume();

        FMFTDataRuns.Clear();
        FPartitionOffset = 0;
        FMFTRecordsCount = 0;
    }

    // returns false when: 
    //   - incorrect record ID specified 
    //   - attemt to position outside of a file
    //   - cannot read needed BytesPerMFTRec bytes from a file
    //   - record does not contain FILE signature
    TErrorCode LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData) override
    {
        // check that MFT Rec ID is less than MFT table size
        if (mftRecRef.sId.low >= FMFTRecordsCount)
            return TErrorCode::WrongMFTRecID;

        auto offset = MFTRecIdToOffset(mftRecRef.sId.low);
        if (offset == -1) return TErrorCode::WrongMFTRecID; // MFT rec ID is out of MFT bounds

        EXPECT_GE(offset, 0); // offset>=0 must be

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

            return TErrorCode::WrongMFTRecID;
        }

        // because we read MFT records directly from a file, we need to fixup USA in them.
        // when we read via DeviceIoControl WINAPI call then we do NOT need to fixup USA.
        auto res = FixupUsaMFTRec(mftRec);
        if (res != TErrorCode::Success) return res;

        return TErrorCode::Success;
    }

    TErrorCode ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf)
    {
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
    TErrorCode FixupUsaMFTRec(NTFS_RECORD_HEADER* mftRec)
    {
        return FixupUSA1(mftRec, FVolumeData.BytesPerMFTRec, FVolumeData.BytesPerSector);
    }

};