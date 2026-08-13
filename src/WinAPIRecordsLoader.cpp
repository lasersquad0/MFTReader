
#include "Readers.h"

// make volume look like \\.\C:
// vol should contain volume letter ('c','d', etc) and symbol ':' after it.
// also vol string can start from \\.\ followed by volume letter and then symbol ':', rest of the string is not checked and cut off
string_t IRecordsLoader::NormalizeVolume(const string_t& vol)
{
    if (vol.starts_with(_T("\\\\.\\")))
    {
        if (vol.size() == 4) return _T(""); // no default value, return "" as an error 
        if (!std::isalpha(vol[4])) return _T(""); // indicates error, no default value 
        if (vol.size() == 5) return vol + _T(':');
        if (vol[5] != ':') return _T(""); // indicates error, no default value
        return vol.substr(0, 6);
    }
    else
    {
        if (vol.size() == 0) return _T(""); // indicates error, no default value
        if (!std::isalpha(vol[0])) return _T(""); // indicates error, no default value 
        if (vol.size() == 1) return string_t{ _T("\\\\.\\") } + vol[0] + _T(':'); // extract C, append ':'
        if (vol[1] != ':') return _T(""); // indicates error, no default value
        return string_t{ _T("\\\\.\\") } + vol[0] + vol[1]; // extract 'C:' from vol
    }
}

// translate c and c: into "c:\" 
string_t IRecordsLoader::PreNormalize(const string_t& str)
{
    if (str.size() == 1 && std::isalpha(str[0])) return str + _T(":\\");
    if (str.size() == 2 && std::isalpha(str[0]) && str[1] == ':') return str + _T("\\");
    return str;
}

string_t IRecordsLoader::AbsPath(const string_t& str)
{
    std::error_code ec;
    std::filesystem::path relPath(IRecordsLoader::PreNormalize(str));
    std::filesystem::path absPath = std::filesystem::absolute(relPath, ec);
    if (ec) return str;
    return convert_string<string_t::value_type>(absPath.wstring());
}

bool IRecordsLoader::IsPath(const string_t& str)
{
    if (str.size() < 4) return false; // str must contain at least 4 symbols 'c:\f' to look like a path
    if (!std::isalpha(str[0])) return false;
    if (str[1] != ':') return false;
    if (str[2] != '\\') return false;
    return true;
}

// FixupUSA1 makes USA fixes in buffer refered by record param.
// This function is going to be used for processing ALLOC attr Index Blocks or MFT records (only when they read directly from disk, not via WINAPI). 
// When MFT records loadded via WINAPI call they already have USA fixed up.
// BytesPerBlock value is usually defined in ATTR_ROOT attr (field IndexBlockSize) and it may differ from filesystem's ClusterSize.
// For MFT records BytesPerBlock is standard MFT record size (BytesPerMFTRec)
// record buffer should be at least BytesPerBlock size
TErrorCode  IRecordsLoader::FixupUSA1(NTFS_RECORD_HEADER* record, uint32_t BytesPerBlock, uint32_t BytesPerSector)
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

expected_uintptr IRecordsLoader::LoadMFTRecordCache(MFT_REF mftRecRef) // returns NULL if error occurred during loading MFT record
{
    assert(IsOpened());

    uint8_t** result = FMFTRecCache.GetValuePointer(mftRecRef.sId.low);

    if (result == nullptr) // no value in cache, load MFT record from disk
    {
        uint8_t* mftRecBuf = DBG_NEW uint8_t[FVolumeData.BytesPerMFTRec];
        TErrorCode res = LoadMFTRecord(mftRecRef, mftRecBuf);
        if (res != TErrorCode::Success)
            return std::unexpected(res); // error loading MFT record

        //we use mftRecRef.sId.low here because high part of mftRecRef.Id may change when MFT record is modified
        FMFTRecCache.SetValue(mftRecRef.sId.low, mftRecBuf); // update cache

        return mftRecBuf;
    }

    GET_LOGGER;
    logger.Warn("[LoadMFTRecordCache] Record is loaded from cache!");

    return *result; // return MFT record from cache
}

void IRecordsLoader::Close()
{
    if (!IsOpened()) return;

    GET_LOGGER;
    logger.DebugFmt("Closing volume: {}", wtos(FVolumeData.Name));

    // clears data about volume, clears caches

    //CloseHandle(FVolumeData.hVolume);
    auto& volDataBuf = (NTFS_VOLUME_DATA_BUFFER&)FVolumeData;
    ZeroMemory(&volDataBuf, sizeof(NTFS_VOLUME_DATA_BUFFER));
    //FVolumeData.hVolume = INVALID_HANDLE_VALUE;
    FVolumeData.Name.clear();
    FMFTRecCache.Clear();
    FRecordsCount = 0;
    FMetaFilesCount = 0;
}

expected_uint32 IRecordsLoader::ReadMetaFilesCount(TMFTParserBase& parser)
{
    if (!IsOpened()) return std::unexpected(TErrorCode::IOError);
    assert(FRecordsCount > 0);

    uint8_t* mftRecBuf = (uint8_t*)alloca(DEFAULT_BYTES_PER_MFT_REC);
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;
    MFT_REF mftRef{ 0 };
    TErrorCode res;

    while (mftRef.sId.low < FRecordsCount)
    {
        res = InternalLoadMFTRecord(mftRef, mftRecBuf, true); // function checks signature (should be 'FILE'), returns NotInUse error when signature <>'FILE'
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

TErrorCode IRecordsLoader::LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData)
{
    return InternalLoadMFTRecord(mftRecRef, mftRecData, false);
}



void TWinAPIRecordsLoader::InternalOpen(const string_t& vol)
{
    if (IsOpened()) Close();
    assert(FVolumeData.hVolume == INVALID_HANDLE_VALUE);

    string_t vol2 = NormalizeVolume(vol); // returns empty string in case of an error

    if (vol2.size() == 0)
        throw std::runtime_error(std::format("Incorrect volume name specitied: '{}'. Exiting.", convert_string<char>(vol)));

    GET_LOGGER;
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
}

void TWinAPIRecordsLoader::Open(const string_t& vol)
{
    InternalOpen(vol);

    TMFTParserBase parser(*this);
    auto expct =  ReadMetaFilesCount(parser);
    assert(expct);
    FMetaFilesCount = expct.value();
}

void TWinAPIRecordsLoader::Close()
{
    IRecordsLoader::Close();
    CloseHandle(FVolumeData.hVolume);
    FVolumeData.hVolume = INVALID_HANDLE_VALUE;
}


// mftRec should be a buffer with volData.BytesPerMFTRec size
TErrorCode TWinAPIRecordsLoader::InternalLoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData, bool internalCall)
{
    assert(IsOpened());
    assert(FVolumeData.hVolume != INVALID_HANDLE_VALUE);
    assert(mftRecRef.sId.low < FRecordsCount);

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
    // Or we requested record that is Not In Use e.g. during calculating number of meta files
    // Just exit from LoadMFTRecord in that case.
    if (nfrib.FileReferenceNumber.LowPart != pnfrob->FileReferenceNumber.LowPart) // we compare LowPart here to avoid diff by Seq Nums which is checked below
    {
        if (!internalCall)
        {
            GET_LOGGER;
            logger.WarnFmt("Requested MFT Rec ID differs from returned. Looks like requested MFT record is deleted. Requested: {}, returned: {}",
                nfrib.FileReferenceNumber.LowPart, pnfrob->FileReferenceNumber.LowPart);
        }
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
TErrorCode TWinAPIRecordsLoader::ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf)
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
