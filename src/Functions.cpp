
// this is to remove defines min, max in windows headers because they conflict with std::min std::max 
#define NOMINMAX

#include "Debug.h"
#include <iostream>
#include <string>
#include <cstdint>
#include <cassert>
#include <vector>
#include <stdexcept>

#include "strutils/include/string_utils.h"
#include "strutils/include/ci_string.h"
#include "logengine2/LogEngine.h"
#include "logengine2/FileStream.h"
#include "NTFS.h"
#include "Functions.h"
#include "Caches.h"


LogEngine::Logger& GetLoggerFunc()
{
    LogEngine::Logger& logger = LogEngine::GetFileLogger(MFT_LOGGER_NAME_FUNC, "LogMFTReaderFUNC.log");
    logger.SetAsyncMode(true);
    logger.SetLogLevel(LogEngine::Levels::llDebug);
    return logger;
}

// volume should be in format \\.\c:
void ReadVolumeData(const std::wstring& volume, VOLUME_DATA& volumeData)
{
    GET_LOGGER;
    logger.InfoFmt("Opening volume: {}", wtos(volume));

    HANDLE hVolume = CreateFile(volume.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                                0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);

    if (hVolume == INVALID_HANDLE_VALUE)
    {
        auto errMsg = GetErrorMessageTextA(GetLastError(), "CreateFile");
        logger.Error(errMsg);
        throw std::runtime_error(errMsg); // "Error opening volume.");
    }

    NTFS_VOLUME_DATA_BUFFER nvdb{0};

    if (!DeviceIoControl(hVolume, FSCTL_GET_NTFS_VOLUME_DATA, 0, 0, &nvdb, sizeof(nvdb), 0, nullptr))
    {
        std::string errMsg = std::format("[DeviceIoControl] Error reading volume data. Volume: {} error: {}", wtos(volume), GetLastError());
        logger.Error(errMsg);
        throw std::runtime_error(errMsg); // "Error reading volume data.");
    }

    volumeData.hVolume = hVolume;
    volumeData.Name = volume.substr(4); // remove \\.\ in \\.\C:
    volumeData.BytesPerCluster = nvdb.BytesPerCluster;
    volumeData.BytesPerMFTRec = nvdb.BytesPerFileRecordSegment;
    volumeData.TotalClusters = nvdb.TotalClusters;
    volumeData.BytesPerSector = nvdb.BytesPerSector;
    volumeData.MaxMFTIndex = (DWORD)(nvdb.MftValidDataLength.QuadPart / nvdb.BytesPerFileRecordSegment - 1);
}

std::wstring ParseVolume(const std::wstring& vol)
{
    if (vol.size() == 0) return _T(""); // indicates error
    if (vol.size() == 1) return std::wstring{ _T("\\\\.\\") } + vol[0] + _T(':'); // extract C
    return std::wstring{ _T("\\\\.\\") } + vol[0] + vol[1]; // extract C: from vol
}

// fills fnames with list of files got from ihdr
// DOES NOT go to subnodes
void GetFileList(INDEX_HDR* ihdr, FileListPred pred)
{
    GET_LOGGER;

    uint32_t off = ihdr->DEOffset; // offset of 1st dir entry

    while (true) // iterate though all DE+FILE_NAME entries
    {
        assert(off < ihdr->Used);

        NTFS_DE* de = (NTFS_DE*)Add2Ptr(ihdr, off); // NTFS_DE it is a "header" above File Name attribute, covers each file name attribute item

        logger.DebugFmt("DE ref to mft rec: {0} ({0:#x})", de->ref.Id); // reference to MFT rec for this file name
        logger.DebugFmt("DE flags: {}", de->flags);
        logger.DebugFmt("DE size: {}", de->size);
        logger.DebugFmt("DE key_size: {}", de->key_size);
        
        assert(de->size >= de->key_size + sizeof(NTFS_DE));

        if (de->key_size > 0) // key_size>0 means that filenameattr exists
        {
            ATTR_FILE_NAME* fattr = (ATTR_FILE_NAME*)Add2Ptr(de, sizeof(NTFS_DE));

            assert(de->key_size = sizeof(ATTR_FILE_NAME) + fattr->FileNameLen);
            //assert(de->ref.sId.low == fattr->ParentDir.sId.low);
            assert((fattr->dup.FileAttrib & 0x00000080) == 0);// check that NORMAL bit is always zero

            if (fattr->NameType != FILE_NAME_DOS) // bypass DOS filenames
            {
                ci_string ciwnm(GetFName(fattr, sizeof(ATTR_FILE_NAME)), fattr->FileNameLen);
                if (logger.ShouldLog(LogEngine::Levels::llDebug))
                {
                    logger.DebugFmt("DE ATTR Parent rec: {0} ({0:#x})", fattr->ParentDir.Id);//TODO check that parent of each file refers to MFT rec we are currently parsing
                    logger.DebugFmt("DE ATTR Attrib: {0} {0:#x}", fattr->dup.FileAttrib);
                    logger.DebugFmt("DE ATTR Name: '{}'", wtos(ciwnm));
                    logger.DebugFmt("DE ATTR Filesize: {}", fattr->dup.FileSize);
                }

                pred(fattr, de->ref);
                // level.AddValue(parent, level.Level(), de->ref, fattr);
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

// reads list of files in SORTED order starting from Index Root (referred by ihdr)
// goes to subnodes and uses pre-loaded list of LCNs containing ALLOC attribute values
void GetFileListFromNode(INDEX_HDR* ihdr, TLCNRecs& lcns, TFileList& fnames)
{
    GET_LOGGER;
    
    uint32_t off = ihdr->DEOffset; // offset of 1st dir entry

    while (true) // iterate though all DE+FILE_NAME entries
    {
        assert(off < ihdr->Used);

        NTFS_DE* de = (NTFS_DE*)Add2Ptr(ihdr, off); // NTFS_DE it is a "header" above File Name attribute, covers each file name attribute item

        logger.DebugFmt("[GetFileListFromNode] Dir Entry File rec {0} ({0:#x})", de->ref.Id);
        logger.DebugFmt("[GetFileListFromNode] Dir Entry flags: {}", de->flags);
        logger.DebugFmt("[GetFileListFromNode] Dir Entry size: {}", de->size);
        logger.DebugFmt("[GetFileListFromNode] Dir Entry key_size: {}", de->key_size);

        assert(de->size >= de->key_size + sizeof(NTFS_DE));

        if (de->flags & NTFS_IE_HAS_SUBNODES)
        {
            CLST vcn = *(CLST*)Add2Ptr(ihdr, off + de->size - sizeof(uint64_t)); // last 8 bytes contain the VÑN of subnode. This field is present only if (flags & NTFS_IE_HAS_SUBNODES)
    
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
            assert((fattr->dup.FileAttrib & 0x00000080) == 0);// check that NORMAL bit is always zero

            if (fattr->NameType != FILE_NAME_DOS) // bypass DOS filenames
            {
                ci_string ciwnm(GetFName(fattr, sizeof(ATTR_FILE_NAME)), fattr->FileNameLen);
                logger.DebugFmt("[GetFileListFromNode] Dir Entry Parent rec ID: {0} ({0:#x})", fattr->ParentDir.Id);
                logger.DebugFmt("[GetFileListFromNode] Dir Entry File/Dir name: '{}'", wtos(ciwnm));
                logger.DebugFmt("[GetFileListFromNode] Dir Entry File Attrib: {0} (0:#x)", fattr->dup.FileAttrib);
                logger.DebugFmt("[GetFileListFromNode] Dir Entry File size: {}", fattr->dup.FileSize);

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

    assert(dataRuns.Count() == 1); //assuming that one LCN is always enough for list of attributes

    DATA_RUN_ITEM& rli = dataRuns[0];
    logger.DebugFmt("[ParseNonresAttrList] Run Length Item VCN: {}, LCN: {}, Length:{}", rli.vcn, rli.lcn, rli.len);

    assert(rli.len == 1);

    auto dataBufSize = rli.len * volData.BytesPerCluster;
    uint8_t* dataBuf = (uint8_t*)alloca(dataBufSize);

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

    uint32_t resIndex = 0;
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

        uint32_t resIndex = 0;
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
    uint64_t dataBufLen = 0; // memory size in clusters, how many clusters is allocated in dataBuf
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
            bmpRecs.AddMany(dataBuf, (uint32_t)rli.len); //TODO think to avoid several copying dataBuf. first into bmpRecs, then into bitmap

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
        uint64_t bmpLenBytes = dataBufLen * volData.BytesPerCluster; //TODO shall we use nonres.RealSize here?
        assert((bmpLenBytes & 0x07) == 0); // bitmap data size always multiple of 8
        bitmap.SetData((uint64_t*)dataBuf, bmpLenBytes >> 3);
    }

    delete[] dataBuf;
    return true;
}

static bool ParseBitmap(const VOLUME_DATA& volData, MFT_ATTR_HEADER* attr, TBitField& bitmap)
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

static bool ParseAlloc(MFT_ATTR_HEADER* attr, TDataRuns& dataRuns)
{
    // sometimes one ATTR_LIST list may contain two ATTR_ALLOC attributes for some reason
    // it means we come here two times during parsing one MFT record with such ATTR_LIST 
    //assert(dataRuns.Count() == 0);

    if (!attr) return true; // attr==NULL means that ALLOC attribute is not present in MFT rec. Usually ALLOC is not needed in MFT record for empty directories

    assert(attr->NonResidentFlag == 1);

    return DataRunsDecode(attr, dataRuns); // DataRunDecode writes message to log in case of an error
}

static void ParseIndexRoot(MFT_ATTR_HEADER* attr, TLCNRecs& lcns, TFileList& fileList)
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

    logger.DebugFmt("IndexRoot indexed attr type: {:#x} ({})", (uint32_t)indexR->AttrType, AttrName(indexR->AttrType));
    logger.DebugFmt("Collation rule: {}", (uint32_t)indexR->Rule);
    logger.DebugFmt("Dir type: {} {}", indexR->ihdr.Flags, (indexR->ihdr.Flags == 0 ? " (SMALL DIR)" : " (BIG DIR)"));
    logger.DebugFmt("Used bytes: {}", pihdr->Used);

    GetFileListFromNode(pihdr, lcns, fileList);
}

// reads three required attributes from mftRec (INDEX_ROOT, ALLOC and BITMAP) 
// and then loads files from then in SORTED order starting from IndexRoot
// goes to subnodes when needed
// mftRec record must be a directory type
// node parameter is here only for returning back list of files in node.Filelist field.
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

    TLCNRecs lcns(volData.BytesPerCluster, (uint32_t)lcnTotalCount);
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

// goes through path dirs, reads files in SORTED order, goes to subdirs and so on, untill end of path
// returns MFT Record ID (low part of it)
// if path is incorrect function returns 0 (zero).
uint32_t MFTRecIdByPath(VOLUME_DATA& volData, const ci_string& path) // ci_string is for case insensitive search here
{
    if (path.size() == 0) return 0;

    GET_LOGGER;

    std::vector<ci_string> arr;
    StringToArray(path, arr, L'\\');
    if (arr.size() == 0) return 0;
    
    //TODO make check that volData.hVolume and volume in path parameter are the same (both C: or both D:, etc)
    assert(arr[0][0] == 'C' || arr[0][0] == 'c'); // we support only C: disks for now

    MFT_REF mftRecID;
    mftRecID.Id = MFT_ROOT_REC_ID; // starting MFT record
    uint8_t* mftRecBuf = (uint8_t*)alloca(volData.BytesPerMFTRec);

    DIR_NODE node;
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;

    for (size_t i = 1; i < arr.size(); i++) // bypass drive letter for now
    {
        if (!LoadMFTRecord(volData, mftRecID, mftRecBuf))
        {
            logger.Error("LoadMFTRecord finished with error.");
            return false;
        }

        node.Clear();
        GetFileListFromMFTRec(volData, mftRec, node); //TODO add error handling here
        FILE_NAME fn;
        fn.ciName = arr[i];
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

// reads list of files in already sorted order
bool ReadDirectoryV2(VOLUME_DATA& volData, MFT_REF parentMftRecID, uint32_t dirLevel, TFileList& gDirList)
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
    int32_t cnt = GetFileListFromMFTRec(volData, mftRec, node); // writes error to log file in case of error
    if (cnt == -1) return false;

    for (auto& item : node.FileList)
    {
        if (!item.NtfsInternal()/*!item.IsMetaFile() && !item.IsDotDir()*/) // do not add hidden metafiles into file list
        {
            if (dirLevel == 0) std::wcout << item.ciName.c_str() << std::endl;
            //if (dirLevel == 1) std::wcout << "      " << item.ciName.c_str() << std::endl;

            gDirList.AddValue(item);

            if (item.IsDir())
            {
                if (!item.IsReparse())
                {
                    if (!ReadDirectoryV2(volData, item.MFTRef, dirLevel + 1, gDirList))
                        logger.ErrorFmt("ReadDirectoryV2 finished with error for MFT rec: {}", item.MFTRef.sId.low);
                }
            }
        }
    }

    return true;
}

/*static bool compare(const FILE_NAME& a, const FILE_NAME& b)
{
    if (IsDir(a.Attr) && !IsDir(b.Attr)) return true; // folders on top during sorting
    if (!IsDir(a.Attr) && IsDir(b.Attr)) return false;
    return a.ciName < b.ciName;
}*/

void ReadDirsV2(VOLUME_DATA& volData)
{
    TFileList dirList;
    dirList.SetCapacity(1'000'000);

    MFT_REF startId;
    startId.Id = MFT_ROOT_REC_ID;
    ReadDirectoryV2(volData, startId, 0, dirList);

    auto dirCount = std::count_if(dirList.begin(), dirList.end(), [](FILE_NAME& a) { return a.IsDir(); });

    std::cout << toStringSepA(dirList.Count()) + " - total" << std::endl;
    std::cout << toStringSepA(dirList.Count() - dirCount) + " - files" << std::endl; // only files 
    std::cout << toStringSepA(dirCount) + " - dirs" << std::endl; // only dirs 

    /* std::string filename = "ListMFTFile_sortedV2.log";
     LogEngine::TFileStream ff(filename);

     string_t endl;
     BUILD_ENDL(endl);

     ff << toStringSepW(dirList.Count()) + L" - total" << endl;
     ff << toStringSepW(dirList.Count() - dirCount) + L" - files" << endl; // only files
     ff << toStringSepW(dirCount) + L" - dirs" << endl; // only dirs

     std::cout << "Sorting... " << std::endl;
     std::sort(dirList.begin(), dirList.end());

     std::cout << "Saving list of files to " << filename << std::endl;

     for (auto& item : dirList)
     {
         ff << item.ciName;
         if (item.IsDir()) ff << L'\\';
         ff << endl;
     }*/

    dirList.ClearMem();
}

