
#include "Readers.h"

// make volume look like \\.\\C: 
string_t IRecordLoader::NormalizeVolume(const string_t& vol)
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

// FixupUSA1 makes USA fixes in buffer refered by record param.
// This function is going to be used for processing ALLOC attr Index Blocks or MFT records (only when they read directly from disk, not via WINAPI). 
// When MFT records loadded via WINAPI call they already have USA fixed up.
// BytesPerBlock value is usually defined in ATTR_ROOT attr (field IndexBlockSize) and it may differ from filesystem's ClusterSize.
// For MFT records BytesPerBlock is standard MFT record size (BytesPerMFTRec)
// record buffer should be at least BytesPerBlock size
TErrorCode  IRecordLoader::FixupUSA1(NTFS_RECORD_HEADER* record, uint32_t BytesPerBlock, uint32_t BytesPerSector)
{
    UNREFERENCED_PARAMETER(BytesPerBlock);

    uint32_t wordsPerSector = BytesPerSector >> 1;

    uint16_t sectorsCnt = record->FixupCnt - 1;
    assert(sectorsCnt == BytesPerBlock / BytesPerSector);

    uint16_t* fixupArr = (uint16_t*)(Add2Ptr(record, record->FixupOffset));
    uint16_t checkValue = *fixupArr;
    fixupArr++; // now it refers to first array item

    uint16_t* sectorEnd = (uint16_t*)(record)+wordsPerSector - 1;

    uint32_t s = 0;
    while (s < sectorsCnt)
    {
        assert(checkValue == *sectorEnd);
        if (checkValue != *sectorEnd)
        {
            GET_LOGGER;
            logger.Error("[FixupUSA1] Error: looks like data is corrupted in the sector");
            return TErrorCode::CorruptedData; // looks like data is corrupted in this sector
        }

        *sectorEnd = fixupArr[s]; // restore data

        sectorEnd += wordsPerSector;
        s++;
    }

    return TErrorCode::Success;
}

std::expected<uint32_t, TErrorCode> IRecordLoader::ReadMetaFilesCount(TMFTParserBase& parser)
{
    if (!IsOpened()) return std::unexpected(TErrorCode::IOError);
    assert(FRecordsCount > 0);

    uint8_t* mftRecBuf = (uint8_t*)alloca(DEFAULT_BYTES_PER_MFT_REC);
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;
    MFT_REF mftRef{ 0 };
    TErrorCode res;

    while (mftRef.sId.low < FRecordsCount)
    {
        res = LoadMFTRecord(mftRef, mftRecBuf); // function checks signature (should be 'FILE'), returns NotInUse error when signature <>'FILE'
        if ((res == TErrorCode::MFTRecordNotInUse) || (mftRec->Flags & MFT_FLAG_IN_USE) == 0)
        {
            // MFT record does not contain 'FILE' signature (consider it as NotInUse)
            // OR
            // MFT record contains 'FILE' signature, but field Flags tell us that this record is NOT In Use.

            // nothing to do
        }
        else if (res != TErrorCode::Success)
        {
            return std::unexpected(res);
        }
        else
        {
            TAttrCollection coll;
            res = parser.FillAttrCollection(mftRec, MakeAttrBitmask(ATTR_FILENAME), coll); // MFTRecordNotInUse is valid return value
            assert(res == TErrorCode::Success);

            auto& afn = coll.Get(ATTR_FILENAME);

            ATTR_FILE_NAME* fn{ nullptr };
            // sometimes there are 'system' MFT records without ATTR_FILENAME attribute (ids #12-#15)
            if (afn.Count() > 0)
            {
                // some disk images contain system files like $TxfLogContainer0000000000000000001 which have two names (DOS and WIN), both names start from '$'
                //ASSERT_EQ(1ul, afn.Count());
                auto attr = afn[0];
                assert(nullptr != attr);
                fn = (ATTR_FILE_NAME*)Add2Ptr(attr, attr->res.DataOffset);
            }

            if ((fn != nullptr) && (fn->FileNameLen > 0) && (GetFName(fn)[0] != L'$'))
                if ((fn->FileNameLen > 1) || GetFName(fn)[0] != L'.')
                    break;
        }

        mftRef.sId.low++;
    }

    return mftRef.sId.low;
}

void TMFTRecordLoader::Open(const string_t& vol)
{
    if (IsOpened()) Close();

    string_t vol2 = NormalizeVolume(vol);

    GET_LOGGER;

    // closing previously opened volume
    if (IsOpened()) Close();
    assert(FVolumeData.hVolume == INVALID_HANDLE_VALUE);

    logger.DebugFmt("Opening volume: {}", wtos(vol2));

    HANDLE hVolume = CreateFile(vol2.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);

    if (hVolume == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        auto errMsg = GetErrorMessageTextA(err, "CreateFile");
        //logger.Error(errMsg);
        throw std::system_error(std::error_code(err, std::system_category()), errMsg);
    }

    DWORD bytesReturned;
    // this is correct that we pass sizeof(NTFS_VOLUME_DATA_BUFFER) because DeviceIoControl expects NTFS_VOLUME_DATA_BUFFER
    if (!DeviceIoControl(hVolume, FSCTL_GET_NTFS_VOLUME_DATA, 0, 0, &FVolumeData, sizeof(NTFS_VOLUME_DATA_BUFFER), &bytesReturned, nullptr))
    {
        DWORD err = GetLastError();

        std::string errMsg = GetErrorMessageTextA(err, "DeviceIoControl"); 
        //logger.Error(errMsg);
        throw std::system_error(std::error_code(err, std::system_category()), errMsg);
    }

    assert(FVolumeData.BytesPerMFTRec == DEFAULT_BYTES_PER_MFT_REC); // always 1024 ??

    FVolumeData.hVolume = hVolume;
    FVolumeData.Name = convert_string<wchar_t>(vol2.substr(4)); // remove \\.\ from \\.\C:

    FRecordsCount = FVolumeData.MftValidDataLength.QuadPart / FVolumeData.BytesPerMFTRec;

    SetOpened(true); // must be before ReadMetaFilesCount() call

    TMFTParserBase parser(*this);
    auto expct =  ReadMetaFilesCount(parser);
    assert(expct);
    FMetaFilesCount = expct.value();

}


// mftRec should be a buffer with volData.BytesPerMFTRec size
TErrorCode TMFTRecordLoader::LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData)
{
    assert(IsOpened());
    assert(FVolumeData.hVolume != INVALID_HANDLE_VALUE);

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
        return TErrorCode::IOError;
    }

    // DeviceIoControl may return other MFT record than we requested.
    // This may happen when requested MFT record has been deleted while we were parsing MFT structures and navigating, that happens not so rarely
    // just exit from LoadMFTRecord in that case.
    if (nfrib.FileReferenceNumber.LowPart != pnfrob->FileReferenceNumber.LowPart) // we compare LowPart here to avoid diff by Seq Nums which is checked below
    {
        GET_LOGGER;
        logger.WarnFmt("Requested MFT Rec ID differs from returned. Looks like requested MFT record is deleted. Requested: {}, returned: {}",
                nfrib.FileReferenceNumber.LowPart, pnfrob->FileReferenceNumber.LowPart);
        return TErrorCode::MFTRecordNotInUse;
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

    return TErrorCode::Success;
}

/**
* @brief Reads series of sequential clusters starting from cluster with number lcnStart
* @details DataBuf should be large enough to fit lcnCnt clusters of data
* @param lcnStart number (id) of first cluster to be read
* @param lcnCnt Count of sequential clusters to be read
* @param dataBuf Buffer where all clusters will be read. Should be at least size lcnCnt*VolumeClusterSize
**/
TErrorCode TMFTRecordLoader::ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf)
{
    assert(IsOpened());

    LARGE_INTEGER offset{ 0 };
    DWORD bytesToRead, bytesRead;

    offset.QuadPart = lcnStart * FVolumeData.BytesPerCluster;

    BOOL res = SetFilePointerEx(FVolumeData.hVolume, offset, nullptr, FILE_BEGIN);
    if (!res)
    {
        GET_LOGGER;
        logger.ErrorFmt("ReadCluster.SetFilePointerEx has failed with error: {}", GetLastError());
        return TErrorCode::IOError;
    }

    // read lcnCnt clusters
    bytesToRead = (DWORD)(lcnCnt * FVolumeData.BytesPerCluster);
    res = ReadFile(FVolumeData.hVolume, dataBuf, bytesToRead, &bytesRead, nullptr);
    if (res)
    {
        assert(bytesToRead == bytesRead);
        return TErrorCode::Success;
    }
    else
    {
        GET_LOGGER;
        logger.ErrorFmt("ReadCluster.ReadFile has failed with error: {}", GetLastError());
        return TErrorCode::IOError;
    }
}
