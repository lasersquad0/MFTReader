
#include "gtest/gtest.h"
#include "Readers.h"
#include "TestUtils.h"
#include "MFTRawImgLoader.h"
#include "MFTBaseParamTest.h"


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


//TODO looks like this test will be called several times depending on number of .raw files being tested (see INSTANTIATE_TEST_CASE_P below)
// think how to make it called only once
TEST_P(MFTImgFileParserTest, DISABLED_MFTRecIdToOffset_1)
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


TEST_P(MFTImgFileParserTest, DISABLED_ReadDiskImagePlain_1)
{
    string_t imgFileName = GetParam();

    TMFTRawImageLoader tldr(imgFileName);
    TMFTStatCollector stat(tldr);

    uint8_t* mftRecBuf = (uint8_t*)alloca(DEFAULT_BYTES_PER_MFT_REC);
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;
    MFT_REF mftRef{ 0 };

    bool res;
    while (!tldr.EOMFT(mftRef.sId.low))
    {
        if (mftRef.sId.low != 9 && mftRef.sId.low != 24 && mftRef.sId.low != 25)
        {
            res = tldr.LoadMFTRecord(mftRef, mftRecBuf);
            ASSERT_TRUE(res) << "Error loading MFT record " << mftRef.sId.low;

            if (!ntfs_is_file_recp(mftRec->RecHeader.Signature) && !ntfs_is_magicp(mftRec->RecHeader.Signature, zero))
                FAIL() << "MFT record with incorrect signature found " << mftRec->RecHeader.Signature << " (neither 'FILE' nor '0000')";

            ITEM_INFO item;
            // try to parse only 'FILE' records, and only records in use
            if (ntfs_is_file_recp(mftRec->RecHeader.Signature) && ((mftRec->Flags & MFT_FLAG_IN_USE) > 0))
                res = stat.ReadMftItemInfoBuf(mftRec, item);
            ASSERT_TRUE(res) << "Error parsing MFT record " << mftRef.sId.low;
        }

        mftRef.sId.low++;
    }
}

TEST_P(MFTImgFileParserTest, DISABLED_ReadDiskImageFromRoot_1)
{
    string_t imgFileName = GetParam();

    TMFTRawImageLoader tldr(imgFileName);
    TMFTStatCollector stat(tldr);

    MFT_REF startId{ 0 };
    startId.Id = MFT_ROOT_REC_ID;

    if (!stat.ReadMftItems(startId, 0))
        FAIL() << "ReadMftItems() returned false!";
}

TEST_P(MFTImgFileParserTest, ReadDiskWINAPI_1)
{
    //string_t imgFileName = GetParam();

    TMFTRecordLoader tldr(_T("d:\\"));
    TMFTStatCollector stat(tldr);

    MFT_REF startId{ 0 };
    startId.Id = MFT_ROOT_REC_ID;

    if (!stat.ReadMftItems(startId, 0))
        FAIL() << "ReadMftItems() returned false!";
}


//INSTANTIATE_TEST_CASE_P(DiskImage1, MFTImgFileParserTest, testing::Values(_T(TEST_DATA_DIR "ntfs-ptrn.raw")));
INSTANTIATE_TEST_CASE_P(DiskImage2, MFTImgFileParserTest, testing::Values(_T(TEST_DATA_DIR "ntfs_index.raw")));
