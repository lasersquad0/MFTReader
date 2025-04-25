
#include <iostream>
#include <string>
#include <cassert>
//#include <malloc.h>

#include "string_utils.h"
#include "LogEngine.h"
#include "Functions.h"

LogEngine::Logger& GetLoggerFunc()
{
    LogEngine::Logger& logger = LogEngine::GetFileLogger(MFT_LOGGER_NAME_FUNC, "LogMFTReaderFUNC.log");
    logger.SetAsyncMode(true);
    logger.SetLogLevel(LogEngine::Levels::llDebug);
    logger.GetSink(MFT_LOGGER_NAME_FUNC)->SetLogLevel(LogEngine::Levels::llDebug);
    return logger;
}

void GetFileListFromNode(INDEX_HDR* ihdr, TLCNRecs& lcns, THArray<FILE_NAME>& fnames)
{
    LogEngine::Logger& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);

    uint32_t off = ihdr->DEOffset; // offset of 1st dir entry

    while (true) // iterate though all DE+FILE_NAME entries
    {
        assert(off < ihdr->Used);

        NTFS_DE* de = (NTFS_DE*)Add2Ptr(ihdr, off); // NTFS_DE it is a "header" above File Name attribute, covers each file name attribute item

        // logger.DebugFmt("DE File rec {:#x}", de->ref.Id);
        // logger.DebugFmt("DE size: {}", de->size);
        // logger.DebugFmt("DE key_size: {}", de->key_size);

        assert(de->size >= de->key_size + sizeof(NTFS_DE));

        if (de->flags & NTFS_IE_HAS_SUBNODES)
        {
            CLST vcn = *(CLST*)Add2Ptr(ihdr, off + de->size - sizeof(uint64_t));
            logger.DebugFmt("DE has subnodes in VCN={}", vcn);

            INDEX_BUFFER* allocIndex = (INDEX_BUFFER*)lcns.GetRecByVCN(vcn);
            
            // process items only if cluster starts from correct signature INDX
            // sometimes fully empty (filled with zero) clusters present in run list without starting INDX signature
            if (ntfs_is_indx_recp(allocIndex->RecHeader.Signature))
            {
                //logger.DebugFmt("Alloc Attr cluster's VCN: {} LCN: {}", allocIndex->vcn, clst);
                
                assert(vcn == allocIndex->vcn);

                auto pihdr = &(allocIndex->ihdr);
                GetFileListFromNode(pihdr, lcns, fnames);

            }
            else
            {
                uint8_t* sign = allocIndex->RecHeader.Signature;
                logger.WarnFmt("Signature 'INDX' has not been found in LCN cluster {}. Signature found: {}{}{}{}", lcns.GetLCNByVCN(vcn), sign[0], sign[1], sign[2], sign[3]);
            }
        }

        if (de->key_size > 0) // key_size>0 means that File Name attr exists
        {
            ATTR_FILE_NAME* fattr = (ATTR_FILE_NAME*)Add2Ptr(de, sizeof(NTFS_DE));

            assert(de->key_size = sizeof(ATTR_FILE_NAME) + fattr->FileNameLen);

            if (fattr->NameType != FILE_NAME_DOS) // bypass DOS filenames
            {
                ci_string ciwnm(GetFName(fattr, sizeof(ATTR_FILE_NAME)), fattr->FileNameLen);//TODO select one string here
                std::wstring wnm(GetFName(fattr, sizeof(ATTR_FILE_NAME)), fattr->FileNameLen);
                std::string nm = wtos(wnm);

                logger.DebugFmt("DE ATTR Parent rec: {:#x}", fattr->ParentDir.Id);
                logger.DebugFmt("DE ATTR name: '{}'", nm);

                fnames.AddValue({ ciwnm, *fattr, de->ref });
            }
        }

        off += de->size; // moving to the next DE

        // check if this is last DE or we have exceeded pihdr->used
        if (((de->flags & NTFS_IE_LAST) > 0) || (de->size < sizeof(NTFS_DE)) || (off > ihdr->Used)) // off refers to next DE here
        {
            break;
        }
    }
}

// attr parameter cannot be NULL here. It should be valid pointer to MFT_ATTR_HEADER structure
bool DataRunsDecode(MFT_ATTR_HEADER* attr, THArray<DATA_RUN_ITEM>& runs)
{
    LogEngine::Logger& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);

    assert(runs.Count() == 0); // make sure that old data runs are cleared

    if (!attr || !attr->NonResidentFlag) 
    {
        // this in incorrect data in MFT
        logger.Error("[DataRunsDecode] Attr (!currAttr || !currAttr->NonResidentFlag) = TRUE");
        return false;
    }

    uint8_t* datarun = Add2Ptr(attr, attr->nonres.DataRunsOffset);
    uint8_t* attrEnd = Add2Ptr(attr, attr->AttrSize);

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

        logger.DebugFmt("DataRuns VCN: {}", ri.vcn);
        logger.DebugFmt("DataRuns LCN: {}", ri.lcn);
        logger.DebugFmt("DataRuns Length: {}", ri.len);

    }

    logger.DebugFmt("DataRuns total count: {}", runs.Count());

    return true;
}

// reads series of sequential volume clusters starting from #lcnStart
// dataBuf should be large enough to fit lcnCnt clusters of data
bool ReadClusters(const VOLUME_DATA& volData, uint64_t lcnStart, uint32_t lcnCnt, PBYTE dataBuf)
{
    LARGE_INTEGER offset;
    DWORD bytesRead;

    offset.QuadPart = lcnStart * volData.BytesPerCluster;

    SetFilePointerEx(volData.hVolume, offset, NULL, FILE_BEGIN);

    // read clusterCnt clusters
    BOOL res = ReadFile(volData.hVolume, dataBuf, lcnCnt * volData.BytesPerCluster, &bytesRead, nullptr);
    if (res)
    {
        assert(lcnCnt * volData.BytesPerCluster == bytesRead);
        return true;
    }
    else
    {
        LogEngine::Logger& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);
        logger.ErrorFmt("ReadCluster.ReadFile has failed with error: {}", GetLastError());
        return false;
    }
}

bool FixupUSA(const VOLUME_DATA& volData, PBYTE dataBuf, DATA_RUN_ITEM& rli)
{
    NTFS_RECORD_HEADER* indexRec = (NTFS_RECORD_HEADER*)dataBuf;
    uint32_t wordsPerSector = volData.BytesPerSector >> 1;

    for (size_t i = 0; i < rli.len; i++)
    {
        if (!ntfs_is_indx_recp(indexRec->Signature)) // bypass non 'INDX' clusters (usually filled by zero)
        {
            //LogEngine::Logger& logger = LogEngine::GetLogger(MFT_LOGGER_NAME);
            //uint8_t* sign = indexRec->Signature;
            //logger.WarnFmt("[FixupUSA] Signature 'INDX' has not been found in LCN cluster {}. Signature found: {}{}{}{}", rli.lcn + i, sign[0], sign[1], sign[2], sign[3]);
            
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
    
    //TODO speed improvement - allocate memory in stack here
    auto pnfrob = (PNTFS_FILE_RECORD_OUTPUT_BUFFER)alloca(cb);

    if (!DeviceIoControl(volData.hVolume, FSCTL_GET_NTFS_FILE_RECORD, &nfrib, sizeof(nfrib), pnfrob, cb, 0, nullptr))
    {
        logger.ErrorFmt("DeviceIoControl failed with error: {}", GetLastError());
        //free(pnfrob);
        return false;
    }

    uint8_t* pFileRec = pnfrob->FileRecordBuffer;
    //TODO think how to avoid this memcpy_s
    memcpy_s(mftRec, volData.BytesPerMFTRec, pFileRec, volData.BytesPerMFTRec);
    //free(pnfrob);

    return true;
}

// mftRec should be a buffer with volData.BytesPerMFTRec size
uint8_t* LoadMFTRecordCache(const VOLUME_DATA& volData, MFT_REF recID)
{
    TMFTRecCache* cache = Singleton<TMFTRecCache>::getInstance();

    uint8_t** result = cache->GetValuePointer(recID.Id);
    if (result == nullptr) // no value in cache, load mft record from disk
    {
        uint8_t* mftRecBuf = DBG_NEW uint8_t[volData.BytesPerMFTRec];
        if (!LoadMFTRecord(volData, recID, mftRecBuf))
            return nullptr; // error loading mft record

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

        attrValues[MakeAttrTypeIndex(currAttr->AttrType)] = currAttr;

        currAttr = (MFT_ATTR_HEADER*)Add2Ptr(currAttr, currAttr->AttrSize);
        assert(mftRec->FileRecSize > Diff2Ptr(mftRec, currAttr));

    } while (*((uint32_t*)currAttr) != ATTR_END);
}

MFT_ATTR_HEADER* ParseNonresAttrList(const VOLUME_DATA& volData, MFT_ATTR_HEADER* attrList, ATTR_TYPE attrType)
{
    GET_LOGGER_FUNC;

    assert(attrList->NonResidentFlag == 1);

    TDataRuns dataRuns;
    if (!DataRunsDecode(attrList, dataRuns)) // DataRunsDecode writes a message into log file in case of an error
        return nullptr;

    uint8_t* dataBuf = nullptr;

    assert(dataRuns.Count() == 1); //assuming that one LCN is always enough for list of attributes

    DATA_RUN_ITEM& rli = dataRuns[0];
    logger.DebugFmt("[ParseNonresAttrList] Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

    assert(rli.len == 1);

    dataBuf = (uint8_t*)alloca(rli.len * volData.BytesPerCluster);

    if (!ReadClusters(volData, rli.lcn, rli.len, dataBuf)) // ReadClusters writes a message into log file in case of an error
    {
        //delete[] dataBuf;
        return nullptr;
    }

    /*if (!FixupUSA(volData, dataBuf, rli))
    {
        logger.Error("FixupUSA returned error.");
        break;
    }*/

    ATTR_LIST_ENTRY* attrListItem = (ATTR_LIST_ENTRY*)dataBuf;
    uint8_t* dataBufEnd = dataBuf + /*rli.len * */volData.BytesPerCluster;

    while (true) // loop by LCNs in one data run
    {
        if (attrListItem->AttrType == attrType)
        {
            uint8_t* mftRecBuf = LoadMFTRecordCache(volData, attrListItem->ref);
            auto mftRec = (MFT_FILE_RECORD*)mftRecBuf;
            assert(mftRecBuf != nullptr);
            if (mftRecBuf)
            {
                PMFT_ATTR_HEADER attrValues2[ATTR_TYPE_CNT];
                FillAttrValues(mftRec, attrValues2);
                auto attr = GetAttr(volData, attrType, attrValues2);
                assert(attr);
                return attr;
            }
            else
            {
                logger.Error("[ParseNonresAttrList] LoadMFTRecordCache returned nullptr.");
                //delete[] dataBuf;
                return nullptr;
            }
        }

        attrListItem = (ATTR_LIST_ENTRY*)Add2Ptr(attrListItem, attrListItem->AttrSize);
        if ((uint8_t*)attrListItem >= dataBufEnd) break;
    }

    //delete[] dataBuf;
    logger.ErrorFmt("[ParseNonresAttrList] Attr: {} not found in the nonResident ATTR_LIST in LCN {}.", wtos(AttrTypeNames[MakeAttrTypeIndex(attrListItem->AttrType)]), rli.lcn);
    return nullptr;
}

MFT_ATTR_HEADER* GetAttr(const VOLUME_DATA& volData, ATTR_TYPE attrType, const PMFT_ATTR_HEADER* const attrValues)
{
    PMFT_ATTR_HEADER currAttr = attrValues[MakeAttrTypeIndex(attrType)];
    if (currAttr) return currAttr; // if attr is found return it

    // otherwise look it in ATT_LIST attribute

    currAttr = attrValues[MakeAttrTypeIndex(ATTR_LIST_ATTR)];
    if (!currAttr) return nullptr; 

    GET_LOGGER;
    logger.Debug("ATTR_LIST_ATTR START");
    
    if (currAttr->NonResidentFlag == 1)
    {
        MFT_ATTR_HEADER* res = ParseNonresAttrList(volData, currAttr, attrType);
        if (res)
        {
            logger.Debug("ATTR_LIST_ATTR FINISHED");
            return res;
        }
    }
    else
    {
        assert(currAttr->NonResidentFlag == 0);

        ATTR_LIST_ENTRY* attrListItem = (ATTR_LIST_ENTRY*)Add2Ptr(currAttr, currAttr->res.DataOffset);
        uint8_t* currAttrEnd = (uint8_t*)currAttr + currAttr->AttrSize;

        /*if (attrListItem->StartVCN != 0)
        {
            std::string aname2 = wtos(AttrTypeNames[MakeAttrTypeIndex(attrListItem->AttrType)]);
            std::string aname1 = wtos(AttrTypeNames[MakeAttrTypeIndex(attrType)]);
            logger.WarnFmt("[GetAttr] ATTR LIST attrListItem->StartVCN != 0! StartVCN: {:#x}, Initial AttrType: {}, AttrType: {}, AttrRef: {}, AttrSize: {}",
                             attrListItem->StartVCN, aname1, aname2, attrListItem->ref.Id, attrListItem->AttrSize);
        }*/

        assert(attrListItem->StartVCN == 0);

        while (true)
        {
            // we come here only when requested atribute is located in another MFT rec
            if (attrListItem->AttrType == attrType)
            {
                uint8_t* mftRecBuf = LoadMFTRecordCache(volData, attrListItem->ref);
                auto mftRec = (MFT_FILE_RECORD*)mftRecBuf;
                assert(mftRecBuf != nullptr);
                if (mftRecBuf)
                {
                    PMFT_ATTR_HEADER attrValues2[ATTR_TYPE_CNT];
                    FillAttrValues(mftRec, attrValues2);
                    auto attr = GetAttr(volData, attrType, attrValues2);
                    assert(attr);
                    logger.Debug("ATTR_LIST_ATTR FINISHED");
                    return attr;
                }
                else
                {
                    // TODO add error handling here
                }
            }

            attrListItem = (ATTR_LIST_ENTRY*)Add2Ptr(attrListItem, attrListItem->AttrSize);
            if ((uint8_t*)attrListItem >= currAttrEnd) break;
        }

        assert((attrType == ATTR_BITMAP) || (attrType == ATTR_ALLOC)); // only these two types can be missing
    }

    logger.Debug("ATTR_LIST_ATTR FINISHED NULL");

    return nullptr; // some attrs may not present in MFT rec. e.g. BITMAP is not created for empty directories while INDEX_ROOT exists
};



