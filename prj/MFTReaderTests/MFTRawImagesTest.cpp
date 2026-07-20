
#include "gtest/gtest.h"
#include "Readers.h"
#include "TestUtils.h"
#include "MFTBaseParamTest.h"

class TMFTRawImageLoader : public IRecordLoader
{
private:
    HANDLE FHFile = INVALID_HANDLE_VALUE;
    uint64_t FPartitionOffset{};
    uint64_t FMFTOffset{};
    TDataRuns FMFTDataRuns;
    uint64_t FMFTRecordsCount{};

public:
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
        DATA_RUN_ITEM rli{0};
         
        for(uint i = 0; i < runs.Count(); i++)
        {
            rli = runs[i];
            EXPECT_GT(rli.len, 0ul); // len>0
            sum += rli.len*k;
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

            if (!(ReadFile(FHFile, &partNTFS, sizeof(partNTFS), &bytesRead, nullptr) && (bytesRead == sizeof(partNTFS))) )
                FAIL() << "Error reading file '" << imgFileName << "'";

            if (memcmp(partNTFS.OemId, NTFS_LABEL, 8) == 0)
            {
                FPartitionOffset = mbr.FirstLBA * (uint64_t)DEFAULT_SECTOR_SIZE;
                break;
            }

            off += sizeof(NTFS_BOOT_SECTOR);
        }

        ASSERT_NE(0, FPartitionOffset); // check that we've found NTFS partition
       
        EXPECT_EQ(DEFAULT_SECTOR_SIZE, partNTFS.BytesPerSector);
        EXPECT_EQ(8, partNTFS.SectorsPerCluster);

        FVolumeData.TotalClusters.QuadPart = partNTFS.TotalSectors;
        FVolumeData.BytesPerCluster = partNTFS.BytesPerSector * partNTFS.SectorsPerCluster;
        FVolumeData.BytesPerMFTRec = (partNTFS.ClustersPerFileRecord >= 0)? partNTFS.ClustersPerFileRecord * FVolumeData.BytesPerCluster: 1u << (-partNTFS.ClustersPerFileRecord);
        EXPECT_EQ(FVolumeData.BytesPerMFTRec, DEFAULT_BYTES_PER_MFT_REC);
        FVolumeData.BytesPerSector = partNTFS.BytesPerSector;
        FVolumeData.ClustersPerFileRecordSegment = FVolumeData.BytesPerMFTRec/FVolumeData.BytesPerCluster;
        FVolumeData.hVolume = FHFile;
        FVolumeData.MftZoneStart.QuadPart = partNTFS.MftStartLcn;
        FVolumeData.MftZoneEnd.QuadPart = 1000;
        FVolumeData.Name = imgFileName;

        // here where MFT record #0 located
        FMFTOffset = FPartitionOffset + partNTFS.MftStartLcn * FVolumeData.BytesPerCluster;

        // reading MFT record #0, getting $MFT LCNs
        uint8_t* mftRecBuf = (uint8_t*)alloca(FVolumeData.BytesPerMFTRec);
        MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;
        MFT_REF mftRef{ {0,0,0} };
 
        // temporary values needed for proper work of LoadMFTRecord
        FMFTRecordsCount = 1;
        FMFTDataRuns.AddValue({ 1, 0, 0 });
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
    }

    // returns false when all records are loaded from a file
    bool LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData) override
    {
        // check that MFT Rec ID is less than MFT table size
        if (mftRecRef.sId.low >= FMFTRecordsCount)
            return false;
        
        auto offset = MFTRecIdToOffset(mftRecRef.sId.low);
        EXPECT_GE(offset, 0); // offset>=0 must be
        EXPECT_NE(-1, offset);

        if (!SetFilePointer(FHFile, (uint32_t)FMFTOffset + (uint32_t)offset, 0, FILE_BEGIN))
            return false;

        
        DWORD bytesRead = 0;
        if (!(ReadFile(FHFile, mftRecData, FVolumeData.BytesPerMFTRec, &bytesRead, nullptr) && (bytesRead == FVolumeData.BytesPerMFTRec)))
            return false; //FAIL() << "Error reading file";

        return true;
    }

};

class MFTImgFileParserTest : public MFTStringParamTest
{
public:
    static void SetUpTestCase()
    {
        FName = "MFTImgFileParserParamTest";
        MFTStringParamTest::SetUpTestCase();
    }

    void SetUp() override
    {
        // it is important to call SetUp of the parent class here 
        MFTStringParamTest::SetUp();

        // add here your test initialization code
    }

    // You can implement all the usual fixture class members here.
    // To access the test parameter, call GetParam() from class TestWithParam<T>.
};

// Inside a test, access the test parameter with the GetParam() method of the TestWithParam<T> class:
TEST_P(MFTImgFileParserTest, ReadDiskImage_1)
{
    string_t imgFileName = GetParam();

    TMFTRawImageLoader tldr(imgFileName);
    TMFTStatCollector stat(tldr);

    // reading MFT record #0
    uint8_t* mftRecBuf = (uint8_t*)alloca(DEFAULT_BYTES_PER_MFT_REC);
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;
    MFT_REF mftRef{ 0 };
    mftRef.sId.low = 0;

    bool res;
    res = tldr.LoadMFTRecord(mftRef, mftRecBuf);
    ASSERT_TRUE(res) << "Error loading MFT record " << mftRef.sId.low;
    
    if (!ntfs_is_file_recp(mftRec->RecHeader.Signature) && !ntfs_is_magicp(mftRec->RecHeader.Signature, zero))
        FAIL() << "MFT record with incorrect signature found " << mftRec->RecHeader.Signature << " (neither 'FILE' nor '0000')";

    ITEM_INFO item;
    // try to parse only 'FILE' records
    if (ntfs_is_file_recp(mftRec->RecHeader.Signature))
        res = stat.ReadMftItemInfoBuf(mftRec, item);
    ASSERT_TRUE(res) << "Error parsing MFT record " << mftRef.sId.low;

}

// Inside a test, access the test parameter with the GetParam() method of the TestWithParam<T> class:
//TODO looks like this test will be called several times depending on number of .raw files being tested (see INSTANTIATE_TEST_CASE_P below)
// think how to make it called only once
TEST_P(MFTImgFileParserTest, MFTRecIdToOffset_1)
{
    const uint32_t DEF_CLUSTER_SIZE = 4096;

    TDataRuns runs;
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(100, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.AddValue({ 1, 0, 5 });
    EXPECT_EQ(5*DEF_CLUSTER_SIZE + 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 1024, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 2048, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 3072, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(5, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(100, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 2, 0, 33 });
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024*0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024*1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024*2, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024*3, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024*4, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024*5, TMFTRawImageLoader::MFTRecIdToOffset(5, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024*6, TMFTRawImageLoader::MFTRecIdToOffset(6, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024*7, TMFTRawImageLoader::MFTRecIdToOffset(7, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(8, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(9, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 1, 0, 5 });
    runs.AddValue({ 1, 1, 33 });
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 1024 * 1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 1024 * 2, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 1024 * 3, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024 * (4-4), TMFTRawImageLoader::MFTRecIdToOffset(4, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024 * (5-4), TMFTRawImageLoader::MFTRecIdToOffset(5, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024 * (6-4), TMFTRawImageLoader::MFTRecIdToOffset(6, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024 * (7-4), TMFTRawImageLoader::MFTRecIdToOffset(7, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(8, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(9, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 2, 0, 5 });
    runs.AddValue({ 1, 1, 999 });
    runs.AddValue({ 1, 2, 44 });
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 1024 * 1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 1024 * 2, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 1024 * 3, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 1024 * 4, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 1024 * 5, TMFTRawImageLoader::MFTRecIdToOffset(5, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 1024 * 6, TMFTRawImageLoader::MFTRecIdToOffset(6, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 1024 * 7, TMFTRawImageLoader::MFTRecIdToOffset(7, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(999 * DEF_CLUSTER_SIZE + 1024 * (8-8), TMFTRawImageLoader::MFTRecIdToOffset(8, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(999 * DEF_CLUSTER_SIZE + 1024 * (9-8), TMFTRawImageLoader::MFTRecIdToOffset(9, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(999 * DEF_CLUSTER_SIZE + 1024 * (10-8), TMFTRawImageLoader::MFTRecIdToOffset(10, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(999 * DEF_CLUSTER_SIZE + 1024 * (11-8), TMFTRawImageLoader::MFTRecIdToOffset(11, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(44 * DEF_CLUSTER_SIZE + 1024 * (12-12), TMFTRawImageLoader::MFTRecIdToOffset(12, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(44 * DEF_CLUSTER_SIZE + 1024 * (13-12), TMFTRawImageLoader::MFTRecIdToOffset(13, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(44 * DEF_CLUSTER_SIZE + 1024 * (14-12), TMFTRawImageLoader::MFTRecIdToOffset(14, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(44 * DEF_CLUSTER_SIZE + 1024 * (15-12), TMFTRawImageLoader::MFTRecIdToOffset(15, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(16, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(17, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(10000, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
}

INSTANTIATE_TEST_CASE_P(DiskImage1, MFTImgFileParserTest, testing::Values(_T(TEST_DATA_DIR "ntfs-ptrn.raw")));

