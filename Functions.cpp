
// this is to remove defines min, max in windows headers because they conflict with std::min std::max 
#define NOMINMAX

#include <iostream>
#include <string>
#include <cassert>
//#include <malloc.h>

#include "string_utils.h"
#include "LogEngine.h"
#include "MTFReader.h"
#include "Functions.h"

LogEngine::Logger& GetLoggerFunc()
{
    LogEngine::Logger& logger = LogEngine::GetFileLogger(MFT_LOGGER_NAME_FUNC, "LogMFTReaderFUNC.log");
    logger.SetAsyncMode(true);
    logger.SetLogLevel(LogEngine::Levels::llDebug);
    return logger;
}

void GetFileListFromNode(INDEX_HDR* ihdr, TLCNRecs& lcns, THArray<FILE_NAME>& fnames)
{
    GET_LOGGER;
    
    uint32_t off = ihdr->DEOffset; // offset of 1st dir entry

    while (true) // iterate though all DE+FILE_NAME entries
    {
        assert(off < ihdr->Used);

        NTFS_DE* de = (NTFS_DE*)Add2Ptr(ihdr, off); // NTFS_DE it is a "header" above File Name attribute, covers each file name attribute item

        logger.DebugFmt("[GetFileListFromNode] Dir Entry File rec {:#x}", de->ref.Id);
        logger.DebugFmt("[GetFileListFromNode] Dir Entry size: {}", de->size);
        logger.DebugFmt("[GetFileListFromNode] Dir Entry key_size: {}", de->key_size);
        logger.DebugFmt("[GetFileListFromNode] Dir Entry flags: {}", de->flags);

        assert(de->size >= de->key_size + sizeof(NTFS_DE));

        if (de->flags & NTFS_IE_HAS_SUBNODES)
        {
            CLST vcn = *(CLST*)Add2Ptr(ihdr, off + de->size - sizeof(uint64_t)); // last 8 bytes contain the VÑN of subnode. This field is presented only if (flags & NTFS_IE_HAS_SUBNODES)
    
            auto rec = lcns.GetRecByVCN(vcn);
            logger.DebugFmt("[GetFileListFromNode] Dir Entry has subnodes located in VCN={}, LCN={}", vcn, rec.first);

            INDEX_BUFFER* allocIndex = (INDEX_BUFFER*)rec.second;
            
            // process items only if cluster starts from correct signature INDX
            // sometimes fully empty (filled with zero) clusters present in run list without starting INDX signature
            if (ntfs_is_indx_recp(allocIndex->RecHeader.Signature))
            {
                assert(vcn == allocIndex->vcn);

                auto pihdr = &(allocIndex->ihdr);
                GetFileListFromNode(pihdr, lcns, fnames);
            }
            else // INDX not found
            {
                uint8_t* sign = allocIndex->RecHeader.Signature;
                logger.WarnFmt("[GetFileListFromNode] Signature 'INDX' has not been found in LCN cluster {}. Signature found: {}{}{}{}", lcns.GetRecByVCN(vcn).first, sign[0], sign[1], sign[2], sign[3]);
            }
        }

        if (de->key_size > 0) // key_size>0 means that FileName attr exists
        {
            ATTR_FILE_NAME* fattr = (ATTR_FILE_NAME*)Add2Ptr(de, sizeof(NTFS_DE));

            assert(de->key_size = sizeof(ATTR_FILE_NAME) + fattr->FileNameLen);

            if (fattr->NameType != FILE_NAME_DOS) // bypass DOS filenames
            {
                ci_string ciwnm(GetFName(fattr, sizeof(ATTR_FILE_NAME)), fattr->FileNameLen);
                logger.DebugFmt("[GetFileListFromNode] Dir Entry Parent rec ID: {:#x}", fattr->ParentDir.Id);
                logger.DebugFmt("[GetFileListFromNode] Dir Entry File/Dir name: '{}'", wtos(ciwnm));

                fnames.AddValue({ ciwnm, *fattr, de->ref });
            }
        }

        off += de->size; // moving to the next DE

        // check if this is last DE or we have exceeded pihdr->used
        if (((de->flags & NTFS_IE_LAST) > 0) || (de->size < sizeof(NTFS_DE)) || (off >= ihdr->Used)) // off refers to next DE here
        {
            break;
        }
    }
}

// attr parameter cannot be NULL here. It should be valid pointer to MFT_ATTR_HEADER structure
bool DataRunsDecode(MFT_ATTR_HEADER* attr, THArray<DATA_RUN_ITEM>& runs)
{
    GET_LOGGER;

    // we do not need this assert because sometimes one ATTR_LIST list may contain two ATTR_ALLOC attributes for some reason
    // it means we come here two times during parsing one MFT record with such ATTR_LIST 
    //assert(runs.Count() == 0); // make sure that old data runs are cleared

    if (!attr || !attr->NonResidentFlag) // parameters validation
    {
        // this in incorrect data in MFT
        logger.Error("[DataRunsDecode] Attr (!currAttr || !currAttr->NonResidentFlag) = TRUE");
        return false;
    }

    uint8_t* datarun = Add2Ptr(attr, attr->nonres.DataRunsOffset);
    uint8_t* attrEnd = Add2Ptr(attr, attr->AttrSize);
    assert(attr->AttrSize > 0);

    uint64_t currVCN = attr->nonres.StartVCN;
    uint64_t currLCN = 0;

    // read all data runs 
    while ((datarun < attrEnd) && *datarun) // stop if we reached zero in both half bytes
    {
        DATA_RUN_ITEM ri;
        int64_t deltaxcn;

        ri.lcn = currLCN;
        ri.vcn = currVCN;

        uint8_t lens = *datarun;
        uint8_t b = lens & 0x0F; // minor half byte is length (in bytes) of the following int value "number of clusters in current data run"
        if (b)
        {
            // reading number of bytes specified in minor half byte and interpret it as integer "number of clusters"
            for (deltaxcn = datarun[b--]; b; b--)
                deltaxcn = (deltaxcn << 8) + datarun[b];
        }
        else
        {
            // the length entry cannot be zero
            logger.Error("[DataRunsDecode] Missing length entry in mapping pairs (run len) array.");
            //deltaxcn = (int64_t)-1;
            return false;
        }

        assert(deltaxcn > 0);
        ri.len = deltaxcn;
        currVCN += deltaxcn;

        // major half byte is a length (in bytes) the of LCN 
        uint8_t b2 = lens & 0x0F;
        uint8_t b3 = b = b2 + ((lens >> 4) & 0x0F);
        deltaxcn = (datarun[b] & 0x80) ? (uint64_t)-1 : 0; // delta LCN can be negative in datarun! Fill initial deltaxcn with 0xFFF..FFF in that case 
        //deltaxcn = datarun[b--]
        for (; b > b2; b--) // read num of bytes specified in major half byte and interpret it as LCN
            deltaxcn = (deltaxcn << 8) + datarun[b];
        
        ri.lcn += deltaxcn;
        currLCN = ri.lcn;
        
        runs.AddValue(ri);

        datarun += b3 + 1; // move to the next data run

        logger.DebugFmt("DataRunsDecode VCN: {}", ri.vcn);
        logger.DebugFmt("DataRunsDecode LCN: {}", ri.lcn);
        logger.DebugFmt("DataRunsDecode Length: {}", ri.len);
    }

    logger.DebugFmt("[DataRunsDecode] Data Runs Count: {}", runs.Count());

    return true;
}

// reads series of sequential volume clusters starting from #lcnStart
// dataBuf should be large enough to fit lcnCnt clusters of data
bool ReadClusters(const VOLUME_DATA& volData, uint64_t lcnStart, uint32_t lcnCnt, PBYTE dataBuf)
{
    LARGE_INTEGER offset;
    DWORD bytesRead;

    offset.QuadPart = lcnStart * volData.BytesPerCluster;

    BOOL res = SetFilePointerEx(volData.hVolume, offset, NULL, FILE_BEGIN);
    if (!res)
    {
        GET_LOGGER;
        logger.ErrorFmt("ReadCluster.SetFilePointerEx has failed with error: {}", GetLastError());
        return false;
    }

    // read clusterCnt clusters
    res = ReadFile(volData.hVolume, dataBuf, lcnCnt * volData.BytesPerCluster, &bytesRead, nullptr);
    if (res)
    {
        assert(lcnCnt * volData.BytesPerCluster == bytesRead);
        return true;
    }
    else
    {
        GET_LOGGER; 
        logger.ErrorFmt("ReadCluster.ReadFile has failed with error: {}", GetLastError());
        return false;
    }
}

// dataBuf contains data for rli.len clusters
bool FixupUSA(const VOLUME_DATA& volData, PBYTE dataBuf, DATA_RUN_ITEM& rli, uint32_t rlilen)
{
    NTFS_RECORD_HEADER* indexRec = (NTFS_RECORD_HEADER*)dataBuf;
    uint32_t wordsPerSector = volData.BytesPerSector >> 1;

    // loop by LCNs loaded into dataBuf
    for (size_t i = 0; i < rlilen; i++)
    {
        if (!ntfs_is_indx_recp(indexRec->Signature)) // bypass non 'INDX' clusters (usually filled by zero)
        {
            GET_LOGGER;
            uint8_t* sign = indexRec->Signature;
            logger.WarnFmt("[FixupUSA] Signature 'INDX' has not been found in LCN cluster {}. Signature found: {}{}{}{}", rli.lcn + i, sign[0], sign[1], sign[2], sign[3]);
            
            continue;
        }

        uint16_t sectorsCnt = indexRec->FixupCnt - 1;
        assert(sectorsCnt == volData.BytesPerCluster / volData.BytesPerSector);

        uint16_t* fixuparr = (uint16_t*)(Add2Ptr(indexRec, indexRec->FixupOffset));
        uint16_t checkValue = *fixuparr;
        fixuparr++; // now it refers to first array item

        uint16_t* sectorEnd = (uint16_t*)(indexRec) + wordsPerSector - 1;

        uint s = 0;
        while (s < sectorsCnt)
        {
            assert(checkValue == *sectorEnd);
            if (checkValue != *sectorEnd) return false; // looks like data is corrupted in this sector

            *sectorEnd = fixuparr[s]; // restore data

            sectorEnd += wordsPerSector;
            s++;
        }

        indexRec = (NTFS_RECORD_HEADER*)Add2Ptr(indexRec, volData.BytesPerCluster);
    }

    return true;
}

// mftRec should be a buffer with volData.BytesPerMFTRec size
bool LoadMFTRecord(const VOLUME_DATA& volData, MFT_REF recID, uint8_t* mftRec)
{
    GET_LOGGER;

    NTFS_FILE_RECORD_INPUT_BUFFER nfrib;
    nfrib.FileReferenceNumber.QuadPart = recID.Id;
    //nfrib.FileReferenceNumber.QuadPart = nvdb.MftValidDataLength.QuadPart / nvdb.BytesPerFileRecordSegment - 1;

    //ULONG cb = __builtin_offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer[volData.BytesPerMFTRec]);
    ULONG cb = offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer[volData.BytesPerMFTRec]);
    
    auto pnfrob = (PNTFS_FILE_RECORD_OUTPUT_BUFFER)alloca(cb);

    if (!DeviceIoControl(volData.hVolume, FSCTL_GET_NTFS_FILE_RECORD, &nfrib, sizeof(nfrib), pnfrob, cb, 0, nullptr))
    {
        logger.ErrorFmt("DeviceIoControl failed with error. Error code: {}", GetLastError());
        return false;
    }

    MFT_FILE_RECORD* mftRecord = (MFT_FILE_RECORD*)(pnfrob->FileRecordBuffer);
    assert(mftRecord->IndexMFTRec == recID.sId.low); // make sure we've got the same record as requested.

    //TODO think how to avoid this memcpy_s
    memcpy_s(mftRec, volData.BytesPerMFTRec, mftRecord, volData.BytesPerMFTRec);

    return true;
}

// returns NULL if error occurred during loading MFT record
uint8_t* LoadMFTRecordCache(const VOLUME_DATA& volData, MFT_REF recID)
{
    TMFTRecCache* cache = Singleton<TMFTRecCache>::getInstance();

    uint8_t** result = cache->GetValuePointer(recID.Id);
    if (result == nullptr) // no value in cache, load mft record from disk
    {
        uint8_t* mftRecBuf = DBG_NEW uint8_t[volData.BytesPerMFTRec];
        if (!LoadMFTRecord(volData, recID, mftRecBuf))
            return nullptr; // error loading mft record, it means that DeviceIoControl failed with error

        cache->SetValue(recID.Id, mftRecBuf); // update cache

        return mftRecBuf;
    }

    return *result; // return mft record from cache
}

void FillAttrValues(MFT_FILE_RECORD* mftRec, PMFT_ATTR_HEADER* attrValues)
{
    PMFT_ATTR_HEADER currAttr = (MFT_ATTR_HEADER*)Add2Ptr(mftRec, mftRec->FirstAttrOffset);
    ZeroMemory(attrValues, ATTR_TYPE_CNT * sizeof(PMFT_ATTR_HEADER));

    do
    {
        if(currAttr->NonResidentFlag == 0)
            assert(currAttr->res.DataSize + currAttr->res.DataOffset <= currAttr->AttrSize);

        // all atributes except ATTR_FILENAME and ATTR_LOGGED_UTILITY_STREAM should be in a single copy
        if ((currAttr->AttrType != ATTR_FILENAME) && (currAttr->AttrType != ATTR_LOGGED_UTILITY_STREAM))
            assert(attrValues[MakeAttrTypeIndex(currAttr->AttrType)] == nullptr);

        attrValues[MakeAttrTypeIndex(currAttr->AttrType)] = currAttr;

        assert(currAttr->AttrSize > 0);
        currAttr = (MFT_ATTR_HEADER*)Add2Ptr(currAttr, currAttr->AttrSize);
        assert(mftRec->FileRecSize > Diff2Ptr(mftRec, currAttr));

    } while (*((uint32_t*)currAttr) != ATTR_END);
}

bool ParseNonresAttrList(const VOLUME_DATA& volData, MFT_ATTR_HEADER* attrAttrList, ATTR_TYPE attrType, PMFT_ATTR_HEADER* result)
{
    GET_LOGGER_FUNC;

    ZeroMemory(result, SAME_ATTR_CNT * sizeof(result[0]));

    assert(attrAttrList->NonResidentFlag == 1);

    TDataRuns dataRuns;
    if (!DataRunsDecode(attrAttrList, dataRuns)) // DataRunsDecode writes a message into log file in case of an error
    {
        return false;
    }

    uint8_t* dataBuf = nullptr;

    assert(dataRuns.Count() == 1); //assuming that one LCN is always enough for list of attributes

    DATA_RUN_ITEM& rli = dataRuns[0];
    logger.DebugFmt("[ParseNonresAttrList] Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

    assert(rli.len == 1);

    uint dataBufSize = rli.len * volData.BytesPerCluster;
    dataBuf = (uint8_t*)alloca(dataBufSize);

    if (!ReadClusters(volData, rli.lcn, rli.len, dataBuf)) // ReadClusters writes a message into log file in case of an error
    {
        return false;
    }

    ATTR_LIST_ENTRY* attrListItem = (ATTR_LIST_ENTRY*)dataBuf;
    uint8_t* dataBufEnd1 = dataBuf + dataBufSize;
    uint8_t* dataBufEnd2 = dataBuf + attrAttrList->nonres.RealSize;
    
    assert(attrListItem->AttrSize > 0);
    assert(attrListItem->AttrType > 0);
    assert(((uint32_t)(attrListItem->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero

    uint resIndex = 0;
    while (true) // loop by LCNs in one data run
    {
        if (attrListItem->AttrType == attrType)
        {
            MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)LoadMFTRecordCache(volData, attrListItem->ref);
            assert(mftRec != nullptr);
            if (mftRec)
            {
                PMFT_ATTR_HEADER attrValues2[ATTR_TYPE_CNT];
                FillAttrValues(mftRec, attrValues2);
                PMFT_ATTR_HEADER currAttr2 = attrValues2[MakeAttrTypeIndex(attrType)];
                assert(currAttr2);

                //TODO optimization - for attributes other them ATT_ALLOC return immediately when first value found
                if (currAttr2)
                    result[resIndex++] = currAttr2;
                else
                    logger.WarnFmt("[ParseNonresAttrList] Attr: {} cannot be found is nonres ATT_LIST.", AttrName(attrType));

                assert(resIndex < SAME_ATTR_CNT);
            }
            else
            {
                logger.Error("[ParseNonresAttrList] LoadMFTRecordCache returned nullptr.");
                //return;
            }
        }

         
        attrListItem = (ATTR_LIST_ENTRY*)Add2Ptr(attrListItem, attrListItem->AttrSize);
        if (((uint8_t*)attrListItem >= dataBufEnd1)) break; 
        if (((uint8_t*)attrListItem >= dataBufEnd2)) break; 
        assert(attrListItem->AttrType > 0);
        assert(attrListItem->AttrSize > 0);
        assert(attrListItem->StartVCN == 0);
        assert(((uint32_t)(attrListItem->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
        
    }

    if(resIndex == 0)
        logger.ErrorFmt("[ParseNonresAttrList] Attr: {} not found in the nonResident ATTR_LIST in LCN {}.", AttrName(attrListItem->AttrType), rli.lcn);
    
    return true;
}

void GetAttr(const VOLUME_DATA& volData, ATTR_TYPE attrType, const PMFT_ATTR_HEADER* const attrValues, PMFT_ATTR_HEADER* result)
{
    ZeroMemory(result, SAME_ATTR_CNT*sizeof(result[0]));

    PMFT_ATTR_HEADER currAttr = attrValues[MakeAttrTypeIndex(attrType)];
    if (currAttr)
    {
        result[0] = currAttr; // if attr is found return it
        return;
    }

    /* otherwise look it in ATT_LIST attribute */

    currAttr = attrValues[MakeAttrTypeIndex(ATTR_LIST_ATTR)];
    if (!currAttr)
    {
        result[0] = nullptr;
        return;
    }

    GET_LOGGER;
    logger.Debug("ATTR_LIST_ATTR START");
    
    if (currAttr->NonResidentFlag == 1)
    {
        if (!ParseNonresAttrList(volData, currAttr, attrType, result))
        {
            logger.Error("ParseNonresAttrList returned error.");
            return;
        }

        if (result[0] != nullptr) // if result is empty then it goes to "ATTR_LIST_ATTR FINISHED NULL"
        {
            logger.Debug("ATTR_LIST_ATTR FINISHED");
            return;
        }
    }
    else
    {
        assert(currAttr->NonResidentFlag == 0);

        //TODO this code is very similar to code in ParseNonresAttrList. Think of extracting into separate method.
        ATTR_LIST_ENTRY* attrListItem = (ATTR_LIST_ENTRY*)Add2Ptr(currAttr, currAttr->res.DataOffset);
        uint8_t* currAttrEnd = (uint8_t*)currAttr + currAttr->AttrSize;

        assert(attrListItem->AttrSize > 0);
        assert(attrListItem->AttrType > 0);
        assert(attrListItem->StartVCN == 0);
        assert(((uint32_t)(attrListItem->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero

        uint resIndex = 0;
        while (true)
        {
            // we come here only when requested atribute is located in another MFT rec
            if (attrListItem->AttrType == attrType)
            {
                uint8_t* mftRecBuf = LoadMFTRecordCache(volData, attrListItem->ref);
                auto mftRec = (MFT_FILE_RECORD*)mftRecBuf;
                assert(mftRecBuf != nullptr); // mftRecBuf=null indicates error
                if (mftRecBuf)
                {
                    PMFT_ATTR_HEADER attrValues2[ATTR_TYPE_CNT];
                    FillAttrValues(mftRec, attrValues2);
                    PMFT_ATTR_HEADER currAttr2 = attrValues2[MakeAttrTypeIndex(attrType)];
                    assert(currAttr2);

                    //TODO optimization - for attributes other them ATT_ALLOC return immediately when first value found
                    if (currAttr2)
                        result[resIndex++] = currAttr2; 
                    else
                        logger.WarnFmt("Attribute {} cannot be found is ATT_LIST", AttrName(attrType));
                    
                    assert(resIndex < SAME_ATTR_CNT);

                    logger.Debug("ATTR_LIST_ATTR FINISHED");
                }
                else
                {
                    // error loading MFT record
                    // do not break loop and trying to load more records
                    logger.Warn("LoadMFTRecordCache returned null MFT record!");
                }
            }

            
            attrListItem = (ATTR_LIST_ENTRY*)Add2Ptr(attrListItem, attrListItem->AttrSize);
            if ((uint8_t*)attrListItem >= currAttrEnd) break; 
            assert(attrListItem->AttrSize > 0);
            assert(attrListItem->StartVCN == 0); 
            assert(attrListItem->AttrType > 0);
            assert(((uint32_t)(attrListItem->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
        }

        if(resIndex == 0)
            assert((attrType == ATTR_BITMAP) || (attrType == ATTR_ALLOC)); // only these two types can be missing

    }

    logger.Debug("ATTR_LIST_ATTR FINISHED NULL");

    // some attrs may not present in MFT rec. e.g. BITMAP is not created for empty directories while INDEX_ROOT exists
};


bool ParseNonresBitmap(const VOLUME_DATA& volData, MFT_ATTR_HEADER* attr, TBitField& bitmap)
{
    assert(attr->NonResidentFlag == 1);

    TDataRuns dataRuns;
    if (!DataRunsDecode(attr, dataRuns)) // DataRunsDecode writes a message into log file in case of an error
        return false;

    assert(dataRuns.Count() > 0);

    uint8_t* dataBuf = nullptr;
    uint32_t dataBufLen = 0; // memory size in clusters, how many clusters is allocated in dataBuf
    uint32_t currRun = 0;
    THArrayRaw bmpRecs(volData.BytesPerCluster);

    if (dataRuns.Count() > 1)
    {
        GET_LOGGER;
        logger.WarnFmt("[ParseNonresBitmap] Bitmap occupies more than one data run. Bitmap Data Runs Count: {}", dataRuns.Count());
    }

    while (currRun < dataRuns.Count())
    {
        DATA_RUN_ITEM& rli = dataRuns[currRun];

        if (rli.len > dataBufLen)
        {
            delete[] dataBuf;
            dataBuf = DBG_NEW uint8_t[rli.len * volData.BytesPerCluster];
            dataBufLen = rli.len;
        }

        if (!ReadClusters(volData, rli.lcn, rli.len, dataBuf)) // ReadCluster writes error meesage to log file in case of an error
        {
            delete[] dataBuf;
            return false;
        }

        // in most cases nonresident Bitmap will occupy one data run
        // therefore we have special handling of this case
        if (dataRuns.Count() > 1)
            bmpRecs.AddMany(dataBuf, rli.len); //TODO think to avoid several copying dataBuf. first into bmpRecs, then into bitmap

        currRun++;
    }

    if (dataRuns.Count() > 1)
    {
        uint32_t bmpLenBytes = bmpRecs.Count() * bmpRecs.GetItemSize();
        assert((bmpLenBytes & 0x07) == 0); // bitmap data size always multiple of 8
        bitmap.SetData((uint64_t*)bmpRecs.Memory(), bmpLenBytes >> 3);
    }
    else
    {
        uint32_t bmpLenBytes = dataBufLen * volData.BytesPerCluster; //TODO shall we use nonres.RealSize here?
        assert((bmpLenBytes & 0x07) == 0); // bitmap data size always multiple of 8
        bitmap.SetData((uint64_t*)dataBuf, bmpLenBytes >> 3);
    }

    delete[] dataBuf;
    return true;
}

bool ParseBitmap(const VOLUME_DATA& volData, MFT_ATTR_HEADER* attr, TBitField& bitmap)
{
    if (!attr) return true; // attr==nullptr means that no LCNs need to be parsed, usually Bitmap is null for empty directories

    assert(bitmap.Count() == 0);

    if (attr->NonResidentFlag == 0)
    {
        assert((attr->res.DataSize & 0x07) == 0); // bitmap data size always multiple of 8

        ATTR_BITMAP_ATTR* bmp = (ATTR_BITMAP_ATTR*)Add2Ptr(attr, attr->res.DataOffset);
        bitmap.SetData((uint64_t*)bmp->bitmap, attr->res.DataSize >> 3);
        return true;
    }
    else
    {
        return ParseNonresBitmap(volData, attr, bitmap);
    }
}

bool ParseAlloc(MFT_ATTR_HEADER* attr, TDataRuns& dataRuns)
{
    // sometimes one ATTR_LIST list may contain two ATTR_ALLOC attributes for some reason
    // it means we come here two times during parsing one MFT record with such ATTR_LIST 
    //assert(dataRuns.Count() == 0);

    if (!attr) return true; // attr==NULL means that ALLOC attribute is not present in MFT rec. Usually ALLOC is not needed in MFT record for empty directories

    assert(attr->NonResidentFlag == 1);

    return DataRunsDecode(attr, dataRuns); // DataRunDecode writes message to log in case of an error
}

void ParseIndexRoot(MFT_ATTR_HEADER* attr, TLCNRecs& lcns, TFileList& fileList)
{
    GET_LOGGER;

    assert(fileList.Count() == 0);
    assert(attr->NonResidentFlag == 0); // always resident

    ATTR_INDEX_ROOT* indexR = (ATTR_INDEX_ROOT*)Add2Ptr(attr, attr->res.DataOffset);
    auto pihdr = &(indexR->ihdr);

    logger.DebugFmt("IndexRoot attr type: {:#x} ({})", (uint32_t)indexR->AttrType, AttrName(indexR->AttrType));
    logger.DebugFmt("Collation rule: {}", (uint32_t)indexR->Rule);
    logger.DebugFmt("Dir type: {} {}", indexR->ihdr.Flags, (indexR->ihdr.Flags == 0 ? " (SMALL DIR)" : " (BIG DIR)"));
    
    GetFileListFromNode(pihdr, lcns, fileList);
}

// mftRec record must be a directory type
// loads files from MFT rec in sorted order starting from Index Root
int32_t GetFileListFromMFTRec(const VOLUME_DATA& volData, MFT_FILE_RECORD* mftRec, DIR_NODE& node)
{
    GET_LOGGER;

    assert(mftRec->Flags == 0x03); // only "directory" record should go here
    if (mftRec->Flags != 0x03)
    {
        logger.Error("[GetFileListFromMFTRec] Error: mftRec->Flags != 0x03 !");
        return -1; // error
    }

    assert(node.Bitmap.Count() == 0);

    PMFT_ATTR_HEADER attrValues[ATTR_TYPE_CNT];
    FillAttrValues(mftRec, attrValues);

    PMFT_ATTR_HEADER multValues[SAME_ATTR_CNT];

    GetAttr(volData, ATTR_BITMAP, attrValues, multValues);
    auto bitmap = multValues[0]; // bitmap can be NULL here
    assert(multValues[1] == nullptr); // only single value can be returned for bitmap
    if (multValues[1] != nullptr)
    {
        logger.Warn("[GetFileListFromMFTRec] Warning: looks like two ATTR_BITMAP attributes in one MFT record!");
        // no need to return an error, let work with first bitmap
    }

    // bitmap can be null, its ok
    if (bitmap)
    {
        if (!ParseBitmap(volData, bitmap, node.Bitmap)) // copy bitmap into TBitField class for easier access
        {
            logger.Error("[GetFileListFromMFTRec] ParseBitmap finished with error.");
        }
    }

    GetAttr(volData, ATTR_ALLOC, attrValues, multValues); // multiple values can be returned
    uint i = 0;
    while (multValues[i] != nullptr)
    {
        if (!ParseAlloc(multValues[i++], node.DataRuns)) // decode data runs and store them in node.DataRuns
        {
            logger.Error("[GetFileListFromMFTRec] ParseAlloc finished with error.");
        }
    }

    uint32_t lcnTotalCount = 0;
    for (auto& run : node.DataRuns) lcnTotalCount += run.len;

    TLCNRecs lcns(volData.BytesPerCluster, lcnTotalCount);
    if (!lcns.LoadDataRuns(volData, node))
    {
        logger.Error("[GetFileListFromMFTRec] TLCNRecs.LoadDataRuns finished with error.");
        return -1; // fail to load data runs this is critical error, return immediately with error
    }

    GetAttr(volData, ATTR_ROOT, attrValues, multValues);
    auto root = multValues[0];
    assert(multValues[1] == nullptr); // only single value can be returned for ATT_ROOT
    if (multValues[1] != nullptr)
    {
        logger.Warn("[GetFileListFromMFTRec] Warning: looks like two ATTR_ROOT attributes in one MFT record!");
        //no need to return, let work with first root attribute
    }

    ParseIndexRoot(root, lcns, node.FileList);

    return node.FileList.Count();
}

// reads list of files in already sorted order
bool ReadDirectoryV2(VOLUME_DATA& volData, MFT_REF parentMftRecID, uint32_t dirLevel, THArray<FILE_NAME>& gDirList)
{
    if (dirLevel > 30) throw std::runtime_error("dirLevel > 30 !!!!!!!");

    GET_LOGGER;

    uint8_t* mftRecBuf = (uint8_t*)alloca(volData.BytesPerMFTRec);
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;

    if (!LoadMFTRecord(volData, parentMftRecID, mftRecBuf))
    {
        logger.Error("LoadMFTRecord finished with error.");
        return false;
    }

    DIR_NODE node;
    int32_t cnt = GetFileListFromMFTRec(volData, mftRec, node);// writes error to log file in case of error
    if (cnt == -1) return false;

    for (auto& item : node.FileList)
    {
        if (!IsMetaFile(item) && !IsDotDir(item.ciName)) // do not add hidden metafiles into file list
        {
            if (dirLevel == 0) std::wcout << item.ciName.c_str() << std::endl;
            //if (dirLevel == 1) std::wcout << "      " << item.ciName.c_str() << std::endl;

            if (IsDir(item.Attr))
                gDirList.AddValue(item);
            else
                gDirList.AddValue(item);
        }

        //if (IsReparse(item.Attr)) { logger.InfoFmt("REPARSE detected: {}", wtos(item.Name)); continue; }

        // item.Attr.ParentDir actually is ref to item's MFT Id, not to the parent dir
        if (IsDir(item.Attr) && !IsMetaFile(item) && !IsDotDir(item.ciName)) // bypass hidden mft metafiles
            if (!ReadDirectoryV2(volData, item.MFTRef, dirLevel + 1, gDirList))
            {
                logger.ErrorFmt("ReadDirectory2 finished with error for MFT rec: {}", item.MFTRef.sId.low);
            }
    }

    return true;
}
