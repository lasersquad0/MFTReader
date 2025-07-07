
// this is to remove defines min, max in windows headers because they conflict with std::min std::max 
#define NOMINMAX

#include "Debug.h"
#include "NTFS.h"
#include "Functions.h"
#include "Caches.h"

// reads series of sequential volume clusters starting from #lcnStart
// dataBuf should be large enough to fit lcnCnt clusters of data
bool ReadClusters(const VOLUME_DATA& volData, uint64_t lcnStart, uint32_t lcnCnt, PBYTE dataBuf)
{
    LARGE_INTEGER offset{0};
    DWORD bytesRead;

    offset.QuadPart = lcnStart * volData.BytesPerCluster;

    BOOL res = SetFilePointerEx(volData.hVolume, offset, NULL, FILE_BEGIN);
    if (!res)
    {
        GET_LOGGER;
        logger.ErrorFmt("ReadCluster.SetFilePointerEx has failed with error: {}", GetLastError());
        return false;
    }

    // read clusterCnt clusters
    res = ReadFile(volData.hVolume, dataBuf, lcnCnt * volData.BytesPerCluster, &bytesRead, nullptr);
    if (res)
    {
        assert(lcnCnt * volData.BytesPerCluster == bytesRead);
        return true;
    }
    else
    {
        GET_LOGGER;
        logger.ErrorFmt("ReadCluster.ReadFile has failed with error: {}", GetLastError());
        return false;
    }
}

// dataBuf contains data for rlilen clusters
bool FixupUSA(const VOLUME_DATA& volData, PBYTE dataBuf, DATA_RUN_ITEM& rli, uint32_t rlilen)
{
    NTFS_RECORD_HEADER* indexRec = (NTFS_RECORD_HEADER*)dataBuf;
    uint32_t wordsPerSector = volData.BytesPerSector >> 1;

    // loop by LCNs loaded into dataBuf
    for (size_t i = 0; i < rlilen; i++)
    {
        if (!ntfs_is_indx_recp(indexRec->Signature)) // bypass non 'INDX' clusters (usually filled by zero)
        {
            /* This is correct situation when list of LCNs in one data run has "holes" according to Bitmap attribute.
            *  We read all LCNs from current data run as a single operation. Some of these LCNs are "not used" and do not contain INDX signature
            *  Such LCNs have appropriate bit=0 in Bitmap attribute.
            */

            GET_LOGGER;
            uint8_t* sign = indexRec->Signature;
            logger.WarnFmt("[FixupUSA] Signature 'INDX' has not been found in LCN cluster {}. Signature found: {}{}{}{}", rli.lcn + i, sign[0], sign[1], sign[2], sign[3]);

            continue;
        }

        uint16_t sectorsCnt = indexRec->FixupCnt - 1;
        assert(sectorsCnt == volData.BytesPerCluster / volData.BytesPerSector);

        uint16_t* fixuparr = (uint16_t*)(Add2Ptr(indexRec, indexRec->FixupOffset));
        uint16_t checkValue = *fixuparr;
        fixuparr++; // now it refers to first array item

        uint16_t* sectorEnd = (uint16_t*)(indexRec)+wordsPerSector - 1;

        uint s = 0;
        while (s < sectorsCnt)
        {
            assert(checkValue == *sectorEnd);
            if (checkValue != *sectorEnd) return false; // looks like data is corrupted in this sector

            *sectorEnd = fixuparr[s]; // restore data

            sectorEnd += wordsPerSector;
            s++;
        }

        indexRec = (NTFS_RECORD_HEADER*)Add2Ptr(indexRec, volData.BytesPerCluster);
    }

    return true;
}

// mftRec should be a buffer with volData.BytesPerMFTRec size
bool LoadMFTRecord(const VOLUME_DATA& volData, MFT_REF recID, uint8_t* mftRec)
{
    GET_LOGGER;

    NTFS_FILE_RECORD_INPUT_BUFFER nfrib;
    nfrib.FileReferenceNumber.QuadPart = recID.Id;
    //nfrib.FileReferenceNumber.QuadPart = nvdb.MftValidDataLength.QuadPart / nvdb.BytesPerFileRecordSegment - 1;

    //ULONG cb = __builtin_offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer[volData.BytesPerMFTRec]);
    ULONG cb = offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer[volData.BytesPerMFTRec]);

    auto pnfrob = (PNTFS_FILE_RECORD_OUTPUT_BUFFER)alloca(cb);

    if (!DeviceIoControl(volData.hVolume, FSCTL_GET_NTFS_FILE_RECORD, &nfrib, sizeof(nfrib), pnfrob, cb, 0, nullptr))
    {
        logger.ErrorFmt("DeviceIoControl failed with error. Error code: {}", GetLastError());
        return false;
    }

    MFT_FILE_RECORD* mftRecord = (MFT_FILE_RECORD*)(pnfrob->FileRecordBuffer);
    assert(mftRecord->IndexMFTRec == recID.sId.low); // make sure we've got the same record as requested.

    //TODO think how to avoid this memcpy_s
    memcpy_s(mftRec, volData.BytesPerMFTRec, mftRecord, volData.BytesPerMFTRec);

    return true;
}

// returns NULL if error occurred during loading MFT record
uint8_t* LoadMFTRecordCache(const VOLUME_DATA& volData, MFT_REF recID)
{
    TMFTRecCache* cache = Singleton<TMFTRecCache>::getInstance();

    uint8_t** result = cache->GetValuePointer(recID.Id);
    if (result == nullptr) // no value in cache, load mft record from disk
    {
        uint8_t* mftRecBuf = DBG_NEW uint8_t[volData.BytesPerMFTRec];
        if (!LoadMFTRecord(volData, recID, mftRecBuf))
            return nullptr; // error loading mft record, it means that DeviceIoControl failed with error

        //TODO shall we use recID.sId.low here because high part of recID.Id may change when MFT record is modified
        cache->SetValue(recID.Id, mftRecBuf); // update cache

        return mftRecBuf;
    }

    return *result; // return mft record from cache
}

// attr parameter cannot be NULL here. It should be valid pointer to MFT_ATTR_HEADER structure
bool DataRunsDecode(MFT_ATTR_HEADER* attr, THArray<DATA_RUN_ITEM>& runs)
{
    GET_LOGGER;

    // we do not need this assert because sometimes one ATTR_LIST list may contain two ATTR_ALLOC attributes for some reason
    // it means we come here two times during parsing one MFT record with such ATTR_LIST 
    //assert(runs.Count() == 0); // make sure that old data runs are cleared

    if (!attr || !attr->NonResidentFlag) // parameters validation
    {
        // this in incorrect data in MFT
        logger.Error("[DataRunsDecode] Attr (!currAttr || !currAttr->NonResidentFlag) = TRUE");
        return false;
    }

    uint8_t* datarun = Add2Ptr(attr, attr->nonres.DataRunsOffset);
    uint8_t* attrEnd = Add2Ptr(attr, attr->AttrSize);
    assert(attr->AttrSize > 0);

    uint64_t currVCN = attr->nonres.StartVCN;
    uint64_t currLCN = 0;

    // read all data runs 
    while ((datarun < attrEnd) && *datarun) // stop if we reached zero in both half bytes
    {
        DATA_RUN_ITEM ri;
        int64_t deltaxcn;

        ri.lcn = currLCN;
        ri.vcn = currVCN;

        uint8_t lens = *datarun;
        uint8_t b = lens & 0x0F; // minor half byte is length (in bytes) of the following int value "number of clusters in current data run"
        if (b)
        {
            // reading number of bytes specified in minor half byte and interpret it as integer "number of clusters"
            for (deltaxcn = datarun[b--]; b; b--)
                deltaxcn = (deltaxcn << 8) + datarun[b];
        }
        else
        {
            // the length entry cannot be zero
            logger.Error("[DataRunsDecode] Missing length entry in mapping pairs (run len) array.");
            //deltaxcn = (int64_t)-1;
            return false;
        }

        assert(deltaxcn > 0);
        ri.len = deltaxcn;
        currVCN += deltaxcn;

        // major half byte is a length (in bytes) the of LCN 
        uint8_t b2 = lens & 0x0F;
        uint8_t b3 = b = b2 + ((lens >> 4) & 0x0F);
        deltaxcn = (datarun[b] & 0x80) ? (uint64_t)-1 : 0; // delta LCN can be negative in datarun! Fill initial deltaxcn with 0xFFF..FFF in that case 
        //deltaxcn = datarun[b--]
        for (; b > b2; b--) // read num of bytes specified in major half byte and interpret it as LCN
            deltaxcn = (deltaxcn << 8) + datarun[b];

        ri.lcn += deltaxcn;
        currLCN = ri.lcn;

        runs.AddValue(ri);

        datarun += b3 + 1; // move to the next data run

        logger.DebugFmt("DataRunsDecode VCN: {}", ri.vcn);
        logger.DebugFmt("DataRunsDecode LCN: {}", ri.lcn);
        logger.DebugFmt("DataRunsDecode Length: {}", ri.len);
    }

    logger.DebugFmt("[DataRunsDecode] Data Runs Count: {}", runs.Count());

    return true;
}







bool ReadAllMftRecords(PCWSTR szVolume, TLCNRecs& mftRecs)
{
    std::wcout << L"Opening volume: " << szVolume << std::endl;

    HANDLE hVolume = CreateFile(szVolume, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);

    if (hVolume == INVALID_HANDLE_VALUE)
    {
        std::wcout << L"Error opening volume: " << GetLastError() << std::endl;
        return false;
    }

    NTFS_VOLUME_DATA_BUFFER vdb;
    //OVERLAPPED ov = {};
    int cnt = 0;

    if (DeviceIoControl(hVolume, FSCTL_GET_NTFS_VOLUME_DATA, 0, 0, &vdb, sizeof(vdb), 0, nullptr/*&ov*/))
    {
        NTFS_FILE_RECORD_INPUT_BUFFER inputBuf;
        // calculate MFT last record number
        inputBuf.FileReferenceNumber.QuadPart = vdb.MftValidDataLength.QuadPart / vdb.BytesPerFileRecordSegment - 1;

        mftRecs.SetRecordSize(vdb.BytesPerFileRecordSegment);
        mftRecs.SetCapacity(inputBuf.FileReferenceNumber.LowPart);

        // size of fixed fields of NTFS_FILE_RECORD_OUTPUT_BUFFER + size of MFT single record 
        ULONG cb = offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer[vdb.BytesPerFileRecordSegment]);
        PNTFS_FILE_RECORD_OUTPUT_BUFFER pOutputBuf = (PNTFS_FILE_RECORD_OUTPUT_BUFFER)malloc(cb);
        assert(pOutputBuf);

        do
        {
            if (!DeviceIoControl(hVolume, FSCTL_GET_NTFS_FILE_RECORD, &inputBuf, sizeof(inputBuf), pOutputBuf, cb, 0, nullptr/*&ov*/))
            {
                std::wcout << L"DeviceIoControl failed with error: " << GetLastError() << std::endl;
                free(pOutputBuf);
                CloseHandle(hVolume);
                return false;
            }

            uint8_t* pFileRec = pOutputBuf->FileRecordBuffer;
            MFT_FILE_RECORD* pmftrec = (MFT_FILE_RECORD*)pFileRec;

            //TODO return it back later
            //mftRecs.AddMFTRec(pFileRec, pmftrec->IndexMFTRec);

            assert(pOutputBuf->FileReferenceNumber.LowPart == pmftrec->IndexMFTRec);

            if (!ntfs_is_file_recp(pmftrec->RecHeader.Signature))
            {
                std::string str = std::format("Signature do NOT MATCH. MFT RecID: {}, Expected: {}, Actual: {}", pmftrec->IndexMFTRec, (uint32_t)NTFS_SIGNATURE::magic_FILE, (char*)pmftrec->RecHeader.Signature);
                std::cout << str << std::endl;
            }

            inputBuf.FileReferenceNumber.QuadPart = pOutputBuf->FileReferenceNumber.QuadPart - 1;
            cnt++;

        } while (32 < inputBuf.FileReferenceNumber.QuadPart); // do not read buildin metafiles

        free(pOutputBuf);
    }

    CloseHandle(hVolume);
    return true;
}

void ReadMft2(PCWSTR szVolume, HANDLE hVolume, PNTFS_VOLUME_DATA_BUFFER nvdb)
{
    static PCWSTR MFT = L"\\SourceForge\\LogEngine\\logengine\\prj\\MSVC\\.vs\\LogEngine\\v17\\ipch\\AutoPCH\\4cb79dc870ced840\\Itransition CV - Architect - Ilya Skiba.pdf";
    static STARTING_VCN_INPUT_BUFFER vcn{};
    static volatile UCHAR guz;
    size_t len;

    PVOID stack = alloca(guz);

    union
    {
        PVOID buf;
        PWSTR lpFileName;
        PRETRIEVAL_POINTERS_BUFFER rpb;
    };

    // len is in wchars, not bytes
    len = wcslen(szVolume) + wcslen(MFT) + 1; //TODO do we need +1 here for terminating 0?
    lpFileName = (PWSTR)alloca(len * sizeof(WCHAR));

    wcscpy_s(lpFileName, len, szVolume);
    //unsigned len2 = wcslen(lpFileName);
    wcscat_s(lpFileName, len, MFT);

    HANDLE hFile = CreateFile(lpFileName, 0, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL /* FILE_FLAG_BACKUP_SEMANTICS FILE_OPEN_FOR_BACKUP_INTENT*/, 0);

    if (hFile != INVALID_HANDLE_VALUE)
    {
        OVERLAPPED ov{};

        ULONG cb = Diff2Ptr(buf, stack); // (ULONG)(PBYTE(stack) - PBYTE(buf)); /*RtlPointerToOffset(buf, stack),*/
        ULONG rcb, ExtentCount = 2;
        DWORD err;

        do
        {
            //rcb = __builtin_offsetof(RETRIEVAL_POINTERS_BUFFER, Extents[ExtentCount]);
            rcb = offsetof(RETRIEVAL_POINTERS_BUFFER, Extents[ExtentCount]);

            if (cb < rcb)
            {
                buf = alloca(rcb - cb);
                cb = Diff2Ptr(buf, stack); // PBYTE(stack) - PBYTE(buf); // RtlPointerToOffset(buf = alloca(rcb - cb), stack);
            }

            if (DeviceIoControl(hFile, FSCTL_GET_RETRIEVAL_POINTERS, &vcn, sizeof(vcn), buf, cb, 0, &ov))
            {
                /* if (rpb->Extents->Lcn.QuadPart != nvdb->MftStartLcn.QuadPart)
                 {
                     __debugbreak();
                 }*/

                ExtentCount = rpb->ExtentCount;
                if (ExtentCount > 0)
                {
                    auto Extents = rpb->Extents;

                    ULONG BytesPerCluster = nvdb->BytesPerCluster;
                    ULONG BytesPerFileRecordSegment = nvdb->BytesPerFileRecordSegment;

                    //LONGLONG StartingVcn = rpb->StartingVcn.QuadPart, NextVcn, len2;

                    PVOID FileRecordBuffer = alloca(BytesPerFileRecordSegment);

                    do
                    {
                        //NextVcn = Extents->NextVcn.QuadPart;
                        //len2 = NextVcn - StartingVcn, StartingVcn = NextVcn;

                        //std::cout << std::format("{} {}\n", Extents->Lcn.QuadPart, len2);
                        //DbgPrint("%I64x %I64x\n", Extents->Lcn.QuadPart, len);

                        if (Extents->Lcn.QuadPart != -1)
                        {
                            LARGE_INTEGER off;

                            off.QuadPart = (uint64_t)21723753 * BytesPerCluster;
                            //off.QuadPart = Extents->Lcn.QuadPart * BytesPerCluster;

                            //Extents->Lcn.QuadPart *= BytesPerCluster;
                            //ov.Offset = off.LowPart;// Extents->Lcn.LowPart;
                            //ov.OffsetHigh = off.HighPart;// Extents->Lcn.HighPart;

                            // Set the file pointer to the desired cluster
                            SetFilePointerEx(hVolume, off, NULL, FILE_BEGIN);

                            // read 1 record
                            BOOL res = ReadFile(hVolume, FileRecordBuffer, BytesPerFileRecordSegment, 0, nullptr/*&ov*/);
                            if (!res)
                            {
                                std::cout << "ReadFile failed with error: " << GetLastError() << std::endl;
                                break;
                            }
                        }

                    } while (Extents++, --ExtentCount);
                }
                break;
            }

            ExtentCount <<= 1;
            err = GetLastError();
        } while (err == ERROR_MORE_DATA);

        CloseHandle(hFile);
    }
}

/*
std::cout << "Hello World!\n";

TCHAR fl[] = L"\\\\?\\C:\\$MFT";

HANDLE hFile, hVolume = 0;
IO_STATUS_BLOCK iosb;
OBJECT_ATTRIBUTES oa;

NTSTATUS status = NtOpenFile(&hFile, SYNCHRONIZE, &oa, &iosb, FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT);

union
{
    FILE_INTERNAL_INFORMATION fii;

    NTFS_FILE_RECORD_INPUT_BUFFER nfrib;

    struct
    {
        LONGLONG MftRecordIndex : 48;
        LONGLONG SequenceNumber : 16;
    };
};

if (0 <= (status = NtQueryInformationFile(hFile, &iosb, &fii, sizeof(fii), FileInternalInformation)))
{
    //need open '\Device\HarddiskVolume<N>' or '<X>:'
   // status = OpenVolume(hFile, &hVolume);
}

NtClose(hFile);
*/
