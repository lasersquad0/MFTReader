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
	BYTE     OemId[8];        // Magic "NTFS    "
	WORD     BytesPerSector;  //  Size of a sector in bytes. 
	BYTE     SectorsPerCluster; 
	BYTE     Reserved[7];
	BYTE     MediaDescriptor; // 0xf8 = hard disk
	WORD     Reserved2;
	WORD     SectorsPerTrack; // Required to boot Windows.
	WORD     NumberOfHeads;   // Required to boot Windows.
	DWORD    HiddenSectors;
	DWORD    Unused1;       // zero, NTFS diskedit.exe states that this is actually :
	                        // u8 physical_drive;		// 0x80
	                        // u8 current_head;		// zero
	                        // u8 extended_boot_signature;	// 0x80
	                        //u8 unused;			// zero
	DWORD    Unused2;
	ULONGLONG TotalSectors;  // Number of sectors in volume.Gives maximum volume size of 2 ^ 63 sectors.
		                     // Assuming standard sector size of 512 bytes, the maximum byte size is approx. 4.7x10 ^ 21 bytes. (-;
	ULONGLONG MftStartLcn; // Cluster location of mft data
	ULONGLONG MftMirrorStartLcn; //Cluster location of copy of mft.
	CHAR     ClustersPerFileRecord; //Mft record size in clusters.
	BYTE     Reserved3[3];
	CHAR     ClustersPerIndexBlock; //Index block size in clusters
	BYTE     Reserved4[3];
	ULONGLONG VolumeSerialNumber;
	DWORD     Checksum;   //Boot sector checksum
};

static_assert(sizeof(NTFS_BOOT_SECTOR) == 84);

#pragma pack(pop)
