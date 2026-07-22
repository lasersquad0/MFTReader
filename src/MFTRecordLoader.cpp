
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
    }

    DWORD bytesReturned;
    // this is correct that we pass sizeof(NTFS_VOLUME_DATA_BUFFER) because DeviceIoControl expects NTFS_VOLUME_DATA_BUFFER
    if (!DeviceIoControl(hVolume, FSCTL_GET_NTFS_VOLUME_DATA, 0, 0, &FVolumeData, sizeof(NTFS_VOLUME_DATA_BUFFER), &bytesReturned, nullptr))
    {
        DWORD err = GetLastError();

        std::string errMsg = GetErrorMessageTextA(err, "DeviceIoControl"); 
        logger.Error(errMsg);
        throw std::system_error(std::error_code(err, std::system_category()), errMsg);
    }

    assert(FVolumeData.BytesPerMFTRec == DEFAULT_BYTES_PER_MFT_REC); // always 1024 ??

    FVolumeData.hVolume = hVolume;
    FVolumeData.Name = convert_string<wchar_t>(vol2.substr(4)); // remove \\.\ from \\.\C:
}


// mftRec should be a buffer with volData.BytesPerMFTRec size
bool TMFTRecordLoader::LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData)
{
    assert(GetVolumeData().hVolume != INVALID_HANDLE_VALUE);

    NTFS_FILE_RECORD_INPUT_BUFFER nfrib{ 0 };
    nfrib.FileReferenceNumber.LowPart = mftRecRef.sId.low;

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
            logger.WarnFmt("MFT record SEQ numbers differs from each other. Looks like MFT record is overwritten. From Dir: {}, From MFT Rec ID Seq: {:#x}",
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

/**
* @brief Reads series of sequential clusters starting from cluster with number lcnStart
* @details DataBuf should be large enough to fit lcnCnt clusters of data
* @param lcnStart number (id) of first cluster to be read
* @param lcnCnt Count of sequential clusters to be read
* @param dataBuf Buffer where all clusters will be read. Should be at least size lcnCnt*VolumeClusterSize
**/
bool TMFTRecordLoader::ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf)
{
    LARGE_INTEGER offset{ 0 };
    DWORD bytesToRead, bytesRead;

    offset.QuadPart = lcnStart * FVolumeData.BytesPerCluster;

    BOOL res = SetFilePointerEx(FVolumeData.hVolume, offset, nullptr, FILE_BEGIN);
    if (!res)
    {
        GET_LOGGER;
        logger.ErrorFmt("ReadCluster.SetFilePointerEx has failed with error: {}", GetLastError());
        return false;
    }

    // read lcnCnt clusters
    bytesToRead = (DWORD)(lcnCnt * FVolumeData.BytesPerCluster);
    res = ReadFile(FVolumeData.hVolume, dataBuf, bytesToRead, &bytesRead, nullptr);
    if (res)
    {
        assert(bytesToRead == bytesRead);
        return true;
    }
    else
    {
        GET_LOGGER;
        logger.ErrorFmt("ReadCluster.ReadFile has failed with error: {}", GetLastError());
        return false;
    }
}

