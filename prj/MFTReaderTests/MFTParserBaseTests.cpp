
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

// MFT IDs will be different for different disks, this test linked to one disk only
TEST_F(MFTParserBaseTests, GetMFTRecIdByPath_1)
{
    TMFTRecordLoader ldr(_T("c:")); // assume that all path are on C:
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

template<typename STRING, typename ARRAY>
bool EXPECT_ONE_OF(STRING expected, ARRAY arr)
{
    // make sure that STRING is one of instantiations of strings
    static_assert(std::is_base_of<std::basic_string<typename STRING::value_type, typename STRING::traits_type>, STRING>::value);
    // make sure that wstr is either std::string (no conversion required) or std::wstring
    static_assert(std::is_same_v<typename STRING::value_type, wchar_t> || std::is_same_v<typename STRING::value_type, char>);
    static_assert(std::is_same_v<typename STRING::value_type, typename ARRAY::item_type::value_type>);
    //static_assert(std::is_convertible_v<typename ARRAY::item_type, STRING>);

    for (auto& item : arr)
        if (expected == STRING(item.c_str())) return true;
    return false;
}

// MFT IDs will be different for different disks, this test linked to disk C: only
TEST_F(MFTParserBaseTests, GetPathByMFTRecId_1)
{
    TMFTRecordLoader ldr(_T("c:")); // assume that all path are on C:
    TMFTParserBase ps(ldr);

    THArray<std::pair<uint32_t, ci_string>> testData{
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
        ps.GetPathByMFTRecID(id, paths);
        EXPECT_TRUE(EXPECT_ONE_OF(p.second, paths));
    }
}