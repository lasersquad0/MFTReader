#pragma once

#include "windows.h"
#include "strutils/include/string_utils.h"

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
	BYTE  BootFlag;     // 0x08 -  bootable partition, 0x00 - non-bootable
	BYTE  StartCHS[3];  // old and unused
	BYTE  Type;         // 0x07 NTFS partition, 0x0C - FAT, 0x83 - Linux, etc.
	BYTE  EndCHS[3];    // old and unused
	DWORD FirstLBA;     // First sector of partition. Counted from beginning of physical disk
	DWORD SectorCount;  // total number of sectors occupied by partition starting from FirstLBA.
};

static_assert(sizeof(MBR_PARTITION_ENTRY) == 16);


struct NTFS_BOOT_SECTOR {
	BYTE     Jump[3];         // 0x00 jump to boot code
	BYTE     OemId[8];        // 0x03 Magic "NTFS    "
	WORD     BytesPerSector;  // 0x0B Size of a sector in bytes. 
	BYTE     SectorsPerCluster; // 0x0D
	BYTE     Reserved[7];      
	BYTE     MediaDescriptor; // 0x15 0xf8 = hard disk
	WORD     Reserved2;
	WORD     SectorsPerTrack; // 0x18 Required to boot Windows.
	WORD     NumberOfHeads;   // 0x1A Required to boot Windows.
	DWORD    HiddenSectors;   // 0x1C
	DWORD    Unused1;         // zero, NTFS diskedit.exe states that this is actually :
	                          // u8 physical_drive;		// 0x80
	                          // u8 current_head;		// zero
	                          // u8 extended_boot_signature;	// 0x80
	                          //u8 unused;			// zero
	DWORD    Unused2;
	ULONGLONG TotalSectors;        // 0x28 Number of sectors in volume.Gives maximum volume size of 2 ^ 63 sectors.
		                           // Assuming standard sector size of 512 bytes, the maximum byte size is approx. 4.7x10 ^ 21 bytes. (-;
	ULONGLONG MftStartLcn;         // 0x30 Cluster location of mft data
	ULONGLONG MftMirrorStartLcn;   // 0x38 Cluster location of copy of mft.
	CHAR     ClustersPerFileRecord;// 0x40 Mft record size in clusters.
	BYTE     Reserved3[3];
	CHAR     ClustersPerIndexBlock;// 0x44 Index block size in clusters
	BYTE     Reserved4[3];
	ULONGLONG VolumeSerialNumber;  // 0x48
	DWORD     Checksum;            // 0x50 Boot sector checksum
}; // 0x54

static_assert(sizeof(NTFS_BOOT_SECTOR) == 84);

#pragma pack(pop)

template<typename STRING, typename ARRAY>
bool EXPECT_ONE_OF(STRING expected, ARRAY arr)
{
	// make sure that STRING is one of instantiations of strings
	static_assert(std::is_base_of<std::basic_string<typename STRING::value_type, typename STRING::traits_type>, STRING>::value);
	// make sure that wstr is either std::string (no conversion required) or std::wstring
	static_assert(std::is_same_v<typename STRING::value_type, wchar_t> || std::is_same_v<typename STRING::value_type, char>);
	static_assert(std::is_same_v<typename ARRAY::item_type::value_type, wchar_t> || std::is_same_v<typename ARRAY::item_type::value_type, char>);
	//static_assert(std::is_same_v<typename STRING::value_type, typename ARRAY::item_type::value_type>);
	//static_assert(std::is_convertible_v<typename ARRAY::item_type, STRING>);
	
	for (auto& item : arr)
	{
		auto str = convert_string<typename STRING::value_type>(item);
		if (expected == str.c_str()) return true; //==STRING(str.c_str())
	}
	return false;
}


inline string_t GetVolumeName(const string_t& path)
{
	string_t vol;
	vol.resize(MAX_PATH);

	BOOL r = GetVolumePathName(path.c_str(), vol.data(), MAX_PATH);
	UNREFERENCED_PARAMETER(r);
	assert(r != 0);

	vol.resize(vol.find(_T('\0')));
	if (vol[vol.size() - 1] == '\\')
	{
		vol[vol.size() - 1] = _T('\0');
		vol.resize(vol.size() - 1);
	}

	return vol;
}