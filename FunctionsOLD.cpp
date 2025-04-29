
#include <Windows.h>
#include <cassert>

#include "LogEngine.h"
#include "Functions.h"
#include "string_utils.h"

uint CountBitsTrailingOnes(uint64_t* bitField, uint num)
{
    uint bitCount = 0;

    for (uint i = 0; i < num; i++)
    {
        uint64_t bitInt = *bitField;
        if (bitInt == 0xFFFFFFFFFFFFFFFF) { bitCount += 64; bitField++; continue; }

        while (bitInt & 1)
        {
            bitCount++;
            bitInt >>= 1;
        }

        //assert(bitInt == 0);//TODO it was an assumption that there are no holes in 1 bits sequence. looks like it is wrong....
        bitField++;
    }

    return bitCount;
}

// fills fnames with list of files got from ihdr
// does not go to subnodes
void GetFileList(INDEX_HDR* ihdr, THArray<FILE_NAME>& fnames)
{
    LogEngine::Logger& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);

    uint32_t off = ihdr->DEOffset; // offset of 1st dir entry
    
    while (true) // iterate though all DE+FILE_NAME entries
    {
        assert(off < ihdr->Used);

        NTFS_DE* de = (NTFS_DE*)Add2Ptr(ihdr, off); // NTFS_DE it is a "header" above File Name attribute, covers each file name attribute item

        logger.DebugFmt("DE File rec {:#x}", de->ref.Id);
        logger.DebugFmt("DE size: {}", de->size);
        logger.DebugFmt("DE key_size: {}", de->key_size);

        assert(de->size >= de->key_size + sizeof(NTFS_DE));

        if (de->key_size > 0) // key_size>0 means that filenameattr exists
        {
            ATTR_FILE_NAME* fattr = (ATTR_FILE_NAME*)Add2Ptr(de, sizeof(NTFS_DE));

            assert(de->key_size = sizeof(ATTR_FILE_NAME) + fattr->FileNameLen);

            if (fattr->NameType != FILE_NAME_DOS) // bypass DOS filenames
            {
                ci_string ciwnm(GetFName(fattr, sizeof(ATTR_FILE_NAME)), fattr->FileNameLen);
                if (logger.ShouldLog(LogEngine::Levels::llDebug))
                {
                    std::wstring wnm(GetFName(fattr, sizeof(ATTR_FILE_NAME)), fattr->FileNameLen);
                    std::string nm = wtos(wnm);
                    logger.DebugFmt("DE ATTR Parent rec: {:#x}", fattr->ParentDir.Id);
                    logger.DebugFmt("DE ATTR name: '{}'", nm);
                }

                fnames.AddValue({ ciwnm, *fattr, de->ref });
            }
        }

        off += de->size; // moving to the next DE

        /*if (de->flags & NTFS_IE_HAS_SUBNODES)
        {
            uint64_t vcn = *(uint64_t*)Add2Ptr(ihdr, off - sizeof(uint64_t)); // off refers to next DE here
            logger.DebugFmt("DE has subnodes in VCN={}", vcn);
        }*/

        // check if this is last DE or we have exceeded pihdr->used
        if (((de->flags & NTFS_IE_LAST) > 0) || (de->size < sizeof(NTFS_DE)) || (off > ihdr->Used)) // off refers to next DE here
        {
            break;
        }
    }
}

bool ProcessDataRuns(VOLUME_DATA& volData, DIR_NODE& node)
{
    GET_LOGGER;
    logger.Debug("---------- PROCESSING Data Runs ---------");

    int64_t bitsCounter = 0;
    int64_t lastBit = node.Bitmap.LastBit();

    if (lastBit == -1)
    {
        logger.Debug("BITMAP attr is NULL or not present !!!");
        return true;
    }

    logger.DebugFmt("BITMAP Size in 64bit words: {}, Value64: {:#x}", node.Bitmap.Count(), *(uint64_t*)node.Bitmap.GetData());

    uint8_t* dataBuf = nullptr;
    uint32_t dataBufLen = 0; // memory of how many clusters is allocated in dataBuf
    uint32_t r = 0;
    while (r < node.DataRuns.Count())
    {
        if (bitsCounter > lastBit) // no more valid LCNs 
            break;

        DATA_RUN_ITEM& rli = node.DataRuns[r];
        logger.DebugFmt("Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

        if (rli.len > dataBufLen)
        {
            delete[] dataBuf;
            dataBuf = DBG_NEW uint8_t[rli.len * volData.BytesPerCluster];
            dataBufLen = rli.len;
        }

        //TODO optimization: instead of rli.len we can read min(lastbit+1-bitsCounter, rli.len) clusters
        if (!ReadClusters(volData, rli.lcn, rli.len, dataBuf))
            break;

        if (!FixupUSA(volData, dataBuf, rli))
        {
            logger.Error("FixupUSA returned error.");
            break;
        }

        INDEX_BUFFER* allocIndex = (INDEX_BUFFER*)dataBuf;
        uint8_t* dataBufEnd = dataBuf + rli.len * volData.BytesPerCluster;

        uint32_t cnt = 0;
        while (true) // loop by LCNs in one data run
        {
            if (node.Bitmap.Test(bitsCounter)) // bypass this LCN if bitmap contain zero bit in that position
            {
                // read items only if cluster starts from correct signature INDX
                // sometimes fully empty (filled with zero) clusters present in run list without INDX signature
                if (ntfs_is_indx_recp(allocIndex->RecHeader.Signature))
                {
                    assert(rli.vcn + cnt == allocIndex->vcn);

                    auto pihdr = &(allocIndex->ihdr);
                    GetFileList(pihdr, node.FileList);

                }
                else
                {
                    uint8_t* sign = allocIndex->RecHeader.Signature;
                    logger.WarnFmt("Signature 'INDX' has not been found in LCN cluster {}. Signature found: {}{}{}{}", cnt + rli.lcn, sign[0], sign[1], sign[2], sign[3]);
                }
            }
            else
            {
                logger.DebugFmt("Bitmap bit {}th is zero. LastBit: {}. Bypassing LCN cluster {}.", bitsCounter, lastBit, cnt + rli.lcn);
                if (bitsCounter > lastBit) // no more valid LCNs 
                    break;
            }

            assert(cnt < rli.len);

            allocIndex = (INDEX_BUFFER*)Add2Ptr(allocIndex, volData.BytesPerCluster);
            cnt++; 
            bitsCounter++;

            if ((uint8_t*)allocIndex >= dataBufEnd) break;
        }

        r++;
    }

    logger.Debug("---------- END OF PROCESSING Data Runs ---------");

    delete[] dataBuf;
    return true;
}

// mftRec should be a buffer of volData.BytesPerMFTRec size
// Parses the following attrs: ATTR_ROOT, ATTR_ALLOC, ATTR_BITMAP, ATTR_LIST_ATTR
// ATTR_LIST_ATTR - goes to child records and parses attrs listed above from there
// ATTR_ALLOC - only does data runs decoding, does not go to LCNs. 
bool ParseMFTRecord(VOLUME_DATA& volData, uint8_t* mftRecData, DIR_NODE& node)
{
    LogEngine::Logger& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);

    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecData;

    assert(mftRec->FileRecSize > mftRec->FirstAttrOffset);

    logger.Debug("---------- PARSING MFT RECORD ---------");
    logger.DebugFmt("Signature: {}", (char*)mftRec->RecHeader.Signature);
    logger.DebugFmt("MFT Rec ID: {}", mftRec->IndexMFTRec);
    logger.DebugFmt("Parent MFT Rec ID: {:#x}", mftRec->ParentFileRec.Id);
    logger.DebugFmt("MFT Rec Size: {}", mftRec->FileRecSize);
    logger.DebugFmt("MFT Alloc Rec Size: {}", mftRec->AllocFileRecSize);

    switch (mftRec->Flags)
    {
    case (uint16_t)MFT_RECORD_FLAGS::IN_USE: logger.Debug("MFT Rec type: IN USE (file or anything else)"); break;
    case (uint16_t)MFT_RECORD_FLAGS::IS_DIRECTORY: logger.Debug("MFT Rec type: DIRECTORY"); break;
    case (int16_t)MFT_RECORD_FLAGS::IN_USE | (int16_t)MFT_RECORD_FLAGS::IS_DIRECTORY: logger.DebugFmt("MFT Rec type: IN_USE&DIRECTORY {:#x}", (uint16_t)mftRec->Flags); break;
    default:
        logger.WarnFmt("MFT Rec type: UNKNOWN {:#x}", (uint16_t)mftRec->Flags);
    }

    // usually we call ParseMFTRecord ONLY for directories (for base MFT records).
    // somethimes ParseMFTRecord can be called for special kinds of records e.g. when attributes do not fit into base record and moved into a another "special" record.
    // such special MFT records may be either IN_USE or IN_USE|IS_DIRECTORY type
    // therefore I added if before assert
    if(mftRec->ParentFileRec.Id == 0)
        assert(mftRec->Flags == 0x03); // only "directory" record should go here

    MFT_ATTR_HEADER* currAttr = (MFT_ATTR_HEADER*)Add2Ptr(mftRec, mftRec->FirstAttrOffset);

    int i = 1;
    do  // reading all attributes in a loop
    {
        logger.DebugFmt("****** #{} Attribute ({:#x} {})", i++, (uint32_t)currAttr->AttrType, wtos(AttrTypeNames[MakeAttrTypeIndex(currAttr->AttrType)]));
        logger.DebugFmt("Attr Id: {}", currAttr->AttrID);
        logger.DebugFmt("Attr flags: {}", currAttr->Flags);
        logger.Debug(currAttr->NonResidentFlag ? "Attr type - NONRESIDENT" : "Attr type - RESIDENT");

        if (currAttr->AttrNameSize > 0) // if attr has name - show it
        {
            wchar_t* attrname = GetAName(currAttr, AttrNameOffset); 
            std::wstring name(attrname, currAttr->AttrNameSize);
            logger.DebugFmt("Attr name: '{}'", wtos(name));
        }

        if (currAttr->NonResidentFlag == 0) // attribute is RESident
        {
            logger.DebugFmt("Attr indexed: {}", currAttr->res.IndexedFlag);

            assert(currAttr->res.DataSize + currAttr->res.DataOffset <= currAttr->AttrSize);

            uint8_t* attrValue = Add2Ptr(currAttr, currAttr->res.DataOffset); //(PBYTE)currAttr + currAttr->res.DataOffset;

            switch (currAttr->AttrType)
            {
            case ATTR_ROOT:
            {
                assert(node.FileList.Count() == 0);

                ATTR_INDEX_ROOT* indexR = (ATTR_INDEX_ROOT*)attrValue;
                logger.DebugFmt("Stored attr type: {:#x} ({})", (uint32_t)indexR->AttrType, wtos(AttrTypeNames[MakeAttrTypeIndex(indexR->AttrType)]));
                logger.DebugFmt("Collation rule: {}", (uint32_t)indexR->Rule);
                logger.DebugFmt("Dir type: {} {}", indexR->ihdr.Flags, (indexR->ihdr.Flags == 0 ? " (small dir)" : " (big dir)"));

                auto pihdr = &(indexR->ihdr);

                GetFileList(pihdr, node.FileList);

                break;
            }
            case ATTR_LIST_ATTR:
            {
                logger.Debug("ATTR_LIST_ATTR START");

                assert(node.FileList.Count() == 0);

                THArray<uint32_t> visitedMFTRec;

                ATTR_LIST_ENTRY* attrList = (ATTR_LIST_ENTRY*)attrValue;
                uint8_t* currAttrEnd = (uint8_t*)currAttr + currAttr->AttrSize;

                assert(attrList->StartVCN == 0);

                // at least one attribute is located in another MFT record, so we will need this malloc at least once
                uint8_t* mftRecBuf = (uint8_t*)alloca(volData.BytesPerMFTRec);

                while (true)
                {
                    if (attrList->ref.sId.low != mftRec->IndexMFTRec)
                    {
                        if (visitedMFTRec.IndexOf(attrList->ref.sId.low) == -1) // make sure we parse each record only one
                        {
                            if (LoadMFTRecord(volData, attrList->ref, mftRecBuf)) //TODO shall we call LoadRecordCache here?
                            {
                                if (ParseMFTRecord(volData, mftRecBuf, node))
                                    visitedMFTRec.AddValue(attrList->ref.sId.low);
                            }
                        }
                    }

                    attrList = (ATTR_LIST_ENTRY*)Add2Ptr(attrList, attrList->AttrSize);
                    if ((uint8_t*)attrList >= currAttrEnd) break;
                }

                //free(mftRecBuf);

                logger.Debug("ATTR_LIST_ATTR FINISHED");

                break;
            }
            case ATTR_BITMAP:
            {
                assert(node.Bitmap.Count() == 0);

                ATTR_BITMAP_ATTR* bmp = (ATTR_BITMAP_ATTR*)attrValue;
                logger.DebugFmt("ATTR_BITMAP Size in bytes: {}, Value64: {:#x}", currAttr->res.DataSize, *(uint64_t*)bmp);

                assert((currAttr->res.DataSize & 0x07) == 0); // bitmap data size always multiple of 8

                node.Bitmap.SetData((uint64_t*)bmp->bitmap, currAttr->res.DataSize >> 3);

                break;
            }
            case ATTR_STD_INFO: // resident only
            case ATTR_FILENAME: // resident only
            case ATTR_ID:
            case ATTR_SECURE:
            case ATTR_REPARSE: // can be resident or non-resident
            case ATTR_EA:
            case ATTR_EA_INFO: // can be resident or non-resident
                //case ATTR_PROPERTYSET:
            case ATTR_LOGGED_UTILITY_STREAM: // can be resident or non-resident
            {
                logger.Debug("Do not process this attribute.");
                break;
            }
            case ATTR_DATA: // can be resident or non-resident
            {
                logger.WarnFmt("Resident Data size: {}", currAttr->res.DataSize);
                logger.Warn("Do not process this attribute.");
                break;
            }

            default:
                logger.WarnFmt("UNKNOWN Resident ATTR has been met. Type: {}, MFT Id: {:#x}", wtos(AttrTypeNames[MakeAttrTypeIndex(currAttr->AttrType)]), mftRec->IndexMFTRec);
            } //switch
        }
        else // Attribute is NONresident
        {
            logger.DebugFmt("Attr StartVCN: {}", currAttr->nonres.StartVCN);
            logger.DebugFmt("Attr LastVCN: {}", currAttr->nonres.LastVCN);
            logger.DebugFmt("Attr RealSize: {}", currAttr->nonres.RealSize);
            logger.DebugFmt("Attr StreamSize: {}", currAttr->nonres.StreamSize);
            logger.DebugFmt("Attr AllocatedSize: {}", currAttr->nonres.AllocatedSize);

            switch (currAttr->AttrType)
            {
            case ATTR_BITMAP: // Bitmap can be either resident and non resident
            {
                logger.Warn("BITMAP NON-Resident has been met!");
                if (!ParseNonresBitmap(volData, currAttr, node.Bitmap))
                {
                    logger.Error("ParseNonresBitmap finished with error.");
                }

                break;
            }
            case ATTR_ALLOC:
            {
                // sometimes one ATTR_LIST list may contain two ATTR_ALLOC attributes for some reason
                // it means we come here two times during parsing one MFT record with such ATTR_LIST 
                if (node.DataRuns.Count() > 0)
                    logger.Warn("Multiple ATTR_ALLOC attributes (node.DataRuns.Count() > 0).");

                if (!DataRunsDecode(currAttr, node.DataRuns))
                {
                    logger.Error("DataRunsDecode finished with error.");
                }
                break;
            }
            case ATTR_LIST_ATTR:
            {
                logger.Warn("NonResident ATTR_LIST has been met");
                logger.Debug("nonres ATTR_LIST_ATTR START");

                assert(node.FileList.Count() == 0);

                TDataRuns dataRuns;
                if (!DataRunsDecode(currAttr, dataRuns)) // DataRunsDecode writes a message into log file in case of an error
                {
                    logger.Error("DataRunsDecode finished with error.");
                    break;
                }

                uint8_t* dataBuf = nullptr;

                assert(dataRuns.Count() == 1); //assuming that one LCN is always enough for list of attributes

                DATA_RUN_ITEM& rli = dataRuns[0];
                logger.DebugFmt("Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

                assert(rli.len == 1);

                dataBuf = (uint8_t*)alloca(rli.len * volData.BytesPerCluster);

                if (!ReadClusters(volData, rli.lcn, rli.len, dataBuf)) // ReadClusters writes a message into log file in case of an error
                {
                    logger.Error("ReadClusters finished with error."); 
                    break;
                }

                ATTR_LIST_ENTRY* attrListItem = (ATTR_LIST_ENTRY*)dataBuf;
                uint8_t* dataBufEnd = dataBuf + /*rli.len * */volData.BytesPerCluster;

                while (true) // loop by LCNs in one data run
                {
                    // attributes in attr list located in a separate cluster may refer back to the base record
                    // because part of attributes resides in base mft record and the others in "child" mft record(s)
                    // the attrs list attribute itself is located in cluster that is not mft record, it does not contain signature or Fixups values, etc.
                    if ((attrListItem->ref.Id & MFT_REF_MASK) != mftRec->IndexMFTRec)
                    {
                        if ((attrListItem->AttrType == ATTR_ALLOC) || (attrListItem->AttrType == ATTR_ROOT) || (attrListItem->AttrType == ATTR_BITMAP))
                        {
                            uint8_t* mftRecBuf = LoadMFTRecordCache(volData, attrListItem->ref);
                            assert(mftRecBuf != nullptr);
                            if (mftRecBuf)
                            {
                                if (!ParseMFTRecord(volData, mftRecBuf, node))
                                    logger.Error("ParseMFTRecord finished with error.");
                            }
                            else
                            {
                                logger.Error("LoadMFTRecordCache returned nullptr.");
                            }
                        }
                    }
                    attrListItem = (ATTR_LIST_ENTRY*)Add2Ptr(attrListItem, attrListItem->AttrSize);
                    if ( (attrListItem->AttrType == ATTR_ZERO) || ((uint8_t*)attrListItem >= dataBufEnd)) break;
                }

                logger.Debug("nonres ATTR_LIST_ATTR FINISH");
                break;
            }

            case ATTR_DATA: // can be resident or non-resident
            case ATTR_REPARSE: // can be resident or non-resident
            case ATTR_EA_INFO: // can be resident or non-resident
            case ATTR_LOGGED_UTILITY_STREAM: // can be resident or non-resident
            {
                logger.Debug("Do not process this attribute.");
                break;
            }

            default:
                logger.WarnFmt("UNKNOWN NONResident ATTR has been met. Type: {}, MFT Id: {:#x}", wtos(AttrTypeNames[MakeAttrTypeIndex(currAttr->AttrType)]), mftRec->IndexMFTRec);

            } //switch
        }

        currAttr = (MFT_ATTR_HEADER*)Add2Ptr(currAttr, currAttr->AttrSize);
        assert(mftRec->FileRecSize > Diff2Ptr(mftRec, currAttr));

    } while (*((DWORD*)currAttr) != ATTR_END);

    logger.Debug("---------- END OF PARSING MFT RECORD ---------");

    return true;
}

bool ReadDirectoryX(VOLUME_DATA& volData, MFT_REF mftRecID, uint32_t dirLevel, THArray<FILE_NAME>& gDirList)
{
    if (dirLevel > 30) throw std::runtime_error("dirLevel > 30 !!!!!!!");

    LogEngine::Logger& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);

    uint8_t* mftRecBuf = (uint8_t*)alloca(volData.BytesPerMFTRec);

    if (!LoadMFTRecord(volData, mftRecID, mftRecBuf))
    {
        logger.Error("LoadMFTRecord finished with error.");
        //free(mftRecBuf);
        return false;
    }

    DIR_NODE node;
    if (!ParseMFTRecord(volData, mftRecBuf, node))
    {
        logger.Error("ParseMFTRecord finished with error.");
        //free(mftRecBuf);
        return false;
    }

    //free(mftRecBuf);

    if (node.DataRuns.Count() > 0)
    {
        if (!ProcessDataRuns(volData, node))
        {
            logger.Error("ProcessDataRuns finished with error.");
            //return is not needed here because node.FileList may contain items from INDEX_ROOT and partially from ALLOCATION
            //return false;
        }
    }

    for (auto& item : node.FileList)
    {
        if (!IsMetaFile(item) && !IsDotDir(item.ciName)) // do not add hidden metafiles into file list
        {
            if (dirLevel == 0) std::wcout << item.ciName.c_str() << std::endl;

            gDirList.AddValue(item);

        }

        //if (IsReparse(item.Attr)) { logger.InfoFmt("REPARSE detected: {}", wtos(item.Name)); continue; }

        if (IsDir(item.Attr) && !IsMetaFile(item) && !IsDotDir(item.ciName)) // bypass hidden mft metafiles
            ReadDirectoryX(volData, item.MFTRef, dirLevel + 1, gDirList);
    }

    return true;
}

bool ReadDirectoryOLD(VOLUME_DATA& volData, MFT_REF parentDirRecID, uint32_t dirLevel, THArray<FILE_NAME>& gDirList)
{
    if (dirLevel > 30) throw std::runtime_error("dirLevel > 20 !!!!!!!");

    LogEngine::Logger& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);
    //LogEngine::Logger& loggerlist = LogEngine::GetLogger(MFT_LOGGER_NAME_LIST);

    NTFS_FILE_RECORD_INPUT_BUFFER nfrib;
    nfrib.FileReferenceNumber.QuadPart = parentDirRecID.Id; //136309; // 216632; // 158; //44; // 13569; // 158; 68; 3501 68; 
    //nfrib.FileReferenceNumber.QuadPart = nvdb.MftValidDataLength.QuadPart / nvdb.BytesPerFileRecordSegment - 1;

    ULONG cb = __builtin_offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer[volData.BytesPerMFTRec]);
    //ULONG cb = offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer[volData.BytesPerFileRecordSegment]);

    PNTFS_FILE_RECORD_OUTPUT_BUFFER pnfrob = (PNTFS_FILE_RECORD_OUTPUT_BUFFER)malloc(cb);
    // OVERLAPPED ov = {};

    if (!DeviceIoControl(volData.hVolume, FSCTL_GET_NTFS_FILE_RECORD, &nfrib, sizeof(nfrib), pnfrob, cb, 0, nullptr/*&ov*/))
    {
        logger.ErrorFmt("DeviceIoControl failed with error: {}", GetLastError());
        return false;
    }

    uint8_t* pFileRec = pnfrob->FileRecordBuffer;
    MFT_FILE_RECORD* pmftrec = (MFT_FILE_RECORD*)pFileRec;

    assert(pmftrec->FileRecSize > pmftrec->FirstAttrOffset);

    logger.Debug("---------- NEW MFT RECORD ---------");
    logger.DebugFmt("Signature: {}", (char*)pmftrec->RecHeader.Signature);
    assert(pnfrob->FileReferenceNumber.QuadPart == pmftrec->IndexMFTRec);
    //logger.DebugFmt("MFT Rec NUM: {}", pnfrob->FileReferenceNumber.QuadPart);
    logger.DebugFmt("MFT Rec ID: {}", pmftrec->IndexMFTRec);
    logger.DebugFmt("Parent MFT Rec ID: {:#x}", pmftrec->ParentFileRec.Id);
    //assert(pmftrec->ParentFileRec.Id == 0);
    logger.DebugFmt("MFT Rec Size: {}", pmftrec->FileRecSize);
    logger.DebugFmt("MFT Alloc Rec Size: {}", pmftrec->AllocFileRecSize);

    switch (pmftrec->Flags)
    {
    case (uint16_t)MFT_RECORD_FLAGS::IN_USE: logger.Debug("MFT Rec type: FILE"); break;
    case (uint16_t)MFT_RECORD_FLAGS::IS_DIRECTORY: logger.Debug("MFT Rec type: DIRECTORY"); break;
    case (int16_t)MFT_RECORD_FLAGS::IN_USE | (int16_t)MFT_RECORD_FLAGS::IS_DIRECTORY: logger.DebugFmt("MFT Rec type: FILE_DIRECTORY {:#x}", (uint16_t)pmftrec->Flags); break;
    default:
        logger.DebugFmt("MFT Rec type: UNKNOWN {:#x}", (uint16_t)pmftrec->Flags);
    }
    MFT_ATTR_HEADER* currAttr = (MFT_ATTR_HEADER*)(pFileRec + pmftrec->FirstAttrOffset);
    THArray<FILE_NAME> dirList;

    int i = 1;
    do  // reading all attributes in a loop
    {
        logger.DebugFmt("****** #{} Attribute ({:#x} {})", i++, (uint32_t)currAttr->AttrType, wtos(AttrTypeNames[currAttr->AttrType >> 4]));
        logger.DebugFmt("Attr Id: {}", currAttr->AttrID);
        logger.DebugFmt("Attr flags: {}", currAttr->Flags);
        logger.Debug(currAttr->NonResidentFlag ? "Attr type - NONRESIDENT" : "Attr type - RESIDENT");

        if (currAttr->AttrNameSize > 0) // if attr has name - show it
        {
            wchar_t* attrname = GetAName(currAttr, AttrNameOffset); // (wchar_t*)((uint8_t*)currAttr + currAttr->AttrNameOffset);
            std::wstring name(attrname, currAttr->AttrNameSize);
            logger.DebugFmt("Attr name: '{}'", wtos(name));
        }

        if (currAttr->NonResidentFlag == 0) // attribute is RESident
        {
            logger.DebugFmt("Attr indexed: {}", currAttr->res.IndexedFlag);

            uint8_t* attrValue = (PBYTE)currAttr + currAttr->res.DataOffset;

            assert(currAttr->res.DataSize + currAttr->res.DataOffset <= currAttr->AttrSize);

            // DWORD dateTimeFlags = FDTF_DEFAULT | FDTF_NOAUTOREADINGORDER;

            switch (currAttr->AttrType)
            {
            case ATTR_STD_INFO:
            {
                /*ATTR_STD_INFO5* stdinfo = (ATTR_STD_INFO5*)attrValue;
                wchar_t buf[100];
                FILETIME ft;

                ft.dwLowDateTime = LODWORD(stdinfo->CreateTime);
                ft.dwHighDateTime = HIDWORD(stdinfo->CreateTime);
                SHFormatDateTime(&ft, &dateTimeFlags, buf, 100);
                logger.TraceFmt("Created: {}", wtos(buf));

                ft.dwLowDateTime = LODWORD(stdinfo->ModifyTime);
                ft.dwHighDateTime = HIDWORD(stdinfo->ModifyTime);
                SHFormatDateTime(&ft, &dateTimeFlags, buf, 100);
                logger.TraceFmt("Modified: {}", wtos(buf));

                ft.dwLowDateTime = LODWORD(stdinfo->LastAccessTime);
                ft.dwHighDateTime = HIDWORD(stdinfo->LastAccessTime);
                SHFormatDateTime(&ft, &dateTimeFlags, buf, 100);
                logger.TraceFmt("Last access: {}", wtos(buf));
                */
                //std::wcout << L"Version number: " << stdinfo->VersionNum << std::endl;
                //std::wcout << L"Max version num: " << stdinfo->max_ver_num << std::endl;
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
                    logger.DebugFmt("File DOS attrib : {:#x}", fname->dup.FileAttrib);
                    logger.DebugFmt("File name: '{}'", wtos(name));

                    /*wchar_t buf[100];
                    FILETIME ft;

                    ft.dwLowDateTime = LODWORD(fname->dup.cr_time);
                    ft.dwHighDateTime = HIDWORD(fname->dup.cr_time);
                    SHFormatDateTime(&ft, &dateTimeFlags, buf, 100);
                    logger.TraceFmt("Created: {}", wtos(buf));

                    ft.dwLowDateTime = LODWORD(fname->dup.m_time);
                    ft.dwHighDateTime = HIDWORD(fname->dup.m_time);
                    SHFormatDateTime(&ft, &dateTimeFlags, buf, 100);
                    logger.TraceFmt("Modified: {}", wtos(buf));

                    ft.dwLowDateTime = LODWORD(fname->dup.a_time);
                    ft.dwHighDateTime = HIDWORD(fname->dup.a_time);
                    SHFormatDateTime(&ft, &dateTimeFlags, buf, 100);
                    logger.TraceFmt("Last access: {}", wtos(buf));

                    //ft.dwLowDateTime = LODWORD(fname->dup.c_time);
                    //ft.dwHighDateTime = HIDWORD(fname->dup.c_time);
                    //SHFormatDateTime(&ft, &dateTimeFlags, buf, 100);
                    //std::wcout << L"Attrib modification time: " << buf << std::endl;
                    */
                }
                break;
            }
            case ATTR_ID:
            {
                /*
                OBJECT_ID* objID = (OBJECT_ID*)attrValue;

                wchar_t buf[100];
                if (!StringFromGUID2(objID->ObjId, buf, 100))
                    logger.Error("ATTR_ID Error in StringFromGUID2.");

                logger.DebugFmt("Object ID: {}", wtos(buf));
                */
                break;
            }
            case ATTR_ROOT:
            {
                ATTR_INDEX_ROOT* indexR = (ATTR_INDEX_ROOT*)attrValue;
                logger.DebugFmt("Stored attr type: {:#x} ({})", (uint32_t)indexR->AttrType, wtos(AttrTypeNames[indexR->AttrType >> 4]));
                logger.DebugFmt("Collation rule: {}", (uint32_t)indexR->Rule);
                logger.DebugFmt("Dir type: {} {}", indexR->ihdr.Flags, (indexR->ihdr.Flags == 0 ? " (small dir)" : " (big dir)"));

                auto pihdr = &(indexR->ihdr);

                GetFileList(pihdr, dirList);

                break;
            }
            case ATTR_LIST_ATTR:
            {
                logger.Info("ATTR_LIST_ATTR is met. START");

                THArray<uint32_t> visitedMFTRec;

                ATTR_LIST_ENTRY* attrEntry = (ATTR_LIST_ENTRY*)attrValue;
                assert(attrEntry->StartVCN == 0);

                while (true)
                {
                    if (attrEntry->ref.sId.low != pmftrec->IndexMFTRec)
                    {
                        if (visitedMFTRec.IndexOf(attrEntry->ref.sId.low) == -1)
                        {
                            ReadDirectoryOLD(volData, attrEntry->ref, dirLevel, gDirList);
                            visitedMFTRec.AddValue(attrEntry->ref.sId.low);
                        }
                    }

                    if ((uint8_t*)attrEntry + attrEntry->AttrSize - (uint8_t*)currAttr >= currAttr->AttrSize) break;
                    attrEntry = (ATTR_LIST_ENTRY*)Add2Ptr(attrEntry, attrEntry->AttrSize);
                }

                logger.Info("ATTR_LIST_ATTR FINISHED");

                break;
            }
            case ATTR_BITMAP:
            {
                //ATTR_BITMAP_ATTR* bmp = (ATTR_BITMAP_ATTR*)attrValue;
                //logger.InfoFmt("ATTR_BITMAP Size in bytes: {}, Value8: {:#x}, Value16: {:#x}, Value32: {:#x}, Value64: {:#x}", currAttr->res.DataSize, *(uint8_t*)bmp, *(uint16_t*)bmp, *(uint32_t*)bmp, *(uint64_t*)bmp);

                assert((currAttr->res.DataSize & 0x07) == 0); // bitmap data size always multiple of 8

                //CountBitsTrailingOnes((uint64_t*)bmp->bitmap, currAttr->res.DataSize >> 3);

                /*ATTR_FILE_NAME* attrFN = GetAttrValue<ATTR_FILENAME>(pmftrec);
                auto attrBMP = GetAttrValue<ATTR_BITMAP>(pmftrec);
                auto attrAL = GetAttrValue<ATTR_ALLOC>(pmftrec);
                auto attrRO = GetAttrValue<ATTR_ROOT>(pmftrec);
                ATTR_STD_INFO5* attrSTD = GetAttrValue<ATTR_STD_INFO>(pmftrec);*/
                break;
            }
            case ATTR_ALLOC:
            case ATTR_SECURE:
            case ATTR_REPARSE:
            case ATTR_EA:
            case ATTR_EA_INFO:
            case ATTR_PROPERTYSET:
            case ATTR_LOGGED_UTILITY_STREAM:
            {
                logger.Debug("Do not process this attribute.");
                break;
            }
            case ATTR_DATA:
            {
                logger.DebugFmt("Resident Data size: {}", currAttr->res.DataSize);
                //std::wcout << L"ATTR DATA stream size: " << currAttr->res.DataSize << std::endl;
                //auto rtree = (RunsTree*)((PBYTE)currAttr + currAttr->nonres.DataRunsOffset);
                break;
            }

            default:
                logger.Warn("UNKNOWN Resident ATTR has been met");

            }

        }
        else // Attribute is NONresident
        {
            logger.DebugFmt("Attr StartVCN: {}", currAttr->nonres.StartVCN);
            logger.DebugFmt("Attr LastVCN: {}", currAttr->nonres.LastVCN);
            logger.DebugFmt("Attr RealSize: {}", currAttr->nonres.RealSize);
            logger.DebugFmt("Attr StreamSize: {}", currAttr->nonres.StreamSize);
            logger.DebugFmt("Attr AllocatedSize: {}", currAttr->nonres.AllocatedSize);

            switch (currAttr->AttrType)
            {
            case ATTR_DATA:
            {
                logger.Debug("Do not process this attribute.");
                break;
            }
            case ATTR_ALLOC:
            {
                TDataRuns runs;

                if (!DataRunsDecode(currAttr, runs))
                {
                    logger.Error("DataRunsDecode finished with error.");
                }

                uint validLcnCnt = 0;
                PMFT_ATTR_HEADER attrValues[ATTR_TYPE_CNT];
                FillAttrValues(pmftrec, attrValues);
                PMFT_ATTR_HEADER bmpAttr = attrValues[MakeAttrTypeIndex(ATTR_BITMAP)];
                if (bmpAttr) //TODO add proper handling bitmap attribute for LIST_ATTR attribute type
                {
                    ATTR_BITMAP_ATTR* bmp = (ATTR_BITMAP_ATTR*)Add2Ptr(bmpAttr, bmpAttr->res.DataOffset);
                    logger.InfoFmt("BITMAP Size in bytes: {}, Value64: {:#x}", bmpAttr->res.DataSize, *(uint64_t*)bmp->bitmap);
                    assert((bmpAttr->res.DataSize & 0x07) == 0);
                    validLcnCnt = CountBitsTrailingOnes((uint64_t*)bmp->bitmap, bmpAttr->res.DataSize >> 3);
                }
                else
                {
                    logger.Info("BITMAP attr IS NULL !!!");
                }

                uint runLenLen = 0;
                for (auto& rl : runs) runLenLen += rl.len; // calc total number of clusters in all data runs

                //assert(validLcnCnt > 0);
                assert(validLcnCnt <= runLenLen);

                //if (validLcnCnt < rli.len)
                //    logger.InfoFmt("BITMAP Attribute works! DataRun LCN Len: {}, LCN Len from BITMAP:{}", rli.len, validLcnCnt);

                uint8_t* dataBuf{ 0 };
                uint32_t dataBufLen = 0; // size of allocated memory in clusters
                uint32_t r = 0;
                while (r < runs.Count())
                {
                    DATA_RUN_ITEM& rli = runs[r];
                    logger.InfoFmt("Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

                    //rli.len = validLcnCnt;

                    if (rli.len > dataBufLen)
                    {
                        if (dataBufLen > 0) delete[] dataBuf;
                        dataBuf = DBG_NEW uint8_t[rli.len * volData.BytesPerCluster];
                        dataBufLen = rli.len;
                    }

                    if (!ReadClusters(volData, rli.lcn, rli.len, dataBuf))
                    {
                        logger.ErrorFmt("ReadCluster finished with error. GetLastError: {}", GetLastError());
                        break;
                    }

                    if (!FixupUSA(volData, dataBuf, rli))
                    {
                        logger.Error("FixupUSA returned error.");
                        break;
                    }

                    INDEX_BUFFER* allocIndex = (INDEX_BUFFER*)dataBuf;
                    uint8_t* dataBufEnd = dataBuf + rli.len * volData.BytesPerCluster;

                    uint32_t cnt = 0;
                    while (true)
                    {
                        // read items only if cluster starts from correct signature INDX
                        // sometimes fully empty (filled with zero) clusters present in run list without INDX signature
                        if (ntfs_is_indx_recp(allocIndex->RecHeader.Signature))
                        {
                            //logger.DebugFmt("Alloc Attr cluster's VCN: {} LCN: {}", allocIndex->vcn, clst);
                            //std::wcout << L"Alloc ATTR cluster's VCN: " << allocIndex->vcn << " LCN: " << clst << std::endl;

                            assert(rli.vcn + cnt == allocIndex->vcn);

                            auto pihdr = &(allocIndex->ihdr);
                            GetFileList(pihdr, dirList);

                        }
                        else
                        {
                            uint8_t* sign = allocIndex->RecHeader.Signature;
                            logger.WarnFmt("Signature 'INDX' has not been found in LCN cluster {}. Signature found: {}{}{}{}", cnt + rli.lcn, sign[0], sign[1], sign[2], sign[3]);
                        }

                        allocIndex = (INDEX_BUFFER*)Add2Ptr(allocIndex, volData.BytesPerCluster);
                        cnt++;

                        if ((uint8_t*)allocIndex >= dataBufEnd) break;
                    }

                    r++;
                }

                delete[] dataBuf;
                break;
            }

            default:
                logger.Warn("UNKNOWN NONresident ATTR has been met.");

            }//switch
        }

        currAttr = (MFT_ATTR_HEADER*)Add2Ptr(currAttr, currAttr->AttrSize);
        assert(pmftrec->FileRecSize > Diff2Ptr(pmftrec, currAttr));

    } while (*((DWORD*)currAttr) != ATTR_END /*0xFFFFFFFF*/);

    free(pnfrob);

    for (auto& item : dirList)
    {
        if (!IsMetaFile(item)) // do not add hidden metafiles into file list
        {
            gDirList.AddValue(item);

            if (IsDir(item.Attr))
            {
                //*ffff << item.Name;
                //ffff->Write(item.Name.data(), item.Name.length() * sizeof(item.Name[0]));
                //ffff->Write(L"\r\n", 4);
            }
            else
            {
                //*ffff << item.Name;
                //ffff->Write(item.Name.data(), item.Name.length() * sizeof(item.Name[0]));
                //ffff->Write(L"\r\n", 4);
            }
        }

        //if (item.Name == L".") continue;

        //if (dirLevel == 0 && item.Name != L"temp" && item.Name != L"SourceForge") continue;
        //if (dirLevel == 0 && item.Name != L"SourceForge") continue;
        //if (dirLevel == 1 && item.Name != L"cppunit-1.11.6.ren") continue;

        // item.Attr.ParentDir actually is ref to item's MFT Id, not to the parent dir
        if (IsDir(item.Attr) && !IsMetaFile(item) && !IsDotDir(item.ciName)) // bypass hidden mft metafiles
            ReadDirectoryOLD(volData, item.Attr.ParentDir, dirLevel + 1, gDirList);
    }

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
