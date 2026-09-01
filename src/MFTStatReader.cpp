
// this is to remove defines min, max in windows headers because they conflict with std::min std::max 
#define NOMINMAX

#include "Debug.h"
#include <windows.h>
#include <shlwapi.h>
#include <cassert>
#include <numeric>
#include <execution>

#include "strutils/include/string_utils.h"
#include "strutils/include/ci_string.h"
#include "strutils/include/Ticks.h"
#include "Functions.h"
#include "NTFS.h"
#include "Readers.h"


/** 
* @brief Reads all information about one MFT record 
* @details Reads information ONLY about one MFT record refered by mftRecRef. Does NOT go to child items recursively.
* @param mftRecRef MFT record information will be read about
* @param parentMFTRecRef Parent MFT record, used to find appropriate file name in MFT record because of many names and hard links 
* @param itemInfo results of reading MFT record
* @return TErrorCode code.
*/ 
/*TErrorCode TMFTStatCollector::ReadMftItemInfo(MFT_REF mftRecRef, ITEM_INFO& itemInfo)
{
  //  FILE_NAME fn;
  //  fn.MFTRecID = mftRecRef;
    return ReadMftItemInfo(mftRecRef, nullptr, itemInfo);
}*/

TErrorCode TMFTStatCollector::ReadMftItemInfo(MFT_REF mftRecRef, IFILE_NAME* iFileItem, ITEM_INFO& itemInfo)
{
    uint8_t* mftRecBuf = (uint8_t*)alloca(getVolData().BytesPerMFTRec);

    auto res = FLoader.LoadMFTRecord(mftRecRef, mftRecBuf);
    if (res != TErrorCode::Success)
    {
        GET_LOGGER;
        logger.Error("LoadMFTRecord finished with error.");
        return res;
    }
    else
    {
        auto mftRec = (MFT_FILE_RECORD*)mftRecBuf;
        assert(mftRecRef.sId.low == mftRec->IndexMFTRec);

        // for BASE records assert below should be valid
        // for CHILD records it is always NOT valid because fileItem->MFTRecID is a base record ID
        if (iFileItem && (mftRec->ParentFileRec.Id == 0)) 
            assert(iFileItem->MFTRecID.Id == mftRecRef.Id);

        return ReadMftItemInfoBuf(mftRec, iFileItem, itemInfo);
    }
}

/*TErrorCode TMFTStatCollector::ReadMftItemInfoBuf(MFT_FILE_RECORD* mftRec, ITEM_INFO& itemInfo)
{
   // FILE_NAME fn;
   // fn.MFTRecID.Id = mftRec->IndexMFTRec;
    return ReadMftItemInfoBuf(mftRec, nullptr, itemInfo);
}*/

TErrorCode TMFTStatCollector::ReadMftItemInfoBuf(MFT_FILE_RECORD* mftRec, IFILE_NAME* iFileItem, ITEM_INFO& itemInfo)
{
    GET_LOGGER;

    AddFileAttrPred addToFileListPred = [&itemInfo](const ATTR_FILE_NAME* attr, const MFT_REF& ref)
        {
            std::wstring wnm(GetFName(attr), attr->FileNameLen);
            
            itemInfo.Node.FileList.AddValue({convert_string<ci_string::value_type>(wnm).c_str(), *attr, ref});
        };

    AttrListPred callReadMftItemInfoPred = [this, iFileItem, &itemInfo](const MFT_REF& ref)
        {
            //auto tmpFileItem = fileItem;
            //tmpFileItem.MFTRecID = ref;

            // ref - is a child MFT rec where attr value is located
            auto res = ReadMftItemInfo(ref, iFileItem, itemInfo);
            if (res != TErrorCode::Success) // ReadMftItemInfo writes message to log file in case of an error
            {
                //do nothing, continue executing
                GET_LOGGER;
                logger.Error("ReadMftItemInfo() returned false!");
            }

            return res;
        };

    //TODO this is the same predicate code as in MFTSearchReader.cpp. Think how to avoid duplication
    ProcessiBlocksPred processAllocPred = [this, &addToFileListPred](uint8_t* dataBuf, uint64_t VCN, uint64_t LCN)
        {
            auto allocIndex = (INDEX_BUFFER*)dataBuf;

            // read items only if Index Block starts from correct signature INDX
            // sometimes fully empty (filled with zero) clusters present in run list without INDX signature
            if (ntfs_is_indx_recp(allocIndex->RecHeader.Signature))
            {
                assert(VCN == allocIndex->vcn);
                UNREFERENCED_PARAMETER(VCN);
                
                auto pihdr = &(allocIndex->ihdr);
                GetFileList(pihdr, addToFileListPred);
            }
            else
            {
                GET_LOGGER;
                uint8_t* sign = allocIndex->RecHeader.Signature;
                logger.WarnFmt("Signature 'INDX' has not been found in Index Block LCN {}. Signature found: {}{}{}{}", LCN, sign[0], sign[1], sign[2], sign[3]);
            }
        };

    // This record is NOT in use. Does not contain info about any file. Do not parse it because it can contain any garbage.
    if ((mftRec->Flags & MFT_FLAG_IN_USE) == 0) 
    {
        logger.WarnFmt("Warn! Record is not in use. MFT Rec ID: {}", MFT_REF::toHexString(mftRec->IndexMFTRec));
        return TErrorCode::MFTRecordNotInUse;
    }

    //fixup array must go right after IndexMFTRec
    assert(offsetof(MFT_FILE_RECORD, IndexMFTRec) + sizeof(mftRec->IndexMFTRec) == mftRec->RecHeader.FixupOffset);
    assert(ntfs_is_file_recp(mftRec->RecHeader.Signature));
    assert(mftRec->FileRecSize > mftRec->FirstAttrOffset);
    assert(mftRec->FileRecSize <= mftRec->AllocFileRecSize);
    assert(mftRec->AllocFileRecSize == getVolData().BytesPerMFTRec);
    assert((mftRec->Flags & MFT_FLAG_IN_USE) > 0);
    assert(mftRec->FirstAttrOffset < mftRec->AllocFileRecSize);

    bool isBASERec = mftRec->ParentFileRec.Id == 0;

    // whether we are reading base MFT record or child one
    if (isBASERec)
    {
        // we are reading base record
        logger.Debug("\n---------- BASE MFT Record ---------");
        assert(itemInfo.MFTRecID.Id == 0);
        //itemInfo.MFTRecID.Id = 0;
        itemInfo.MFTRecID.Id = mftRec->IndexMFTRec;
        itemInfo.HardLinksCount = mftRec->HardLinksCnt;
    }
    else
    {
        // we are reading child record refered by ATTR_LIST_ATTR attribute
        logger.Debug("\n---------- CHILD MFT Record ---------");
        // in most cases assert below should work without if
        // the only case when if needed - user asked to show info about certain MFT Rec and this rec is child rec.
        if(iFileItem) assert(itemInfo.MFTRecID.Id != 0);
    }

    if (logger.ShouldLog(LogEngine::llDebug))
    {
        logger.DebugFmt("MFT Rec Signature:     '{}'", std::string((char*)mftRec->RecHeader.Signature, 4));
        logger.DebugFmt("MFT Rec ID:             {}", MFT_REF::toHexString(mftRec->IndexMFTRec));
        logger.DebugFmt("MFT Rec Sequence:       {}", mftRec->SeqNum);
        logger.DebugFmt("MFT Log Seq Number:     {}", mftRec->RecHeader.LogFileSeqNum);
        logger.DebugFmt("MFT Parent Rec ID:      {} {}", mftRec->ParentFileRec.toHexString(), mftRec->ParentFileRec.Id > 0 ? " CHILD" : "BASE");
        logger.DebugFmt("MFT Hard Links Count:   {} {}", mftRec->HardLinksCnt, mftRec->ParentFileRec.Id > 0 ? " (may be inaccurate for child records)" : "");
        logger.DebugFmt("MFT Rec Size:           {}", mftRec->FileRecSize);
        logger.DebugFmt("MFT Allocated Rec Size: {} ", mftRec->AllocFileRecSize);
    }

    switch (mftRec->Flags)
    {
    case MFT_FLAG_IN_USE: logger.Debug("MFT Rec Type:          'IN USE' (file or anything else)"); break;
    case MFT_FLAG_IS_DIRECTORY: logger.Warn("(!) MFT Rec Type:       DELETED Directory - unusual case"); break;
    case MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY: logger.DebugFmt("MFT Rec Type:          'IN USE DIRECTORY' {:#x}", (uint16_t)mftRec->Flags); break;
    default:
        logger.WarnFmt("MFT Rec Type: UNKNOWN {:#x}", (uint16_t)mftRec->Flags);
    }

    TErrorCode result = TErrorCode::Success;

    MFT_ATTR_HEADER* currAttr = (MFT_ATTR_HEADER*)Add2Ptr(mftRec, mftRec->FirstAttrOffset);

    ATTR_STD_INFO5* stdinfo = nullptr;
    int attrOrderNum = 1;
    do  // reading all attributes in a loop
    {
        assert(currAttr->AttrSize > 0);
        assert(currAttr->AttrSize < mftRec->FileRecSize);

        logger.DebugFmt("\n********** #{} Attribute ({} {:#x}) **********", attrOrderNum++, AttrName(currAttr->AttrType), (uint32_t)currAttr->AttrType);
        logger.Debug(currAttr->NonResidentFlag == ATTR_FLAG_NONRESIDENT ? "Attr Type:          NON-RESIDENT" : "Attr Type:          RESIDENT");
        logger.DebugFmt("Attr ID:            {}", currAttr->AttrID);
        logger.DebugFmt("Attr Size:          {}", currAttr->AttrSize);
        logger.DebugFmt("Attr Flags:         {}", currAttr->Flags);

        std::wstring nameOfAttrW = STREAM_NONAME_W;
        std::string nameOfAttrA = STREAM_NONAME;
        if (currAttr->AttrNameSize > 0) // if attr has a name - show it
        {
            nameOfAttrW.assign(GetAttrName(currAttr, AttrNameOffset), currAttr->AttrNameSize);
            nameOfAttrA = wtos(nameOfAttrW);
            logger.DebugFmt("Attr Name:         '{}'", nameOfAttrA);
        }
        
        // all attributes except for ATTR_FILENAME, ATTR_DATA and ATTR_LOGGED_UTILITY_STREAM must have only single instance in one MFT record.
        // also there can be more than one ATTR_ALLOC attribute because folders may have huge number of files and Data Runs in ATTR_ALLOC attribute can be long
        if ((currAttr->AttrType != ATTR_FILENAME) && (currAttr->AttrType != ATTR_DATA) &&
            (currAttr->AttrType != ATTR_LOGGED_UTILITY_STREAM) && (currAttr->AttrType != ATTR_ALLOC) &&
            (itemInfo.AttrCounters[MATI(currAttr->AttrType)] > 0))
            logger.InfoFmt("Looks like two and more {} ({}) attributes have found in MFT Rec ID: {}", 
                      AttrName(currAttr->AttrType), nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));

        
        if (currAttr->NonResidentFlag == ATTR_FLAG_RESIDENT) // attribute is RESident
        {
            itemInfo.AttrCounters[MATI(currAttr->AttrType)]++;
            itemInfo.AttrsCount++;

            logger.DebugFmt("Attr Indexed:       {}", currAttr->res.IndexedFlag);

            assert(currAttr->res.DataSize + currAttr->res.DataOffset <= currAttr->AttrSize);
            uint8_t* attrValue = Add2Ptr(currAttr, currAttr->res.DataOffset);
            
            switch (currAttr->AttrType)
            {
            case ATTR_STD_INFO: // Resident. Only.
            {
                stdinfo = (ATTR_STD_INFO5*)attrValue;

                assert(isBASERec); //STD_INFO cannot be in child MFT record
                assert((stdinfo->FileAttrib & FILE_ATTRIBUTE_NORMAL) == 0);// check that NORMAL bit is always zero

                if (logger.ShouldLog(LogEngine::llDebug))
                {
                    /*logger.Debug(FileDateToString("Created: ", stdinfo->CreateTime));
                      logger.Debug(FileDateToString("Modified: ", stdinfo->ModifyTime));
                      logger.Debug(FileDateToString("LastAccess: ", stdinfo->LastAccessTime));
                    */    
                    logger.DebugFmt("File Attrib:        {:#x} {}", stdinfo->FileAttrib, FormatFileAttributes(stdinfo->FileAttrib));
                    logger.DebugFmt("Version Number:     {}", stdinfo->VersionNum);
                    logger.DebugFmt("Max Version num:    {}", stdinfo->max_ver_num);
                    logger.DebugFmt("Class Id:           {}", stdinfo->class_id);
                    logger.DebugFmt("Owner Id:           {}", stdinfo->owner_id);
                    logger.DebugFmt("USN:                {:#x}", stdinfo->usn);
                    logger.DebugFmt("Security ID:        {}", stdinfo->security_id);
                    logger.DebugFmt("Quota Charged:      {}", stdinfo->quota_charged);
                }

                break;
            }
            case ATTR_FILENAME: // Resident. Only.
            {
                ATTR_FILE_NAME* fname = (ATTR_FILE_NAME*)attrValue;
                std::wstring name(GetFName(fname), fname->FileNameLen);
                itemInfo.FileNames.AddValue({ convert_string<char_t>(name).c_str(), *fname, fname->ParentDir });

                assert(fname->NameType <= FILE_NAME_UNICODE_AND_DOS);

                
                if (itemInfo.MainName.size() == 0)
                    if (fname->NameType != FILE_NAME_DOS)
                        if (iFileItem == nullptr) // take the first name non-DOS name
                        {
                            itemInfo.MainName = name;
                            itemInfo.FileAttrib = fname->dup.FileAttrib;
                            itemInfo.ParentDir = fname->ParentDir;
                        }
                        else
                        {
                            // take name that referers exactly to parent directory
                            //TODO this may not work in some cases e.g. when two hard linked files are in one directory
                            // they will have equal ParentDir but diff names
                            if (fname->ParentDir.sId.low == iFileItem->Attr.ParentDir.sId.low)
                            {
                                itemInfo.MainName = convert_string<wchar_t>(iFileItem->ciName);
                                itemInfo.FileAttrib = iFileItem->Attr.dup.FileAttrib;
                                itemInfo.ParentDir = iFileItem->Attr.ParentDir;
                                assert(iFileItem->Attr.ParentDir.Id == fname->ParentDir.Id);
                            }

                        }
            
                assert((fname->dup.FileAttrib & FILE_ATTRIBUTE_NORMAL) == 0);// check that NORMAL bit is always zero

                if (logger.ShouldLog(LogEngine::llDebug))
                {
                    if(!isBASERec) logger.DebugFmt("Where (child rec):  {}", mftRec->IndexMFTRec);
                    logger.DebugFmt("File Parent Rec ID: {}", fname->ParentDir.toHexString());
                    logger.DebugFmt("File Name Type:     {:#x} '{}' ", fname->NameType, FileNameTypes[fname->NameType]);
                    logger.DebugFmt("File DOS Attrib:    {:#x} {}", fname->dup.FileAttrib, FormatFileAttributes(fname->dup.FileAttrib));
                    logger.DebugFmt("File Name:         '{}'", wtos(name));
                    logger.DebugFmt("File Size:          {}", fname->dup.FileSize);

                    /*logger.Debug(FileDateToString("Created: ", fname->dup.CreateTime));
                    logger.Debug(FileDateToString("Modified: ", fname->dup.ModifyTime));
                    logger.Debug(FileDateToString("LastAccess: ", fname->dup.LastAccessTime));
                    */

                }

                break;
            }
            case ATTR_ROOT: // Resident. ATTR_ROOT is resident only.
            {
                // $SDH INDEX_ROOT attribute name is related to storing and searching security descriptors (usually in MFT=0x09 $Secure).

                ATTR_INDEX_ROOT* indexR = (ATTR_INDEX_ROOT*)attrValue;
                uint32_t BytesPerCluster = getVolData().BytesPerCluster;
                if (indexR->IndexBlockSize >= BytesPerCluster)
                {
                    assert((indexR->IndexBlockSize % BytesPerCluster) == 0); 
                    assert(indexR->IndexBlockClst == indexR->IndexBlockSize / BytesPerCluster);
                }
                else
                {
                    assert((BytesPerCluster % indexR->IndexBlockSize) == 0); 
                    assert(indexR->IndexBlockClst == indexR->IndexBlockSize / getVolData().BytesPerSector); // (*) here should be BytesPerSector
                }
                
                assert(indexR->ihdr.Flags < 2); // 0 - Small Dir, 1- Big Dir

                itemInfo.Node.IndexBlockSize = indexR->IndexBlockSize; // need this value for further processing ALLOC Data Runs

                auto pihdr = &(indexR->ihdr);
                assert(pihdr->Used == pihdr->Allocated); // for resident ATTR_ROOT attribute values in these fields are euqal

                if (logger.ShouldLog(LogEngine::llDebug))
                {
                    logger.DebugFmt("IndexRoot Indexed Attr Type: {} {:#x}", AttrName(indexR->AttrType), (uint32_t)indexR->AttrType);
                    logger.DebugFmt("IndexRoot Collation Rule:    {} ({:#x})", CollRuleName((uint32_t)indexR->Rule), (uint32_t)indexR->Rule);
                    logger.DebugFmt("IndexRoot Dir Type:          {} ({:#x})", indexR->ihdr.Flags == 0 ? "SMALL DIR" : "BIG DIR", indexR->ihdr.Flags);
                    logger.DebugFmt("IndexRoot IndexBlockSize:    {}", indexR->IndexBlockSize);
                    logger.DebugFmt("IndexRoot IndexBlockClst:    {}", indexR->IndexBlockClst);
                    logger.DebugFmt("IHDR Used Bytes:             {}", pihdr->Used);
                    logger.DebugFmt("IHDR Allocated Bytes:        {}", pihdr->Allocated);
                }

                assert(itemInfo.Node.FileList.Count() == 0);

                // optimization: preparing for adding many files into FileList for BIG DIRs, to reduce number of memory re-allocations
                if(indexR->ihdr.Flags != 0)
                    itemInfo.Node.FileList.SetCapacity(FILE_LIST_DEF_SIZE);

                //$O exists in $Quota, $ObjId special files, $R exists in $Reparse
                if (nameOfAttrA == "$I30")
                {
                    assert(indexR->AttrType == ATTR_FILENAME);
                    assert(indexR->Rule == COLLATION_RULE::FILENAME);

                    GetFileList(pihdr, addToFileListPred);
                }
                else
                {
                    logger.InfoFmt("Resident ATTR_ROOT has non standard attribute name '{}' (standard name is '$I30'). BYPASSing this attribute. MFT Rec ID: {}",
                        nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));
                }
                
                break;
            }
            case ATTR_LIST_ATTR: // Resident. ATTR_LIST_ATTR can be either resident or non-resident
            {
                assert(isBASERec); // only base records can have ATTR_LIST_ATTR attribute

                if (itemInfo.NonResidentAttrList.has_value()) 
                    logger.WarnFmt("Incorrect case has been met: Looks like two or more ATTR_LIST_ATTR ('{}') attributes present in a one MFT Record: {}.", 
                               nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));
                itemInfo.NonResidentAttrList = false;

                logger.Debug("[Resident ATTR_LIST_ATTR] - START PARSING");

                THArray<MFTRecIndex> visitedMFTRec;
                visitedMFTRec.AddValue(mftRec->IndexMFTRec);

                ATTR_LIST_ENTRY* attrEntry = (ATTR_LIST_ENTRY*)attrValue;
                uint8_t* attrEntryEnd = Add2Ptr(currAttr, currAttr->AttrSize);
                uint64_t processedAttrSize = 0;

                result = ParseAttrList(mftRec->IndexMFTRec, attrEntry, attrEntryEnd, currAttr->res.DataSize, processedAttrSize, visitedMFTRec, callReadMftItemInfoPred);
                if (result != TErrorCode::Success)
                {
                    logger.Error("ParseAttrList returned error.");
                    return result;
                }
                
                logger.Debug("[Resident ATTR_LIST_ATTR] - FINISHED PARSING");

                break;
            }
            case ATTR_BITMAP: // Resident. ATTR_BITMAP can be either resident and non-resident
            {
                if (itemInfo.NonResidentBitmap) logger.Warn("Incorrect case has been met: itemInfo.NonResidentBitmap = true in resident context ");
                itemInfo.NonResidentBitmap = false;

                ATTR_BITMAP_ATTR* bmp = (ATTR_BITMAP_ATTR*)attrValue;
                logger.DebugFmt("ATTR_BITMAP, resident, Size in bytes: {}, Value64: {:#x}", currAttr->res.DataSize, *(uint64_t*)bmp);

                assert((currAttr->res.DataSize & 0x07) == 0); // bitmap data size always multiple of 8
                assert(itemInfo.Node.Bitmap.Count() == 0);
                assert((currAttr->res.DataSize >> 3) > 0);

                itemInfo.Node.Bitmap.SetData((uint64_t*)bmp->bitmap, currAttr->res.DataSize >> 3);

                break;
            }
            case ATTR_DATA: // Resident. ATTR_DATA can be resident or non-resident
            {
                logger.DebugFmt("Attr ATTR_DATA Data Size: {}", currAttr->res.DataSize);

                // "Zone.Identifier" is often a second data attribute for the file
                //if (itemInfo.ResidentData && (nameOfAttrA != "Zone.Identifier"))
                //    logger.InfoFmt("Looks like two resident ATTR_DATA attributes have met in one MFT record: {}, Second Data Attr Name: '{}' ", 
                //              MFT_REF::toHexString(mftRec->IndexMFTRec), nameOfAttrA);
                itemInfo.HasResidentDataAttr = true;

                // each stream name can be met only once
                assert(!itemInfo.DataStreamNames.IfExists(nameOfAttrW));
                itemInfo.DataStreamNames.SetValue(nameOfAttrW, std::nullopt); // for resident - add nullopt instead of TDataRuns instance
               
                break;
            }
            case ATTR_ID: // Resident. 
            {
                ATTR_OBJECT_ID* objID = (ATTR_OBJECT_ID*)attrValue;
                constexpr const uint BUF_SZ = 100;
                std::wstring buf(BUF_SZ, 0);

                if (!StringFromGUID2(objID->ObjId, buf.data(), BUF_SZ))
                    logger.Error("Error. ATTR_OBJECT_ID. StringFromGUID2 has failed 1.");
                logger.DebugFmt("Attr Object ID:       {}", wtos(buf));

                if (currAttr->AttrSize > 16) //0x10
                {
                    if (!StringFromGUID2(objID->BirthVolumeId, buf.data(), BUF_SZ))
                        logger.Error("Error. ATTR_OBJECT_ID. StringFromGUID2 has failed 2.");
                    logger.DebugFmt("Attr Birth Volume ID: {}", wtos(buf));


                    if (currAttr->AttrSize > 32) //0x20
                    {
                        if (!StringFromGUID2(objID->BirthObjectId, buf.data(), BUF_SZ))
                            logger.Error("Error. ATTR_OBJECT_ID. StringFromGUID2 has failed 3.");
                        logger.DebugFmt("Attr Birth Object ID: {}", wtos(buf));

                        if (currAttr->AttrSize > 48) //0x30
                        {
                            if (!StringFromGUID2(objID->DomainId, buf.data(), BUF_SZ))
                                logger.Error("Error. ATTR_OBJECT_ID. StringFromGUID2 has failed 4.");
                            logger.DebugFmt("Attr Domain ID:       {}", wtos(buf));
                        }
                    }
                }

                break;
            }
            case ATTR_ALLOC: // Resident. ATTR_ALLOC is NON-Resident only. 
            {
                logger.WarnFmt("Warning! Resident ATTR_ALLOC has been met in MFT Rec ID: {}!", MFT_REF::toHexString(mftRec->IndexMFTRec));
                break;
            }
            case ATTR_REPARSE: // Resident. ATTR_REPARSE can be resident or non-resident
            {
                ATTR_REPARSE_POINT* rp = (ATTR_REPARSE_POINT*)attrValue;
                logger.DebugFmt("Resident Reparse Point. Tag: {:#x}, Data Length: {}, MFT Rec ID: {}", 
                         rp->ReparseTag, rp->ReparseDataLength, MFT_REF::toHexString(mftRec->IndexMFTRec));
                break;
            }
            case ATTR_LABEL:
            {
                std::wstring label((wchar_t*)attrValue, currAttr->res.DataSize/sizeof(wchar_t));
                logger.InfoFmt("Label: '{}'", wtos(label));
                break;
            }
            case ATTR_VOL_INFO:
            {
                VOLUME_INFO* volInfo = (VOLUME_INFO*)attrValue;
                logger.InfoFmt("Volume Major Ver: {}", volInfo->MajorVer);
                logger.InfoFmt("Volume Minor Ver: {}", volInfo->MinorVer);
                logger.InfoFmt("Volume Flags:     {}", volInfo->Flags);
                break;
            }
            case ATTR_SECURE: 
            case ATTR_EA:       // can be resident or non-resident
            case ATTR_EA_INFO:  // can be resident or non-resident
            case ATTR_PROPERTYSET:
            case ATTR_LOGGED_UTILITY_STREAM: // can be resident or non-resident
            {
                logger.DebugFmt("We do not process this attribute. Attr: '{}', Attr Name: '{}', MFT Rec ID: {}.", 
                         AttrName(currAttr->AttrType), nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));
                break;
            }

            default:
                logger.WarnFmt("UNKNOWN Resident attr has been met. Type:{}, Name:'{}', MFT Rec ID: {}", 
                       AttrName(currAttr->AttrType), nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));

            } // switch

        }
        else // Attribute is NON-Resident
        {
            // do NOT count one attribute divided into several MFT records because of its size 
            if (currAttr->nonres.StartVCN == 0)
            {
                itemInfo.AttrCounters[MakeAttrTypeIndex(currAttr->AttrType)]++;
                itemInfo.AttrsCount++;
            }

            if (logger.ShouldLog(LogEngine::llDebug))
            {
                logger.DebugFmt("Attr StartVCN:      {}", toStringSepA(currAttr->nonres.StartVCN));
                logger.DebugFmt("Attr LastVCN:       {}", toStringSepA(currAttr->nonres.LastVCN));
                logger.DebugFmt("Attr RealSize:      {}", toStringSepA(currAttr->nonres.RealSize));
                logger.DebugFmt("Attr StreamSize:    {}", toStringSepA(currAttr->nonres.StreamSize));
                logger.DebugFmt("Attr Allocated Size:{}", toStringSepA(currAttr->nonres.AllocatedSize));

                if (currAttr->nonres.CompressionUnitSize > 0) // for compressed files
                {
                    logger.DebugFmt("Attr CompressionUnitSize: {} clusters", toStringSepA(2 << currAttr->nonres.CompressionUnitSize));
                    logger.DebugFmt("Attr CompressedSize:      {} (multiple of the cluster size)", toStringSepA(currAttr->nonres.CompressedSize));
                }

                // this is rare case when file contains non-initialised portion of data
                // in this case RealSize contains total file size while StreamSize contains size of initialised data (StreamSize<RealSize) 
                if (currAttr->nonres.RealSize != currAttr->nonres.StreamSize)
                    logger.DebugFmt("Rare case has been met when file contains non-initialised portion of data (RealSize != StreamSize). ReadlSize: {}, StreamSize: {}, MFT Rec ID: {}",
                        currAttr->nonres.RealSize, currAttr->nonres.StreamSize, MFT_REF::toHexString(mftRec->IndexMFTRec));
            }

            if (currAttr->nonres.RealSize < currAttr->nonres.StreamSize)
                logger.WarnFmt("'currAttr.RealSize < currAttr.StreamSize'. RealSize: {}, StreamSize: {}, MFT Rec ID: {}",
                    currAttr->nonres.RealSize, currAttr->nonres.StreamSize, MFT_REF::toHexString(mftRec->IndexMFTRec));

            switch (currAttr->AttrType)
            {
            case ATTR_DATA: // NON-resident. Can be either resident and non-resident
            {
                itemInfo.HasNonResidentDataAttr = true;

                logger.DebugFmt("ATTR_DATA. We do not process this attribute except for decoding Data Runs. Attr Name: '{}'. ", nameOfAttrA);
                
                // for big data runs we can come here several times when one file Data Runs are split between several MFT records.
                // itemInfo.Node.DataRuns will accumulate all data runs from all parts.
                if (TErrorCode::Success != DecodeDataRuns(currAttr, itemInfo.Node.DataRuns)) // DataRunsDecode writes a message into log file in case of an error
                {
                    break; // our further processing does not depend on successfull decoding ATTR_DATA Data Runs, therefore just do break here.
                }

                itemInfo.DataStreamNames.SetValue(nameOfAttrW, itemInfo.Node.DataRuns);

                // save data runs count only for the main (with empty name) data attribute (main data stream)
                if (nameOfAttrA == STREAM_NONAME)
                {
                    uint64_t lcnCnt = 0;
                    for (auto& rli : itemInfo.Node.DataRuns) lcnCnt += rli.len;
                    itemInfo.DataLCNsCount = lcnCnt;

                    // if Data Runs do not fit into one MFT record, severral "extents" (child MFT records) created
                    // In all such child records RealSize=StreamSize=AllocatedSize=0
                    /*if (currAttr->nonres.RealSize != 0)
                    {
                        if (currAttr->nonres.RealSize > lcnCnt * getVolData().BytesPerCluster)
                            logger.WarnFmt("RealSize size is greater than the total size of all LCNs allocated for this file. RealSize: {}, Allocated LCNs Size: {}, MFT Rec ID: {}",
                                toStringSepA(currAttr->nonres.RealSize), toStringSepA(lcnCnt * getVolData().BytesPerCluster), MFT_REF::toHexString(mftRec->IndexMFTRec));

                        if (currAttr->nonres.RealSize < (lcnCnt - 1) * getVolData().BytesPerCluster)
                            logger.WarnFmt("RealSize size is significantly smaller than the total size of all LCNs allocated for this file. RealSize: {}, Allocated LCNs Size: {}, MFT Rec ID: {}",
                                toStringSepA(currAttr->nonres.RealSize), toStringSepA(lcnCnt * getVolData().BytesPerCluster), MFT_REF::toHexString(mftRec->IndexMFTRec));
                    }*/
                }

                break;
            }

            case ATTR_BITMAP: // NON-resident. ATTR_BITMAP can be either resident and non-resident
            {
                logger.InfoFmt("Non-Resident ATTR_BITMAP ('{}') has been met. MFT Rec ID {}", nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));

                if (itemInfo.NonResidentBitmap.has_value())
                    logger.WarnFmt("Incorrect case has been met: Looks like two or more ATTR_BITMAP ('{}') attributes were met in a one MFT record: {}.", 
                              nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));
                itemInfo.NonResidentBitmap = true;

                assert(itemInfo.Node.Bitmap.Count() == 0);

                // most MFT record have BITMAP attribute with '$I30' name
                // however special MFT records (#0...#33) have BITMAP attribute with other names
                // special records have the following attr names: $O, $R, <empty> (for $MFT file record)
                if (nameOfAttrA != "$I30")
                    logger.InfoFmt("Non-Resident ATTR_BITMAP has non standard attribute name '{}' while standard name is '$I30'. MFT Rec ID {}", 
                              nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));

                // all non-Internal BITMAP attrs have name '$I30'
                if (mftRec->IndexMFTRec >= FLoader.GetMetaFilesCount()) assert(nameOfAttrA == "$I30");

                if (FProcessNonResAttr)
                {
                    result = ParseNonresBitmap(currAttr, itemInfo.Node.Bitmap);
                    if (result != TErrorCode::Success)
                    {
                        logger.Error("ParseNonresBitmap finished with error.");
                        return result; //break;
                    }
                }
                else
                {
                    logger.Info("[NON-Resident ATTR_BITMAP] - We have been told to DO NOT process non-resident attributes.");

                    // just decode Data Runs without any further processing
                    TDataRuns dataRuns;
                    result = DecodeDataRuns(currAttr, dataRuns); // DataRunsDecode writes a message into log file in case of an error
                    if (result != TErrorCode::Success) 
                        return result;
                }

                break;
            }
            case ATTR_ALLOC: // NON-resident. Only.
            {
                /*if (itemInfo.NonResidentAlloc)
                    logger.WarnFmt("Looks like two non-resident ALLOC attributes ('{}') have met in a single MFT record: {}.", nameOfAttr2, MFT_REF::toHexString(pmftrec->IndexMFTRec));
                itemInfo.NonResidentAlloc = true;*/
              
                // sometimes one ATTR_LIST_ATTR list may contain two and more ATTR_ALLOC attributes
                // it means we come here two times during parsing one MFT record with such ATTR_LIST_ATTR 
                if (itemInfo.Node.DataRuns.Count() > 0)
                {
                    assert(!isBASERec); // base record cannot contain two ATTR_ALLOC attributes, while child records referred by ATTR_LIST can
                    logger.InfoFmt("Multiple ATTR_ALLOC attributes have met. node.DataRuns.Count(): {}, Child MFT Rec ID: {}, Base MFT Rec ID: {}",
                        itemInfo.Node.DataRuns.Count(), MFT_REF::toHexString(mftRec->IndexMFTRec), mftRec->ParentFileRec.toHexString());
                }
                else // Dataruns.Count() == 0
                {
                    // optimization: processing first ALLOC -> call SetCapacity
                    itemInfo.Node.DataRuns.SetCapacity(DATA_RUNS_DEF_SIZE);
                }
                    
                result = DecodeDataRuns(currAttr, itemInfo.Node.DataRuns); // writes a message into log file in case of an error
                if (result != TErrorCode::Success) 
                {
                    return result; //break;
                }

                // ONLY DECODE DATA RUNS HERE.
                // Processing of data runs is done below after all atrributes are read from MFT base and child records

                break;
            }
            case ATTR_LIST_ATTR: // NON-resident. ATTR_LIST_ATTR can be either resident and non-resident
            {
                logger.Debug("[ATTR_LIST Non-Resident] - START PARSING");

                assert(isBASERec); // only base records can have ATTR_LIST_ATTR attribute

                if (itemInfo.NonResidentAttrList.has_value())
                    logger.WarnFmt("Incorrect case has been met: Looks like two or more ATTR_LIST ('{}') attributes have met in a one MFT record: {}.", 
                              nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));
                itemInfo.NonResidentAttrList = true;

                assert(currAttr->nonres.RealSize == currAttr->nonres.StreamSize);

                if (FProcessNonResAttr)
                {
                    result = ParseNonresAttrList(mftRec->IndexMFTRec, currAttr, callReadMftItemInfoPred);
                    if (result != TErrorCode::Success)
                    {
                        logger.Error("ParseNonresAttrList returned error.");
                        return result;
                    }
                }
                else
                {
                    logger.Info("[ATTR_LIST Non-Resident] - We have been told to DO NOT process non-resident attributes.");
                    
                    // just decode Data Runs without any further processing
                    TDataRuns dataRuns;
                    result = DecodeDataRuns(currAttr, dataRuns);
                    if (result != TErrorCode::Success) // DataRunsDecode writes a message into log file in case of an error
                        return result;
                }

                logger.Debug("[ATTR_LIST Non-Resident] - FINISHED PARSING");

                break;
            }
            case ATTR_REPARSE:  // NON-Resident. ATTR_REPARSE can be resident or non-resident
            {
                logger.InfoFmt("NON-Resident Reparse Point. StartVCN: {}, LastVCN: {}, RealSize: {}, MFT Rec Id: {}", 
                       currAttr->nonres.StartVCN, currAttr->nonres.LastVCN, currAttr->nonres.RealSize, MFT_REF::toHexString(mftRec->IndexMFTRec));
                break;
            }
            case ATTR_EA: // NON-Resident. Can be resident or non-resident 
            case ATTR_LOGGED_UTILITY_STREAM: // can be resident and non-resident
            {
                logger.DebugFmt("We do not process this non-resident attribute. Attr: '{}', Name: '{}'.", AttrName(currAttr->AttrType), nameOfAttrA);
                break;
            }
            default:
                logger.WarnFmt("UNKNOWN NON-Resident attribute has been met. Type: {}, Name: {}, MFT Rec Id: {}", AttrName(currAttr->AttrType), 
                         nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));


            } //switch
        } //currAttr->NonResidentFlag == ATTR_FLAG_RESIDENT

        currAttr = (MFT_ATTR_HEADER*)Add2Ptr(currAttr, currAttr->AttrSize);
        assert(mftRec->FileRecSize > Diff2Ptr(mftRec, currAttr));

    } while (*((DWORD*)currAttr) != ATTR_END);

    // only for base MFT records
    if (isBASERec)
    {  
        // MainName still can be empty for meta records.
        // some .img files have meta records without any FILENAME attributes for some reason
        if ((itemInfo.MainName.size() == 0) && iFileItem)
            itemInfo.MainName = convert_string<wchar_t>(iFileItem->ciName);
        
        // once we read all attributes we are ready to process ALLOC data runs which exist for Dir type only
        // when DataRuns.Count()==0 it means that all files (small number of files) are fit into INDEX_ROOT attribute
        // or directory does not contain any files
        if (itemInfo.IsDir() && (itemInfo.Node.DataRuns.Count() > 0) && FProcessNonResAttr)
        {
            //assert(itemInfo.AttrCounters[MATI(ATTR_ALLOC)] > 0);
            assert(itemInfo.AttrCounters[MATI(ATTR_ROOT)] == 1);
            //assert(itemInfo.AttrCounters[MATI(ATTR_BITMAP)] == 1);

            // calls processAllocPred for every cluster where ALLOC data is located (according to Data Runs) 
            // and where Bitmap contains 1 in appropriate cluster bit
            result = ProcessAllocDataRuns(itemInfo.Node, processAllocPred);
            if (result != TErrorCode::Success)
            {
                logger.Error("ProcessAllocDataRuns finished with error.");
                return result; //return is not needed here because node.FileList may contain items from INDEX_ROOT and partially from ALLOCATION
            }
        }

        // just to free some memory
        itemInfo.Node.DataRuns.ClearMem();
        itemInfo.Node.Bitmap.Clear();

        logger.DebugFmt("---------- END of BASE MFT Record {} ---------", MFT_REF::toHexString(mftRec->IndexMFTRec));
    }
    else
    {
        logger.DebugFmt("---------- END of CHILD MFT Record {} ---------", MFT_REF::toHexString(mftRec->IndexMFTRec));
    }

    return TErrorCode::Success;
}

/**
* @brief Reads all MFT items recursivelly starting from MftRecRef.
* @details if MftRecRef is FILE then only info about this file is read and added to FItemsList (TItemInfoList)
* if MftRecRef is DIRECTORY the function will navigate all child items and child items of child items, read all info and add those items into FItemsList
* @param MftRecRef MFT record reference to read information about
* @param parentMftRecRef reference to parent MFT record. This is directory record that contains MftRecRef item. 
* Needed to find proper file name in mftRecRef because MFT rec can contain many names because of hard links
* @param dirLevel specifies directory hierarchy level, increased by 1 each time when function goes into sub-directory
* @param callback Callback function that allows showing reading progress, now it called with names of files located in dirLevel=1
*/
TErrorCode TMFTStatCollector::ReadMftItems(MFT_REF mftRecRef, IFILE_NAME* fileItem, uint32_t dirLevel, IProgress& callback)
{
    GET_LOGGER;

    if (fileItem) assert(mftRecRef.Id == fileItem->MFTRecID.Id);

    ITEM_INFO itemInfo;
    auto res = ReadMftItemInfo(mftRecRef, fileItem, itemInfo);
    if (res != TErrorCode::Success)
    {
        logger.ErrorFmt("ReadMftItemInfo() finished with error for MFT Rec ID: {}", mftRecRef.toHexString());
        return res;
    }

    assert(itemInfo.Node.Bitmap.Count() == 0);
    assert(itemInfo.Node.DataRuns.Count() == 0);

    if (!itemInfo.IsDir())
        assert(itemInfo.DataStreamNames.Count() > 0); // file always has at least one data stream

    itemInfo.FilesCount = itemInfo.Node.FileList.Count();
    FItemsList.AddValue(itemInfo);

    if (dirLevel == 0 && itemInfo.IsDir())
        callback.Start(itemInfo.FilesCount);

    uint32_t num = 0;
    for (auto& item : itemInfo.Node.FileList)
    {     
        if (dirLevel == 0) callback.Progress(num++, item.ciName.c_str()); 

        if (!FLoader.IsMetaFile(item.MFTRecID.sId.low))
        {
            //if ((dirLevel == 1) && (callback)) callback(std::wstring(_T("\t")) + item.ciName.c_str()); //cout_t << _T("\t") << item.ciName.c_str() << std::endl;

            // reading detailed info about each item (files, directories and reparse points)
            res = ReadMftItems(item.MFTRecID, &item, dirLevel + 1, callback);
            if (res != TErrorCode::Success)
            {
                logger.ErrorFmt("ReadMftItems() finished with error for MFT Rec ID: {}", item.MFTRecID.toHexString());
            }
        }
    }

    if (dirLevel == 0 && itemInfo.IsDir())
        callback.Finish();

    return TErrorCode::Success;
}

/*
static int32_t PrintProgress(uint32_t n100, uint32_t progress, const string_t& data)
{
    const uint32_t LINE_LEN = 100; //100 symbols in console
    const uint32_t FOR_FILE_NM = 20;

    uint32_t realProgress = progress * LINE_LEN / n100;
    uint32_t figureProgress = progress * 100 / n100;

    //string_t str(_T(''\033[32m'));
    string_t str;
    str.reserve(LINE_LEN);
    str.append(realProgress, L'\u2588');
    str.append(abs((int32_t)LINE_LEN - (int32_t)str.size()), L'\u2591');
    //str.append(_T("\033[0m"));
    
    cout_t << _T("\r") << str << std::format(_T(" {}% ({:.{}}){:<{}}"), figureProgress, data, FOR_FILE_NM, _T(""), (FOR_FILE_NM > data.size()? FOR_FILE_NM - data.size(): 0));

    return 1; // not used at the moment
}*/

int64_t TMFTStatCollector::CountFileNamesWithNameType(uint8_t nameType, uint32_t count)
{
    return std::count_if(FItemsList.begin(), FItemsList.end(),
        [nameType, count](ITEM_INFO& x)
        {
            int64_t cnt = std::count_if(x.FileNames.begin(), x.FileNames.end(), [nameType](IFILE_NAME& v)
                {
                    assert(v.Attr.NameType <= FILE_NAME_UNICODE_AND_DOS);
                    return v.Attr.NameType == nameType;
                });
            return cnt == count;
        }
    );
}

/** 
* @brief Reads entire disk and prints to console various statistics
* @details Starts reading from directory defined in FLoader class (usually c: or d:), goes to all subdirs 
* and reads detailed attributes data for each file/dir. DOES NOT calculate dir sizes.
* Result is a plain list of all files/dirs without preserving child-parent relationships
* Use this function mostly for collecting various statictic about files and their NTFS attributes.
* @return TErrorCode value that contains code for success or code of error occurred
*/
TErrorCode TMFTStatCollector::CollectVolumeStat()
{
    GET_LOGGER;

    FItemsList.Clear();
    FItemsList.SetCapacity(1'000'000); // Expect 1M files and dirs

    MFT_REF startMFTRecID{0};
    startMFTRecID.Id = MFT_ROOT_REC_ID;
    
    Ticks::Start(_T("Loading time"));
    ConsoleProgress prgrs(cout_t);
    auto res = ReadMftItems(startMFTRecID, nullptr, 0, prgrs);
    if (res != TErrorCode::Success)
    {
        logger.ErrorFmt("Error reading volume {}.", wtos(getVolData().Name));
        return res;
    }
    Ticks::Finish(_T("Loading time"));

    //TODO because FItemsList contains "duplicates" (items in list that have the same MFTRecID) statistic may be slightly incorrect
    // duplicates appear because NTFS system contains hard links.

    Ticks::Start(_T("Calc statistic"));
    FStatistics.Clear();

    int64_t value;

    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir(); });
    FStatistics.SetValue(L"Total Items Count", toStringSepW(FItemsList.Count()));
    FStatistics.SetValue(L"Total Dirs Count",  toStringSepW(value));
    FStatistics.SetValue(L"Total Files Count", toStringSepW(FItemsList.Count() - value));  

    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.AttrsCount > 9; });
    FStatistics.SetValue(L"Attrs Count > 9", toStringSepW(value));

    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.HardLinksCount > 9; });
    FStatistics.SetValue(L"Hard links Count > 9", toStringSepW(value));

    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.FileNames.Count() > 13; });
    FStatistics.SetValue(L"Filenames Count > 13", toStringSepW(value));

    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.FileNames.Count() == 1; });
    FStatistics.SetValue(L"Filenames Count = 1", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.FileNames.Count() == 0; });
    FStatistics.SetValue(L"Filenames Count = 0", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && (a.HardLinksCount == 1); });
    FStatistics.SetValue(L"Dir Hard links Count = 1", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && (a.HardLinksCount == 2); });
    FStatistics.SetValue(L"Dir Hard links Count = 2", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && (a.HardLinksCount > 2); });
    FStatistics.SetValue(L"Dirs with Hard Links Count > 2", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.FileNames.Count() > 2; });
    FStatistics.SetValue(L"Dir Filenames Count > 2", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.FileNames.Count() == 1; });
    FStatistics.SetValue(L"Dir Filenames Count = 1", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.FileNames.Count() == 2; });
    FStatistics.SetValue(L"Dir Filenames Count = 2", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && (a.AttrCounters[MATI(ATTR_LIST_ATTR)] > 0); });
    FStatistics.SetValue(L"Dir Has ATTR_LIST attribute", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.NonResidentAttrList; });
    FStatistics.SetValue(L"Have non-resident ATTR_LIST", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.NonResidentBitmap; });
    FStatistics.SetValue(L"Have non-resident BITMAP", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.HasResidentDataAttr; });
    FStatistics.SetValue(L"Have resident Data", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.HasNonResidentDataAttr; });
    FStatistics.SetValue(L"Have non-resident Data", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.AttrCounters[MATI(ATTR_REPARSE)] > 0; });
    FStatistics.SetValue(L"Reparse Points Count", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.AttrCounters[MATI(ATTR_LOGGED_UTILITY_STREAM)] > 1; });
    FStatistics.SetValue(L"Logged Utility Streams Count > 1", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.AttrCounters[MATI(ATTR_LOGGED_UTILITY_STREAM)] > 2; });
    FStatistics.SetValue(L"Logged Utility Streams Count > 2", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.AttrCounters[MATI(ATTR_ID)] > 0; });
    FStatistics.SetValue(L"Have Object ID", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.AttrCounters[MATI(ATTR_DATA)] == 0; });
    FStatistics.SetValue(L"DOES NOT have Data attribute", toStringSepW(value));
    
    value = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.DataStreamNames.Count() > 2; });
    FStatistics.SetValue(L"Data streams Count > 2", toStringSepW(value));

    value = CountFileNamesWithNameType(FILE_NAME_DOS, 0);
    FStatistics.SetValue(L"Files with DOS name count = 0", toStringSepW(value));

    value = CountFileNamesWithNameType(FILE_NAME_DOS, 1);
    FStatistics.SetValue(L"Files with DOS name count = 1", toStringSepW(value));

    value = CountFileNamesWithNameType(FILE_NAME_DOS, 2);
    FStatistics.SetValue(L"Files with DOS name count = 2", toStringSepW(value));

    value = CountFileNamesWithNameType(FILE_NAME_UNICODE_AND_DOS, 1);
    FStatistics.SetValue(L"Files with UNICODE_AND_DOS name count = 1", toStringSepW(value));
    
    value = CountFileNamesWithNameType(FILE_NAME_UNICODE_AND_DOS, 2);
    FStatistics.SetValue(L"Files with UNICODE_AND_DOS name count = 2", toStringSepW(value));

    size_t maxMainNameLen = 0;
    auto maxHardLinks = std::max_element(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) { return a.HardLinksCount < b.HardLinksCount; });
    if (maxMainNameLen < (*maxHardLinks).MainName.size()) maxMainNameLen = (*maxHardLinks).MainName.size();

    auto maxAttrs = std::max_element(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) { return a.AttrsCount < b.AttrsCount; });
    if (maxMainNameLen < (*maxAttrs).MainName.size()) maxMainNameLen = (*maxAttrs).MainName.size();

    auto maxFilenames = std::max_element(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) { return a.FileNames.Count() < b.FileNames.Count(); });
    if (maxMainNameLen < (*maxFilenames).MainName.size()) maxMainNameLen = (*maxFilenames).MainName.size();

    auto maxDataStreams = std::max_element(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) { return a.DataStreamNames.Count() < b.DataStreamNames.Count(); });
    if (maxMainNameLen < (*maxDataStreams).MainName.size()) maxMainNameLen = (*maxDataStreams).MainName.size();

    auto maxDataLCNsCount = std::max_element(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) { return a.DataLCNsCount < b.DataLCNsCount; });
    if (maxMainNameLen < (*maxDataLCNsCount).MainName.size()) maxMainNameLen = (*maxDataLCNsCount).MainName.size();

    auto maxFilesInDirCount = std::max_element(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) { return a.FilesCount < b.FilesCount; });
    if (maxMainNameLen < (*maxFilesInDirCount).MainName.size()) maxMainNameLen = (*maxFilesInDirCount).MainName.size();

    FStatistics.SetValue(L"Max Hard Links Count", std::format(L"{:<10} File: {:<{}} ID: {}", toStringSepW((*maxHardLinks).HardLinksCount), (*maxHardLinks).MainName, maxMainNameLen, toStringSepW((*maxHardLinks).MFTRecID.sId.low)));
    FStatistics.SetValue(L"Max Attrs Count", std::format(L"{:<10} File: {:<{}} ID: {}", toStringSepW((*maxAttrs).AttrsCount), (*maxAttrs).MainName, maxMainNameLen, toStringSepW((*maxAttrs).MFTRecID.sId.low)));
    FStatistics.SetValue(L"Max File Names Count", std::format(L"{:<10} File: {:<{}} ID: {}", toStringSepW((*maxFilenames).FileNames.Count()), (*maxFilenames).MainName, maxMainNameLen, toStringSepW((*maxFilenames).MFTRecID.sId.low)));
    FStatistics.SetValue(L"Max Data Streams Count", std::format(L"{:<10} File: {:<{}} ID: {}", toStringSepW((*maxDataStreams).DataStreamNames.Count()), (*maxDataStreams).MainName, maxMainNameLen, toStringSepW((*maxDataStreams).MFTRecID.sId.low)));
    FStatistics.SetValue(L"Max Data Runs Count", std::format(L"{:<10} File: {:<{}} ID: {}", toStringSepW((*maxDataLCNsCount).DataLCNsCount), (*maxDataLCNsCount).MainName, maxMainNameLen, toStringSepW((*maxDataLCNsCount).MFTRecID.sId.low)));
    FStatistics.SetValue(L"Max Files Count in Dir", std::format(L"{:<10} File: {:<{}} ID: {}", toStringSepW((*maxFilesInDirCount).FilesCount), (*maxFilesInDirCount).MainName, maxMainNameLen, toStringSepW((*maxFilesInDirCount).MFTRecID.sId.low)));

    uint32_t FileNamesTotalSymbols = std::accumulate(FItemsList.begin(), FItemsList.end(), (uint32_t)0,
        [](uint32_t acc, ITEM_INFO& x) 
        {
            // find average length of all filenames inside one MFT record
            uint32_t symbols = std::accumulate(x.FileNames.begin(), x.FileNames.end(), (uint32_t)0, [](uint32_t accum, IFILE_NAME& v) 
                { 
                    return accum + (uint32_t)v.ciName.size(); 
                });
            return acc + symbols/x.FileNames.Count();
        }
    );
    uint32_t FileNamesAverageSymbols = FileNamesTotalSymbols / FItemsList.Count(); // average file length in symbols
    uint32_t FileNamesAverageBytes = FileNamesTotalSymbols * sizeof(wchar_t) / FItemsList.Count(); // average file length in bytes

    FStatistics.SetValue(L" ", L""); // just extra new line
    FStatistics.SetValue(L"Filenames Average Length (symbols)", toStringSepW(FileNamesAverageSymbols));
    FStatistics.SetValue(L"Filenames Average Length (bytes)", toStringSepW(FileNamesAverageBytes));

    size_t maxDSLen = 0;
    for (auto ds : (*maxDataStreams).DataStreamNames)
    {
        if (maxDSLen < ds.first.size())
            maxDSLen = ds.first.size();
    }
    std::wstringstream strstream;
    strstream << std::endl;
    for (auto ds : (*maxDataStreams).DataStreamNames)
    {
        if (ds.second.has_value())
        {
            uint64_t lcnCnt = 0;
            for (auto& rli : ds.second.value()) lcnCnt += rli.len;
            
            if (ds.first.empty())
                strstream << std::format(L"    {:<{}} - data runs count: {}, total LCNs: {}", L"'<empty>'", maxDSLen + 2, ds.second.value().Count(), lcnCnt) << std::endl;
            else
                strstream << std::format(L"    {:<{}} - data runs count: {}, total LCNs: {}", std::format(L"'{}'", ds.first), maxDSLen + 2, ds.second.value().Count(), lcnCnt) << std::endl;
        }
        else
            strstream << std::format(L"    {:<{}} - {}", std::format(L"'{}'", ds.first), maxDSLen + 2, L"resident data stream") << std::endl;
    }

    FStatistics.SetValue(L"  ", L""); // just extra new line
    FStatistics.SetValue(L"Datastream names for '" + (*maxDataStreams).MainName + L"'", strstream.str());

   /* std::wcout << std::endl << "File names for '" << (*maxFilenames).MainName.c_str() << "':" << std::endl;
    for (auto& fn : (*maxFilenames).FileNames)
    {
        std::wcout << fn << std::endl;
    }*/
    
    strstream.seekp(0);
    strstream << std::endl;
    for (int i = 1; i < ATTR_TYPE_CNT; i++) // bypass first ATTR_ZERO
    {
        strstream << std::format(L"    {:<19}= {}", stow(AttrTypeNames[i]), toStringSepW((*maxAttrs).AttrCounters[i])) << std::endl;
    }
    //FStatistics.SetValue(L"   ", L"\n");
    FStatistics.SetValue(L"Attribute counts for '" + (*maxAttrs).MainName + _T("'"), strstream.str());


    /*auto hasAttrList = std::find_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.AttrCounters[MATI(ATTR_LIST_ATTR)] > 0; });
    if (hasAttrList != FItemsList.end())
    {
        std::cout << std::format("Dir has ATTR_LIST. Name '{}', MFT Rec id: {})", wtos((*hasAttrList).MainName), (*hasAttrList).RecID.toHexString()) << std::endl;
        
        auto hasAttrList2 = std::find_if(++hasAttrList, FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.AttrCounters[MATI(ATTR_LIST_ATTR)] > 0; });
        if (hasAttrList2 != FItemsList.end())
            std::cout << std::format("Dir has ATTR_LIST. Name '{}', MFT Rec id: {}", wtos((*hasAttrList2).MainName), (*hasAttrList2).RecID.toHexString()) << std::endl;
    }
    */

    Ticks::Finish(_T("Calc statistic"));

    //SaveToFile(_T("ListMFTFile_StatReader.log"));

    Ticks::Start(_T("Freeing Memory"));
    FItemsList.ClearMem();
    Ticks::Finish(_T("Freeing Memory"));

    Ticks::PrintTime();

    return TErrorCode::Success;
}

void TMFTStatCollector::ShowVolumeStat()
{
    if (FStatistics.Count() == 0)
    {
        cout_t << std::endl << _T("No statistics collected. Nothing to show.") << std::endl;
    }
    else
    {
        cout_t << std::endl <<_T("Statistics for ") << convert_string<char_t>(FLoader.GetVolumeData().Name) << std::endl << std::endl;

        for (auto item : FStatistics)
        {
            if(item.second.empty())
                cout_t << std::format(_T("  {:<{}}"), convert_string<char_t>(item.first), 43) << std::endl;
            else
                cout_t << std::format(_T("  {:<{}}: {}"), convert_string<char_t>(item.first), 43, convert_string<char_t>(item.second)) << std::endl;
        }
    }
}

void TMFTStatCollector::SaveToFile(string_t fileName)
{
    cout_t << "Sorting... " << std::endl;

    Ticks::Start(_T("Sorting indexes time"));
    THArray<uint> index;
    index.SetCount(FItemsList.Count());
    cout_t << "FItemsList.Count() " << FItemsList.Count() << std::endl;

    for (auto& item : FItemsList)
        item.MainName = std::format(L"{} {}", item.MFTRecID.sId.low, item.MainName);

    std::iota(index.begin(), index.end(), 0); // fill index with increasing values from 0 to itemList.Count()

    // compare file names, but sort only indexes here (used below)
    std::sort(std::execution::par, index.begin(), index.end(), [&](uint a, uint b)
        {
            return FItemsList[a].MainName < FItemsList[b].MainName;
        });

    Ticks::Finish(_T("Sorting indexes time"));
    
    //std::string filename = "ListMFTFile_sorted.log";
    LogEngine::TFileStream ff(wtos(fileName));

    string_t fendl;
    BUILD_ENDL(fendl);

    ff << L"Total Items Count: " << toStringSep<std::wstring, uint>(index.Count()) << fendl;
    //ff << toStringSepW(itemsList.Count() - dirCount) + L" - files"; // only files
    //ff << toStringSepW(dirCount) + L" - dirs"; // only dirs

    cout_t << _T("Saving list of files to '") << fileName << _T("'...") << std::endl;

    Ticks::Start(_T("Saving time"));
    for (auto& ind : index)
    {
        //if (item.IsDir())
        //    ff << item.MainName  << L'\\' << fendl;
        //else
        
        ff << FItemsList[ind].MainName << fendl;
    }
    Ticks::Finish(_T("Saving time"));

    std::cout << "Saved." << std::endl;
}
