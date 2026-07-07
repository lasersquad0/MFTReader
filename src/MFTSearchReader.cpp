
// this is to remove defines min, max in windows headers because they conflict with std::min std::max 
#define NOMINMAX

#include "Debug.h"
#include <windows.h>
#include <iostream>
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include <execution>

#include "strutils/include/string_utils.h"
#include "strutils/include/Ticks.h"
#include "LogEngine2/DynamicArrays.h"
#include "LogEngine2/LogEngine.h"
#include "Caches.h"
#include "FileLevel.h"
#include "FileCache.h"
#include "NTFS.h"
#include "Readers.h"


/**
* @brief Extracts list of files from node.DataRuns
* @details Extracts list of files from parameter node.DataRuns. Goes through all LCNs in Data Runs and extracts list of files
* Files are extracted in random order (in order of LCNs in Data Runs). Does not go to sub-nodes
* node.Bitmap is used to select which LCNs in Data Runs are valid (need to be processed)
* @param volData General info about NTFS volume. volData.BytesPerCluster used by the function
* @param node Contains Data Runs to be processed, and Bitmap that tells us what LCNs are valid.
* @param pred Predicate used for adding extracted files into external list. Called for each extracted file.
*/
/*
bool TMFTReaderBase::ProcessAllocDataRuns(DIR_NODE& node, AddFileAttrPred pred)
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
    uint64_t dataBufLen = 0; // dataBuf size in clusters
    uint32_t currRun = 0;
    while (currRun < node.DataRuns.Count())
    {
        if (bitsCounter > lastBit) // no more valid LCNs, break loop 
            break;

        DATA_RUN_ITEM& rli = node.DataRuns[currRun];
        logger.DebugFmt("[ProcessAllocDataRuns] Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);
        
        // check correctness of decoded LCNs
        assert(rli.len < (uint64_t)FVolumeData.TotalClusters.QuadPart);
        assert(rli.lcn < (uint64_t)FVolumeData.TotalClusters.QuadPart);
        assert((rli.lcn < (uint64_t)FVolumeData.MftZoneStart.QuadPart) || (rli.lcn > (uint64_t)FVolumeData.MftZoneEnd.QuadPart));

        //TODO MFT may be fragmented, it might be good idea to read list of MFT fragments in advance and check that rli.lcn does not inside any MFT fragment
        //assert((rli.lcn < (uint64_t)volData.MftStartLcn.QuadPart) || (rli.lcn > (uint64_t)(volData.MftStartLcn.QuadPart + volData.MftValidDataLength.QuadPart / volData.BytesPerCluster)));
        
        CLST rlilen = valuemin((CLST)(lastBit + 1 - bitsCounter), rli.len);

        if (rlilen > dataBufLen)
        {
            delete[] dataBuf;
            dataBuf = DBG_NEW uint8_t[rlilen * FVolumeData.BytesPerCluster];
            dataBufLen = rlilen;
        }

        assert(dataBuf);

        if (!ReadClusters(rli.lcn, rlilen, dataBuf))
            break;

        if (!FixupUSA(dataBuf, rli, rlilen))
        {
            logger.Error("FixupUSA returned error.");
            break;
        }

        INDEX_BUFFER* allocIndex = (INDEX_BUFFER*)dataBuf;
        uint8_t* dataBufEnd = dataBuf + rlilen * FVolumeData.BytesPerCluster;

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
            allocIndex = (INDEX_BUFFER*)Add2Ptr(allocIndex, FVolumeData.BytesPerCluster);
            cnt++; 
            bitsCounter++;

            if ((uint8_t*)allocIndex >= dataBufEnd) break;
        }

        currRun++;
    }

    delete[] dataBuf;

    logger.Debug("---------- END OF PROCESSING Data Runs ---------");

    return true;
}*/


/**
* @brief Fills level with files/dirs read from MFT record
* @details Fills one TLevel level with files/dirs read from MFT record refered by mftRecData
* mftRecData must refer to directory MFT record for base records 
* Parses the following FOUR attributes ONLY: ATTR_ROOT, ATTR_ALLOC, ATTR_BITMAP, ATTR_LIST_ATTR
*   ATTR_ROOT - gets list of files (usually this list is empty when appropriate ATTR_ALLOC exists)
*   ATTR_LIST_ATTR - goes to child records and parses 4 listed above attributes from there
*   ATTR_ALLOC - only does data runs decoding, does not go to LCNs.
* mftRec buffer should be volData.BytesPerMFTRec size
* @param volData Contains general information about volume, need for LoadMFTRecord function and for others
* @param mftRecData Pointer to MFT_FILE_RECORD structure in memory
* @param node Will be filled with BITMAP and Data Runs data
* @param parentIdx Index of parent item in upper level. Need for connection read items to parent item.
* @param level Pointer to level where new items will be added (current level)
* @return true if success, false in case of error
*/ 
bool TMFTSearchReader::ParseMFTRecord(uint8_t* mftRecData, DIR_NODE& node, uint32_t parentIdx, TFileCache::TLevel* level)
{
    GET_LOGGER;

    AddFileAttrPred pred = [parentIdx, &level](const ATTR_FILE_NAME* attr, const MFT_REF& ref)
        {
            // do not add NTFS internal files that are in root dir and start from $
            //if (!AttrIsNtfsInt(attr))
                level->AddValue(parentIdx, /*level->Level(),*/ ref, attr);
        };

    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecData;

    assert(mftRec->FileRecSize > mftRec->FirstAttrOffset);
    assert(ntfs_is_file_recp(mftRec->RecHeader.Signature));

    logger.Debug("---------- PARSING MFT RECORD ---------");

    bool IsBASERec = mftRec->ParentFileRec.Id == 0;

    logger.DebugFmt("Signature: {}", std::string((char*)mftRec->RecHeader.Signature, 4));
    logger.DebugFmt("MFT Rec ID: {}", MFT_REF::toHexString(mftRec->IndexMFTRec));
    logger.DebugFmt("MFT Rec Num re-used: {}", mftRec->SeqNum);
    logger.DebugFmt("MFT Parent Rec ID: {} {}", mftRec->ParentFileRec.toHexString(), IsBASERec ? " BASE" : "CHILD");
    logger.DebugFmt("MFT Hard links Count: {}", mftRec->HardLinksCnt, !IsBASERec ? " (may be inaccurate for child records)" : "");
    logger.DebugFmt("MFT Rec Size: {}", mftRec->FileRecSize);
    logger.DebugFmt("MFT Alloc Rec Size: {}", mftRec->AllocFileRecSize);

    switch (mftRec->Flags)
    {
    case MFT_FLAG_IN_USE: logger.Debug("MFT Rec Type: 'IN USE' (file or anything else)"); break;
    case MFT_FLAG_IS_DIRECTORY: logger.Debug("MFT Rec Type: DIRECTORY"); break;
    case MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY: logger.DebugFmt("MFT Rec Type: 'FILE or DIRECTORY' {:#x}", (uint16_t)mftRec->Flags); break;
    default:
        logger.WarnFmt("MFT Rec Type: UNKNOWN {:#x}", (uint16_t)mftRec->Flags);
    }

    
    // usually we call ParseMFTRecord ONLY for directories (for base MFT records).
    // somethimes ParseMFTRecord can be called for special kinds of records e.g. when attributes do not fit into base record and moved into a another "child" record.
    // such special MFT records may be either IN_USE or IN_USE|IS_DIRECTORY type
    // therefore I added if before assert
    if (IsBASERec)
    {
        if (mftRec->Flags != (MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY))
            logger.WarnFmt("Expecting directory base record, but MFT Record is NOT a directory. MFT Rec ID: {}, mftRec->Flags: {:#x}", 
                    MFT_REF::toHexString(mftRec->IndexMFTRec), (uint16_t)mftRec->Flags);
        assert(mftRec->Flags == (MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY)); // only "directory" record should go here for base records
    }

    MFT_ATTR_HEADER* currAttr = (MFT_ATTR_HEADER*)Add2Ptr(mftRec, mftRec->FirstAttrOffset);

    int attroOrderNum = 1;
    do  // reading all attributes in a loop
    {
        logger.DebugFmt("********** #{} Attribute ({} {:#x}) **********", attroOrderNum++, AttrName(currAttr->AttrType), (uint32_t)currAttr->AttrType);
        logger.Debug(currAttr->NonResidentFlag ? "Attr Type: NON-RESIDENT" : "Attr Type: RESIDENT");
        logger.DebugFmt("Attr ID: {}", currAttr->AttrID);
        logger.DebugFmt("Attr Size: {}", currAttr->AttrSize);
        logger.DebugFmt("Attr Flags: {}", currAttr->Flags);
    
        std::wstring nameOfAttrW;
        std::string nameOfAttrA = "<noname>";
        if (currAttr->AttrNameSize > 0) // if attr has name - show it
        {
            nameOfAttrW.assign(GetAttrName(currAttr, AttrNameOffset), currAttr->AttrNameSize);
            nameOfAttrA = wtos(nameOfAttrW);
            logger.DebugFmt("Attr Name: '{}'", nameOfAttrA);
        }

        if (currAttr->NonResidentFlag == 0) // attribute is RESident
        {
            logger.DebugFmt("Attr Indexed: {}", currAttr->res.IndexedFlag);

            assert(currAttr->res.DataSize + currAttr->res.DataOffset <= currAttr->AttrSize);
            uint8_t* attrValue = Add2Ptr(currAttr, currAttr->res.DataOffset); 

            switch (currAttr->AttrType)
            {
            case ATTR_ROOT: // Resident. ATTR_ROOT is always resident
            {
                assert(node.FileList.Count() == 0);

                ATTR_INDEX_ROOT* indexR = (ATTR_INDEX_ROOT*)attrValue;
                assert(indexR->AttrType == ATTR_FILENAME);
                assert(indexR->IndexBlockSize == getVolData().BytesPerCluster);
                assert(indexR->IndexBlockClst == 1);
                assert(indexR->Rule == COLLATION_RULE::FILENAME);

                logger.DebugFmt("IndexRoot Indexed Attr Type: {} {:#x}", AttrName(indexR->AttrType), (uint32_t)indexR->AttrType);
                logger.DebugFmt("IndexRoot Collation Rule: {} ({:#x})", CollRuleName((uint32_t)indexR->Rule), (uint32_t)indexR->Rule);
                logger.DebugFmt("IndexRoot Dir Type: {} ({:#x})", indexR->ihdr.Flags == 0 ? "SMALL DIR" : "BIG DIR", indexR->ihdr.Flags);

                auto pihdr = &(indexR->ihdr);

                // does not go to subnodes
                GetFileList(pihdr, pred);

                break;
            }
            case ATTR_LIST_ATTR: // Resident. ATTR_LIST_ATTR can be either resident or non-resident
            {
                logger.Debug("resident ATTR_LIST_ATTR START");

                assert(IsBASERec); // ATTR_LIST cannot be located in child record
                assert(node.FileList.Count() == 0);

                THArray<uint32_t> visitedMFTRec;
                visitedMFTRec.AddValue(mftRec->IndexMFTRec);

                ATTR_LIST_ENTRY* attrListItem = (ATTR_LIST_ENTRY*)attrValue;
                uint8_t* currAttrEnd = Add2Ptr(currAttr, currAttr->AttrSize);
                assert(attrListItem->AttrType > 0);
                assert(attrListItem->AttrSize > 0);
                assert(attrListItem->StartVCN == 0);
                assert(((uint32_t)(attrListItem->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero

                // at least one attribute is located in another MFT record, so we will need this alloc at least once
                uint8_t* mftRecBuf = (uint8_t*)alloca(getVolData().BytesPerMFTRec);

                while (true)
                {
                    // attributes in attr list located in a separate cluster may refer back to the base record
                    // because some attributes reside in base mft record and the others in "child" mft record(s)
                    // the attr list attribute itself is located in cluster that is not mft record, it does not contain signature or Fixups values, etc.
                    
                    //if (attrEntry->ref.sId.low != mftRec->IndexMFTRec)
                    //{

                    if ((attrListItem->AttrType == ATTR_ALLOC) || (attrListItem->AttrType == ATTR_ROOT) || (attrListItem->AttrType == ATTR_BITMAP))
                    {
                        // StartVCN might be >0 when one attribute does not fit into one MFT record.
                        // This attribute may have very long Data Run list or anything else
                        // In this case ATTR_LIST contains several ATTR_LIST_ENTRY entries for this big attribute.
                        // First entry has StartVCN=0, others - preventry.StartVCN+num_of_vcns_in_preventry_dataruns, etc.
                        // all these entries build up a continious list of VCNs 
                        if (attrListItem->AttrType != ATTR_ALLOC) // StartVCN should be 0 for all attrs except ATTR_DATA and ATTR_ALLOC
                        {
                            if (attrListItem->StartVCN != 0)
                                logger.WarnFmt("Looks like we have met incorrect case. StartVCN({}) <> 0 for {} attribute. MFT Rec ID: {}.",
                                    attrListItem->StartVCN, AttrName(attrListItem->AttrType), MFT_REF::toHexString(mftRec->IndexMFTRec));
                            assert(attrListItem->StartVCN == 0);
                        }

                        if (visitedMFTRec.IndexOf(attrListItem->ref.sId.low) == -1) // make sure we parse each record only once
                        {
                            if (FLoader.LoadMFTRecord(attrListItem->ref, mftRecBuf))
                            {
                                if (ParseMFTRecord(mftRecBuf, node, parentIdx, level)) //TODO shall we break in case of an error in LoadMFTRecord or ParseMFTRecord 
                                    visitedMFTRec.AddValue(attrListItem->ref.sId.low);
                                else
                                    logger.Error("ParseMFTRecord finished with error.");
                            }
                            else
                            {
                                logger.Error("LoadMFTRecord finished with error.");
                            }
                        }
                    }
                    //}

                    attrListItem = (ATTR_LIST_ENTRY*)Add2Ptr(attrListItem, attrListItem->AttrSize);
                    //if (attrListItem->AttrType == ATTR_ZERO) break;
                    if ((uint8_t*)attrListItem >= currAttrEnd) break;
                    assert(attrListItem->AttrType > 0);
                    assert(attrListItem->AttrSize > 0);
                    //assert(attrListItem->StartVCN == 0);
                    assert(((uint32_t)(attrListItem->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
                }

                logger.Debug("resident ATTR_LIST_ATTR FINISHED");

                break;
            }
            case ATTR_BITMAP: // Resident. Bitmap can be resident or non-resident
            {
                assert(node.Bitmap.Count() == 0);

                ATTR_BITMAP_ATTR* bmp = (ATTR_BITMAP_ATTR*)attrValue;
                logger.DebugFmt("ATTR_BITMAP Size in bytes: {}, Value64: {:#x}", currAttr->res.DataSize, *(uint64_t*)bmp);

                assert((currAttr->res.DataSize & 0x07) == 0); // bitmap data size always multiple of 8

                node.Bitmap.SetData((uint64_t*)bmp->bitmap, currAttr->res.DataSize >> 3);

                break;
            }
            case ATTR_DATA: // Resident. ATTR_DATA can be resident or non-resident
            {
                logger.InfoFmt("Resident ATTR_DATA. Size: {}", currAttr->res.DataSize);
                logger.DebugFmt("We do not process this resident attribute. Attr: '{}', AttrName: '{}'.", AttrName(currAttr->AttrType), nameOfAttrA);
                break;
            }
            case ATTR_STD_INFO: // resident only
            case ATTR_FILENAME: // resident only
            case ATTR_ID:
            case ATTR_SECURE:
            case ATTR_REPARSE: // can be resident or non-resident
            case ATTR_EA:      // can be resident or non-resident  
            case ATTR_EA_INFO: // can be resident or non-resident
            case ATTR_LOGGED_UTILITY_STREAM: // can be resident or non-resident
            {
                logger.DebugFmt("We do not process this resident attribute. Attr: '{}', AttrName: '{}'.", AttrName(currAttr->AttrType), nameOfAttrA);
                break;
            }
            
            default:
                logger.WarnFmt("UNKNOWN Resident ATTR has been met. Attr: {0}, AttrName: {1}, MFT Rec ID: {2:#x} ({2})", AttrName(currAttr->AttrType), nameOfAttrA, mftRec->IndexMFTRec);
            } //switch
        }
        else // Attribute is NON-resident
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
                    currAttr->nonres.RealSize, currAttr->nonres.StreamSize, MFT_REF::toHexString(mftRec->IndexMFTRec));

            if (currAttr->nonres.RealSize < currAttr->nonres.StreamSize)
                logger.WarnFmt("'currAttr.RealSize < currAttr.StreamSize'. ReadlSize: {}, StreamSize: {}, MFT Rec ID: {}",
                    currAttr->nonres.RealSize, currAttr->nonres.StreamSize, MFT_REF::toHexString(mftRec->IndexMFTRec));

            switch (currAttr->AttrType)
            {
            case ATTR_BITMAP: // NON-resident. ATTR_BITMAP can be either resident and non-resident
            {
                logger.InfoFmt("Non-Resident ATTR_BITMAP ('{}') has been met. MFT Rec ID {}", nameOfAttrA, MFT_REF::toHexString(mftRec->IndexMFTRec));
            
                if (!ParseNonresBitmap(currAttr, node.Bitmap))
                {
                    logger.Error("ParseNonresBitmap finished with error.");
                }

                break;
            }
            case ATTR_ALLOC: // NON-resident. ATTR_ALLOC is always non-resident.
            {
                // sometimes one ATTR_LIST_ATTR list may contain two ATTR_ALLOC attributes
                // it means we come here two times during parsing one MFT record with such ATTR_LIST_ATTR 
                if (node.DataRuns.Count() > 0)
                {
                    assert(!IsBASERec); // base record cannot contain two ATTR_ALLOC attributes, while child records referred by ATTR_LIST can
                    logger.InfoFmt("Multiple ATTR_ALLOC attributes have met. node.DataRuns.Count(): {}, Child MFT Rec ID: {}, Base MFT Rec ID: {}",  
                            node.DataRuns.Count(), MFT_REF::toHexString(mftRec->IndexMFTRec), mftRec->ParentFileRec.toHexString());
                }

                if (!DecodeDataRuns(currAttr, node.DataRuns))
                {
                    logger.Error("DataRunsDecode finished with error.");
                }

                // we do not process ALLOC data runs here because in this place we do not know all required info, for example Bitmap attribute.
                // that is why data runs processing is done in calling function.
                break;
            }
            case ATTR_LIST_ATTR: // NON-resident. ATTR_LIST_ATTR can be either resident and non-resident
            {
                logger.Info("NON-Resident ATTR_LIST has been met");
                logger.Debug("NON-Resident ATTR_LIST_ATTR START");

                assert(IsBASERec); // ATTR_LIST cannot be located in child record
                assert(node.FileList.Count() == 0);

                TDataRuns dataRuns;
                if (!DecodeDataRuns(currAttr, dataRuns)) // DataRunsDecode writes a message into log file in case of an error
                {
                    break;
                }

                assert(dataRuns.Count() == 1); //assuming that one data run is always enough for directory attr list

                DATA_RUN_ITEM& rli = dataRuns[0];
                logger.DebugFmt("Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

                assert(rli.len == 1); // assuming that one LCN is enough for directory attr list
                
                auto dataBufSize = rli.len * getVolData().BytesPerCluster;
                uint8_t* dataBuf = (uint8_t*)alloca(dataBufSize);

                if (!ReadClusters(rli.lcn, rli.len, dataBuf)) // ReadClusters writes a message into log file in case of an error
                {
                    logger.Error("ReadClusters finished with error."); 
                    break;
                }

                ATTR_LIST_ENTRY* attrListItem = (ATTR_LIST_ENTRY*)dataBuf;
                uint8_t* attrListEnd1 = dataBuf + currAttr->nonres.RealSize;
                uint8_t* attrListEnd2 = dataBuf + dataBufSize;
                
                assert(attrListItem->AttrType > 0);
                assert(attrListItem->AttrSize > 0);
                assert(attrListItem->StartVCN == 0);
                assert(((uint32_t)(attrListItem->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero

                THArray<uint32_t> visitedMFTRec;
                visitedMFTRec.AddValue(mftRec->IndexMFTRec);

                while (true) // loop by LCNs in one data run
                {
                    // attributes in attr list located in a separate cluster may refer back to the base record
                    // because some attributes reside in base mft record and the others in "child" mft record(s)
                    // the attr list attribute itself is located in cluster that is not mft record, it does not contain signature or Fixups values, etc.
                    //if (attrListItem->ref.sId.low != mftRec->IndexMFTRec)
                    //{

                        if ((attrListItem->AttrType == ATTR_ALLOC) || (attrListItem->AttrType == ATTR_ROOT) || (attrListItem->AttrType == ATTR_BITMAP))
                        {
                            // StartVCN might be >0 when one attribute does not fit into one MFT record.
                            // This attribute may have very long Data Run list or anything else
                            // In this case ATTR_LIST contains several ATTR_LIST_ENTRY entries for this big attribute.
                            // First entry has StartVCN=0, others - preventry.StartVCN+num_of_vcns_in_preventry_dataruns, etc.
                            // all these entries build up a continious list of VCNs 
                            if (attrListItem->AttrType == ATTR_ALLOC) // StartVCN should be 0 for all attrs except ATTR_DATA and ATTR_ALLOC
                            {
                                // StartVCN is a cluster where attribute portion value is located
                                if (attrListItem->StartVCN != 0)
                                {
                                    logger.WarnFmt("One attribute does not fit into one MFT record. StartVCN: {}, AttrType: {}, ref: {}, MFT Rec ID: {}",
                                          attrListItem->StartVCN, AttrName(attrListItem->AttrType), attrListItem->ref.toHexString(), MFT_REF::toHexString(mftRec->IndexMFTRec));
                                }
                            }
                            else
                            {
                                if (attrListItem->StartVCN != 0)
                                    logger.WarnFmt("Looks like we have met incorrect case. StartVCN({}) <> 0 for {} attribute. MFT Rec ID: {}.",
                                        attrListItem->StartVCN, AttrName(attrListItem->AttrType), MFT_REF::toHexString(mftRec->IndexMFTRec));
                                assert(attrListItem->StartVCN == 0);
                            }

                            if (visitedMFTRec.IndexOf(attrListItem->ref.sId.low) == -1) // make sure we parse each record only once
                            {
                                uint8_t* mftRecBuf = FLoader.LoadMFTRecordCache(attrListItem->ref);
                                assert(mftRecBuf != nullptr);
                                if (mftRecBuf)
                                {
                                    //TODO There may be a case when 2 attributes located in a one child MFT record. 
                                    // They will be parsed twice now. Think of solution for it. 
                                    if (!ParseMFTRecord(mftRecBuf, node, parentIdx, level))
                                        logger.Error("ParseMFTRecord finished with error.");
                                }
                                else
                                {
                                    logger.Error("LoadMFTRecordCache returned nullptr.");
                                }

                                visitedMFTRec.AddValue(attrListItem->ref.sId.low);
                            }
                      //  }
                    }

                    attrListItem = (ATTR_LIST_ENTRY*)Add2Ptr(attrListItem, attrListItem->AttrSize);
                    if ((uint8_t*)attrListItem >= attrListEnd1) break;
                    if ((uint8_t*)attrListItem >= attrListEnd2) break;
                    assert(attrListItem->AttrType > 0);
                    assert(attrListItem->AttrType != ATTR_LIST_ATTR);// cannot be ATTR_LIST inside another ATTR_LIST
                    assert(((uint32_t)(attrListItem->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
                    assert(attrListItem->AttrSize > 0);
                }

                logger.Debug("NON-Resident ATTR_LIST_ATTR FINISH");
                break;
            }

            case ATTR_DATA:    // can be resident or non-resident
            case ATTR_REPARSE: // can be resident or non-resident
            case ATTR_EA_INFO: // can be resident or non-resident
            case ATTR_EA:      // can be resident or non-resident
            case ATTR_LOGGED_UTILITY_STREAM: // can be resident or non-resident
            {
                logger.DebugFmt("We do not process this non-resident attribute. Attr: '{}', AttrName: '{}'.", AttrName(currAttr->AttrType), nameOfAttrA);
                break;
            }

            default:
                logger.WarnFmt("UNKNOWN NON-Resident ATTR has been met. Attr: {0}, AttrName: {1}, MFT Rec Id: {2:#x} ({2})", AttrName(currAttr->AttrType), nameOfAttrA, mftRec->IndexMFTRec);

            } //switch
        }

        currAttr = (MFT_ATTR_HEADER*)Add2Ptr(currAttr, currAttr->AttrSize);
        assert(mftRec->FileRecSize > Diff2Ptr(mftRec, currAttr));

    } while (*((DWORD*)currAttr) != ATTR_END);

    logger.Debug("---------- END OF PARSING MFT RECORD ---------");

    return true;
}

/**
 * @brief Loads files/dirs into gFileList recursively
 * @details Loads files/dirs refered by parentItem, then goes into subdirs and loads files/dirs from there
 * Adds loaded info gFileList level by level. gFileList can then be used for quick searching for files in volume.
 * Reads only INDEX_ROOT, ALLOC, BiTMAP attributes. Also parses ATTR_LIST attribute too when met.
 * File information is got from aINDEX_ROOT and ALLOC attributes only.
 * Calculates and stores size of each directory met.
 * ReadDirectoryV1 may call itself reccurcively when needed to read CHILD MFT records.
 * @param parentIdx Index of parent item in previous/parent level of gFileList
 * @param parentItem Item (directory) whose files will be read. NULL for root item.
 * @param dirSize Passed by ref because we return dir size to upper directory 
 * @param callback Since reading may take a time function tells its progress to caller via callback of ProgressCallbackPtr type.
*/
bool TMFTSearchReader::ReadDirectoryV1(uint32_t parentIdx, CACHE_ITEM* parentItem, uint64_t& dirSize, ProgressCallbackPtr callback)
{
    static int32_t ProgressCounter = 0;

    if (parentItem == nullptr) assert(parentIdx == 0); // parentIdx must be 0 for root item

    GET_LOGGER;

    uint8_t* mftRecBuf = (uint8_t*)alloca(getVolData().BytesPerMFTRec);

    MFT_REF MFTRec{0};
    if (parentItem == nullptr)
        MFTRec.Id = MFT_ROOT_REC_ID;
    else
        MFTRec = parentItem->FMFTRecID;

    if (!FLoader.LoadMFTRecord(MFTRec, mftRecBuf))
    {
        logger.Error("LoadMFTRecord finished with error.");
        return false;
    }


    // if we are on the root dir - add root item into gFileList
    // then change levelIdx to 1 to properly read root dirs/files into level 1 instead of 0
    if (parentItem == nullptr)
    {
        ProgressCounter = 0;
        MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;
        MFT_ATTR_HEADER* currAttr = (MFT_ATTR_HEADER*)Add2Ptr(mftRec, mftRec->FirstAttrOffset);
        ATTR_STD_INFO5* stdinfo = (ATTR_STD_INFO5*)Add2Ptr(currAttr, currAttr->res.DataOffset);
        
        assert(currAttr->AttrType == ATTR_STD_INFO);

        auto level0 = FFileList.GetLevel(0);
        assert(level0->Count() == 0);

        auto fattrSize = sizeof(ATTR_FILE_NAME) + getVolData().Name.size() * sizeof(wchar_t);
        ATTR_FILE_NAME* fattr = (ATTR_FILE_NAME*)alloca(fattrSize);
        ZeroMemory(fattr, fattrSize);

        fattr->FileNameLen = (uint8_t)getVolData().Name.size(); //name of disk (C:, D:, etc)
        fattr->NameType    = FILE_NAME_UNICODE_AND_DOS;
        fattr->ParentDir   = MFTRec;
        fattr->dup.CreateTime     = stdinfo->CreateTime;
        fattr->dup.ModifyTime     = stdinfo->ModifyTime;
        fattr->dup.ModifyAttrTime = stdinfo->ModifyAttrTime;
        fattr->dup.LastAccessTime = stdinfo->LastAccessTime;
        // this is HACK because STD attr does not contain dir flag for '.' directory for some reason. while FILENAME attr for '.' does contain dir flag.
        // many articles about NTFS tell us that STD attr does not contain DIR flag while FILENAME does 
        fattr->dup.FileAttrib = stdinfo->FileAttrib | (uint32_t)FILE_ATTR_FLAGS::DIRECTORY; 

        auto res = wmemcpy_s((wchar_t*)Add2Ptr(fattr, sizeof(ATTR_FILE_NAME)), fattr->FileNameLen, getVolData().Name.c_str(), fattr->FileNameLen);
        UNREFERENCED_PARAMETER(res);
        assert(!res);

        level0->AddValue(0, MFTRec, fattr);

        //IMPORTANT: change parentItem to new parent to proper read files from root dir into level 1 (level 0 is a root dir like C: or D:) 
        parentItem = level0->GetValue(0);
    }

    assert(parentItem->IsDir()); // only directory can be as parentItem

    auto level = FFileList.GetLevel(parentItem->FLevel + 1);
    assert(level->Level() == parentItem->FLevel + 1);
    uint32_t startPos = level->Count(); // remember start position for newly added items
    //CACHE_ITEM* startItem = level->Last(); // NOTE! Last() returns pointer to item that WILL BE added next. Also startItem may become invalid if realloc happened in the level duing adding new items

    DIR_NODE node; //TODO replace two last parameters with predicate FileListPred similar to passed into ProcessAllocDataRuns below.
    if (!ParseMFTRecord(mftRecBuf, node, parentIdx, level))
    {
        logger.Error("ParseMFTRecord finished with error.");
        return false;
    }

    AddFileAttrPred addToFileListPred = [parentIdx, &level](const ATTR_FILE_NAME* attr, const MFT_REF& ref)
        {
            // do not add NTFS internal files that are in root dir and start from $
            //if (!AttrIsNtfsInt(attr))
                level->AddValue(parentIdx, ref, attr);
        };

    //TODO this is the same predicate code as in FileStat.cpp. Think how to avoid duplication
    ProcessLCNsPred processAllocPred = [this, &addToFileListPred](uint8_t* dataBuf, CLST VCN, CLST LCN)
        {
            auto allocIndex = (INDEX_BUFFER*)dataBuf;

            // read items only if cluster starts from correct signature INDX
            // sometimes fully empty (filled with zero) clusters present in run list without INDX signature
            if (ntfs_is_indx_recp(allocIndex->RecHeader.Signature))
            {
                assert(VCN == allocIndex->vcn);

                auto pihdr = &(allocIndex->ihdr);
                GetFileList(pihdr, addToFileListPred);
            }
            else
            {
                GET_LOGGER;
                uint8_t* sign = allocIndex->RecHeader.Signature;
                logger.WarnFmt("Signature 'INDX' has not been found in LCN cluster {}. Signature found: {}{}{}{}", LCN, sign[0], sign[1], sign[2], sign[3]);
            }
        };

    if (node.DataRuns.Count() > 0)
    {
        if (!ProcessDataRuns(node, processAllocPred) )
        {
            logger.Error("ProcessAllocDataRuns finished with error.");
            //return is not needed here because node.FileList may contain items from INDEX_ROOT and partially from ALLOCATION
            //return false;
        }
    }

    auto newcnt = level->Count(); // count will be changing during for loop below 
    CACHE_ITEM* item = nullptr;

    // this is check if realloc has happened in level array during ParseMFTRecord and ProcessAllocDataRuns calls
    // if it is then startItem pointer became invalid and we need to locate that item again
    //if(level->Belongs(startItem)) // Belongs will return false when startItem==level->last()
    //    item = startItem;
    //else
    if (newcnt > startPos)
    {
        item = level->GetValue(startPos);
        assert(item->FLevel == parentItem->FLevel + 1);
        assert(item->FParent == parentIdx);
    }
    
    // item may be NULL here for empty directories when startPos=newcnt=0
    
    assert(parentItem->IsDir());

    dirSize = 0;
    parentItem->FFilesCount = newcnt - startPos;

    for (uint32_t i = startPos; i < newcnt; i++)
    {
        assert(item);
        
        if (!item->NtfsInternal())
        {
            if (item->IsDir())
            {
                if (!item->IsReparse()) //bypass reparse items
                {
                    uint64_t childDirSize{ 0 };
                    if (!ReadDirectoryV1(i, item, childDirSize, callback))
                        logger.ErrorFmt("ReadDirectoryV1 finished with error for MFT Rec ID: {}", item->FMFTRecID.toHexString());
                    dirSize += childDirSize;
                }
            }
            else // file, not a directory
            {
                dirSize += item->FileAttr.dup.FileSize;
            }

            // print only dirs of first level.
            if (parentItem->FLevel == 0)
            {
                // callback can be NULL
                if (callback) callback(ProgressCounter++); //call callback only for items from root directory
                logger.InfoFmt("{}  [{}]", wtos(std::wstring(item->Name(), item->FileAttr.FileNameLen)), toStringSepA(item->FileAttr.dup.FileSize));
            }

            //if (parentItem->FLevel == 1) 
            //    logger.InfoFmt("\t{} [{}]", wtos(std::wstring(item->Name(), item->FileAttr.FileNameLen)), toStringSepA(item->FileAttr.dup.FileSize));
        }

        item = level->Next(item);
    }

    parentItem->FileAttr.dup.FileSize = dirSize;

    return true;
}

void TMFTSearchReader::ReadDirsV1()
{
    //TFileCache fileCache;
    uint64_t rootDirSize{0};

    std::wcout << _T("Reading volume: ") << getVolData().Name << std::endl;

    Ticks::Start(_T("Loading time"));
    if (!ReadDirectoryV1(0, nullptr, rootDirSize, nullptr))
    {
        throw std::runtime_error("ReadDirectoryV1 finished with error.");
    }
    Ticks::Finish(_T("Loading time"));


    std::cout << std::endl << "Volume root dir size: " << toStringSepA(rootDirSize) << " bytes" << std::endl;
    std::cout << "Total Items Count: " << FFileList.TotalCount() << std::endl;

    //fileCache.SaveTo("MFTReader_items.txt");

    FFileList.PrintLevelsStat();
    
   /* std::cout << "Converting to plain array... " << std::endl;
  
    Ticks::Start(_T("Converting time"));
    THArray<ci_string> arr;
    arr.SetCapacity(fileCache.TotalCount() + 1); // 1 is just in case
    fileCache.ToArray(arr);
    Ticks::Finish(_T("Converting time"));

    std::cout << "Sorting... " << std::endl;
    
    Ticks::Start(_T("Sorting time"));
    std::sort(std::execution::par, arr.begin(), arr.end());
    Ticks::Finish(_T("Sorting time"));

    std::string filename = "ListMFTFile_sortedV1.log";
    LogEngine::TFileStream ff(filename);

    string_t fendl;
    BUILD_ENDL(fendl);
    ff << _T("Total Items Count: ") << toStringSep<uint,string_t>(arr.Count()) << fendl;

    std::cout << "Saving list of files to '" << filename << "'..." << std::endl;
    
    Ticks::Start(_T("Saving time"));
    for (auto& item : arr)
    {
        ff << item << fendl;
    }
    Ticks::Finish(_T("Saving time"));
    */
    Ticks::PrintCon(1);
    
     
}



