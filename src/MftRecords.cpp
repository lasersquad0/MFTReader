
// this is to remove defines min, max in windows headers because they conflict with std::min std::max 
#define NOMINMAX

#include "Debug.h"
#include "NTFS.h"
#include "Functions.h"
#include "Caches.h"
#include "Readers.h"





bool ReadAllMftRecords(string_t volume, TLCNRecs& mftRecs)
{
    cout_t << _T("Opening volume: ") << volume << std::endl;

    HANDLE hVolume = CreateFile(volume.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);

    if (hVolume == INVALID_HANDLE_VALUE)
    {
        cout_t << _T("Error opening volume: ") << GetLastError() << std::endl;
        return false;
    }

    NTFS_VOLUME_DATA_BUFFER vdb;
    //OVERLAPPED ov = {};
    int cnt = 0;
    DWORD bytesReturned;
    if (DeviceIoControl(hVolume, FSCTL_GET_NTFS_VOLUME_DATA, 0, 0, &vdb, sizeof(vdb), &bytesReturned, nullptr/*&ov*/))
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
            if (!DeviceIoControl(hVolume, FSCTL_GET_NTFS_FILE_RECORD, &inputBuf, sizeof(inputBuf), pOutputBuf, cb, &bytesReturned, nullptr/*&ov*/))
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
                std::string str = std::format("Signature do NOT MATCH. MFT RecID: {}, Expected: {}, Actual: {}", pmftrec->IndexMFTRec, (uint32_t)NTFS_SIGNATURE::magic_FILE, std::string((char*)pmftrec->RecHeader.Signature, 4));
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

    HANDLE hFile = CreateFileW(lpFileName, 0, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL /* FILE_FLAG_BACKUP_SEMANTICS FILE_OPEN_FOR_BACKUP_INTENT*/, 0);

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
                            SetFilePointerEx(hVolume, off, nullptr, FILE_BEGIN); //TODO error handling here?

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
