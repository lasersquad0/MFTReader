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

TFileCache gFileCache;

// This is an example of an exported function.
MFTREADERDLL_API TError ReadVolume(wchar_t* volume, uint64_t* volSize, uint32_t* count, uint32_t** data)
{
    GET_LOGGER;

    try
    {
        logger.InfoFmt("Reading volume data for: {}", wtos(volume));

        std::wstring vol = ParseVolume(volume);

        VOLUME_DATA volData{0};
        ReadVolumeData(vol, volData); //throws exptions in case of errors

        auto start1 = std::chrono::high_resolution_clock::now();
       
        //MFT_REF startId{ 0 };
        //startId.Id = MFT_ROOT_REC_ID;

        logger.InfoFmt("Reading file system: {}", wtos(vol));

        gFileCache.Clear();

        uint64_t rootDirSize{0};
        if (!ReadDirectoryV1(volData, 0, nullptr, rootDirSize, gFileCache))
            throw std::runtime_error("ReadDirectoryV1 finished with error.");

        // pcache - array of pointers to levels
        // pcache[i] is a pointer to list of CACHE_ITEMs in memory for i'th level
        uint32_t** pcache = (uint32_t**)gFileCache.GetFirstLevelPointer(); //(uint32_t**)malloc(fileCache.LevelsCount() * sizeof(uint8_t*));
        
        *data = (uint32_t*)pcache;
        *count = gFileCache.LevelsCount();
        *volSize = rootDirSize;

        logger.InfoFmt("data: {:#X}", (uint64_t)(*data));

        for (uint32_t i = 0; i < gFileCache.LevelsCount(); i++)
        {
            auto lev = (TFileCache::TLevel*)pcache[i];//fileCache.GetLevel(i);
           // pcache[i] = (uint32_t*)lev;
            logger.InfoFmt("data[{}] = {:#X}", i, (uint64_t)(pcache[i]));
            logger.InfoFmt("data[{}].FCount = {}", i, (uint64_t)(lev->FCount));
            logger.InfoFmt("data[{}].FStart = {:#X}", i, (uint64_t)(lev->FStart));
        }

        //fileCache.SaveTo("MFTReader_items.txt");

        /*
        logger.Info("Processing...");
        THArray<std::wstring> arr;
        fileCache.ToArray(arr);

        //auto dirCount = std::count_if(dirList.begin(), dirList.end(), [](FILE_NAME& a) { return IsDir(a.Attr); });

        logger.Info("Sorting...");
        std::sort(arr.begin(), arr.end());

        std::string filename = "ListMFTFile_sortedV1.log";
        LogEngine::TFileStream ff(filename);

        ff << toStringSepW(arr.Count()) + L" - total";
        //ff << toStringSepW(arr.Count() - dirCount) + L" - files"; // only files
        //ff << toStringSepW(dirCount) + L" - dirs"; // only dirs

        logger.InfoFmt("Saving list of files to {}", filename);

        std::wstring endl;
        BUILD_ENDL(endl);
        for (auto& item : arr)
        {
            ff << item << endl;
        }
        */
        auto stop = std::chrono::high_resolution_clock::now();
        logger.InfoFmt("Volume reading time : {}", MillisecToStr<std::string>(std::chrono::duration_cast<std::chrono::milliseconds>(stop - start1).count()));

        //Singleton<TMFTRecCache>::Release();
    }
    catch (std::runtime_error& ex)
    {
        logger.ErrorFmt("MFTReaderDLL runtime_error. {}", ex.what());
        TError err{0};
        err.ErrCode = 1;
        wcscpy_s(err.ErrText, sizeof(err.ErrText)/sizeof(err.ErrText[0]), stow(ex.what()).c_str());
        return err;
    }
    catch (std::exception& ex)
    {
        logger.ErrorFmt("MFTReaderDLL std::exception. {}", ex.what());
        TError err{0};
        err.ErrCode = 1;
        wcscpy_s(err.ErrText, sizeof(err.ErrText) / sizeof(err.ErrText[0]), stow(ex.what()).c_str());
        return err;
    }
    catch (...)
    {
        logger.Error("MFTReaderDLL error: UNKNOWN.");
        TError err{ 0 };
        err.ErrCode = 1;
        return err;
    }

    TError err{0};
    return err;
}


