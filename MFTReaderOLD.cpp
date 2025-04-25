
#include <iostream>
#include <cassert>
#include <string>
//#include <winternl.h>
#include <shlwapi.h>

#include "LogEngine.h"
#include "PatternLayout.h"
#include "Functions.h"
#include "MTFReader.h"

#pragma pack(show)

#define ATTR_TYPE_NAMES { L"zero", L"STANDARD INFO", L"attr list", L"FILENAME", L"OBJECT ID", L"secure info", L"label", \
                          L"volume info", L"DATA", L"INDEX ROOT", L"ALLOCATION", L"BITMAP", L"REPARSE", L"EA_INFORMATION", \
                          L"EA", L"PROPERTYSHEET", L"utility stream" }

static const wchar_t* AttrTypeNames[] ATTR_TYPE_NAMES;

static const wchar_t* FileNameTypes[]{ L"POSIX", L"UNICODE", L"DOS", L"UNICODE_AND_DOS" };


void ReadMft(PCWSTR szVolume)
{
    std::wcout << "Opening volume: " << szVolume << std::endl;

    HANDLE hVolume = CreateFile(szVolume, GENERIC_READ /*FILE_READ_DATA*/, /*FILE_SHARE_VALID_FLAGS*/ FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, 
                                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS /* FILE_OPEN_FOR_BACKUP_INTENT*/, 0);

    if (hVolume == INVALID_HANDLE_VALUE)
    {
        std::cout << "Error opening volume: " << std::to_string(GetLastError()) << std::endl;
    }
    else
    {
        NTFS_VOLUME_DATA_BUFFER nvdb;
        OVERLAPPED ov = {};

        if (DeviceIoControl(hVolume, FSCTL_GET_NTFS_VOLUME_DATA, 0, 0, &nvdb, sizeof(nvdb), 0, &ov))
        {
            NTFS_FILE_RECORD_INPUT_BUFFER nfrib;

            nfrib.FileReferenceNumber.QuadPart = 136309; // 216632; // 158; //44; // 13569; // 158; 68; 3501 68; // nvdb.MftValidDataLength.QuadPart / nvdb.BytesPerFileRecordSegment - 1;

            ULONG cb = __builtin_offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer[nvdb.BytesPerFileRecordSegment]);
            //ULONG cb = offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer[nvdb.BytesPerFileRecordSegment]);

            PNTFS_FILE_RECORD_OUTPUT_BUFFER pnfrob = (PNTFS_FILE_RECORD_OUTPUT_BUFFER)alloca(cb);

            do
            {
                if (!DeviceIoControl(hVolume, FSCTL_GET_NTFS_FILE_RECORD, &nfrib, sizeof(nfrib), pnfrob, cb, 0, &ov))
                {
                    std::cout << "DeviceIoControl failed with error: " << GetLastError() << std::endl;
                    break;
                }

                uint8_t* pFileRec = pnfrob->FileRecordBuffer;
                MFT_FILE_RECORD* pmftrec = (MFT_FILE_RECORD*)pFileRec;

                std::wcout << L"-------------------" << std::endl;
                std::wcout << L"Signature: " << (char*)pmftrec->RecHeader.Signature << std::endl;
                std::wcout << L"MFT rec NUM: " << pnfrob->FileReferenceNumber.QuadPart << std::endl;
                std::wcout << L"MFT rec ID: " << pmftrec->IndexMFTRec << std::endl;
                std::wcout << L"Parent MFT rec ID: 0x" << std::hex << pmftrec->ParentFileRec.Id << std::dec << std::endl;
                std::wcout << L"Size: " << pmftrec->FileRecSize << std::endl;
                std::wcout << L"Alloc Size: " << pmftrec->AllocFileRecSize << std::endl;

                if (pmftrec->Flags == 0x01) std::wcout << L"File rec type: FILE" << std::endl;
                else if (pmftrec->Flags == 0x02) std::wcout << L"File rec type: DIRECTORY" << std::endl;
                else if (pmftrec->Flags == 0x03) std::wcout << L"File rec type: FILE_DIRECTORY " << pmftrec->Flags << std::endl;
                else std::wcout << L"File rec type: UNKNOWN " << pmftrec->Flags << std::endl;

                MFT_ATTR_HEADER* currAttr = (MFT_ATTR_HEADER*)(pFileRec + pmftrec->FirstAttrOffset);
               
                LogEngine::Logger& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);
                //LogEngine::Logger& loggerlist = LogEngine::GetLogger(MFT_LOGGER_NAME_LIST);

                THArray<FILE_NAME> dirList;

                int i = 1;
                do  // reading all attributes in a loop
                {
                    logger.DebugFmt("****** #{} Attribute ({:#X} {})", i++, (uint32_t)currAttr->AttrType, wtos(AttrTypeNames[currAttr->AttrType >> 4]));
                    logger.DebugFmt("Attr Id: {}", currAttr->AttrID);
                    logger.DebugFmt("Attr flags: {}", currAttr->Flags);
                    //std::wcout << L" ****** #" << i++ << " Attribute (0x" << std::hex << currAttr->AttrType << std::dec << " " << LogLevelNames[currAttr->AttrType >> 4] << ")" << std::endl;
                    //std::wcout << L"AttrID: " << currAttr->AttrID << std::endl;
                    //std::wcout << L"Attr flags: " << currAttr->Flags << std::endl;
                    
                    if (currAttr->AttrNameSize > 0) // if attr has name - show it
                    {
                        wchar_t* attrname = GetAName(currAttr, AttrNameOffset); // (wchar_t*)((uint8_t*)currAttr + currAttr->AttrNameOffset);
                        std::wstring name(attrname, currAttr->AttrNameSize);
                        logger.DebugFmt("Attr name: {}", wtos(name));
                        //std::wcout << L"AttrName: " << name << std::endl;
                    }

                    if (currAttr->NonResidentFlag == 0) // attribute is RESident
                    {
                        logger.DebugFmt("Attr indexed: {}", currAttr->res.IndexedFlag);
                        //std::wcout << L"Attr indexed: " << currAttr->res.IndexedFlag << std::endl;

                        uint8_t* attrValue = (PBYTE)currAttr + currAttr->res.DataOffset;
                        DWORD dateTimeFlags = FDTF_DEFAULT | FDTF_NOAUTOREADINGORDER;

                        switch (currAttr->AttrType)
                        {
                        case ATTR_STD_INFO:
                        {                            
                            ATTR_STD_INFO5* stdinfo = (ATTR_STD_INFO5*)attrValue;
                            wchar_t buf[100];
                            FILETIME ft;

                            ft.dwLowDateTime = LODWORD(stdinfo->CreateTime);
                            ft.dwHighDateTime = HIDWORD(stdinfo->CreateTime);
                            SHFormatDateTime(&ft, &dateTimeFlags, buf, 100);
                            logger.DebugFmt("Created: {}", wtos(buf));
                            //std::wcout << L"Create time: " << buf << std::endl;

                            ft.dwLowDateTime = LODWORD(stdinfo->ModifyTime);
                            ft.dwHighDateTime = HIDWORD(stdinfo->ModifyTime);
                            SHFormatDateTime(&ft, &dateTimeFlags, buf, 100);
                            logger.DebugFmt("Modified: {}", wtos(buf));
                            //std::wcout << L"Modified: " << buf << std::endl;

                            ft.dwLowDateTime = LODWORD(stdinfo->LastAccessTime);
                            ft.dwHighDateTime = HIDWORD(stdinfo->LastAccessTime);
                            SHFormatDateTime(&ft, &dateTimeFlags, buf, 100);
                            logger.DebugFmt("Last access: {}", wtos(buf));
                            //std::wcout << L"Last access: " << buf << std::endl;

                            //std::wcout << L"Version number: " << stdinfo->VersionNum << std::endl;
                           // std::wcout << L"Max version num: " << stdinfo->max_ver_num << std::endl;
                            //std::wcout << L"Class Id: " << stdinfo->class_id << std::endl;


                            break;
                        }
                        case ATTR_FILENAME:
                        {
                            ATTR_FILE_NAME* fname = (ATTR_FILE_NAME*)attrValue;

                            if (fname->NameType != FILE_NAME_DOS) // do not print DOS file names
                            {
                                wchar_t* tmp = GetFName(fname, sizeof(ATTR_FILE_NAME));
                                std::wstring name(tmp, fname->FileNameLen);

                                logger.DebugFmt("File parent rec ID : {:#x}", fname->ParentDir.Id);
                                logger.DebugFmt("File name type : {} ({})", fname->NameType, wtos(FileNameTypes[fname->NameType]));
                                logger.DebugFmt("File name: {}", wtos(name));
                                logger.DebugFmt("File DOS attrib : {:#x}", fname->dup.FileAttrib);
                                //std::wcout << L"File parent rec ID: 0x" << std::hex << fname->ParentDir.Id << std::dec << std::endl;
                                //std::wcout << L"File name type: " << fname->NameType << " (" << FileNameTypes[fname->NameType] << ")" << std::endl;
                                //std::wcout << L"FileName: " << name << std::endl;
                                //std::wcout << L"File DOS Attrib: 0x" << std::hex << (uint32_t)fname->dup.FileAttrib << std::dec << std::endl;

                                wchar_t buf[100];
                                FILETIME ft;

                                ft.dwLowDateTime = LODWORD(fname->dup.cr_time);
                                ft.dwHighDateTime = HIDWORD(fname->dup.cr_time);
                                SHFormatDateTime(&ft, &dateTimeFlags, buf, 100);
                                logger.DebugFmt("Created: {}", wtos(buf));
                                //std::wcout << L"Created: " << buf << std::endl;

                                ft.dwLowDateTime = LODWORD(fname->dup.m_time);
                                ft.dwHighDateTime = HIDWORD(fname->dup.m_time);
                                SHFormatDateTime(&ft, &dateTimeFlags, buf, 100);
                                logger.DebugFmt("Modified: {}", wtos(buf));
                                //std::wcout << L"File modification time: " << buf << std::endl;
                                
                                ft.dwLowDateTime = LODWORD(fname->dup.a_time);
                                ft.dwHighDateTime = HIDWORD(fname->dup.a_time);
                                SHFormatDateTime(&ft, &dateTimeFlags, buf, 100);
                                logger.DebugFmt("Last access: {}", wtos(buf));
                                //std::wcout << L"Last access: " << buf << std::endl;

                                /*ft.dwLowDateTime = LODWORD(fname->dup.c_time);
                                ft.dwHighDateTime = HIDWORD(fname->dup.c_time);
                                SHFormatDateTime(&ft, &dateTimeFlags, buf, 100);
                                std::wcout << L"Attrib modification time: " << buf << std::endl;*/
                            }
                            break;
                        }
                        case ATTR_ID:
                        {
                            OBJECT_ID* objID = (OBJECT_ID*)attrValue;
                            
                            wchar_t buf[100];
                            if (!StringFromGUID2(objID->ObjId, buf, 100))
                                logger.Error("ATTR_ID Error in StringFromGUID2.");

                            logger.DebugFmt("Object ID: {}", wtos(buf));
                            //std::wcout << L"Object ID: " << buf << std::endl;

                            break;
                        }
                        case ATTR_ROOT:
                        {
                            INDEX_ROOT* indexR = (INDEX_ROOT*)attrValue;
                            logger.DebugFmt("Stored attr type: {:#x} ({})", (uint32_t)indexR->type, wtos(AttrTypeNames[indexR->type >> 4]));
                            logger.DebugFmt("Collation rule: {}", (uint32_t)indexR->rule);
                            logger.DebugFmt("Dir type: {} {}", indexR->ihdr.flags, (indexR->ihdr.flags == 0 ? " (small dir)" : " (big dir)"));
                            //std::wcout << L"IndexRoot type: 0x" << std::hex << indexR->type << std::dec << std::endl;
                            //std::wcout << L"IndexRoot collation rule: " << (uint32_t)indexR->rule << std::endl;
                            //std::wcout << L"IndexRoot dir type: " << indexR->ihdr.flags << (indexR->ihdr.flags == 0 ? " (small dir)":" (big dir)") << std::endl;

                            auto pihdr = &(indexR->ihdr);

                            GetFileList(pihdr, dirList);

                            break;
                        }
                        case ATTR_ALLOC:
                        {
                            logger.Debug("Resident ATTR_ALLOC has been met.");

                            break;
                        }
                        case ATTR_BITMAP:
                        {
                            logger.Debug("Resident ATTR_BITMAP has been met");
                            //std::wcout << L"ATTR_BITMAP has been met " << std::endl;

                            break;
                        }
                        case ATTR_DATA:
                        {
                            logger.DebugFmt("Resident Data size: {}", currAttr->res.DataSize);
                            //std::wcout << L"ATTR DATA stream size: " << currAttr->res.DataSize << std::endl;
                            //auto rtree = (RunsTree*)((PBYTE)currAttr + currAttr->nonres.DataRunsOffset);
                            
                            break;
                        }
                        case ATTR_SECURE:
                        {
                            logger.Debug("Resident ATTR_SECURE has been met");
                            //std::wcout << L"ATTR_SECURE has been been met " << std::endl;

                            break;
                        }
                        case ATTR_LIST_ATTR:
                        {
                            logger.Debug("Resident ATTR_LIST_ATTR has been met");
                            //std::wcout << L"ATTR_LIST_ATTR has been met " << std::endl;

                            break;
                        }
                        case ATTR_REPARSE:
                        {
                            logger.Debug("Resident ATTR_REPARSE has been met");
                            //std::wcout << L"ATTR_REPARSE has been met " << std::endl;

                            break;
                        }
                        default:
                            logger.Warn("UNKNOWN Resident ATTR has been met");
                            //std::wcout << L"UNKNOWN Resident ATTR has been met " << std::endl;

                        }

                    } 
                    else // Attribute is NONresident
                    {
                        logger.Debug("Attr NONResident");
                        logger.DebugFmt("Attr StartVCN: {}", currAttr->nonres.StartVCN);
                        logger.DebugFmt("Attr LastVCN: {}", currAttr->nonres.LastVCN);
                        logger.DebugFmt("Attr RealSize: {}", currAttr->nonres.RealSize);
                        logger.DebugFmt("Attr StreamSize: {}", currAttr->nonres.StreamSize);
                        logger.DebugFmt("Attr AllocatedSize: {}", currAttr->nonres.AllocatedSize);
                        //std::wcout << L"Attr NONResident" << std::endl;
                        //std::wcout << L"Attr StartVCN: " << currAttr->nonres.StartVCN << std::endl;
                        //std::wcout << L"Attr LastVCN: " << currAttr->nonres.LastVCN << std::endl;
                        //std::wcout << L"Attr RealSize: " << currAttr->nonres.RealSize << std::endl;
                        //std::wcout << L"Attr StreamSize: " << currAttr->nonres.StreamSize << std::endl;
                        

                        switch (currAttr->AttrType)
                        {
                        case ATTR_DATA:
                        {
                            //std::wcout << L"DATA NONres real size: " << currAttr->nonres.RealSize << std::endl;
                            //std::wcout << L"DATA NONres stream size: " << currAttr->nonres.StreamSize << std::endl;
                            //std::wcout << L"DATA NONres allocated size: " << currAttr->nonres.AllocatedSize << std::endl;

                            CLST vcn;
                            CLST lcn = 0;
                            vcn = currAttr->nonres.StartVCN;

                            auto runl = ((PBYTE)currAttr + currAttr->nonres.DataRunsOffset);
                            uint8_t b = *runl & 0xf;
                            uint64_t deltaxcn;
                            if (b) 
                            {
                                for (deltaxcn = runl[b--]; b; b--)
                                    deltaxcn = (deltaxcn << 8) + runl[b];
                            }
                            else 
                            { /* The length entry is compulsory. */
                                //ntfs_log_debug("Missing length entry in mapping pairs array.\n");
                                deltaxcn = (int64_t)-1;
                            }
                          
                            RunLenItem ri;
                            ri.len = deltaxcn;
                            vcn += deltaxcn;
                            ri.vcn = vcn;

                            uint8_t b2 = *runl & 0xf;
                            b = b2 + ((*runl >> 4) & 0xf);
                            for (deltaxcn = runl[b--]; b > b2; b--)
                                deltaxcn = (deltaxcn << 8) + runl[b];
                            
                            lcn += deltaxcn;
                            ri.lcn = lcn;


                            break;
                        }
                        case ATTR_ALLOC:
                        { 
                            THArray<RunLenItem> runs;

                            DataRunsDecode(currAttr, runs);

                            uint8_t* dataBuf = new uint8_t[nvdb.BytesPerCluster];
                            uint32_t r = 0;
                            while (r < runs.Count())
                            {
                                uint32_t j = 0;
                                RunLenItem& rli = runs[r];
                                
                                while (j < rli.len)
                                {
                                    if (!ReadCluster(hVolume, rli.lcn + j, nvdb.BytesPerCluster, dataBuf)) //TODO make reading all clusters in data run at once
                                    {
                                        logger.ErrorFmt("ReadCluster finished with error. GetLastError: {}", GetLastError());
                                        break;
                                    }

                                    INDEX_BUFFER* allocIndex = (INDEX_BUFFER*)dataBuf;

                                    // read items only if clu*ter start from correct signature INDX
                                    // sometimes fully empty (filled with zero) clusters present in run list without INDX signature
                                    //if (0 == memcmp(allocIndex->RecHeader.Signature, "INDX", 4))
                                    if (ntfs_is_indx_recp(allocIndex->RecHeader.Signature))
                                    {
                                        logger.DebugFmt("Alloc Attr cluster's VCN: {} LCN: {}", allocIndex->vcn, rli.lcn + j);
                                        //std::wcout << L"Alloc ATTR cluster's VCN: " << allocIndex->vcn << " LCN: " << rli.lcn + j << std::endl;

                                        assert(rli.vcn == allocIndex->vcn);

                                        auto pihdr = &(allocIndex->ihdr);
                                       
                                        GetFileList(pihdr, dirList);
                                    }

                                    j++;
                                }

                                r++;
                            }

                            delete[] dataBuf;
                            break;
                        }

                        default:
                            logger.Warn("UNKNOWN NONresident ATTR has been met.");

                        }
                    }

                    currAttr = (MFT_ATTR_HEADER*)((PBYTE)currAttr + currAttr->AttrSize);

                } 
                while (*((DWORD*)currAttr) != ATTR_END /*0xFFFFFFFF*/);
                
            } while (0 <= (nfrib.FileReferenceNumber.QuadPart = pnfrob->FileReferenceNumber.QuadPart - 1));

            //ReadMft2(szVolume, hVolume, &nvdb);
        }

        CloseHandle(hVolume);
    }
}



int main()
{
    LogEngine::Logger& logger1 = LogEngine::GetFileLogger(MFT_LOGGER_NAME, "MFTReaderLog.log");
    logger1.SetLogLevel(LogEngine::Levels::llDebug);   
    logger1.GetSink(MFT_LOGGER_NAME)->SetLogLevel(LogEngine::Levels::llDebug);
        
    LogEngine::Logger& logger2 = LogEngine::GetFileLogger(MFT_LOGGER_NAME_LIST, "MFTFileist.log");
    logger2.SetLogLevel(LogEngine::Levels::llDebug);
    auto sink = logger2.GetSink(MFT_LOGGER_NAME_LIST);
    sink->SetLogLevel(LogEngine::Levels::llDebug);
    ((LogEngine::PatternLayout*)sink->GetLayout())->SetAllPatterns(MessageMacro);


    logger1.Info("--------------- START --------------");

    PCWSTR str = L"\\\\.\\C:";
    //ReadAllMftRecords(str);

    ReadMft(str);

    logger1.Info("FINISH");


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
}
