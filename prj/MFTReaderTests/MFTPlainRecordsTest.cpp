	
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
    uint64_t FMFTRecordsCount;
public:
    TMFTPlainRecordsLoader() {}
    TMFTPlainRecordsLoader(const string_t& fileName) { OpenVolume(fileName); }
    ~TMFTPlainRecordsLoader() { CloseVolume(); }

    bool Eof() { return FFile.eof(); }

    void OpenVolume(const string_t& fileName) override
    {
        FFile.open(fileName, std::ios::binary);
        if (!FFile) FAIL() << "Error opening file '" << fileName << "'";
        
        FFile.exceptions(std::ios::failbit | std::ios::badbit);

        FVolumeData.TotalClusters.QuadPart = 1'074'790'400;
        FVolumeData.BytesPerCluster = DEFAULT_SECTOR_SIZE * 8;
        FVolumeData.BytesPerFileRecordSegment = DEFAULT_BYTES_PER_MFT_REC;
        FVolumeData.BytesPerSector = DEFAULT_SECTOR_SIZE;
        FVolumeData.ClustersPerFileRecordSegment = 0;
        FVolumeData.hVolume = INVALID_HANDLE_VALUE;
        FVolumeData.MftZoneStart.QuadPart = 0;
        FVolumeData.MftZoneEnd.QuadPart = 1000;
        FVolumeData.Name = std::filesystem::absolute(fileName); // make path absolute (fully qualified and without . and .. )

        // reading MFT record #0, getting $MFT LCNs
        uint8_t* mftRecBuf = (uint8_t*)alloca(FVolumeData.BytesPerMFTRec);
        MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;
        MFT_REF mftRef{ 0 };

        bool res = LoadMFTRecord(mftRef, mftRecBuf); // loading MFT record #0 which is $MFT file
        ASSERT_TRUE(res) << "Error loading MFT record " << mftRef.sId.low;

        if (!ntfs_is_file_recp(mftRec->RecHeader.Signature))
            FAIL() << "MFT record with incorrect signature found " << mftRec->RecHeader.Signature << " (expected 'FILE')";

        // check that record #0 is in use
        ASSERT_TRUE((mftRec->Flags & MFT_FLAG_IN_USE) == MFT_FLAG_IN_USE);

        TMFTParserBase prsr(*this);
        TAttrCollection collection;
        res = prsr.FillAttrCollection(mftRec, collection);
        ASSERT_TRUE(res) << "Error parsing attributes in MFT record buffer " << mftRef.sId.low;
        auto attr = collection.Get(ATTR_DATA);
        ASSERT_NE(nullptr, attr);

        TDataRuns dataRuns;
        res = prsr.DecodeDataRuns(attr, dataRuns);
        ASSERT_TRUE(res);
        ASSERT_EQ(1ul, dataRuns.Count());

        FMFTRecordsCount = dataRuns[0].len * FVolumeData.BytesPerCluster / FVolumeData.BytesPerMFTRec;
    }

    void CloseVolume() override
    {
        FFile.close();
        IRecordLoader::CloseVolume();
    }

    //returns false when all records are loaded from a file
    bool LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData) override
    {
        assert(!FFile.fail());

        try
        {
            FFile.seekg(FVolumeData.BytesPerMFTRec * (uint64_t)mftRecRef.sId.low/*, std::ios::beg*/);
            FFile.read(reinterpret_cast<char*>(mftRecData), FVolumeData.BytesPerMFTRec);
        }
        catch (const std::ios_base::failure& ex)
        {
            auto ec = ex.code();
            GET_LOGGER;
            logger.ErrorFmt("[LoadMFTRecord] FFile.seekg() has failed with error: {}", ec.value());

            return false;
        }

        return true;

        /*
        FFile.seekg(FVolumeData.BytesPerMFTRec * (uint64_t)mftRecRef.sId.low, std::ios::beg);
        if(FFile.fail())
            return false;

        if (FFile.read(reinterpret_cast<char*>(mftRecData), FVolumeData.BytesPerMFTRec))
            return true;
        else
            return false;
        */

        //TODO shall we add FixupUSA1 call here?
    }

    bool ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf) override
    {
        try
        {
            FFile.seekg(lcnStart * FVolumeData.BytesPerCluster/*, std::ios::beg*/);
            FFile.read(reinterpret_cast<char*>(dataBuf), lcnCnt * FVolumeData.BytesPerCluster);
        }
        catch (const std::ios_base::failure& ex)
        {
            auto ec = ex.code();
            GET_LOGGER;
            logger.ErrorFmt("[ReadClusters] FFile.seekg() has failed with error: {}", ec.value());

            return false;
        }


        /*if (!FFile.seekg(lcnStart * FVolumeData.BytesPerCluster, std::ios::beg))
        {
            GET_LOGGER;
            logger.ErrorFmt("FFile.seekg() has failed with error: {}", errno);

            return false;
        }

        if (!FFile.read(reinterpret_cast<char*>(dataBuf), lcnCnt * FVolumeData.BytesPerCluster))
        {
            GET_LOGGER;
            logger.ErrorFmt("FFile.read() has failed with error: {}", errno);
            return false;
        }
        */
        return true;
    }

}; //TMFTPlainRecordsLoader


class MFTPlainRecordsTest : public MFTStringParamTest
{
public:
    static void SetUpTestCase()
    {
        FName = "MFTPlainRecordsTest";
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

TEST_P(MFTPlainRecordsTest, ReadMftItemInfoBuf_1)
{
    auto& fileName = GetParam();
    TMFTPlainRecordsLoader ldr(fileName);
    TMFTStatCollector stat(ldr, false); // DO NOT process non-resident attributes because .mft files do not contain this info

    uint8_t buf[DEFAULT_BYTES_PER_MFT_REC];
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)buf;
    MFT_REF mftRef{ 0 };

    while (!ldr.Eof())
    {
        ITEM_INFO item; // item declaration should be here in the loop   

        if (/*(mftRef.sId.low != 0) && */(mftRef.sId.low != 9) && (mftRef.sId.low != 24) &&
            (mftRef.sId.low != 25) /*&& (mftRef.sId.low != 26) */ )
        {
            bool res;
            res = ldr.LoadMFTRecord(mftRef, buf);
            if (ldr.Eof()) break;
            EXPECT_TRUE(res);

            if (!ntfs_is_file_recp(mftRec->RecHeader.Signature) && !ntfs_is_magicp(mftRec->RecHeader.Signature, zero))
                FAIL() << "MFT record with incorrect signature found " << mftRec->RecHeader.Signature << " (neither 'FILE' nor '0000')";

            // parse only 'IN USE' records, bypass free ones
            if (mftRec->Flags & MFT_FLAG_IN_USE)
            {
                // parse only 'FILE' records
                ASSERT_TRUE(ntfs_is_file_recp(mftRec->RecHeader.Signature));
                res = stat.ReadMftItemInfoBuf(mftRec, item);
                EXPECT_TRUE(res);
            }

            // when assert() fails inside calling function then Google Test aborts immediately, and do not execute remaining tests
            // to prevent this, cover calling function into ASSERT_DEATH macro.
            //ASSERT_DEATH(res = stat.ReadMftItemInfoBuf(mftRec, item), "dfdfdf"); 
        }

        mftRef.sId.low++;
    }
}

TEST_P(MFTPlainRecordsTest, ReadMftItemInfo_1)
{
    auto& fileName = GetParam();
    TMFTPlainRecordsLoader ldr(fileName);
    TMFTStatCollector stat(ldr, false); // DO NOT process non-resident attributes because .mft files do not contain this info

    MFT_REF mftRef{ 0 };
    bool res;

    while (true)
    {
        ITEM_INFO item; // item declaration should be here in the loop   

        if (/*(mftRef.sId.low != 0) && */(mftRef.sId.low != 9) && (mftRef.sId.low != 24) &&
            (mftRef.sId.low != 25) /*&& (mftRef.sId.low != 26) */)
        {
            res = stat.ReadMftItemInfo(mftRef, item);
            if (ldr.Eof()) break;
            EXPECT_TRUE(res);
        }

        mftRef.sId.low++;
    }
}

TEST_P(MFTPlainRecordsTest, DISABLED_GetMFTRecIdByPath_1)
{
    auto& fileName = GetParam();
    TMFTPlainRecordsLoader ldr(fileName);
    TMFTParserBase ps(ldr);

    THArray<std::pair<uint32_t, ci_string>> testData{
        {0, _T("c:\\$MFT")},
        {MFT_ROOT_REC_ID, _T("C")}, {MFT_ROOT_REC_ID, _T("C:")}, { MFT_ROOT_REC_ID, _T("C:\\")},
        {0, _T("")}, {0, _T("\\")}, {MFT_ROOT_REC_ID, _T("C:\"")}, {0, _T("\"")},
        {0, _T("\\\\\\")}, {0, _T("users")}, { 0, _T("users\\default")},
        {0, _T("d:")}, {0, _T("d:\\windows")}, { 0, _T("C:\\win")},
        {0, _T("C:\\windows\\4444\\5555\\5.txt")}, {0, _T("C:\\windows\\windows")},
        {0, _T("C:\\ProgramFiles")},{0, _T("C:\\Program  Files")},
        {58663, _T("C:\\windows")}, {58663, _T("c:\\WINDOWS")}, {58663, _T("C:\\WindowS")},
        {MFT_ROOT_REC_ID, _T("C:WindowS")}, {0, _T("D:WindowS")},
        {104227, _T("c:\\hiberfil.sys")}, {64, _T("c:\\pagefile.sys")},
        {57651, _T("c:\\program files")}, {57651, _T("c:\\Program Files")},
        {406846, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\")},
        {406846, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-bisect--helper.exe")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-branch.exe")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-column.exe")},
        {793004, _T("c:\\Windows\\WinSxS\\amd64_amdgpio2.inf.resources_31bf3856ad364e35_10.0.26100.1_ru-ru_973ad9b5977fab1e\\")},
        {98425, _T("c:\\Program Files\\Google\\Chrome\\Application")},
        {98426, _T("c:\\Program Files\\Google\\Chrome\\Application\\SetupMetrics")},
    };

    for (auto p : testData)
    {
        EXPECT_EQ(p.first, ps.GetMFTRecIdByPath(p.second));
    }

}

INSTANTIATE_TEST_CASE_P(PlainRecords1, MFTPlainRecordsTest, testing::Values(_T(TEST_DATA_DIR "compressed_sparse.mft")));
INSTANTIATE_TEST_CASE_P(PlainRecords2, MFTPlainRecordsTest, testing::Values(_T(TEST_DATA_DIR "deleted.mft")));
INSTANTIATE_TEST_CASE_P(PlainRecords3, MFTPlainRecordsTest, testing::Values(_T(TEST_DATA_DIR "different_la.mft")));