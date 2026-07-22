
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

// first 33 records are internal records
//TODO this is not always correct. Better check is to read first NFT records starting from #6 and find first name that does NOT start from $
#define IsMFTRecInternal(mftrec) ((mftrec) < 33) 

/** 
* @brief Reads information about one MFT record 
* @details Reads information ONLY about one MFT record refered by mftRecRef 
* Does NOT go to child items recursively
* @param mftRecRef MFT record that will be read
* @param itemInfo results of reading MFT record
* @return true in case of success, false in case of error
*/ 
bool TMFTStatCollector::ReadMftItemInfo(MFT_REF mftRecRef, ITEM_INFO& itemInfo)
{
    uint8_t* mftRecBuf = (uint8_t*)alloca(getVolData().BytesPerMFTRec);

    if (!FLoader.LoadMFTRecord(mftRecRef, mftRecBuf))
    {
        GET_LOGGER;
        logger.Error("LoadMFTRecord finished with error.");
        return false;
    }
    else
    {
        return ReadMftItemInfoBuf((MFT_FILE_RECORD*)mftRecBuf, itemInfo);
    }
}

bool TMFTStatCollector::ReadMftItemInfoBuf(MFT_FILE_RECORD* mftRec, ITEM_INFO& itemInfo)
{
    GET_LOGGER;

    AddFileAttrPred addToFileListPred = [&itemInfo](const ATTR_FILE_NAME* attr, const MFT_REF& ref)
        {
            std::wstring ciwnm(GetFName(attr), attr->FileNameLen);
            
            itemInfo.Node.FileList.AddValue({convert_string<ci_string::value_type>(ciwnm).c_str(), *attr, ref});
        };

    AttrListPred callReadMftItemInfoPred = [this, &itemInfo](const MFT_REF& ref)
        {
            // ref - is a child MFT rec where attr value is located
            if (!ReadMftItemInfo(ref, itemInfo)) // ReadMftItemInfo write message to log file in case of an error
            {
                //do nothing, continue executing
                //GET_LOGGER;
                //logger.Error("ReadMftItemInfo() returned false!");
            }
        };

    ProcessiBlocksPred processAllocPred = [this, &addToFileListPred](uint8_t* dataBuf, CLST VCN, CLST LCN)
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
        return false;
    }

    assert(ntfs_is_file_recp(mftRec->RecHeader.Signature));
    assert(mftRec->FileRecSize > mftRec->FirstAttrOffset);
    assert(mftRec->FileRecSize <= mftRec->AllocFileRecSize);
    assert(mftRec->AllocFileRecSize == getVolData().BytesPerMFTRec);
    assert((mftRec->Flags & MFT_FLAG_IN_USE) > 0);
    assert(mftRec->FirstAttrOffset < mftRec->AllocFileRecSize);

    bool isBASERec = itemInfo.RecID.Id == 0;

    // whether we are reading base MFT record or child one
    if (isBASERec)
    {
        // we are reading base record
        logger.Debug("---------- Reading BASE MFT Record ---------");
        assert(mftRec->ParentFileRec.Id == 0);
        itemInfo.RecID.Id = 0;
        itemInfo.RecID.sId.low = mftRec->IndexMFTRec;
        itemInfo.HardLinksCount = mftRec->HardLinksCnt;
    }
    else
    {
        // we are reading child record refered by ATTR_LIST_ATTR attribute
        logger.Debug("---------- Reading CHILD MFT Record ---------");
        assert(mftRec->ParentFileRec.Id != 0);
    }

    logger.DebugFmt("MFT Rec Signature: '{}'", std::string((char*)mftRec->RecHeader.Signature, 4));
    logger.DebugFmt("MFT Rec ID: {}", MFT_REF::toHexString(mftRec->IndexMFTRec));
    logger.DebugFmt("MFT Rec Num re-used: {}", mftRec->SeqNum);
    logger.DebugFmt("MFT Parent Rec ID: {} {}", mftRec->ParentFileRec.toHexString(), mftRec->ParentFileRec.Id > 0 ? " CHILD" : "BASE");
    logger.DebugFmt("MFT Hard Links Count: {}{}", mftRec->HardLinksCnt, mftRec->ParentFileRec.Id > 0? " (may be inaccurate for child records)": "");
    logger.DebugFmt("MFT Rec Size: {}", mftRec->FileRecSize);
    logger.DebugFmt("MFT Allocated Rec Size: {} ", mftRec->AllocFileRecSize);

    switch (mftRec->Flags)
    {
    case MFT_FLAG_IN_USE: logger.Debug("MFT Rec Type: 'IN USE' (file or anything else)"); break;
    case MFT_FLAG_IS_DIRECTORY: logger.Warn("! MFT Rec Type: DELETED Directory - unusual case"); break;
    case MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY: logger.DebugFmt("MFT Rec Type: 'IN USE DIRECTORY' {:#x}", (uint16_t)mftRec->Flags); break;
    default:
        logger.WarnFmt("MFT Rec Type: UNKNOWN {:#x}", (uint16_t)mftRec->Flags);
    }

    MFT_ATTR_HEADER* currAttr = (MFT_ATTR_HEADER*)Add2Ptr(mftRec, mftRec->FirstAttrOffset);

    ATTR_STD_INFO5* stdinfo = nullptr;
    int attrOrderNum = 1;
    do  // reading all attributes in a loop
    {
        assert(currAttr->AttrSize > 0);
        assert(currAttr->AttrSize < mftRec->FileRecSize);

        logger.DebugFmt("********** #{} Attribute ({} {:#x}) **********", attrOrderNum++, AttrName(currAttr->AttrType), (uint32_t)currAttr->AttrType);
        logger.Debug(currAttr->NonResidentFlag ? "Attr Type: NON-RESIDENT" : "Attr Type: RESIDENT");
        logger.DebugFmt("Attr ID: {}", currAttr->AttrID);
        logger.DebugFmt("Attr Size: {}", currAttr->AttrSize);
        logger.DebugFmt("Attr Flags: {}", currAttr->Flags);

        std::wstring nameOfAttrW = STREAM_NONAME_W;
        std::string nameOfAttrA = STREAM_NONAME;
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
            logger.InfoFmt("Looks like two and more {} ({}) attributes have found in MFT Rec: {}", 
                      AttrName(currAttr->AttrType), nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));

        
        if (currAttr->NonResidentFlag == 0) // attribute is RESident
        {
            itemInfo.AttrCounters[MakeAttrTypeIndex(currAttr->AttrType)]++;
            itemInfo.AttrsCount++;

            logger.DebugFmt("Attr Indexed: {}", currAttr->res.IndexedFlag);

            assert(currAttr->res.DataSize + currAttr->res.DataOffset <= currAttr->AttrSize);
            uint8_t* attrValue = Add2Ptr(currAttr, currAttr->res.DataOffset);
            
            switch (currAttr->AttrType)
            {
            case ATTR_STD_INFO: // Resident. Only.
            {
                stdinfo = (ATTR_STD_INFO5*)attrValue;

                assert((stdinfo->FileAttrib & FILE_ATTRIBUTE_NORMAL) == 0);// check that NORMAL bit is always zero

                //if (logger.ShouldLog(LogEngine::Levels::llDebug))
                //{

                    /*logger.Debug(FileDateToString("Created: ", stdinfo->CreateTime));
                    logger.Debug(FileDateToString("Modified: ", stdinfo->ModifyTime));
                    logger.Debug(FileDateToString("LastAccess: ", stdinfo->LastAccessTime));
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
            case ATTR_FILENAME: // Resident. Only.
            {
                ATTR_FILE_NAME* fname = (ATTR_FILE_NAME*)attrValue;
                std::wstring name(GetFName(fname), fname->FileNameLen);
                itemInfo.FileNames.AddValue(name);

                assert(fname->NameType <= FILE_NAME_UNICODE_AND_DOS);
                if (fname->NameType != FILE_NAME_DOS)
                { 
                    itemInfo.MainName = name.c_str();
                    itemInfo.FileAttrib = fname->dup.FileAttrib;
                }

                assert((fname->dup.FileAttrib & FILE_ATTRIBUTE_NORMAL) == 0);// check that NORMAL bit is always zero

                logger.DebugFmt("File Parent Rec ID: {}", fname->ParentDir.toHexString());
                logger.DebugFmt("File Name Type: '{}' ({:#x})", FileNameTypes[fname->NameType], fname->NameType);
                logger.DebugFmt("File DOS Attrib: {:#x} {}", fname->dup.FileAttrib, FormatFileAttributes(fname->dup.FileAttrib));
                logger.DebugFmt("File Name: '{}'", wtos(name));
                logger.DebugFmt("File Size: {}", fname->dup.FileSize);

                /*if (stdinfo && (stdinfo->CreateTime != fname->dup.CreateTime))
                {
                    logger.WarnFmt("CreateTime: STDINFO {} != FNAME {} ", FileDateToString("", stdinfo->CreateTime), FileDateToString("", fname->dup.CreateTime));
                    //assert(stdinfo->CreateTime == fname->dup.CreateTime);
                }*/
                
                /*if (stdinfo->ModifyTime != fname->dup.ModifyTime)
                {
                    //logger.WarnFmt("ModifyTime: STDINFO {} != FNAME {} ", FileDateToString("", stdinfo->ModifyTime), FileDateToString("", fname->dup.ModifyTime));
                    //assert(stdinfo->ModifyTime == fname->dup.ModifyTime);
                }*/

                /*logger.Debug(FileDateToString("Created: ", fname->dup.CreateTime));
                logger.Debug(FileDateToString("Modified: ", fname->dup.ModifyTime));
                logger.Debug(FileDateToString("LastAccess: ", fname->dup.LastAccessTime));
                */
                //assert(fname->dup.FileSize == 0);

                break;
            }
            case ATTR_ID: // Resident. 
            {
                ATTR_OBJECT_ID* objID = (ATTR_OBJECT_ID*)attrValue;
                constexpr const uint BUF_SZ = 100;
                std::wstring buf(BUF_SZ, 0);

                if (!StringFromGUID2(objID->ObjId, buf.data(), BUF_SZ))
                    logger.Error("Error. ATTR_OBJECT_ID. StringFromGUID2 has failed 1.");
                logger.DebugFmt("Attr Object ID: {}", wtos(buf));
                
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
                            logger.DebugFmt("Attr Domain ID: {}", wtos(buf));
                        }
                    }
                }
                
                //logger.DebugFmt("We do not process this attribute. Attr: '{}', AttrName: '{}'.", AttrName(currAttr->AttrType), nameOfAttrA);
                break;
            }
            case ATTR_ROOT: // Resident. ATTR_ROOT is resident only.
            {
                // $SDH INDEX_ROOT attribute name is related to storing and searching security descriptors (usually in MFT=0x09 $Secure).

                ATTR_INDEX_ROOT* indexR = (ATTR_INDEX_ROOT*)attrValue;
                //assert below is not always true. e.g. on some filesystems BytesPerCluster may be=2048 while IndexBlockSize=4096
                //assert(indexR->IndexBlockSize == getVolData().BytesPerCluster);
                assert(indexR->IndexBlockClst == indexR->IndexBlockSize/getVolData().BytesPerCluster);

                //$O exists in $Quota, $ObjId special files, $R exists in $Reparse
                if (nameOfAttrA != "$I30")
                    logger.InfoFmt("Resident ATTR_ROOT has non standard attribute name '{}' while standard name is '$I30'. MFT Rec ID {}",
                        nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));

                // all non-Internal BITMAP attrs have name '$I30'
                if (!IsMFTRecInternal(mftRec->IndexMFTRec))
                {
                    assert(indexR->AttrType == ATTR_FILENAME);//TODO MFT=0x09 contains two ATTR_ROOT attributes one of the with indexR->AttrType=0 for some reason
                    assert(indexR->Rule == COLLATION_RULE::FILENAME);
                    assert(nameOfAttrA == "$I30");
                }

                itemInfo.Node.IndexBlockSize = indexR->IndexBlockSize; // need this value for further processing ALLOC Data Runs

                logger.DebugFmt("IndexRoot Indexed Attr Type: {} {:#x}", AttrName(indexR->AttrType), (uint32_t)indexR->AttrType);
                logger.DebugFmt("IndexRoot Collation Rule: {} ({:#x})", CollRuleName((uint32_t)indexR->Rule), (uint32_t)indexR->Rule);
                logger.DebugFmt("IndexRoot Dir Type: {} ({:#x})", indexR->ihdr.Flags == 0 ? "SMALL DIR" : "BIG DIR", indexR->ihdr.Flags);
                logger.DebugFmt("IndexRoot IndexBlockSize: {}", indexR->IndexBlockSize);
                logger.DebugFmt("IndexRoot IndexBlockClst: {}", indexR->IndexBlockClst);
                
                auto pihdr = &(indexR->ihdr);
                logger.DebugFmt("IHDR Used Bytes: {}", pihdr->Used);

                GetFileList(pihdr, addToFileListPred);

                break;
            }
            case ATTR_LIST_ATTR: // Resident. ATTR_LIST_ATTR can be either resident or non-resident
            {
                assert(isBASERec); // only base records can have ATTR_LIST_ATTR attribute

                if (itemInfo.NonResidentAttrList.has_value()) 
                    logger.WarnFmt("Incorrect case has been met: Looks like two or more ATTR_LIST_ATTR ('{}') attributes present in a one MFT record: {}.", 
                               nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));
                itemInfo.NonResidentAttrList = false;

                logger.Debug("[Resident ATTR_LIST_ATTR] - START PARSING");

                THArray<uint32_t> visitedMFTRec;

                ATTR_LIST_ENTRY* attrEntry = (ATTR_LIST_ENTRY*)attrValue;
                uint8_t* attrEntryEnd = Add2Ptr(currAttr, currAttr->AttrSize);
                uint64_t processedAttrSize = 0;

                ParseAttrList(mftRec->IndexMFTRec, attrEntry, attrEntryEnd, currAttr->res.DataSize, processedAttrSize, callReadMftItemInfoPred);

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
                logger.DebugFmt("Resident ATTR_DATA Data Size: {}", currAttr->res.DataSize);

                // "Zone.Identifier" is often a second data attribute for the file
                //if (itemInfo.ResidentData && (nameOfAttrA != "Zone.Identifier"))
                //    logger.InfoFmt("Looks like two resident ATTR_DATA attributes have met in one MFT record: {}, Second Data Attr Name: '{}' ", 
                //              MFT_REF::toHexString(mftRec->IndexMFTRec), nameOfAttrA);
                itemInfo.HasResidentDataAttr = true;

                // each stream name can be met only once
                assert(!itemInfo.DataStreamNames.IfExists(nameOfAttrW));
                TDataRuns runs;
                itemInfo.DataStreamNames.SetValue(nameOfAttrW, runs); // for resident - add empty data run class
               
                break;
            }
            case ATTR_ALLOC: // Resident. ATTR_ALLOC is NON-Resident only. 
            {
                logger.WarnFmt("Warning! Resident ATTR_ALLOC has been met in MFT Rec ID {}!", MFT_REF::toHexString(mftRec->IndexMFTRec));
                break;
            }
            case ATTR_REPARSE: // Resident. ATTR_REPARSE can be resident or non-resident
            {
                ATTR_REPARSE_POINT* rp = (ATTR_REPARSE_POINT*)attrValue;
                logger.DebugFmt("Resident Reparse Point. Tag: {:#x}, Data Length: {}, MFT Rec ID: {}", 
                         rp->reparse_tag, rp->reparse_data_length, MFT_REF::toHexString(mftRec->IndexMFTRec));
                break;
            }
            case ATTR_SECURE: 
            case ATTR_EA:       // can be resident or non-resident
            case ATTR_EA_INFO:  // can be resident or non-resident
            case ATTR_PROPERTYSET:
            case ATTR_LOGGED_UTILITY_STREAM: // can be resident or non-resident
            {
                logger.DebugFmt("We do not process this attribute. Attr: '{}', Attr Name: '{}', MFT RecID: {}.", 
                         AttrName(currAttr->AttrType), nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));
                break;
            }

            default:
                logger.WarnFmt("UNKNOWN Resident attr has been met. Type:{}, Name:'{}', MFT Rec Id:{}", 
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

            logger.DebugFmt("Attr StartVCN: {}",     currAttr->nonres.StartVCN);
            logger.DebugFmt("Attr LastVCN: {}",      currAttr->nonres.LastVCN);
            logger.DebugFmt("Attr RealSize: {}",     currAttr->nonres.RealSize);
            logger.DebugFmt("Attr StreamSize: {}",   currAttr->nonres.StreamSize);
            logger.DebugFmt("Attr AllocatedSize: {}",currAttr->nonres.AllocatedSize);

            if (currAttr->nonres.CompressionUnitSize > 0) // for compressed files
            {
                logger.DebugFmt("Attr CompressionUnitSize: {}", currAttr->nonres.CompressionUnitSize);
                logger.DebugFmt("Attr CompressedSize: {}", currAttr->nonres.CompressedSize);
            }

            // this is rare case when file contains non-initialised portion of data
            // in this case RealSize contains total file size while StreamSize contains size of initialised data (StreamSize<RealSize) 
            if (currAttr->nonres.RealSize != currAttr->nonres.StreamSize)
                logger.DebugFmt("'currAttr.RealSize != currAttr.StreamSize'. ReadlSize: {}, StreamSize: {}, MFT Rec ID: {}", 
                    currAttr->nonres.RealSize, currAttr->nonres.StreamSize, MFT_REF::toHexString(mftRec->IndexMFTRec));
            
            if (currAttr->nonres.RealSize < currAttr->nonres.StreamSize)
                logger.WarnFmt("'currAttr.RealSize < currAttr.StreamSize'. ReadlSize: {}, StreamSize: {}, MFT Rec ID: {}",
                    currAttr->nonres.RealSize, currAttr->nonres.StreamSize, MFT_REF::toHexString(mftRec->IndexMFTRec));

            switch (currAttr->AttrType)
            {
            case ATTR_DATA: // NON-resident. Can be either resident and non-resident
            {
                itemInfo.HasNonResidentDataAttr = true;

                logger.DebugFmt("We do not process this attribute except for Data Runs decode. Attr: ATTR_DATA. Name: '{}'. ", nameOfAttrA);
                
                // for big data runs we can come here several times when one file data runs are split between several MFT records.
                // itemInfo.Node.DataRuns will accumulate all data runs from all parts.
                if (!DecodeDataRuns(currAttr, itemInfo.Node.DataRuns))
                {
                    break; //DataRunsDecode writes a message into log file in case of an error
                }

                logger.DebugFmt("NON-Resident ATTR_DATA Data Runs Count: {}, Stream Name: '{}'.", itemInfo.Node.DataRuns.Count(), nameOfAttrA);

                itemInfo.DataStreamNames.SetValue(nameOfAttrW, itemInfo.Node.DataRuns);

                // save data runs count only for the main (with empty name) data attribute (main data stream)
                if (nameOfAttrA == STREAM_NONAME)
                {
                    uint64_t lcnCnt = 0;
                    for (auto& rli : itemInfo.Node.DataRuns) lcnCnt += rli.len;
                    itemInfo.DataLCNsCount = lcnCnt;
                }

                break;
            }

            case ATTR_BITMAP: // NON-resident. ATTR_BITMAP can be either resident and non-resident
            {
                if (itemInfo.NonResidentBitmap.has_value())
                    logger.WarnFmt("Incorrect case has been met: Looks like two or more ATTR_BITMAP ('{}') attributes were met in a one MFT record: {}.", 
                              nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));
                itemInfo.NonResidentBitmap = true;

                assert(itemInfo.Node.Bitmap.Count() == 0);

                logger.InfoFmt("Non-Resident ATTR_BITMAP ('{}') has been met. MFT Rec ID {}", nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));
              
                // most MFT record have BITMAP attribute with '$I30' name
                // however special MFT records (#0...#33) have BITMAP attribute with other names
                // special records have the following attr names: $O, $R, <empty> (for $MFT file record)
                if (nameOfAttrA != "$I30")
                    logger.InfoFmt("Non-Resident ATTR_BITMAP has non standard attribute name '{}' while standard name is '$I30'. MFT Rec ID {}", 
                              nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));

                // all non-Internal BITMAP attrs have name '$I30'
                if (!IsMFTRecInternal(mftRec->IndexMFTRec)) assert(nameOfAttrA == "$I30");

                if (!ParseNonresBitmap(currAttr, itemInfo.Node.Bitmap))
                {
                    logger.Error("ParseNonresBitmap finished with error.");
                    break;
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

                if (!DecodeDataRuns(currAttr, itemInfo.Node.DataRuns)) // writes a message into log file in case of an error
                {
                    break;
                }

                // ONLY DECODE DATA RUNS HERE.
                // Processing of data runs is moved below after all atrributes are read from MFT base and child records

                break;
            }
            case ATTR_LIST_ATTR: // NON-resident. ATTR_LIST_ATTR can be either resident and non-resident
            {
                logger.Debug("[NON-Resident ATTR_LIST_ATTR] - START PARSING");

                assert(isBASERec); // only base records can have ATTR_LIST_ATTR attribute

                if (itemInfo.NonResidentAttrList.has_value())
                    logger.WarnFmt("Incorrect case has been met: Looks like two or more ATTR_LIST_ATTR ('{}') attributes have met in a one MFT record: {}.", 
                              nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));
                itemInfo.NonResidentAttrList = true;

                if(!ParseNonresAttrList(mftRec->IndexMFTRec, currAttr, callReadMftItemInfoPred))
                {
                    logger.Error("ParseNonresAttrList returned error.");
                    //return;
                }

                /*TDataRuns dataRuns;
                if (!DecodeDataRuns(currAttr, dataRuns)) // DataRunsDecode writes a message into log file in case of an error
                {
                    break;
                }

                //if (dataRuns.Count() > 1)
                //    logger.InfoFmt("UNUSUAL case. Non-resident ATTR_LIST_ATTR occupies {} data runs instead one.", dataRuns.Count());

                THArray<uint32_t> visitedMFTRec;
                uint32_t currRun = 0;

                // "global" (outside of outer loop) counter of processed attributes in ATTR_LIST
                // sometimes data run list contains 2 data runs, but number of attributes is limited by RealSize value 
                // and may be limited by first data run only
                // second data run is "officially" present, but is not parsed because of RealSize
                uint64_t processedAttrSize = 0;

                while (currRun < dataRuns.Count())
                {
                    DATA_RUN_ITEM& rli = dataRuns[currRun];
                    logger.DebugFmt("Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

                    //if (rli.len > 1)
                    //    logger.InfoFmt("UNUSUAL case. Non-resident ATTR_LIST_ATTR datarun item occupies {} LCNs instead of one.", rli.len);

                    auto dataBufSize = rli.len * getVolData().BytesPerCluster;
                    uint8_t* dataBuf = (uint8_t*)alloca(dataBufSize);//TODO it is not good to allocate memory in a loop each time

                    if (!ReadClusters(rli.lcn, rli.len, dataBuf)) // ReadClusters writes a message into log file in case of an error
                    {
                        break;
                    }

                    ATTR_LIST_ENTRY* attrEntry = (ATTR_LIST_ENTRY*)dataBuf;
                    
                    //TODO probably we need to parse each cluster separately because end of last attrEntry in cluster#1 does not mean start of first attrEntry in cluster#2
                   
                    uint8_t* attrEntryEnd = dataBuf + dataBufSize;
                    
                    //assert(currAttr->nonres.RealSize < getVolData().BytesPerCluster);
                    ParseAttrList(mftRec->IndexMFTRec, attrEntry, attrEntryEnd, currAttr->nonres.RealSize, processedAttrSize, callReadMftItemInfoPred);

                    if (processedAttrSize >= currAttr->nonres.RealSize)
                    {
                        logger.DebugFmt("Outer loop is finished by this condition: 'processedAttrSize >= realSize'. Last Attr: {}, realSize: {}", 
                                AttrName(attrEntry->AttrType), currAttr->nonres.RealSize);
                        break;
                    }

                    currRun++;
                }*/

                logger.Debug("[NON-Resident ATTR_LIST_ATTR] - FINISHED PARSING");

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
        } //currAttr->NonResidentFlag == 0

        currAttr = (MFT_ATTR_HEADER*)Add2Ptr(currAttr, currAttr->AttrSize);
        assert(mftRec->FileRecSize > Diff2Ptr(mftRec, currAttr));

    } while (*((DWORD*)currAttr) != ATTR_END);

    // only for base MFT records
    if (isBASERec)
    {
        // once we read all attributes we are ready to process ALLOC data runs which exist for Dir type only
        // when DataRuns.Count()==0 it means that all files (small number of files) are fit into INDEX_ROOT attribute
        if (itemInfo.IsDir() && (itemInfo.Node.DataRuns.Count() > 0))
        {
            assert(itemInfo.AttrCounters[MakeAttrTypeIndex(ATTR_ALLOC)] > 0);

            // single function but different behaviour of adding files/dirs into lists
            // in other place where ProcessDataRuns called lambda adds found dirs into another list
            if (!ProcessDataRuns(itemInfo.Node, processAllocPred))
            {
                logger.Error("ProcessAllocDataRuns finished with error.");
                //return is not needed here because node.FileList may contain items from INDEX_ROOT and partially from ALLOCATION
            }
        }

        // just to free some memory
        itemInfo.Node.DataRuns.Clear();
        itemInfo.Node.Bitmap.Clear();

        logger.DebugFmt("---------- FINISHED Reading BASE MFT Record ({}) ---------", MFT_REF::toHexString(mftRec->IndexMFTRec));
    }
    else
    {
        logger.DebugFmt("---------- FINISHED Reading CHILD MFT Record ({}) ---------", MFT_REF::toHexString(mftRec->IndexMFTRec));
    }

    return true;
}


// reads all MFT items recursivelly starting from startMftRec.
// if startMftRec is FILE then only info about this file will be read and added to the parameter 'list' (TItemInfoList)
// if startMftRec is DIRECTORY the function will navigate all child items and child items of child items, read all info and add all those items into param 'list'
bool TMFTStatCollector::ReadMftItems(MFT_REF startMftRecRef, uint32_t dirLevel)
{
    GET_LOGGER;

    ITEM_INFO itemInfo;

    if (!ReadMftItemInfo(startMftRecRef, itemInfo))
    {
        logger.ErrorFmt("ReadMftItemInfo() finished with error for MFT rec: {:#x}", startMftRecRef.sId.low);
        return false;
    }

    assert(itemInfo.Node.Bitmap.Count() == 0);
    assert(itemInfo.Node.DataRuns.Count() == 0);

    if (!itemInfo.IsDir())
        assert(itemInfo.DataStreamNames.Count() > 0); // file always has at least one data stream

    itemInfo.FilesCount = itemInfo.Node.FileList.Count();
    FItemsList.AddValue(itemInfo);


    for (auto& item : itemInfo.Node.FileList)
    {     
        if (!item.NtfsInternal()) // bypass hidden mft metafiles
        {
            if (dirLevel == 0) cout_t << item.ciName.c_str() /*<< " [" <<item.Attr.dup.FileSize << "]"*/ << std::endl;
            //if (dirLevel == 1) cout_t << _T("\t") << item.ciName.c_str() << std::endl;

            // reading detailed info about each item (files, directories and reparse points)
            if (!ReadMftItems(item.MFTRef, dirLevel + 1))
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
void TMFTStatCollector::ShowVolumeStat()
{
    GET_LOGGER;

    FItemsList.SetCapacity(1'000'000); // Expect 1M files and dirs

    MFT_REF startId{0};
    startId.Id = MFT_ROOT_REC_ID;
    
    Ticks::Start(_T("Loading time"));
    if (!ReadMftItems(startId, 0))
    {
        logger.Error("ReadMftItems() returned false!");
    }
    Ticks::Finish(_T("Loading time"));

    Ticks::Start(_T("Calc and Print stat time"));
    auto AttrCountGreater9 = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.AttrsCount > 9; });
    auto HardLinksGreater9 = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.HardLinksCount > 9; });
    auto FilenamesCountGreater13 = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.FileNames.Count() > 13; });
    auto FilenamesCountEQ1 = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.FileNames.Count() == 1; });
    auto FilenamesCountEQ0 = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.FileNames.Count() == 0; });
    auto DirHardLinksEQ1 = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.HardLinksCount == 1; });
    auto DirHardLinksEQ2 = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.HardLinksCount == 2; });
    auto DirsHardLinksGreater2 = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.HardLinksCount > 2; });
    auto DirFilenamesCountGreater2 = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.FileNames.Count() > 2; });
    auto DirFilenamesCountEQ2 = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.FileNames.Count() == 2; });
    auto DirFilenamesCountEQ1 = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.FileNames.Count() == 1; });
    auto DirHasAttrList = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.AttrCounters[MATI(ATTR_LIST_ATTR)] > 0; });
    auto HasNonresAttrList = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.NonResidentAttrList; });
    auto HasNonresBitmap = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.NonResidentBitmap; });
    auto HasResidentData = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.HasResidentDataAttr; });
    auto HasNonResidentData = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.HasNonResidentDataAttr; });
    auto ReparsePointsCount = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.AttrCounters[MATI(ATTR_REPARSE)] > 0; });
    auto LoggedUtilityStreamCountGreater1 = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.AttrCounters[MATI(ATTR_LOGGED_UTILITY_STREAM)] > 1; });
    auto LoggedUtilityStreamCountGreater2 = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.AttrCounters[MATI(ATTR_LOGGED_UTILITY_STREAM)] > 2; });
    auto HasObjectID = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.AttrCounters[MATI(ATTR_ID)] > 0; });
    auto DoesNotHaveDataAttribute = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.AttrCounters[MATI(ATTR_DATA)] == 0; });
    auto DataStreamsCountGreater2 = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.DataStreamNames.Count() > 2; });
    
    auto maxHardLinks = std::max_element(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) { return a.HardLinksCount < b.HardLinksCount; });
    auto maxAttrs = std::max_element(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) { return a.AttrsCount < b.AttrsCount; });
    auto maxFilenames = std::max_element(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) { return a.FileNames.Count() < b.FileNames.Count(); });
    auto maxDataStreams = std::max_element(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) { return a.DataStreamNames.Count() < b.DataStreamNames.Count(); });
    auto maxDataLCNsCount = std::max_element(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) { return a.DataLCNsCount < b.DataLCNsCount; });
    auto maxFilesInDirCount = std::max_element(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a, ITEM_INFO& b) { return a.FilesCount < b.FilesCount; });

    uint32_t FileNamesTotalSymbols = std::accumulate(FItemsList.begin(), FItemsList.end(), (uint32_t)0,
        [](uint32_t acc, ITEM_INFO& x) 
        {
            // find average length of all filenames inside one MFT record
            uint32_t symbols = std::accumulate(x.FileNames.begin(), x.FileNames.end(), (uint32_t)0, [](uint32_t accum, std::wstring& v) { return accum + (uint32_t)v.size(); });
            return acc + symbols/x.FileNames.Count();
        }
    );
    uint32_t FileNamesAverageSymbols = FileNamesTotalSymbols / FItemsList.Count(); // average file length in symbols
    uint32_t FileNamesAverageBytes = FileNamesTotalSymbols * sizeof(wchar_t) / FItemsList.Count(); // average file length in bytes

    std::cout << std::endl;
    auto dirCount = std::count_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir(); });

    std::cout << "Total Items Count: " << toStringSep<uint,std::string>(FItemsList.Count()) << std::endl;
    std::cout << "Total Dirs Count: " << toStringSepA(dirCount) << std::endl;
    std::cout << "Total Files Count: " << toStringSepA(FItemsList.Count() - dirCount) << std::endl << std::endl; // only files 

    std::cout << "Attrs Count > 9: " << AttrCountGreater9 << std::endl;
    std::cout << "Hard links Count > 9: " << HardLinksGreater9 << std::endl;
    std::cout << "Filenames Count > 13: " << FilenamesCountGreater13 << std::endl;
    std::cout << "Filenames Count = 1: " << FilenamesCountEQ1 << std::endl;
    std::cout << "Filenames Count = 0: " << FilenamesCountEQ0 << std::endl;
    std::cout << "Reparse Points Count: " << ReparsePointsCount << std::endl;
    std::cout << "Dir Hard links Count = 1: " << DirHardLinksEQ1 << std::endl;
    std::cout << "Dir Hard links Count = 2: " << DirHardLinksEQ2 << std::endl;
    std::cout << "Dirs with Hard Links Count > 2: " << DirsHardLinksGreater2 << std::endl;
    std::cout << "Dir Filenames Count > 2: " << DirFilenamesCountGreater2 << std::endl;
    std::cout << "Dir Filenames Count = 1: " << DirFilenamesCountEQ1 << std::endl;
    std::cout << "Dir Filenames Count = 2: " << DirFilenamesCountEQ2 << std::endl;
    std::cout << "Dir Has ATTR_LIST attribute: " << DirHasAttrList << std::endl;
    std::cout << "Logged Utility Streams Count > 1: " << LoggedUtilityStreamCountGreater1 << std::endl;
    std::cout << "Logged Utility Streams Count > 2: " << LoggedUtilityStreamCountGreater2 << std::endl;
    std::cout << "Have Object ID: " << HasObjectID << std::endl;
    std::cout << "Have non-resident ATTR_LIST: " << HasNonresAttrList << std::endl;
    std::cout << "Have non-resident BITMAP: " << HasNonresBitmap << std::endl;
    std::cout << "Have resident Data: " << HasResidentData << std::endl;
    std::cout << "Have non-resident Data: " << HasNonResidentData << std::endl;
    std::cout << "DOES NOT have Data attribute: " << DoesNotHaveDataAttribute << std::endl;
    std::cout << "Data streams Count > 2: " << DataStreamsCountGreater2 << std::endl;
    
    std::cout << std::format("Max Hard Links Count: {}, file name: '{}' (mft red id: {})", (*maxHardLinks).HardLinksCount, wtos((*maxHardLinks).MainName), MFT_REF::toHexString((*maxHardLinks).RecID.sId.low)) << std::endl;
    std::cout << std::format("Max Attrs Count: {}, file name: '{}' (mft rec id: {})", (*maxAttrs).AttrsCount, wtos((*maxAttrs).MainName), MFT_REF::toHexString((*maxAttrs).RecID.sId.low)) << std::endl;
    std::cout << std::format("Max File Names Count: {}, file name: '{}' (mft rec id: {})", (*maxFilenames).FileNames.Count(), wtos((*maxFilenames).MainName), MFT_REF::toHexString((*maxFilenames).RecID.sId.low)) << std::endl;
    std::cout << std::format("Max Data Streams Count: {}, filename: '{}' (mft rec id: {})", (*maxDataStreams).DataStreamNames.Count(), wtos((*maxDataStreams).MainName), MFT_REF::toHexString((*maxDataStreams).RecID.sId.low)) << std::endl;
    std::cout << std::format("Max Data Runs Count: {}, filename: '{}' (mft rec id: {})", (*maxDataLCNsCount).DataLCNsCount, wtos((*maxDataLCNsCount).MainName), (*maxDataLCNsCount).RecID.toHexString()) << std::endl;
    std::cout << std::format("Max Files Count in Dir: {}, filename: '{}' (mft rec id: {})", (*maxFilesInDirCount).FilesCount, wtos((*maxFilesInDirCount).MainName), (*maxFilesInDirCount).RecID.toHexString()) << std::endl;

    std::cout << std::endl << "Filenames Average Length: " << FileNamesAverageSymbols << " symbols (" << FileNamesAverageBytes << " bytes)" << std::endl;

    std::wcout << std::endl << L"Datastream names for '"<< (*maxDataStreams).MainName.c_str() << "':" << std::endl;
    for (auto ds : (*maxDataStreams).DataStreamNames)
    {
        if(ds.first.empty())
            std::wcout << L"'<empty>' - data runs count: " << ds.second.Count() << std::endl;
        else
            std::wcout << "'" << ds.first << L"' - data runs count: " << ds.second.Count() << std::endl;
    }

   /* std::wcout << std::endl << "File names for '" << (*maxFilenames).MainName.c_str() << "':" << std::endl;
    for (auto& fn : (*maxFilenames).FileNames)
    {
        std::wcout << fn << std::endl;
    }*/

    std::wcout << std::endl << "Attribute counts for '" << (*maxAttrs).MainName.c_str() << "':" << std::endl;
    for (int i = 1; i < ATTR_TYPE_CNT; i++) // bypass ATTR_ZERO
    {
        std::wcout << AttrTypeNames[i] << " = " <<(*maxAttrs).AttrCounters[i] << std::endl;
    }

    auto hasAttrList = std::find_if(FItemsList.begin(), FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.AttrCounters[MATI(ATTR_LIST_ATTR)] > 0; });
    if (hasAttrList != FItemsList.end())
    {
        std::cout << std::format("Dir has ATTR_LIST. Name '{}', MFT Rec id: {})", wtos((*hasAttrList).MainName), (*hasAttrList).RecID.toHexString()) << std::endl;
        
        auto hasAttrList2 = std::find_if(++hasAttrList, FItemsList.end(), [](ITEM_INFO& a) { return a.IsDir() && a.AttrCounters[MATI(ATTR_LIST_ATTR)] > 0; });
        if (hasAttrList2 != FItemsList.end())
            std::cout << std::format("Dir has ATTR_LIST. Name '{}', MFT Rec id: {}", wtos((*hasAttrList2).MainName), (*hasAttrList2).RecID.toHexString()) << std::endl;
    }

    Ticks::Finish(_T("Calc and Print stat time"));

    /*std::cout << "Sorting... " << std::endl;

    Ticks::Start(_T("Sorting time"));
    THArray<uint> index;
    index.AddFillValues(itemsList.Count());
    std::iota(index.begin(), index.end(), 0); // fill index with increasing values from 0 to itemList.Count()

    std::sort(std::execution::par, index.begin(), index.end(), [&](uint a, uint b)
        {
            return itemsList[a].MainName < itemsList[b].MainName;
        });

    Ticks::Finish(_T("Sorting time"));
    */
    /*std::cout << "Converting... " << std::endl;
    Ticks::Start(_T("Converting time"));
    THArray<string_t> arr;
    arr.SetCapacity(itemsList.Count() + 1); // 1 is just in case
    for (auto& item : itemsList)
    {
        arr.AddValue(item.MainName.c_str());
    }
    Ticks::Finish(_T("Converting time"));

    std::cout << "Sorting... " << std::endl;

    Ticks::Start(_T("Sorting time"));
    std::sort(arr.begin(), arr.end());
    Ticks::Finish(_T("Sorting time"));
    */
    /*
    std::string filename = "ListMFTFile_sorted.log";
    LogEngine::TFileStream ff(filename);

    string_t fendl;
    BUILD_ENDL(fendl);
    
    ff << L"Total Items Count: " << toStringSep<uint,std::wstring>(index.Count()) << fendl;
    //ff << toStringSepW(itemsList.Count() - dirCount) + L" - files"; // only files
    //ff << toStringSepW(dirCount) + L" - dirs"; // only dirs

    std::cout << "Saving list of files to '" << filename << "'..." << std::endl;

    Ticks::Start(_T("Saving time"));
    for (auto& ind : index)
    {
        //if (item.IsDir())
        //    ff << item.MainName  << L'\\' << fendl;
        //else
            ff << itemsList[ind].MainName << fendl;
    }
    Ticks::Finish(_T("Saving time"));
    */

    std::cout << std::endl << "Freeing memory..." << std::endl;
    FItemsList.ClearMem();
    std::cout << "Freed" << std::endl << std::endl;

    Ticks::PrintCon(1);
}

