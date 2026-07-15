#pragma once

#include "windows.h"

#define MFT_TESTS_LOG_FILE "LogMFTReaderTests.log"
//#define MFT_TESTS_LOGGER_NAME "mft_tests_logger"

#define TEST_DATA_DIR "../../TestData/"

constexpr DWORD DEFAULT_SECTOR_SIZE = 512;
constexpr char NTFS_LABEL[] = "NTFS    ";

// structure fields alignment set to 1 byte.
// by default alignment is 16 bytes but here we need 1
#pragma pack(push, 1)

struct MBR_PARTITION_ENTRY
{ 
	BYTE  BootFlag;
	BYTE  StartCHS[3];
	BYTE  Type;
	BYTE  EndCHS[3];
	DWORD FirstLBA;
	DWORD SectorCount;
};

static_assert(sizeof(MBR_PARTITION_ENTRY) == 16);

struct NTFS_BOOT_SECTOR {
	BYTE     Jump[3];
	BYTE     OemId[8];
	WORD     BytesPerSector;
	BYTE     SectorsPerCluster;
	BYTE     Reserved[7];
	BYTE     MediaDescriptor;
	WORD     Reserved2;
	WORD     SectorsPerTrack;
	WORD     NumberOfHeads;
	DWORD    HiddenSectors;
	DWORD    Unused1;
	DWORD    Unused2;
	ULONGLONG TotalSectors;
	ULONGLONG MftStartLcn;
	ULONGLONG MftMirrorStartLcn;
	CHAR     ClustersPerFileRecord;
	BYTE     Reserved3[3];
	CHAR     ClustersPerIndexBlock;
	BYTE     Reserved4[3];
	ULONGLONG VolumeSerialNumber;
	DWORD     Checksum;
};

static_assert(sizeof(NTFS_BOOT_SECTOR) == 84);

#pragma pack(pop)
