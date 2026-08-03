	
#include "gtest/gtest.h"
#include "Readers.h"
#include "TestUtils.h"
#include "MFTBaseParamTest.h"

// reads files containing just MFT records (.mft)
// these are files where first bytes in a file are first bytes of first MFT record
// such files do not have internal structure, just MFT records one-by-one
class TMFTPlainRecordsLoader : public IRecordLoader
{
private:
    std::ifstream FFile;
    uint64_t FMFTRecordsCount = 0;
public:
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
        FVolumeData.Name = GetVolumeName(fileName);

        // reading MFT record #0, getting $MFT LCNs
        uint8_t* mftRecBuf = (uint8_t*)alloca(FVolumeData.BytesPerMFTRec);
        MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;
        MFT_REF mftRef{ 0 };

        FMFTRecordsCount = 1; //temporary
        TErrorCode res = LoadMFTRecord(mftRef, mftRecBuf); // loading MFT record #0 which is $MFT file
        ASSERT_EQ(TErrorCode::Success, res) << "Error loading MFT record " << mftRef.sId.low;

        if (!ntfs_is_file_recp(mftRec->RecHeader.Signature))
            FAIL() << "MFT record with incorrect signature found " << mftRec->RecHeader.Signature << " (expected 'FILE')";

        // check that record #0 is in use
        ASSERT_TRUE((mftRec->Flags & MFT_FLAG_IN_USE) == MFT_FLAG_IN_USE);

        TMFTParserBase prsr(*this);
        TAttrCollection collection;
        res = prsr.FillAttrCollection(mftRec, MakeAttrBitmask(ATTR_DATA), collection);
        ASSERT_EQ(TErrorCode::Success, res) << "Error parsing attributes in MFT record buffer " << mftRef.sId.low;
        
        auto& adata = collection.Get(ATTR_DATA);
        ASSERT_EQ(1ul, adata.Count()); // $MFT file contain only one ATR_DATA attribute

        auto attr = adata[0];
        TDataRuns dataRuns;
        res = prsr.DecodeDataRuns(attr, dataRuns);
        ASSERT_EQ(TErrorCode::Success, res);
        ASSERT_EQ(1ul, dataRuns.Count());

        FMFTRecordsCount = dataRuns[0].len * FVolumeData.BytesPerCluster / FVolumeData.BytesPerMFTRec;
    }

    void CloseVolume() override
    {
        FFile.close();
        IRecordLoader::CloseVolume();
    }

    //TODO think of logic described below. possibly need to change this logic.
    // returns error NotFound error when out of bounds record id is requested
    // also it sets eofbit bit in the FFile
    TErrorCode LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData) override
    {
        assert(!FFile.fail());

        if (mftRecRef.sId.low >= FMFTRecordsCount)
        {
            FFile.setstate(std::ios_base::eofbit);
            return TErrorCode::NotFound;
        }

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

            return TErrorCode::IOError;
        }

        return TErrorCode::Success;

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

    TErrorCode ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf) override
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

            return TErrorCode::IOError;
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
        return TErrorCode::Success;
    }

}; //TMFTPlainRecordsLoader


#define COMPRESSED_SPARSE_FILE _T(TEST_DATA_DIR "compressed_sparse.mft")
#define DELETED_FILE           _T(TEST_DATA_DIR "deleted.mft")
#define DIFFERENT_LA_FILE      _T(TEST_DATA_DIR "different_la.mft")

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

static THash<string_t, uint32_t> NotInUseCounts{
    {COMPRESSED_SPARSE_FILE, 220},
    {DELETED_FILE, 221 },
    {DIFFERENT_LA_FILE, 217}
};

TEST_P(MFTPlainRecordsTest, ReadMftItemInfoBuf_1)
{
    auto& fileName = GetParam();
    TMFTPlainRecordsLoader ldr(fileName);
    TMFTStatCollector stat(ldr, false); // DO NOT process non-resident attributes because .mft files do not contain this info

    uint8_t buf[DEFAULT_BYTES_PER_MFT_REC];
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)buf;
    MFT_REF mftRef{ 0 };
    uint32_t notInUseCount = 0;

    while (!ldr.Eof())
    {
        ITEM_INFO item; // item declaration should be here in the loop   

        if ((mftRef.sId.low != 9) && (mftRef.sId.low != 24) && (mftRef.sId.low != 25) )
        {
            TErrorCode res = ldr.LoadMFTRecord(mftRef, buf);
            if (ldr.Eof()) break;
            
            EXPECT_EQ(TErrorCode::Success, res);

            if (!ntfs_is_file_recp(mftRec->RecHeader.Signature) && !ntfs_is_magicp(mftRec->RecHeader.Signature, zero))
                FAIL() << "MFT record with incorrect signature found " << mftRec->RecHeader.Signature << " (neither 'FILE' nor '0000')";

            // parse only 'IN USE' records, bypass free ones
            if ((mftRec->Flags & MFT_FLAG_IN_USE) == MFT_FLAG_IN_USE)
            {
                // parse only 'FILE' records
                ASSERT_TRUE(ntfs_is_file_recp(mftRec->RecHeader.Signature));
                res = stat.ReadMftItemInfoBuf(mftRec, item);
                EXPECT_EQ(TErrorCode::Success, res);
            }
            else
            {
                notInUseCount++;
            }

            // when assert() fails inside calling function then Google Test aborts immediately, and do not execute remaining tests
            // to prevent this, cover calling function into ASSERT_DEATH macro.
            //ASSERT_DEATH(res = stat.ReadMftItemInfoBuf(mftRec, item), "dfdfdf"); 
        }

        mftRef.sId.low++;
    }

    // check that we processed all available in .mft files records.
    ASSERT_EQ(256ul, mftRef.sId.low);
    EXPECT_EQ(NotInUseCounts[fileName], notInUseCount);
}

TEST_P(MFTPlainRecordsTest, ReadMftItemInfo_1)
{
    auto& fileName = GetParam();
    TMFTPlainRecordsLoader ldr(fileName);
    TMFTStatCollector stat(ldr, false); // DO NOT process non-resident attributes because .mft files do not contain this info

    MFT_REF mftRef{ 0 };
    TErrorCode res;
    uint32_t notInUseCount = 0;

    while (!ldr.Eof())
    {
        ITEM_INFO item; // item declaration should be here in the loop   

        if ((mftRef.sId.low != 9) && (mftRef.sId.low != 24) && (mftRef.sId.low != 25) )
        {
            res = stat.ReadMftItemInfo(mftRef, item);
            if (ldr.Eof()) break;

            if (res == TErrorCode::MFTRecordNotInUse) // bypass not in use records
            {
                notInUseCount++;
            }
            else
            {
                EXPECT_EQ(TErrorCode::Success, res);
            }
        }

        mftRef.sId.low++;
    }

    // check that we processed all available in .mft files records.
    ASSERT_EQ(256ul, mftRef.sId.low);
    EXPECT_EQ(NotInUseCounts[fileName], notInUseCount);
}

TEST_P(MFTPlainRecordsTest, GetMFTRecIdByPath_1)
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
    };

    for (auto p : testData)
    {
        auto expctValue = ps.MFTRecIdByPath(p.second);
        if (expctValue)
            EXPECT_EQ(p.first, expctValue.value());
        else
            EXPECT_EQ(p.first, 0ul);
            //EXPECT_TRUE(false) << "GetMFTRecIdByPath failed with error: " << ErrorCodeNames[(uint8_t)expctValue.error()];
    }
}

// MFT IDs will be different for different disks, this test linked to disk C: only
TEST_P(MFTPlainRecordsTest, GetPathByMFTRecId_1)
{
    auto& fileName = GetParam();
    TMFTPlainRecordsLoader ldr(fileName);
    TMFTParserBase ps(ldr);

    THArray<std::pair<uint32_t, ci_string>> testData
    {
        {MFT_ROOT_REC_ID, _T("C:")},
        {0, _T("c:\\$MFT")},
        {1, _T("C:\\$MFTMirr")}, {2, _T("C:\\$LogFile")},
        {3, _T("C:\\$Volume")},
        {4, _T("c:\\$AttrDef")}, {6, _T("c:\\$Bitmap")},
        {57651, _T("NOT_FOUND")},
        {406846, _T("NOT_FOUND")},
    };

    for (auto p : testData)
    {
        THArray<std::wstring> paths;
        MFT_REF id{ p.first };
        paths.Clear();
        auto res = ps.PathByMFTRecID(id, paths);
        if (res == TErrorCode::Success)
            EXPECT_TRUE(EXPECT_ONE_OF(p.second, paths));
        else if (res == TErrorCode::NotFound)
            EXPECT_EQ(ci_string(_T("NOT_FOUND")), p.second);
        else
            FAIL() << "PathByMFTRecID returned error: " << ErrorCodeNames[(uint8_t)res];
    }
}


INSTANTIATE_TEST_CASE_P(PlainRecords1, MFTPlainRecordsTest, testing::Values(COMPRESSED_SPARSE_FILE));
INSTANTIATE_TEST_CASE_P(PlainRecords2, MFTPlainRecordsTest, testing::Values(DELETED_FILE));
INSTANTIATE_TEST_CASE_P(PlainRecords3, MFTPlainRecordsTest, testing::Values(DIFFERENT_LA_FILE));