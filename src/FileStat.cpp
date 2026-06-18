
// this is to remove defines min, max in windows headers because they conflict with std::min std::max 
#define NOMINMAX

#include "Debug.h"
#include <windows.h>
#include <shlwapi.h>
#include <cassert>
#include <numeric>

#include "strutils/include/string_utils.h"
#include "strutils/include/ci_string.h"
#include "Functions.h"
#include "NTFS.h"


//parses either resident or non-resident ATTR_LIST
static void ParseAttrList(MFTRecIndex indexMFTRec, ATTR_LIST_ENTRY* startListItem, uint8_t* attrListEnd, uint64_t realSize, AttrListPred pred)
{
    GET_LOGGER;

    uint64_t processedAttrSize = 0;

    ATTR_LIST_ENTRY* attrEntry = startListItem;

    assert(attrEntry->AttrSize > 0);
    assert(attrEntry->AttrType > 0);
    assert(((uint32_t)(attrEntry->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
    assert(attrEntry->AttrType != ATTR_ZERO);
    assert(attrEntry->AttrType != ATTR_END);

    THArray<uint32_t> visitedMFTRec;

    // this is do not not parse current indexMFTRec again when reading attrEntry->ref MFT record   
    // because attrs located in current MFT rec either already parsed or will be parsed during usual cycle of parsing 
    visitedMFTRec.AddValue(indexMFTRec);

    while (true)
    {
        // StartVCN might be >0 when one attribute does not fit into one MFT record.
        // This attribute may have very long Data Run list or anything else
        // In this case ATTR_LIST contains several ATTR_LIST_ENTRY entries for this big attribute.
        // First entry has StartVCN=0, others - preventry.StartVCN+num_of_vcns_in_preventry_dataruns, etc.
        // all these entries build up a continious list of VCNs 
        if ( (attrEntry->AttrType != ATTR_DATA) && (attrEntry->AttrType != ATTR_ALLOC) )  // StartVCN should be 0 for all attrs except ATTR_DATA and ATTR_ALLOC
        {
            assert(attrEntry->StartVCN == 0);
            if (attrEntry->StartVCN != 0)
                logger.WarnFmt("Looks like we have met incorrect case. StartVCN({}) <> 0 for {} attribute. MFT Rec ID: {}.",
                    attrEntry->StartVCN, AttrName(attrEntry->AttrType), MFT_REF::toHexString(indexMFTRec));
        }

        // attributes in non-resident attr list located in a separate LCN cluster may refer back to the base record
        // because some attributes may reside in base mft record and the others in "child" mft record(s)
        // the attr list attribute itself is located in LCN cluster that is not mft record, it does not contain signature or Fixups values, etc.
 
        //if (attrEntry->ref.sId.low != indexMFTRec) 
        {
            if (visitedMFTRec.IndexOf(attrEntry->ref.sId.low) == -1) // whether we haven't parsed this MFT record yet
            {
                pred(attrEntry->ref);
                // attrEntry->ref is a MFT rec where attr value is located
                //if (!ReadMftItemInfo(volData, attrEntry->ref, itemInfo))
                //{
                //    logger.Error("ReadMftItemInfo() returned false!");
                //}
                visitedMFTRec.AddValue(attrEntry->ref.sId.low);
            }
        }

        // StartVCN is a cluster where attribute portion value is located
        if (attrEntry->StartVCN != 0)
        {
            assert( (attrEntry->AttrType == ATTR_DATA) || (attrEntry->AttrType == ATTR_ALLOC) );
            if (attrEntry->AttrType != ATTR_DATA)
                logger.WarnFmt("One attribute does not fit into one MFT record. StartVCN: {}, AttrType: {}, ref: {}, MFT Rec ID: {}",
                              attrEntry->StartVCN, AttrName(attrEntry->AttrType), attrEntry->ref.toHexString(), MFT_REF::toHexString(indexMFTRec));
        }

        processedAttrSize += attrEntry->AttrSize;
        if (processedAttrSize >= realSize)
        {
            logger.DebugFmt("Loop is finished by this condition: 'processedAttrSize >= realSize'. Last Attr: {}, realSize: {}", AttrName(attrEntry->AttrType), realSize);
            break;
        }

        attrEntry = (ATTR_LIST_ENTRY*)Add2Ptr(attrEntry, attrEntry->AttrSize);

        if ((uint8_t*)attrEntry >= attrListEnd)
        {
            logger.InfoFmt("Loop is finished by this condition: 'attrEntry >= attrEntryEnd' (end of buffer with clusters). Last Attr: {}, currAttr->nonres.RealSize: {}",
                AttrName(attrEntry->AttrType), realSize);
            break;
        }


        //assert(attrEntry->AttrId > 0);
        assert(attrEntry->AttrType > 0);
        assert(attrEntry->AttrSize > 0);
        assert(((uint32_t)(attrEntry->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
        assert(attrEntry->AttrType != ATTR_ZERO);
        assert(attrEntry->AttrType != ATTR_END);
    }
}


/** 
* @brief Reads information about one MFT record 
* @details Reads information ONLY about one MFT record refered by mftRecRef 
* Does NOT go to child items recursively
* @param itemInfo results of reading MFT record
* @return true in case of success, false in case of error
*/ 
bool ReadMftItemInfo(VOLUME_DATA& volData, MFT_REF mftRecRef, ITEM_INFO& itemInfo)
{
    GET_LOGGER;

    FileListPred addToFileListPred = [&itemInfo](const ATTR_FILE_NAME* attr, const MFT_REF& ref)
        {
            ci_string ciwnm(GetFName(attr), attr->FileNameLen);
            itemInfo.Node.FileList.AddValue({ ciwnm, *attr, ref });
        };

    NTFS_FILE_RECORD_INPUT_BUFFER nfrib{0};
    nfrib.FileReferenceNumber.LowPart = mftRecRef.sId.low;

    //ULONG cb = __builtin_offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer[volData.BytesPerMFTRec]);
    ULONG cb = offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer[volData.BytesPerFileRecordSegment/*BytesPerMFTRec*/]);

    PNTFS_FILE_RECORD_OUTPUT_BUFFER pnfrob = (PNTFS_FILE_RECORD_OUTPUT_BUFFER)alloca(cb);
    
    DWORD bytesReturned;
    if (!DeviceIoControl(volData.hVolume, FSCTL_GET_NTFS_FILE_RECORD, &nfrib, sizeof(nfrib), pnfrob, cb, &bytesReturned, nullptr))
    {
        logger.ErrorFmt("DeviceIoControl failed with error: {}", GetLastError());
        return false;
    }

    MFT_FILE_RECORD* pmftrec = (MFT_FILE_RECORD*)pnfrob->FileRecordBuffer;

    // DeviceIoControl may return other MFT record than we requested.
    // This may happen when requested MFT record has been deleted while we were parsing MFT structures and navigating,
    // that happens not so rarely
    // just exit from ReadMftItemInfo in that case.
    // itemInfo will be "empty" in that case (no atributes) - so we can easily detect such items in the list.
    if (nfrib.FileReferenceNumber.QuadPart != pnfrob->FileReferenceNumber.QuadPart)
    {
        logger.WarnFmt("Requested MFT rec ID differs from returned. Looks like requested MFT record is deleted. Requested: {:#x}, returned: {:#x}", nfrib.FileReferenceNumber.QuadPart, pnfrob->FileReferenceNumber.QuadPart);
        return false;
    }

    // Make sure DeviceIoControl returned exactly the MFT record number we requested.
    // DeviceIoControl may return closest existing MFT record when record with requested ID is "free".
    assert(pnfrob->FileReferenceNumber.QuadPart == pmftrec->IndexMFTRec);
    assert(mftRecRef.sId.low == pmftrec->IndexMFTRec);

    assert(pmftrec->FileRecSize > pmftrec->FirstAttrOffset);
    assert(ntfs_is_file_recp(pmftrec->RecHeader.Signature));

    bool isBASERec = itemInfo.RecID.Id == 0;

    // whether we are reading base MFT record or child one
    if (isBASERec)
    {
        // we are reading base record
        logger.Debug("---------- Reading BASE MFT Record ---------");
        assert(pmftrec->ParentFileRec.Id == 0);
        itemInfo.RecID.Id = 0;
        itemInfo.RecID.sId.low = pmftrec->IndexMFTRec;
        itemInfo.HardLinksCount = pmftrec->HardLinksCnt;
    }
    else
    {
        // we are reading child record refered by ATTR_LIST_ATTR attribute
        logger.Debug("---------- Reading CHILD MFT Record ---------");
        assert(pmftrec->ParentFileRec.Id != 0);
    }
  
    logger.DebugFmt("MFT Rec Signature: '{}'", std::string((char*)pmftrec->RecHeader.Signature, 4));
    logger.DebugFmt("MFT Rec ID: {}", MFT_REF::toHexString(pmftrec->IndexMFTRec));
    logger.DebugFmt("MFT Rec Num re-used: {}", pmftrec->SeqNum);
    logger.DebugFmt("MFT Parent Rec ID: {} {}", pmftrec->ParentFileRec.toHexString(), pmftrec->ParentFileRec.Id > 0 ? " CHILD" : "BASE");
    logger.DebugFmt("MFT Hard Links Count: {}{}", pmftrec->HardLinksCnt, pmftrec->ParentFileRec.Id > 0? " (may be inaccurate for child records)": "");
    logger.DebugFmt("MFT Rec Size: {}", pmftrec->FileRecSize);
    logger.DebugFmt("MFT Allocated Rec Size: {} ", pmftrec->AllocFileRecSize);

    switch (pmftrec->Flags)
    {
    case MFT_FLAG_IN_USE: logger.Debug("MFT Rec Type: 'IN USE' (file or anything else)"); break;
    case MFT_FLAG_IS_DIRECTORY: logger.Debug("MFT Rec Type: DIRECTORY"); break;
    case MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY: logger.DebugFmt("MFT Rec Type: 'FILE or DIRECTORY' {:#x}", (uint16_t)pmftrec->Flags); break;
    default:
        logger.DebugFmt("MFT Rec Type: UNKNOWN {:#x}", (uint16_t)pmftrec->Flags);
    }

    MFT_ATTR_HEADER* currAttr = (MFT_ATTR_HEADER*)Add2Ptr(pmftrec, pmftrec->FirstAttrOffset);

    int attrOrderNum = 1;
    do  // reading all attributes in a loop
    {
        logger.DebugFmt("********** #{} Attribute ({} {:#x}) **********", attrOrderNum++, AttrName(currAttr->AttrType), (uint32_t)currAttr->AttrType);
        logger.Debug(currAttr->NonResidentFlag ? "Attr Type: NON-RESIDENT" : "Attr Type: RESIDENT");
        logger.DebugFmt("Attr ID: {}", currAttr->AttrID);
        logger.DebugFmt("Attr Size: {}", currAttr->AttrSize);
        logger.DebugFmt("Attr Flags: {}", currAttr->Flags);

        std::wstring nameOfAttrW;
        std::string nameOfAttrA = "<noname>";
        if (currAttr->AttrNameSize > 0) // if attr has a name - show it
        {
            nameOfAttrW.assign(GetAttrName(currAttr, AttrNameOffset), currAttr->AttrNameSize);
            nameOfAttrA = wtos(nameOfAttrW);
            logger.DebugFmt("Attr Name: '{}'", nameOfAttrA);
        }
        
        // all attributes except for ATTR_FILENAME, ATTR_DATA and ATTR_LOGGED_UTILITY_STREAM must have only single instance in one MFT record.
        // also there can be more than one ALLOCATION attribute because folders may have huge number of files and Data Runs in ALLOCATION attribute can be long
        if ((currAttr->AttrType != ATTR_FILENAME) && (currAttr->AttrType != ATTR_DATA) &&
            (currAttr->AttrType != ATTR_LOGGED_UTILITY_STREAM) && (currAttr->AttrType != ATTR_ALLOC) &&
            (itemInfo.AttrCounters[MakeAttrTypeIndex(currAttr->AttrType)] > 0))
            logger.WarnFmt("Looks like two and more {} ({}) attributes have found in MFT Rec: {}", AttrName(currAttr->AttrType), nameOfAttrA, MFT_REF::toHexString(pmftrec->IndexMFTRec));

        itemInfo.AttrCounters[MakeAttrTypeIndex(currAttr->AttrType)]++;
        itemInfo.AttrsCount++;

        if (currAttr->NonResidentFlag == 0) // attribute is RESident
        {
            logger.DebugFmt("Attr Indexed: {}", currAttr->res.IndexedFlag);

            assert(currAttr->res.DataSize + currAttr->res.DataOffset <= currAttr->AttrSize);
            uint8_t* attrValue = Add2Ptr(currAttr, currAttr->res.DataOffset);

            switch (currAttr->AttrType)
            {
            case ATTR_STD_INFO: // resident. Only.
            {
                ATTR_STD_INFO5* stdinfo = (ATTR_STD_INFO5*)attrValue;

                assert((stdinfo->FileAttrib & FILE_ATTRIBUTE_NORMAL/*0x00000080*/) == 0);// check that NORMAL bit is always zero

                //if (logger.ShouldLog(LogEngine::Levels::llDebug))
                //{

                    logger.Debug(FileDateToString("Created: ", stdinfo->CreateTime));
                    logger.Debug(FileDateToString("Modified: ", stdinfo->ModifyTime));
                    logger.Debug(FileDateToString("LastAccess: ", stdinfo->LastAccessTime));

                    /*const uint BUF_SZ = 100;
                    wchar_t buf[BUF_SZ];
                    DWORD dateTimeFlags = FDTF_DEFAULT | FDTF_NOAUTOREADINGORDER;
                    FILETIME ft{0};

                    ft.dwLowDateTime = LODWORD(stdinfo->CreateTime);
                    ft.dwHighDateTime = HIDWORD(stdinfo->CreateTime);
                    SHFormatDateTime(&ft, &dateTimeFlags, buf, BUF_SZ);
                    std::string str = wtos(&buf[0]);
                    logger.DebugFmt("Created: {}", wtos(&(buf[0])));
                    

                    ft.dwLowDateTime = LODWORD(stdinfo->ModifyTime);
                    ft.dwHighDateTime = HIDWORD(stdinfo->ModifyTime);
                    SHFormatDateTime(&ft, &dateTimeFlags, buf, BUF_SZ);
                    logger.DebugFmt("Modified: {}", wtos(&buf[0]));

                    ft.dwLowDateTime = LODWORD(stdinfo->LastAccessTime);
                    ft.dwHighDateTime = HIDWORD(stdinfo->LastAccessTime);
                    SHFormatDateTime(&ft, &dateTimeFlags, buf, BUF_SZ);
                    logger.DebugFmt("Last Access: {}", wtos(&buf[0]));
                    */
               // }

                logger.DebugFmt("File Attrib: {:#x} {}", stdinfo->FileAttrib, FormatFileAttributes(stdinfo->FileAttrib));
                logger.DebugFmt("Version number: {}", stdinfo->VersionNum);
                logger.DebugFmt("Max Version num: {}", stdinfo->max_ver_num);
                logger.DebugFmt("Class Id: {}", stdinfo->class_id);
                logger.DebugFmt("Owner Id: {}", stdinfo->owner_id);
                logger.DebugFmt("USN: {:#x}", stdinfo->usn);
                logger.DebugFmt("Security ID: {}", stdinfo->security_id);
                logger.DebugFmt("Quota Charged: {}", stdinfo->quota_charged);


                break;
            }
            case ATTR_FILENAME: // resident. Only.
            {
                ATTR_FILE_NAME* fname = (ATTR_FILE_NAME*)attrValue;
                std::wstring name(GetFName(fname), fname->FileNameLen);
                itemInfo.FileNames.AddValue(name);

                if (fname->NameType != FILE_NAME_DOS)
                { 
                    itemInfo.MainName = name.c_str();
                    itemInfo.FileAttrib = fname->dup.FileAttrib;
                }

                assert((fname->dup.FileAttrib & FILE_ATTRIBUTE_NORMAL/*0x00000080*/) == 0);// check that NORMAL bit is always zero

                logger.DebugFmt("File Parent Rec ID: {}", fname->ParentDir.toHexString());
                logger.DebugFmt("File Name Type: '{}' ({:#x})", FileNameTypes[fname->NameType], fname->NameType);
                logger.DebugFmt("File DOS Attrib: {:#x} {}", fname->dup.FileAttrib, FormatFileAttributes(fname->dup.FileAttrib));
                logger.DebugFmt("File Name: '{}'", wtos(name));
                logger.DebugFmt("File Size: {}", fname->dup.FileSize);

                logger.Debug(FileDateToString("Created: ", fname->dup.CreateTime));
                logger.Debug(FileDateToString("Modified: ", fname->dup.ModifyTime));
                logger.Debug(FileDateToString("LastAccess: ", fname->dup.LastAccessTime));

                //assert(fname->dup.FileSize == 0);

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
                logger.DebugFmt("We do not process this attribute. Attr: '{}', AttrName: '{}'.", AttrName(currAttr->AttrType), nameOfAttrA);
                break;
            }
            case ATTR_ROOT: // resident. ATTR_ROOT is resident only.
            {
                // $SDH attribute name is related to storing and searching security descriptors (usually in MFT=0x09).

                ATTR_INDEX_ROOT* indexR = (ATTR_INDEX_ROOT*)attrValue;
                assert(indexR->AttrType == ATTR_FILENAME);//TODO MFT=0x09 contains two ATTR_ROOT attributes one of the with indexR->AttrType=0 for some reason
                assert(indexR->IndexBlockSize == volData.BytesPerCluster);
                assert(indexR->IndexBlockClst == 1);
                assert(indexR->Rule == COLLATION_RULE::FILENAME);

                logger.DebugFmt("IndexRoot Indexed Attr Type: {} {:#x}", AttrName(indexR->AttrType), (uint32_t)indexR->AttrType);
                logger.DebugFmt("IndexRoot Collation Rule: {} ({})", CollRuleName((uint32_t)indexR->Rule), (uint32_t)indexR->Rule);
                logger.DebugFmt("IndexRoot Dir Type: {} ({:#x})", indexR->ihdr.Flags == 0 ? " SMALL DIR" : " BIG DIR", indexR->ihdr.Flags);

                auto pihdr = &(indexR->ihdr);

                GetFileList(pihdr, addToFileListPred);

                break;
            }
            case ATTR_LIST_ATTR: // resident. ATTR_LIST_ATTR can be either resident or non-resident
            {
                assert(pmftrec->ParentFileRec.Id == 0); // only base records can have ATTR_LIST_ATTR attribute

                if (itemInfo.NonResidentAttrList.has_value()) 
                    logger.WarnFmt("Incorrect case has been met: Looks like two or more ATTR_LIST_ATTR ('{}') attributes present in a one MFT record: {}.", nameOfAttrA, MFT_REF::toHexString(pmftrec->IndexMFTRec));
                itemInfo.NonResidentAttrList = false;

                logger.Debug("resident ATTR_LIST_ATTR START");

                THArray<uint32_t> visitedMFTRec;

                ATTR_LIST_ENTRY* attrEntry = (ATTR_LIST_ENTRY*)attrValue;
                uint8_t* attrEntryEnd = Add2Ptr(currAttr, currAttr->AttrSize);

                AttrListPred CallReadMftItemInfo = [&itemInfo, &visitedMFTRec, &volData](const MFT_REF& ref)
                    {
                        GET_LOGGER;
                        // ref - is a MFT rec where attr value is located
                        if (!ReadMftItemInfo(volData, ref, itemInfo))
                        {
                            logger.Error("ReadMftItemInfo() returned false!");
                        }
                    };

                ParseAttrList(pmftrec->IndexMFTRec, attrEntry, attrEntryEnd, currAttr->res.DataSize, CallReadMftItemInfo);
                /*
                assert(attrEntry->AttrType > 0);
                assert(attrEntry->AttrSize > 0);
                assert(attrEntry->AttrType != ATTR_ZERO);
                assert(attrEntry->AttrType != ATTR_END);
                assert(attrEntry->StartVCN == 0); // because first attr in the list is ATTR_STD_INFO
                assert(((uint32_t)(attrEntry->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero

                while (true)
                {
                    // StartVCN should be 0 for all attrs except DATA and ALLOC
                    // ATTR_LIST can contain several ALLOC attributes, first such attr contains StartVCN=0 second - StartVCN>0
                    if ( (attrEntry->AttrType != ATTR_DATA) && (attrEntry->AttrType != ATTR_ALLOC) )  
                    {
                        assert(attrEntry->StartVCN == 0);
                        if (attrEntry->StartVCN != 0)
                            logger.WarnFmt("Looks like we have met incorrect case. StartVCN ({}) <> 0 for {} attribute. MFT Rec ID: {}.", attrEntry->StartVCN, AttrName(attrEntry->AttrType), MFT_REF::toHexString(pmftrec->IndexMFTRec));
                    }

                    if (attrEntry->StartVCN > volData.MaxMFTIndex) 
                        logger.WarnFmt("StartVCN ({}) for {} attribute is greater than max MFT Index ({:#x}).", attrEntry->StartVCN, AttrName(attrEntry->AttrType), volData.MaxMFTIndex);

                    if (attrEntry->ref.sId.low != pmftrec->IndexMFTRec)
                    {
                        // several attrEntries can refer to one MFT record, we want to parse MFT record only once
                        if (visitedMFTRec.IndexOf(attrEntry->ref.sId.low) == -1)
                        {
                            CallReadMftItemInfo(attrEntry->ref);
                            // attrEntry->ref is a MFT rec where attr value is located
                            //if (!ReadMftItemInfo(volData, attrEntry->ref, itemInfo))
                            //{
                            //    logger.Error("ReadMftItemInfo() returned false!");
                            //}
                            visitedMFTRec.AddValue(attrEntry->ref.sId.low);
                        }
                    }

                    attrEntry = (ATTR_LIST_ENTRY*)Add2Ptr(attrEntry, attrEntry->AttrSize);
                    if ((uint8_t*)attrEntry >= attrEntryEnd) break;
                    assert(attrEntry->AttrType > 0);
                    assert(attrEntry->AttrSize > 0);
                    assert(((uint32_t)(attrEntry->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
                    //assert(attrEntry->StartVCN == 0);
                    assert(attrEntry->AttrType != ATTR_ZERO);
                    assert(attrEntry->AttrType != ATTR_END);
                }*/

                logger.Debug("resident ATTR_LIST_ATTR FINISHED");

                break;
            }
            case ATTR_BITMAP: // resident. ATTR_BITMAP can be either resident and non-resident
            {
                if (itemInfo.NonResidentBitmap) logger.Warn("Incorrect case has been met: itemInfo.NonResidentBitmap = true in resident context ");
                itemInfo.NonResidentBitmap = false;

                ATTR_BITMAP_ATTR* bmp = (ATTR_BITMAP_ATTR*)attrValue;
                logger.DebugFmt("ATTR_BITMAP, resident, Size in bytes: {}, Value64: {:#x}", currAttr->res.DataSize, *(uint64_t*)bmp);

                assert((currAttr->res.DataSize & 0x07) == 0); // bitmap data size always multiple of 8

                itemInfo.Node.Bitmap.SetData((uint64_t*)bmp->bitmap, currAttr->res.DataSize >> 3);

                break;
            }
            case ATTR_DATA: // resident. ATTR_DATA can be resident or non-resident
            {
                if (itemInfo.ResidentData)
                    logger.DebugFmt("Looks like two resident ATTR_DATA ('{}') attributes have met in one MFT record: {}.", nameOfAttrA, MFT_REF::toHexString(pmftrec->IndexMFTRec));
                itemInfo.ResidentData = true;

                //std::wstring name(GetAttrName(currAttr, AttrNameOffset), currAttr->AttrNameSize);
                itemInfo.DataStreamNames[nameOfAttrW]++;
                
                logger.DebugFmt("Resident ATTR_DATA size: {}", currAttr->res.DataSize);

                break;
            }
            case ATTR_ALLOC: // resident. ATTR_ALLOC is NON-Resident only. 
            {
                logger.WarnFmt("Warning! Resident ATTR_ALLOC has been met in MFT Rec ID {}!", MFT_REF::toHexString(pmftrec->IndexMFTRec));
                break;
            }
            case ATTR_REPARSE:  // resident. ATTR_REPARSE can be resident or non-resident
            {
                ATTR_REPARSE_POINT* rp = (ATTR_REPARSE_POINT*)attrValue;
                logger.DebugFmt("Resident Reparse Point. Tag: {:#x}, Data Length: {}, MFT Rec ID: {}", rp->reparse_tag, rp->reparse_data_length, MFT_REF::toHexString(pmftrec->IndexMFTRec));
                break;
            }
            case ATTR_SECURE: 
            case ATTR_EA:       // can be resident or non-resident
            case ATTR_EA_INFO:  // can be resident or non-resident
            case ATTR_PROPERTYSET:
            case ATTR_LOGGED_UTILITY_STREAM: // can be resident or non-resident
            {
                logger.DebugFmt("We do not process this attribute. Attr: '{}', Attr Name: '{}', MFT RecID: {}.", AttrName(currAttr->AttrType), nameOfAttrA, MFT_REF::toHexString(pmftrec->IndexMFTRec));
                break;
            }

            default:
                logger.WarnFmt("UNKNOWN Resident attr has been met. Type:{}, Name:'{}', MFT Rec Id:{}", AttrName(currAttr->AttrType), nameOfAttrA, MFT_REF::toHexString(pmftrec->IndexMFTRec));

            } // switch

        }
        else // Attribute is NON-Resident
        {
            logger.DebugFmt("Attr StartVCN: {}",     currAttr->nonres.StartVCN);
            logger.DebugFmt("Attr LastVCN: {}",      currAttr->nonres.LastVCN);
            logger.DebugFmt("Attr RealSize: {}",     currAttr->nonres.RealSize);
            logger.DebugFmt("Attr StreamSize: {}",   currAttr->nonres.StreamSize);
            logger.DebugFmt("Attr AllocatedSize: {}",currAttr->nonres.AllocatedSize);

            // this is rare case when file contains non-initialised portion of data
            // in this case RealSize contains total file size while StreamSize contains size of initialised data (StreamSize<RealSize) 
            if (currAttr->nonres.RealSize != currAttr->nonres.StreamSize)
                logger.DebugFmt("'currAttr.RealSize != currAttr.StreamSize'. ReadlSize: {}, StreamSize: {}, MFT Rec ID: {}", 
                    currAttr->nonres.RealSize, currAttr->nonres.StreamSize, MFT_REF::toHexString(pmftrec->IndexMFTRec));
            
            if (currAttr->nonres.RealSize < currAttr->nonres.StreamSize)
                logger.WarnFmt("'currAttr.RealSize < currAttr.StreamSize'. ReadlSize: {}, StreamSize: {}, MFT Rec ID: {}",
                    currAttr->nonres.RealSize, currAttr->nonres.StreamSize, MFT_REF::toHexString(pmftrec->IndexMFTRec));

            //assert(currAttr->nonres.RealSize == currAttr->nonres.StreamSize);

            switch (currAttr->AttrType)
            {
            case ATTR_DATA: // NON-resident. Can be either resident and non-resident
            {
                if (itemInfo.ResidentData)
                    logger.DebugFmt("Looks like resident DATA attr ('{}') is met in NONresident context. MFT Rec ID: {}", nameOfAttrA, MFT_REF::toHexString(pmftrec->IndexMFTRec));
                itemInfo.ResidentData = false;

                //std::wstring name(GetAttrName(currAttr, AttrNameOffset), currAttr->AttrNameSize);
                itemInfo.DataStreamNames[nameOfAttrW]++;

                logger.DebugFmt("We do not process this attribute except for DataRunsDecode. Attr: ATTR_DATA. Name: '{}'. ", nameOfAttrA);
                
                // decode data runs just for testing purposes
                if (DataRunsDecode(currAttr, itemInfo.Node.DataRuns)) 
                {
                    logger.DebugFmt("NON-Resident ATTR_DATA data runs count: {}, Stream Name: {}.", itemInfo.Node.DataRuns.Count(), nameOfAttrA);
                }
                else
                {
                    //logger.Error("DataRunsDecode finished with ERROR.");
                    break; //DataRunsDecode writes a message into log file in case of an error
                }

                itemInfo.Node.DataRuns.Clear(); // we do not need data runs any more

                break;
            }

            case ATTR_BITMAP: // NON-resident. ATTR_BITMAP can be either resident and non-resident
            {
                if (itemInfo.NonResidentBitmap.has_value())
                    logger.WarnFmt("Incorrect case has been met: Looks like two or more ATTR_BITMAP ('{}') attributes were met in a one MFT record: {}.", nameOfAttrA, MFT_REF::toHexString(pmftrec->IndexMFTRec));
                itemInfo.NonResidentBitmap = true;

                logger.InfoFmt("Non-Resident ATTR_BITMAP ('{}') has been met. MFT Rec ID {}", nameOfAttrA, MFT_REF::toHexString(pmftrec->IndexMFTRec));

                if (!ParseNonresBitmap(volData, currAttr, itemInfo.Node.Bitmap))
                {
                    logger.Error("ParseNonresBitmap finished with error.");
                    break;
                }
                //TODO do we need Bitmap value returned from ParseNonresBitmap?
                break;
            }
            case ATTR_ALLOC: // NON-resident. Only.
            {
                /*if (itemInfo.NonResidentAlloc)
                    logger.WarnFmt("Looks like two non-resident ALLOC attributes ('{}') have met in a single MFT record: {}.", nameOfAttr2, MFT_REF::toHexString(pmftrec->IndexMFTRec));
                itemInfo.NonResidentAlloc = true;*/
                
                //there can be two and more ALLOCATE attributes in one MFT Rec (in total).
                // they will actually be located in child MFT records, base MFT record will contain ATTR_LIST attribute
                //assert(itemInfo.Node.DataRuns.Count() == 0);

                if (!DataRunsDecode(currAttr, itemInfo.Node.DataRuns)) // writes a message into log file in case of an error
                {
                   // logger.Error("DataRunsDecode finished with error.");
                    break;
                }

                // ONLY DECODE DATA RUNS HERE.
                // Processing of data runs is moved below after all atrributes are read from MFT base and child records

                break;
            }
            case ATTR_LIST_ATTR: // NON-resident. ATTR_LIST_ATTR can be either resident and non-resident
            {
                assert(pmftrec->ParentFileRec.Id == 0); // only base records can have ATTR_LIST_ATTR attribute

                if (itemInfo.NonResidentAttrList.has_value())
                    logger.WarnFmt("Incorrect case has been met: Looks like two or more ATTR_LIST_ATTR ('{}') attributes have met in a one MFT record: {}.", nameOfAttrA, MFT_REF::toHexString(pmftrec->IndexMFTRec));
                itemInfo.NonResidentAttrList = true;

                logger.Debug("NON-Resident ATTR_LIST_ATTR START");

                TDataRuns dataRuns;
                if (!DataRunsDecode(currAttr, dataRuns)) // DataRunsDecode writes a message into log file in case of an error
                {
                    break;
                }

                if (dataRuns.Count() > 1)
                    logger.InfoFmt("UNUSUAL case. Non-resident ATTR_LIST_ATTR occupies {} data runs instead one.", dataRuns.Count());

                THArray<uint32_t> visitedMFTRec;
                uint32_t currRun = 0;
                //uint64_t processedAttrSize = 0;

                while (currRun < dataRuns.Count())
                {
                    DATA_RUN_ITEM& rli = dataRuns[currRun];
                    logger.DebugFmt("Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

                    if (rli.len > 1)
                        logger.InfoFmt("UNUSUAL case. Non-resident ATTR_LIST_ATTR datarun item occupies {} LCNs instead of one.", rli.len);

                    auto dataBufSize = rli.len * volData.BytesPerCluster;
                    uint8_t* dataBuf = (uint8_t*)alloca(dataBufSize);

                    if (!ReadClusters(volData, rli.lcn, rli.len, dataBuf)) // ReadClusters writes a message into log file in case of an error
                    {
                        break;
                    }


                    AttrListPred CallReadMftItemInfo = [&itemInfo, &visitedMFTRec, &volData](const MFT_REF& ref)
                        {
                            GET_LOGGER;
                            // ref - is a MFT rec where attr value is located
                            if (!ReadMftItemInfo(volData, ref, itemInfo))
                            {
                                logger.Error("ReadMftItemInfo() returned false!");
                            }
                        };

                    ATTR_LIST_ENTRY* attrEntry = (ATTR_LIST_ENTRY*)dataBuf;

                    //TODO probably we need to parse each cluster separately because end of last attrEntry in cluster#1 does not mean start of first attrEntry in cluster#2
                   
                    uint8_t* attrEntryEnd = dataBuf + dataBufSize;

                    ParseAttrList(pmftrec->IndexMFTRec, attrEntry, attrEntryEnd, currAttr->nonres.RealSize, CallReadMftItemInfo);

                    /*assert(attrEntry->AttrSize > 0);
                    assert(attrEntry->AttrType > 0);
                    assert(((uint32_t)(attrEntry->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
                    assert(attrEntry->AttrType != ATTR_ZERO);
                    assert(attrEntry->AttrType != ATTR_END);

                    while (true)
                    {
                        // StartVCN might be >0 when one attribute does not fit into one MFT record.
                        // This attribute may have very long Data Run list or anything else
                        // In this case ATTR_LIST contains several ATTR_LIST_ENTRY entries for this big attribute.
                        // First entry has StartVCN=0, others - preventry.StartVCN+num_of_vcns_in_preventry_dataruns, etc.
                        // all these entries build up a continious list of VCNs 
                        if (attrEntry->AttrType != ATTR_DATA)  // StartVCN should be 0 for all attrs except ATTR_DATA
                        {
                            assert(attrEntry->StartVCN == 0);
                            if (attrEntry->StartVCN != 0)
                                logger.WarnFmt("Looks like we have met incorrect case. StartVCN({}) <> 0 for {} attribute. MFT Rec ID: {}.", attrEntry->StartVCN, AttrName(attrEntry->AttrType), MFT_REF::toHexString(pmftrec->IndexMFTRec));
                        }

                        if (attrEntry->StartVCN > volData.MaxMFTIndex)
                            logger.WarnFmt("StartVCN ({}) for {} attribute is greater than max MFT Index ({:#x}).", attrEntry->StartVCN, AttrName(attrEntry->AttrType), volData.MaxMFTIndex);

                        // count only first portion of such attributes
                        if (attrEntry->StartVCN == 0)
                        {
                            // attributes in attr list located in a separate cluster may refer back to the base record
                            // because some attributes reside in base mft record and the others in "child" mft record(s)
                            // the attr list attribute itself is located in cluster that is not mft record, it does not contain signature or Fixups values, etc.
                            //TODO may be good idea to add pmftrec->IndexMFTRec into visitedMFTRec as a first item and remove this "if"
                            if (attrEntry->ref.sId.low != pmftrec->IndexMFTRec) // attrs located in current pmftrec either already parsed or will be parsed during usual cycle of parsing 
                            {
                                if (visitedMFTRec.IndexOf(attrEntry->ref.sId.low) == -1) // whether we haven't parsed this MFT record yet
                                {
                                    CallReadMftItemInfo(attrEntry->ref);
                                    // attrEntry->ref is a MFT rec where attr value is located
                                    //if (!ReadMftItemInfo(volData, attrEntry->ref, itemInfo))
                                    //{
                                    //    logger.Error("ReadMftItemInfo() returned false!");
                                    //}
                                    visitedMFTRec.AddValue(attrEntry->ref.sId.low);
                                }
                            }
                        }
                        else // StartVCN is a cluster where attribute portion value is located
                        {
                            logger.WarnFmt("ATTR_LIST_ENTRY.StartVCN <> 0! StartVCN: {}, AttrType: {}, ref: {}, MFT Rec ID: {}", 
                                          attrEntry->StartVCN, AttrName(attrEntry->AttrType), attrEntry->ref.toHexString(), MFT_REF::toHexString(pmftrec->IndexMFTRec));

                            if (attrEntry->StartVCN > volData.MaxMFTIndex)
                                logger.WarnFmt("StartVCN ({}) for {} attribute is greater than MAXimum MFT Index ({}).", attrEntry->StartVCN, AttrName(attrEntry->AttrType), volData.MaxMFTIndex);
                        }
                        
                        processedAttrSize += attrEntry->AttrSize;
                        if (processedAttrSize >= currAttr->nonres.RealSize) break;

                        attrEntry = (ATTR_LIST_ENTRY*)Add2Ptr(attrEntry, attrEntry->AttrSize);
                        if ((uint8_t*)attrEntry >= attrEntryEnd) break;
                        //assert(attrEntry->AttrId > 0);
                        assert(attrEntry->AttrType > 0);
                        assert(attrEntry->AttrSize > 0);
                        assert(((uint32_t)(attrEntry->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
                        assert(attrEntry->AttrType != ATTR_ZERO);
                        assert(attrEntry->AttrType != ATTR_END);
                    }

                    
                    // check again if we finished with ATTR_LIST attribute
                    if ((uint8_t*)attrEntry >= attrEntryEnd)
                    {
                        logger.InfoFmt("Loop is finished by this condition: 'attrEntry >= attrEntryEnd' (end of buffer with clusters). Last Attr: {}, currAttr->nonres.RealSize:", 
                                       AttrName(attrEntry->AttrType), currAttr->nonres.RealSize);
                        break;
                    }*/

                    currRun++;
                }

                logger.Debug("NON-Resident ATTR_LIST_ATTR FINISHED");

                break;
            }
            case ATTR_REPARSE:  // resident. ATTR_REPARSE can be resident or non-resident
            {
                logger.InfoFmt("NON-Resident Reparse Point. StartVCN: {}, LastVCN: {}, RealSize: {}, MFT Rec Id: {}", currAttr->nonres.StartVCN, currAttr->nonres.LastVCN, currAttr->nonres.RealSize, MFT_REF::toHexString(pmftrec->IndexMFTRec));
                break;
            }
            case ATTR_EA: // can be resident or non-resident 
            case ATTR_LOGGED_UTILITY_STREAM: // can be resident and non-resident
            {
                logger.DebugFmt("We do not process this non-resident attribute. Attr: '{}', Name: '{}'.", AttrName(currAttr->AttrType), nameOfAttrA);
                break;
            }
            default:
                logger.WarnFmt("UNKNOWN NON-Resident attribute has been met. Type: {}, Name: {}, MFT Rec Id: {}", AttrName(currAttr->AttrType), nameOfAttrA, MFT_REF::toHexString(pmftrec->IndexMFTRec));


            } //switch
        } //currAttr->NonResidentFlag == 0

        currAttr = (MFT_ATTR_HEADER*)Add2Ptr(currAttr, currAttr->AttrSize);
        assert(pmftrec->FileRecSize > Diff2Ptr(pmftrec, currAttr));

    } while (*((DWORD*)currAttr) != ATTR_END /*0xFFFFFFFF*/);

    // only for base MFT records
    if (isBASERec)
    {
        // once we read all attributes we are ready to process ALLOC data runs
        if (itemInfo.Node.DataRuns.Count() > 0)
        {
            assert(itemInfo.AttrCounters[MakeAttrTypeIndex(ATTR_ALLOC)] > 0);
            //assert(itemInfo.AttrCounters[MakeAttrTypeIndex(ATTR_ALLOC)] == 1);

            // single function but different behaviour of adding files/dirs into lists
            // in other place where ProcessAllocDataRuns called lambda adds found dirs into another list
            if (!ProcessAllocDataRuns(volData, itemInfo.Node, addToFileListPred))
            {
                logger.Error("ProcessAllocDataRuns finished with error.");
                //return is not needed here because node.FileList may contain items from INDEX_ROOT and partially from ALLOCATION
            }
        }

        logger.DebugFmt("---------- FINISHED Reading BASE MFT Record ({}) ---------", MFT_REF::toHexString(pmftrec->IndexMFTRec));
    }
    else
    {
        logger.DebugFmt("---------- FINISHED Reading CHILD MFT Record ({}) ---------", MFT_REF::toHexString(pmftrec->IndexMFTRec));
    }

    return true;
}

// reads all MFT items recursivelly starting from startMftRec.
// if startMftRec is FILE then only info about this file will be read and added to the parameter 'list' (TItemInfoList)
// if startMftRec is DIRECTORY the function will navigate all child items and child items of child items, read all info and add all those items into param 'list'
static bool ReadMftItems(VOLUME_DATA& volData, MFT_REF startMmftRec, uint32_t dirLevel, TItemInfoList& list)
{
    GET_LOGGER;

    ITEM_INFO itemInfo{ 0 };

    if (!ReadMftItemInfo(volData, startMmftRec, itemInfo))
    {
        logger.ErrorFmt("ReadMftItemInfo() finished with error for MFT rec: {:#x}", startMmftRec.sId.low);
        return false;
    }

    list.AddValue(itemInfo);

    for (auto& item : itemInfo.Node.FileList)
    {     
        if (!item.NtfsInternal()) // bypass hidden mft metafiles
        {
            if (dirLevel == 0) std::wcout << item.ciName.c_str() /*<< " [" <<item.Attr.dup.FileSize << "]"*/ << std::endl;

            // reading detailed info about each item (files, directories and reparse points)
            if (!ReadMftItems(volData, item.MFTRef, dirLevel + 1, list))
            {
                logger.ErrorFmt("ReadMftItems() finished with error for MFT Rec ID: {:#x}", item.MFTRef.sId.low);
            }
        }
    }

    itemInfo.Node.Clear(); // list of subitems is not needed any more. save memory.

   return true;
}

// Reads entire disk and prints to console various statistics
// Starts reading from root dir (c:), goes to all subdirs and reads detailed attributes data for each file/dir
// Result is a plain list of all files/dirs wihtout preserving child-parent relationships
// Use this function mostly for collecting detailed statictic about files and their NTFS attributes.
// DOES NOT calculate dir sizes
void ShowVolumeStat(VOLUME_DATA& volData)
{
    GET_LOGGER;

    TItemInfoList itemsList;
    itemsList.SetCapacity(1'000'000); // Expect 1M files and dirs

    MFT_REF startId{0};
    startId.Id = MFT_ROOT_REC_ID;
    
    if (!ReadMftItems(volData, startId, 0, itemsList))
    {
        logger.Error("ReadMftItems() returned false!");
    }


    auto AttrCountGreater9 = std::count_if(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a) { return a.AttrsCount > 9; });
    auto HardLinksGreater9 = std::count_if(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a) { return a.HardLinksCount > 9; });
    auto DirHardLinksEQ1 = std::count_if(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.HardLinksCount == 1; });
    auto DataStreamsCountGreater2 = std::count_if(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a) { return a.DataStreamNames.Count() > 2; });
    auto FilenamesCountGreater13 = std::count_if(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a) { return a.FileNames.Count() > 13; });
    auto FilenamesCountEQ1 = std::count_if(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a) { return a.FileNames.Count() == 1; });
    auto FilenamesCountEQ0 = std::count_if(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a) { return a.FileNames.Count() == 0; });
    auto DirFilenamesCountGreater2 = std::count_if(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.FileNames.Count() > 2; });
    auto DirFilenamesCountEQ1 = std::count_if(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.FileNames.Count() == 1; });
    auto HasNonresAttrList = std::count_if(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a) { return a.NonResidentAttrList; });
    auto HasNonresBitmap = std::count_if(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a) { return a.NonResidentBitmap; });
    auto HasResidentData = std::count_if(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a) { return a.ResidentData; });
    auto ReparsePointsCount = std::count_if(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a) { return a.AttrCounters[MakeAttrTypeIndex(ATTR_REPARSE)] > 0; });
    auto DoesNotHaveDataAttribute = std::count_if(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a) { return a.AttrCounters[MakeAttrTypeIndex(ATTR_DATA)] == 0; }); 
    auto DirsHardLinksGreater2 = std::count_if(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.HardLinksCount > 2; });

    auto maxHardLinks = std::max_element(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) { return a.HardLinksCount < b.HardLinksCount; });
    auto maxAttrs = std::max_element(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) { return a.AttrsCount < b.AttrsCount; });
    auto maxFilenames = std::max_element(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) { return a.FileNames.Count() < b.FileNames.Count(); });
    auto maxDataStreams = std::max_element(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) { return a.DataStreamNames.Count() < b.DataStreamNames.Count(); });

    uint32_t FileNamesTotalSymbols = std::accumulate(itemsList.begin(), itemsList.end(), (uint32_t)0, 
        [](uint32_t acc, ITEM_INFO& x) 
        {
            // find average length of all filenames inside one MFT record
            uint32_t symbols = std::accumulate(x.FileNames.begin(), x.FileNames.end(), (uint32_t)0, [](uint32_t accum, std::wstring& v) { return accum + (uint32_t)v.size(); });
            return acc + symbols/x.FileNames.Count();
        }
    );
    uint32_t FileNamesAverageSymbols = FileNamesTotalSymbols / itemsList.Count(); // average file length in symbols
    uint32_t FileNamesAverageBytes = FileNamesTotalSymbols * sizeof(wchar_t) / itemsList.Count(); // average file length in bytes

    std::cout << std::endl;
    auto dirCount = std::count_if(itemsList.begin(), itemsList.end(), [](ITEM_INFO& a) { return a.IsDir(); });

    std::cout << "Total Items Count: " << toStringSepA(itemsList.Count()) << std::endl;
    std::cout << "Total Dirs Count: " << toStringSepA(dirCount) << std::endl;
    std::cout << "Total Files Count: " << toStringSepA(itemsList.Count() - dirCount) << std::endl << std::endl; // only files 

    std::cout << "Attrs Count > 9: " << AttrCountGreater9 << std::endl;
    std::cout << "Hard links Count > 9: " << HardLinksGreater9 << std::endl;
    std::cout << "Dir Hard links Count = 1: " << DirHardLinksEQ1 << std::endl;
    std::cout << "Data streams Count > 2: " << DataStreamsCountGreater2 << std::endl;
    std::cout << "Filenames Count > 13: " << FilenamesCountGreater13 << std::endl;
    std::cout << "Filenames Count = 1: " << FilenamesCountEQ1 << std::endl;
    std::cout << "Filenames Count = 0: " << FilenamesCountEQ0 << std::endl;
    std::cout << "Dir Filenames Count > 2: " << DirFilenamesCountGreater2 << std::endl;
    std::cout << "Dir Filenames Count = 1: " << DirFilenamesCountEQ1 << std::endl;
    std::cout << "Reparse Points Count: " << ReparsePointsCount << std::endl;
    std::cout << "Have non-resident ATTR_LIST: " << HasNonresAttrList << std::endl;
    std::cout << "Have non-resident BITMAP: " << HasNonresBitmap << std::endl;
    std::cout << "Have resident Data: " << HasResidentData << std::endl;
    std::cout << "DOES NOT have Data attribute: " << DoesNotHaveDataAttribute << std::endl;
    std::cout << "Dirs with Hard Links Count > 2: " << DirsHardLinksGreater2 << std::endl;

    std::cout << "Filenames Average Length: " << FileNamesAverageSymbols << " symbols (" << FileNamesAverageBytes << " bytes)"  << std::endl;

    std::cout << std::format("Max hard links Count: {}, file name: {} (mft red id: {})", (*maxHardLinks).HardLinksCount, wtos((*maxHardLinks).MainName), MFT_REF::toHexString((*maxHardLinks).RecID.sId.low)) << std::endl;
    std::cout << std::format("Max attrs Count: {}, file name: {} (mft rec id: {})", (*maxAttrs).AttrsCount, wtos((*maxAttrs).MainName), MFT_REF::toHexString((*maxAttrs).RecID.sId.low)) << std::endl;
    std::cout << std::format("Max file names Count: {}, file name: {} (mft rec id: {})", (*maxFilenames).FileNames.Count(), wtos((*maxFilenames).MainName), MFT_REF::toHexString((*maxFilenames).RecID.sId.low)) << std::endl;
    std::cout << std::format("Max data streams Count: {}, filename:{} (mft rec id: {})", (*maxDataStreams).DataStreamNames.Count(), wtos((*maxDataStreams).MainName), MFT_REF::toHexString((*maxDataStreams).RecID.sId.low)) << std::endl;

    std::wcout << std::endl << L"Datastream names for "<< (*maxDataStreams).MainName.c_str() << ":" << std::endl;
    for (auto ds : (*maxDataStreams).DataStreamNames)
    {
        if(ds.first.empty())
            std::wcout << L"<empty> - " << ds.second << std::endl;
        else
            std::wcout << ds.first << " - " << ds.second << std::endl;
    }

   /* std::wcout << std::endl << "File names for " << (*maxFilenames).MainName.c_str() << ":" << std::endl;
    for (auto& fn : (*maxFilenames).FileNames)
    {
        std::wcout << fn << std::endl;
    }*/

    std::wcout << "Attribute counts for " << (*maxAttrs).MainName.c_str() << ":" << std::endl;
    for (int i = 1; i < ATTR_TYPE_CNT; i++) // bypass ATTR_ZERO
    {
        std::wcout << AttrTypeNames[i] << " = " <<(*maxAttrs).AttrCounters[i] << std::endl;
    }

    
    /*std::cout << "Sorting... " << std::endl;
    std::sort(itemsList.begin(), itemsList.end());

    std::string filename = "ListMFTFile_sorted.log";
    LogEngine::TFileStream ff(filename);

    ff << toStringSepW(itemsList.Count()) + L" - total";
    ff << toStringSepW(itemsList.Count() - dirCount) + L" - files"; // only files
    ff << toStringSepW(dirCount) + L" - dirs"; // only dirs

    std::cout << "Saving list of files to " << filename << std::endl;

    for (auto& item : itemsList)
    {
        if (IsItemDir(item))
            item.MainName += L'\\';

        ff << item.MainName;
    }*/

    std::cout << "Freeing memory..." << std::endl;
    itemsList.ClearMem();
    std::cout << "Freed" << std::endl;
}

