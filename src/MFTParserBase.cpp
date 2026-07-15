
#include "Debug.h"
#include "NTFS.h"
#include "Functions.h"
#include "Caches.h"
#include "Readers.h"

/**
* @brief "Template" function reading list of LCNs from Data Run
* @details Reads all LCNs from all DataRuns present in node.DataRun. For each LCN it calls predicate pred for processing each LCN.
* Predicate can either extract list of files from LCN or add the LCN to cache of LCN or anything else.
* When used to extract list of files from LCN files are extracted in random order (in order of LCNs in Data Runs) and does not go to sub-nodes.
* node.Bitmap is used to select which LCNs in Data Runs are valid. Predicate is called only for valid LCNs.
* @param node Contains Data Runs to be processed, and Bitmap that tells us what LCNs are valid.
* @param pred Predicate used for processing each LCN.
*/
bool TMFTParserBase::ProcessDataRuns(DIR_NODE& node, ProcessLCNsPred pred)
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
    bool result = true;

    while (currRun < node.DataRuns.Count())
    {
        if (bitsCounter > lastBit) // no more valid LCNs, break loop 
            break;

        DATA_RUN_ITEM& rli = node.DataRuns[currRun];
        logger.DebugFmt("[ProcessAllocDataRuns] Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

        // check correctness of decoded LCNs
        assert(rli.len < (uint64_t)getVolData().TotalClusters.QuadPart);
        assert(rli.lcn < (uint64_t)getVolData().TotalClusters.QuadPart);
       // assert((rli.lcn < (uint64_t)getVolData().MftZoneStart.QuadPart) || (rli.lcn > (uint64_t)getVolData().MftZoneEnd.QuadPart));

        //TODO MFT may be fragmented, it might be good idea to read list of MFT fragments in advance and check that rli.lcn does not inside any MFT fragment
        //assert((rli.lcn < (uint64_t)volData.MftStartLcn.QuadPart) || (rli.lcn > (uint64_t)(volData.MftStartLcn.QuadPart + volData.MftValidDataLength.QuadPart / volData.BytesPerCluster)));

        CLST rlilen = valuemin((CLST)(lastBit + 1 - bitsCounter), rli.len);

        if (rlilen > dataBufLen)
        {
            delete[] dataBuf;
            dataBuf = DBG_NEW uint8_t[rlilen * getVolData().BytesPerCluster];
            dataBufLen = rlilen;
        }

        assert(dataBuf);

        if (!ReadClusters(rli.lcn, rlilen, dataBuf)) // ReadClusters wrties error message to log file in case of an error
        {
            result = false;
            break;
        }

        if (!FixupUSA(dataBuf, rli, rlilen))  // FixupUSA wrties error message to log file in case of an error
        {
            //logger.Error("FixupUSA returned error.");
            result = false;
            break;
        }

        for (size_t i = 0; i < rlilen; i++)
        {
            if (node.Bitmap.Test(bitsCounter++)) // add only LCNs which are marked in bitmap bitfield
            {
                //process particular LCN, either add to list of LCNs in cache or get list of files from this record, depending on predicate
                pred(dataBuf + i * getVolData().BytesPerCluster, rli.vcn + i, rli.lcn + i);
            }
            else
            {
                logger.DebugFmt("Bitmap bit {}th is zero. LastBit: {}. Bypassing LCN cluster {}.", bitsCounter, lastBit, rli.lcn + i);
                //logger.InfoFmt("[ProcessAllocDataRuns] Bypassing this LCN because of Bitmap: VCN: {}, LCN : {}", rli.vcn + i, rli.lcn + i);
                if (bitsCounter > lastBit) // no more valid LCNs 
                    break;
            }
        }

        currRun++;
    }

    delete[] dataBuf;

    logger.Debug("---------- END OF PROCESSING Alloc Attr Data Runs ---------");

    return result;
}


//TODO make it work with resident attributes too
bool TMFTParserBase::DecodeDataRuns(MFT_ATTR_HEADER* attr, TDataRuns& runs)
{
    assert(attr->AttrType == ATTR_ALLOC || attr->AttrType == ATTR_BITMAP || attr->AttrType == ATTR_LIST_ATTR || attr->AttrType == ATTR_DATA);

    GET_LOGGER;

    // data runs exist in non-resident attributes only (ALLOC and DATA)
    if (!attr || !attr->NonResidentFlag)
    {
        // looks like incorrect data in MFT
        logger.Error("[DataRunsDecode] Attr parameter is NULL or is resident (must be non-resident)!!!");
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
        DATA_RUN_ITEM ri{ 0 };
        int64_t deltaxcn; // can be negative, it's ok

        ri.vcn = currVCN;
        ri.lcn = currLCN;

        uint8_t lens = *datarun;
        uint8_t b = lens & 0x0F; // minor half byte is length (in bytes) of the following int value "number of clusters in current data run"
        if (b)
        {
            assert(b <= 8);
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

        currLCN += deltaxcn;
        if (deltaxcn == 0) ri.lcn = 0; // for sparse files data run contains "virtual" LCN virtualy filled by zero
        else ri.lcn = currLCN;

        runs.AddValue(ri);

        datarun += b3 + 1; // move to the next data run

        logger.DebugFmt("[DataRunsDecode] Data Run. VCN: {}, LCN: {}, Len: {}", ri.vcn, ri.lcn, ri.len);
    }

    logger.DebugFmt("[DataRunsDecode] Data Runs Count: {}, Last VCN: {}", runs.Count(), currVCN);

    return true;
}


/**
* @brief Reads series of sequential clusters starting from cluster with number lcnStart
* @details DataBuf should be large enough to fit lcnCnt clusters of data
* @param lcnStart number (id) of first cluster to be read
* @param lcnCnt Count of sequential clusters to be read
* @param dataBuf Buffer where all clusters will be read. Should be at least size lcnCnt*VolumeClusterSize
**/
bool TMFTParserBase::ReadClusters(CLST lcnStart, CLST lcnCnt, uint8_t* dataBuf)
{
    LARGE_INTEGER offset{ 0 };
    DWORD bytesRead;

    offset.QuadPart = lcnStart * getVolData().BytesPerCluster;

    BOOL res = SetFilePointerEx(getVolData().hVolume, offset, nullptr, FILE_BEGIN);
    if (!res)
    {
        GET_LOGGER;
        logger.ErrorFmt("ReadCluster.SetFilePointerEx has failed with error: {}", GetLastError());
        return false;
    }

    // read clusterCnt clusters
    res = ReadFile(getVolData().hVolume, dataBuf, (DWORD)(lcnCnt * getVolData().BytesPerCluster), &bytesRead, nullptr);
    if (res)
    {
        assert(lcnCnt * getVolData().BytesPerCluster == bytesRead);
        return true;
    }
    else
    {
        GET_LOGGER;
        logger.ErrorFmt("ReadCluster.ReadFile has failed with error: {}", GetLastError());
        return false;
    }
}

// dataBuf contains data for rlilen clusters
bool TMFTParserBase::FixupUSA(uint8_t* dataBuf, DATA_RUN_ITEM& rli, uint64_t rlilen)
{
    NTFS_RECORD_HEADER* indexRec = (NTFS_RECORD_HEADER*)dataBuf;
    uint32_t wordsPerSector = getVolData().BytesPerSector >> 1;

    // loop by LCNs loaded into dataBuf
    for (uint64_t i = 0; i < rlilen; i++)
    {
        if (!ntfs_is_indx_recp(indexRec->Signature)) // bypass non 'INDX' clusters (usually filled by zero)
        {
            /* This is correct situation when list of LCNs in one data run has "holes" according to Bitmap attribute.
            *  We read all LCNs from current data run as a single operation. Some of these LCNs are "not used" and do not contain INDX signature
            *  Such LCNs have appropriate bit=0 in Bitmap attribute.
            */

            GET_LOGGER;
            uint8_t* sign = indexRec->Signature;
            logger.WarnFmt("[FixupUSA] Signature 'INDX' has not been found in LCN cluster {}. Signature found: {}{}{}{}", rli.lcn + i, sign[0], sign[1], sign[2], sign[3]);

            continue;
        }

        uint16_t sectorsCnt = indexRec->FixupCnt - 1;
        assert(sectorsCnt == getVolData().BytesPerCluster / getVolData().BytesPerSector);

        uint16_t* fixuparr = (uint16_t*)(Add2Ptr(indexRec, indexRec->FixupOffset));
        uint16_t checkValue = *fixuparr;
        fixuparr++; // now it refers to first array item

        uint16_t* sectorEnd = (uint16_t*)(indexRec)+wordsPerSector - 1;

        uint32_t s = 0;
        while (s < sectorsCnt)
        {
            assert(checkValue == *sectorEnd);
            if (checkValue != *sectorEnd)
            {
                GET_LOGGER;
                logger.Error("[FixupUSA] Error: looks like data is corrupted in the sector");
                return false; // looks like data is corrupted in this sector
            }

            *sectorEnd = fixuparr[s]; // restore data

            sectorEnd += wordsPerSector;
            s++;
        }

        indexRec = (NTFS_RECORD_HEADER*)Add2Ptr(indexRec, getVolData().BytesPerCluster);
    }

    return true;
}


/// calls predicate pred for all files got from ihdr
/// DOES NOT go to subnodes
void TMFTParserBase::GetFileList(INDEX_HDR* ihdr, AddFileAttrPred pred)
{
    GET_LOGGER;

    uint32_t off = ihdr->DEOffset; // offset of 1st dir entry

    while (true) // iterate though all DE+FILE_NAME entries
    {
        assert(off < ihdr->Used);

        NTFS_DE* de = (NTFS_DE*)Add2Ptr(ihdr, off); // NTFS_DE it is a "header" above File Name attribute, covers each file name attribute item

        logger.DebugFmt("DE Ref to MFT Rec: {}", de->ref.toHexString()); // reference to MFT Rec for this file name
        logger.DebugFmt("DE Flags: {} ({:#x})", de->flags == NTFS_IE_HAS_SUBNODES ? "HAS SUBNODES" : de->flags == NTFS_IE_LAST ? "LAST" : de->flags == 0 ? "OTHER" : "UNKNOWN", de->flags);
        logger.DebugFmt("DE Size: {}", de->size);
        logger.DebugFmt("DE Key_size: {} {}", de->key_size, de->key_size == 0 ? "(last DE usually empty, does not contain any FILE_ATTR attribute)" : "");

        assert(de->size >= de->key_size + sizeof(NTFS_DE));

        if (de->key_size > 0) // key_size>0 means that filenameattr exists
        {
            ATTR_FILE_NAME* fattr = (ATTR_FILE_NAME*)Add2Ptr(de, sizeof(NTFS_DE));

            assert(de->key_size == sizeof(ATTR_FILE_NAME) + fattr->FileNameLen * sizeof(wchar_t));
            assert(de->size >= sizeof(NTFS_DE) + sizeof(ATTR_FILE_NAME) + fattr->FileNameLen * sizeof(wchar_t));
            assert((fattr->dup.FileAttrib & FILE_ATTRIBUTE_NORMAL) == 0);// check that NORMAL bit is always zero

            if (fattr->NameType != FILE_NAME_DOS) // bypass DOS filenames
            {
                pred(fattr, de->ref);
            }

            std::wstring wnm(GetFName(fattr), fattr->FileNameLen);
            logger.DebugFmt("DE ATTR Parent Rec ID: {}", fattr->ParentDir.toHexString()); //TODO check that parent of each file refers to MFT Rec we are currently parsing
            logger.DebugFmt("DE ATTR File Name Type: '{}' ({:#x})", FileNameTypes[fattr->NameType], fattr->NameType);
            logger.DebugFmt("DE ATTR DOS Attrib: {:#x} {}", fattr->dup.FileAttrib, FormatFileAttributes(fattr->dup.FileAttrib));
            logger.DebugFmt("DE ATTR Name: '{}'", wtos(wnm));
            logger.DebugFmt("DE ATTR File Size: {}", fattr->dup.FileSize);

            /*logger.Debug(FileDateToString("DE ATTR Created: ", fattr->dup.CreateTime));
            logger.Debug(FileDateToString("DE ATTR Modified: ",  fattr->dup.ModifyTime));
            logger.Debug(FileDateToString("DE ATTR LastAccess: ",fattr->dup.LastAccessTime));
            */
        }

        off += de->size; // moving to the next DE

        // check if this is last DE or we have exceeded pihdr->used
        if (((de->flags & NTFS_IE_LAST) > 0) || (off >= ihdr->Used) || (de->size < sizeof(NTFS_DE))) // off refers to next DE here
        {
            break;
        }
    }
}

// reads list of files in SORTED order starting from Index Root referred by ihdr
// goes to subnodes and uses pre-loaded list of LCNs containing ALLOC attribute values
void TMFTParserBase::GetFileListFromNode(INDEX_HDR* ihdr, TLCNRecs& lcns, TFileList& fnames)
{
    GET_LOGGER;

    uint32_t off = ihdr->DEOffset; // offset of 1st dir entry

    while (true) // iterate though all DE+FILE_NAME entries
    {
        assert(off < ihdr->Used);

        NTFS_DE* de = (NTFS_DE*)Add2Ptr(ihdr, off); // NTFS_DE it is a "header" above File Name attribute, covers each file name attribute item

        logger.DebugFmt("Dir Entry Ref to MFT Rec: {0} ({0:#x})", de->ref.Id);
        logger.DebugFmt("Dir Entry Flags: {} ({})", de->flags == NTFS_IE_HAS_SUBNODES ? "HAS SUBNODES" : de->flags == NTFS_IE_LAST ? "LAST" : de->flags == 0 ? "OTHER" : "UNKNOWN", de->flags);
        logger.DebugFmt("Dir Entry Size: {}", de->size);
        logger.DebugFmt("Dir Entry Key_size: {}", de->key_size);

        assert(de->size >= de->key_size + sizeof(NTFS_DE));

        if (de->flags & NTFS_IE_HAS_SUBNODES)
        {
            CLST vcn = *(CLST*)Add2Ptr(ihdr, off + de->size - sizeof(uint64_t)); // last 8 bytes contain the VÑN of subnode. This field is present only if (flags & NTFS_IE_HAS_SUBNODES)

            auto rec = lcns.GetRecByVCN(vcn);
            logger.DebugFmt("Dir Entry has subnodes located in VCN={}, LCN={}", vcn, rec.first);

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
                logger.WarnFmt("Signature 'INDX' has not been found in LCN cluster {}. Signature found: {}{}{}{}", lcns.GetRecByVCN(vcn).first, sign[0], sign[1], sign[2], sign[3]);
            }
        }

        if (de->key_size > 0) // key_size>0 means that FileName attr exists
        {
            ATTR_FILE_NAME* fattr = (ATTR_FILE_NAME*)Add2Ptr(de, sizeof(NTFS_DE));

            assert(de->key_size = sizeof(ATTR_FILE_NAME) + fattr->FileNameLen);
            assert((fattr->dup.FileAttrib & FILE_ATTRIBUTE_NORMAL) == 0);// check that NORMAL bit is always zero

            std::wstring wnm(GetFName(fattr), fattr->FileNameLen);
            logger.DebugFmt("Dir Entry Parent Rec ID: {}", fattr->ParentDir.toHexString());
            logger.DebugFmt("Dir Entry File Name Type: '{}' ({:#x})", FileNameTypes[fattr->NameType], fattr->NameType);
            logger.DebugFmt("Dir Entry File/Dir name: '{}'", wtos(wnm));
            logger.DebugFmt("Dir Entry File DOS Attrib: {:#x} {}", fattr->dup.FileAttrib, FormatFileAttributes(fattr->dup.FileAttrib));
            logger.DebugFmt("Dir Entry File Size: {}", fattr->dup.FileSize);

            /*logger.Debug(FileDateToString("Dir Entry Created: ", fattr->dup.CreateTime));
            logger.Debug(FileDateToString("Dir Entry Modified: ",  fattr->dup.ModifyTime));
            logger.Debug(FileDateToString("Dir Entry LastAccess: ",fattr->dup.LastAccessTime));
            */
            if (fattr->NameType != FILE_NAME_DOS) // bypass DOS filenames
            {
                fnames.AddValue({ convert_string<ci_string::value_type>(wnm).c_str(), *fattr, de->ref });//TODO wtos(ciwnm).c_str() may be incorrect for unicode and non-unicode settings
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

// returns pointer to the first met non-DOS ATTR_FILENAME attribute in mftRec
// called for dir MFT recs only
// dir MFT rec can contain either one or two ATTR_FILENAME attributes, one of two is DOS attribute
//TODO do we need to process ATTR_LIST here? because ATTR_FILENAMEs can be located in other MFT records 
ATTR_FILE_NAME* TMFTParserBase::GetFirstFileNameAttr(MFT_FILE_RECORD* mftRec)
{
    PMFT_ATTR_HEADER currAttr = (MFT_ATTR_HEADER*)Add2Ptr(mftRec, mftRec->FirstAttrOffset);
    assert(currAttr->res.DataSize + currAttr->res.DataOffset <= currAttr->AttrSize);
    // should be called for dir MFT rec only
    assert(mftRec->Flags == (MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY));

    ATTR_FILE_NAME* attrFNameNDOS{ 0 }, * attrFNames[2]{ 0,0 };
    uint ind = 0;

    do
    {
        if (currAttr->AttrType == ATTR_FILENAME)
        {
            attrFNameNDOS = (ATTR_FILE_NAME*)Add2Ptr(currAttr, currAttr->res.DataOffset);
            attrFNames[ind++] = attrFNameNDOS;
            if (attrFNameNDOS->NameType == FILE_NAME_DOS) attrFNameNDOS = nullptr;
        }

        assert(currAttr->AttrSize > 0);
        currAttr = (MFT_ATTR_HEADER*)Add2Ptr(currAttr, currAttr->AttrSize);
        assert(mftRec->FileRecSize > Diff2Ptr(mftRec, currAttr));

    } while (*((uint32_t*)currAttr) != ATTR_END);

    if (attrFNames[0] == nullptr) //TODO or just return nullptr in this case?
        throw std::runtime_error("[GetFirstFileNameAttr] Attribute ATTR_FILENAME not found in MFT Rec!");

    assert(attrFNameNDOS); // non-DOS filename should always present

#ifndef NDEBUG    
    if (ind == 1) // if one, it should be non-DOS filename 
    {
        assert(attrFNameNDOS == attrFNames[0]);
        assert(attrFNames[0]->NameType != FILE_NAME_DOS);
    }

    if (ind == 2)
    {
        // at least one of two needs to be non-DOS filename
        assert((attrFNames[0]->NameType != FILE_NAME_DOS) || (attrFNames[1]->NameType != FILE_NAME_DOS));
    }
#endif

    return attrFNameNDOS;
}

// fills array attrFileNames with pointers to all ATTR_FILENAME attributes which mftRec contains
// attrFileNames is cleared each time before filling with new values
//TODO need to go to ATTR_LIST and get ATTR_FILENAME attrs from there
void TMFTParserBase::GetFileNameAttrPointers(MFT_FILE_RECORD* mftRec, THArray<ATTR_FILE_NAME*>& attrFileNames)
{
    attrFileNames.Clear();

    PMFT_ATTR_HEADER currAttr = (MFT_ATTR_HEADER*)Add2Ptr(mftRec, mftRec->FirstAttrOffset);
    ATTR_FILE_NAME* attrFName;
    THash<uint32_t, std::wstring> parents;

    assert(currAttr->res.DataSize + currAttr->res.DataOffset <= currAttr->AttrSize);

    do
    {
        assert(currAttr->AttrSize > 0);

        if (currAttr->AttrType == ATTR_FILENAME)
        {
            attrFName = (ATTR_FILE_NAME*)Add2Ptr(currAttr, currAttr->res.DataOffset);
            
            if (attrFName->NameType != FILE_NAME_DOS)
            {
                std::wstring wnm(GetFName(attrFName), attrFName->FileNameLen);

                if (parents.IfExists(attrFName->ParentDir.sId.low)) // all pairs (FileName, parent ID) should be different (excluding FILE_NAME_DOS)
                    assert(parents[attrFName->ParentDir.sId.low] != wnm);
                attrFileNames.AddValue(attrFName);
                parents.SetValue(attrFName->ParentDir.sId.low, wnm);
            }
        }

        currAttr = (MFT_ATTR_HEADER*)Add2Ptr(currAttr, currAttr->AttrSize);

        assert(mftRec->FileRecSize > Diff2Ptr(mftRec, currAttr));

    } while (*((uint32_t*)currAttr) != ATTR_END);
}


std::wstring TMFTParserBase::GetPathByAttrFileName(ATTR_FILE_NAME* attrFileName)
{
    THArray<std::wstring> arrPath;
    ATTR_FILE_NAME* attrFName = attrFileName;
    size_t ssize = 0;

    uint8_t* mftRecBuf = (uint8_t*)alloca(getVolData().BytesPerMFTRec);
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;

    std::wstring str(GetFName(attrFName), attrFName->FileNameLen);
    arrPath.AddValue(str);
    ssize += str.size();

    do
    {
        if (!FLoader.LoadMFTRecord(attrFName->ParentDir, mftRecBuf))
        {
            // throw exception because this is kind of critical error for this function
            throw std::runtime_error("[GetPathByAttrFileName] LoadMFTRecord call failed!");
        }

        // dir can contain only one or two filenames
        // in case of two - one is DOS another is WIN
        attrFName = GetFirstFileNameAttr(mftRec);

        str.assign(GetFName(attrFName), attrFName->FileNameLen);
        arrPath.AddValue(str);
        ssize += str.size();

    } while (attrFName->ParentDir.sId.low != MFT_ROOT_REC_ID);

    std::wstring result;
    result.reserve((size_t)(ssize * 1.1)); //TODO 1.1 is for backslashes, pay attention here later
    result = getVolData().Name;
    arrPath.Reverse();

    for (auto it = arrPath.begin(); it != arrPath.end(); ++it) {
        result += '\\';
        result += *it;
    }

    return result;
}

// there can be several paths starting from one MFT record, because of hard links.
bool TMFTParserBase::GetPathByMFTRecID(MFT_REF mftRecRef, THArray<std::wstring>& paths)
{
    THArray<ATTR_FILE_NAME*> attrFileNames;

    uint8_t* mftRecBuf = (uint8_t*)alloca(getVolData().BytesPerMFTRec);
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;

    if (!FLoader.LoadMFTRecord(mftRecRef, mftRecBuf))
    {
        // throw exception because this is kind of critical error for this function
        throw std::runtime_error("[GetPathByMFTRecID] LoadMFTRecord call failed!");
    }

    GetFileNameAttrPointers(mftRec, attrFileNames); // get all file names except for DOS ones

    for (auto attrFName : attrFileNames)
    {
        paths.AddValue(GetPathByAttrFileName(attrFName));
    }

    return true;
}

// fills array attrValues[] with pointers to all attributes which mftRec contains
// DOES NOT go inside ATTR_LIST_ATTR even if present (for optimization purposes)
// for ATTR_FILE_NAME and ATTR_LOGGED_UTILITY_STREAM only last such attribute placed into attrValues
// assumes that attrValues has allocated size for ATTR_TYPE_CNT items
void TMFTParserBase::FillAttrCollection(MFT_FILE_RECORD* mftRec, TAttrCollection& collection)
{
    MFT_ATTR_HEADER* currAttr = (MFT_ATTR_HEADER*)Add2Ptr(mftRec, mftRec->FirstAttrOffset);
    do
    {
        assert(mftRec->FileRecSize > Diff2Ptr(mftRec, currAttr));
        if (currAttr->NonResidentFlag == 0)
            assert(currAttr->res.DataSize + currAttr->res.DataOffset <= currAttr->AttrSize);

        if (currAttr->AttrType != ATTR_LIST_ATTR)
        {
            collection.Set(currAttr);
        }
        else
        {
            GET_LOGGER;

            if (currAttr->NonResidentFlag == 1)
            {
                logger.Debug("[FillAttrCollection] ATTR_LIST_ATTR non-resident START");

                if (!ParseNonresAttrList(currAttr, collection))
                {
                    logger.Error("ParseNonresAttrList returned error.");
                    return;
                }

                return;
            }
            else // ATTR_LIST is Resident
            {
                logger.Debug("[FillAttrCollection] ATTR_LIST_ATTR resident START");

                assert(currAttr->NonResidentFlag == 0);

                ATTR_LIST_ENTRY* attrListItem = (ATTR_LIST_ENTRY*)Add2Ptr(currAttr, currAttr->res.DataOffset);
                uint8_t* currAttrEnd = (uint8_t*)currAttr + currAttr->AttrSize;

                FillCollectionFromAttrList(attrListItem, currAttrEnd, currAttrEnd, collection); //TODO add error check

            }
        }

        assert(currAttr->AttrSize > 0);
        currAttr = (MFT_ATTR_HEADER*)Add2Ptr(currAttr, currAttr->AttrSize);
    } while (*((uint32_t*)currAttr) != ATTR_END);
}


// fills array attrValues[] with pointers to all attributes which mftRec contains
// DOES NOT go inside ATTR_LIST_ATTR even if present (for optimization purposes)
// for ATTR_FILE_NAME and ATTR_LOGGED_UTILITY_STREAM only last such attribute placed into attrValues
// assumes that attrValues has allocated size for ATTR_TYPE_CNT items
void TMFTParserBase::FillAttrValues(MFT_FILE_RECORD* mftRec, PMFT_ATTR_HEADER* attrValues)
{
    PMFT_ATTR_HEADER currAttr = (MFT_ATTR_HEADER*)Add2Ptr(mftRec, mftRec->FirstAttrOffset);
    ZeroMemory(attrValues, ATTR_TYPE_CNT * sizeof(PMFT_ATTR_HEADER));

    do
    {
        if (currAttr->NonResidentFlag == 0)
            assert(currAttr->res.DataSize + currAttr->res.DataOffset <= currAttr->AttrSize);

        // all atributes except ATTR_FILENAME and ATTR_LOGGED_UTILITY_STREAM should be in a single copy in one MFT rec
        if ((currAttr->AttrType != ATTR_FILENAME) && (currAttr->AttrType != ATTR_LOGGED_UTILITY_STREAM))
            assert(attrValues[MATI(currAttr->AttrType)] == nullptr);

        attrValues[MATI(currAttr->AttrType)] = currAttr;

        assert(currAttr->AttrSize > 0);
        currAttr = (MFT_ATTR_HEADER*)Add2Ptr(currAttr, currAttr->AttrSize);
        assert(mftRec->FileRecSize > Diff2Ptr(mftRec, currAttr));

    } while (*((uint32_t*)currAttr) != ATTR_END);
}

// returns true if requested attribute found in ATTR_LIST otherwise returns false 
// for resident ATTR_LIST it is called from GetAttr()
// for non-resident ATTR_LIST it is called from ParseNonresAttrList()
bool TMFTParserBase::GetAttrFromAttrList(ATTR_LIST_ENTRY* startListItem, ATTR_TYPE attrType, uint8_t* attrListEnd1, uint8_t* attrListEnd2, PMFT_ATTR_HEADER* result)
{
    GET_LOGGER;

    ATTR_LIST_ENTRY* attrListItem = startListItem;
    //uint8_t* dataBufEnd1 = dataBuf + dataBufSize;
    //uint8_t* dataBufEnd2 = dataBuf + attrAttrList->nonres.RealSize;

    assert(attrListItem->AttrSize > 0);
    assert(attrListItem->AttrType > 0);
    assert(attrListItem->StartVCN == 0);
    assert(((uint32_t)(attrListItem->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero

    uint32_t resIndex = 0;
    while (true)
    {
        if (attrListItem->AttrType == attrType)
        {
            MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)FLoader.LoadMFTRecordCache(attrListItem->ref);
            assert(mftRec != nullptr);
            if (mftRec)
            {
                PMFT_ATTR_HEADER attrValues2[ATTR_TYPE_CNT];
                FillAttrValues(mftRec, attrValues2);
                PMFT_ATTR_HEADER currAttr2 = attrValues2[MATI(attrType)];
                assert(currAttr2);
                assert(attrValues2[MATI(ATTR_LIST_ATTR)] == nullptr); // this is check that no ATTR_LIST_ATTR inside ATTR_LIST_ATTR

                //TODO optimization - for attributes other than ATTR_ALLOC return immediately when first value found
                if (currAttr2)
                    result[resIndex++] = currAttr2;
                else
                    logger.WarnFmt("[GetAttrFromAttrList] Attr: {} cannot be found is ATTR_LIST.", AttrName(attrType));

                assert(resIndex < SAME_ATTR_CNT);
            }
            else
            {
                // error loading MFT record
                // do not break loop and trying to load more records
                logger.Error("[GetAttrFromAttrList] LoadMFTRecordCache returned NULL MFT record!");
                //return;
            }
        }

        attrListItem = (ATTR_LIST_ENTRY*)Add2Ptr(attrListItem, attrListItem->AttrSize);
        if (((uint8_t*)attrListItem >= attrListEnd1)) break;
        if (((uint8_t*)attrListItem >= attrListEnd2)) break;
        assert(attrListItem->AttrType > 0);
        assert(attrListItem->AttrSize > 0);
        assert(attrListItem->StartVCN == 0);
        assert(((uint32_t)(attrListItem->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero        
    } //while

    if (resIndex == 0) // no requested attribute found in ATTR_LIST
    {
        logger.ErrorFmt("[GetAttrFromAttrList] Attr: {} is not found in the ATTR_LIST.", AttrName(attrListItem->AttrType));
        assert((attrType == ATTR_BITMAP) || (attrType == ATTR_ALLOC)); // only these two types can be missing
        return false;
    }

    return true;
}

// fills attr collection with all attrs from ATTR_LIST
// for resident ATTR_LIST it is called from GetAttr()
// for non-resident ATTR_LIST it is called from ParseNonresAttrList()
bool TMFTParserBase::FillCollectionFromAttrList(ATTR_LIST_ENTRY* startListItem, uint8_t* attrListEnd1, uint8_t* attrListEnd2, TAttrCollection& collection)
{
    GET_LOGGER;

    ATTR_LIST_ENTRY* attrListItem = startListItem;

    assert(attrListItem->AttrSize > 0);
    assert(attrListItem->AttrType > 0);
    assert(attrListItem->StartVCN == 0);
    assert(((uint32_t)(attrListItem->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero

    while (true)
    {
            MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)FLoader.LoadMFTRecordCache(attrListItem->ref);
            assert(mftRec != nullptr);
            if (mftRec)
            {
                FillAttrCollection(mftRec, collection);
            }
            else
            {
                // error loading MFT record
                // do not break loop and trying to load more records
                logger.Error("[GetAttrFromAttrList] LoadMFTRecordCache returned NULL MFT record!");
                //return;
            }

        attrListItem = (ATTR_LIST_ENTRY*)Add2Ptr(attrListItem, attrListItem->AttrSize);
        if (((uint8_t*)attrListItem >= attrListEnd1)) break;
        if (((uint8_t*)attrListItem >= attrListEnd2)) break;
        assert(attrListItem->AttrType > 0);
        assert(attrListItem->AttrSize > 0);
        assert(attrListItem->StartVCN == 0);
        assert(((uint32_t)(attrListItem->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero        
    } //while

    return true;
}

/**
* @brief Parses NON-RESIDENT ATTR_LIST_ATTR attribute
* @details Parses Non-Resident ATTR_LIST_ATTR attribute. Decodes data runs from the attribute and loads LCNs.
* After that it looks for attrType attribute in ATTR_LIST_ENTRY entries
* if several attrType attributes found all of them are returned in resulting array 'result'
* @param volData Need for ReadClusters call and for BytesPerCluster value
* @param attrListAttr Pointer to ATTR_LIST attribute to be parsed
* @param attrType Attrbite type we are looking for
* @param result Array where pointer to found attribute will be added. If several attributes of attrType are present, all of them will be added into result
*/
bool TMFTParserBase::ParseNonresAttrList(MFT_ATTR_HEADER* attrListAttr, ATTR_TYPE attrType, PMFT_ATTR_HEADER* result)
{
    GET_LOGGER_FUNC;

    ZeroMemory(result, SAME_ATTR_CNT * sizeof(result[0]));

    assert(attrListAttr->AttrType == ATTR_LIST_ATTR);
    assert(attrListAttr->NonResidentFlag == 1);

    TDataRuns dataRuns;
    if (!DecodeDataRuns(attrListAttr, dataRuns)) // DataRunsDecode writes a message into log file in case of an error
    {
        return false;
    }

    if (dataRuns.Count() > 1)
        logger.InfoFmt("[ParseNonresAttrList] UNUSUAL case. Non-resident ATTR_LIST_ATTR occupies {} data runs instead one.", dataRuns.Count());
    assert(dataRuns.Count() == 1); // assuming that one data run is always enough for list of attributes

    DATA_RUN_ITEM& rli = dataRuns[0];
    logger.DebugFmt("[ParseNonresAttrList] Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

    if (rli.len > 1)
        logger.InfoFmt("[ParseNonresAttrList] UNUSUAL case. Non-resident ATTR_LIST_ATTR datarun item occupies {} LCNs instead of one.", rli.len);
    assert(rli.len == 1); // assuming that one LCN is always enough for list of attributes

    auto dataBufSize = rli.len * getVolData().BytesPerCluster;
    uint8_t* dataBuf = (uint8_t*)alloca(dataBufSize);

    if (!ReadClusters(rli.lcn, rli.len, dataBuf)) // ReadClusters writes a message into log file in case of an error
    {
        return false;
    }

    assert(attrListAttr->nonres.RealSize < getVolData().BytesPerCluster); //may be incorrect assumption
    
    ATTR_LIST_ENTRY* attrListItem = (ATTR_LIST_ENTRY*)dataBuf;
    uint8_t* dataBufEnd1 = dataBuf + dataBufSize;
    uint8_t* dataBufEnd2 = dataBuf + attrListAttr->nonres.RealSize;

    if (!GetAttrFromAttrList(attrListItem, attrType, dataBufEnd1, dataBufEnd2, result))
        return false;

    return true;
}

bool TMFTParserBase::ParseNonresAttrList(MFT_ATTR_HEADER* attrListAttr, TAttrCollection& collection)
{
    GET_LOGGER_FUNC;

    assert(attrListAttr->AttrType == ATTR_LIST_ATTR);
    assert(attrListAttr->NonResidentFlag == 1);

    TDataRuns dataRuns;
    if (!DecodeDataRuns(attrListAttr, dataRuns)) // DataRunsDecode writes a message into log file in case of an error
    {
        return false;
    }

    if (dataRuns.Count() > 1)
        logger.InfoFmt("[ParseNonresAttrList] UNUSUAL case. Non-resident ATTR_LIST_ATTR occupies {} data runs instead one.", dataRuns.Count());
    assert(dataRuns.Count() == 1); // assuming that one data run is always enough for list of attributes

    DATA_RUN_ITEM& rli = dataRuns[0];
    logger.DebugFmt("[ParseNonresAttrList] Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

    if (rli.len > 1)
        logger.InfoFmt("[ParseNonresAttrList] UNUSUAL case. Non-resident ATTR_LIST_ATTR datarun item occupies {} LCNs instead of one.", rli.len);
    assert(rli.len == 1); // assuming that one LCN is always enough for list of attributes

    auto dataBufSize = rli.len * getVolData().BytesPerCluster;
    uint8_t* dataBuf = (uint8_t*)alloca(dataBufSize);

    if (!ReadClusters(rli.lcn, rli.len, dataBuf)) // ReadClusters writes a message into log file in case of an error
        return false;

    assert(attrListAttr->nonres.RealSize < getVolData().BytesPerCluster); //may be incorrect assumption

    ATTR_LIST_ENTRY* attrListItem = (ATTR_LIST_ENTRY*)dataBuf;
    uint8_t* dataBufEnd1 = dataBuf + dataBufSize;
    uint8_t* dataBufEnd2 = dataBuf + attrListAttr->nonres.RealSize;

    if (!FillCollectionFromAttrList(attrListItem, dataBufEnd1, dataBufEnd2, collection))
        return false;

    return true;
}

// goes to ATTR_LIST when needed to get requested attribute
//TODO what if MFT record contains several attrType attributes e.g. several ATTR_FILENAMEs?
void TMFTParserBase::GetAttr(ATTR_TYPE attrType, const PMFT_ATTR_HEADER* const attrValues, PMFT_ATTR_HEADER* result)
{
    ZeroMemory(result, SAME_ATTR_CNT * sizeof(result[0]));

    MFT_ATTR_HEADER* currAttr = attrValues[MakeAttrTypeIndex(attrType)];
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

    if (currAttr->NonResidentFlag == 1)
    {
        logger.Debug("[GetAttr] ATTR_LIST_ATTR non-resident START");

        if (!ParseNonresAttrList(currAttr, attrType, result))
        {
            logger.Error("ParseNonresAttrList returned error.");
            return;
        }

        if (result[0] == nullptr)
            logger.Debug("[GetAttr] ATTR_LIST_ATTR non-resident FINISHED NULL");
        else
            logger.Debug("[GetAttr] ATTR_LIST_ATTR non-resident FINISHED");

        return;
    }
    else // ATTR_LIST is Resident
    {
        logger.Debug("[GetAttr] ATTR_LIST_ATTR resident START");

        assert(currAttr->NonResidentFlag == 0);

        ATTR_LIST_ENTRY* attrListItem = (ATTR_LIST_ENTRY*)Add2Ptr(currAttr, currAttr->res.DataOffset);
        uint8_t* currAttrEnd = (uint8_t*)currAttr + currAttr->AttrSize;

        GetAttrFromAttrList(attrListItem, attrType, currAttrEnd, currAttrEnd, result);//TODO add error check

        if (result[0] == nullptr)
            logger.Debug("[GetAttr] ATTR_LIST_ATTR resident FINISHED NULL");
        else
            logger.Debug("[GetAttr] ATTR_LIST_ATTR resident FINISHED");

        /*
        assert(attrListItem->AttrSize > 0);
        assert(attrListItem->AttrType > 0);
        assert(attrListItem->StartVCN == 0);
        assert(((uint32_t)(attrListItem->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero

        uint32_t resIndex = 0;
        while (true)
        {
            // we come here only when requested atribute is located in another MFT rec
            if (attrListItem->AttrType == attrType)
            {
                MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)LoadMFTRecordCache(volData, attrListItem->ref);
                //auto mftRec = (MFT_FILE_RECORD*)mftRecBuf;
                assert(mftRec != nullptr); // mftRecBuf=null indicates error
                if (mftRec)
                {
                    PMFT_ATTR_HEADER attrValues2[ATTR_TYPE_CNT];
                    FillAttrValues(mftRec, attrValues2);
                    PMFT_ATTR_HEADER currAttr2 = attrValues2[MakeAttrTypeIndex(attrType)];
                    assert(currAttr2);
                    assert(attrValues2[MakeAttrTypeIndex(ATTR_LIST_ATTR)] == nullptr); // this is check that no ATTR_LIST_ATTR inside ATTR_LIST_ATTR

                    //TODO optimization - for attributes other then ATTR_ALLOC return immediately when first value found
                    if (currAttr2)
                        result[resIndex++] = currAttr2;
                    else
                        logger.WarnFmt("Attribute {} cannot be found is ATTR_LIST", AttrName(attrType));

                    assert(resIndex < SAME_ATTR_CNT);

                    logger.Debug("ATTR_LIST_ATTR FINISHED");
                }
                else
                {
                    // error loading MFT record
                    // do not break loop and trying to load more records
                    logger.Error("LoadMFTRecordCache returned null MFT record!");
                }
            }


            attrListItem = (ATTR_LIST_ENTRY*)Add2Ptr(attrListItem, attrListItem->AttrSize);
            if ((uint8_t*)attrListItem >= currAttrEnd) break;
            assert(attrListItem->AttrSize > 0);
            assert(attrListItem->StartVCN == 0);
            assert(attrListItem->AttrType > 0);
            assert(((uint32_t)(attrListItem->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
        }// while

        if(resIndex == 0)
            assert((attrType == ATTR_BITMAP) || (attrType == ATTR_ALLOC)); // only these two types can be missing
*/
    }

    // some attrs may not present in MFT rec. e.g. BITMAP is not created for empty directories while INDEX_ROOT exists
};

bool TMFTParserBase::ParseNonresBitmap(MFT_ATTR_HEADER* attr, TBitField& bitmap)
{
    assert(attr->NonResidentFlag == 1);
    assert(bitmap.Count() == 0);

    TDataRuns dataRuns;
    if (!DecodeDataRuns(attr, dataRuns)) // DataRunsDecode writes a message into log file in case of an error
        return false;

    assert(dataRuns.Count() > 0);

    uint8_t* dataBuf = nullptr;
    uint64_t dataBufLen = 0;  // dataBuf buffer size in clusters, how many clusters is allocated in dataBuf
    uint64_t dataBufSize = 0; // dataBuf buffer size in bytes
    uint32_t currRun = 0;
    //THArrayRaw bmpRecs(volData.BytesPerCluster);

    if (dataRuns.Count() > 1)
    {
        GET_LOGGER;
        logger.InfoFmt("[ParseNonresBitmap] Bitmap occupies more than one data run. Bitmap Data Runs Count: {}", dataRuns.Count());
    }

    while (currRun < dataRuns.Count())
    {
        DATA_RUN_ITEM& rli = dataRuns[currRun];

        if (rli.len > dataBufLen)
        {
            delete[] dataBuf;
            dataBufSize = rli.len * getVolData().BytesPerCluster;
            dataBuf = DBG_NEW uint8_t[dataBufSize];
            dataBufLen = rli.len;
        }

        assert(dataBuf);

        if (!ReadClusters(rli.lcn, rli.len, dataBuf)) // ReadCluster writes error meesage to log file in case of an error
        {
            delete[] dataBuf;
            return false;
        }

        // in most cases non-resident Bitmap will occupy one data run
        // therefore we have special handling of this case
        //if (dataRuns.Count() > 1)
        //    bmpRecs.AddMany(dataBuf, (uint32_t)rli.len); 

        assert((dataBufSize & 0x07) == 0); // bitmap data size always multiple of 8
        bitmap.AddData((uint64_t*)dataBuf, (uint32_t)dataBufSize >> 3);

        currRun++;
    }
    /*
    if (dataRuns.Count() > 1)
    {
        uint32_t bmpLenBytes = bmpRecs.Count() * bmpRecs.GetItemSize();
        assert((bmpLenBytes & 0x07) == 0); // bitmap data size always multiple of 8
        bitmap.SetData((uint64_t*)bmpRecs.Memory(), bmpLenBytes >> 3);
    }
    else
    {
        uint32_t bmpLenBytes = (uint32_t)(dataBufLen * volData.BytesPerCluster); //TODO shall we use nonres.RealSize here?
        assert(dataBufSize == bmpLenBytes);
        assert((bmpLenBytes & 0x07) == 0); // bitmap data size always multiple of 8
        bitmap.SetData((uint64_t*)dataBuf, bmpLenBytes >> 3);
    }*/

    delete[] dataBuf;
    return true;
}

bool TMFTParserBase::ParseBitmap(MFT_ATTR_HEADER* attr, TBitField& bitmap)
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
        return ParseNonresBitmap(attr, bitmap);
    }
}

void TMFTParserBase::ParseIndexRoot(MFT_ATTR_HEADER* attr, TLCNRecs& lcns, TFileList& fileList)
{
    GET_LOGGER;

    assert(fileList.Count() == 0);
    assert(attr->NonResidentFlag == 0); // always resident

    ATTR_INDEX_ROOT* indexR = (ATTR_INDEX_ROOT*)Add2Ptr(attr, attr->res.DataOffset);
    auto pihdr = &(indexR->ihdr);

    //assert(indexR->IndexBlockSize == volData.BytesPerCluster);
    assert(indexR->IndexBlockClst == 1);
    assert(indexR->AttrType == ATTR_FILENAME);
    assert(indexR->Rule == COLLATION_RULE::FILENAME);

    logger.DebugFmt("IndexRoot Indexed Attr Type: {} {:#x}", AttrName(indexR->AttrType), (uint32_t)indexR->AttrType);
    logger.DebugFmt("IndexRoot Collation Rule: {} ({:#x})", CollRuleName((uint32_t)indexR->Rule), (uint32_t)indexR->Rule);
    logger.DebugFmt("IndexRoot Dir Type: {} ({:#x})", indexR->ihdr.Flags == 0 ? "SMALL DIR" : "BIG DIR", indexR->ihdr.Flags);
    logger.DebugFmt("IndexRoot IndexBlockSize: {}", indexR->IndexBlockSize);
    logger.DebugFmt("IHDR Used Bytes: {}", pihdr->Used);

    GetFileListFromNode(pihdr, lcns, fileList);
}

//TOOD very short function. May be we do not need it?
bool TMFTParserBase::ParseAlloc(MFT_ATTR_HEADER* attr, TDataRuns& dataRuns)
{
    // sometimes one ATTR_LIST list may contain two ATTR_ALLOC attributes for some reason
    // it means we come here two times during parsing one MFT record with such ATTR_LIST 
    //assert(dataRuns.Count() == 0);

    if (!attr) return true; // attr==NULL means that ALLOC attribute is not present in MFT rec. Usually ALLOC is not needed in MFT record for empty directories

    assert(attr->AttrType == ATTR_ALLOC);
    assert(attr->NonResidentFlag == 1);

    return DecodeDataRuns(attr, dataRuns); // DataRunDecode writes message to log in case of an error
}

// reads three required attributes from mftRec (INDEX_ROOT, ALLOC and BITMAP) 
// and then loads files from then in SORTED order starting from IndexRoot
// goes to subnodes when needed
// mftRec record must be a directory type
// 'node' parameter is for returning back list of files only (in node.Filelist field).
// Returns -1 in case of error
int32_t TMFTParserBase::GetFileListFromMFTRec(MFT_FILE_RECORD* mftRec, DIR_NODE& node)
{
    GET_LOGGER;

    assert(mftRec->Flags == (MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY)); // only "directory" record should go here
    if (mftRec->Flags != (MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY))
    {
        logger.Error("[GetFileListFromMFTRec] Error: mftRec->Flags != MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY !");
        return -1; // error
    }

    assert(node.Bitmap.Count() == 0);

    PMFT_ATTR_HEADER attrValues[ATTR_TYPE_CNT]; // list of pointers to attributes, each pointer refers to attr inside mftRec
    FillAttrValues(mftRec, attrValues); // does NOT go to inside ATTR_LIST or to other MFT/LCN records

    PMFT_ATTR_HEADER multValues[SAME_ATTR_CNT];

    GetAttr(ATTR_BITMAP, attrValues, multValues);
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
        if (!ParseBitmap(bitmap, node.Bitmap)) // copy bitmap into TBitField class for easier access
        {
            logger.Error("[GetFileListFromMFTRec] ParseBitmap finished with error.");
        }
    }
    //TODO looks like we will parse ATTR_LIST_ATTR several times here if BITMAP, ALLOC or INDEX_ROOT are in ATTR_LIST_ATTR attribute
    GetAttr(ATTR_ALLOC, attrValues, multValues); // multiple values can be returned
    uint32_t i = 0;
    while (multValues[i] != nullptr)
    {
        if (!ParseAlloc(multValues[i++], node.DataRuns)) // decode data runs and store them in node.DataRuns
        {
            logger.Error("[GetFileListFromMFTRec] ParseAlloc finished with error.");
        }
    }

    uint64_t lcnTotalCount = 0;
    for (auto& run : node.DataRuns) lcnTotalCount += run.len;
    TLCNRecs lcns(getVolData().BytesPerCluster, (uint32_t)lcnTotalCount);

    ProcessLCNsPred addToLcnsPred = [&lcns](uint8_t* dataBuf, CLST VCN, CLST LCN)
        {
            lcns.AddRec(dataBuf, VCN, LCN);
        };


    if (!ProcessDataRuns(node, addToLcnsPred))
        //if (lcns.LoadDataRuns(FVolumeData, node) == -1)
    {
        logger.Error("[GetFileListFromMFTRec] TLCNRecs.LoadDataRuns finished with error.");
        return -1; // fail to load data runs this is critical error, return immediately with error
    }

    GetAttr(ATTR_ROOT, attrValues, multValues);
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


//parses either resident or non-resident ATTR_LIST
void TMFTParserBase::ParseAttrList(MFTRecIndex indexMFTRec, ATTR_LIST_ENTRY* startListItem, uint8_t* attrListEnd, uint64_t realSize, uint64_t& processedAttrSize, AttrListPred processChildMFTRecPred)
{
    GET_LOGGER;

    //uint64_t processedAttrSize = 0;

    ATTR_LIST_ENTRY* attrEntry = startListItem;

    assert(attrEntry->AttrSize > 0);
    assert(attrEntry->AttrType > 0);
    // assert(attrEntry->StartVCN == 0);
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
        if ((attrEntry->AttrType != ATTR_DATA) && (attrEntry->AttrType != ATTR_ALLOC))  // StartVCN should be 0 for all attrs except ATTR_DATA and ATTR_ALLOC
        {
            if (attrEntry->StartVCN != 0)
                logger.WarnFmt("Looks like we have met incorrect case. StartVCN({}) <> 0 for {} attribute. MFT Rec ID: {}.",
                    attrEntry->StartVCN, AttrName(attrEntry->AttrType), MFT_REF::toHexString(indexMFTRec));
            assert(attrEntry->StartVCN == 0);
        }

        // attributes in non-resident attr list located in a separate LCN cluster may refer back to the base record
        // because some attributes may reside in base mft record and the others in "child" mft record(s)
        // the attr list attribute itself is located in LCN cluster that is not mft record, it does not contain signature or Fixups values, etc.

        if (visitedMFTRec.IndexOf(attrEntry->ref.sId.low) == -1) // whether we haven't parsed this MFT record yet
        {
            processChildMFTRecPred(attrEntry->ref);
            // attrEntry->ref is a MFT rec where attr value is located
            //if (!ReadMftItemInfo(volData, attrEntry->ref, itemInfo))
            //{
            //    logger.Error("ReadMftItemInfo() returned false!");
            //}
            visitedMFTRec.AddValue(attrEntry->ref.sId.low);
        }

        // StartVCN is a cluster where attribute portion value is located
        if (attrEntry->StartVCN != 0)
        {
            assert((attrEntry->AttrType == ATTR_DATA) || (attrEntry->AttrType == ATTR_ALLOC));
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

        assert(attrEntry->AttrType > 0);
        assert(attrEntry->AttrSize > 0);
        assert(((uint32_t)(attrEntry->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
        assert(attrEntry->AttrType != ATTR_ZERO);
        assert(attrEntry->AttrType != ATTR_END);
    }
}


/**
* @brief Returns MFT Record ID (low part of it) for specified path string
* @details Goes through path sub-dirs in path, reads files in SORTED order, goes to subdirs and so on, until end of path reached.
* Returns MFT Record ID (low part of it). If path is incorrect function returns 0 (zero).
* Uses ci_string intentionally to proper case insensitive folders compare.
* @param VolData Volume data. Needed for reading file system.
* @param path Fully qualified path to file or folder that starts from disk name.
*/
MFTRecIndex TMFTParserBase::GetMFTRecIdByPath(const ci_string& path) // ci_string is for case INsensitive search here
{
    //TODO what is path is relative (does not start from c:\)?

    if (path.size() == 0) return 0;

    GET_LOGGER;

    std::vector<ci_string> arr;
    StringToArray(path, arr, '\\');
    if (arr.size() == 0) return 0;

    // make sure that volData.hVolume and volume in path parameter are the same (both C: or both D:, etc)
    assert(toupper(arr[0][0]) == toupper(getVolData().Name[0]));

    MFT_REF mftRecID{ 0 };
    mftRecID.sId.low = MFT_ROOT_REC_ID;
    uint8_t* mftRecBuf = (uint8_t*)alloca(getVolData().BytesPerMFTRec);

    DIR_NODE node;
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;

    for (size_t i = 1; i < arr.size(); i++) // bypass drive letter for now
    {
        if (!FLoader.LoadMFTRecord(mftRecID, mftRecBuf))
        {
            logger.Error("LoadMFTRecord finished with error.");
            return false;
        }

        node.Clear();
        GetFileListFromMFTRec(mftRec, node); //TODO add error handling here
        FILE_NAME fn;
        fn.ciName = arr[i];
        // binary search in sorted array
        auto iter = std::lower_bound(node.FileList.begin(), node.FileList.end(), fn); // fn has overrided operators < and ==
        if (iter != node.FileList.end() && (*iter).ciName == fn.ciName)
        {
            mftRecID = iter->MFTRef;
        }
        else
        {
            mftRecID.Id = 0; // signal that file not found
            break; // file not found 
        }
    }

    return mftRecID.sId.low;
}
