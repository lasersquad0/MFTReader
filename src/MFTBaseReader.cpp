
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
        CLST iblocksCount = rliBufSize / node.IndexBlockSize;

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
        
        if (logger.ShouldLog(LogEngine::Levels::llDebug))
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

            if (logger.ShouldLog(LogEngine::Levels::llDebug))
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
            CLST vcn = *(CLST*)Add2Ptr(ihdr, off + de->size - sizeof(uint64_t)); 

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
ATTR_FILE_NAME* TMFTBaseReader::GetFileNameAttr(MFT_FILE_RECORD* mftRec)
{
    // should be called for dir MFT rec only
    assert(mftRec->Flags == (MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY));

    TAttrCollection collection;
    TErrorCode res = FillAttrCollection(mftRec, MakeAttrBitmask(ATTR_FILENAME), collection);// fill collection only with ATTR_FILENAME attributes
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

    /*
    PMFT_ATTR_HEADER currAttr = (MFT_ATTR_HEADER*)Add2Ptr(mftRec, mftRec->FirstAttrOffset);
    assert(currAttr->res.DataSize + currAttr->res.DataOffset <= currAttr->AttrSize);
    
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
        assert(currAttr->AttrType != ATTR_LIST_ATTR);

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
        assert((attrFNames[0]->NameType == FILE_NAME_DOS) || (attrFNames[1]->NameType == FILE_NAME_DOS));
        assert(attrFNames[0]->ParentDir.sId.low == attrFNames[1]->ParentDir.sId.low);
    }
#endif

    return attrFNameNDOS;
}*/

// fills array attrFileNames with pointers to all ATTR_FILENAME attributes which mftRec contains except DOS ones 
// Goes inside of ATTR_LIST_ATTR attribute if MFT record has it
// attrFileNames is cleared each time before filling with new values
TErrorCode TMFTBaseReader::GetFileNameAttrPointers(MFT_FILE_RECORD* mftRec, THArray<ATTR_FILE_NAME*>& attrFileNames)
{
    attrFileNames.Clear();

    TAttrCollection collection;
    TErrorCode res = FillAttrCollection(mftRec, MakeAttrBitmask(ATTR_FILENAME), collection); // fill collection only with ATTR_FILENAME attributes
    assert(res == TErrorCode::Success);
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
        attrFName = GetFileNameAttr(mftRec);

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
        paths.AddValue(GetPathByAttrFileName(attrFName));
    }

    return TErrorCode::Success;
}

/*
TErrorCode TMFTBaseReader::FillAttrCollection(MFT_REF mftRecRef, TAttrCollection& collection)
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

        return FillAttrCollection(mftRec, collection);
    }
}*/

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
                collection.Set(currAttr);
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

                auto res = ParseAttrList(mftRec->IndexMFTRec, attrFilter, attrListItem, currAttrEnd, currAttr->res.DataSize, processedAttrSize, callProcessChildMFTRecsPred);
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


// fills array attrValues[] with pointers to all attributes which mftRec contains
// DOES NOT go inside ATTR_LIST_ATTR even if present (for optimization purposes)
// for ATTR_FILE_NAME and ATTR_LOGGED_UTILITY_STREAM only last such attribute placed into attrValues
// assumes that attrValues has allocated size for ATTR_TYPE_CNT items
/*void TMFTParserBase::FillAttrValues(MFT_FILE_RECORD* mftRec, PMFT_ATTR_HEADER* attrValues)
{
    PMFT_ATTR_HEADER currAttr = (MFT_ATTR_HEADER*)Add2Ptr(mftRec, mftRec->FirstAttrOffset);
    ZeroMemory(attrValues, ATTR_TYPE_CNT * sizeof(PMFT_ATTR_HEADER));

    do
    {
        if (currAttr->NonResidentFlag == ATTR_FLAG_RESIDENT)
            assert(currAttr->res.DataSize + currAttr->res.DataOffset <= currAttr->AttrSize);

        // all atributes except ATTR_FILENAME and ATTR_LOGGED_UTILITY_STREAM should be in a single copy in one MFT rec
        if ((currAttr->AttrType != ATTR_FILENAME) && (currAttr->AttrType != ATTR_LOGGED_UTILITY_STREAM))
            assert(attrValues[MATI(currAttr->AttrType)] == nullptr);

        attrValues[MATI(currAttr->AttrType)] = currAttr;

        assert(currAttr->AttrSize > 0);
        currAttr = (MFT_ATTR_HEADER*)Add2Ptr(currAttr, currAttr->AttrSize);
        assert(mftRec->FileRecSize > Diff2Ptr(mftRec, currAttr));

    } while (*((uint32_t*)currAttr) != ATTR_END);
}*/

// returns true if requested attribute found in ATTR_LIST otherwise returns false 
// for resident ATTR_LIST it is called from GetAttr()
// for non-resident ATTR_LIST it is called from ParseNonresAttrList()
/*bool TMFTParserBase::GetAttrFromAttrList(ATTR_LIST_ENTRY* startListItem, ATTR_TYPE attrType, uint8_t* attrListEnd1, uint8_t* attrListEnd2, PMFT_ATTR_HEADER* result)
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
}*/

// fills attr collection with all attrs from ATTR_LIST
/*bool TMFTParserBase::FillCollectionFromAttrList(MFTRecIndex indexMFTRec, uint32_t attrFilter, ATTR_LIST_ENTRY* startListItem, uint8_t* attrListEnd1, uint8_t* attrListEnd2, TAttrCollection& collection)
{
    GET_LOGGER;

    ATTR_LIST_ENTRY* attrListItem = startListItem;
    
    assert(attrListItem->AttrSize > 0);
    assert(attrListItem->AttrType > 0);
    assert(attrListItem->StartVCN == 0);
    assert(((uint32_t)(attrListItem->AttrType) & 0x0F) == 0); // Attr type minor byte is always zero
    assert(attrListItem->AttrType != ATTR_ZERO);
    assert(attrListItem->AttrType != ATTR_END);

    THArray<uint32_t> visitedMFTRec;
    // this is do not not parse current indexMFTRec again when reading attrListItem->ref MFT record   
    // because attrs located in current MFT rec either already parsed or will be parsed during usual cycle of parsing  
    visitedMFTRec.AddValue(indexMFTRec);

    while (true)
    {
        if (MakeAttrBitmask(attrListItem->AttrType) & attrFilter)
        {
            // StartVCN might be >0 when one attribute does not fit into one MFT record.
            // This attribute may have very long Data Run list or anything else
            // In this case ATTR_LIST contains several ATTR_LIST_ENTRY entries for this big attribute.
            // First entry has StartVCN=0, others - preventry.StartVCN+num_of_vcns_in_preventry_dataruns, etc.
            // all these entries build up a continious list of VCNs 
            if ((attrListItem->AttrType != ATTR_DATA) && (attrListItem->AttrType != ATTR_ALLOC))  // StartVCN should be 0 for all attrs except ATTR_DATA and ATTR_ALLOC
            {
                if (attrListItem->StartVCN != 0)
                    logger.WarnFmt("Looks like we have met incorrect case. StartVCN({}) <> 0 for {} attribute. MFT Rec ID: {}.",
                        attrListItem->StartVCN, AttrName(attrListItem->AttrType), MFT_REF::toHexString(indexMFTRec));
                assert(attrListItem->StartVCN == 0);
            }

            // attributes in non-resident attr list located in a separate LCN cluster may refer back to the base record
            // because some attributes may reside in base mft record and the others in "child" mft record(s)
            // the attr list attribute itself is located in LCN cluster that is not mft record, it does not contain signature or Fixups values, etc.
            if (visitedMFTRec.IndexOf(attrListItem->ref.sId.low) == -1) // whether we've parsed this MFT record already or not 
            {
                // we need cache version because mftRecBuf should remain valid till we return back to GetMFTRecIdByPath in calls stack
                uint8_t* mftRecBuf = FLoader.LoadMFTRecordCache(attrListItem->ref);
                if (mftRecBuf)
                {
                    FillAttrCollection((MFT_FILE_RECORD*)mftRecBuf, attrFilter, collection);
                }
                else
                {
                    // error loading MFT record
                    // do not break loop and trying to load more records
                    logger.Error("[GetAttrFromAttrList] LoadMFTRecordCache returned NULL MFT record!");
                    //return;
                }
                visitedMFTRec.AddValue(attrListItem->ref.sId.low);

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

    return true;
}*/

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
/*
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
}*/


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
        logger.DebugFmt("[ParseNonresAttrList] Data Run Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

        auto dataBufSize = rli.len * getVolData().BytesPerCluster;
        uint8_t* dataBuf = (uint8_t*)alloca(dataBufSize);//TODO this is not good to allocate memory several times in a loop

        res = FLoader.ReadClusters(rli.lcn, rli.len, dataBuf);
        if (res != TErrorCode::Success) // ReadClusters writes a message into log file in case of an error
            return res;
      
        ATTR_LIST_ENTRY* attrEntry = (ATTR_LIST_ENTRY*)dataBuf;

        //TODO probably we need to parse each cluster separately because end of last attrEntry in cluster#1 does not mean start of first attrEntry in cluster#2

        uint8_t* attrEntryEnd = dataBuf + dataBufSize;

        res = ParseAttrList(indexMFTRec, attrFilter, attrEntry, attrEntryEnd, attrListAttr->nonres.RealSize, processedAttrSize, processChildMFTRecPred);
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

    if (logger.ShouldLog(LogEngine::Levels::llDebug))
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
TErrorCode TMFTBaseReader::GetFileListFromMFTRec(MFT_FILE_RECORD* mftRec, DIR_NODE& node)
{
    GET_LOGGER;

    assert(node.Bitmap.Count() == 0);
    assert(node.DataRuns.Count() == 0);
    assert(node.FileList.Count() == 0);

    if (mftRec->Flags != (MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY))
    {
        logger.Error("[GetFileListFromMFTRec] Error: mftRec->Flags != MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY !");
        return TErrorCode::InvalidArgument; // error
    }
    assert(mftRec->Flags == (MFT_FLAG_IN_USE | MFT_FLAG_IS_DIRECTORY)); // only "directory" record should go here

    TAttrCollection collection;
    TErrorCode res = FillAttrCollection(mftRec, MakeAttrBitmask(ATTR_BITMAP) | MakeAttrBitmask(ATTR_ALLOC) | MakeAttrBitmask(ATTR_ROOT), collection);
    if(res != TErrorCode::Success)
    {
        logger.Error("[GetFileListFromMFTRec] FillAttrCollection finished with error!");
        return res; // error
    }

    auto& abmp = collection.Get(ATTR_BITMAP);

    // bitmap may be missing, its ok
    if (abmp.Count() > 0)
    {
        res = ParseBitmap(abmp[0], node.Bitmap);
        if (res != TErrorCode::Success) // copy bitmap into TBitField class for easier access
        {
            logger.Error("[GetFileListFromMFTRec] ParseBitmap finished with error.");
            return res; // error
        }
    }

    auto& aalloc = collection.Get(ATTR_ALLOC);

    if (aalloc.Count() > 0)
    {
        if(aalloc.Count() > 1)
            logger.InfoFmt("[GetFileListFromMFTRec] MFT record contains {} {} attributes. MFT Rec ID: {}", 
                aalloc.Count(), AttrName(ATTR_ALLOC), MFT_REF::toHexString(mftRec->IndexMFTRec));

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
    uint32_t k = node.IndexBlockSize / getVolData().BytesPerCluster;
    assert(k > 0);

    for (auto& run : node.DataRuns) 
    {
        assert((run.len % k) == 0);
        iblocksTotalCount += run.len/k;
    }
    TLCNRecs lcns(node.IndexBlockSize, (uint32_t)iblocksTotalCount);

    assert(lcns.Count() == 0);
    res = ProcessAllocDataRuns(node, [&lcns](uint8_t* dataBuf, CLST VCN, CLST LCN) { lcns.AddRec(dataBuf, VCN, LCN); });
    if (res != TErrorCode::Success)
    {
        logger.Error("[GetFileListFromMFTRec] ProcessAllocDataRuns finished with error.");
        return res; // fail to process data runs this is critical error, return immediately with error
    }

    if (node.DataRuns.Count() > 0) assert(lcns.Count() > 0); 
   
    ParseIndexRoot(root, lcns, node.FileList);

    return TErrorCode::Success;
}

//parses either resident or non-resident ATTR_LIST
TErrorCode TMFTBaseReader::ParseAttrList(MFTRecIndex indexMFTRec, ATTR_LIST_ENTRY* startListItem, uint8_t* attrListEnd, uint64_t realSize, uint64_t& processedAttrSize, AttrListPred processChildMFTRecPred)
{
    return ParseAttrList(indexMFTRec, ALL_ATTRS_FILTER, startListItem, attrListEnd, realSize, processedAttrSize, processChildMFTRecPred);
}

// Parses either resident or non-resident ATTR_LIST
// Gets only attributes specified by attrFilter parameter (bitwise mask)
TErrorCode TMFTBaseReader::ParseAttrList(MFTRecIndex indexMFTRec, uint32_t attrFilter, ATTR_LIST_ENTRY* startListItem, uint8_t* attrListEnd, uint64_t realSize, uint64_t& processedAttrSize, AttrListPred processChildMFTRecPred)
{
    GET_LOGGER;

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
std::expected<MFTRecIndex, TErrorCode> TMFTBaseReader::MFTRecIdByPath(const ci_string& path) // ci_string is for case INsensitive search here
{
    if (path.size() == 0) return std::unexpected(TErrorCode::InvalidArgument);

    // make sure that volData.hVolume and volume in path parameter are the same (both C: or both D:, etc)
    if(toupper(path[0]) != toupper(getVolData().Name[0])) return std::unexpected(TErrorCode::InvalidArgument);

    std::vector<ci_string> arr;
    StringToArray(path, arr, '\\');
    if (arr.size() == 0) return std::unexpected(TErrorCode::InvalidArgument);

    MFT_REF mftRecID{ 0 };
    mftRecID.sId.low = MFT_ROOT_REC_ID;
    uint8_t* mftRecBuf = (uint8_t*)alloca(getVolData().BytesPerMFTRec);

    DIR_NODE node;
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

        node.Clear();
        res = GetFileListFromMFTRec(mftRec, node);
        assert(res == TErrorCode::Success);

        IFILE_NAME fn;
        fn.ciName = arr[i];
        // binary search in sorted array (assume that GetFileListFromMFTRec has read items in SORTED order)
        auto iter = std::lower_bound(node.FileList.begin(), node.FileList.end(), fn); // fn has overrided operators < and ==
        if (iter != node.FileList.end() && (*iter).ciName == fn.ciName)
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

TErrorCode TMFTBaseReader::PrintSTDInfo(MFT_ATTR_HEADER* attr)
{
    assert(attr->AttrType == ATTR_STD_INFO);

    auto stdInfo = (ATTR_STD_INFO5*)Add2Ptr(attr, attr->res.DataOffset);
    assert((stdInfo->FileAttrib & FILE_ATTRIBUTE_NORMAL) == 0);// check that NORMAL bit is always zero

    FOut << FileDateToString(_T("Created: "), stdInfo->CreateTime);
    FOut << FileDateToString(_T("Modified: "), stdInfo->ModifyTime);
    FOut << FileDateToString(_T("Modified Attr: "), stdInfo->ModifyAttrTime);
    FOut << FileDateToString(_T("Last Access: "), stdInfo->LastAccessTime);

    FOut << std::format(_T("File Attrib:        {:#x} {}"), stdInfo->FileAttrib, convert_string<char_t>(FormatFileAttributes(stdInfo->FileAttrib)));
    FOut << _T("Version Number:     ") << stdInfo->VersionNum;
    FOut << _T("Max Version num:    ") << stdInfo->max_ver_num;
    FOut << _T("Class Id:           ") << stdInfo->class_id;
    FOut << _T("Owner Id:           ") << stdInfo->owner_id;
    FOut << std::format(_T("USN:                {:#x}"), stdInfo->usn);
    FOut << _T("Security ID:        ") << stdInfo->security_id;
    FOut << _T("Quota Charged:      ") << stdInfo->quota_charged;

    return TErrorCode::Success;
}

TErrorCode TMFTBaseReader::PrintFileNames(TAttrHeaderList& fileNamesList)
{
    for (auto& attr : fileNamesList)
    {
        assert(attr->AttrType == ATTR_FILENAME);

        auto fname = (ATTR_FILE_NAME*)Add2Ptr(attr, attr->res.DataOffset);
        std::wstring name(GetFName(fname), fname->FileNameLen);
        
        assert(fname->NameType <= FILE_NAME_UNICODE_AND_DOS);
        assert((fname->dup.FileAttrib & FILE_ATTRIBUTE_NORMAL) == 0);// check that NORMAL bit is always zero

        FOut << FileDateToString(_T("Created: "), fname->dup.CreateTime);
        FOut << FileDateToString(_T("Modified: "), fname->dup.ModifyTime);
        FOut << FileDateToString(_T("Modified Attr: "), fname->dup.ModifyAttrTime);
        FOut << FileDateToString(_T("LastAccess: "), fname->dup.LastAccessTime);

        FOut << _T("File Parent Rec ID: ") << convert_string<char_t>(fname->ParentDir.toHexString());
        FOut << std::format(_T("File Name Type:     {:#x} '{}' "), fname->NameType, convert_string< char_t>(FileNameTypes[fname->NameType]));
        FOut << std::format(_T("File DOS Attrib:    {:#x} {}"), fname->dup.FileAttrib, convert_string<char_t>(FormatFileAttributes(fname->dup.FileAttrib)));
        FOut << _T("File Name:         '") << convert_string<char_t>(name) << _T("'");
        FOut << _T("File Size:          ") << fname->dup.FileSize; 
    }

    return TErrorCode::Success;

}
TErrorCode TMFTBaseReader::PrintDirectory(TAttrCollection& collection)
{

    return TErrorCode::Success;
}
TErrorCode TMFTBaseReader::PrintObjectID(MFT_ATTR_HEADER* attr)
{
    assert(attr->AttrType == ATTR_ID);
    auto objID = (ATTR_OBJECT_ID*)Add2Ptr(attr, attr->res.DataOffset);
    
    constexpr const uint BUF_SZ = 100;
    std::wstring buf(BUF_SZ, 0);

   // GET_LOGGER;

    if (!StringFromGUID2(objID->ObjId, buf.data(), BUF_SZ))
        return TErrorCode::CorruptedData;
        //logger.Error("Error. ATTR_OBJECT_ID. StringFromGUID2 has failed 1.");

    FOut << _T("Attr Object ID:       ") << convert_string<char_t>(buf);

    if (attr->AttrSize > 16) //0x10
    {
        if (!StringFromGUID2(objID->BirthVolumeId, buf.data(), BUF_SZ))
            return TErrorCode::CorruptedData;
            //logger.Error("Error. ATTR_OBJECT_ID. StringFromGUID2 has failed 2.");
        
        FOut << _T("Attr Birth Volume ID: ") << convert_string<char_t>(buf);

        if (attr->AttrSize > 32) //0x20
        {
            if (!StringFromGUID2(objID->BirthObjectId, buf.data(), BUF_SZ))
                return TErrorCode::CorruptedData;
                //logger.Error("Error. ATTR_OBJECT_ID. StringFromGUID2 has failed 3.");
            
            FOut << _T("Attr Birth Object ID: ") << convert_string<char_t>(buf);

            if (attr->AttrSize > 48) //0x30
            {
                if (!StringFromGUID2(objID->DomainId, buf.data(), BUF_SZ))
                    return TErrorCode::CorruptedData;
                    //logger.Error("Error. ATTR_OBJECT_ID. StringFromGUID2 has failed 4.");
                FOut << _T("Attr Domain ID:       ") << convert_string<char_t>(buf);
            }
        }
    }

    return TErrorCode::Success;
}
TErrorCode TMFTBaseReader::PrintLabel(MFT_ATTR_HEADER* attr)
{
    std::wstring label((wchar_t*)Add2Ptr(attr, attr->res.DataOffset), attr->res.DataSize / sizeof(wchar_t));
    FOut << _T("Label: '") << convert_string<char_t>(label) << _T("'");
    return TErrorCode::Success;
}
TErrorCode TMFTBaseReader::PrintVolumeInfo(MFT_ATTR_HEADER* attr)
{
    auto volInfo = (VOLUME_INFO*)Add2Ptr(attr, attr->res.DataOffset);
    
    FOut <<_T("Volume Major Ver: {}") << volInfo->MajorVer;
    FOut << _T("Volume Minor Ver: {}") << volInfo->MinorVer;
    FOut << _T("Volume Flags:     {}") << volInfo->Flags;

    return TErrorCode::Success;
}
TErrorCode TMFTBaseReader::PrintDataInfo(MFT_ATTR_HEADER* attr)
{

    return TErrorCode::Success;
}

#define CH_ERR(_res_) \
if (_res_ != TErrorCode::Success) \
    return _res_;

TErrorCode TMFTBaseReader::PrintMFTRecord(MFT_FILE_RECORD* mftRec)
{
    TAttrCollection collection;
    CH_ERR(FillAttrCollection(mftRec, collection));
  
    // STD_INFO always present in MFT record
    auto& attrList = collection.Get(ATTR_STD_INFO);
    if (SingleAttributes[MATI(ATTR_STD_INFO)]) assert(attrList.Count() == 1);
    CH_ERR(PrintSTDInfo(attrList[0]));

    attrList = collection.Get(ATTR_FILENAME);
    if (SingleAttributes[MATI(ATTR_FILENAME)]) assert(attrList.Count() < 2);
    assert(attrList.Count() > 0); // FILENAME attr is always present, at least one name
    CH_ERR(PrintFileNames(attrList));

    attrList = collection.Get(ATTR_ID);
    if (SingleAttributes[MATI(ATTR_ID)]) assert(attrList.Count() < 2);
    if(attrList.Count() > 0) CH_ERR(PrintObjectID(attrList[0]));

    attrList = collection.Get(ATTR_LABEL);
    if (SingleAttributes[MATI(ATTR_LABEL)]) assert(attrList.Count() < 2);
    if (attrList.Count() > 0) CH_ERR(PrintLabel(attrList[0]));

    attrList = collection.Get(ATTR_VOL_INFO);
    if (SingleAttributes[MATI(ATTR_VOL_INFO)]) assert(attrList.Count() < 2);
    if (attrList.Count() > 0) CH_ERR(PrintVolumeInfo(attrList[0]));

    if (((mftRec->Flags & MFT_FLAG_IS_DIRECTORY) == MFT_FLAG_IS_DIRECTORY))
    {
        attrList = collection.Get(ATTR_BITMAP);
        if (SingleAttributes[MATI(ATTR_ID)]) assert(attrList.Count() < 2);
        if (attrList.Count() > 0) CH_ERR(PrintObjectID(attrList[0]));
    }

    return TErrorCode::Success;
}