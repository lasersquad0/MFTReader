/*
STANDARD_INFO attr always present in each base MFT record, it always goes first
FILENAME - one mft record can contain 1 or 2 such attributes (one such attr is for DOS 8.3 name)
INDEX_ROOT always present in each record that contains directory, may contain zero elements, in this case all elements will be in ALLOCATION attribute
ALLOCATION attr is present in directory MFT record only if there are several (more than two?) files in the dir. INDEX_ROOT is also present together wiht ALLOC
BITMAP present in directory MFT record only when ALLOCATION is present
BITMAP attr can be 0 in some cases (which cases?)
BITMAP sometimes contains zero bits inside a sequence of 1 bits (why? - some LCNs are avoided in the list?)
Some records contain EA and/or EA_INFO attributes (what is this for in windows?)

*/

#pragma once

#include <windows.h>

//#define LODWORD(x)  (static_cast<DWORD>(x))
//#define HIDWORD(x)  (static_cast<DWORD>((x) >> 32))

#define Diff2Ptr(ptr1, ptr2) ((ULONG)(PBYTE(ptr2) - PBYTE(ptr1)))

#define Add2Ptr(P, I)		((uint8_t*)(P) + (I))
//#define PtrOffset(B, O)		((size_t)((size_t)(O) - (size_t)(B)))

#define GetAName(pRec, field)  ( (wchar_t*)((PBYTE)(pRec) + (pRec->field)) )
#define GetFName(pRec, offset) ( (wchar_t*)((PBYTE)(pRec) + (offset)) )

// mask to remove sequence number of MFT_REF
#define MFT_REF_MASK 0x0000FFFFFFFFFFFF

#define MFT_ROOT_RECID 5

#pragma pack(push, 1)

/* MFT record number structure. */
struct MFT_REF 
{
    union 
    {
    struct 
    {
        uint32_t low;	// The low part of the file number.
        uint16_t high;	// The high part of the file number.
        uint16_t seq;	// The sequence number of MFT record.
    } sId;
    uint64_t Id;
    };
};

static_assert(sizeof(MFT_REF) == 0x08);

/**
 * enum MFT_RECORD_FLAGS -
 *
 * These are the so far known MFT_RECORD_* flags (16-bit) which contain
 * information about the mft record in which they are present.
 *
 * MFT_RECORD_IS_4 exists on all $Extend sub-files.
 * It seems that it marks it is a metadata file with MFT record >24, however,
 * it is unknown if it is limited to metadata files only.
 *
 * MFT_RECORD_IS_VIEW_INDEX exists on every metafile with a non directory
 * index, that means an INDEX_ROOT and an INDEX_ALLOCATION with a name other
 * than "$I30". It is unknown if it is limited to metadata files only.
 */
enum class MFT_RECORD_FLAGS : uint16_t
{
    IN_USE        = 0x0001, //MY: IN_USE used for files AND for special "child" records when some attributes do not fit into base record
    IS_DIRECTORY  = 0x0002, //MY: for directories flag value is set to 0x03 (IN_USE | IS_DIRECTORY)
    IS_4          = 0x0004, // it is called RECORD_FLAG_SYSTEM in another source
    IS_VIEW_INDEX = 0x0008, // it is called RECORD_FLAG_UNKNOWN in another source
    SPACE_FILLER  = 0xFFFF, //TODO Just to make flags 16-bit. Do we really need it since we have uint16_t in enum definition?
};

static_assert(sizeof(MFT_RECORD_FLAGS) == 2);

/**
 * enum NTFS_SIGNATURE
 *
 * Magic identifiers present at the beginning of all ntfs record containing
 * records (like mft records for example).
 */
enum class NTFS_SIGNATURE: uint32_t 
{
    /* Found in $MFT/$DATA. */
    magic_FILE = 0x454c4946, // Mft entry. 'FILE' 
    magic_INDX = 0x58444e49, // Index buffer. 'INDX'
    magic_HOLE = 0x454c4f48, // ? (NTFS 3.0+?) 

    /* Found in $LogFile/$DATA. */
    magic_RSTR = 0x52545352, // Restart page. 
    magic_RCRD = 0x44524352, // Log record page. 
    
    // Found in $LogFile/$DATA.  (May be found in $MFT/$DATA, also?) 
    magic_CHKD = 0x444b4843, // Modified by chkdsk. 

    /* Found in all ntfs record containing records. */
    magic_BAAD = 0x44414142, // Failed multi sector transfer was detected.

    /* Found in $LogFile/$DATA when a page is full or 0xff bytes and is thus not initialized.  
       User has to initialize the page before using it. */
    magic_empty =0xffffffff, // Record is empty and has to be initialized before it can be used.
};

static_assert(sizeof(NTFS_SIGNATURE) == 4);


// Generic magic comparison macros. Finally found a use for the ## preprocessor operator! (-8
#define ntfs_is_magic(x, m)	 ( (uint32_t)(x) == (uint32_t)NTFS_SIGNATURE::magic_##m )
#define ntfs_is_magicp(p, m) ( *(uint32_t*)(p) == (uint32_t)NTFS_SIGNATURE::magic_##m )

// Specialised magic comparison macros for the NTFS_RECORD_TYPES defined above.
#define ntfs_is_file_rec(x)	 ( ntfs_is_magic (x, FILE) )
#define ntfs_is_file_recp(p) ( ntfs_is_magicp(p, FILE) )
#define ntfs_is_indx_rec(x)	 ( ntfs_is_magic (x, INDX) )
#define ntfs_is_indx_recp(p) ( ntfs_is_magicp(p, INDX) )


// Used in both MFT_FILE_RECORD and in INDEX_BUFFER ($INDEX_ALLOCATION) attribute
struct NTFS_RECORD_HEADER 
{
    /* Record magic number, equals 'FILE'/'INDX'/'RSTR'/'RCRD'. */
    uint8_t Signature[4];    // enum NTFS_SIGNATURE sign; // 0x00:
    uint16_t FixupOffset;	 // 0x04: Offset to the Update Sequence Array (usa) from the start of the ntfs record.
    uint16_t FixupCnt;	     // 0x06: Number of 2bytes sized entries in the usa including the Update Sequence Number(usn), 
                             // thus the number of fixups is the usa_count minus 1.
    uint64_t LogFileSeqNum;  // 0x08: Log file sequence number,
};

static_assert(sizeof(NTFS_RECORD_HEADER) == 0x10);

struct MFT_FILE_RECORD
{
    NTFS_RECORD_HEADER RecHeader;
    uint16_t SeqNum;           /* 0x10: Number of times this mft record has been reused. NOTE: The increment(skipping zero)
                                        is done when the file is deleted. NOTE: If this is zero it is left zero. */
    uint16_t HardLinksCnt;     /* 0x12: Number of hard links, i.e. the number of directory entries referencing this record.
                                        NOTE: Only used in mft base records. NOTE: When deleting a directory entry we check the link_count 
                                        and if it is 1 we delete the file.Otherwise we delete the FILE_NAME_ATTR being referenced by the
                                        directory entry from the mft record and decrement the link_count. */
    uint16_t FirstAttrOffset;  // 0x14: Offset to the first attribute.
    uint16_t Flags;            // 0x16: See MFT_RECORD_FLAGS. 
    //MFT_RECORD_FLAGS Flags;  
    uint32_t FileRecSize;      // 0x18: The size of used part.
    uint32_t AllocFileRecSize; // 0x1C: Total record size.
    MFT_REF  ParentFileRec;    // 0x20: Parent MFT record.
    uint16_t NextAttrID;       // 0x28: The ID that will be assigned to the next attribute added to this mft record. Incremented each time after it is used. Every time the mft record is reused this number is set to zero.
    uint16_t Align;            // 0x2A: High part of MFT record?
    uint32_t IndexMFTRec;      // 0x2C: Current MFT record number. this is 32bit, yes...    
    /*When (re)using the mft record, we place the update sequence array at this
        * offset, i.e.before we start with the attributes.This also makes sense,
        * otherwise we could run into problems with the update sequence array
        * containing in itself the last two bytes of a sector which would mean that
        * multi sector transfer protection wouldn't work. As you can't protect data
        * by overwriting it since you then can't get it back...
        * When reading we obviously use the data from the ntfs record header.
        */
  //  uint16_t FixUpNum;        // 0x30
};

static_assert(sizeof(MFT_FILE_RECORD) == 0x30);

enum ATTR_TYPE : uint32_t
{
    ATTR_ZERO        = 0x00,
    ATTR_STD_INFO    = 0x10,
    ATTR_LIST_ATTR   = 0x20,
    ATTR_FILENAME    = 0x30,
    ATTR_ID          = 0x40, // ATTR_VOLUME_VERSION on Nt4
    ATTR_SECURE      = 0x50,
    ATTR_LABEL       = 0x60,
    ATTR_VOL_INFO    = 0x70,
    ATTR_DATA        = 0x80,
    ATTR_ROOT        = 0x90,
    ATTR_ALLOC       = 0xA0,
    ATTR_BITMAP      = 0xB0,
    ATTR_REPARSE     = 0xC0, // ATTR_SYMLINK on Nt4
    ATTR_EA_INFO     = 0xD0,
    ATTR_EA          = 0xE0,
    ATTR_PROPERTYSET = 0xF0,
    ATTR_LOGGED_UTILITY_STREAM = 0x100,
    ATTR_END         = 0xFFFFFFFF
};

#define ATTR_TYPE_CNT 0x11

static_assert(sizeof(enum ATTR_TYPE) == 4);
struct MFT_ATTR_RESIDENT
{
    uint32_t DataSize;      //0x10 Byte size of attribute value. 
    uint16_t DataOffset;    //0x14 Byte offset of the attribute value from the start of the attribute record.
                            //     When creating, align to 8 - byte boundary if we have a name present as this might
                            //     not have a length of a multiple of 8 - bytes.
    uint8_t  IndexedFlag;   //0x16
    uint8_t  Align;         //0x17 
}; // 0x18

static_assert(sizeof(MFT_ATTR_RESIDENT) == 0x08);

struct MFT_ATTR_NONRESIDENT
{
    uint64_t StartVCN;        // 0x10 Starting VCN of this segment.
    uint64_t LastVCN;         // 0x18 End VCN of this segment.
    uint16_t DataRunsOffset;  // 0x20 Offset to packed runs.
    //  Unit of Compression size for this stream, expressed
//  as a log of the cluster size.
//	0 means file is not compressed
//	1, 2, 3, and 4 are potentially legal values if the
//	    stream is compressed, however the implementation
//	    may only choose to use 4, or possibly 3.  Note
//	    that 4 means cluster size time 16.	If convenient
//	    the implementation may wish to accept a
//	    reasonable range of legal values here (1-5?),
//	    even if the implementation only generates
//	    a smaller set of values itself.
    uint8_t CompressionUnitSize; // 0x22: The compression unit for the attribute expressed as the logarithm to the base two of the number 
                                 // of clusters in a compression unit. If CompressionUnitSize is zero, the attribute is not compressed.
    uint8_t res1[5];		     // 0x23:
    uint64_t AllocatedSize;      // 0x28 Byte size of disk space allocated to hold the attribute value. Always is a multiple of the cluster size.
                                 // When a file is compressed, this field is a multiple of the compression block size (2 ^ compression_unit) and it represents 
                                 // the logically allocated space rather than the actual on disk usage. For this use the CompressedSize below.
    uint64_t RealSize;           // 0x30 Byte size of the attribute value. Can be larger than AllocatedSize if attribute value is compressed or sparse.
    uint64_t StreamSize;         // 0x38 Byte size of initialized portion of the attribute value. Usually equals RealSize.
    uint64_t CompressedSize;     // 0x40 Byte size of the attribute value after compression. Only present when compressed. 
                                 // Always is a multiple of the cluster size. Represents the actual amount of disk space being used on the disk.
    // (present only for the first segment (0 == vcn)
    // of compressed attribute)

}; // (0x30 or 0x38 if compressed) or 0x40 or 0x48 if counted with MFT_ATTR_HEADER

static_assert(sizeof(MFT_ATTR_NONRESIDENT) == 0x38);

// Possible values of MFT_ATTR_HEADER.flags:
#define ATTR_FLAG_COMPRESSED	  0x0001
#define ATTR_FLAG_COMPRESSED_MASK 0x00FF
#define ATTR_FLAG_ENCRYPTED	      0x4000
#define ATTR_FLAG_SPARSED	      0x8000

struct MFT_ATTR_HEADER
{
    ATTR_TYPE AttrType;
    uint32_t AttrSize;       //0x4 Byte size of the resident part of the attribute (aligned to 8 - byte boundary). Used to get to the next attribute.
    uint8_t  NonResidentFlag;//0x8 Specifies, when true, that the attribute value is nonresident
    uint8_t  AttrNameSize;   //0x9 The size, in characters, of the name (if any) of the attribute.
    uint16_t AttrNameOffset; //0xA The offset, in bytes, from the start of the structure to the attribute name. The attribute name is stored as a Unicode string.
    uint16_t Flags;          //0xC see ATTR_FLAG_XXX defines above
    uint16_t AttrID;         //0xE The ID of this attribute record. This number is unique within this mft record (see MFT_FILE_RECORD/NextAttrId above )

    union
    {
        MFT_ATTR_RESIDENT res;
        MFT_ATTR_NONRESIDENT nonres;
    };
};

static_assert(sizeof(MFT_ATTR_HEADER) == 0x48);

enum class FILE_ATTR_FLAGS : uint32_t
{
    READONLY      = 0x00000001,
    HIDDEN        = 0x00000002,
    SYSTEM        = 0x00000004,
    ARCHIVE       = 0x00000020,
    DEVICE        = 0x00000040,
    TEMPORARY     = 0x00000100,
    SPARSE_FILE   = 0x00000200,
    REPARSE_POINT = 0x00000400,
    COMPRESSED    = 0x00000800,
    OFFLINE       = 0x00001000,
    NOT_CONTENT_INDEXED = 0x00002000,
    ENCRYPTED     = 0x00004000,
    VALID_FLAGS   = 0x00007fb7,
    DIRECTORY     = 0x10000000,
};

static_assert(sizeof(enum FILE_ATTR_FLAGS) == 4);

/*
 * NOTE on times in NTFS: All times are in MS standard time format, i.e. they
 * are the number of 100-nanosecond intervals since 1st January 1601, 00:00:00
 * universal coordinated time (UTC). (In Linux time starts 1st January 1970,
 * 00:00:00 UTC and is stored as the number of 1-second intervals since then.)
 */

// NOTE: Always resident.
// NOTE: Present in all base file records on a volume.
// NOTE: There is conflicting information about the meaning of each of the time fields but the meaning as defined below has been verified to be
// correct by practical experimentation on Windows NT4 SP6a and is hence assumed to be the one and only correct interpretation.
struct ATTR_STD_INFO5
{
    uint64_t CreateTime;	  // 0x00 File creation file.
    uint64_t ModifyTime;	  // 0x08 File modification time.
    uint64_t ModifyAttrTime;  // 0x10 Last time any attribute of this MFT record was modified.
    uint64_t LastAccessTime;  // 0x18 File last access time.
    uint32_t FileAttrib;      // 0x20 Standard DOS attributes & more.
    //FILE_ATTRIBUTE FileAttrib; 
    uint32_t max_ver_num;	  // 0x24 Maximum allowed versions for file. Zero if version numbering is disabled.
    uint32_t VersionNum;	  // 0x28 This file's version (if any). Set to zero if max_ver_num is zero.
    uint32_t class_id;	      // 0x2C Class Id from bidirectional Class Id index.
    uint32_t owner_id;	      // 0x30 Owner_id of the user owning the file. Translate via $Q index in FILE_Extend/$Quota to the quota control entry for 
                              // the user owning the file. Zero if quotas are disabled
    uint32_t security_id;	  // 0x34 Security_id for the file. Translate via $SII index and $SDS data stream in FILE_Secure to the security descriptor.
    uint64_t quota_charged;   // 0x38 Byte size of the charge to the quota for all streams of the file. Note: Is zero if quotas are disabled.
    uint64_t usn;		      // 0x40: Last Update Sequence Number of the file. This is a direct index into the file $UsnJrnl. If zero, the USN Journal is disabled.
};

static_assert(sizeof(ATTR_STD_INFO5) == 0x48);

// Attribute ATTR_LIST_ATTR structure (0x20)  - located between STD_INFO and FILE_NAME attributes
struct ATTR_LIST_ENTRY 
{
    enum ATTR_TYPE AttrType; // 0x00 The type of attribute.
    uint16_t AttrSize;		 // 0x04 The size, in bytes, of the attribute list entry.
    uint8_t NameLen;		 // 0x06 The length of attribute name.
    uint8_t NameOffset;		 // 0x07 The offset to attribute name.
    uint64_t StartVCN;       // 0x08 Lowest VCN of this portion of the attribute value.This is usually 0. It is non-zero for the case where one attribute
                             // does not fit into one mft record and thus several mft records are allocated to hold this attribute. In the latter case, each mft
                             // record holds one extent of the attribute and there is one attribute list entry for each extent
    MFT_REF ref;	         // 0x10 The reference of the MFT record holding the MFT_ATTR_HEADER for this portion of the attribute value.
    uint16_t AttrId;		 // 0x18 If StartVCN = 0, the Id of the attribute being referenced; otherwise 0.
    uint16_t name[3];		 // 0x1A Just to align. To get real name can use bNameOffset.

}; // sizeof(0x20)

static_assert(sizeof(struct ATTR_LIST_ENTRY) == 0x20);

/* File name types (the field type in struct ATTR_FILE_NAME). */
#define FILE_NAME_POSIX   0
#define FILE_NAME_UNICODE 1
#define FILE_NAME_DOS	  2
#define FILE_NAME_UNICODE_AND_DOS (FILE_NAME_DOS | FILE_NAME_UNICODE)

struct NTFS_DUP_INFO
{
    uint64_t CreateTime;	 // 0x00 File creation file.
    uint64_t ModifyTime;	 // 0x08 File modification time.
    uint64_t ModifyAttrTime; // 0x10 Last time any attribute was modified.
    uint64_t LastAccessTime; // 0x18 File last access time.
    uint64_t AllocSize;      // 0x20 Data attribute allocated size (for unnamed $DATA attribute), multiple of cluster size.
    uint64_t FileSize;	     // 0x28 Actual data attribute size <= AllocSize.
    uint32_t FileAttrib;     // 0x30 Standard DOS attributes & more.
    //FILE_ATTRIBUTE FileAttrib;
    uint16_t ea_size;		 // 0x34 Packed EAs.
    uint16_t reparse;		 // 0x36 Used by Reparse.

}; // 0x38
 
static_assert(sizeof(NTFS_DUP_INFO) == 0x38);

// Filename attribute structure (0x30). 
struct ATTR_FILE_NAME
{
    MFT_REF ParentDir;   // 0x00 reference to MFT record for parent directory.
    NTFS_DUP_INFO dup;   // 0x08
    uint8_t FileNameLen; // 0x40 File name length in words for unicode.
    uint8_t NameType;	 // 0x41 File name type: POSIX=0, UNICODE=1, DOS=2, BOTH=3
}; //0x42   

static_assert(sizeof(ATTR_FILE_NAME) == 0x42);

/*struct VOLUME_INFO
{
    uint64_t res1;	   // 0x00
    uint8_t major_ver; // 0x08: NTFS major version number (before .)
    uint8_t minor_ver; // 0x09: NTFS minor version number (after .)
    uint16_t flags;	   // 0x0A: Volume flags, see VOLUME_FLAG_XXX

}; // sizeof=0xC   */

#pragma pack(show)  

/* Object ID (0x40) */
struct ATTR_OBJECT_ID
{
    GUID ObjId;	// 0x00: Unique Id assigned to file.
    GUID BirthVolumeId; // 0x10: Birth Volume Id is the Object Id of the Volume on.
    // which the Object Id was allocated. It never changes.
    GUID BirthObjectId; // 0x20: Birth Object Id is the first Object Id that was
    // ever assigned to this MFT Record. I.e. If the Object Id
    // is changed for some reason, this field will reflect the
    // original value of the Object Id.
    GUID DomainId;	// 0x30: Domain Id is currently unused but it is intended to be
    // used in a network environment where the local machine is
    // part of a Windows 2000 Domain. This may be used in a Windows
    // 2000 Advanced Server managed domain.
};

static_assert(sizeof(ATTR_OBJECT_ID) == 0x40);

struct INDEX_HDR
{
    uint32_t DEOffset; // 0x00 The offset from the start of this structure to the first NTFS_DE.
    uint32_t Used;     // 0x04 The size of this structure plus all entries (quad-word aligned).
    uint32_t Total;	   // 0x08 The allocated size of for this structure plus all entries.
    uint8_t Flags;	   // 0x0C 0x00 = Small directory, 0x01 = Large directory.
    uint8_t res[3];
    //
    // DEOffset + used <= total
    //
};

static_assert(sizeof(INDEX_HDR) == 0x10);

/* Index root structure ( 0x90 ). */
enum class COLLATION_RULE: uint32_t
{
    BINARY = 0, 
    // $I30
    FILENAME = 0x01,
    // $SII of $Secure and $Q of Quota
    UINT = 0x10,
    // $O of Quota
    SID = 0x11,
    // $SDH of $Secure
    SECURITY_HASH = 0x12,
    // $O of ObjId and "$R" for Reparse
    UINTS = 0x13
};

static_assert(sizeof(COLLATION_RULE) == 4);

//
struct ATTR_INDEX_ROOT
{
    enum ATTR_TYPE AttrType;  // 0x00: The type of attribute to index on. 0 if entry does not use an attribute
    enum COLLATION_RULE Rule; // 0x04: The rule.
    uint32_t index_block_size;// 0x08: The size of index record.
    uint8_t index_block_clst; // 0x0C: The number of clusters or sectors per index.
    uint8_t res[3];
    struct INDEX_HDR ihdr;	// 0x10:
};

static_assert(sizeof(ATTR_INDEX_ROOT) == 0x20);
static_assert(offsetof(ATTR_INDEX_ROOT, ihdr) == 0x10);


// Index entry defines ( the field flags in NtfsDirEntry ).
#define NTFS_IE_HAS_SUBNODES (1)
#define NTFS_IE_LAST		 (2)

// Directory entry structure. 
struct NTFS_DE 
{
    union 
    {
        struct MFT_REF ref; // 0x00: MFT record number with this file.
        struct 
        {
            uint16_t data_off;  // 0x00:
            uint16_t data_size; // 0x02:
            uint32_t res;	    // 0x04: Must be 0.
        } view;
    };
    uint16_t size;		// 0x08: The size of this entry.
    uint16_t key_size;	// 0x0A: The size of File name attr in bytes (sizeof(ATTR_FILE_NAME) + length of file name).
    uint16_t flags;		// 0x0C: Entry flags: NTFS_IE_XXX.
    uint16_t res;		// 0x0E:

    // Here any indexed attribute can be placed.
    // One of them is:
    // struct ATTR_FILE_NAME AttrFileName;
    //

    // The last 8 bytes of this structure contains the VBN of subnode.
    // !!! Note !!!
    // This field is presented only if (flags & NTFS_IE_HAS_SUBNODES)
    // __le64 vbn;
}; // 0x10

static_assert(sizeof(NTFS_DE) == 0x10);

// This struct located in clusters as a part of $INDEX_ALLOCATION attribute
struct INDEX_BUFFER 
{
    NTFS_RECORD_HEADER RecHeader; // 'INDX'
    uint64_t vcn;   // 0x10: Virtual cluster number of the index buffer. vcn if index >= cluster or vsn id index < cluster
    INDEX_HDR ihdr; // 0x18:
};

static_assert(sizeof(INDEX_BUFFER) == 0x28);


typedef uint64_t CLST;

struct DATA_RUN_ITEM 
{
    uint32_t len; // Length in clusters.
    CLST vcn; // Virtual cluster number.
    CLST lcn; // Logical cluster number.

    bool operator==(const DATA_RUN_ITEM& other) const { return len == other.len && vcn == other.vcn && lcn == other.lcn; }
};

/*
struct RUNS_TREE 
{
    struct ntfs_run* runs;
    size_t count; // Currently used size a ntfs_run storage
    size_t allocated; // Currently allocated ntfs_run storage size.
};*/

/**
 * struct BITMAP_ATTR - Attribute: Bitmap (0xb0).
 *
 * Contains an array of bits (aka a bitfield).
 *
 * When used in conjunction with the index allocation attribute, each bit
 * corresponds to one index block within the index allocation attribute. Thus
 * the number of bits in the bitmap * index block size / cluster size is the
 * number of clusters in the index allocation attribute.
 */
struct ATTR_BITMAP_ATTR 
{
    uint8_t bitmap[1];	// Array of bits. 
};

#pragma pack(pop)


