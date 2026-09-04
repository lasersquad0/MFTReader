
#include "Debug.h"
#include "NTFS.h"
#include "Functions.h" // for TErrorCode
#include "Readers.h"

/**
* @brief Function for reading Index Blocks from Data Runs and passing them into predicate (second param) for processing
* @details Reads all LCNs from Data Runs in node.DataRuns. For each LCN it calls predicate processIndexBlockPred for processing each Index Block.
* Predicate can either extract list of files from LCN or add the LCN to cache for further processing, or do anything else.
* When used to extract list of files from LCNs, files are extracted in random order (in order of LCNs in Data Runs) and does not go to sub-nodes.
* node.Bitmap is used to select which Index Blocks are valid. Predicate processIndexBlockPred is called only for valid Index Blocks.
* @param node Contains Data Runs to be processed, and Bitmap that tells us what Index Blocks are valid.
* @param processIndexBlockPred Predicate used for processing each Index Block.
*/
TErrorCode TMFTBaseReader::ProcessAllocDataRuns(DIR_NODE& node, ProcessiBlocksPred processIndexBlockPred)
{
    GET_LOGGER;
    logger.Debug("---------- START PROCESSING ATTR_ALLOC Data Runs ---------");

    uint32_t BytesPerCluster = getVolData().BytesPerCluster;

    assert(node.IndexBlockSize > 0);
    if (node.IndexBlockSize >= BytesPerCluster)
        assert((node.IndexBlockSize % BytesPerCluster) == 0);
    else
        assert((BytesPerCluster % node.IndexBlockSize) == 0);
    
    int64_t lastBit = node.Bitmap.LastBit();
    
    if (lastBit == -1)
    {
        if (node.Bitmap.Count() == 0)
            logger.Info("[ProcessAllocDataRuns] BITMAP attribite is not present.");
        else
            logger.DebugFmt("[ProcessAllocDataRuns] BITMAP attribute present, but all bits set zero. Bits count: {}", node.Bitmap.Count() * 64ull);

        logger.Debug("---------- END OF PROCESSING ATTR_ALLOC Data Runs ---------");

        return TErrorCode::Success;
    }

    logger.DebugFmt("BITMAP Size in 64bit words: {}, Value64: {:#x}", node.Bitmap.Count(), *(uint64_t*)node.Bitmap.GetData());

    int64_t iblockCounter = 0; // counter in Index Blocks (Index Block size may differ from cluster size)
    uint8_t* dataBuf = nullptr;
    uint64_t dataBufSize = 0;
    uint32_t currRun = 0;
    TErrorCode result = TErrorCode::Success;

    while (currRun < node.DataRuns.Count())
    {
        if (iblockCounter > lastBit) // no more valid LCNs, break loop 
            break;

        DATA_RUN_ITEM& rli = node.DataRuns[currRun];
        logger.DebugFmt("[ProcessAllocDataRuns] Data Run Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

        // check correctness of decoded LCNs
        assert(rli.len < (uint64_t)getVolData().TotalClusters.QuadPart);
        assert(rli.lcn < (uint64_t)getVolData().TotalClusters.QuadPart);

        //uint32_t k = node.IndexBlockSize / getVolData().BytesPerCluster;
        //assert(k > 0);

        assert(lastBit + 1 - iblockCounter > 0);
        //TODO return to this optimization later because assert((rliBufSize % getVolData().BytesPerCluster) == 0) fails for some reason
        uint64_t rliBufSize = rli.len * getVolData().BytesPerCluster; //valuemin((uint64_t)(lastBit + 1 - iblockCounter) * node.IndexBlockSize, rli.len * getVolData().BytesPerCluster);
        assert((rliBufSize % node.IndexBlockSize) == 0);
        assert((rliBufSize % getVolData().BytesPerCluster) == 0);

        if (rliBufSize > dataBufSize)
        {
            delete[] dataBuf;
            dataBufSize = rliBufSize; //rlilen * getVolData().BytesPerCluster;
            dataBuf = DBG_NEW uint8_t[dataBufSize]; 
            assert(dataBuf);
            //dataBufLen = rlilen;
        }

        result = FLoader.ReadClusters(rli.lcn, rliBufSize / getVolData().BytesPerCluster, dataBuf);
        if (result != TErrorCode::Success) // ReadClusters wrties error message to log file in case of an error
        {
            //result = res;
            break;
        }

        // how many Index Blocks we've read by recent ReadClusters call
        uint64_t iblocksCount = rliBufSize / node.IndexBlockSize;

        NTFS_RECORD_HEADER* indexRec = (NTFS_RECORD_HEADER*)dataBuf;

        for (size_t i = 0; i < iblocksCount; i++)
        {
            if (node.Bitmap.Test(iblockCounter++)) // add only Index Blocks (not LCNs) which are marked in bitmap bitfield
            {
                if (!ntfs_is_indx_recp(indexRec->Signature)) // bypass non 'INDX' clusters (usually filled by zero)
                {
                    // Not sure if this is correct situation when list of LCNs in one data run has "holes" for which Bitmap attribute has 1 in appropriate cluster.
                   
                    uint8_t* sign = indexRec->Signature;
                    logger.WarnFmt("[ProcessAllocDataRuns] Signature 'INDX' has not been found in LCN cluster {}. Signature found: {}{}{}{}", 
                        rli.lcn + i* node.IndexBlockSize / getVolData().BytesPerCluster, sign[0], sign[1], sign[2], sign[3]);

                 //   continue;
                }
                else
                {
                    // do fixups only for valid blocks
                    result = FLoader.FixupUSA1((NTFS_RECORD_HEADER*)(dataBuf + i * node.IndexBlockSize), node.IndexBlockSize, getVolData().BytesPerSector);
                    if (result != TErrorCode::Success)
                    {
                        //result = false;
                        break;
                    }
                }

                //TODO why do we call processIndexBlockPred for index blocks that do not contain INDS signature?
                //process particular Index Block, either add to list of blocks in cache or get list of files from this record, depending on predicate
                processIndexBlockPred(dataBuf + i * node.IndexBlockSize, rli.vcn + i*node.IndexBlockSize/getVolData().BytesPerCluster, rli.lcn + i*node.IndexBlockSize/getVolData().BytesPerCluster);
            }
            else
            {
                logger.DebugFmt("[ProcessAllocDataRuns] Bitmap bit {}th is zero. LastBit: {}. Bypassing Index Block# {}.", 
                    iblockCounter, lastBit, rli.lcn + i* node.IndexBlockSize / getVolData().BytesPerCluster);
                
                if (iblockCounter > lastBit) // no more valid LCNs 
                    break;
            }
        }

        currRun++;
    }

    delete[] dataBuf;

    logger.Debug("---------- END OF PROCESSING ATTR_ALLOC Data Runs ---------");

    return result;
}


TErrorCode TMFTBaseReader::DecodeDataRuns(MFT_ATTR_HEADER* attr, TDataRuns& runs)
{
    assert(attr->AttrType == ATTR_ALLOC || attr->AttrType == ATTR_BITMAP || attr->AttrType == ATTR_LIST_ATTR || attr->AttrType == ATTR_DATA);

    GET_LOGGER;

    // data runs exist in non-resident attributes only (ALLOC, DATA, BITMAP)
    if (!attr || (attr->NonResidentFlag == ATTR_FLAG_RESIDENT))
    {
        // looks like incorrect data in MFT
        logger.Error("[DataRunsDecode] Attr parameter is NULL or is resident (must be non-resident)!!!");
        return TErrorCode::InvalidArgument;
    }

    uint8_t* datarun = Add2Ptr(attr, attr->nonres.DataRunsOffset);
    uint8_t* attrEnd = Add2Ptr(attr, attr->AttrSize);
    assert(attr->AttrSize > 0);

    uint64_t currVCN = attr->nonres.StartVCN;
    uint64_t currLCN = 0;

    // read all data runs 
    while ((datarun < attrEnd) && *datarun) // stop if we reached zero in both nibbles (half bytes) or reached attrEnd
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
            return TErrorCode::CorruptedData;
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
        if (deltaxcn == 0) 
            ri.lcn = 0; // for sparse files data run contains "virtual" LCN virtualy filled by zero
        else 
            ri.lcn = currLCN;

        runs.AddValue(ri);

        datarun += b3 + 1; // move to the next data run

        logger.TraceFmt("[DataRunsDecode] Data Run#{}, VCN: {}, LCN: {}, Len: {}", runs.Count(), ri.vcn, ri.lcn, ri.len);
    }

    logger.DebugFmt("[DataRunsDecode] Total Data Runs: {}, Last VCN: {}", runs.Count(), currVCN);

    return TErrorCode::Success;
}


/// calls predicate pred for all files got from ihdr
/// DOES NOT go to subnodes
void TMFTBaseReader::GetFileList(INDEX_HDR* ihdr, AddFileAttrPred pred)
{
    GET_LOGGER;

    assert(ihdr->Used <= ihdr->Allocated);

    uint32_t off = ihdr->DEOffset; // offset of 1st dir entry

    while (true) // iterate though all DE+FILE_NAME entries
    {
        assert(off < ihdr->Used);

        NTFS_DE* de = (NTFS_DE*)Add2Ptr(ihdr, off); // NTFS_DE it is a "header" above File Name attribute, covers each file name attribute item
        
        if (logger.ShouldLog(LogEngine::llDebug))
        {
            logger.DebugFmt("DE Ref to MFT Rec: {}", de->RecRef.toHexString()); // reference to MFT Rec for this file name
            logger.DebugFmt("DE Flags: {} ({:#x})", de->flags == NTFS_IE_HAS_SUBNODES ? "HAS SUBNODES" : de->flags == NTFS_IE_LAST ? "LAST" : de->flags == 0 ? "OTHER" : "UNKNOWN", de->flags);
            logger.DebugFmt("DE Size: {}", de->size);
            logger.DebugFmt("DE Key_size: {} {}", de->key_size, de->key_size == 0 ? "(last DE usually empty, does not contain any FILE_ATTR attribute)" : "");
        }

        assert(de->size >= de->key_size + sizeof(NTFS_DE));

        if (de->key_size > 0) // key_size>0 means that filenameattr exists
        {
            ATTR_FILE_NAME* fattr = (ATTR_FILE_NAME*)Add2Ptr(de, sizeof(NTFS_DE));

            assert(de->key_size == sizeof(ATTR_FILE_NAME) + fattr->FileNameLen * sizeof(wchar_t));
            assert(de->size >= (sizeof(NTFS_DE) + sizeof(ATTR_FILE_NAME) + fattr->FileNameLen * sizeof(wchar_t)));
            assert((fattr->dup.FileAttrib & FILE_ATTRIBUTE_NORMAL) == 0);// check that NORMAL bit is always zero

            if (fattr->NameType != FILE_NAME_DOS) // bypass DOS filenames
            {
                pred(fattr, de->RecRef);
            }

            if (logger.ShouldLog(LogEngine::llDebug))
            {
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
// DOES NOT add DOS file names into fnames list
void TMFTBaseReader::GetFileListFromNode(INDEX_HDR* ihdr, TLCNRecs& lcns, TFileList& fnames)
{
    GET_LOGGER;

    uint32_t off = ihdr->DEOffset; // offset of 1st dir entry

    while (true) // iterate though all DE+FILE_NAME entries
    {
        assert(off < ihdr->Used);

        NTFS_DE* de = (NTFS_DE*)Add2Ptr(ihdr, off); // NTFS_DE it is a "header" above File Name attribute, covers each file name attribute item

        logger.DebugFmt("Dir Entry Ref to MFT Rec: {0} ({0:#x})", de->RecRef.Id);
        logger.DebugFmt("Dir Entry Flags: {} ({})", de->flags == NTFS_IE_HAS_SUBNODES ? "HAS SUBNODES" : de->flags == NTFS_IE_LAST ? "LAST" : de->flags == 0 ? "OTHER" : "UNKNOWN", de->flags);
        logger.DebugFmt("Dir Entry Size: {}", de->size);
        logger.DebugFmt("Dir Entry Key_size: {}", de->key_size);

        assert(de->size >= de->key_size + sizeof(NTFS_DE));

        if (de->flags & NTFS_IE_HAS_SUBNODES)
        {
            // last 8 bytes contain the VCN of subnode. This field is present only if (flags & NTFS_IE_HAS_SUBNODES)
            uint64_t vcn = *(uint64_t*)Add2Ptr(ihdr, off + de->size - sizeof(uint64_t));

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
                fnames.AddValue({ convert_string<ci_string::value_type>(wnm).c_str(), *fattr, de->RecRef });
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

// called for directory MFT records only
// returns pointer to the first non-DOS ATTR_FILENAME attribute in mftRec
// dir MFT rec can contain either one or two ATTR_FILENAME attributes, in case of two one of them is DOS attribute
ATTR_FILE_NAME* TMFTBaseReader::GetDirNameAttr(MFT_FILE_RECORD* mftRec)
{
    // should be called for dir MFT rec only
    assert(mftRec->Flags == (MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY));

    TAttrCollection collection;
    auto res = FillAttrCollection(mftRec, MakeAttrBitmask(ATTR_FILENAME), collection);// fill collection only with ATTR_FILENAME attributes
    assert(res == TErrorCode::Success);
    if (res != TErrorCode::Success)
        return nullptr;

    auto& fileNames = collection.Get(ATTR_FILENAME);

    // dir MFT record can contain only 1 or 2 ATTR_FILENAME attributes
    assert(fileNames.Count() < 3);
    assert(fileNames.Count() > 0);
    
    ATTR_FILE_NAME* attrFNameNDOS{ 0 };

    auto attr = fileNames[0];

    attrFNameNDOS = (ATTR_FILE_NAME*)Add2Ptr(attr, attr->res.DataOffset);

    if (fileNames.Count() == 1)
    {
        assert(attrFNameNDOS->NameType != FILE_NAME_DOS);
    }
    else
    {
        attr = fileNames[1];
        auto attrFNameNDOS2 = (ATTR_FILE_NAME*)Add2Ptr(attr, attr->res.DataOffset);

        if (attrFNameNDOS->NameType == FILE_NAME_DOS)
        {
            assert(attrFNameNDOS2->NameType != FILE_NAME_DOS);
            assert(attrFNameNDOS->ParentDir.sId.low == attrFNameNDOS2->ParentDir.sId.low);
            attrFNameNDOS = attrFNameNDOS2;
        }
        else
        {
            assert(attrFNameNDOS2->NameType == FILE_NAME_DOS);
            assert(attrFNameNDOS->ParentDir.sId.low == attrFNameNDOS2->ParentDir.sId.low);
        }
    }

    return attrFNameNDOS;
}

// fills array attrFileNames with pointers to all ATTR_FILENAME attributes which mftRec contains except DOS ones 
// Goes inside of ATTR_LIST_ATTR attribute if MFT record has it
// attrFileNames is cleared each time before filling with new values
TErrorCode TMFTBaseReader::GetFileNameAttrPointers(MFT_FILE_RECORD* mftRec, THArray<ATTR_FILE_NAME*>& attrFileNames)
{
    attrFileNames.Clear();

    TAttrCollection collection;
    auto res = FillAttrCollection(mftRec, MakeAttrBitmask(ATTR_FILENAME), collection); // fill collection only with ATTR_FILENAME attributes
    if (res != TErrorCode::Success)
        return res;

    ATTR_FILE_NAME* attrFName;
    THash<MFTRecIndex, std::wstring> parents;

    for (auto attr : collection.Get(ATTR_FILENAME))
    {
        assert(attr->AttrType == ATTR_FILENAME);
        assert(attr->NonResidentFlag == ATTR_FLAG_RESIDENT);

        attrFName = (ATTR_FILE_NAME*)Add2Ptr(attr, attr->res.DataOffset);

        if (attrFName->NameType != FILE_NAME_DOS)
        {
            std::wstring wnm(GetFName(attrFName), attrFName->FileNameLen);

            if (parents.IfExists(attrFName->ParentDir.sId.low)) // all pairs (FileName, parent ID) should be different (excluding FILE_NAME_DOS)
                assert(parents[attrFName->ParentDir.sId.low] != wnm);
            parents.SetValue(attrFName->ParentDir.sId.low, wnm);
            attrFileNames.AddValue(attrFName);
        }
    }

    return TErrorCode::Success;
}


std::wstring TMFTBaseReader::GetPathByAttrFileName(ATTR_FILE_NAME* attrFileName)
{
    THArray<std::wstring> arrPath;
    ATTR_FILE_NAME* attrFName = attrFileName;
    size_t ssize = 0;

    uint8_t* mftRecBuf = (uint8_t*)alloca(getVolData().BytesPerMFTRec);
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;

    std::wstring str(GetFName(attrFName), attrFName->FileNameLen);
    arrPath.AddValue(str);
    ssize += str.size();

    while (attrFName->ParentDir.sId.low != MFT_ROOT_REC_ID)
    {
        auto res = FLoader.LoadMFTRecord(attrFName->ParentDir, mftRecBuf);
        if (res != TErrorCode::Success)
        {
            // throw exception because this is kind of critical error for this function
            throw std::runtime_error("[GetPathByAttrFileName] LoadMFTRecord call failed!");
        }

        // dir can contain only one or two filenames
        // in case of two - one is DOS another is WIN
        attrFName = GetDirNameAttr(mftRec);

        str.assign(GetFName(attrFName), attrFName->FileNameLen);
        arrPath.AddValue(str);
        ssize += str.size();

    }

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

/**
* @brief Gets full path(s) of a file specified by MFT record Id. 
* @details There can exist several paths which "start" from one MFT record, because of hard links.
* @param mftRecRef MFT record ID to build path(s) for
* @param paths Array where all found paths will be returned back. Function does NOT clear paths array before adding new ones.
*/
TErrorCode TMFTBaseReader::PathByMFTRecID(MFT_REF mftRecRef, THArray<std::wstring>& paths)
{
    // if we are on root dir (C:\), add it and exit
    if (mftRecRef.sId.low == MFT_ROOT_REC_ID)
    {
        paths.AddValue(getVolData().Name);
        return TErrorCode::Success;
    }

    uint8_t* mftRecBuf = (uint8_t*)alloca(getVolData().BytesPerMFTRec);
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;

    auto res = FLoader.LoadMFTRecord(mftRecRef, mftRecBuf);
    if (res != TErrorCode::Success)
        return res;

    THArray<ATTR_FILE_NAME*> attrFileNames;

    res = GetFileNameAttrPointers(mftRec, attrFileNames); // get all file names except for DOS ones
    assert(res == TErrorCode::Success);
    if (res != TErrorCode::Success)
        return res;

    for (auto attrFName : attrFileNames)
    {
        std::wstring str = GetPathByAttrFileName(attrFName);
        assert(paths.IndexOf(str) == -1); // check for duplicates
        paths.AddValue(str);
    }

    return TErrorCode::Success;
}

/**
* @brief Fills parameter collection with pointers to all attributes which mftRec contains
* @details if ATTR_LIST_ATTR is present it goes inside and gets attributes from ATTR_LIST_ATTR.
* There can be multiple ATTR_FILE_NAME, ATTR_DATA and ATTR_LOGGED_UTILITY_STREAM attributes in one MFT record.
* FillAttrCollection collects all such attributes into internal arrays.
* @param mftRec Pointer to a record to be parsed for attributes
* @param attrFilter bitwise mask that tells which attrbutes will be added to collection. This is bitwise mask of
*/
TErrorCode TMFTBaseReader::FillAttrCollection(MFT_FILE_RECORD* mftRec, TAttrCollection& collection)
{
    return FillAttrCollection(mftRec, ALL_ATTRS_FILTER, collection);
}

TErrorCode TMFTBaseReader::FillAttrCollection(MFT_FILE_RECORD* mftRec, uint32_t attrFilter, TAttrCollection& collection)
{
    AttrListPred callProcessChildMFTRecsPred = [this, &attrFilter, &collection](const MFT_REF& RecRef)
        {
            // RecRef - is a child MFT rec where attr value is located

            // we need cache version because mftRecBuf should remain valid till we return back to GetMFTRecIdByPath in calls stack
            auto mftRecBuf = FLoader.LoadMFTRecordCache(RecRef);
            if (mftRecBuf)
            {
                return FillAttrCollection((MFT_FILE_RECORD*)(*mftRecBuf), attrFilter, collection);
            }
            else
            {
                GET_LOGGER;
                // error loading MFT record
                // do not break loop and trying to load more records
                logger.Error("[callAddItemToCollectionPred] LoadMFTRecordCache returned NULL!");
                return mftRecBuf.error();
            }

            //return TErrorCode::Success;
        };

    MFT_ATTR_HEADER* currAttr = (MFT_ATTR_HEADER*)Add2Ptr(mftRec, mftRec->FirstAttrOffset);
    do
    {
        assert(mftRec->FileRecSize > Diff2Ptr(mftRec, currAttr));
        if (currAttr->NonResidentFlag == ATTR_FLAG_RESIDENT)
            assert(currAttr->res.DataSize + currAttr->res.DataOffset <= currAttr->AttrSize);

        //attr ATTR_LIST_ATTR cannot be filtered, it always processed
        if (currAttr->AttrType != ATTR_LIST_ATTR)
        {
            if (MakeAttrBitmask(currAttr->AttrType) & attrFilter)
                collection.Set(currAttr, mftRec->IndexMFTRec);
        }
        else
        {
            GET_LOGGER;

            if (currAttr->NonResidentFlag == ATTR_FLAG_NONRESIDENT)
            {
                logger.Debug("[FillAttrCollection] ATTR_LIST Non-Resident - START PARSING");

                auto res = ParseNonresAttrList(mftRec->IndexMFTRec, attrFilter, currAttr, callProcessChildMFTRecsPred);
                if (res != TErrorCode::Success)
                {
                    logger.Error("ParseNonresAttrList returned error.");
                    return res;
                }

                logger.Debug("[FillAttrCollection] ATTR_LIST Non-Resident - FINISHED PARSING");

            }
            else // ATTR_LIST is Resident
            {
                logger.Debug("[FillAttrCollection] ATTR_LIST Resident - START PARING");

                assert(currAttr->NonResidentFlag == ATTR_FLAG_RESIDENT);

                ATTR_LIST_ENTRY* attrListItem = (ATTR_LIST_ENTRY*)Add2Ptr(currAttr, currAttr->res.DataOffset);
                uint8_t* currAttrEnd = (uint8_t*)currAttr + currAttr->AttrSize;
                uint64_t processedAttrSize = 0;

                THArray<MFTRecIndex> visitedMFTRec;
                visitedMFTRec.AddValue(mftRec->IndexMFTRec);

                auto res = ParseAttrList(mftRec->IndexMFTRec, attrFilter, attrListItem, currAttrEnd, currAttr->res.DataSize, processedAttrSize, visitedMFTRec, callProcessChildMFTRecsPred);
                if (res != TErrorCode::Success)
                {
                    logger.Error("ParseAttrList returned error.");
                    return res;
                }

                logger.Debug("[FillAttrCollection] ATTR_LIST Resident - FINISHED PARING");
            }
        }

        assert(currAttr->AttrSize > 0);
        currAttr = (MFT_ATTR_HEADER*)Add2Ptr(currAttr, currAttr->AttrSize);

    } while (*((uint32_t*)currAttr) != ATTR_END);

    return TErrorCode::Success;
}

/**
* @brief Version of function without attrFilter parameter. processChildMFTRecPred will be called for all attributes found in attrListAttr.
*/
TErrorCode TMFTBaseReader::ParseNonresAttrList(MFTRecIndex indexMFTRec, MFT_ATTR_HEADER* attrListAttr, AttrListPred processChildMFTRecPred)
{
    return ParseNonresAttrList(indexMFTRec, ALL_ATTRS_FILTER, attrListAttr, processChildMFTRecPred);
}

/**
* @brief Parses NON-RESIDENT ATTR_LIST attribute
* @details Decodes data runs from the ATTR_LIST attribute and loads LCNs.
* After that it looks for attributes defined by attrFilter parameter in ATTR_LIST_ENTRY entries
* For each attribute if it included into attrFilter it calls processChildMFTRecPred predicate 
* @param indexMFTRec index of MFT record being parsed
* @param attrFilter bitwise mask that tells which attributes will be processed. For these attributes processChildMFTRecPred will be called 
* @param attrListAttr Pointer to attribute header containing ATTR_LIST attribute to be parsed
* @param processChildMFTRecPred predicate of ArrListPRes type that will be called for each attrivute found in attr list (provided that it is included into attrFilter). 
*/
TErrorCode TMFTBaseReader::ParseNonresAttrList(MFTRecIndex indexMFTRec, uint32_t attrFilter, MFT_ATTR_HEADER* attrListAttr, AttrListPred processChildMFTRecPred)
{
    GET_LOGGER;

    assert(attrListAttr);
    assert(attrListAttr->AttrType == ATTR_LIST_ATTR);
    assert(attrListAttr->NonResidentFlag == ATTR_FLAG_NONRESIDENT);

    TDataRuns dataRuns;
    auto res = DecodeDataRuns(attrListAttr, dataRuns);
    if (res != TErrorCode::Success) // DataRunsDecode writes a message into log file in case of an error
        return res;

    THArray<MFTRecIndex> visitedMFTRec;
    // this is do not not parse current indexMFTRec again when reading attrEntry->ref MFT records   
    // because attrs located in current MFT rec either already parsed or will be parsed during usual cycle of parsing 
    visitedMFTRec.AddValue(indexMFTRec);

    uint32_t currRun = 0;

    // "global" (outside of outer loop) counter of processed attributes in ATTR_LIST
    // sometimes data run list contains 2 data runs, but number of attributes is limited by RealSize value 
    // and may be limited by first data run only
    // second data run is "officially" present, but is not parsed because of RealSize
    uint64_t processedAttrSize = 0;

    while (currRun < dataRuns.Count())
    {
        DATA_RUN_ITEM& rli = dataRuns[currRun];
        logger.DebugFmt("[ParseNonresAttrList] Data Run Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

        auto dataBufSize = rli.len * getVolData().BytesPerCluster;
        uint8_t* dataBuf = (uint8_t*)alloca(dataBufSize);//TODO this is not good to allocate memory several times in a loop

        res = FLoader.ReadClusters(rli.lcn, rli.len, dataBuf);
        if (res != TErrorCode::Success) // ReadClusters writes a message into log file in case of an error
            return res;
      
        ATTR_LIST_ENTRY* attrEntry = (ATTR_LIST_ENTRY*)dataBuf;

        //TODO probably we need to parse each cluster separately because end of last attrEntry in cluster#1 does not mean start of first attrEntry in cluster#2

        uint8_t* attrEntryEnd = dataBuf + dataBufSize;

        res = ParseAttrList(indexMFTRec, attrFilter, attrEntry, attrEntryEnd, attrListAttr->nonres.RealSize, processedAttrSize, visitedMFTRec, processChildMFTRecPred);
        if (res != TErrorCode::Success)
        {
            logger.Error("ParseAttrList returned error.");
            return res;
        }

        if (processedAttrSize >= attrListAttr->nonres.RealSize) // its important to have this condition here too
            break;

        currRun++;
    }

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::ParseNonresBitmap(MFT_ATTR_HEADER* attr, TBitField& bitmap)
{
    assert(attr->NonResidentFlag == ATTR_FLAG_NONRESIDENT);
    assert((attr->nonres.RealSize & 0x07) == 0);
    assert((attr->nonres.RealSize >> 3) > 0);
    assert(bitmap.Count() == 0);

    TDataRuns dataRuns;
    auto res = DecodeDataRuns(attr, dataRuns);
    if (res != TErrorCode::Success) // DataRunsDecode writes a message into log file in case of an error
        return res ;

    assert(dataRuns.Count() > 0);

    uint8_t* dataBuf = nullptr;
    uint64_t dataBufLen = 0;  // dataBuf buffer size in clusters, how many clusters is allocated in dataBuf
    uint64_t rliSize = 0; // size in bytes of the current Data Run
    uint32_t currRun = 0;

    if (dataRuns.Count() > 1)
    {
        GET_LOGGER;
        logger.InfoFmt("[ParseNonresBitmap] Bitmap occupies more than one data run. Bitmap Data Runs Count: {}", dataRuns.Count());
    }

    uint64_t processedSize = 0;

    while (currRun < dataRuns.Count())
    {
        DATA_RUN_ITEM& rli = dataRuns[currRun];
        assert(rli.len > 0);

        rliSize = rli.len * getVolData().BytesPerCluster;
        assert(rliSize > 0);
        assert((rliSize & 0x07) == 0); // bitmap data size always multiple of 8
        assert((rliSize >> 3) > 0);

        if (rli.len > dataBufLen)
        {
            delete[] dataBuf;
            dataBuf = DBG_NEW uint8_t[rliSize];
            dataBufLen = rli.len;
        }

        assert(dataBuf);
        res = FLoader.ReadClusters(rli.lcn, rli.len, dataBuf);
        if (res != TErrorCode::Success) // ReadCluster writes error meesage to log file in case of an error
        {
            delete[] dataBuf;
            return res;
        }

        processedSize += rliSize;
        if (processedSize >= attr->nonres.RealSize)
        {
            auto delta = attr->nonres.RealSize - (processedSize - rliSize);
            
            assert(delta > 0);
            assert((delta >> 3) > 0);
            assert((delta & 0x07) == 0);

            bitmap.AddData((uint64_t*)dataBuf, (uint32_t)(delta >> 3));
            break; // last piece of bitmap added
        }
        else
        {
            bitmap.AddData((uint64_t*)dataBuf, (uint32_t)(rliSize >> 3));
        }

        currRun++;
    }

    if (dataRuns.Count() - currRun > 1)
    {
        GET_LOGGER;
        logger.InfoFmt("[ParseNonresBitmap] Stopped processing by reaching RealSize, but {} unprocessed Data Runs left.", dataRuns.Count() - currRun);
    }
    else if (dataRuns.Count() - currRun == 0)
    {
        GET_LOGGER;
        logger.WarnFmt("[ParseNonresBitmap] AllData Runs are processed, but RealSize has not beed reached. Data Runs Count: ", dataRuns.Count());
    }

    delete[] dataBuf;

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::ParseBitmap(MFT_ATTR_HEADER* attr, TBitField& bitmap)
{
    if (!attr) return TErrorCode::Success; // attr==nullptr means that no LCNs need to be parsed, usually Bitmap is null for empty directories

    assert(bitmap.Count() == 0);

    if (attr->NonResidentFlag == ATTR_FLAG_RESIDENT)
    {
        assert((attr->res.DataSize & 0x07) == 0); // bitmap data size always multiple of 8
        assert((attr->res.DataSize >> 3) > 0);

        ATTR_BITMAP_ATTR* bmp = (ATTR_BITMAP_ATTR*)Add2Ptr(attr, attr->res.DataOffset);
        bitmap.SetData((uint64_t*)bmp->bitmap, attr->res.DataSize >> 3);
        return TErrorCode::Success;
    }
    else
    {
        return ParseNonresBitmap(attr, bitmap);
    }
}

void TMFTBaseReader::ParseIndexRoot(MFT_ATTR_HEADER* attr, TLCNRecs& lcns, TFileList& fileList)
{
    GET_LOGGER;

    assert(fileList.Count() == 0);
    assert(attr->NonResidentFlag == ATTR_FLAG_RESIDENT); // always resident

    ATTR_INDEX_ROOT* indexR = (ATTR_INDEX_ROOT*)Add2Ptr(attr, attr->res.DataOffset);
    auto pihdr = &(indexR->ihdr);

    assert(indexR->IndexBlockSize >= getVolData().BytesPerCluster); // AI told that this rule should always be true
    assert((indexR->IndexBlockSize % getVolData().BytesPerCluster) == 0); // AI told that this rule should always be true
    assert(indexR->IndexBlockClst == 1);
    assert(indexR->AttrType == ATTR_FILENAME);
    assert(indexR->Rule == COLLATION_RULE::FILENAME);

    if (logger.ShouldLog(LogEngine::llDebug))
    {
        logger.DebugFmt("IndexRoot Indexed Attr Type: {} {:#x}", AttrName(indexR->AttrType), (uint32_t)indexR->AttrType);
        logger.DebugFmt("IndexRoot Collation Rule: {} ({:#x})", CollRuleName((uint32_t)indexR->Rule), (uint32_t)indexR->Rule);
        logger.DebugFmt("IndexRoot Dir Type: {} ({:#x})", indexR->ihdr.Flags == 0 ? "SMALL DIR" : "BIG DIR", indexR->ihdr.Flags);
        logger.DebugFmt("IndexRoot IndexBlockSize: {}", indexR->IndexBlockSize);
        logger.DebugFmt("IndexRoot IndexBlockClst: {}", indexR->IndexBlockClst);
        logger.DebugFmt("IHDR Used Bytes: {}", pihdr->Used);
    }

    GetFileListFromNode(pihdr, lcns, fileList);
}

/** 
* @brief Reads three required attributes from mftRec (INDEX_ROOT, ALLOC and BITMAP)
* and then loads list of files from them in SORTED order starting from IndexRoot, goes to subnodes when needed
* @details Reads into memory all clusters defined by ALLOC attr and calls GetFileListFromNode() for reading list of files.
* mftRec record must be a directory type
* @param mftRec pointer to MFT record buffer of directory type
* @param node parameter is for returning back list of files only (in node.Filelist field).
* @return TErrorCode code. List of loaded files stored in node.FileList
*/
TErrorCode TMFTBaseReader::GetFileListFromMFTRec(MFT_FILE_RECORD* mftRec, TFileList& fileList)
{
    GET_LOGGER;

    assert(fileList.Count() == 0);

    if (mftRec->Flags != (MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY))
    {
        logger.Error("[GetFileListFromMFTRec] Error: mftRec->Flags != MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY !");
        return TErrorCode::InvalidArgument; // error
    }
    assert(mftRec->Flags == (MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY)); // only "directory" record should go here

    TAttrCollection collection;
    auto res = FillAttrCollection(mftRec, MakeAttrBitmask(ATTR_BITMAP) | MakeAttrBitmask(ATTR_ALLOC) | MakeAttrBitmask(ATTR_ROOT), collection);
    if (res != TErrorCode::Success)
    {
        logger.Error("[GetFileListFromMFTRec] FillAttrCollection finished with error!");
        return res; // error
    }

    return GetFileListFromMFTRec(collection, fileList);

}

TErrorCode TMFTBaseReader::GetFileListFromMFTRec(TAttrCollection& collection, TFileList& fileList)
{
    GET_LOGGER;
    TErrorCode res;
    DIR_NODE node;

    auto& abmp = collection.Get(ATTR_BITMAP);

    // bitmap may be missing, its ok
    if (abmp.Count() > 0)
    {
        res = ParseBitmap(abmp[0], node.Bitmap);
        if (res != TErrorCode::Success) // copy bitmap into TBitField class for easier access
        {
            logger.Error("[GetFileListFromMFTRec] ParseBitmap finished with error.");
            return res;
        }
    }

    auto& aalloc = collection.Get(ATTR_ALLOC);

    if (aalloc.Count() > 0)
    {
        if(aalloc.Count() > 1)
            logger.InfoFmt("[GetFileListFromMFTRec] MFT record contains {} {} attributes." /*MFT Rec ID : {}"*/, 
                aalloc.Count(), AttrName(ATTR_ALLOC)/*, MFT_REF::toHexString(mftRec->IndexMFTRec)*/);

        auto alloc = aalloc[0];
        assert(alloc->NonResidentFlag == ATTR_FLAG_NONRESIDENT);

        res = DecodeDataRuns(alloc, node.DataRuns);
        if (res != TErrorCode::Success)
        {
            logger.Error("[GetFileListFromMFTRec] DecodeDataRuns for ATTR_ALLOC finished with error.");
            return res; // fail to decode data runs is a critical error, return immediately with error
        }

        //ParseAlloc(alloc, node.DataRuns);
        assert(node.DataRuns.Count() > 0);
    }

    auto& aroot = collection.Get(ATTR_ROOT);
    assert(aroot.Count() > 0);
    auto root = aroot[0];
    assert(root);
    assert(root->NonResidentFlag == ATTR_FLAG_RESIDENT);
    

    ATTR_INDEX_ROOT* indexR = (ATTR_INDEX_ROOT*)Add2Ptr(root, root->res.DataOffset);
    
    assert(indexR->AttrType == ATTR_FILENAME);
    assert(indexR->Rule == COLLATION_RULE::FILENAME);
    assert(indexR->ihdr.Flags < 2); // 0 - Small Dir, 1- Big Dir

    node.IndexBlockSize = indexR->IndexBlockSize; // need IndexBlockSize for proper parsing ALLOC data runs.
    
    assert(node.IndexBlockSize >= getVolData().BytesPerCluster);
    assert((node.IndexBlockSize % getVolData().BytesPerCluster) == 0);
    assert(indexR->IndexBlockClst == indexR->IndexBlockSize / getVolData().BytesPerCluster);

    // we work with ALLOC here that is why we use Index Blocks instead of LCNs. Index Blocks may have different size than LCNs
    uint64_t iblocksTotalCount = 0;
    //uint32_t k = node.IndexBlockSize / getVolData().BytesPerCluster;
    //assert(k > 0);

    for (auto& run : node.DataRuns) 
    {
        assert(((run.len * getVolData().BytesPerCluster) % node.IndexBlockSize) == 0);
        iblocksTotalCount += run.len * getVolData().BytesPerCluster / node.IndexBlockSize;
    }
    TLCNRecs lcns(node.IndexBlockSize, (uint32_t)iblocksTotalCount);

    assert(lcns.Count() == 0);
    res = ProcessAllocDataRuns(node, [&lcns](uint8_t* dataBuf, uint64_t VCN, uint64_t LCN) { lcns.AddRec(dataBuf, VCN, LCN); });
    if (res != TErrorCode::Success)
    {
        logger.Error("[GetFileListFromMFTRec] ProcessAllocDataRuns finished with error.");
        return res; // fail to process data runs this is critical error, return immediately with error
    }

    if (node.DataRuns.Count() > 0) assert(lcns.Count() > 0); 
   
    ParseIndexRoot(root, lcns, fileList);

    return TErrorCode::Success;
}

//parses either resident or non-resident ATTR_LIST
TErrorCode TMFTBaseReader::ParseAttrList(MFTRecIndex indexMFTRec, ATTR_LIST_ENTRY* startListItem, uint8_t* attrListEnd, uint64_t realSize, 
                                         uint64_t& processedAttrSize, THArray<MFTRecIndex> visitedMFTRec, AttrListPred processChildMFTRecPred)
{
    return ParseAttrList(indexMFTRec, ALL_ATTRS_FILTER, startListItem, attrListEnd, realSize, processedAttrSize, visitedMFTRec, processChildMFTRecPred);
}

// Parses both resident or non-resident ATTR_LISTs
// Gets only attributes specified by attrFilter parameter (bitwise mask)
TErrorCode TMFTBaseReader::ParseAttrList(MFTRecIndex indexMFTRec, uint32_t attrFilter, ATTR_LIST_ENTRY* startListItem, uint8_t* attrListEnd, uint64_t realSize, 
                                         uint64_t& processedAttrSize, THArray<MFTRecIndex> visitedMFTRec, AttrListPred processChildMFTRecPred)
{
    GET_LOGGER;

    ATTR_LIST_ENTRY* attrEntry = startListItem;

    assert(attrEntry->AttrSize > 0);
    assert(attrEntry->AttrType > 0);
    // assert(attrEntry->StartVCN == 0);
    assert(((uint32_t)(attrEntry->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
    assert(attrEntry->AttrType != ATTR_ZERO);
    assert(attrEntry->AttrType != ATTR_END);

    //THArray<uint32_t> visitedMFTRec;

    // this is do not not parse current indexMFTRec again when reading attrEntry->ref MFT record   
    // because attrs located in current MFT rec either already parsed or will be parsed during usual cycle of parsing 
    //visitedMFTRec.AddValue(indexMFTRec);

    while (true)
    {
        if (MakeAttrBitmask(attrEntry->AttrType) & attrFilter)
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

            if (visitedMFTRec.IndexOf(attrEntry->RecRef.sId.low) == -1) // whether we haven't parsed this MFT record yet
            {
                auto res = processChildMFTRecPred(attrEntry->RecRef);
                if (res != TErrorCode::Success)
                    return res;

                visitedMFTRec.AddValue(attrEntry->RecRef.sId.low);
            }

            // StartVCN is a cluster where attribute portion value is located
            if (attrEntry->StartVCN != 0)
            {
                assert((attrEntry->AttrType == ATTR_DATA) || (attrEntry->AttrType == ATTR_ALLOC));
                if (attrEntry->AttrType != ATTR_DATA)
                    logger.WarnFmt("One attribute does not fit into one MFT record. StartVCN: {}, AttrType: {}, RecRef: {}, MFT Rec ID: {}",
                        attrEntry->StartVCN, AttrName(attrEntry->AttrType), attrEntry->RecRef.toHexString(), MFT_REF::toHexString(indexMFTRec));
            }
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
            logger.InfoFmt("Loop is finished by condition: 'attrEntry >= attrListEnd' (end of buffer with clusters). RealSize: {}, processedAttrSize: {}",
                 realSize, processedAttrSize);
            break;
        }

        assert(attrEntry->AttrType > 0);
        assert(attrEntry->AttrSize > 0);
        assert(((uint32_t)(attrEntry->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
        assert(attrEntry->AttrType != ATTR_ZERO);
        assert(attrEntry->AttrType != ATTR_END);
    }

    return TErrorCode::Success;
}


/**
* @brief Returns MFT Record ID (low part of it) for specified path string
* @details Goes through path sub-dirs in path, reads files in SORTED order, goes to subdirs and so on, until end of path reached.
* Returns MFT Record ID (low part of it). If path is incorrect function returns 0 (zero).
* Uses ci_string intentionally to proper case insensitive folders compare.
* @param VolData Volume data. Needed for reading file system.
* @param path Fully qualified and ABSOLUTE path to file or folder that starts from disk name.
*/
expected_uint32 TMFTBaseReader::MFTRecIdByPath(const ci_string& path) // ci_string is for case INsensitive search here
{
    if (path.size() == 0) return std::unexpected(TErrorCode::InvalidArgument);

    // make sure that volData.hVolume and volume in path parameter are the same (both C: or both D:, etc)
    if(toupper(path[0]) != toupper(getVolData().Name[0])) return std::unexpected(TErrorCode::InvalidArgument);

    std::vector<ci_string> arr;
    StringToArray(path, arr, ci_string(_T("\\/")));
    
    if (arr.size() == 0) 
        return std::unexpected(TErrorCode::InvalidArgument);

    MFT_REF mftRecID{ 0 };
    mftRecID.sId.low = MFT_ROOT_REC_ID;
    uint8_t* mftRecBuf = (uint8_t*)alloca(getVolData().BytesPerMFTRec);

    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;

    for (size_t i = 1; i < arr.size(); i++) // bypass drive letter for now
    {
        auto res = FLoader.LoadMFTRecord(mftRecID, mftRecBuf);
        if (res != TErrorCode::Success)
        {
            GET_LOGGER;
            logger.Error("LoadMFTRecord finished with error.");
            return std::unexpected(res);
        }

        //node.Clear();
        TFileList fileList;
        res = GetFileListFromMFTRec(mftRec, fileList);
        if (res != TErrorCode::Success)
        {
            GET_LOGGER;
            logger.Error("GetFileListFromMFTRec finished with error.");
            return std::unexpected(res);
        }

        IFILE_NAME fn;
        fn.ciName = arr[i];
        // binary search in sorted array (assume that GetFileListFromMFTRec has read items in SORTED order)
        auto iter = std::lower_bound(fileList.begin(), fileList.end(), fn); // fn has overrided operators < and ==
        if (iter != fileList.end() && (*iter).ciName == fn.ciName)
        {
            mftRecID = iter->MFTRecID;
        }
        else
        {
            return std::unexpected(TErrorCode::NotFound);
            //mftRecID.Id = 0; // signal that file not found
            //break; // file not found 
        }
    }

    return mftRecID.sId.low;
}

TErrorCode TMFTBaseReader::PrintMFTRecord(MFT_REF mftRecRef)
{
    uint8_t* mftRecBuf = (uint8_t*)alloca(getVolData().BytesPerMFTRec);

    // LoadMFTRecord check that returned mftRecBuf contains requested MFT record ID
    // If non-existing record is requested then error code MFTRecordNotInUse returned
    auto res = FLoader.LoadMFTRecord(mftRecRef, mftRecBuf);
    if (res != TErrorCode::Success)
    {
        GET_LOGGER;
        logger.Error("LoadMFTRecord finished with error.");
        return res;
    }
    else
    {
        return PrintMFTRecord((MFT_FILE_RECORD*)mftRecBuf);
    }
}

TErrorCode TMFTBaseReader::PrintMFTRecord(MFT_FILE_RECORD* mftRec)
{
    TAttrCollection collection;
    CH_ERR(FillAttrCollection(mftRec, collection));

    CH_ERR(PrintMFTHeader(mftRec));

    assert(FAttrCurrIndex == 0);
    FAttrCurrIndex = 1;

    // attributes are printed in order of their ATTR_TYPE enum
    CH_ERR(PrintSTDInfo(collection));

    CH_ERR(PrintFileNames(collection));

    CH_ERR(PrintObjectID(collection));

    CH_ERR(PrintSecure(collection));

    CH_ERR(PrintLabel(collection));

    CH_ERR(PrintVolumeInfo(collection));

    CH_ERR(PrintDataInfo(collection));

    CH_ERR(PrintReparse(collection));

    if (((mftRec->Flags & MFT_FLAG_IS_DIRECTORY) == MFT_FLAG_IS_DIRECTORY))
    {
        CH_ERR(PrintDirectory(collection));
    }

    CH_ERR(PrintEAInfo(collection));

    CH_ERR(PrintEA(collection));

    CH_ERR(PrintLUS(collection));

    CH_ERR(PrintMFTFooter(mftRec));

    FAttrCurrIndex = 0; // reset index for future use

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::PrintMFTHeader(MFT_FILE_RECORD* mftRec)
{
    //fixup array must go right after IndexMFTRec
    assert(offsetof(MFT_FILE_RECORD, IndexMFTRec) + sizeof(mftRec->IndexMFTRec) == mftRec->RecHeader.FixupOffset);
    assert(ntfs_is_file_recp(mftRec->RecHeader.Signature));
    assert(mftRec->FileRecSize > mftRec->FirstAttrOffset);
    assert(mftRec->FileRecSize <= mftRec->AllocFileRecSize);
    assert(mftRec->AllocFileRecSize == getVolData().BytesPerMFTRec);
    assert((mftRec->Flags & MFT_FLAG_IN_USE) > 0);
    assert(mftRec->FirstAttrOffset < mftRec->AllocFileRecSize);

    string_t str = std::format(_T("MFT Record ID: {0:#x} (#{0})"), mftRec->IndexMFTRec);
    size_t spacesL = (MFT_LINE_LEN - str.size()) / 2;
    size_t spacesR = MFT_LINE_LEN - str.size() - spacesL;
    string_t line = _T("+");
    line.append(MFT_LINE_LEN - 2, '-');
    line.append(1, '+');

    // 4 lines below should be printed into FOut, not into Out()
    FOut << std::endl << line << std::endl;
    FOut << std::format(_T("{:<{}}{}{:>{}}"), _T("|"), spacesL, str, _T("|"), spacesR) << std::endl;
    FOut << line << std::endl;
    FOut << std::endl;

    Out() << std::format(_T("{:<{}}:'{}'"), _T("MFT Rec Signature"), F_WIDTH, convert_string<char_t>(std::string((char*)mftRec->RecHeader.Signature, 4))) << std::endl;

    switch (mftRec->Flags)
    {
    case MFT_FLAG_IN_USE: Out() << std::left << std::setw(F_WIDTH) << _T("MFT Rec Type") << _T(":'IN USE'") << std::endl; break;
    case MFT_FLAG_IS_DIRECTORY: Out() << std::left << std::setw(F_WIDTH) << _T("(!) MFT Rec Type") << _T(": DELETED Directory - unusual case") << std::endl; break;
    case MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY: Out() << std::format(_T("{:<{}}:{} {:#x}"), _T("MFT Rec Type"), F_WIDTH, _T("'IN USE DIRECTORY'"), (uint16_t)mftRec->Flags) << std::endl; break;
    default:
        Out() << std::format(_T("{:<{}}: {} {:#x}"), _T("MFT Rec Type"), F_WIDTH, _T("UNKNOWN"), (uint16_t)mftRec->Flags) << std::endl;
    }

    Out() << std::left << std::setw(F_WIDTH) << _T("MFT Rec ID") << _T(": ") << convert_string<char_t>(MFT_REF::toHexString(mftRec->IndexMFTRec)) << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("MFT Rec Sequence") << _T(": ") << mftRec->SeqNum << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("MFT Log Seq Number") << _T(": ") << mftRec->RecHeader.LogFileSeqNum << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("MFT Parent Rec ID") << _T(": ") << convert_string<char_t>(mftRec->ParentFileRec.toHexString()) << (mftRec->ParentFileRec.Id > 0 ? _T(" CHILD") : _T(" BASE")) << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("MFT Hard Links Count") << _T(": ") << mftRec->HardLinksCnt << (mftRec->ParentFileRec.Id > 0 ? " (may be inaccurate for child records)" : "") << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("MFT Next Attr ID") << _T(": ") << mftRec->NextAttrID << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("MFT Rec Size") << _T(": ") << mftRec->FileRecSize << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("MFT Allocated Size") << _T(": ") << mftRec->AllocFileRecSize << std::endl;

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::PrintMFTFooter(MFT_FILE_RECORD* mftRec)
{
    string_t str = std::format(_T("END of MFT Record ID: {0:#x} (#{0})"), mftRec->IndexMFTRec);
    size_t spacesL = (MFT_LINE_LEN - str.size()) / 2;
    size_t spacesR = MFT_LINE_LEN - str.size() - spacesL;
    string_t line = _T("+");
    line.append(MFT_LINE_LEN - 2, '-');
    line.append(1, '+');

    // 4 lines below should be printed into FOut, not into Out()
    FOut << std::endl << line << std::endl;
    FOut << std::format(_T("{:<{}}{}{:>{}}"), _T("|"), spacesL, str, _T("|"), spacesR) << std::endl;
    FOut << line << std::endl;
    FOut << std::endl;

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::PrintAttrHeader(MFT_ATTR_HEADER* attr, MFTRecIndex mftRecID)
{
    assert(attr->AttrSize > 0);

    string_t str1 = std::format(_T(" Attribute {} ({:#x}) "), convert_string<char_t>(AttrName(attr->AttrType)), (uint32_t)attr->AttrType);
    string_t str2 = std::format(_T("#{} "), FAttrCurrIndex);
    size_t spacesL = (ATTR_LINE_LEN - str1.size() - str2.size()) / 2;
    size_t spacesR = ATTR_LINE_LEN - str1.size() - str2.size() - spacesL;
    string_t strL(spacesL, '-');
    string_t strR(spacesR, '-');

    Out() << std::endl;
    Out() << str2 << strL << str1 << strR << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("Attr location:") << convert_string<char_t>(MFT_REF::toHexString(mftRecID)) << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("Residence:") << (attr->NonResidentFlag == ATTR_FLAG_NONRESIDENT ? _T("NON - RESIDENT") : _T("RESIDENT")) << std::endl;
    Out() << std::format(_T("{:<{}}{} {:#x}"), _T("Type:"), F_WIDTH, convert_string<char_t>(AttrName(attr->AttrType)), (uint32_t)attr->AttrType) << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("Attr ID:") << attr->AttrID << std::endl;

    std::wstring nameOfAttrW = STREAM_NONAME_W;
    if (attr->AttrNameSize > 0) // if attr has a name - show it
    {
        nameOfAttrW.assign(GetAttrName(attr, AttrNameOffset), attr->AttrNameSize);
        Out() << std::format(_T("{:<{}}'{}'"), _T("Attr Name:"), F_WIDTH, convert_string<char_t>(nameOfAttrW)) << std::endl;
    }

    //Out() << std::left << std::setw(F_WIDTH) << _T("Attr Size:")  << attr->AttrSize << std::endl;
    Out() << std::format(_T("{:<{}}{} {}"), _T("Flags:"), F_WIDTH, attr->Flags, convert_string<char_t>(AttrFlagToString(attr->Flags))) << std::endl;

    if (attr->NonResidentFlag == ATTR_FLAG_RESIDENT) // attribute is RESident
    {
        Out() << std::left << std::setw(F_WIDTH) << _T("Indexed:") << attr->res.IndexedFlag << std::endl;
        assert(attr->res.DataSize + attr->res.DataOffset <= attr->AttrSize);
    }
    else
    {
        Out() << std::left << std::setw(F_WIDTH) << _T("StartVCN:") << toStringSep<string_t>(attr->nonres.StartVCN) << std::endl;
        Out() << std::left << std::setw(F_WIDTH) << _T("LastVCN:") << toStringSep(attr->nonres.LastVCN) << std::endl;
        Out() << std::left << std::setw(F_WIDTH) << _T("RealSize:") << toStringSep(attr->nonres.RealSize) << _T(" bytes") << std::endl;
        Out() << std::left << std::setw(F_WIDTH) << _T("StreamSize:") << toStringSep(attr->nonres.StreamSize) << _T(" bytes") << std::endl;
        Out() << std::left << std::setw(F_WIDTH) << _T("Allocated Size:") << toStringSep(attr->nonres.AllocatedSize) << _T(" bytes") << std::endl;

        if (attr->nonres.CompressionUnitSize > 0) // for compressed files
        {
            Out() << std::left << std::setw(F_WIDTH) << _T("Compressed Unit Size:") << toStringSep(2 << attr->nonres.CompressionUnitSize) << _T(" clusters") << std::endl;
            Out() << std::left << std::setw(F_WIDTH) << _T("Compressed Size:") << toStringSep(attr->nonres.CompressedSize) << _T(" bytes (multiple of the cluster size)") << std::endl;
        }
    }

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::PrintSTDInfo(TAttrCollection& collection)
{
    auto& attrList = collection.Get(ATTR_STD_INFO);

    if (SingleAttributes[MATI(ATTR_STD_INFO)]) assert(attrList.Count() < 2);
    // STD_INFO always present in MFT record
    assert(attrList.Count() == 1);

    if (attrList.Count() != 1) 
        return TErrorCode::CorruptedData;

    if (SingleAttributes[MATI(ATTR_STD_INFO)] && attrList.Count() > 1)
        return TErrorCode::CorruptedData;

    auto attr = attrList[0];
    assert(attr->AttrType == ATTR_STD_INFO);

    auto stdInfo = (ATTR_STD_INFO5*)Add2Ptr(attr, attr->res.DataOffset);
    assert((stdInfo->FileAttrib & FILE_ATTRIBUTE_NORMAL) == 0);// check that NORMAL bit is always zero

    CH_ERR(PrintAttrHeader(attr, collection.GetLoc(attr)));

    Out() << std::format(_T("{:<{}}{}"), _T("Created:"), F_WIDTH, FileDateToString(stdInfo->CreateTime)) << std::endl;
    Out() << std::format(_T("{:<{}}{}"), _T("Modified:"), F_WIDTH, FileDateToString(stdInfo->ModifyTime)) << std::endl;
    Out() << std::format(_T("{:<{}}{}"), _T("MFT Changed:"), F_WIDTH, FileDateToString(stdInfo->ModifyAttrTime)) << std::endl;
    Out() << std::format(_T("{:<{}}{}"), _T("Last Access:"), F_WIDTH, FileDateToString(stdInfo->LastAccessTime)) << std::endl;

    Out() << std::format(_T("{:<{}}{:#x} {}"), _T("DOS Attrib:"), F_WIDTH, stdInfo->FileAttrib, convert_string<char_t>(FormatFileAttributes(stdInfo->FileAttrib))) << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("Version Number:") << stdInfo->VersionNum << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("Max Version num:") << stdInfo->max_ver_num << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("Class Id:") << stdInfo->class_id << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("Owner Id:") << stdInfo->owner_id << std::endl;
    Out() << std::format(_T("{:<{}}{:#x}"), _T("USN:"), F_WIDTH, stdInfo->usn) << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("Security ID:") << stdInfo->security_id << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("Quota Charged:") << stdInfo->quota_charged << std::endl;

    FAttrCurrIndex++;

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::PrintFileNames(TAttrCollection& collection)
{
    auto& attrList = collection.Get(ATTR_FILENAME);

    if (SingleAttributes[MATI(ATTR_FILENAME)]) assert(attrList.Count() < 2);
    assert(attrList.Count() > 0);
    // FILENAME attr is always present, at least one filename
    if ((SingleAttributes[MATI(ATTR_FILENAME)] && attrList.Count() > 1) || attrList.Count() == 0)
        return TErrorCode::CorruptedData;

    for (auto& attr : attrList)
    {
        assert(attr->AttrType == ATTR_FILENAME);

        auto fname = (ATTR_FILE_NAME*)Add2Ptr(attr, attr->res.DataOffset);
        std::wstring name(GetFName(fname), fname->FileNameLen);
        
        assert(fname->NameType <= FILE_NAME_UNICODE_AND_DOS);
        assert((fname->dup.FileAttrib & FILE_ATTRIBUTE_NORMAL) == 0);// check that NORMAL bit is always zero

        CH_ERR(PrintAttrHeader(attr, collection.GetLoc(attr)));
        FAttrCurrIndex++;
        Out() << std::format(_T("{:<{}}'{}'"), _T("File Name:"), F_WIDTH, convert_string<char_t>(name)) << std::endl;
        Out() << std::format(_T("{:<{}}'{}' {:#x}"), _T("Name Type:"), F_WIDTH, convert_string< char_t>(FileNameTypes[fname->NameType]), fname->NameType) << std::endl;
        Out() << std::format(_T("{:<{}}{:#x} {}"), _T("DOS Attrib:"), F_WIDTH, fname->dup.FileAttrib, convert_string<char_t>(FormatFileAttributes(fname->dup.FileAttrib))) << std::endl;
        Out() << std::left << std::setw(F_WIDTH) << _T("Parent Rec ID:") << convert_string<char_t>(fname->ParentDir.toHexString()) << std::endl;
        Out() << std::format(_T("{:<{}}{}"), _T("Created:"), F_WIDTH, FileDateToString(fname->dup.CreateTime)) << std::endl;
        Out() << std::format(_T("{:<{}}{}"), _T("Modified:"), F_WIDTH, FileDateToString(fname->dup.ModifyTime)) << std::endl;
        Out() << std::format(_T("{:<{}}{}"), _T("MFT Changed:"), F_WIDTH, FileDateToString(fname->dup.ModifyAttrTime)) << std::endl;
        Out() << std::format(_T("{:<{}}{}"), _T("Last Access:"), F_WIDTH, FileDateToString(fname->dup.LastAccessTime)) << std::endl;

        //FileSize in MFT record contains 0 while FileSize in directory index contain read file size. This is done for optimization purposes.
        //Out() << std::left << std::setw(F_WIDTH) << _T("File Size:") << fname->dup.FileSize << std::endl;
    }

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::PrintDirectory(TAttrCollection& collection)
{ 
    string_t str = _T(" DIRECTORY FILE LIST ");
    size_t spacesL = (ATTR_LINE_LEN - str.size()) / 2;
    size_t spacesR = ATTR_LINE_LEN - str.size() - spacesL;
    string_t strL(spacesL, '-');
    string_t strR(spacesR, '-');

    Out() << std::endl;
    Out() << strL << str << strR << std::endl;

    TFileList fileList;
    CH_ERR(GetFileListFromMFTRec(collection, fileList));

    for (auto& item : fileList)
    {
        if(item.IsDir())
            Out() << _T("[") << item.ciName.c_str() << _T("]") << std::endl;
        else
            Out() << item.ciName.c_str() << std::endl;
    }

    str = _T(" END OF DIR FILE LIST ");
    spacesL = (ATTR_LINE_LEN - str.size()) / 2;
    spacesR = ATTR_LINE_LEN - str.size() - spacesL;
    strL.assign(spacesL, '-');
    strR.assign(spacesR, '-');

    Out() << strL << str << strR << std::endl;

    FAttrCurrIndex++;

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::PrintObjectID(TAttrCollection& collection)
{
    auto& attrList = collection.Get(ATTR_ID);
    if (SingleAttributes[MATI(ATTR_ID)]) assert(attrList.Count() < 2);

    // its Ok if no ID attribute in MFT record
    if (attrList.Count() == 0) 
        return TErrorCode::Success;

    if (SingleAttributes[MATI(ATTR_ID)] && attrList.Count() > 1)
        return TErrorCode::CorruptedData;

    auto attr = attrList[0];
    assert(attr->AttrType == ATTR_ID);
    auto objID = (ATTR_OBJECT_ID*)Add2Ptr(attr, attr->res.DataOffset);
    
    CH_ERR(PrintAttrHeader(attr, collection.GetLoc(attr)));

    constexpr const uint BUF_SZ = 100;
    std::wstring buf(BUF_SZ, 0);

    if (!StringFromGUID2(objID->ObjId, buf.data(), BUF_SZ))
        return TErrorCode::CorruptedData;
        //logger.Error("Error. ATTR_OBJECT_ID. StringFromGUID2 has failed 1.");

    Out() << std::left << std::setw(F_WIDTH) << _T("Object ID:") << convert_string<char_t>(buf) << std::endl;

    if (attr->AttrSize > 16) //0x10
    {
        if (!StringFromGUID2(objID->BirthVolumeId, buf.data(), BUF_SZ))
            return TErrorCode::CorruptedData;
            //logger.Error("Error. ATTR_OBJECT_ID. StringFromGUID2 has failed 2.");
        
        Out() << std::left << std::setw(F_WIDTH) << _T("Birth Volume ID:") << convert_string<char_t>(buf) << std::endl;

        if (attr->AttrSize > 32) //0x20
        {
            if (!StringFromGUID2(objID->BirthObjectId, buf.data(), BUF_SZ))
                return TErrorCode::CorruptedData;
                //logger.Error("Error. ATTR_OBJECT_ID. StringFromGUID2 has failed 3.");
            
            Out() << std::left << std::setw(F_WIDTH) << _T("Birth Object ID:") << convert_string<char_t>(buf) << std::endl;

            if (attr->AttrSize > 48) //0x30
            {
                if (!StringFromGUID2(objID->DomainId, buf.data(), BUF_SZ))
                    return TErrorCode::CorruptedData;
                    //logger.Error("Error. ATTR_OBJECT_ID. StringFromGUID2 has failed 4.");
                Out() << std::left << std::setw(F_WIDTH) << _T("Domain ID:") << convert_string<char_t>(buf) << std::endl;
            }
        }
    }

    FAttrCurrIndex++;

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::PrintLabel(TAttrCollection& collection)
{
    auto& attrList = collection.Get(ATTR_LABEL);
    if (SingleAttributes[MATI(ATTR_LABEL)]) assert(attrList.Count() < 2);

    if (attrList.Count() == 0) 
        return TErrorCode::Success;
    
    if (SingleAttributes[MATI(ATTR_LABEL)] && attrList.Count() > 1)
        return TErrorCode::CorruptedData;

    auto attr = attrList[0];
    assert(attr->AttrType == ATTR_LABEL);

    CH_ERR(PrintAttrHeader(attr, collection.GetLoc(attr)));

    std::wstring label((wchar_t*)Add2Ptr(attr, attr->res.DataOffset), attr->res.DataSize / sizeof(wchar_t));
    Out() << std::format(_T("{:<{}} '{}'"), _T("Label:"), F_WIDTH, convert_string<char_t>(label)) << std::endl;
    FAttrCurrIndex++;

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::PrintVolumeInfo(TAttrCollection& collection)
{
    auto& attrList = collection.Get(ATTR_VOL_INFO);
    if (SingleAttributes[MATI(ATTR_VOL_INFO)]) assert(attrList.Count() < 2);
    
    if (attrList.Count() == 0) 
        return TErrorCode::Success;

    if (SingleAttributes[MATI(ATTR_VOL_INFO)] && attrList.Count() > 1)
        return TErrorCode::CorruptedData;

    auto attr = attrList[0];
    assert(attr->AttrType == ATTR_VOL_INFO);
    
    CH_ERR(PrintAttrHeader(attr, collection.GetLoc(attr)));

    auto volInfo = (VOLUME_INFO*)Add2Ptr(attr, attr->res.DataOffset);
    
    Out() << std::left << std::setw(F_WIDTH) << _T("Volume Major Ver:") << volInfo->MajorVer << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("Volume Minor Ver:") << volInfo->MinorVer << std::endl;
    Out() << std::left << std::setw(F_WIDTH) << _T("Volume Flags:")     << volInfo->Flags    << std::endl;
    FAttrCurrIndex++;

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::PrintDataInfo(TAttrCollection& collection)
{
    auto& attrList = collection.Get(ATTR_DATA);
    if (SingleAttributes[MATI(ATTR_DATA)]) assert(attrList.Count() < 2);

    if (attrList.Count() == 0) 
        return TErrorCode::Success;
    
    if (SingleAttributes[MATI(ATTR_DATA)] && attrList.Count() > 1)
        return TErrorCode::CorruptedData;

    auto attr = attrList[0];
    assert(attr->AttrType == ATTR_DATA);

    CH_ERR(PrintAttrHeader(attr, collection.GetLoc(attr)));

    if (attr->NonResidentFlag == ATTR_FLAG_NONRESIDENT)
    {
        TDataRuns dataRuns;
        CH_ERR(DecodeDataRuns(attr, dataRuns));

        uint64_t totalClusters = 0;
        for (auto& rli : dataRuns) totalClusters += rli.len;

        Out() << std::left << std::setw(F_WIDTH) << _T("Data Runs Count:") << dataRuns.Count() << std::endl;
        Out() << std::left << std::setw(F_WIDTH) << _T("Total Clusters Used:") << totalClusters << std::endl;

        Out() << std::endl;
        Out() << _T("Data Runs List:") << std::endl;

        uint32_t clusterDigits = (uint32_t)(log10(totalClusters) + 1 + 1); // extra +1 for space after : (see below)
        uint32_t drDigits = (uint32_t)(log10(dataRuns.Count()) + 1);

        DATA_RUN_ITEM rli;
        for (uint32_t i = 0; i < dataRuns.Count();++i)
        {
            rli = dataRuns[i];
            Out() << std::format(_T("#{:<{}} | VCN:{:{}} | LCN:{:10} | Len:{:{}}"), i, drDigits, rli.vcn, clusterDigits, rli.lcn, rli.len, clusterDigits) << std::endl;
        }
    }
    else
    {
        Out() << std::left << std::setw(F_WIDTH) << _T("Data Size:") << attr->res.DataSize << std::endl;
    }

    FAttrCurrIndex++;

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::PrintEA(TAttrCollection& collection)
{
    auto& attrList = collection.Get(ATTR_EA);
    if (SingleAttributes[MATI(ATTR_EA)]) assert(attrList.Count() < 2);

    if (attrList.Count() == 0) 
        return TErrorCode::Success;

    if (SingleAttributes[MATI(ATTR_EA)] && attrList.Count() > 1)
        return TErrorCode::CorruptedData;

    auto attr = attrList[0];
    assert(attr->AttrType == ATTR_EA);

    CH_ERR(PrintAttrHeader(attr, collection.GetLoc(attr)));
    
    FAttrCurrIndex++;

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::PrintEAInfo(TAttrCollection& collection)
{
    auto& attrList = collection.Get(ATTR_EA_INFO);
    if (SingleAttributes[MATI(ATTR_EA_INFO)]) assert(attrList.Count() < 2);
    
    if (attrList.Count() == 0) 
        return TErrorCode::Success;
    
    if (SingleAttributes[MATI(ATTR_EA_INFO)] && attrList.Count() > 1)
        return TErrorCode::CorruptedData;

    auto attr = attrList[0];
    assert(attr->AttrType == ATTR_EA_INFO);

    CH_ERR(PrintAttrHeader(attr, collection.GetLoc(attr)));

    FAttrCurrIndex++;

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::PrintLUS(TAttrCollection& collection)
{
    auto & attrList = collection.Get(ATTR_LOGGED_UTILITY_STREAM);
    if (SingleAttributes[MATI(ATTR_LOGGED_UTILITY_STREAM)]) assert(attrList.Count() < 2);
    
    if (attrList.Count() == 0) 
        return TErrorCode::Success;
    
    if (SingleAttributes[MATI(ATTR_LOGGED_UTILITY_STREAM)] && attrList.Count() > 1)
        return TErrorCode::CorruptedData;

    for (auto& attr : attrList)
    {
        assert(attr->AttrType == ATTR_LOGGED_UTILITY_STREAM);

        CH_ERR(PrintAttrHeader(attr, collection.GetLoc(attr)));
    }

    FAttrCurrIndex++;

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::PrintSecure(TAttrCollection& collection)
{
    auto& attrList = collection.Get(ATTR_SECURE);
    if (SingleAttributes[MATI(ATTR_SECURE)]) assert(attrList.Count() < 2);
    
    if (attrList.Count() == 0) 
        return TErrorCode::Success;
    
    if (SingleAttributes[MATI(ATTR_SECURE)] && attrList.Count() > 1)
        return TErrorCode::CorruptedData;

    auto attr = attrList[0];
    assert(attr->AttrType == ATTR_SECURE);

    CH_ERR(PrintAttrHeader(attr, collection.GetLoc(attr)));

    FAttrCurrIndex++;

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::PrintReparse(TAttrCollection& collection)
{
    auto& attrList = collection.Get(ATTR_REPARSE);
    if (SingleAttributes[MATI(ATTR_REPARSE)]) assert(attrList.Count() < 2);
    
    if (attrList.Count() == 0) 
        return TErrorCode::Success;
    if (SingleAttributes[MATI(ATTR_REPARSE)] && attrList.Count() > 1)
        return TErrorCode::CorruptedData;

    auto attr = attrList[0];
    assert(attr->AttrType == ATTR_REPARSE);

    CH_ERR(PrintAttrHeader(attr, collection.GetLoc(attr)));

    auto rp = (ATTR_REPARSE_POINT*)Add2Ptr(attr, attr->res.DataOffset);

    if (attr->NonResidentFlag == ATTR_FLAG_RESIDENT)
    {
        //TODO show more fields here
        Out() << std::left << std::setw(F_WIDTH) << _T("Reparse Point Tag:") << std::hex << rp->ReparseTag << std::dec << std::endl;
        Out() << std::left << std::setw(F_WIDTH) << _T("Reparse Data Length:") << rp->ReparseDataLength << std::endl;
    }
    else
    {
        return TErrorCode::InvalidArgument;
    }

    FAttrCurrIndex++;

    return TErrorCode::Success;
}




