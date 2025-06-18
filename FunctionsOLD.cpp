
// this is to remove defines min, max in windows headers because they conflict with std::min std::max 
#define NOMINMAX

#include <Windows.h>
#include <cassert>

#include "LogEngine.h"
#include "Functions.h"
#include "string_utils.h"


// fills fnames with list of files got from ihdr
// does NOT go to subnodes
void GetFileList(INDEX_HDR* ihdr, TFileList& fnames)
{
    GET_LOGGER;

    uint32_t off = ihdr->DEOffset; // offset of 1st dir entry
    
    while (true) // iterate though all DE+FILE_NAME entries
    {
        assert(off < ihdr->Used);

        NTFS_DE* de = (NTFS_DE*)Add2Ptr(ihdr, off); // NTFS_DE it is a "header" above File Name attribute, covers each file name attribute item

        logger.DebugFmt("DE File rec {:#x}", de->ref.Id);
        logger.DebugFmt("DE size: {}", de->size);
        logger.DebugFmt("DE key_size: {}", de->key_size);
        logger.DebugFmt("DE flags: {}", de->flags);

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
                    logger.DebugFmt("DE ATTR Parent rec: {:#x}", fattr->ParentDir.Id);
                    logger.DebugFmt("DE ATTR name: '{}'", wtos(ciwnm));
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
        if (((de->flags & NTFS_IE_LAST) > 0) || (de->size < sizeof(NTFS_DE)) || (off >= ihdr->Used)) // off refers to next DE here
        {
            break;
        }
    }
}

bool ProcessAllocDataRuns(VOLUME_DATA& volData, DIR_NODE& node)
{
    GET_LOGGER;
    logger.Debug("---------- PROCESSING Alloc Attr Data Runs ---------");

    int64_t lastBit = node.Bitmap.LastBit();

    if (lastBit == -1)
    {
        logger.Debug("[ProcessAllocDataRuns] BITMAP attr is NULL or not present!");
        logger.Debug("---------- END OF PROCESSING Alloc Attr Data Runs ---------");
        return true;
    }

    logger.DebugFmt("BITMAP Size in 64bit words: {}, Value64: {:#x}", node.Bitmap.Count(), *(uint64_t*)node.Bitmap.GetData());

    int64_t bitsCounter = 0;
    uint8_t* dataBuf = nullptr;
    uint32_t dataBufLen = 0; // memory of how many clusters is allocated in dataBuf
    uint32_t currRun = 0;
    while (currRun < node.DataRuns.Count())
    {
        if (bitsCounter > lastBit) // no more valid LCNs, break loop 
            break;

        DATA_RUN_ITEM& rli = node.DataRuns[currRun];
        logger.DebugFmt("Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

        uint32_t rlilen = valuemin((uint32_t)(lastBit + 1 - bitsCounter), rli.len);

        if (rlilen > dataBufLen)
        {
            delete[] dataBuf;
            dataBuf = DBG_NEW uint8_t[rlilen * volData.BytesPerCluster];
            dataBufLen = rlilen;
        }

        if (!ReadClusters(volData, rli.lcn, rlilen, dataBuf))
            break;

        if (!FixupUSA(volData, dataBuf, rli, rlilen))
        {
            logger.Error("FixupUSA returned error.");
            break;
        }

        INDEX_BUFFER* allocIndex = (INDEX_BUFFER*)dataBuf;
        uint8_t* dataBufEnd = dataBuf + rlilen * volData.BytesPerCluster;

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
                    logger.WarnFmt("Signature 'INDX' has not been found in LCN cluster {}. Signature found: {}{}{}{}", rli.lcn + cnt, sign[0], sign[1], sign[2], sign[3]);
                }
            }
            else
            {
                logger.DebugFmt("Bitmap bit {}th is zero. LastBit: {}. Bypassing LCN cluster {}.", bitsCounter, lastBit, rli.lcn + cnt);
                if (bitsCounter > lastBit) // no more valid LCNs 
                    break;
            }

            assert(cnt < rlilen);

            // go to the next LCN
            allocIndex = (INDEX_BUFFER*)Add2Ptr(allocIndex, volData.BytesPerCluster);
            cnt++; 
            bitsCounter++;

            if ((uint8_t*)allocIndex >= dataBufEnd) break;
        }

        currRun++;
    }

    delete[] dataBuf;

    logger.Debug("---------- END OF PROCESSING Data Runs ---------");

    return true;
}

// mftRec should be a buffer of volData.BytesPerMFTRec size
// Parses the following attrs: ATTR_ROOT, ATTR_ALLOC, ATTR_BITMAP, ATTR_LIST_ATTR
// ATTR_LIST_ATTR - goes to child records and parses attrs listed above from there
// ATTR_ALLOC - only does data runs decoding, does not go to LCNs. 
bool ParseMFTRecord(VOLUME_DATA& volData, uint8_t* mftRecData, DIR_NODE& node)
{
    GET_LOGGER;

    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecData;

    assert(mftRec->FileRecSize > mftRec->FirstAttrOffset);
    
    logger.Debug("---------- PARSING MFT RECORD ---------");
    logger.DebugFmt("Signature: {}", (char*)mftRec->RecHeader.Signature);
    logger.DebugFmt("MFT Rec ID: {}", mftRec->IndexMFTRec);
    logger.DebugFmt("Parent MFT Rec ID: {:#x}", mftRec->ParentFileRec.Id);
    logger.DebugFmt("MFT Hard links cnt: {}", mftRec->HardLinksCnt);
    logger.DebugFmt("MFT Rec Size: {}", mftRec->FileRecSize);
    logger.DebugFmt("MFT Alloc Rec Size: {}", mftRec->AllocFileRecSize);

    switch (mftRec->Flags)
    {
    case (uint16_t)MFT_RECORD_FLAGS::IN_USE: logger.Debug("MFT Rec type: IN USE (file or anything else)"); break;
    case (uint16_t)MFT_RECORD_FLAGS::IS_DIRECTORY: logger.Debug("MFT Rec type: DIRECTORY"); break;
    case (int16_t)MFT_RECORD_FLAGS::IN_USE | (int16_t)MFT_RECORD_FLAGS::IS_DIRECTORY: logger.DebugFmt("MFT Rec type: IN_USE & DIRECTORY {:#x}", (uint16_t)mftRec->Flags); break;
    default:
        logger.WarnFmt("MFT Rec type: UNKNOWN {:#x}", (uint16_t)mftRec->Flags);
    }

    // usually we call ParseMFTRecord ONLY for directories (for base MFT records).
    // somethimes ParseMFTRecord can be called for special kinds of records e.g. when attributes do not fit into base record and moved into a another "special" record.
    // such special MFT records may be either IN_USE or IN_USE|IS_DIRECTORY type
    // therefore I added if before assert
    if(mftRec->ParentFileRec.Id == 0)
        assert(mftRec->Flags == 0x03); // only "directory" record should go here for base records

    MFT_ATTR_HEADER* currAttr = (MFT_ATTR_HEADER*)Add2Ptr(mftRec, mftRec->FirstAttrOffset);

    int attroOrderNum = 1;
    do  // reading all attributes in a loop
    {
        logger.DebugFmt("****** #{} Attribute ({:#x} {})", attroOrderNum++, (uint32_t)currAttr->AttrType, AttrName(currAttr->AttrType));
        logger.Debug(currAttr->NonResidentFlag ? "Attr type - NONRESIDENT" : "Attr type - RESIDENT");
        logger.DebugFmt("Attr Id: {}", currAttr->AttrID);
        logger.DebugFmt("Attr flags: {}", currAttr->Flags);
    
        if (currAttr->AttrNameSize > 0) // if attr has name - show it
        {
            wchar_t* attrname = GetAttrName(currAttr, AttrNameOffset); 
            std::wstring name(attrname, currAttr->AttrNameSize);
            logger.DebugFmt("Attr name: '{}'", wtos(name));
        }

        if (currAttr->NonResidentFlag == 0) // attribute is RESident
        {
            logger.DebugFmt("Attr indexed: {}", currAttr->res.IndexedFlag);

            assert(currAttr->res.DataSize + currAttr->res.DataOffset <= currAttr->AttrSize);

            uint8_t* attrValue = Add2Ptr(currAttr, currAttr->res.DataOffset); 

            switch (currAttr->AttrType)
            {
            case ATTR_ROOT: // resident. ATTR_ROOT is always resident
            {
                assert(node.FileList.Count() == 0);

                ATTR_INDEX_ROOT* indexR = (ATTR_INDEX_ROOT*)attrValue;
                logger.DebugFmt("IndexRoot attr type: {:#x} ({})", (uint32_t)indexR->AttrType, AttrName(indexR->AttrType));
                logger.DebugFmt("Collation rule: {}", (uint32_t)indexR->Rule);
                logger.DebugFmt("Dir type: {} {}", indexR->ihdr.Flags, (indexR->ihdr.Flags == 0 ? " (small dir)" : " (big dir)"));

                auto pihdr = &(indexR->ihdr);

                GetFileList(pihdr, node.FileList); //does not go to subnodes

                break;
            }
            case ATTR_LIST_ATTR: // resident. ATTR_LIST_ATTR can be either resident or non-resident
            {
                logger.Debug("resident ATTR_LIST_ATTR START");

                assert(node.FileList.Count() == 0);

                THArray<uint32_t> visitedMFTRec;

                ATTR_LIST_ENTRY* attrEntry = (ATTR_LIST_ENTRY*)attrValue;
                uint8_t* currAttrEnd = Add2Ptr(currAttr, currAttr->AttrSize);
                assert(attrEntry->AttrType > 0);
                assert(attrEntry->AttrSize > 0);
                assert(attrEntry->StartVCN == 0);
                assert(((uint32_t)(attrEntry->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero

                // at least one attribute is located in another MFT record, so we will need this malloc at least once
                uint8_t* mftRecBuf = (uint8_t*)alloca(volData.BytesPerMFTRec);

                while (true)
                {
                    if (attrEntry->ref.sId.low != mftRec->IndexMFTRec)
                    {
                        if (visitedMFTRec.IndexOf(attrEntry->ref.sId.low) == -1) // make sure we parse each record only once
                        {
                            if (LoadMFTRecord(volData, attrEntry->ref, mftRecBuf)) //TODO shall we call LoadRecordCache here?
                            {
                                if (ParseMFTRecord(volData, mftRecBuf, node)) //TODO shall we break in case of an error in LoadMFTRecord or ParseMFTRecord 
                                    visitedMFTRec.AddValue(attrEntry->ref.sId.low);
                            }
                        }
                    }

                    attrEntry = (ATTR_LIST_ENTRY*)Add2Ptr(attrEntry, attrEntry->AttrSize);
                    //if (attrEntry->AttrType == ATTR_ZERO) break;
                    if ((uint8_t*)attrEntry >= currAttrEnd) break;
                    assert(attrEntry->AttrType > 0);
                    assert(attrEntry->AttrSize > 0);
                    assert(attrEntry->StartVCN == 0);
                    assert(((uint32_t)(attrEntry->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
                }

                logger.Debug("resudent ATTR_LIST_ATTR FINISHED");

                break;
            }
            case ATTR_BITMAP: //resident. Bitmap can be resident or non-resident
            {
                assert(node.Bitmap.Count() == 0);

                ATTR_BITMAP_ATTR* bmp = (ATTR_BITMAP_ATTR*)attrValue;
                logger.DebugFmt("ATTR_BITMAP Size in bytes: {}, Value64: {:#x}", currAttr->res.DataSize, *(uint64_t*)bmp);

                assert((currAttr->res.DataSize & 0x07) == 0); // bitmap data size always multiple of 8

                node.Bitmap.SetData((uint64_t*)bmp->bitmap, currAttr->res.DataSize >> 3);

                break;
            }
            case ATTR_DATA: //resident. ATTR_DATA can be resident or non-resident
            {
                logger.WarnFmt("Resident Data Attr. Size: {}", currAttr->res.DataSize);
                logger.Warn("Do not process this attribute.");
                break;
            }
            case ATTR_STD_INFO: // resident only
            case ATTR_FILENAME: // resident only
            case ATTR_ID:
            case ATTR_SECURE:
            case ATTR_REPARSE: // can be resident or non-resident
            case ATTR_EA:      // can be resident or non-resident  
            case ATTR_EA_INFO: // can be resident or non-resident
            //case ATTR_PROPERTYSET:
            case ATTR_LOGGED_UTILITY_STREAM: // can be resident or non-resident
            {
                logger.DebugFmt("Attr: {}. We do not process this attribute.", AttrName(currAttr->AttrType));
                break;
            }
            
            default:
                logger.WarnFmt("UNKNOWN Resident ATTR has been met. Type: {0}, MFT Id: {1:#x} ({1})", AttrName(currAttr->AttrType), mftRec->IndexMFTRec);
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
            case ATTR_BITMAP: //non-resident. ATTR_BITMAP can be either resident and non resident
            {
                logger.Warn("BITMAP NON-Resident has been met!");
                if (!ParseNonresBitmap(volData, currAttr, node.Bitmap))
                {
                    logger.Error("ParseNonresBitmap finished with error.");
                }

                break;
            }
            case ATTR_ALLOC: //non-resident. ATTR_ALLOC is always non-resident.
            {
                // sometimes one ATTR_LIST_ATTR list may contain two ATTR_ALLOC attributes for some reason
                // it means we come here two times during parsing one MFT record with such ATTR_LIST_ATTR 
                if (node.DataRuns.Count() > 0)
                    logger.WarnFmt("Multiple ATTR_ALLOC attributes (node.DataRuns.Count() == {}).", node.DataRuns.Count());

                if (!DataRunsDecode(currAttr, node.DataRuns))
                {
                    logger.Error("DataRunsDecode finished with error.");
                }

                // we do not process ALLOC data runs here because in this place we do not know all requred info, for example Bitmap attribute.
                // that is why data runs processing is done in calling function.
                break;
            }
            case ATTR_LIST_ATTR: // non-resident. ATTR_LIST_ATTR can be either resident and non resident
            {
                logger.Warn("NonResident ATTR_LIST has been met");
                logger.Debug("nonres ATTR_LIST_ATTR START");

                assert(node.FileList.Count() == 0);

                TDataRuns dataRuns;
                if (!DataRunsDecode(currAttr, dataRuns)) // DataRunsDecode writes a message into log file in case of an error
                {
                    break;
                }

                uint8_t* dataBuf = nullptr;

                assert(dataRuns.Count() == 1); //assuming that one LCN is always enough for list of attributes

                DATA_RUN_ITEM& rli = dataRuns[0];
                logger.DebugFmt("Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

                assert(rli.len == 1);

                uint dataBufSize = rli.len * volData.BytesPerCluster;
                dataBuf = (uint8_t*)alloca(dataBufSize);

                if (!ReadClusters(volData, rli.lcn, rli.len, dataBuf)) // ReadClusters writes a message into log file in case of an error
                {
                    logger.Error("ReadClusters finished with error."); 
                    break;
                }

                ATTR_LIST_ENTRY* attrListItem = (ATTR_LIST_ENTRY*)dataBuf;
                uint8_t* attrListEnd1 = dataBuf + currAttr->nonres.RealSize;
                uint8_t* attrListEnd2 = dataBuf + dataBufSize;
                
                assert(attrListItem->AttrType > 0);
                assert(attrListItem->AttrSize > 0);
                assert(((uint32_t)(attrListItem->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero

                THArray<uint32_t> visitedMFTRec;

                while (true) // loop by LCNs in one data run
                {
                    // attributes in attr list located in a separate cluster may refer back to the base record
                    // because some attributes reside in base mft record and the others in "child" mft record(s)
                    // the attr list attribute itself is located in cluster that is not mft record, it does not contain signature or Fixups values, etc.
                    if ((attrListItem->ref.sId.low) != mftRec->IndexMFTRec)
                    {
                        if ((attrListItem->AttrType == ATTR_ALLOC) || (attrListItem->AttrType == ATTR_ROOT) || (attrListItem->AttrType == ATTR_BITMAP))
                        {
                            if (visitedMFTRec.IndexOf(attrListItem->ref.sId.low) == -1) // make sure we parse each record only once
                            {
                                uint8_t* mftRecBuf = LoadMFTRecordCache(volData, attrListItem->ref);
                                assert(mftRecBuf != nullptr);
                                if (mftRecBuf)
                                {
                                    //TODO There may be a case when 2 attributes located in a one child MFT record. They will be parsed twice now. Think of solution for it. 
                                    if (!ParseMFTRecord(volData, mftRecBuf, node))
                                        logger.Error("ParseMFTRecord finished with error.");
                                }
                                else
                                {
                                    logger.Error("LoadMFTRecordCache returned nullptr.");
                                }

                                visitedMFTRec.AddValue(attrListItem->ref.sId.low);
                            }
                        }
                    }

                    attrListItem = (ATTR_LIST_ENTRY*)Add2Ptr(attrListItem, attrListItem->AttrSize);
                    if ((uint8_t*)attrListItem >= attrListEnd1) break;
                    if ((uint8_t*)attrListItem >= attrListEnd2) break;
                    //if (attrEntry->AttrType == ATTR_ZERO) break;
                    //assert(attrEntry->AttrId > 0);
                    assert(attrListItem->AttrType > 0);
                    assert(((uint32_t)(attrListItem->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
                    assert(attrListItem->AttrSize > 0);

                    //attrListItem = (ATTR_LIST_ENTRY*)Add2Ptr(attrListItem, attrListItem->AttrSize);
                    //if ( (attrListItem->AttrType == ATTR_ZERO) || ((uint8_t*)attrListItem >= dataBufEnd)) break;
                    //assert(attrListItem->AttrSize > 0);
                }

                logger.Debug("nonres ATTR_LIST_ATTR FINISH");
                break;
            }

            case ATTR_DATA:    // can be resident or non-resident
            case ATTR_REPARSE: // can be resident or non-resident
            case ATTR_EA_INFO: // can be resident or non-resident
            case ATTR_LOGGED_UTILITY_STREAM: // can be resident or non-resident
            {
                logger.Debug("Do not process this attribute.");
                break;
            }

            default:
                logger.WarnFmt("UNKNOWN NONResident ATTR has been met. Type: {0}, MFT Id: {1:#x} ({1})", AttrName(currAttr->AttrType), mftRec->IndexMFTRec);

            } //switch
        }

        currAttr = (MFT_ATTR_HEADER*)Add2Ptr(currAttr, currAttr->AttrSize);
        assert(mftRec->FileRecSize > Diff2Ptr(mftRec, currAttr));

    } while (*((DWORD*)currAttr) != ATTR_END);

    logger.Debug("---------- END OF PARSING MFT RECORD ---------");

    return true;
}

bool ReadDirectoryV1(VOLUME_DATA& volData, MFT_REF mftRecID, uint32_t dirLevel, TFileList& gDirList)
{
    if (dirLevel > 30) throw std::runtime_error("dirLevel > 30 !!!!!!!");

    GET_LOGGER;

    uint8_t* mftRecBuf = (uint8_t*)alloca(volData.BytesPerMFTRec);

    if (!LoadMFTRecord(volData, mftRecID, mftRecBuf))
    {
        logger.Error("LoadMFTRecord finished with error.");
        return false;
    }

    DIR_NODE node;
    if (!ParseMFTRecord(volData, mftRecBuf, node))
    {
        logger.Error("ParseMFTRecord finished with error.");
        return false;
    }

    if (node.DataRuns.Count() > 0)
    {
        if (!ProcessAllocDataRuns(volData, node))
        {
            logger.Error("ProcessAllocDataRuns finished with error.");
            //return is not needed here because node.FileList may contain items from INDEX_ROOT and partially from ALLOCATION
            //return false;
        }
    }

    for (auto& item : node.FileList)
    {
        if (!IsMetaFile(item) && !IsDotDir(item.ciName)) // do not add hidden metafiles into file list
        {
            // print to console only dirs of first and second levels.
            if (dirLevel == 0) std::wcout << item.ciName.c_str() << std::endl;
            //if (dirLevel == 1) std::wcout << "\t" << item.ciName.c_str() << std::endl;

            gDirList.AddValue(item);
        }

        //if (IsReparse(item.Attr)) { logger.InfoFmt("REPARSE detected: {}", wtos(item.Name)); continue; }

        if (IsDir(item.Attr) && !IsMetaFile(item) && !IsDotDir(item.ciName)) // bypass hidden mft metafiles
            ReadDirectoryV1(volData, item.MFTRef, dirLevel + 1, gDirList);
    }

    return true;
}

TItemInfoList gItemsList;

bool ReadMftItemInfo(VOLUME_DATA& volData, MFT_REF parentDirRecID, uint32_t dirLevel, ITEM_INFO& itemInfo)
{
    if (dirLevel > 60) throw std::runtime_error("dirLevel > 30 !!!!!!!");

    GET_LOGGER;

    NTFS_FILE_RECORD_INPUT_BUFFER nfrib;
    nfrib.FileReferenceNumber.QuadPart = parentDirRecID.sId.low; 
    //nfrib.FileReferenceNumber.QuadPart = nvdb.MftValidDataLength.QuadPart / nvdb.BytesPerFileRecordSegment - 1;

    ULONG cb = __builtin_offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer[volData.BytesPerMFTRec]);
    //ULONG cb = offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer[volData.BytesPerFileRecordSegment]);

    PNTFS_FILE_RECORD_OUTPUT_BUFFER pnfrob = (PNTFS_FILE_RECORD_OUTPUT_BUFFER)alloca(cb);
    // OVERLAPPED ov = {};

    if (!DeviceIoControl(volData.hVolume, FSCTL_GET_NTFS_FILE_RECORD, &nfrib, sizeof(nfrib), pnfrob, cb, 0, nullptr/*&ov*/))
    {
        logger.ErrorFmt("DeviceIoControl failed with error: {}", GetLastError());
        return false;
    }

    MFT_FILE_RECORD* pmftrec = (MFT_FILE_RECORD*)pnfrob->FileRecordBuffer;

    // Make sure DeviceIoControl returned exactly the MFT record number we requested.
    // DeviceIoControl may return closest existing MFT record when record with requested ID is "free".
    assert(nfrib.FileReferenceNumber.QuadPart == pnfrob->FileReferenceNumber.QuadPart);
    assert(pnfrob->FileReferenceNumber.QuadPart == pmftrec->IndexMFTRec);
    assert(parentDirRecID.sId.low == pmftrec->IndexMFTRec);

    assert(pmftrec->FileRecSize > pmftrec->FirstAttrOffset);

    // whether we are reading base MFT record or child one
    if (itemInfo.RecID.Id > 0) 
    {
        // we are reading child record refered by ATTR_LIST_ATTR attribute
        logger.Debug("---------- Reading CHILD MFT Record ---------");
        assert(pmftrec->ParentFileRec.Id != 0);
    }
    else
    {
        // we are reading base record
        logger.Debug("---------- Reading BASE MFT Record ---------");
        assert(pmftrec->ParentFileRec.Id == 0);
        itemInfo.RecID.sId.low = pmftrec->IndexMFTRec;
        itemInfo.HardLinksCount = pmftrec->HardLinksCnt;
    }

    logger.DebugFmt("Signature: {}", (char*)pmftrec->RecHeader.Signature);
    logger.DebugFmt("MFT Rec ID: {}", pmftrec->IndexMFTRec);
    logger.DebugFmt("Parent MFT Rec ID: {:#x}", pmftrec->ParentFileRec.Id);
    logger.DebugFmt("MFT Hard links cnt: {}", pmftrec->HardLinksCnt);
    logger.DebugFmt("MFT Rec Size: {}", pmftrec->FileRecSize);
    logger.DebugFmt("MFT Alloc Rec Size: {}", pmftrec->AllocFileRecSize);
    
    switch (pmftrec->Flags)
    {
    case (uint16_t)MFT_RECORD_FLAGS::IN_USE: logger.Debug("MFT Rec type: IN USE (file or anything else)"); break;
    case (uint16_t)MFT_RECORD_FLAGS::IS_DIRECTORY: logger.Debug("MFT Rec type: DIRECTORY"); break;
    case (int16_t)MFT_RECORD_FLAGS::IN_USE | (int16_t)MFT_RECORD_FLAGS::IS_DIRECTORY: logger.DebugFmt("MFT Rec type: FILE & DIRECTORY {:#x}", (uint16_t)pmftrec->Flags); break;
    default:
        logger.DebugFmt("MFT Rec type: UNKNOWN {:#x}", (uint16_t)pmftrec->Flags);
    }

    MFT_ATTR_HEADER* currAttr = (MFT_ATTR_HEADER*)Add2Ptr(pmftrec, pmftrec->FirstAttrOffset);

    int attrOrderNum = 1;
    do  // reading all attributes in a loop
    {
        logger.DebugFmt("****** #{} Attribute ({:#x} {})", attrOrderNum++, (uint32_t)currAttr->AttrType, AttrName(currAttr->AttrType));
        logger.Debug(currAttr->NonResidentFlag ? "Attr type - NONRESIDENT" : "Attr type - RESIDENT");
        logger.DebugFmt("Attr Id: {}", currAttr->AttrID);
        logger.DebugFmt("Attr flags: {}", currAttr->Flags);

        if (currAttr->AttrNameSize > 0) // if attr has name - show it
        {
            wchar_t* attrname = GetAttrName(currAttr, AttrNameOffset);
            std::wstring name(attrname, currAttr->AttrNameSize);
            logger.DebugFmt("Attr name: '{}'", wtos(name));
        }

        // all attributes except for ATTR_FILENAME and ATTR_DATA must have only single instance in one MFT record.
        if ((currAttr->AttrType != ATTR_FILENAME) && (currAttr->AttrType != ATTR_DATA) && 
            (currAttr->AttrType != ATTR_LOGGED_UTILITY_STREAM) && (itemInfo.HasAttr[MakeAttrTypeIndex(currAttr->AttrType)])  )
            logger.WarnFmt("Looks like two and more {} attributes have found in MFTRec: {}", AttrName(currAttr->AttrType), pmftrec->IndexMFTRec);

        itemInfo.HasAttr[MakeAttrTypeIndex(currAttr->AttrType)] = true;
        itemInfo.AttrsCount++;

        if (currAttr->NonResidentFlag == 0) // attribute is RESident
        {
            logger.DebugFmt("Attr indexed: {}", currAttr->res.IndexedFlag);

            assert(currAttr->res.DataSize + currAttr->res.DataOffset <= currAttr->AttrSize);

            uint8_t* attrValue = Add2Ptr(currAttr, currAttr->res.DataOffset);
            //uint8_t* attrValue = (uint8_t*)currAttr + currAttr->res.DataOffset;

            // DWORD dateTimeFlags = FDTF_DEFAULT | FDTF_NOAUTOREADINGORDER;

            switch (currAttr->AttrType)
            {
            case ATTR_STD_INFO: // resident. Only.
            {
                ATTR_STD_INFO5* stdinfo = (ATTR_STD_INFO5*)attrValue;
                
                /*wchar_t buf[100];
                FILETIME ft;
                ft.dwLowDateTime = LODWORD(stdinfo->CreateTime);
                ft.dwHighDateTime = HIDWORD(stdinfo->CreateTime);
                SHFormatDateTime(&ft, &dateTimeFlags, buf, 100);
                logger.TraceFmt("Created: {}", wtos(buf));
                */

                logger.DebugFmt("Version number: {}", stdinfo->VersionNum);
                logger.DebugFmt("Max version num: {}", stdinfo->max_ver_num);
                logger.DebugFmt("Class Id: {}", stdinfo->class_id);
                logger.DebugFmt("Owner Id: {}", stdinfo->owner_id);

                break;
            }
            case ATTR_FILENAME: // resident. Only.
            {
                ATTR_FILE_NAME* fname = (ATTR_FILE_NAME*)attrValue;
                wchar_t* tmp = GetFName(fname, sizeof(ATTR_FILE_NAME));
                std::wstring name(tmp, fname->FileNameLen);
                itemInfo.FileNames.AddValue(name);
                itemInfo.FileNamesCount++;
                
                assert(itemInfo.FileNamesCount < 200);

                if (fname->NameType != FILE_NAME_DOS) itemInfo.MainName = name;

                logger.DebugFmt("File parent rec ID : {:#x}", fname->ParentDir.Id);
                logger.DebugFmt("File name type : {} ({})", fname->NameType, FileNameTypes[fname->NameType]);
                logger.DebugFmt("File DOS attrib : {:#x}", fname->dup.FileAttrib);
                logger.DebugFmt("File name: '{}'", wtos(name));

                break;
            }
            case ATTR_ID: // resident. 
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
            case ATTR_ROOT: // resident. ATTR_ROOT is resident only.
            {
                ATTR_INDEX_ROOT* indexR = (ATTR_INDEX_ROOT*)attrValue;
                logger.DebugFmt("IndexRoot attr type: {:#x} ({})", (uint32_t)indexR->AttrType, AttrName(currAttr->AttrType));
                logger.DebugFmt("Collation rule: {}", (uint32_t)indexR->Rule);
                logger.DebugFmt("Dir size: {} {}", indexR->ihdr.Flags, (indexR->ihdr.Flags == 0 ? " (SMALL DIR)" : " (BIG DIR)"));

                auto pihdr = &(indexR->ihdr);

                GetFileList(pihdr, itemInfo.Node.FileList);

                break;
            }
            case ATTR_LIST_ATTR: // resident. ATTR_LIST_ATTR can be either resident or non-resident
            {
                if(itemInfo.NonResidentAttrList) logger.Warn("Incorrect case has been met: itemInfo.NonResidentListAttr = true in resident context ");
                itemInfo.NonResidentAttrList = false; 

                logger.Debug("ATTR_LIST_ATTR START");

                THArray<uint32_t> visitedMFTRec;

                ATTR_LIST_ENTRY* attrEntry = (ATTR_LIST_ENTRY*)attrValue;
                uint8_t* attrEntryEnd = Add2Ptr(currAttr, currAttr->AttrSize);
                assert(attrEntry->AttrType > 0);
                assert(attrEntry->AttrSize > 0);
                assert(attrEntry->StartVCN == 0); // because first attr in the list is ATTR_STD_INFO
                assert(((uint32_t)(attrEntry->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero

                while (true)
                {
                    if (attrEntry->ref.sId.low != pmftrec->IndexMFTRec)
                    {
                        if (visitedMFTRec.IndexOf(attrEntry->ref.sId.low) == -1)
                        {                         
                            if(!ReadMftItemInfo(volData, attrEntry->ref, dirLevel, itemInfo))
                            {
                                logger.Error("ReadMftItemInfo() returned false!");
                            }
                            visitedMFTRec.AddValue(attrEntry->ref.sId.low);
                        }
                    }

                    attrEntry = (ATTR_LIST_ENTRY*)Add2Ptr(attrEntry, attrEntry->AttrSize);
                    if ((uint8_t*)attrEntry >= attrEntryEnd) break; 
                    assert(attrEntry->AttrType > 0);
                    assert(attrEntry->AttrSize > 0);
                    assert(((uint32_t)(attrEntry->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
                    //assert(attrEntry->StartVCN == 0);
                    //if (attrEntry->AttrType == ATTR_ZERO) break;
                   
                }

                logger.Debug("ATTR_LIST_ATTR FINISHED");

                break;
            }
            case ATTR_BITMAP: // resident. ATTR_BITMAP can be either resident and non-resident
            {
                if (itemInfo.NonResidentBitmap) logger.Warn("Incorrect case has been met: itemInfo.NonResidentBitmap = true in resident context "); 
                itemInfo.NonResidentBitmap = false;

                ATTR_BITMAP_ATTR* bmp = (ATTR_BITMAP_ATTR*)attrValue;
                logger.DebugFmt("ATTR_BITMAP res Size in bytes: {}, Value64: {:#x}", currAttr->res.DataSize, *(uint64_t*)bmp);

                assert((currAttr->res.DataSize & 0x07) == 0); // bitmap data size always multiple of 8

                itemInfo.Node.Bitmap.SetData((uint64_t*)bmp->bitmap, currAttr->res.DataSize >> 3);

                break;
            }
            case ATTR_DATA: // resident. ATTR_DATA can be resident or non-resident
            {
                if (itemInfo.ResidentData) logger.Warn("Looks like two resident DATA attributes have met in a single MFT record.");
                itemInfo.ResidentData = true;

                std::wstring name(GetAttrName(currAttr, AttrNameOffset), currAttr->AttrNameSize);
                itemInfo.DataStreamNames[itemInfo.DataStreamsCount++] = name;
                assert(itemInfo.DataStreamsCount < 20);

                logger.DebugFmt("Resident Data size: {}", currAttr->res.DataSize);
               
                break;
            }
            case ATTR_ALLOC: // resident. ATTR_ALLOC is NONResident only. 
            {
                logger.Warn("Warning! Resident ATTR_ALLOC has been met!");
                break;
            }
            case ATTR_SECURE:
            case ATTR_REPARSE:  // can be resident or non-resident
            case ATTR_EA:       // can be resident or non-resident
            case ATTR_EA_INFO:  // can be resident or non-resident
            case ATTR_PROPERTYSET:
            case ATTR_LOGGED_UTILITY_STREAM: // can be resident or non-resident
            {
                logger.DebugFmt("Attr: {}. We do not process this attribute.", AttrName(currAttr->AttrType));
                break;
            }
            
            default:
                logger.WarnFmt("UNKNOWN Resident ATTR has been met. Type: {0}, MFT Id: {1:#x} ({1})", AttrName(currAttr->AttrType), pmftrec->IndexMFTRec);


            } // switch

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
            case ATTR_DATA: // NONresident. Can be either resident and non-resident
            {
                if (itemInfo.ResidentData) 
                    logger.Warn("May be incorrect case has been met: itemInfo.ResidentData = true in NONresident context ");
                itemInfo.ResidentData = false;
                
                std::wstring name(GetAttrName(currAttr, AttrNameOffset), currAttr->AttrNameSize);
                itemInfo.DataStreamNames[itemInfo.DataStreamsCount++] = name;
                assert(itemInfo.DataStreamsCount < 20);

                logger.Debug("Atts: ATTR_DATA. We do not process this attribute.");
                break;
            }

            case ATTR_BITMAP: // NONresident. ATTR_BITMAP can be either resident and non-resident
            {
                itemInfo.NonResidentBitmap = true;

                logger.Warn("BITMAP NON-Resident has been met!");
                if (!ParseNonresBitmap(volData, currAttr, itemInfo.Node.Bitmap))
                {
                    logger.Error("ParseNonresBitmap finished with error.");
                    break;
                }

                break;
            }
            case ATTR_ALLOC: // NONresident. Only.
            {
                if (itemInfo.NonResidentAlloc) logger.Warn("Looks like two non-resident ALLOC attributes have met in a single MFT record.");
                itemInfo.NonResidentAlloc = true;
      
                if (!DataRunsDecode(currAttr, itemInfo.Node.DataRuns))
                {
                    logger.Error("DataRunsDecode finished with error.");
                    break;
                }
                
                // we need to know other attributes as well for proper data runs parsing below.
                PMFT_ATTR_HEADER attrValues[ATTR_TYPE_CNT];
                FillAttrValues(pmftrec, attrValues);
                PMFT_ATTR_HEADER bmpAttr = attrValues[MakeAttrTypeIndex(ATTR_BITMAP)];

                if (bmpAttr == nullptr) logger.Info("BITMAP attr IS NULL !!!");

                //TODO add proper handling bitmap attribute for ATTR_LIST_ATTR attribute type as it is done in ParseMFTRecord
                if ((itemInfo.Node.Bitmap.GetData() == nullptr) && (bmpAttr != nullptr))
                {
                    // expect only resident Bitmap here
                    assert(bmpAttr->NonResidentFlag == 0);

                    ATTR_BITMAP_ATTR* bmp = (ATTR_BITMAP_ATTR*)Add2Ptr(bmpAttr, bmpAttr->res.DataOffset);

                    assert((bmpAttr->res.DataSize & 0x07) == 0);

                    itemInfo.Node.Bitmap.SetData((uint64_t*)bmp->bitmap, bmpAttr->res.DataSize >> 3);

                    logger.DebugFmt("BITMAP Size in bytes: {}, Value64: {:#x}", bmpAttr->res.DataSize, *(uint64_t*)bmp->bitmap);
                }

                if (!ProcessAllocDataRuns(volData, itemInfo.Node))
                {
                    logger.Error("ProcessAllocDataRuns finished with error.");
                    //return is not needed here because node.FileList may contain items from INDEX_ROOT and partially from ALLOCATION
                    //return false;
                }

                break;
            }
            case ATTR_LIST_ATTR: // NONresident. ATTR_LIST_ATTR can be either resident and non-resident
            {  
                if (itemInfo.NonResidentAttrList) logger.Warn("Looks like two non-resident ATTR_LIST attributes have met in a single MFT record.");
                itemInfo.NonResidentAttrList = true;

                logger.Debug("nonres ATTR_LIST_ATTR START");

                TDataRuns dataRuns;
                if (!DataRunsDecode(currAttr, dataRuns)) // DataRunsDecode writes a message into log file in case of an error
                {
                    break;
                }

                uint8_t* dataBuf = nullptr;

                //assert(dataRuns.Count() == 1); //assuming that one LCN is always enough for list of attributes

                THArray<uint32_t> visitedMFTRec;
                uint32_t currRun = 0;
                
                while (currRun < dataRuns.Count())
                {
                    DATA_RUN_ITEM& rli = dataRuns[currRun];
                    logger.DebugFmt("[ReadMftItemInfo] Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

                    //assert(rli.len == 1);

                    uint dataBufSize = rli.len * volData.BytesPerCluster;
                    dataBuf = (uint8_t*)alloca(dataBufSize);

                    if (!ReadClusters(volData, rli.lcn, rli.len, dataBuf)) // ReadClusters writes a message into log file in case of an error
                    {
                        break;
                    }

                    ATTR_LIST_ENTRY* attrEntry = (ATTR_LIST_ENTRY*)dataBuf;
                   // ATTR_LIST_ENTRY* prevAttrEntry = nullptr;

                    //TODO probably we need to parse each cluster separately because end of last attrEntry in cluster#1 does not mean start of first attrEntry in cluster#2
                    uint8_t* attrEntryEnd1 = dataBuf + currAttr->nonres.RealSize;
                    uint8_t* attrEntryEnd2 = dataBuf + dataBufSize;
                    
                    assert(attrEntry->AttrSize > 0);
                    assert(attrEntry->AttrType > 0);
                    assert(((uint32_t)(attrEntry->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero

                    while (true)
                    {
                        if(attrEntry->AttrType != ATTR_DATA)  // StartVCN should be 0 for all attrs except DATA
                            assert(attrEntry->StartVCN == 0);
                        
                        if (attrEntry->StartVCN == 0) // attrEntry->ref is a MFT rec where attr value is located
                        {
                            if (attrEntry->ref.sId.low != pmftrec->IndexMFTRec) // attrs located in current pmftrec either already parsed or will be parsed during usual cycle of parsing 
                            {
                                if (visitedMFTRec.IndexOf(attrEntry->ref.sId.low) == -1) // whether we haven't parsed this attribute still
                                {
                                    if(!ReadMftItemInfo(volData, attrEntry->ref, dirLevel, itemInfo))
                                    {
                                        logger.Error("ReadMftItemInfo() returned false!");
                                    }
                                    visitedMFTRec.AddValue(attrEntry->ref.sId.low);
                                }
                            }
                        }
                        else // StartVCN is a cluster where attr value is located
                        {
                            logger.WarnFmt("[ReadMftItemInfo] ATTR_LIST_ENTRY.StartVCN <> 0! StartVCN: {}, AttrType: {}, MFTRec ID:{}", attrEntry->StartVCN, AttrName(attrEntry->AttrType), itemInfo.RecID.sId.low);
                        }

                        attrEntry = (ATTR_LIST_ENTRY*)Add2Ptr(attrEntry, attrEntry->AttrSize); 
                        if ((uint8_t*)attrEntry >= attrEntryEnd1) break;
                        if ((uint8_t*)attrEntry >= attrEntryEnd2) break;
                        //if (attrEntry->AttrType == ATTR_ZERO) break; 
                        //assert(attrEntry->AttrId > 0);
                        assert(attrEntry->AttrType > 0);
                        assert(attrEntry->AttrSize > 0); 
                        assert(((uint32_t)(attrEntry->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
                    }

                    currRun++;
                }

                logger.Debug("nonres ATTR_LIST_ATTR FINISHED");

                break;
            }
            case ATTR_EA: // non-resident. 
            {
                logger.DebugFmt("Attr: {}. We do not process this attribute.", AttrName(currAttr->AttrType));
                break;
            }
            default:
                logger.WarnFmt("UNKNOWN NONResident ATTR has been met. Type: {0}, MFT Id: {1:#x} ({1})", AttrName(currAttr->AttrType), pmftrec->IndexMFTRec);


            } //switch
        } //currAttr->NonResidentFlag == 0

        currAttr = (MFT_ATTR_HEADER*)Add2Ptr(currAttr, currAttr->AttrSize);
        assert(pmftrec->FileRecSize > Diff2Ptr(pmftrec, currAttr));

    } while (*((DWORD*)currAttr) != ATTR_END /*0xFFFFFFFF*/);

    // add only base records
    if(pmftrec->ParentFileRec.Id == 0)
        gItemsList.AddValue(itemInfo);

   //free(pnfrob);

    for (auto& item : itemInfo.Node.FileList)
    {
        ITEM_INFO newItemInfo{0};

        if (!IsMetaFile(item) && !IsDotDir(item.ciName)) // bypass hidden mft metafiles
            if (!ReadMftItemInfo(volData, item.MFTRef, dirLevel + 1, newItemInfo))
            {
                logger.Error("ReadMftItemInfo() returned false!");
            }
    }

    if (pmftrec->ParentFileRec.Id == 0)
        itemInfo.Node.Clear(); // list of subitems is not needed any more. save memory.

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
