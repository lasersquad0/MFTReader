
// this is to remove defines min, max in windows headers because they conflict with std::min std::max 
#define NOMINMAX

#include "Debug.h"
#include <windows.h>
#include <cassert>
#include "strutils/include/string_utils.h"
#include "strutils/include/ci_string.h"
#include "Functions.h"
#include "NTFS.h"

TItemInfoList gItemsList;

bool ReadMftItemInfo(VOLUME_DATA& volData, MFT_REF parentDirRecID, uint32_t dirLevel, ITEM_INFO& itemInfo)
{
    if (dirLevel > 60) throw std::runtime_error("dirLevel > 30 !!!!!!!");

    GET_LOGGER;

    FileListPred pred = [&itemInfo](const ATTR_FILE_NAME* attr, const MFT_REF& ref)
        {
            ci_string ciwnm(GetFName(attr, sizeof(ATTR_FILE_NAME)), attr->FileNameLen);
            itemInfo.Node.FileList.AddValue({ ciwnm, *attr, ref });
        };

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

    // DeviceIoControl may return other MFT record than we requested.
    // This may happen when requested MFT record has been deleted while we were parsing MFT structures and navigating
    // that happens not so rarely
    // just exit from ReadMftItemInfo in that case.
    // itemInfo will be "empty" in that case (no atributes) - so we can easily detect such items in the list.
    if (nfrib.FileReferenceNumber.QuadPart != pnfrob->FileReferenceNumber.QuadPart)
    {
        logger.WarnFmt("Requested MFT rec ID differs from returned. Looks like requested MFT record is deleted. Requested: {}, returned: {}", nfrib.FileReferenceNumber.QuadPart, pnfrob->FileReferenceNumber.QuadPart);
        return false;
    }

    // Make sure DeviceIoControl returned exactly the MFT record number we requested.
    // DeviceIoControl may return closest existing MFT record when record with requested ID is "free".
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

        std::wstring nameOfAttr;
        if (currAttr->AttrNameSize > 0) // if attr has a name - show it
        {
            //std::wstring name(GetAttrName(currAttr, AttrNameOffset), currAttr->AttrNameSize);
            nameOfAttr.assign(GetAttrName(currAttr, AttrNameOffset), currAttr->AttrNameSize);
            logger.DebugFmt("Attr name: '{}'", wtos(nameOfAttr));
        }
        std::string nameOfAttr2 = wtos(nameOfAttr);

        // all attributes except for ATTR_FILENAME and ATTR_DATA must have only single instance in one MFT record.
        if ((currAttr->AttrType != ATTR_FILENAME) && (currAttr->AttrType != ATTR_DATA) &&
            (currAttr->AttrType != ATTR_LOGGED_UTILITY_STREAM) && (itemInfo.AttrCounters[MakeAttrTypeIndex(currAttr->AttrType)] > 0))
            logger.WarnFmt("Looks like two and more {} ({}) attributes have found in MFTRec: {}", AttrName(currAttr->AttrType), nameOfAttr2, pmftrec->IndexMFTRec);

        itemInfo.AttrCounters[MakeAttrTypeIndex(currAttr->AttrType)]++;
        itemInfo.AttrsCount++;

        if (currAttr->NonResidentFlag == 0) // attribute is RESident
        {
            logger.DebugFmt("Attr indexed: {}", currAttr->res.IndexedFlag);

            assert(currAttr->res.DataSize + currAttr->res.DataOffset <= currAttr->AttrSize);
            uint8_t* attrValue = Add2Ptr(currAttr, currAttr->res.DataOffset);

            switch (currAttr->AttrType)
            {
            case ATTR_STD_INFO: // resident. Only.
            {
                ATTR_STD_INFO5* stdinfo = (ATTR_STD_INFO5*)attrValue;

                /*wchar_t buf[100];
                DWORD dateTimeFlags = FDTF_DEFAULT | FDTF_NOAUTOREADINGORDER;
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

                if (fname->NameType != FILE_NAME_DOS)
                {
                    itemInfo.MainName = name;
                    itemInfo.FileAttr = fname->dup.FileAttrib;
                }

                assert(itemInfo.FileNamesCount < 200);

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
                assert(indexR->AttrType == ATTR_FILENAME);
                assert(indexR->IndexBlockSize == volData.BytesPerCluster);
                assert(indexR->IndexBlockClst == 1);
                assert(indexR->Rule == COLLATION_RULE::FILENAME);

                logger.DebugFmt("IndexRoot attr type: {:#x} ({})", (uint32_t)indexR->AttrType, AttrName(currAttr->AttrType));
                logger.DebugFmt("Collation rule: {}", (uint32_t)indexR->Rule);
                logger.DebugFmt("Dir size: {} {}", indexR->ihdr.Flags, (indexR->ihdr.Flags == 0 ? " (SMALL DIR)" : " (BIG DIR)"));

                auto pihdr = &(indexR->ihdr);

                GetFileList(pihdr, pred);

                break;
            }
            case ATTR_LIST_ATTR: // resident. ATTR_LIST_ATTR can be either resident or non-resident
            {
                if (itemInfo.NonResidentAttrList) logger.Warn("Incorrect case has been met: itemInfo.NonResidentListAttr = true in resident context ");
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
                    if (attrEntry->AttrType != ATTR_DATA)  // StartVCN should be 0 for all attrs except DATA
                    {
                        assert(attrEntry->StartVCN == 0);
                        if (attrEntry->StartVCN != 0)
                            logger.WarnFmt("Looks like incorrecdt case. StartVCN ({}) <> 0 for {} attribute. MFTRec: {}.", attrEntry->StartVCN, AttrName(attrEntry->AttrType), pmftrec->IndexMFTRec);
                    }

                    if (attrEntry->StartVCN > volData.MaxMFTIndex)
                        logger.WarnFmt("StartVCN ({}) greater than max MFT index ({}).", attrEntry->StartVCN, volData.MaxMFTIndex);

                    if (attrEntry->ref.sId.low != pmftrec->IndexMFTRec)
                    {
                        if (visitedMFTRec.IndexOf(attrEntry->ref.sId.low) == -1)
                        {
                            if (!ReadMftItemInfo(volData, attrEntry->ref, dirLevel, itemInfo))
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
                if (itemInfo.ResidentData)
                    logger.DebugFmt("Looks like two resident DATA ('{}') attributes have met in a single MFT record: {}.", nameOfAttr2, pmftrec->IndexMFTRec);
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
                logger.DebugFmt("Attr: {}, AttrName: {}. We do not process this attribute.", AttrName(currAttr->AttrType), nameOfAttr2);
                break;
            }

            default:
                logger.WarnFmt("UNKNOWN Resident attr has been met. Type:{0}, Name:{1}, MFT Id:{2:#x} ({2})", AttrName(currAttr->AttrType), nameOfAttr2, pmftrec->IndexMFTRec);

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
                    logger.DebugFmt("Looks like resident DATA attr ('{}') is met in NONresident context. MFT: {}", nameOfAttr2, pmftrec->IndexMFTRec);
                itemInfo.ResidentData = false;

                std::wstring name(GetAttrName(currAttr, AttrNameOffset), currAttr->AttrNameSize);
                itemInfo.DataStreamNames[itemInfo.DataStreamsCount++] = name;
                assert(itemInfo.DataStreamsCount < 20);

                logger.DebugFmt("Attr: ATTR_DATA. Name: {}. We do not process this attribute.", nameOfAttr2);
                break;
            }

            case ATTR_BITMAP: // NONresident. ATTR_BITMAP can be either resident and non-resident
            {
                itemInfo.NonResidentBitmap = true;

                logger.WarnFmt("NON-Resident BITMAP ('{}') attr has been met!", nameOfAttr2);
                if (!ParseNonresBitmap(volData, currAttr, itemInfo.Node.Bitmap))
                {
                    logger.Error("ParseNonresBitmap finished with error.");
                    break;
                }

                break;
            }
            case ATTR_ALLOC: // NONresident. Only.
            {
                if (itemInfo.NonResidentAlloc)
                    logger.WarnFmt("Looks like two non-resident ALLOC attributes ('{}') have met in a single MFT record: {}.", nameOfAttr2, pmftrec->IndexMFTRec);

                itemInfo.NonResidentAlloc = true;

                if (!DataRunsDecode(currAttr, itemInfo.Node.DataRuns))
                {
                    logger.Error("DataRunsDecode finished with error.");
                    break;
                }

                // ONLY DECODE DATA RUNS HERE.
                // Processing of data runs is moved below after all atrributes are read from MFT base and child records

                break;
            }
            case ATTR_LIST_ATTR: // NONresident. ATTR_LIST_ATTR can be either resident and non-resident
            {
                if (itemInfo.NonResidentAttrList)
                    logger.WarnFmt("Looks like two non-resident ATTR_LIST ('{}') attributes have met in a single MFT record: {}.", nameOfAttr2, pmftrec->IndexMFTRec);

                itemInfo.NonResidentAttrList = true;

                logger.Debug("nonres ATTR_LIST_ATTR START");

                TDataRuns dataRuns;
                if (!DataRunsDecode(currAttr, dataRuns)) // DataRunsDecode writes a message into log file in case of an error
                {
                    break;
                }

                uint8_t* dataBuf = nullptr;

                //assert(dataRuns.Count() == 1); //assuming that one LCN is always enough for list of attributes

                if (dataRuns.Count() > 1)
                    logger.InfoFmt("Unusual case. ATTR_LIST occupies {} data runs", dataRuns.Count());

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

                    //TODO probably we need to parse each cluster separately because end of last attrEntry in cluster#1 does not mean start of first attrEntry in cluster#2
                    uint8_t* attrEntryEnd1 = dataBuf + currAttr->nonres.RealSize;
                    uint8_t* attrEntryEnd2 = dataBuf + dataBufSize;

                    assert(attrEntry->AttrSize > 0);
                    assert(attrEntry->AttrType > 0);
                    assert(((uint32_t)(attrEntry->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero

                    while (true)
                    {
                        if (attrEntry->AttrType != ATTR_DATA)  // StartVCN should be 0 for all attrs except DATA
                        {
                            assert(attrEntry->StartVCN == 0);
                            if (attrEntry->StartVCN != 0)
                                logger.WarnFmt("Looks like incorrecdt case. StartVCN({}) <> 0 for {} attribute. MFTRec: {}.", attrEntry->StartVCN, AttrName(attrEntry->AttrType), pmftrec->IndexMFTRec);
                        }

                        if (attrEntry->StartVCN == 0)
                        {
                            if (attrEntry->ref.sId.low != pmftrec->IndexMFTRec) // attrs located in current pmftrec either already parsed or will be parsed during usual cycle of parsing 
                            {
                                if (visitedMFTRec.IndexOf(attrEntry->ref.sId.low) == -1) // whether we haven't parsed this attribute still
                                {
                                    // attrEntry->ref is a MFT rec where attr value is located
                                    if (!ReadMftItemInfo(volData, attrEntry->ref, dirLevel, itemInfo))
                                    {
                                        logger.Error("ReadMftItemInfo() returned false!");
                                    }
                                    visitedMFTRec.AddValue(attrEntry->ref.sId.low);
                                }
                            }
                        }
                        else // StartVCN is a cluster where attr value is located
                        {
                            //logger.WarnFmt("[ReadMftItemInfo] ATTR_LIST_ENTRY.StartVCN <> 0! StartVCN: {}, AttrType: {}, MFTRec ID:{}", attrEntry->StartVCN, AttrName(attrEntry->AttrType), pmftrec->IndexMFTRec);

                            if (attrEntry->StartVCN > volData.MaxMFTIndex)
                                logger.WarnFmt("StartVCN ({}) greater than max MFT index ({}).", attrEntry->StartVCN, volData.MaxMFTIndex);
                        }

                        attrEntry = (ATTR_LIST_ENTRY*)Add2Ptr(attrEntry, attrEntry->AttrSize);
                        if ((uint8_t*)attrEntry >= attrEntryEnd1) break;
                        if ((uint8_t*)attrEntry >= attrEntryEnd2) break;
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
            case ATTR_EA: // can be resident or non-resident 
            case ATTR_LOGGED_UTILITY_STREAM: // can be resident and non-resident
            {
                logger.DebugFmt("Attr: {}, Name: {}. We do not process this attribute.", AttrName(currAttr->AttrType), nameOfAttr2);
                break;
            }
            default:
                logger.WarnFmt("UNKNOWN NONResident attr has been met. Type: {0}, Name: {1}, MFT Id: {2:#x} ({2})", AttrName(currAttr->AttrType), nameOfAttr2, pmftrec->IndexMFTRec);


            } //switch
        } //currAttr->NonResidentFlag == 0

        currAttr = (MFT_ATTR_HEADER*)Add2Ptr(currAttr, currAttr->AttrSize);
        assert(pmftrec->FileRecSize > Diff2Ptr(pmftrec, currAttr));

    } while (*((DWORD*)currAttr) != ATTR_END /*0xFFFFFFFF*/);


    if (pmftrec->ParentFileRec.Id == 0)
    {
        // once we read all attributes we are ready to process ALLOC data runs
        if (itemInfo.Node.DataRuns.Count() > 0)
        {
            // single function but different behaviour of adding files/dirs into lists
            // in other place where ProcessAllocDataRuns called lambda adds found dirs into another list
            if (!ProcessAllocDataRuns(volData, itemInfo.Node, pred /*[&itemInfo](const ATTR_FILE_NAME* attr, const MFT_REF& ref)
                {
                    ci_string ciwnm(GetFName(attr, sizeof(ATTR_FILE_NAME)), attr->FileNameLen);
                    itemInfo.Node.FileList.AddValue({ ciwnm, *attr, ref });
                } */)
               )
            {
                logger.Error("ProcessAllocDataRuns finished with error.");
                //return is not needed here because node.FileList may contain items from INDEX_ROOT and partially from ALLOCATION
            }
        }

        gItemsList.AddValue(itemInfo);

        for (auto& item : itemInfo.Node.FileList)
        {
            ITEM_INFO newItemInfo{ 0 };

            if (dirLevel == 0) std::wcout << item.ciName.c_str() << std::endl;

            if (!IsMetaFile(item) && !IsDotDir(item.ciName)) // bypass hidden mft metafiles
                if (!ReadMftItemInfo(volData, item.MFTRef, dirLevel + 1, newItemInfo))
                {
                    logger.ErrorFmt("ReadMftItemInfo() finished with error for MFT rec: {}", item.MFTRef.sId.low);
                }
        }

        itemInfo.Node.Clear(); // list of subitems is not needed any more. save memory.
    }

    return true;
}



void ReadItems(VOLUME_DATA& volData)
{
    GET_LOGGER;

    extern TItemInfoList gItemsList;
    gItemsList.SetCapacity(1'000'000); // Expect 1M files and dirs

    MFT_REF startId;
    startId.Id = MFT_ROOT_REC_ID;
    ITEM_INFO iInfo{ 0 };

    if (!ReadMftItemInfo(volData, startId, 0, iInfo))
    {
        logger.Error("ReadMftItemInfo() returned false!");
    }


    auto AttrCountGreater7 = std::count_if(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a) { return a.AttrsCount > 7; });
    auto HardLinksGreater7 = std::count_if(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a) { return a.HardLinksCount > 7; });
    auto DataStreamsCountGreater2 = std::count_if(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a) { return a.DataStreamsCount > 2; });
    auto FilenamesCountGreater9 = std::count_if(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a) { return a.FileNamesCount > 9; });
    auto FilenamesCountGreater5 = std::count_if(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a) { return a.FileNamesCount > 5; });
    auto HasNonresAttrList = std::count_if(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a) { return a.NonResidentAttrList; });
    auto HasNonresBitmap = std::count_if(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a) { return a.NonResidentBitmap; });
    auto HasResidentData = std::count_if(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a) { return a.ResidentData; });

    auto maxHardLinks = std::max_element(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) {return a.HardLinksCount < b.HardLinksCount; });
    auto maxAttrs = std::max_element(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) {return a.AttrsCount < b.AttrsCount; });
    auto maxFilenames = std::max_element(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) {return a.FileNamesCount < b.FileNamesCount; });
    auto maxDataStreams = std::max_element(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) {return a.DataStreamsCount < b.DataStreamsCount; });

    std::cout << "Total items count: " << gItemsList.Count() << std::endl;
    std::cout << "Attrs count > 7: " << AttrCountGreater7 << std::endl;
    std::cout << "Hard Links count > 7: " << HardLinksGreater7 << std::endl;
    std::cout << "Data streams count > 2: " << DataStreamsCountGreater2 << std::endl;
    std::cout << "Filenames count > 9: " << FilenamesCountGreater9 << std::endl;
    std::cout << "Filenames count > 5: " << FilenamesCountGreater5 << std::endl;
    std::cout << "Have non-resident attr_list: " << HasNonresAttrList << std::endl;
    std::cout << "Have non-resident bitmap: " << HasNonresBitmap << std::endl;
    std::cout << "Have resident data: " << HasResidentData << std::endl;
    std::cout << "ITEM_INFO size:" << sizeof(ITEM_INFO) << std::endl;
    std::cout << "DIR_NODE size:" << sizeof(DIR_NODE) << std::endl;
    std::cout << std::format("Max hard links count: {}, file name: {}", (*maxHardLinks).HardLinksCount, wtos((*maxHardLinks).MainName)) << std::endl;
    std::cout << std::format("Max attrs count: {}, file name: {}", (*maxAttrs).AttrsCount, wtos((*maxAttrs).MainName)) << std::endl;
    std::cout << std::format("Max file names count:{}, file name: {}", (*maxFilenames).FileNamesCount, wtos((*maxFilenames).MainName)) << std::endl;
    std::cout << std::format("Max data streams count:{}, filename:{}", (*maxDataStreams).DataStreamsCount, wtos((*maxDataStreams).MainName)) << std::endl;

    std::cout << "Datastream names:" << std::endl;
    for (auto& ds : (*maxDataStreams).DataStreamNames)
    {
        std::cout << wtos(ds) << std::endl;
    }

    std::cout << "File names:" << std::endl;
    for (auto& fn : (*maxFilenames).FileNames)
    {
        std::cout << wtos(fn) << std::endl;
    }

    auto dirCount = std::count_if(gItemsList.begin(), gItemsList.end(), [](ITEM_INFO& a) { return IsItemDir(a); });

    std::cout << toStringSepA(gItemsList.Count()) + " - total" << std::endl;
    std::cout << toStringSepA(gItemsList.Count() - dirCount) + " - files" << std::endl; // only files 
    std::cout << toStringSepA(dirCount) + " - dirs" << std::endl; // only dirs 

    /*std::cout << "Sorting... " << std::endl;
    std::sort(gItemsList.begin(), gItemsList.end());

    std::string filename = "ListMFTFile_sorted.log";
    LogEngine::TFileStream ff(filename);

    ff << toStringSepW(gItemsList.Count()) + L" - total";
    ff << toStringSepW(gItemsList.Count() - dirCount) + L" - files"; // only files
    ff << toStringSepW(dirCount) + L" - dirs"; // only dirs

    std::cout << "Saving list of files to " << filename << std::endl;

    for (auto& item : gItemsList)
    {
        if (IsItemDir(item))
            item.MainName += L'\\';

        ff << item.MainName;
    }*/

    gItemsList.ClearMem();
}