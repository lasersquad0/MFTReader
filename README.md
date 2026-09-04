# MFTReader tool
This project is an attempt to create application that reads NTFS system directly.
"Directly" means that application reads raw data (MFT records) of NTFS filesystem, parses it and gets required data from there.

There is no much information in Internet and in Microsoft documentation about NTFS filesystem structure. Microsoft does not disclose this information.
There are however a number of articles in Internet describing NTFS filesystem structure with different level of details.
MFTReader application is based on this information.

What MFTReader app can do:
- read single file/folder information by MFT record ID. Shows all NTFS attributes and much more.
- read single file/folder information by path. Shows all NTFS attributes and much more.
- read entire NTFS filesystem (all files and folders, their attributes, without file data) and build statistic that cannot built with using other methods

Statistic that is built by MFTReader app includes:
- number of hard links for file
- number of names assigned to the file/folder (you will discover many interesting thing here!)
- number and names of data streams assigned to file/folder (very interesting too)
- number of NTFS attributes that file/folder has (do not mix with file DOS file attributes, NTFS attributes is a different thing)
- statistic of files having either resident or non-resident NTFS attributes
- number of reparse points for the file/folder
- etc.

MFTReader works only with NTFS file systems and does not work with any others.


## How to use this tool
### Command line parameters
MFTReader has 3 commands (`-r, -p, -s`), each command can have one or two parameters.

```
Usage: MFTReader -command <arg1>...<argN>

Commands:
-r, --record <req arg> <arg> Display information about MFT record. First argument is MFT record ID, 
                             second argument - volume name (if omitted default volume c:\ is used).
-p, --path   <arg>           Display information about file/directory by specified path.
-s, --stat   <arg>           Show interesting volume/disk statistics.
-c, --cache  <arg>           Build cache for file search and show some statistics.
-t, --test                   For testing purposes.
```


### Read file information by path
Type in console the following command.

```bash
MFTReaderConsole.exe -p c:\windows\notepad.exe
```
You will get the output similar to below.

Alternate paths - you will be surprised how many files has alternate names (hard links).

MFT Rec ID - shown in both dec and hex format. You can use this id later to show information about this file.

MFT Changed - datetime when this attribute (not file) has changed. This date hasn't shown any where in Windows Explorer.

You may notice that some information is duplicated e.g. datetime fields present in both STANDARD_INFO and FILENAME attributes.
This is how NTFS filesystem works. It has some data duplication for better speed.

```
MFTReader shows useful information about your NTFS file system.

Info about file  : c:\windows\notepad.exe
MFT Record       : 853 523
Alternate paths  : c:\Windows\notepad.exe
                   c:\Windows\WinSxS\amd64_microsoft-windows-notepad_31bf3856ad364e35_10.0.26100.8737
                   _none_0abe6ba8201cb866\notepad.exe
                   c:\Windows\System32\notepad.exe


+-------------------------------------------------------+
|           MFT Record ID: 0xd0613 (#853523)            |
+-------------------------------------------------------+

  MFT Rec Signature     :'FILE'
  MFT Rec Type          :'IN USE'
  MFT Rec ID            : 0xd0613 (853523)
  MFT Rec Sequence      : 9
  MFT Log Seq Number    : 278 274 101 483
  MFT Parent Rec ID     : s:0x0 h:0x0 l:0x0 (0) BASE
  MFT Hard Links Count  : 3
  MFT Next Attr ID      : 16
  MFT Rec Size          : 776
  MFT Allocated Size    : 1 024

  #1 -------- Attribute STANDARD_INFO (0x10) --------
  Attr location:        0xd0613 (853523)
  Residence:            RESIDENT
  Type:                 STANDARD_INFO 0x10
  Attr ID:              0
  Flags:                0
  Indexed:              0
  Created:              27-Jul-26 10:54:05 AM
  Modified:             27-Jul-26 10:54:05 AM
  MFT Changed:          28-Jul-26 10:15:50 PM
  Last Access:          04-Sep-26 10:42:00 PM
  DOS Attrib:           0x40020 ----A----------L-
  Version Number:       0
  Max Version num:      0
  Class Id:             0
  Owner Id:             0
  USN:                  0x9a2d33e90
  Security ID:          9 002
  Quota Charged:        0

  #2 ---------- Attribute FILENAME (0x30) -----------
  Attr location:        0xd0613 (853523)
  Residence:            RESIDENT
  Type:                 FILENAME 0x30
  Attr ID:              13
  Flags:                0
  Indexed:              1
  File Name:            'notepad.exe'
  Name Type:            'POSIX' 0x0
  DOS Attrib:           0x40020 ----A----------L-
  Parent Rec ID:        s:0x2 h:0x0 l:0xe527 (58663)
  Created:              27-Jul-26 10:54:05 AM
  Modified:             27-Jul-26 10:54:05 AM
  MFT Changed:          28-Jul-26 10:15:18 PM
  Last Access:          28-Jul-26 9:49:03 PM

  #3 ---------- Attribute FILENAME (0x30) -----------
  Attr location:        0xd0613 (853523)
  Residence:            RESIDENT
  Type:                 FILENAME 0x30
  Attr ID:              11
  Flags:                0
  Indexed:              1
  File Name:            'notepad.exe'
  Name Type:            'UNICODE_AND_DOS' 0x3
  DOS Attrib:           0x40020 ----A----------L-
  Parent Rec ID:        s:0x3 h:0x0 l:0xf675b (1009499)
  Created:              27-Jul-26 10:54:05 AM
  Modified:             27-Jul-26 10:54:05 AM
  MFT Changed:          28-Jul-26 9:49:03 PM
  Last Access:          28-Jul-26 9:48:51 PM

  #4 ---------- Attribute FILENAME (0x30) -----------
  Attr location:        0xd0613 (853523)
  Residence:            RESIDENT
  Type:                 FILENAME 0x30
  Attr ID:              15
  Flags:                0
  Indexed:              1
  File Name:            'notepad.exe'
  Name Type:            'POSIX' 0x0
  DOS Attrib:           0x40020 ----A----------L-
  Parent Rec ID:        s:0x2 h:0x0 l:0xfbf5 (64501)
  Created:              27-Jul-26 10:54:05 AM
  Modified:             27-Jul-26 10:54:05 AM
  MFT Changed:          28-Jul-26 10:15:22 PM
  Last Access:          28-Jul-26 9:49:03 PM

  #5 ------------ Attribute DATA (0x80) -------------
  Attr location:        0xd0613 (853523)
  Residence:            NON - RESIDENT
  Type:                 DATA 0x80
  Attr ID:              4
  Flags:                0
  StartVCN:             0
  LastVCN:              87
  RealSize:             360 448 bytes
  StreamSize:           360 448 bytes
  Allocated Size:       360 448 bytes
  Data Runs Count:      1
  Total Clusters Used:  88

  Data Runs List:
  #0 | VCN:  0 | LCN:  10042328 | Len: 88

  #6 ------- Attribute EA_INFORMATION (0xd0) --------
  Attr location:        0xd0613 (853523)
  Residence:            RESIDENT
  Type:                 EA_INFORMATION 0xd0
  Attr ID:              6
  Flags:                0
  Indexed:              0

  #7 ------------- Attribute EA (0xe0) --------------
  Attr location:        0xd0613 (853523)
  Residence:            NON - RESIDENT
  Type:                 EA 0xe0
  Attr ID:              14
  Flags:                0
  StartVCN:             0
  LastVCN:              0
  RealSize:             432 bytes
  StreamSize:           432 bytes
  Allocated Size:       4 096 bytes

  #8 ----- Attribute LOGGED_UTIL_STREAM (0x100) -----
  Attr location:        0xd0613 (853523)
  Residence:            RESIDENT
  Type:                 LOGGED_UTIL_STREAM 0x100
  Attr ID:              12
  Attr Name:            '$TXF_DATA'
  Flags:                0
  Indexed:              0

+-------------------------------------------------------+
|        END of MFT Record ID: 0xd0613 (#853523)        |
+-------------------------------------------------------+

```

### Read file information by MFT record ID

MFT record ID can be specified either in decimal or hexadecimal numeral system.

```bash
MFTReaderConsole.exe -r 53523
MFTReaderConsole.exe -r 53523 d:\
MFTReaderConsole.exe -r 0xd0613
```
### Collect and show files/directories statistic for entire volume

If you don't specify which volume to read, c:\ will be read.

```bash
MFTReaderConsole.exe -s
MFTReaderConsole.exe -s d:
```

