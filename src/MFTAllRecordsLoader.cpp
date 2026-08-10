#include "Readers.h"

void TMFTAllRecordsLoader::Open(const string_t& vol)
{
    if (IsOpened()) Close();

    TMFTRecordLoader::Open(vol);

    ReadAllMftRecords();
    SetOpened(true);

   /* MFT_REF mftRecRef{0};

    uint8_t* mftRecData = (uint8_t*)alloca(FVolumeData.BytesPerMFTRec);
    MFT_FILE_RECORD* mftRecord = (MFT_FILE_RECORD*)mftRecData;

    // reading $MFT record
    auto res = TMFTRecordLoader::LoadMFTRecord(mftRecRef, mftRecData);
    if (res != TErrorCode::Success)
        return; //TODO may be we need throw exception here

    TMFTParserBase prsr(*this);
    TAttrCollection collection;

    res = prsr.FillAttrCollection(mftRecord, MakeAttrBitmask(ATTR_BITMAP), collection);
    if (res != TErrorCode::Success)
        return; //TODO may be we need throw exception here

    auto& adata = collection.Get(ATTR_BITMAP);
    assert(1ul == adata.Count()); // $MFT file contain only one ATR_BITMAP attribute
    assert(adata[0]->NonResidentFlag == ATTR_FLAG_NONRESIDENT);

    FBitmap.Clear();
    res = prsr.ParseNonresBitmap(adata[0], FBitmap);
    if (res != TErrorCode::Success)
        return; //TODO may be we need throw exception here
    
    assert(FBitmap.BitsCount() >= FVolumeData.MftValidDataLength.QuadPart / FVolumeData.BytesPerMFTRec);
    */



   /* for (uint32_t i = 1; i < FBitmap.BitsCount(); i++)
    {
        if (FBitmap.Test(i))
            assert(FMFTRecs.FKeys.IndexOf(i) != -1);
        else
            assert(FMFTRecs.FKeys.IndexOf(i) == -1);
    }*/

}

TErrorCode TMFTAllRecordsLoader::LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData)
{
    if(!IsOpened()) return TErrorCode::IOError;

    if (FBitmap.Test(mftRecRef.sId.low))
    {
        //TODO mft rec copying happens here, think how to avoid that
        FRecs.Get(mftRecRef.sId.low, mftRecData);
        return TErrorCode::Success;
    }
    else
        return TErrorCode::NotFound;
    
}

TErrorCode TMFTAllRecordsLoader::ReadAllMftRecords()
{
    assert(FVolumeData.hVolume != INVALID_HANDLE_VALUE);

    DWORD bytesReturned;
    NTFS_FILE_RECORD_INPUT_BUFFER inputBuf{};

    // calculate number of MFT records
    inputBuf.FileReferenceNumber.QuadPart = FVolumeData.MftValidDataLength.QuadPart / FVolumeData.BytesPerMFTRec;
    FBitmap.SetData(((inputBuf.FileReferenceNumber.QuadPart - 1) >> TBitField::DWORD_2POWER) + 1, false);

    FRecs.Clear();
    FRecs.SetItemSize(FVolumeData.BytesPerMFTRec);
    FRecs.SetCount(inputBuf.FileReferenceNumber.LowPart);

    inputBuf.FileReferenceNumber.QuadPart--; // last MFT record index

    // size of fixed fields of NTFS_FILE_RECORD_OUTPUT_BUFFER + size of MFT single record 
    ULONG cb = offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer[FVolumeData.BytesPerMFTRec]);
    PNTFS_FILE_RECORD_OUTPUT_BUFFER pOutputBuf = (PNTFS_FILE_RECORD_OUTPUT_BUFFER)alloca(cb);
    assert(pOutputBuf);

    do
    {
        if (!DeviceIoControl(FVolumeData.hVolume, FSCTL_GET_NTFS_FILE_RECORD, &inputBuf, sizeof(inputBuf), pOutputBuf, cb, &bytesReturned, nullptr))
        {
            GET_LOGGER;
            logger.ErrorFmt("DeviceIoControl failed with error. Error code: {}", GetLastError());
            return TErrorCode::IOError;
        }

        uint8_t* mftRecData = pOutputBuf->FileRecordBuffer;
        MFT_FILE_RECORD* mftRecord = (MFT_FILE_RECORD*)(mftRecData);

        assert(pOutputBuf->FileReferenceNumber.LowPart == mftRecord->IndexMFTRec);

        if (ntfs_is_file_recp(mftRecord->RecHeader.Signature))
        {
            //add to cache only if signature is valid
            FRecs.Update(mftRecord->IndexMFTRec, mftRecData);
            FBitmap.SetTrue(mftRecord->IndexMFTRec);
        }
        else
        {
            GET_LOGGER;
            logger.WarnFmt("Signature do NOT MATCH. MFT RecID: {}, Expected: {}, Actual: {}",
                mftRecord->IndexMFTRec, (uint32_t)NTFS_SIGNATURE::magic_FILE, std::string((char*)mftRecord->RecHeader.Signature, 4));
        }

        inputBuf.FileReferenceNumber.QuadPart = pOutputBuf->FileReferenceNumber.QuadPart - 1;

    } while (inputBuf.FileReferenceNumber.QuadPart > 0); // do not read builtin metafiles

    return TErrorCode::Success;
}
