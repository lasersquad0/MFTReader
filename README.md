# MFTReader

This project is an attempt to create application that reads NTFS system directly.
"Directly" means that application reads raw data (MFT records) of NTFS filesystem, parses it and gets required data from there.

There is no much information in Internet and in Microsoft documentation about NTFS filesystem structure. Microsoft does not disclose this information.
There are however some articles in Internet describing NTFS filesystem structure with different level of details.
MFTReader application is mostly based on this information.

What MFTReader app can do:
- read single file/folder information by MFT record ID.
- read single file/folder information by full path
- read entire NTFS filesystem (all files and folders, their attributes, without file data) and build statistic that cannot built with using other methods

Statistic that is built by MFTReader app includes:
- number of hard links for file
- number of names assigned to the file/folder (you will discover many interesting thing here!)
- number and names of data streams assigned to file/folder (very interesting too)
- number of NTFS attributes that file/folder has (do not mix with file DOS file attributes, NTFS attributes is a different thing)
- statistic of files having either resident or non-resident NTFS attributes
- number of reparse points for the file/folder
- etc.
- 

