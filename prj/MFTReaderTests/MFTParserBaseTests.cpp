
#include "gtest/gtest.h"
#include "Readers.h"
#include "TestUtils.h"

class MFTParserBaseTests : public ::testing::Test
{
protected:
    static void SetUpTestCase()
    {
        LogEngine::Logger& logger = LogEngine::GetFileLoggerST(MFT_LOGGER_NAME, MFT_TESTS_LOG_FILE);
        logger.SetLogLevel(LogEngine::Levels::llInfo);
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

TEST_F(MFTParserBaseTests, NormalizeVolume_1)
{
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T(""))); // no default value here, empty string indicates an error.
    EXPECT_EQ(_T("\\\\.\\C:"), IRecordsLoader::NormalizeVolume(_T("C")));
    EXPECT_EQ(_T("\\\\.\\c:"), IRecordsLoader::NormalizeVolume(_T("c")));
    EXPECT_EQ(_T("\\\\.\\C:"), IRecordsLoader::NormalizeVolume(_T("C:")));
    EXPECT_EQ(_T("\\\\.\\c:"), IRecordsLoader::NormalizeVolume(_T("c:")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("cc:")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("1")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("5:")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("$")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("-")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T(":")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("*")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T(" ")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T(".")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("..")));

    EXPECT_EQ(_T("\\\\.\\D:"), IRecordsLoader::NormalizeVolume(_T("D")));
    EXPECT_EQ(_T("\\\\.\\d:"), IRecordsLoader::NormalizeVolume(_T("d")));
    EXPECT_EQ(_T("\\\\.\\D:"), IRecordsLoader::NormalizeVolume(_T("D:")));
    EXPECT_EQ(_T("\\\\.\\d:"), IRecordsLoader::NormalizeVolume(_T("d:")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("dd")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("dd:")));
    
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("\\\\.\\")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("\\\\.\\cc")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("\\\\.\\CC:")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("\\\\.\\1")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("\\\\.\\*")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("\\\\.\\ ")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("\\\\.\\ :")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("\\\\.\\-")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("\\\\.\\$:")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("\\\\.\\:")));

    EXPECT_EQ(_T("\\\\.\\e:"), IRecordsLoader::NormalizeVolume(_T("\\\\.\\e")));
    EXPECT_EQ(_T("\\\\.\\e:"), IRecordsLoader::NormalizeVolume(_T("\\\\.\\e:")));
    EXPECT_EQ(_T("\\\\.\\e:"), IRecordsLoader::NormalizeVolume(_T("\\\\.\\e:\\")));
    EXPECT_EQ(_T("\\\\.\\e:"), IRecordsLoader::NormalizeVolume(_T("\\\\.\\e:filename1")));
    EXPECT_EQ(_T("\\\\.\\x:"), IRecordsLoader::NormalizeVolume(_T("\\\\.\\x:folder\\filename1")));
    EXPECT_EQ(_T("\\\\.\\X:"), IRecordsLoader::NormalizeVolume(_T("\\\\.\\X:filename1")));
    EXPECT_EQ(_T("\\\\.\\e:"), IRecordsLoader::NormalizeVolume(_T("\\\\.\\e:\\folder\\filename1")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("\\\\.\\filename1")));
    EXPECT_EQ(_T(""), IRecordsLoader::NormalizeVolume(_T("\\\\.\\cc")));

    EXPECT_TRUE(true);
}

TEST_F(MFTParserBaseTests, AbsPath_1)
{
    string_t curr = std::filesystem::current_path();

    EXPECT_EQ(_T(""), IRecordsLoader::AbsPath(_T(""))); 
    EXPECT_EQ(_T(" "), IRecordsLoader::AbsPath(_T(" ")));
    EXPECT_EQ(_T("  "), IRecordsLoader::AbsPath(_T("  ")));
    EXPECT_EQ(_T("C:\\"), IRecordsLoader::AbsPath(_T("C")));
    EXPECT_EQ(_T("c:\\"), IRecordsLoader::AbsPath(_T("c")));
    EXPECT_EQ(_T("f:\\"), IRecordsLoader::AbsPath(_T("f")));
    EXPECT_EQ(_T("c:\\"), IRecordsLoader::AbsPath(_T("c:")));
    EXPECT_EQ(_T("x:\\"), IRecordsLoader::AbsPath(_T("x:")));
    EXPECT_EQ(_T("c:\\"), IRecordsLoader::AbsPath(_T("c:\\")));
    EXPECT_EQ(_T("f:\\"), IRecordsLoader::AbsPath(_T("f:\\")));
    EXPECT_EQ(_T("c:\\"), IRecordsLoader::AbsPath(_T("c:\\\\")));
    EXPECT_EQ(_T("c:\\"), IRecordsLoader::AbsPath(_T("c:\\\\\\")));
    EXPECT_EQ(_T("f:\\folder"), IRecordsLoader::AbsPath(_T("f:\\folder")));
    EXPECT_EQ(_T("c:\\folder\\file"), IRecordsLoader::AbsPath(_T("c:\\folder\\\\file")));

    EXPECT_EQ(_T("c:\\"), IRecordsLoader::AbsPath(_T("c:/")));
    EXPECT_EQ(_T("f:\\"), IRecordsLoader::AbsPath(_T("f:/")));
    EXPECT_EQ(_T("c:\\"), IRecordsLoader::AbsPath(_T("c://")));
    EXPECT_EQ(_T("c:\\"), IRecordsLoader::AbsPath(_T("c:///")));
    EXPECT_EQ(_T("f:\\folder"), IRecordsLoader::AbsPath(_T("f://folder")));
    EXPECT_EQ(_T("c:\\folder\\file"), IRecordsLoader::AbsPath(_T("c:/folder/file")));

    string_t val = curr + _T("\\1");
    EXPECT_EQ(val, IRecordsLoader::AbsPath(_T("1")));
    val = curr + _T("\\:");
    EXPECT_EQ(val, IRecordsLoader::AbsPath(_T(":")));
    val = curr + _T("\\$");
    EXPECT_EQ(val, IRecordsLoader::AbsPath(_T("$")));
    val = curr + _T("\\^");
    EXPECT_EQ(val, IRecordsLoader::AbsPath(_T("^")));
    val = curr;
    EXPECT_EQ(curr, IRecordsLoader::AbsPath(_T(".")));
    val = curr;
    val = val.substr(0, val.find_last_of('\\')); // need to remove last dir from path
    EXPECT_EQ(val, IRecordsLoader::AbsPath(_T("..")));
}

TEST_F(MFTParserBaseTests, Open_1)
{
    TWinAPIRecordsLoader ldr;
    ldr.Open(_T("c:")); // generates an exception if not run as Admin
    ldr.Open(_T("c"));
    ldr.Open(_T("c:\\testfile"));
}

TEST_F(MFTParserBaseTests, Open_2)
{
    TWinAPIRecordsLoader ldr;

    EXPECT_THROW(ldr.Open(_T("d:")), std::system_error); 
    EXPECT_THROW(ldr.Open(_T("f")), std::system_error);

    EXPECT_THROW(ldr.Open(_T(":")), std::runtime_error);
    EXPECT_THROW(ldr.Open(_T("\\")), std::runtime_error); // first symbol should be alfa sym
    EXPECT_THROW(ldr.Open(_T("/")), std::runtime_error);

    EXPECT_THROW(ldr.Open(_T("::")), std::runtime_error);
    EXPECT_THROW(ldr.Open(_T("\\\\")), std::runtime_error);
    EXPECT_THROW(ldr.Open(_T("//")), std::runtime_error);

    EXPECT_THROW(ldr.Open(_T("\\.\\")), std::runtime_error);
    EXPECT_THROW(ldr.Open(_T("\\\\.\\")), std::runtime_error);
    EXPECT_THROW(ldr.Open(_T("\\\\.\\\\")), std::runtime_error);

}

TEST_F(MFTParserBaseTests, OpenClose_1)
{
    TWinAPIRecordsLoader ldr;

    ldr.Close();

    ldr.Open(_T("c:")); // generates an exception if not run as Admin
    ldr.Close();
    ldr.Close();

    ldr.Open(_T("c"));
    ldr.Open(_T("c:\testfile"));
    ldr.Close();

    ldr.Open(_T("c"));
    EXPECT_THROW(ldr.Open(_T("//")), std::runtime_error);
    ldr.Close();
}

// MFT IDs will be different for different disks, this test linked to one disk only
TEST_F(MFTParserBaseTests, MFTRecIdByPath_1)
{
    TWinAPIRecordsLoader ldr(_T("c:")); // assume that all path are on C:
    TMFTBaseReader ps(ldr);

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
        auto expctValue = ps.MFTRecIdByPath(p.second);
        if (expctValue)
            EXPECT_EQ(p.first, expctValue.value());
        else
            EXPECT_EQ(p.first, 0ul);
            //EXPECT_TRUE(false) << "GetMFTRecIdByPath failed with error: " << ErrorCodeNames[(uint8_t)expctValue.error()];
    }
}


// MFT IDs will be different for different disks, this test linked to disk C: only
TEST_F(MFTParserBaseTests, PathByMFTRecId_1)
{
    TWinAPIRecordsLoader ldr(_T("c:")); // assume that all path are on C:
    TMFTBaseReader ps(ldr);

    THArray<std::pair<uint32_t, ci_string>> testData
    {
        {MFT_ROOT_REC_ID, _T("C:")},
        {0, _T("c:\\$MFT")}, 
        {1, _T("C:\\$MFTMirr")}, {2, _T("C:\\$LogFile")},
        {58663, _T("C:\\windows")},
        {104227, _T("c:\\hiberfil.sys")}, {64, _T("c:\\pagefile.sys")},
        {57651, _T("c:\\program files")}, {57651, _T("c:\\Program Files")},
        {406846, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\bin\\git.exe")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\bin\\git-receive-pack.exe")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-bisect--helper.exe")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-branch.exe")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-column.exe")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-am.exe")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-add.exe")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-log.exe")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-annotate.exe")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-apply.exe")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-cherry.exe")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-clean.exe")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-clone.exe")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-diff.exe")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-help.exe")},
        {405697, _T("c:\\Program Files\\Git\\mingw64\\libexec\\git-core\\git-version.exe")},
        {793004, _T("c:\\Windows\\WinSxS\\amd64_amdgpio2.inf.resources_31bf3856ad364e35_10.0.26100.1_ru-ru_973ad9b5977fab1e")},
        {98425, _T("c:\\Program Files\\Google\\Chrome\\Application")},
        {98426, _T("c:\\Program Files\\Google\\Chrome\\Application\\SetupMetrics")},
        
    };

    for (auto p : testData)
    {
        THArray<std::wstring> paths;
        MFT_REF id{ p.first };
        paths.Clear();
        EXPECT_EQ(TErrorCode::Success, ps.PathByMFTRecID(id, paths));
        EXPECT_TRUE(EXPECT_ONE_OF(p.second, paths));
    }
}

static bool is_in(uint val, uint* arr, uint cnt)
{
    for (uint i = 0; i < cnt; i++)
        if (val == arr[i]) return true;

    return false;
}

TEST_F(MFTParserBaseTests, BitField_1)
{
    const uint32_t DWORDS = 10;
    const uint64_t BITS = DWORDS*64;

    TBitField bmp;
    bmp.SetData(DWORDS, false); // 10 dwords it is 640 bits in total

    uint arr[] = {4,6,100,55,639,199,9, 0,1,2,3, 512, 127,128,129, 600,599,601,602,603};
    
    for(uint val: arr)
        bmp.SetTrue(val);
    
    for (uint val : arr)
        ASSERT_EQ(true, bmp.Test(val));

    for (uint i = 0; i < BITS; i++)
        if (is_in(i, arr, sizeof(arr)/sizeof(arr[0])))
            ASSERT_EQ(true, bmp.Test(i));
        else
            ASSERT_EQ(false, bmp.Test(i));
}
