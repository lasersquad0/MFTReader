
#include "Readers.h"



// make volume look like \\.\\C: 
string_t IRecordLoader::ParseVolume(const string_t& vol)
{
    if (vol.starts_with(_T("\\\\.\\")))
    {
        if (vol.size() == 4) return _T(""); // no default value, return "" as an error 
        if (vol.size() == 5) return vol + _T(':');
        return vol.substr(0, 6);
    }
    else
    {
        if (vol.size() == 0) return _T(""); // indicates error, no default value
        if (vol.size() == 1) return string_t{ _T("\\\\.\\") } + vol[0] + _T(':'); // extract C, append ':'
        return string_t{ _T("\\\\.\\") } + vol[0] + vol[1]; // extract 'C:' from vol
    }
}

// closes volume handle opened by OpenVolume
// clears data about volume, clears caches
void TMFTRecordLoader::CloseVolume()
{
    GET_LOGGER;
    logger.DebugFmt("Closing volume: {}", wtos(FVolumeData.Name));

    CloseHandle(FVolumeData.hVolume);
    ZeroMemory(&FVolumeData, sizeof(FVolumeData));
    FMFTRecCache.Clear();
}

void TMFTRecordLoader::OpenVolume(const string_t& vol)
{
    string_t vol2 = ParseVolume(vol);

    GET_LOGGER;

    // closing previously opened volume
    if (FVolumeData.hVolume != INVALID_HANDLE_VALUE) CloseVolume();

    logger.DebugFmt("Opening volume: {}", wtos(vol2));

    HANDLE hVolume = CreateFile(vol2.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);

    if (hVolume == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        auto errMsg = GetErrorMessageTextA(err, "CreateFile");
        logger.Error(errMsg);
        throw std::system_error(std::error_code(err, std::system_category()), errMsg);
        //throw std::runtime_error(errMsg); // "Error opening volume.");
    }

    //NTFS_VOLUME_DATA_BUFFER nvdb{0};
    DWORD bytesReturned;

    if (!DeviceIoControl(hVolume, FSCTL_GET_NTFS_VOLUME_DATA, 0, 0, &FVolumeData /*&nvdb*/, sizeof(NTFS_VOLUME_DATA_BUFFER /*nvdb*/), &bytesReturned, nullptr))
    {
        DWORD err = GetLastError();

        std::string errMsg = GetErrorMessageTextA(err, "DeviceIoControl"); //std::format("[DeviceIoControl] Error reading volume data. Volume: {}, error: {}", wtos(volume), GetLastError());
        logger.Error(errMsg);
        throw std::system_error(std::error_code(err, std::system_category()), errMsg);
        //throw std::runtime_error(errMsg); // "Error reading volume data.");
    }

    FVolumeData.hVolume = hVolume;
    FVolumeData.Name = convert_string<wchar_t>(vol2.substr(4)); // remove \\.\ from \\.\C:
}


// mftRec should be a buffer with volData.BytesPerMFTRec size
bool TMFTRecordLoader::LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData)
{
    assert(GetVolumeData().hVolume != INVALID_HANDLE_VALUE);

    NTFS_FILE_RECORD_INPUT_BUFFER nfrib{ 0 };
    nfrib.FileReferenceNumber.LowPart = mftRecRef.sId.low;
    //nfrib.FileReferenceNumber.QuadPart = nvdb.MftValidDataLength.QuadPart / nvdb.BytesPerFileRecordSegment - 1;

    //ULONG cb = __builtin_offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer[volData.BytesPerMFTRec]);
    ULONG cb = offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer[FVolumeData.BytesPerMFTRec]);

    auto pnfrob = (PNTFS_FILE_RECORD_OUTPUT_BUFFER)alloca(cb);
    DWORD bytesReturned;

    if (!DeviceIoControl(FVolumeData.hVolume, FSCTL_GET_NTFS_FILE_RECORD, &nfrib, sizeof(nfrib), pnfrob, cb, &bytesReturned, nullptr))
    {
        GET_LOGGER;
        logger.ErrorFmt("DeviceIoControl failed with error. Error code: {}", GetLastError());
        return false;
    }

    // DeviceIoControl may return other MFT record than we requested.
    // This may happen when requested MFT record has been deleted while we were parsing MFT structures and navigating, that happens not so rarely
    // just exit from LoadMFTRecord in that case.
    if (nfrib.FileReferenceNumber.LowPart != pnfrob->FileReferenceNumber.LowPart) // we compare LowPart here to avoid diff by Seq Nums which is checked below
    {
        GET_LOGGER;
        logger.WarnFmt("Requested MFT Rec ID differs from returned. Looks like requested MFT record is deleted. Requested: {}, returned: {}",
                nfrib.FileReferenceNumber.LowPart, pnfrob->FileReferenceNumber.LowPart);
        return false;
    }

    MFT_FILE_RECORD* mftRecord = (MFT_FILE_RECORD*)(pnfrob->FileRecordBuffer);

    // Make sure DeviceIoControl returned exactly the MFT record number we requested.
    // DeviceIoControl may return closest existing MFT record when record with requested ID is "free".
    assert(pnfrob->FileReferenceNumber.LowPart == mftRecord->IndexMFTRec);
    assert(mftRecRef.sId.low == mftRecord->IndexMFTRec); // make sure we've got the same record as requested.
    
    // checking that sequence numbers are the same in mftRecRef read from parent directory and MFT record read directly by number
    // if seq number differ it means that MFT record has updated and mftRecRef contains old (and may be incorrect) info 
    if (mftRecRef.sId.low != MFT_ROOT_REC_ID)
    {
        if ((mftRecRef.sId.seq > 0) && (mftRecord->SeqNum != mftRecRef.sId.seq))
        {
            GET_LOGGER;
            logger.WarnFmt("MFT record SEQ numbers differs from each other. Looks like MFT record is deleted or overwritten. From Dir: {}, From MFT Rec ID Seq: {:#x}",
                mftRecRef.toHexString(), mftRecord->SeqNum);
        }
        //diff in Seq is not major problem, allow to continue app work
        //assert((mftRecRef.sId.seq == 0) || (mftRecord->SeqNum == mftRecRef.sId.seq));
    }

    //TODO think how to avoid this memcpy_s
    auto res = memcpy_s(mftRecData, FVolumeData.BytesPerMFTRec, mftRecord, FVolumeData.BytesPerMFTRec);
    UNREFERENCED_PARAMETER(res);
    assert(!res);

    return true;
}

// returns NULL if error occurred during loading MFT record
uint8_t* TMFTRecordLoader::LoadMFTRecordCache(MFT_REF mftRecRef)
{
    uint8_t** result = FMFTRecCache.GetValuePointer(mftRecRef.sId.low);
    if (result == nullptr) // no value in cache, load MFT record from disk
    {
        uint8_t* mftRecBuf = DBG_NEW uint8_t[FVolumeData.BytesPerMFTRec];
        if (!LoadMFTRecord(mftRecRef, mftRecBuf))
            return nullptr; // error loading MFT record, it means that DeviceIoControl failed with error

        //we use mftRecRef.sId.low here because high part of mftRecRef.Id may change when MFT record is modified
        FMFTRecCache.SetValue(mftRecRef.sId.low, mftRecBuf); // update cache

        return mftRecBuf;
    }

    return *result; // return MFT record from cache
}
