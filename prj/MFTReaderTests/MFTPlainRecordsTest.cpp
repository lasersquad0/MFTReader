	
#include "gtest/gtest.h"
#include "Readers.h"
#include "TestUtils.h"
#include "MFTBaseParamTest.h"

// reads file with MFT records
// file has no internal structure, just MFT records one-by-one
class TMFTPlainRecordsLoader : public IRecordLoader
{
private:
    std::ifstream FFile;
public:
    TMFTPlainRecordsLoader() {}
    TMFTPlainRecordsLoader(const string_t& fileName) { OpenVolume(fileName); }
    ~TMFTPlainRecordsLoader() { CloseVolume(); }

    void OpenVolume(const string_t& fileName) override
    {
        FFile.open(fileName, std::ios::binary);
        if (!FFile) FAIL() << "Error opening file '" << fileName << "'";

        FVolumeData.TotalClusters.QuadPart = 1'074'790'400;
        FVolumeData.BytesPerCluster = 4096;
        FVolumeData.BytesPerFileRecordSegment = DEFAULT_BYTES_PER_MFT_REC;
        FVolumeData.BytesPerSector = DEFAULT_SECTOR_SIZE;
        FVolumeData.ClustersPerFileRecordSegment = 0;
        FVolumeData.hVolume = 0;
        FVolumeData.MftZoneStart.QuadPart = 0;
        FVolumeData.MftZoneEnd.QuadPart = 1000;
        FVolumeData.Name = fileName;
    }

    void CloseVolume() override
    {
        FFile.close();
        IRecordLoader::CloseVolume();
    }

    //returns false when all records are loaded from a file
    bool LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData) override
    {
        if (!FFile.seekg(FVolumeData.BytesPerMFTRec * (uint64_t)mftRecRef.sId.low, std::ios::beg))
            return false;

        if (FFile.read(reinterpret_cast<char*>(mftRecData), FVolumeData.BytesPerMFTRec))
            return true;
        else
            return false;
    }

};

class MFTPlainRecordsTest : public MFTStringParamTest
{
public:
    static void SetUpTestCase()
    {
        FName = "MFTPlainRecordsTest";
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

TEST_F(MFTPlainRecordsTest, DISABLED_ReadMftItemInfoBuf_1)
{
    auto& fileName = GetParam();
    TMFTPlainRecordsLoader tldr(fileName);
    TMFTStatCollector stat(tldr);

    uint8_t buf[1024];
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)buf;
    MFT_REF mftRef{ 0 };

    while (true)
    {
        ITEM_INFO item; // item declaration should be here in the loop   

        if ((mftRef.sId.low != 0) && (mftRef.sId.low != 9) && (mftRef.sId.low != 24) &&
            (mftRef.sId.low != 25) && (mftRef.sId.low != 26))
        {
            bool res;
            res = tldr.LoadMFTRecord(mftRef, buf);
            if (!res) break; // we loaded all records from a file

            if (!ntfs_is_file_recp(mftRec->RecHeader.Signature) && !ntfs_is_magicp(mftRec->RecHeader.Signature, zero))
                FAIL() << "MFT record with incorrect signature found " << mftRec->RecHeader.Signature << " (neither 'FILE' nor '0000')";

            // try to parse only 'FILE' records
            if (ntfs_is_file_recp(mftRec->RecHeader.Signature))
                res = stat.ReadMftItemInfoBuf(mftRec, item);
            EXPECT_TRUE(res);

            // when assert() fails inside calling function then Google Test aborts immediately, and do not execute remaining tests
            // to prevent this, cover calling function into ASSERT_DEATH macro.
            //ASSERT_DEATH(res = stat.ReadMftItemInfoBuf(mftRec, item), "dfdfdf"); 
        }

        mftRef.sId.low++;
    }
}


INSTANTIATE_TEST_CASE_P(PlainRecords1, MFTPlainRecordsTest, testing::Values(_T(TEST_DATA_DIR "compressed_sparse.mft")));
INSTANTIATE_TEST_CASE_P(PlainRecords2, MFTPlainRecordsTest, testing::Values(_T(TEST_DATA_DIR "deleted.mft")));
INSTANTIATE_TEST_CASE_P(PlainRecords3, MFTPlainRecordsTest, testing::Values(_T(TEST_DATA_DIR "different_la.mft")));