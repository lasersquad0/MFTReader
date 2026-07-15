
#include "gtest/gtest.h"
#include "Readers.h"
#include "TestUtils.h"
#include "MFTBaseParamTest.h"

class TMFTRawImageLoader : public IRecordLoader
{
private:
    HANDLE FHFile = INVALID_HANDLE_VALUE;
    uint64_t FMFTOffset{};
public:
    TMFTRawImageLoader() {}
    TMFTRawImageLoader(const string_t& imgFileName) { OpenVolume(imgFileName); }
    ~TMFTRawImageLoader() { CloseVolume(); }

    //finds first MFT record and fills FMFTOffset with byte offset to this record from beginning of the image file 
    void OpenVolume(const string_t& imgFileName) override
    {
        FHFile = CreateFile(imgFileName.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        ASSERT_NE(INVALID_HANDLE_VALUE, FHFile) << "Error opening file '" << imgFileName << "'";;

        MBR_PARTITION_ENTRY mbr;
        NTFS_BOOT_SECTOR partNTFS;
        uint64_t ntfsOff = 0; // partition not found
        DWORD off = 0x1BE;

        // look list of partitions for NTFS partition
        for (size_t i = 0; i < 4; i++)
        {
            if (!SetFilePointer(FHFile, off, 0, FILE_BEGIN))
                FAIL() << "Error setting file pointer '" << imgFileName << "'";

            DWORD bytesRead = 0, mbrsz = sizeof(mbr);

            if (!(ReadFile(FHFile, &mbr, mbrsz, &bytesRead, nullptr) && (bytesRead == mbrsz)))
                FAIL() << "Error reading file '" << imgFileName << "'";
            //READ_FILE(hFile, &mbr, mbrsz, bytesRead);

            if (!SetFilePointer(FHFile, mbr.FirstLBA * DEFAULT_SECTOR_SIZE, 0, FILE_BEGIN))
                FAIL() << "Error setting file pointer '" << imgFileName << "'";
            //SET_FILE_POINTER(hFile, mbr.FirstLBA * DEFAULT_SECTOR_SIZE);

            if (!(ReadFile(FHFile, &partNTFS, sizeof(partNTFS), &bytesRead, nullptr) && (bytesRead == sizeof(partNTFS))) )
                FAIL() << "Error reading file '" << imgFileName << "'";
            //READ_FILE(hFile, &ntfsPart, sizeof(ntfsPart), bytesRead);

            if (memcmp(partNTFS.OemId, NTFS_LABEL, 8) == 0)
            {
                ntfsOff = mbr.FirstLBA * (uint64_t)DEFAULT_SECTOR_SIZE;
                break;
            }

            off += sizeof(NTFS_BOOT_SECTOR);
        }

        ASSERT_NE(0, ntfsOff); // check that we've found NTFS partition
       
        EXPECT_EQ(DEFAULT_SECTOR_SIZE, partNTFS.BytesPerSector);
        EXPECT_EQ(8, partNTFS.SectorsPerCluster);

        FVolumeData.TotalClusters.QuadPart = partNTFS.TotalSectors;
        FVolumeData.BytesPerCluster = partNTFS.BytesPerSector * partNTFS.SectorsPerCluster;
        FVolumeData.BytesPerMFTRec = (partNTFS.ClustersPerFileRecord >= 0)? partNTFS.ClustersPerFileRecord * FVolumeData.BytesPerCluster: 1u << (-partNTFS.ClustersPerFileRecord);
        EXPECT_EQ(FVolumeData.BytesPerMFTRec, DEFAULT_BYTES_PER_MFT_REC);
        FVolumeData.BytesPerSector = partNTFS.BytesPerSector;
        FVolumeData.ClustersPerFileRecordSegment = FVolumeData.BytesPerMFTRec/FVolumeData.BytesPerCluster;
        FVolumeData.hVolume = FHFile;
        FVolumeData.MftZoneStart.QuadPart = partNTFS.MftStartLcn;
        FVolumeData.MftZoneEnd.QuadPart = 1000;
        FVolumeData.Name = imgFileName;

        // here where MFT record #0 located
        FMFTOffset = ntfsOff + partNTFS.MftStartLcn * FVolumeData.BytesPerCluster;

        // reading MFT record #0, getting $MFT LCNs
        uint8_t* mftRecBuf = (uint8_t*)alloca(FVolumeData.BytesPerMFTRec);
        MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;
        MFT_REF mftRef{ {0,0,0} };
 
        bool res = LoadMFTRecord(mftRef, mftRecBuf);
        ASSERT_TRUE(res) << "Error loading MFT record " << mftRef.sId.low;

        if (!ntfs_is_file_recp(mftRec->RecHeader.Signature) && !ntfs_is_magicp(mftRec->RecHeader.Signature, zero))
            FAIL() << "MFT record with incorrect signature found " << mftRec->RecHeader.Signature << " (neither 'FILE' nor '0000')";

        ITEM_INFO item;
        // try to parse only 'FILE' records
        //if (ntfs_is_file_recp(mftRec->RecHeader.Signature))
        //    res = stat.ReadMftItemInfoBuf(mftRec, item);
        ASSERT_TRUE(res) << "Error parsing MFT record " << mftRef.sId.low;

        //TODO need to parse $MFT file here to get info where $MFT ends.
        //TODO also $MFT may be fragmented, need to count this too here
    }

    // returns false when all records are loaded from a file
    bool LoadMFTRecord(MFT_REF mftRecRef, uint8_t* mftRecData) override
    {
        if (!SetFilePointer(FHFile, FMFTOffset + FVolumeData.BytesPerMFTRec * (uint64_t)mftRecRef.sId.low, 0, FILE_BEGIN))
            return false; //FAIL() << "Error setting file pointer";

        //if (!FFile.seekg(FVolumeData.BytesPerMFTRec * (uint64_t)mftRecRef.sId.low, std::ios::beg))
        //    return false;
        
        DWORD bytesRead = 0;
        if (!(ReadFile(FHFile, mftRecData, FVolumeData.BytesPerMFTRec, &bytesRead, nullptr) && (bytesRead == FVolumeData.BytesPerMFTRec)))
            return false; //FAIL() << "Error reading file";

        return true;

       // if (FFile.read(reinterpret_cast<char*>(mftRecData), FVolumeData.BytesPerMFTRec))
       //     return true;
       // else
       //     return false;
    }

};

class MFTImgFileParserTest : public MFTStringParamTest
{
public:
    static void SetUpTestCase()
    {
        FName = "MFTImgFileParserParamTest";
        MFTStringParamTest::SetUpTestCase();
    }

    void SetUp() override
    {
        // it is important to call SetUp of the parent class here 
        MFTStringParamTest::SetUp();

        // add here your test initialization code
    }

    // You can implement all the usual fixture class members here.
    // To access the test parameter, call GetParam() from class TestWithParam<T>.
};

// Inside a test, access the test parameter with the GetParam() method of the TestWithParam<T> class:
TEST_P(MFTImgFileParserTest, ReadDiskImage_1)
{
    string_t imgFileName = GetParam();

    TMFTRawImageLoader tldr(imgFileName);
    TMFTStatCollector stat(tldr);

    // reading MFT record #0
    uint8_t* mftRecBuf = (uint8_t*)alloca(DEFAULT_BYTES_PER_MFT_REC);
    MFT_FILE_RECORD* mftRec = (MFT_FILE_RECORD*)mftRecBuf;
    MFT_REF mftRef{ 0 };
    mftRef.sId.low = 0;

    bool res;
    res = tldr.LoadMFTRecord(mftRef, mftRecBuf);
    ASSERT_TRUE(res) << "Error loading MFT record " << mftRef.sId.low;
    
    if (!ntfs_is_file_recp(mftRec->RecHeader.Signature) && !ntfs_is_magicp(mftRec->RecHeader.Signature, zero))
        FAIL() << "MFT record with incorrect signature found " << mftRec->RecHeader.Signature << " (neither 'FILE' nor '0000')";

    ITEM_INFO item;
    // try to parse only 'FILE' records
    if (ntfs_is_file_recp(mftRec->RecHeader.Signature))
        res = stat.ReadMftItemInfoBuf(mftRec, item);
    ASSERT_TRUE(res) << "Error parsing MFT record " << mftRef.sId.low;

}

INSTANTIATE_TEST_CASE_P(DiskImage1, MFTImgFileParserTest, testing::Values(_T(TEST_DATA_DIR "ntfs-ptrn.raw")));
