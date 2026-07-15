
#include "gtest/gtest.h"
#include "Readers.h"
#include "TestUtils.h"

class MFTParserBaseTests : public ::testing::Test
{
protected:
    static void SetUpTestCase()
    {
        LogEngine::Logger& logger = LogEngine::GetFileLoggerST(MFT_LOGGER_NAME, MFT_TESTS_LOG_FILE);
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

