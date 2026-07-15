#pragma once

#include "gtest/gtest.h"
#include "strutils/include/ci_string.h"
#include "logengine2/LogEngine.h"

class MFTStringParamTest : public testing::TestWithParam<string_t>
{
protected:
    static inline std::string FName = "***INCORRECT NAME***";
    THArray<string_t> FCache;
public:
    static void SetUpTestCase()
    {
        LogEngine::Logger& logger = LogEngine::GetFileLoggerST(MFT_LOGGER_NAME, MFT_TESTS_LOG_FILE);
        logger.SetLogLevel(LogEngine::Levels::llDebug);
        //logger.Debug("MFTImgFileParserParamTest START");
    }
    static void TearDownTestCase()
    {
        LogEngine::Logger& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);
        logger.Debug(FName + " FINISH");
        LogEngine::ShutdownLoggers();
    }

    void SetUp() override
    {
        auto& param = GetParam();
        if (FCache.IndexOf(param) == -1)
        {
            auto& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);
            std::string str = convert_string<std::string::value_type>(param);
            logger.DebugFmt("{} START ('{}')", FName, str);
            FCache.AddValue(param);
        }
    }

    // You can implement all the usual fixture class members here.
    // To access the test parameter, call GetParam() from class TestWithParam<T>.
};