// MFTReaderDLL.cpp : Defines the exported functions for the DLL.
//

#include <iostream>
#include <string>
#include <cstdint>
#include <algorithm>
#include <stdexcept>

#include "framework.h"


// This is an example of an exported variable
//MFTREADERDLL_API int nMFTReaderDLL=0;

// this cache variable needs to be global because cache needs to exist after ReadVolume call
// because cache is returned to outer function. 
TFileCache gFileCache;

// volume parameter can be any of these: C, C:, c:\, c:\folder
// only first two symbols from volume will be used as volume name. if volume contains single symbol ("C") one symbol will be used.
MFTREADERDLL_API TError ReadVolume(wchar_t* volume, wchar_t* exclFolders, uint32_t* count, uint32_t** data, ProgressCallbackPtr callback)
{
    GET_LOGGER;

    try
    {
        gFileCache.Clear(); //clear previous data if any
        
        logger.InfoFmt("Reading volume data for: {}", wtos(volume));

        std::wstring vol = ParseVolume(volume); // gets first two symbols of volume (e.g. C:) and adds prefix "\\.\" in front of it 

        VOLUME_DATA volData;
        ReadVolumeData(vol, volData); // throws exceptions in case of errors

        THArray<MFTRecIndex> exclIDs;
        wchar_t* currFolder = exclFolders;
        while (true)
        {
            if (*currFolder == '\0') break;
            MFTRecIndex mftId = GetMFTRecIdByPath(volData, currFolder);
            if(mftId != 0) exclIDs.AddValue(mftId); //0 means "path not found", ignore it
            currFolder += wcslen(currFolder) + 1;
        }

        //auto start1 = std::chrono::high_resolution_clock::now();
        Ticks::Start(_T("ReadVolumeTime"));

        logger.InfoFmt("Reading file system: {}", wtos(vol));

        uint64_t rootDirSize{0};
        if (!ReadDirectoryV1(volData, 0, nullptr, rootDirSize, gFileCache, callback))
            throw std::runtime_error("ReadDirectoryV1 finished with error.");

        // pcache - array of pointers to levels
        // pcache[i] is a pointer to list of CACHE_ITEMs in memory for i'th level
        uint32_t** pcache = (uint32_t**)gFileCache.GetFirstLevelPointer(); //(uint32_t**)malloc(fileCache.LevelsCount() * sizeof(uint8_t*));
        
        *data = (uint32_t*)pcache;
        *count = gFileCache.LevelsCount();

        logger.DebugFmt("data: {:#X}", (uint64_t)(*data));

        /*for (uint32_t i = 0; i < gFileCache.LevelsCount(); i++)
        {
            auto lev = (TFileCache::TLevel*)pcache[i];//fileCache.GetLevel(i);
           // pcache[i] = (uint32_t*)lev;
            logger.InfoFmt("data[{}] = {:#X}", i, (uint64_t)(pcache[i]));
            logger.InfoFmt("data[{}].FCount = {}", i, (uint64_t)(lev->FCount));
            logger.InfoFmt("data[{}].FStart = {:#X}", i, (uint64_t)(lev->FStart));
        }*/

        //fileCache.SaveTo("MFTReader_items.txt");

        CloseHandle(volData.hVolume);
        //Singleton<TMFTRecCache>::Release();

        auto stop = std::chrono::high_resolution_clock::now();
        Ticks::Finish(_T("ReadVolumeTime"));

        logger.InfoFmt("Volume reading time : {}", MillisecToStr<std::string>(Ticks::GetTick(_T("ReadVolumeTime")))); //std::chrono::duration_cast<std::chrono::milliseconds>(stop - start1).count()));
    }

    catch (std::system_error& ex) 
    {
        logger.ErrorFmt("MFTReaderDLL system_error. {}", ex.what());
        TError err{ 0 };
        err.ErrCode = ex.code().value();
        err.Important = 1; // all errors from DLL are important
        wcscpy_s(err.ErrText, sizeof(err.ErrText) / sizeof(err.ErrText[0]), stow(ex.what()).c_str());
        return err;
    }
    catch (std::runtime_error& ex)
    {
        logger.ErrorFmt("MFTReaderDLL runtime_error. {}", ex.what());
        TError err{ .ErrCode = 1, .Important = 1 }; // all errors from DLL are important
        wcscpy_s(err.ErrText, sizeof(err.ErrText)/sizeof(err.ErrText[0]), stow(ex.what()).c_str());
        return err;
    }
    catch (std::exception& ex)
    {
        logger.ErrorFmt("MFTReaderDLL std::exception. {}", ex.what());
        TError err{.ErrCode = 1, .Important = 1}; // all errors from DLL are important
        wcscpy_s(err.ErrText, sizeof(err.ErrText) / sizeof(err.ErrText[0]), stow(ex.what()).c_str());
        return err;
    }
    catch (...)
    {
        logger.Error("MFTReaderDLL error: UNKNOWN.");
        TError err{ 1, L"MFTReaderDLL error : UNKNOWN.", 1 };
        return err;
    }

    TError err{0};
    return err;
}


