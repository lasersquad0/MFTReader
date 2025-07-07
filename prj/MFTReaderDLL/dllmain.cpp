// dllmain.cpp : Defines the entry point for the DLL application.
#include "framework.h"

static void InitLogger()
{
    try
    {
        // if MFTReaderDLL.lfg exists load loggers from that file
        /*if (std::filesystem::exists("MFTReaderDLL.lfg"))
        {
            LogEngine::InitFromFile("MFTReaderDLL.lfg");
        }
        else*/ // otherwise configure loggers in code 
        {
            using namespace LogEngine;
            auto& logger = GetLogger(MFT_LOGGER_NAME);
            std::shared_ptr<FileLockSinkST> sink(new FileLockSinkST("filelocksink", "LogMFTReaderDLL.log"));
            logger.AddSink(sink);
            //logger.SetAsyncMode(true);
            logger.SetLogLevel(LogEngine::Levels::llInfo);
        }
    }
    // catching various LogEngine exceptions
    catch (std::runtime_error& ex)
    {
        OutputDebugStringA((std::string("MFTReaderDLL runtime_error. ") + ex.what()).c_str());
        throw ex;
    }
    catch (std::exception& ex)
    {
        OutputDebugStringA((std::string("MFTReaderDLL std::exception. ") + ex.what()).c_str());
        throw ex;
    }
    catch (...)
    {
        OutputDebugStringA("MFTReaderDLL error: UNKNOWN.");
        throw;
    }
}

static void ShutDownLogger()
{
    auto& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);
    logger.Info("LogEngine STOPPED");
    LogEngine::ShutdownLoggers();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    try
    {
        switch (ul_reason_for_call)
        {
        case DLL_PROCESS_ATTACH:
        {
            std::string s = "DLL_PROCESS_ATTACH. hModule: " + std::to_string((uint64_t)hModule);
            OutputDebugStringA(s.c_str());
    
            InitLogger();
            auto& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);

            OutputDebugStringA("After InitLogger.");

            logger.InfoFmt("DLL_PROCESS_ATTACH hModule: {}", (uint64_t)hModule);

            break;
        }
        case DLL_THREAD_ATTACH:
            break;
        case DLL_THREAD_DETACH:
            break;
        case DLL_PROCESS_DETACH:
        {
            auto& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);
            std::string s = "DLL_PROCESS_DETACH. hModule: " + std::to_string((uint64_t)hModule);
            OutputDebugStringA(s.c_str());

            logger.InfoFmt("DLL_PROCESS_DETACH hModule: {}", (uint64_t)hModule);
            //ShutDownLogger();
            break;
        }
        }
    }
    // catching various exceptions
    catch (std::runtime_error& ex)
    {
        auto& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);
        logger.ErrorFmt("MFTReaderDLL runtime_error. {}", ex.what());
        return 1;
    }
    catch (std::exception& ex)
    {
        auto& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);
        logger.ErrorFmt("MFTReaderDLL runtime_error. {}", ex.what());
        return 1;
    }
    catch (...)
    {
        auto& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);
        logger.Error("MFTReaderDLL runtime_error: UNKNOWN.");

        return 1;
    }

    return TRUE;
}

