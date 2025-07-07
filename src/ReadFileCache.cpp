
// this is to remove defines min, max in windows headers because they conflict with std::min std::max 
#define NOMINMAX

#include "Debug.h"
#include <windows.h>
#include <iostream>
#include <cassert>
#include <cstdint>
#include <string>
#include <algorithm>

#include "strutils/include/string_utils.h"
#include "LogEngine2/DynamicArrays.h"
#include "LogEngine2/LogEngine.h"
#include "logengine2/FileStream.h"
#include "Caches.h"
#include "FileLevel.h"
#include "FileCache.h"
#include "NTFS.h"



bool ProcessAllocDataRuns(VOLUME_DATA& volData, DIR_NODE& node, FileListPred pred)
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
                    GetFileList(pihdr, pred);

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
// ATTR_ROOT - gets list of files from this attr (usually this list is empty when appropriate ATTR_ALLOC exists)
// ATTR_LIST_ATTR - goes to child records and parses attrs listed above from there
// ATTR_ALLOC - only does data runs decoding, does not go to LCNs. 
bool ParseMFTRecord(VOLUME_DATA& volData, uint8_t* mftRecData, DIR_NODE& node, uint32_t parent, TFileCache::TLevel* level)
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
                assert(indexR->AttrType == ATTR_FILENAME);
                assert(indexR->IndexBlockSize == volData.BytesPerCluster);
                assert(indexR->IndexBlockClst == 1);
                assert(indexR->Rule == COLLATION_RULE::FILENAME);

                logger.DebugFmt("IndexRoot attr type: {:#x} ({})", (uint32_t)indexR->AttrType, AttrName(indexR->AttrType));
                logger.DebugFmt("Collation rule: {}", (uint32_t)indexR->Rule);
                logger.DebugFmt("Dir type: {} {}", indexR->ihdr.Flags, (indexR->ihdr.Flags == 0 ? " (small dir)" : " (big dir)"));

                auto pihdr = &(indexR->ihdr);

                // does not go to subnodes
                GetFileList(pihdr, [parent, &level](const ATTR_FILE_NAME* attr, const MFT_REF& ref) 
                    {
                        level->AddValue(parent, level->Level(), ref, attr); 
                    } ); 

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
                                if (ParseMFTRecord(volData, mftRecBuf, node, parent, level)) //TODO shall we break in case of an error in LoadMFTRecord or ParseMFTRecord 
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
                                    if (!ParseMFTRecord(volData, mftRecBuf, node, parent, level))
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
            case ATTR_EA:      // can be resident or non-resident
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

// parentIdx - index of parent item (in previous level) in gFileList
// levelIdx - level number for being added items
// DirSize passed by ref because we return dir size to upper directory 
bool ReadDirectoryV1(VOLUME_DATA& volData, /*MFT_REF mftRecID,*/ uint32_t parentIdx, CACHE_ITEM* parentItem, uint64_t& dirSize, TFileCache& gFileList)
{
//    if (parentItem->FLevel + 1 > 30) throw std::runtime_error("dirLevel > 30 !!!!!!!");

    GET_LOGGER;

    uint8_t* mftRecBuf = (uint8_t*)alloca(volData.BytesPerMFTRec);

    MFT_REF MFTRec{0};
    if (parentItem == nullptr)
        MFTRec.Id = MFT_ROOT_REC_ID;
    else
        MFTRec = parentItem->FMFTRecID;

    if (!LoadMFTRecord(volData, MFTRec /*mftRecID*/, mftRecBuf))
    {
        logger.Error("LoadMFTRecord finished with error.");
        return false;
    }

    if (parentItem == nullptr) assert(parentIdx == 0);

    // we are on the root dir - add root item into gFileList
    // then change levelIdx to 1 to properly read root dirs/files into level 1 instead of 0
    if (parentItem == nullptr)
    {
        MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;
        MFT_ATTR_HEADER* currAttr = (MFT_ATTR_HEADER*)Add2Ptr(mftRec, mftRec->FirstAttrOffset);
        ATTR_STD_INFO5* stdinfo = (ATTR_STD_INFO5*)Add2Ptr(currAttr, currAttr->res.DataOffset);
        
        auto level0 = gFileList.GetLevel(0);
        assert(level0->Count() == 0);

        ATTR_FILE_NAME* fattr = (ATTR_FILE_NAME*)alloca(sizeof(ATTR_FILE_NAME) + (volData.Name.size())*sizeof(wchar_t));
        
        fattr->FileNameLen = (uint8_t)volData.Name.size();
        fattr->NameType = FILE_NAME_UNICODE_AND_DOS;
        fattr->ParentDir = MFTRec;
        fattr->dup.CreateTime = stdinfo->CreateTime;
        fattr->dup.ModifyTime = stdinfo->ModifyTime;
        fattr->dup.ModifyAttrTime = stdinfo->ModifyAttrTime;
        fattr->dup.LastAccessTime = stdinfo->LastAccessTime;
        fattr->dup.FileAttrib = stdinfo->FileAttrib | (uint32_t)FILE_ATTR_FLAGS::DIRECTORY; //this is HACK because STD attr does not contain dir flag for '.' directory for some reason. while FILENAME attr for '.' does contain dir flag.
        //wcsncpy_s((wchar_t*)Add2Ptr(fattr, sizeof(ATTR_FILE_NAME)), fattr->FileNameLen, volData.Name.c_str(), fattr->FileNameLen);
        wmemcpy_s((wchar_t*)Add2Ptr(fattr, sizeof(ATTR_FILE_NAME)), fattr->FileNameLen, volData.Name.c_str(), fattr->FileNameLen);

        level0->AddValue(0, 0, MFTRec, fattr);
        parentItem = level0->GetValue(0);
        
        //IMPORTANT: change levelIdx to 1 to proper read files from root dir into level 1 instead of 0 
        //levelIdx = 1;
    }

    auto level = gFileList.GetLevel(parentItem->FLevel + 1);
    assert(level->Level() == parentItem->FLevel + 1);
    uint32_t startPos = level->Count(); // remember start position for newly added items
    CACHE_ITEM* startItem = level->Last(); // NOTE! Last() returns pointer to item that WILL BE added next. Also startItem may become invalid if realloc happened in the level duing adding new items

    DIR_NODE node;
    if (!ParseMFTRecord(volData, mftRecBuf, node, parentIdx, level))
    {
        logger.Error("ParseMFTRecord finished with error.");
        return false;
    }

    if (node.DataRuns.Count() > 0)
    {
        if (!ProcessAllocDataRuns(volData, node, [parentIdx, &level](const ATTR_FILE_NAME* attr, const MFT_REF& ref)
            {
                level->AddValue(parentIdx, level->Level(), ref, attr);
            }))
        {
            logger.Error("ProcessAllocDataRuns finished with error.");
            //return is not needed here because node.FileList may contain items from INDEX_ROOT and partially from ALLOCATION
            //return false;
        }
    }

    auto cnt = level->Count(); // count will be changing duing for loop below 
    CACHE_ITEM* item{nullptr};

    // this is check if realloc has happened in level array during ParseMFTRecord and ProcessAllocDataRuns calls
    // if it is then startItem pointer became invalid and we need to locate that item again
    if(level->Belongs(startItem)) 
        item = startItem;
    else
        if(startPos < cnt) item = level->GetValue(startPos);
    
    // item may be NULL here for empty directories
    if (item != nullptr) 
        assert((item->FLevel == parentItem->FLevel + 1) && item->FParent == parentIdx);
    
    assert(parentItem->Dir());

    dirSize = 0;

    for (uint32_t i = startPos; i < cnt; i++)
    {
        if (!item->MetaFile() && !item->DotDir()) // do not add hidden metafiles into file list
        {
            if (item->Dir())
            {
                if (!item->Reparse())
                {
                    uint64_t childDirSize{ 0 };
                    if (!ReadDirectoryV1(volData, /*item->FMFTRecID,*/ i, item /*levelIdx + 1*/, childDirSize, gFileList))
                        logger.ErrorFmt("ReadDirectoryV1 finished with error for MFT rec: {}", item->FMFTRecID.sId.low);
                    dirSize += childDirSize;
                }
            }
            else // file, not a directory
            {
                dirSize += item->FileAttr.dup.FileSize;
            }

            // print only dirs of first and second levels.
            if (parentItem->FLevel == 0)
                logger.InfoFmt("{} - {}", wtos(std::wstring(item->Name(), item->FileAttr.FileNameLen)), toStringSepA(item->FileAttr.dup.FileSize));

            //if (levelIdx == 2) logger.InfoFmt("\t{}", wtos(std::wstring(item->Name(), item->FileAttr.FileNameLen)));
        }

        item = level->Next(item);
    }

    parentItem->FileAttr.dup.FileSize = dirSize;

    return true;
}

void ReadDirsV1(VOLUME_DATA& volData)
{
    TFileCache fileCache;
    uint64_t rootDirSize{0};

    if (!ReadDirectoryV1(volData, 0, nullptr, rootDirSize, fileCache))
    {
        throw std::runtime_error("ReadDirectoryV1 finished with error.");
    }

    std::cout << "Root dir size: " << toStringSepA(rootDirSize) << std::endl;

    //fileCache.SaveTo("MFTReader_items.txt");

    fileCache.PrintLevelsStat();
    std::cout << "File Cache Total Count: " << fileCache.TotalCount() << std::endl;

    /*
    std::cout << "Exporting... " << std::endl;
    THArray<string_t> arr;
    fileCache.ToArray(arr);

    //auto dirCount = std::count_if(dirList.begin(), dirList.end(), [](FILE_NAME& a) { return IsDir(a.Attr); });
    //std::cout << toStringSepA(dirList.Count()) + " - total" << std::endl;
    //std::cout << toStringSepA(dirList.Count() - dirCount) + " - files" << std::endl; // only files 
    //std::cout << toStringSepA(fileCache.Count()) + " - total" << std::endl; // only dirs 

    std::cout << "Sorting... " << std::endl;
    std::sort(arr.begin(), arr.end());

    std::string filename = "ListMFTFile_sortedV1.log";
    LogEngine::TFileStream ff(filename);

    ff << toStringSepW(arr.Count()) + L" - total";
    //ff << toStringSepW(arr.Count() - dirCount) + L" - files"; // only files
    //ff << toStringSepW(dirCount) + L" - dirs"; // only dirs

    std::cout << "Saving list of files to " << filename << std::endl;
    
    string_t endl;
    BUILD_ENDL(endl);
    for (auto& item : arr)
     {
         ff << item << endl;
     }
     */
}



