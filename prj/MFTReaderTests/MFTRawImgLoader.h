#pragma once

#include "Readers.h"
#include "TestUtils.h"

class TMFTRawImageLoader : public IRecordLoader
{
private:
    HANDLE FHFile = INVALID_HANDLE_VALUE;
    uint64_t FPartitionOffset{ 0 };
    //uint64_t FMFTOffset{};
    TDataRuns FMFTDataRuns;
    uint64_t FMFTRecordsCount{ 0 };

public:
    bool EOMFT(MFTRecIndex id) { return id >= FMFTRecordsCount; }

    int64_t MFTRecIdToOffset(MFTRecIndex MFTRecID)
    {
        return MFTRecIdToOffset(MFTRecID, FMFTDataRuns, FVolumeData.BytesPerCluster, FVolumeData.BytesPerMFTRec);
    }

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

    TMFTRawImageLoader() {}
    TMFTRawImageLoader(const string_t& imgFileName) { OpenVolume(imgFileName); }
    ~TMFTRawImageLoader() { CloseVolume(); }

    //finds first MFT record and fills FMFTOffset with byte offset to this record from beginning of the image file 
    void OpenVolume(const string_t& imgFileName) override
    {
        FHFile = CreateFile(imgFileName.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        ASSERT_NE(INVALID_HANDLE_VALUE, FHFile) << "Error opening file '" << imgFileName << "'";;

        MBR_PARTITION_ENTRY mbr;
        NTFS_BOOT_SECTOR partNTFS;
        //uint64_t ntfsOff = 0; // partition not found
        DWORD off = 0x1BE;

        // look list of partitions for NTFS partition
        for (size_t i = 0; i < 4; i++)
        {
            if (!SetFilePointer(FHFile, off, 0, FILE_BEGIN))
                FAIL() << "Error setting file pointer '" << imgFileName << "'";

            DWORD bytesRead = 0, mbrsz = sizeof(mbr);

            if (!(ReadFile(FHFile, &mbr, mbrsz, &bytesRead, nullptr) && (bytesRead == mbrsz)))
                FAIL() << "Error reading file '" << imgFileName << "'";

            if (!SetFilePointer(FHFile, mbr.FirstLBA * DEFAULT_SECTOR_SIZE, 0, FILE_BEGIN))
                FAIL() << "Error setting file pointer '" << imgFileName << "'";

            if (!(ReadFile(FHFile, &partNTFS, sizeof(partNTFS), &bytesRead, nullptr) && (bytesRead == sizeof(partNTFS))))
                FAIL() << "Error reading file '" << imgFileName << "'";

            if (memcmp(partNTFS.OemId, NTFS_LABEL, 8) == 0)
            {
                FPartitionOffset = mbr.FirstLBA * (uint64_t)DEFAULT_SECTOR_SIZE;
                break;
            }

            off += sizeof(NTFS_BOOT_SECTOR);
        }

        ASSERT_NE(0, FPartitionOffset); // check that we've found NTFS partition

        FVolumeData.BytesPerSector = partNTFS.BytesPerSector;
        FVolumeData.TotalClusters.QuadPart = partNTFS.TotalSectors;
        FVolumeData.BytesPerCluster = partNTFS.BytesPerSector * partNTFS.SectorsPerCluster;
        FVolumeData.BytesPerMFTRec = (partNTFS.ClustersPerFileRecord >= 0) ? partNTFS.ClustersPerFileRecord * FVolumeData.BytesPerCluster : 1u << (-partNTFS.ClustersPerFileRecord);
        FVolumeData.ClustersPerFileRecordSegment = FVolumeData.BytesPerMFTRec / FVolumeData.BytesPerCluster;
        FVolumeData.hVolume = FHFile;
        FVolumeData.MftStartLcn.QuadPart = partNTFS.MftStartLcn;
        FVolumeData.MftZoneStart.QuadPart = partNTFS.MftStartLcn;
        FVolumeData.Mft2StartLcn.QuadPart = partNTFS.MftMirrorStartLcn;
        FVolumeData.Name = imgFileName;

        EXPECT_EQ(DEFAULT_SECTOR_SIZE, FVolumeData.BytesPerSector);
        EXPECT_EQ(DEFAULT_BYTES_PER_MFT_REC, FVolumeData.BytesPerMFTRec);

        // here where MFT record #0 located
        //FMFTOffset = FPartitionOffset + partNTFS.MftStartLcn * FVolumeData.BytesPerCluster;

        // reading MFT record #0, getting $MFT LCNs
        uint8_t* mftRecBuf = (uint8_t*)alloca(FVolumeData.BytesPerMFTRec);
        MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;
        MFT_REF mftRef{ {0,0,0} };

        // temporary values needed for proper work of LoadMFTRecord
        FMFTRecordsCount = 1;
        FMFTDataRuns.AddValue({ 1, 0, partNTFS.MftStartLcn });
        bool res = LoadMFTRecord(mftRef, mftRecBuf); // loading record #0 which is $MFT file
        ASSERT_TRUE(res) << "Error loading MFT record " << mftRef.sId.low;

        if (!ntfs_is_file_recp(mftRec->RecHeader.Signature) /* && !ntfs_is_magicp(mftRec->RecHeader.Signature, zero)*/)
            FAIL() << "MFT record with incorrect signature found " << mftRec->RecHeader.Signature << " (neither 'FILE' nor '0000')";

        // check that record #0 is in use
        ASSERT_TRUE((mftRec->Flags & MFT_FLAG_IN_USE) == 1);

        TMFTParserBase prsr(*this);
        TAttrCollection collection;
        prsr.FillAttrCollection(mftRec, collection);
        auto attr = collection.Get(ATTR_DATA);
        ASSERT_TRUE(attr != nullptr);

        FMFTDataRuns.Clear();
        res = prsr.DecodeDataRuns(attr, FMFTDataRuns);
        ASSERT_TRUE(res);
        ASSERT_GT(FMFTDataRuns.Count(), 0ul);

        FMFTRecordsCount = 0;
        for (auto& rli : FMFTDataRuns) FMFTRecordsCount += rli.len;

        // FMFTRecordsCount is in clusters here
        FVolumeData.MftZoneEnd.QuadPart = FVolumeData.MftStartLcn.QuadPart + FMFTRecordsCount;
        FVolumeData.MftValidDataLength.QuadPart = FMFTRecordsCount * FVolumeData.BytesPerCluster;

        // recalc LCNs into MFT records, FMFTRecordsCount is in MFT records here
        FMFTRecordsCount *= FVolumeData.BytesPerCluster / FVolumeData.BytesPerMFTRec;
    }

    // returns false when all records are loaded from a file
    bool LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData) override
    {
        // check that MFT Rec ID is less than MFT table size
        if (mftRecRef.sId.low >= FMFTRecordsCount)
            return false;

        auto offset = MFTRecIdToOffset(mftRecRef.sId.low);
        if (offset == -1) return false; // MFT rec ID is out of MFT bounds

        EXPECT_GE(offset, 0); // offset>=0 must be

        if (!SetFilePointer(FHFile, (uint32_t)FPartitionOffset + (uint32_t)offset, 0, FILE_BEGIN))
            return false;

        DWORD bytesRead = 0;
        if (!(ReadFile(FHFile, mftRecData, FVolumeData.BytesPerMFTRec, &bytesRead, nullptr) && (bytesRead == FVolumeData.BytesPerMFTRec)))
            return false;

        return true;
    }

    bool ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf)
    {
        LARGE_INTEGER offset{ 0 };
        DWORD bytesToRead, bytesRead;

        offset.QuadPart = FPartitionOffset + lcnStart * FVolumeData.BytesPerCluster;

        BOOL res = SetFilePointerEx(FHFile, offset, nullptr, FILE_BEGIN);
        if (!res)
        {
            GET_LOGGER;
            logger.ErrorFmt("ReadClusters.SetFilePointerEx() has failed with error: {}", GetLastError());
            return false;
        }

        // read lcnCnt clusters
        bytesToRead = (DWORD)(lcnCnt * FVolumeData.BytesPerCluster);
        res = ReadFile(FHFile, dataBuf, bytesToRead, &bytesRead, nullptr);
        if (res)
        {
            assert(bytesToRead == bytesRead);
            return true;
        }
        else
        {
            GET_LOGGER;
            logger.ErrorFmt("ReadClusters.ReadFile() has failed with error: {}", GetLastError());
            return false;
        }
    }

};