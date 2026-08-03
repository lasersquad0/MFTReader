
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
        FName = "MFTImgFileParserTest";
        MFTStringParamTest::SetUpTestCase(); // need for proper initialization of my logging system
    }

    void SetUp() override
    {
        // it is important to call SetUp of the parent class here 
        // need it for proper logging param name (instantiation name) 
        MFTStringParamTest::SetUp();

        // add here your test initialization code
    }

    // You can implement all the usual fixture class members here.
    // To access the test parameter, call GetParam() from class TestWithParam<T>.
};

#define IMG_PTRN_FILE     _T(TEST_DATA_DIR "ntfs-ptrn.raw")
#define IMG_INDEX_FILE    _T(TEST_DATA_DIR "ntfs_index.raw")
#define IMG_NTFS3_FILE    _T(TEST_DATA_DIR "ntfs3.raw")
#define IMG_KW_1_FILE     _T(TEST_DATA_DIR "ntfs-img-kw-1.dd")
#define IMG_DFR_16_FILE   _T(TEST_DATA_DIR "dfr-16-ntfs.dd")
#define IMG_2M_FILE       _T(TEST_DATA_DIR "ntfs-2m.raw")
#define IMG_RAMSLACK_FILE _T(TEST_DATA_DIR "ntfs-ramslack.raw")

struct VOLUME_FIGURES
{
    uint32_t TotalMFTRecs;
    uint32_t TotalNotInUse;
    uint32_t ChildMFTRecsCount;
    uint32_t FilesCount;
    uint32_t DirsCount;
    uint32_t SystemMFTRecs;

    bool operator==(const VOLUME_FIGURES& vf) const = default;
    //{ 
    //    return TotalMFTRecs == vf.TotalMFTRecs && TotalNotInUse == vf.TotalNotInUse && FilesCount == vf.FilesCount && DirsCount == vf.DirsCount;  };
};

static THash<string_t, VOLUME_FIGURES> ImgFileFigures{
    {IMG_PTRN_FILE,     {256, 219, 0, 6, 3, 36} },
    {IMG_INDEX_FILE,    {256, 173, 0, 50, 5, 36} },
    {IMG_NTFS3_FILE,    {1152, 1129, 0, 3, 2, 27} },
    {IMG_KW_1_FILE,     {48, 18, 0, 7, 5, 27} },
    {IMG_DFR_16_FILE,   {1104, 46, 0, 1022, 10, 64} },
    {IMG_RAMSLACK_FILE, {256, 221, 0, 4, 3, 36} },
    {IMG_2M_FILE,       {1104, 46, 0, 1041, 14, 20} },
};

TEST_P(MFTImgFileParserTest, DiskImageCheckMetaFilesCount_1)
{
    string_t imgFileName = GetParam();

    TMFTRawImageLoader tldr(imgFileName);
    
    // number of system MFT recs including Not In Use, until first non-system rec met
    ASSERT_EQ(ImgFileFigures[imgFileName].SystemMFTRecs, tldr.GetSystemFilesCount()); 
}

TEST_P(MFTImgFileParserTest, DISABLED_ReadDiskImageMFTRecordsOneByOne_1)
{
    string_t imgFileName = GetParam();

    TMFTRawImageLoader tldr(imgFileName);
    TMFTStatCollector stat(tldr);

    uint8_t* mftRecBuf = (uint8_t*)alloca(DEFAULT_BYTES_PER_MFT_REC);
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;
    MFT_REF mftRef{ 0 };
    uint32_t notInUseCount = 0, dirsCount = 0, filesCount = 0, 
             childItemsCount = 0, hiddenCount = 0;
    TErrorCode res;

    while (!tldr.Eof(mftRef.sId.low))
    {
        if (mftRef.sId.low != 9)
        {
            res = tldr.LoadMFTRecord(mftRef, mftRecBuf); // function checks signature (should be 'FILE'), returns NotInUse error when signature <>'FILE'
            if (res == TErrorCode::MFTRecordNotInUse) 
            {
                // bypass records that do not contain FILE signature (consider them as NotInUse)
                notInUseCount++;
            }
            else
            {
                // no need to check signature here because LoadMFTRecord checks signature for 'FILE'

                ASSERT_EQ(TErrorCode::Success, res) << "Error loading MFT record " << mftRef.sId.low;

                ITEM_INFO item;

                res = stat.ReadMftItemInfoBuf(mftRec, item); // MFTRecordNotInUse is valid return value
                if (res == TErrorCode::MFTRecordNotInUse)
                {
                    notInUseCount++;
                }
                else
                {
                    ASSERT_EQ(TErrorCode::Success, res) << "Error parsing MFT record " << mftRef.sId.low;

                    assert((mftRec->Flags & MFT_FLAG_IN_USE) == MFT_FLAG_IN_USE);

                    if (mftRec->ParentFileRec.Id == 0) //base record
                    {
                        // bypass all "system" files which start from '$'
                        // these "system" files may have empty file name (does not contain ATTR_FILENAME attribute)
                        if ((item.MainName.size() == 0) || (item.MainName[0] == L'$')) 
                            hiddenCount++;
                        else
                            ((mftRec->Flags & MFT_FLAG_IS_DIRECTORY) == MFT_FLAG_IS_DIRECTORY) ? dirsCount++ : filesCount++;
                    }
                    else childItemsCount++;
                }
            }
        }

        mftRef.sId.low++;
    }

    ASSERT_EQ(mftRef.sId.low, dirsCount + filesCount + childItemsCount + notInUseCount + hiddenCount + 1);

    EXPECT_EQ(ImgFileFigures[imgFileName].TotalMFTRecs, mftRef.sId.low);
    EXPECT_EQ(ImgFileFigures[imgFileName].TotalNotInUse, notInUseCount);
    EXPECT_EQ(ImgFileFigures[imgFileName].FilesCount, filesCount);
    EXPECT_EQ(ImgFileFigures[imgFileName].DirsCount, dirsCount);
    EXPECT_EQ(ImgFileFigures[imgFileName].ChildMFTRecsCount, childItemsCount);
}

TEST_P(MFTImgFileParserTest, ReadDiskImageRootAndGoSubDirs_1)
{
    string_t imgFileName = GetParam();

    TMFTRawImageLoader tldr(imgFileName);
    TMFTStatCollector stat(tldr);

    MFT_REF startId{ 0 };
    startId.Id = MFT_ROOT_REC_ID;

    if (TErrorCode::Success != stat.ReadMftItems(startId, 0, nullptr))
        FAIL() << "ReadMftItems() returned error!";
   
    auto& ItemsList = stat.GetItemsList();

    auto DirsCount = 1 + std::count_if(ItemsList.begin(), ItemsList.end(), [](ITEM_INFO& a) { return a.IsDir(); });

    EXPECT_EQ(ImgFileFigures[imgFileName].FilesCount, ItemsList.Count() - DirsCount);
    EXPECT_EQ(ImgFileFigures[imgFileName].DirsCount, DirsCount);
}

TEST_P(MFTImgFileParserTest, DISABLED_ReadDiskImageRootAndGoSubDirs_WINAPI)
{
    //string_t imgFileName = GetParam();

    TMFTRecordLoader tldr(_T("d:\\"));
    TMFTStatCollector stat(tldr);

    MFT_REF startId{ 0 };
    startId.Id = MFT_ROOT_REC_ID;

    if (TErrorCode::Success != stat.ReadMftItems(startId, 0, nullptr))
        FAIL() << "ReadMftItems() returned error!";
}

INSTANTIATE_TEST_CASE_P(NTFS_PTRN_RAW, MFTImgFileParserTest, testing::Values(IMG_PTRN_FILE));
INSTANTIATE_TEST_CASE_P(NTFS_INDEX_RAW, MFTImgFileParserTest, testing::Values(IMG_INDEX_FILE));
INSTANTIATE_TEST_CASE_P(NTFS3_RAW, MFTImgFileParserTest, testing::Values(IMG_NTFS3_FILE));
INSTANTIATE_TEST_CASE_P(NTFS_IMG_KW_1_DD, MFTImgFileParserTest, testing::Values(IMG_KW_1_FILE));
INSTANTIATE_TEST_CASE_P(DFR_16_NTFS_DD, MFTImgFileParserTest, testing::Values(IMG_DFR_16_FILE));
INSTANTIATE_TEST_CASE_P(NTFS_RAMSLACK_RAW, MFTImgFileParserTest, testing::Values(IMG_RAMSLACK_FILE));

// there is proble with ntfs-2.raw file - reference to #0 MFT record is incorrect. It refers to empty MFT record.
// while I see that the file has a number of FILE record inside.
//INSTANTIATE_TEST_CASE_P(NTFS_2M_RAW, MFTImgFileParserTest, testing::Values(IMG_2M_FILE));



//TODO looks like this test will be called several times depending on number of .raw files being tested (see INSTANTIATE_TEST_CASE_P below)
// Test is lightweight, it should not be a problem to call it several times.
TEST_P(MFTImgFileParserTest, MFTRecIdToOffset_4096)
{
    const uint32_t DEF_CLUSTER_SIZE = 4096;

    TDataRuns runs;
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(100, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.AddValue({ 1, 0, 5 });
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 1024, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 2048, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * DEF_CLUSTER_SIZE + 3072, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(5, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(100, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 2, 0, 33 });
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024 * 1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024 * 2, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024 * 3, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024 * 4, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024 * 5, TMFTRawImageLoader::MFTRecIdToOffset(5, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024 * 6, TMFTRawImageLoader::MFTRecIdToOffset(6, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024 * 7, TMFTRawImageLoader::MFTRecIdToOffset(7, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
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
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024 * (4 - 4), TMFTRawImageLoader::MFTRecIdToOffset(4, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024 * (5 - 4), TMFTRawImageLoader::MFTRecIdToOffset(5, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024 * (6 - 4), TMFTRawImageLoader::MFTRecIdToOffset(6, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * DEF_CLUSTER_SIZE + 1024 * (7 - 4), TMFTRawImageLoader::MFTRecIdToOffset(7, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
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
    EXPECT_EQ(999 * DEF_CLUSTER_SIZE + 1024 * (8 - 8), TMFTRawImageLoader::MFTRecIdToOffset(8, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(999 * DEF_CLUSTER_SIZE + 1024 * (9 - 8), TMFTRawImageLoader::MFTRecIdToOffset(9, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(999 * DEF_CLUSTER_SIZE + 1024 * (10 - 8), TMFTRawImageLoader::MFTRecIdToOffset(10, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(999 * DEF_CLUSTER_SIZE + 1024 * (11 - 8), TMFTRawImageLoader::MFTRecIdToOffset(11, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(44 * DEF_CLUSTER_SIZE + 1024 * (12 - 12), TMFTRawImageLoader::MFTRecIdToOffset(12, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(44 * DEF_CLUSTER_SIZE + 1024 * (13 - 12), TMFTRawImageLoader::MFTRecIdToOffset(13, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(44 * DEF_CLUSTER_SIZE + 1024 * (14 - 12), TMFTRawImageLoader::MFTRecIdToOffset(14, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(44 * DEF_CLUSTER_SIZE + 1024 * (15 - 12), TMFTRawImageLoader::MFTRecIdToOffset(15, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(16, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(17, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(10000, runs, DEF_CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
}

TEST_P(MFTImgFileParserTest, MFTRecIdToOffset_512)
{
    const uint32_t CLUSTER_SIZE = 512;

    TDataRuns runs;
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(100, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.AddValue({ 1, 0, 5 });  // DataRun len is not divisible by DEFAULT_BYTES_PER_MFT_REC
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(100, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 2, 0, 2 });
    EXPECT_EQ(2 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(100'000, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 3, 0, 13 });  // Data Run len is not divisible by DEFAULT_BYTES_PER_MFT_REC
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(555, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(1'000'000, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 4, 0, 0 });
    EXPECT_EQ(0 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(0 * CLUSTER_SIZE + 1024 * 1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999'999, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 5, 0, 33 }); // Data Run len is not divisible by DEFAULT_BYTES_PER_MFT_REC
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(9, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 6, 0, 33 });
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * 1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * 2, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(6, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(55, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 7, 0, 333'333 });  // Data Run len is not divisible by DEFAULT_BYTES_PER_MFT_REC
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(5, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(55, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 8, 0, 333'333 });
    EXPECT_EQ(333'333 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(333'333 * CLUSTER_SIZE + 1024 * 1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(333'333 * CLUSTER_SIZE + 1024 * 2, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(333'333 * CLUSTER_SIZE + 1024 * 3, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(5, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(55, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 9, 0, 333'333 });  // Data Run len is not divisible by DEFAULT_BYTES_PER_MFT_REC
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(5, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(6, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(7, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(77, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 2, 0, 55 });
    runs.AddValue({ 1, 1, 33 });
    EXPECT_EQ(55 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC)); // because second Data Run contains len that is not divisible by DEFAULT_BYTES_PER_MFT_REC 
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(8, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(9, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 2, 0, 55 });
    runs.AddValue({ 2, 1, 33 });
    EXPECT_EQ(55 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * (1 - 1), TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(6, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 4, 0, 55 });
    runs.AddValue({ 4, 1, 33 });
    EXPECT_EQ(55 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(55 * CLUSTER_SIZE + 1024 * 1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * (2 - 2), TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * (3 - 2), TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(5, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(6, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 16, 0, 5 });
    runs.AddValue({ 8, 1, 999 });
    runs.AddValue({ 8, 2, 44 });
    EXPECT_EQ(5 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * CLUSTER_SIZE + 1024 * 1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * CLUSTER_SIZE + 1024 * 2, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * CLUSTER_SIZE + 1024 * 3, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * CLUSTER_SIZE + 1024 * 4, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * CLUSTER_SIZE + 1024 * 5, TMFTRawImageLoader::MFTRecIdToOffset(5, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * CLUSTER_SIZE + 1024 * 6, TMFTRawImageLoader::MFTRecIdToOffset(6, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * CLUSTER_SIZE + 1024 * 7, TMFTRawImageLoader::MFTRecIdToOffset(7, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(999 * CLUSTER_SIZE + 1024 * (8 - 8), TMFTRawImageLoader::MFTRecIdToOffset(8, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(999 * CLUSTER_SIZE + 1024 * (9 - 8), TMFTRawImageLoader::MFTRecIdToOffset(9, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(999 * CLUSTER_SIZE + 1024 * (10 - 8), TMFTRawImageLoader::MFTRecIdToOffset(10, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(999 * CLUSTER_SIZE + 1024 * (11 - 8), TMFTRawImageLoader::MFTRecIdToOffset(11, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(44 * CLUSTER_SIZE + 1024 * (12 - 12), TMFTRawImageLoader::MFTRecIdToOffset(12, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(44 * CLUSTER_SIZE + 1024 * (13 - 12), TMFTRawImageLoader::MFTRecIdToOffset(13, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(44 * CLUSTER_SIZE + 1024 * (14 - 12), TMFTRawImageLoader::MFTRecIdToOffset(14, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(44 * CLUSTER_SIZE + 1024 * (15 - 12), TMFTRawImageLoader::MFTRecIdToOffset(15, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(16, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(17, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(10000, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
}

TEST_P(MFTImgFileParserTest, MFTRecIdToOffset_1024)
{
    const uint32_t CLUSTER_SIZE = 1024;

    TDataRuns runs;
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(100, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.AddValue({ 1, 0, 5 }); 
    EXPECT_EQ(5 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(100, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 2, 0, 2 });
    EXPECT_EQ(2 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(2 * CLUSTER_SIZE + 1024 * 1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(100'000, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 3, 0, 13 });
    EXPECT_EQ(13 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(13 * CLUSTER_SIZE + 1024 * 1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(13 * CLUSTER_SIZE + 1024 * 2, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(555, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(1'000'000, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 4, 0, 0 });
    EXPECT_EQ(0 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(0 * CLUSTER_SIZE + 1024 * 1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(0 * CLUSTER_SIZE + 1024 * 2, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(0 * CLUSTER_SIZE + 1024 * 3, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(5, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999'999, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 5, 0, 33 });
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * 1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * 2, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * 3, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * 4, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(5, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(6, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(7, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(9, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 6, 0, 33 });
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * 1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * 2, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * 3, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * 4, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * 5, TMFTRawImageLoader::MFTRecIdToOffset(5, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(6, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(7, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(8, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(55, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 7, 0, 333'333 }); 
    EXPECT_EQ(333'333 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(333'333 * CLUSTER_SIZE + 1024 * 1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(333'333 * CLUSTER_SIZE + 1024 * 2, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(333'333 * CLUSTER_SIZE + 1024 * 3, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(333'333 * CLUSTER_SIZE + 1024 * 4, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(333'333 * CLUSTER_SIZE + 1024 * 5, TMFTRawImageLoader::MFTRecIdToOffset(5, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(333'333 * CLUSTER_SIZE + 1024 * 6, TMFTRawImageLoader::MFTRecIdToOffset(6, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(7, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(8, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(9, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(55, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 1, 0, 55 });
    runs.AddValue({ 1, 1, 33 });
    EXPECT_EQ(55 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * (1 - 1), TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(8, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 2, 0, 55 });
    runs.AddValue({ 2, 1, 33 });
    EXPECT_EQ(55 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(55 * CLUSTER_SIZE + 1024 * 1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * (2 - 2), TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * (3 - 2), TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(5, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(6, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 1, 0, 55 });
    runs.AddValue({ 2, 1, 33 });
    EXPECT_EQ(55 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * (1 - 1), TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * (2 - 1), TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(6, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 2, 0, 55 });
    runs.AddValue({ 1, 1, 33 });
    EXPECT_EQ(55 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(55 * CLUSTER_SIZE + 1024 * 1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(33 * CLUSTER_SIZE + 1024 * (1 - 1), TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(6, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(999, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));

    runs.Clear();
    runs.AddValue({ 8, 0, 5 });
    runs.AddValue({ 4, 1, 999 });
    runs.AddValue({ 3, 2, 44 });
    EXPECT_EQ(5 * CLUSTER_SIZE + 1024 * 0, TMFTRawImageLoader::MFTRecIdToOffset(0, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * CLUSTER_SIZE + 1024 * 1, TMFTRawImageLoader::MFTRecIdToOffset(1, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * CLUSTER_SIZE + 1024 * 2, TMFTRawImageLoader::MFTRecIdToOffset(2, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * CLUSTER_SIZE + 1024 * 3, TMFTRawImageLoader::MFTRecIdToOffset(3, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * CLUSTER_SIZE + 1024 * 4, TMFTRawImageLoader::MFTRecIdToOffset(4, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * CLUSTER_SIZE + 1024 * 5, TMFTRawImageLoader::MFTRecIdToOffset(5, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * CLUSTER_SIZE + 1024 * 6, TMFTRawImageLoader::MFTRecIdToOffset(6, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(5 * CLUSTER_SIZE + 1024 * 7, TMFTRawImageLoader::MFTRecIdToOffset(7, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(999 * CLUSTER_SIZE + 1024 * (8 - 8), TMFTRawImageLoader::MFTRecIdToOffset(8, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(999 * CLUSTER_SIZE + 1024 * (9 - 8), TMFTRawImageLoader::MFTRecIdToOffset(9, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(999 * CLUSTER_SIZE + 1024 * (10 - 8), TMFTRawImageLoader::MFTRecIdToOffset(10, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(999 * CLUSTER_SIZE + 1024 * (11 - 8), TMFTRawImageLoader::MFTRecIdToOffset(11, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(44 * CLUSTER_SIZE + 1024 * (12 - 12), TMFTRawImageLoader::MFTRecIdToOffset(12, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(44 * CLUSTER_SIZE + 1024 * (13 - 12), TMFTRawImageLoader::MFTRecIdToOffset(13, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(44 * CLUSTER_SIZE + 1024 * (14 - 12), TMFTRawImageLoader::MFTRecIdToOffset(14, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(15, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(16, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
    EXPECT_EQ(-1, TMFTRawImageLoader::MFTRecIdToOffset(100'000, runs, CLUSTER_SIZE, DEFAULT_BYTES_PER_MFT_REC));
}
