
#include "gtest/gtest.h"
#include "Readers.h"

#define MFT_TESTS_LOGGER_NAME "mft_tests_logger"

class MFTParserBaseTests : public ::testing::Test
{
protected:
    static void SetUpTestCase()
    {
        LogEngine::Logger& logger = LogEngine::GetFileLoggerST(MFT_LOGGER_NAME, "LogMFTReaderTests.log");
        logger.SetLogLevel(LogEngine::Levels::llDebug);
        logger.Debug("MFTReaderBaseTests START");
    }
    static void TearDownTestCase()
    {
        LogEngine::Logger& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);
        logger.Debug("MFTReaderBaseTests FINISH");
        LogEngine::ShutdownLoggers();
    }
    /*
    static void SetUpTestSuite()  // Called one time BEFORE all TEST_F tests of this fixture
    {       
        LogEngine::Logger& logger = LogEngine::GetFileLoggerST(MFT_LOGGER_NAME, "LogMFTReaderTests.log");
        logger.SetLogLevel(LogEngine::Levels::llDebug);
    }

    static void TearDownTestSuite() // Called one time AFTER all TEST_F tests of this fixture
    {
        LogEngine::ShutdownLoggers();
    }*/
};

TEST_F(MFTParserBaseTests, ParseVolume_Empty)
{
    EXPECT_EQ(_T(""), IRecordLoader::ParseVolume(_T(""))); // no default value here, empty string indicates an error.
    EXPECT_EQ(_T("\\\\.\\C:"), IRecordLoader::ParseVolume(_T("C")));
    EXPECT_EQ(_T("\\\\.\\c:"), IRecordLoader::ParseVolume(_T("c")));
    EXPECT_EQ(_T("\\\\.\\C:"), IRecordLoader::ParseVolume(_T("C:")));
    EXPECT_EQ(_T("\\\\.\\c:"), IRecordLoader::ParseVolume(_T("c:")));

    EXPECT_EQ(_T("\\\\.\\D:"), IRecordLoader::ParseVolume(_T("D")));
    EXPECT_EQ(_T("\\\\.\\d:"), IRecordLoader::ParseVolume(_T("d")));
    EXPECT_EQ(_T("\\\\.\\D:"), IRecordLoader::ParseVolume(_T("D:")));
    EXPECT_EQ(_T("\\\\.\\d:"), IRecordLoader::ParseVolume(_T("d:")));

    EXPECT_EQ(_T("\\\\.\\ :"), IRecordLoader::ParseVolume(_T(" "))); // it does not check that disk letter is correct disk letter
    EXPECT_EQ(_T("\\\\.\\1:"), IRecordLoader::ParseVolume(_T("1")));
    EXPECT_EQ(_T("\\\\.\\*:"), IRecordLoader::ParseVolume(_T("*")));

    EXPECT_EQ(_T(""), IRecordLoader::ParseVolume(_T("\\\\.\\")));
    EXPECT_EQ(_T("\\\\.\\e:"), IRecordLoader::ParseVolume(_T("\\\\.\\e")));
    EXPECT_EQ(_T("\\\\.\\e:"), IRecordLoader::ParseVolume(_T("\\\\.\\e:")));
    EXPECT_EQ(_T("\\\\.\\e:"), IRecordLoader::ParseVolume(_T("\\\\.\\e:filename1")));
    EXPECT_EQ(_T("\\\\.\\fi"), IRecordLoader::ParseVolume(_T("\\\\.\\filename1")));//TODO not sure this is correct test

    EXPECT_TRUE(true);
}

class TMFTRecordLoaderTest : public IRecordLoader
{
private:
    std::ifstream FFile;
public:
    TMFTRecordLoaderTest() {}
    TMFTRecordLoaderTest(const string_t& fileName) { OpenVolume(fileName); }
    ~TMFTRecordLoaderTest() { CloseVolume(); }

    void OpenVolume(const string_t& fileName) override
    {
        FFile.open(fileName, std::ios::binary);
        if (!FFile) FAIL() << "Error opening file '" << fileName <<"'";

        FVolumeData.TotalClusters.QuadPart = 1'074'790'400;
        FVolumeData.BytesPerCluster = 4096;
        FVolumeData.BytesPerFileRecordSegment = 1024;
        FVolumeData.BytesPerSector = 512;
        FVolumeData.ClustersPerFileRecordSegment = 0;
        FVolumeData.hVolume = 0;
        FVolumeData.MftZoneStart.QuadPart = 0;
        FVolumeData.MftZoneEnd.QuadPart = 1000;
        FVolumeData.Name = L"\\\\.\\c:";
    }

    void CloseVolume() override
    {
        FFile.close();
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

    uint8_t* LoadMFTRecordCache(MFT_REF mftRecRef) override
    {
        uint8_t** result = FMFTRecCache.GetValuePointer(mftRecRef.sId.low);
        if (result == nullptr) // no value in cache, load MFT record from disk
        {
            uint8_t* mftRecBuf = DBG_NEW uint8_t[FVolumeData.BytesPerMFTRec];
            if (!LoadMFTRecord(mftRecRef, mftRecBuf))
                return nullptr; // error loading MFT record

            //we use mftRecRef.sId.low here because high part of mftRecRef.Id may change when MFT record is modified
            FMFTRecCache.SetValue(mftRecRef.sId.low, mftRecBuf); // update cache

            return mftRecBuf;
        }

        return *result; // return MFT record from cache
    }
};

#define TEST_DATA_DIR "../../TestData/"

TEST_F(MFTParserBaseTests, DISABLED_ReadMftItemInfoBuf_1)
{
    auto current_dir = std::filesystem::current_path();

    TMFTRecordLoaderTest tldr(_T(TEST_DATA_DIR "compressed_sparse.mft"));
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

TEST_F(MFTParserBaseTests, OpenVolume_1)
{
    TMFTRecordLoader ldr;
    ldr.OpenVolume(_T("c:")); // generates an exception if not run as Admin
    ldr.OpenVolume(_T("c"));
    ldr.OpenVolume(_T("c:\testfile"));
}

TEST_F(MFTParserBaseTests, OpenVolume_2)
{
    TMFTRecordLoader ldr;

    EXPECT_THROW(ldr.OpenVolume(_T("d:")), std::system_error); // generates an exception if drive not found
    EXPECT_THROW(ldr.OpenVolume(_T("f")), std::system_error); // generates an exception if drive not found

    EXPECT_THROW(ldr.OpenVolume(_T(":")), std::system_error);
    EXPECT_THROW(ldr.OpenVolume(_T("\\")), std::system_error);
    EXPECT_THROW(ldr.OpenVolume(_T("/")), std::system_error);

    EXPECT_THROW(ldr.OpenVolume(_T("::")), std::system_error);
    EXPECT_THROW(ldr.OpenVolume(_T("\\\\")), std::system_error);
    EXPECT_THROW(ldr.OpenVolume(_T("//")), std::system_error);

    EXPECT_THROW(ldr.OpenVolume(_T("\\.\\")), std::system_error);
    EXPECT_THROW(ldr.OpenVolume(_T("\\\\.\\")), std::system_error);
    EXPECT_THROW(ldr.OpenVolume(_T("\\\\.\\\\")), std::system_error);

}

TEST_F(MFTParserBaseTests, DISABLED_DecodeDataRuns_1)
{
    TMFTRecordLoader ldr;
    TMFTParserBase parser(ldr);

    std::string fileName = TEST_DATA_DIR "decoded-20260708121645.bin";
    std::ifstream file(fileName, std::ios::binary);
    if (!file) FAIL() << "Error opening file '" << fileName << "'";
    
    file.seekg(0, std::ios::end);
    uint32_t sz = file.tellg();
    file.seekg(0, std::ios::beg);

    uint8_t* buf = (uint8_t*)calloc(sizeof(MFT_ATTR_HEADER) + sz, 1);
    ASSERT_TRUE(buf != nullptr);

    MFT_ATTR_HEADER* attr = (MFT_ATTR_HEADER*)buf;
    attr->AttrType = ATTR_DATA;
    attr->AttrSize = sz + sizeof(MFT_ATTR_HEADER);
    attr->NonResidentFlag = 1;
    attr->nonres.DataRunsOffset = sizeof(MFT_ATTR_HEADER);
    attr->nonres.StartVCN = 0;
    attr->nonres.CompressionUnitSize = 0;
    attr->nonres.StreamSize = 1024 * 1024;
    attr->nonres.RealSize = 1024 * 1024;
    attr->nonres.AllocatedSize = 1024 * 1024;
 
    uint8_t* runsBuf = buf + sizeof(MFT_ATTR_HEADER);
    
    if(!file.read((char*)runsBuf, sz)) FAIL() << "Cannot read " << sz << " bytes from '" << fileName << "' file";

    TDataRuns runs;
    bool res = parser.DecodeDataRuns(attr, runs);
    ASSERT_TRUE(res);

}

TEST_F(MFTParserBaseTests, DecodeDataRuns_2)
{
    TMFTRecordLoader ldr;
    TMFTParserBase parser(ldr);

    std::string fileName = TEST_DATA_DIR "DataRuns1.txt";
    std::ifstream file(fileName);
    if (!file) FAIL() << "Error opening file '" << fileName << "'";
 
    std::vector<uint8_t> numbers;
    std::string line;

    while (std::getline(file, line)) 
    {
        std::istringstream iss(line);
        uint32_t num;

        numbers.clear();

        while (iss >> std::hex >> num) {
            ASSERT_TRUE(num < 256);
            numbers.push_back((uint8_t)num);
        }

        uint32_t sz = numbers.size() * sizeof(numbers[0]);
        uint8_t* buf = (uint8_t*)calloc(sizeof(MFT_ATTR_HEADER) + sz, 1);
        ASSERT_TRUE(buf != nullptr);
        uint8_t* runsBuf = buf + sizeof(MFT_ATTR_HEADER);
        memcpy_s(runsBuf, sz, numbers.data(), sz);

        MFT_ATTR_HEADER* attr = (MFT_ATTR_HEADER*)buf;
        attr->AttrType = ATTR_DATA;
        attr->AttrSize = sz + sizeof(MFT_ATTR_HEADER);
        attr->NonResidentFlag = 1;
        attr->nonres.DataRunsOffset = sizeof(MFT_ATTR_HEADER);
        attr->nonres.StartVCN = 0;
        attr->nonres.CompressionUnitSize = 0;
        attr->nonres.StreamSize = 1024 * 1024;
        attr->nonres.RealSize = 1024 * 1024;
        attr->nonres.AllocatedSize = 1024 * 1024;

        TDataRuns runs;
        bool res = parser.DecodeDataRuns(attr, runs);
        ASSERT_TRUE(res);

        if (!std::getline(file, line)) FAIL() << "Error: Incorrect file";

        // check decoded data runs with etalon data
        
        std::istringstream iss2(line);
        iss2 >> std::hex >> num; // number of data runs
        ASSERT_EQ(num, runs.Count());
        
        DATA_RUN_ITEM rli;
        for (size_t i = 0; i < num; i++)
        {
            uint32_t lcn, len;
            rli = runs[i];
            iss2 >> lcn;
            iss2 >> len;
            ASSERT_EQ(lcn, rli.lcn);
            ASSERT_EQ(len, rli.len);
        }

        ASSERT_FALSE(iss2 >> num); // check that we've read entire line

    }

    file.close();
}

