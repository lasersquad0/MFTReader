#pragma once

#include "Debug.h"
//#include <windows.h>
//#include <winver.h>
//#include <DbgHelp.h>
#include <iostream>
#include <cassert>
#include <cstdint>
//#include <filesystem>
//#include <vector>
//#include <tchar.h>
//#include <regex>
#include <string>

#include "strutils/include/string_utils.h"
#include "logengine2/DynamicArrays.h"
#include "logengine2/FileStream.h"
#include "NTFS.h"
#include "FileLevel.h"


#define MAX_DIR_LEVELS 100
#define MAX_DIRS 100'000

struct CacheItemRef
{
	uint32_t ItemLevel{};
	uint32_t ItemIndex{};
};


class TFileCache
{
public:
	typedef TFileLevelList TLevel;

private:
	THArray<TLevel*> FCacheData;

public:
	TFileCache()
	{
		FCacheData.SetCapacity(MAX_DIR_LEVELS);
	}

	~TFileCache()
	{
		FCacheData.Clear(); // free memory for all levels and cache items
	}

	TLevel** GetFirstLevelPointer()
	{
		return FCacheData.GetValuePointer(0);
	}

	CACHE_ITEM* GetItem(uint32_t level, uint32_t index)
	{
		return FCacheData.GetValue(level)->GetValue(index);
	}

	CACHE_ITEM* GetItem(CacheItemRef& itemRef)
	{
		return FCacheData.GetValue(itemRef.ItemLevel)->GetValue(itemRef.ItemIndex);
	}

	// adds requested level if not present
	TLevel* GetLevel(uint32_t level)
	{
		assert(level <= FCacheData.Count());

		if (level < FCacheData.Count())
		{
			return FCacheData.GetValue(level);
		}
		else
		{
			assert(level == FCacheData.Count());
			//level_type vLevel(level, MAX_DIRS * sizeof(CACHE_ITEM)); // approximate capacity in bytes 
			//vLevel.reserve(MAX_DIRS);
			auto resultLevel = DBG_NEW TLevel(level, MAX_DIRS * sizeof(CACHE_ITEM));
			FCacheData.AddValue(resultLevel);
			//level_type& resultLevel = FCacheData.GetValue(level);
			return resultLevel;
		}
	}
	
	CacheItemRef AddRootItem(ATTR_FILE_NAME* fileData)
	{
		TLevel* level = GetLevel(0);
		
		assert(level->Level() == 0);
		assert(level->Count() == 0); // cannot AddRoot several times
		
		MFT_REF rootId{0};
		rootId.Id = MFT_ROOT_REC_ID; // root MFT is is always 5.
		
		uint32_t idx = level->AddValue(0, 0, rootId, fileData);
		assert(idx == 0);
		
		return CacheItemRef{0, 0};
	}

	CacheItemRef AddItem(uint32_t parent, uint32_t itemLevel, MFT_REF MFTRecID, ATTR_FILE_NAME* fileData)
	{
		TLevel* level = GetLevel(itemLevel);
		assert(level->Level() == itemLevel);
		return CacheItemRef{ itemLevel, level->AddValue(parent, itemLevel, MFTRecID, fileData) };
	}

	uint32_t TotalCount()
	{
		uint32_t res = 0;
		for (auto lv : FCacheData) res += lv->Count();
		
		return res;
	}

	uint32_t LevelsCount()
	{
		return FCacheData.Count();
	}

	void Clear() 
	{
		for (auto lv : FCacheData) delete lv;
		FCacheData.Clear(); 
	}


	/*
void Serialize(LogEngine::TStream& os)
{
	os << FCacheData.size();
	for (level_type& lv: FCacheData)
	{
		os << lv.size();
		for (CacheItem& item: lv)
		{
			item.Serialize(os);
		}
	}
}

void Deserialize(LogEngine::TStream& is)
{
	FCacheData.clear();

	size_t cacheSize;
	is >> cacheSize;
	FCacheData.reserve(cacheSize);

	for (int i = 0; i < cacheSize; i++)
	{
		level_type tmpLevel;
		FCacheData.push_back(tmpLevel);
		level_type& level = FCacheData.at(FCacheData.size()-1);

		size_t levelSize;
		is >> levelSize;
		level.reserve(levelSize);

		for (int j = 0; j < levelSize; j++)
		{
			CacheItem item;
			item.Deserialize(is);
			level.push_back(item);
		}
	}
}*/


	/*
ullong ReadDirectory(const ci_string currDir, CacheItemRef parent)
{
	static_assert(sizeof(ullong) == 8);

	ullong dirSize{};

	ci_string searchDir = currDir + TEXT("\\*");
	FILEDATA fileData{};

	HANDLE hFind = FindFirstFileEx(searchDir.c_str(), FindExInfoBasic, &fileData, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		if (GetLastError() != ERROR_ACCESS_DENIED) // print error message only if other than ERROR_ACCESS_DENIED error occurred
			std::wcout << "ERROR: " << "'" << searchDir.c_str() << "'" << " GetLastError: " << GetLastError() << std::endl;
		return dirSize;
	}

	assert(fileData.cFileName[0] != 0);

	// bypass dirs with names '.' and '..'
	if (!IS_DOT_DIR(fileData.cFileName))
	{
		auto itemRef = AddItem(parent.ItemIndex, fileData, parent.ItemLevel + 1);

		if (IS_DIR(fileData))
		{
			// do NOT go into reparse points to avoid file duplications
			if (!IS_REPARSE(fileData)) dirSize = ReadDirectory(currDir + TEXT("\\") + fileData.cFileName, itemRef);
		}
		else
			dirSize = MAKE_FULL_FILESIZE(fileData.nFileSizeHigh, fileData.nFileSizeLow);
	}

	while (true)
	{
		if (FindNextFile(hFind, &fileData) != 0)
		{
			assert(fileData.cFileName[0] != 0);

			if (IS_DOT_DIR(fileData.cFileName)) continue;

			auto itemRef = AddItem(parent.ItemIndex, fileData, parent.ItemLevel + 1);

			if (IS_DIR(fileData))
			{   // do NOT go into reparse points to avoid file duplications
				if (!IS_REPARSE(fileData)) dirSize += ReadDirectory(currDir + TEXT("\\") + fileData.cFileName, itemRef);
			}
			else
				dirSize += MAKE_FULL_FILESIZE(fileData.nFileSizeHigh, fileData.nFileSizeLow);
		}
		else
		{
			if (GetLastError() == ERROR_NO_MORE_FILES) break;
			std::cout << "Error in FindNextFile(). GetLAstError(): " << GetLastError() <<std::endl;
			break;
		}
	}

	CacheItem& item = GetItem(parent);
	LARGE_INTEGER tmp;
	tmp.QuadPart = dirSize;
	item.FFileData.nFileSizeHigh = tmp.HighPart;
	item.FFileData.nFileSizeLow = tmp.LowPart;
	item.FFullFileSize = dirSize;

	BOOL_CHECK(FindClose(hFind));

	return dirSize;
}

void FillFileData(const TCHAR* filePath, FILEDATA& fileData)
{
	fileData.dwFileAttributes = GetFileAttributes(filePath);

	HANDLE hf = CreateFile(filePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (hf == INVALID_HANDLE_VALUE)
	{
		std::wcout << GetErrorMessageText(GetLastError(), _T("Open file")) << std::endl << filePath << std::endl;
		return;
	}

	BOOL_CHECK(::GetFileTime(hf, &fileData.ftCreationTime, &fileData.ftLastAccessTime, &fileData.ftLastWriteTime));
	LARGE_INTEGER fileSize;
	BOOL_CHECK(::GetFileSizeEx(hf, &fileSize));
	fileData.nFileSizeHigh = fileSize.HighPart;
	fileData.nFileSizeLow = fileSize.LowPart;

	BOOL_CHECK(CloseHandle(hf));
}
*/

	/*CacheItemRef AddFullPath(const ci_string path)
	{
		std::vector<ci_string> pathArray;
		std::vector<ci_string> pathArrayAccum;

		StringToArrayAccum<ci_string>(path, pathArrayAccum, '\\');
		StringToArray<ci_string>(path, pathArray, '\\');

		FILEDATA fileData{};
		_tcsncpy_s<MAX_PATH>(fileData.cFileName, pathArray[0].c_str(), pathArray[0].size());
		//fileData.cFileName[pathArray[0].size()] = '\0'; 
		FillFileData(pathArrayAccum[0].c_str(), fileData);
		CacheItemRef parent = AddRootItem(fileData);
		
		size_t lv = 1;
		assert(lv == parent.ItemLevel + 1);
		for (; lv < pathArray.size(); lv++)
		{
			assert(lv == parent.ItemLevel + 1);
			_tcsncpy_s<MAX_PATH>(fileData.cFileName, pathArray[lv].c_str(), pathArray[lv].size());
			//fileData.cFileName[pathArray[lv].size()] = '\0';
			FillFileData(pathArrayAccum[lv].c_str(), fileData);
			parent = AddItem(parent.ItemIndex, fileData, lv, true);
		}

		return parent;
	}

	void SerializeTo(const std::string fileName)
	{
		LogEngine::TFileStream fout(fileName, LogEngine::TFileMode::fmWriteTrunc);
		Serialize(fout);

	}

	void DeserializeFrom(const std::string fileName)
	{
		LogEngine::TFileStream fin(fileName, LogEngine::TFileMode::fmRead);
		Deserialize(fin);
	}
	*/

	// Saves all cache item names into text file one file per line
	void SaveTo(const std::string fileName)
	{
		LogEngine::TFileStream fout(fileName, LogEngine::TFileMode::fmWriteTrunc);
		
		fout << toStringSepA(TotalCount()) << " - total items" << EndLine;

		//for (uint32_t i = 0; i < FCacheData.Count(); ++i)
		for (auto level : FCacheData)
		{
			//TLevel* level = FCacheData.GetValue(i);

			CACHE_ITEM* sitem = level->First();
			do
			{
				string_t fn(sitem->Name(), sitem->FileAttr.FileNameLen);
				if (sitem->IsDir())
					fout << wtos(fn) << '\\' << EndLine;
				else
					fout << wtos(fn) << EndLine;
				sitem = level->Next(sitem);
			} while (!level->IsEnd(sitem));
		}
	}

	void ToArray(THArray<string_t>& array)
	{
		for (auto level: FCacheData)
		{
			CACHE_ITEM* sitem = level->First();
			if (level->IsEnd(sitem)) continue; // case - when level is empty
			do
			{
				string_t fn(sitem->Name(), sitem->FileAttr.FileNameLen);
				if (sitem->IsDir())
					array.AddValue(fn + L"\\");
				else
					array.AddValue(fn);

				sitem = level->Next(sitem);

			} while (!(level->IsEnd(sitem)));
		}
	}

	void PrintLevelsStat()
	{
		std::cout << "Levels Count: " << FCacheData.Count() << std::endl;

		size_t i = 0, sum = 0;
		for (auto level : FCacheData)
		{
			std::cout << "Level" << i << " : " << level->Count() << std::endl;
			i++;
			sum += level->Count();
		}

		std::cout << "SUMM of Levels : " << sum << std::endl;
	}

	/*
	void ReadFileSystem(const ci_string& startDir)
	{
		std::wcout << "Start reading file system: " << startDir.c_str() << std::endl;

		CacheItemRef startItemRef = AddFullPath(startDir);

		ullong folderSize = ReadDirectory(startDir, startItemRef);

		std::wcout << "Finished. Folder size: " << toStringSepW(folderSize) << std::endl;
	}



	ci_string MakePathString(CacheItemRef& ref)
	{ 
		return MakePathString(ref.ItemLevel, ref.ItemIndex);
	}

	ci_string MakePathString(size_t itemLevel, size_t itemIndex)
	{
		ci_string pathStr;
		pathStr.reserve(MAX_PATH);
		std::vector<ci_string> path;
		path.reserve(MAX_DIR_LEVELS);

		CacheItem& firstItem = GetItem(itemLevel, itemIndex);

		path.push_back(firstItem.FFileData.cFileName);
		size_t index = firstItem.FParent;
		int ii = (int)itemLevel - 1;

		while (ii >= 0)
		{
			CacheItem& item = GetItem(ii, index);
			path.push_back(item.FFileData.cFileName);
			index = item.FParent;
			ii--;
		}

		pathStr.append(path.back());
		for (int k = (int)path.size() - 2; k >= 0; --k)
		{
			pathStr.append(TEXT("\\"));
			pathStr.append(path.at(k));
		}

		return pathStr;
	}

	void PrintAllItems()
	{
		for (int i = (int)FCacheData.size() - 1; i >= 0; --i)
		{
			level_type& level = FCacheData.at(i);
			ci_string pathStr;

			for (size_t j = 0; j < level.size(); j++)
			{
				CacheItem& sitem = level.at(j);

				if (sitem.FFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					pathStr = MakePathString(i, j);

					std::wcout << pathStr.c_str() << "\t" << toStringSepW(sitem.FFullFileSize) << std::endl;
					//"\t" << FileTimeToString(sitem.FFileData.ftLastWriteTime) << "\t" << FileTimeToString(sitem.FFileData.ftLastAccessTime) << std::endl;
				}
			}
		}
	}

	template<size_t SIZE>
	class TopFolders
	{
	protected:
		std::vector<CacheItem> FItems;
		ullong FMin{};
		ullong FMax{};

		void UpdateMinMaxAndDelete()
		{
			if (FItems.size() == 0) return;

			FMax = 0;
			FMin = FItems[0].FFullFileSize;
			size_t minIndex = 0;
			for (size_t i = 0; i < FItems.size(); i++)
			{
				CacheItem& item = FItems[i];
				if (FMax < item.FFullFileSize) FMax = item.FFullFileSize;
				if (FMin > item.FFullFileSize) { FMin = item.FFullFileSize; minIndex = i; };
			}

			FItems.erase(FItems.begin() + minIndex);
		}

	public:
		TopFolders()
		{
			FItems.reserve(SIZE);
		}

		void AddValue(CacheItem& item)
		{
			if (FItems.size() < SIZE)
			{
				FItems.push_back(item);
				if (FMax < item.FFullFileSize) FMax = item.FFullFileSize;
				if (FMin > item.FFullFileSize) FMin = item.FFullFileSize;
			}
			else
			{
				if (item.FFullFileSize > FMin)
				{
					FItems.push_back(item);
					UpdateMinMaxAndDelete();
				}
			}
		}

		void PrintTopFolders(FileNamesCache& cache)
		{
			std::cout << std::format("Top {} biggest folders:", SIZE) << std::endl;

			std::sort(FItems.begin(), FItems.end(), [](CacheItem& a, CacheItem& b) -> bool { return a.FFullFileSize > b.FFullFileSize; });

			for (size_t k = 0; k < FItems.size() - 1; k++)
			{
				CacheItem& item = FItems[k];
				ci_string pathStr;
				if (item.FLevel > 0)
				{
					pathStr = cache.MakePathString(item.FLevel - 1, item.FParent);
					pathStr.append(TEXT("\\"));
				}
				pathStr.append(item.FFileData.cFileName);
				std::wcout << pathStr.c_str() << "\t" << toStringSep(item.FFullFileSize) << std::endl;
			}
		}
	};

	void PrintTop10Folders()
	{
		TopFolders<30> top10;

		for (int i = (int)FCacheData.size() - 1; i >= 0; --i)
		{
			level_type& level = FCacheData.at(i);
			
			for (size_t j = 0; j < level.size(); j++)
			{
				CacheItem& sitem = level.at(j);
				//assert(sitem.FFullFileSize > 0);

				if ((sitem.FFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				{
					top10.AddValue(sitem);
				}		
			}
		}

		top10.PrintTopFolders(*this);
	}

	void PrintByLevel(size_t filterLevel, ulong filterAttr = FILE_ATTRIBUTE_DIRECTORY)
	{
		level_type& levelData = FCacheData.at(filterLevel);

		for (size_t j = 0; j < levelData.size(); j++)
		{
			CacheItem& item = levelData.at(j);
			
			assert(item.FFullFileSize > 0);

			if ((item.FFileData.dwFileAttributes & filterAttr) == filterAttr)
			{
				std::wcout << std::setw(30) << item.FFileData.cFileName << "\t" << toStringSepW(MAKE_FULL_FILESIZE(item.FFileData.nFileSizeHigh, item.FFileData.nFileSizeLow)) << std::endl;
			}
		}
	}

	void PrintSearch(const ci_string& wildCard, size_t filterLevel, ulong filterAttr)
	{
		std::wregex rgx(wildCard);
		std::wsmatch wideMatch;
		std::wcmatch wideMatch2;

		//std::basic_regex<typename ci_string::value_type> rgx(wildCard);
		

		if (filterLevel == -1)
		{
		}
		else
		{
			level_type& levelData = FCacheData.at(filterLevel);

			for (size_t j = 0; j < levelData.size(); j++)
			{
				CacheItem& item = levelData.at(j);

				assert(item.FFullFileSize > 0);

				if ((item.FFileData.dwFileAttributes & filterAttr) == filterAttr)
				{
					//bool found = std::regex_match<typename ci_string::value_type, typename std::regex_traits<typename ci_string::value_type>>(item.FFileData.cFileName, rgx);
					bool found = std::regex_match(item.FFileData.cFileName, wideMatch2, rgx);

					if(found)
						std::wcout << std::setw(30) << item.FFileData.cFileName << "\t" << toStringSepW(MAKE_FULL_FILESIZE(item.FFileData.nFileSizeHigh, item.FFileData.nFileSizeLow)) << std::endl;
				}
			}
		}
	}

	enum FileTypes {ftFile, ftDir, ftTemp, ftArchive, ftReadOnly, ftHidden, ftSystem, ftDevice, ftSymbolic, ftCompressed, 
					ftEncrypted, ftOffline, ftSparse, ftPinned, ftNotIndexed, ftLast};
	void PrintStat()
	{
		uint32_t stat[ftLast]{};
		size_t totalItems = 0;
		size_t countedItems = 0;
		for (level_type& lv : FCacheData)
		{
			totalItems += lv.size();
			for (CacheItem& item : lv)
			{
				assert(item.FFullFileSize > 0);

				bool counted = false;
				if (item.FFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { stat[ftDir]++; counted= true; }
				if (item.FFileData.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE)   { stat[ftArchive]++; counted = true; }
				if (item.FFileData.dwFileAttributes & FILE_ATTRIBUTE_READONLY)  { stat[ftReadOnly]++; counted = true; }
				if (item.FFileData.dwFileAttributes & FILE_ATTRIBUTE_NORMAL)    { stat[ftFile]++; counted = true; }
				if (item.FFileData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)    { stat[ftHidden]++; counted = true; }
				if (item.FFileData.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY) { stat[ftTemp]++;  counted = true; }
				if (item.FFileData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM)    { stat[ftSystem]++; counted = true; }
				if (item.FFileData.dwFileAttributes & FILE_ATTRIBUTE_DEVICE)    { stat[ftDevice]++; counted = true; }
				if (item.FFileData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {stat[ftSymbolic]++; counted = true; }
				if (item.FFileData.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED){ stat[ftCompressed]++; counted = true; }
				if (item.FFileData.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) { stat[ftEncrypted]++; counted = true; }
				if (item.FFileData.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) { stat[ftOffline]++; counted = true; }
				if (item.FFileData.dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) { stat[ftSparse]++; counted = true; }
				if (item.FFileData.dwFileAttributes & FILE_ATTRIBUTE_PINNED) { stat[ftPinned]++; counted = true; }
				if (item.FFileData.dwFileAttributes & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED) { stat[ftNotIndexed]++; counted = true; }

				if(!counted)
					std::wcout << "Missing file attribute : " << item.FFileData.cFileName << " :" << item.FFileData.dwFileAttributes << std::endl;
				if (counted) countedItems++;
			}
		}

		std::cout << "Total number of files and dirs : " << totalItems << std::endl;
		std::cout << "Total without dirs : " << totalItems - stat[ftDir] << std::endl;
		std::cout << "Directories : " << stat[ftDir] << std::endl;
		std::cout << "Read Only   : " << stat[ftReadOnly] << std::endl;
		std::cout << "Archive     : " << stat[ftArchive] << std::endl;
		std::cout << "Hidden      : " << stat[ftHidden] << std::endl;
		std::cout << "Temporary   : " << stat[ftTemp] << std::endl;
		std::cout << "System      : " << stat[ftSystem] << std::endl;
		std::cout << "Devices     : " << stat[ftDevice] << std::endl;
		std::cout << "Symbolic    : " << stat[ftSymbolic] << std::endl;
		std::cout << "Compressed  : " << stat[ftCompressed] << std::endl;
		std::cout << "Encrypted   : " << stat[ftEncrypted] << std::endl;
		std::cout << "Offline     : " << stat[ftOffline] << std::endl;
		std::cout << "Sparse      : " << stat[ftSparse] << std::endl;
		std::cout << "Normal      : " << stat[ftFile] << std::endl;
		std::cout << "Pinned      : " << stat[ftPinned] << std::endl;
		std::cout << "NOT Indexed : " << stat[ftNotIndexed] << std::endl;

		std::cout << "Remaining   : " << totalItems - countedItems << std::endl;
	}

	void LoadFrom(const std::string& fileName)
	{
		LogEngine::TFileStream fin(fileName, LogEngine::TFileMode::fmRead);

		ci_string str;
		str.reserve(MAX_PATH);
		std::vector<ci_string> path;
		path.reserve(MAX_DIR_LEVELS); // we do not expect more that 100 levels of subdirs

		int lines = 0;
		while (true)
		{
			if (fin.Eof()) break;

			fin >> str;
			lines++;

			AddFullPath(str);
		}

		std::cout << "Lines processed : " << lines << std::endl;
		std::cout << "Levels : " << FCacheData.size() << std::endl;

		size_t i = 0, sum = 0;
		for (auto item : FCacheData)
		{
			std::cout << "Level" << i << " : " << item.size() << std::endl;
			i++;
			sum += item.size();
		}

		std::cout << "SUMM of Levels : " << sum << std::endl;
	}*/


};

/*
class CacheItem
{
public:
	size_t FParent;
	FILEDATA FFileData;
	ullong FFullFileSize;
	size_t FLevel;

	void Serialize(LogEngine::TStream& os)
	{
		os << FParent;
		os << FFileData.dwFileAttributes;
		os << FFileData.ftCreationTime.dwHighDateTime;
		os << FFileData.ftCreationTime.dwLowDateTime;
		os << FFileData.ftLastAccessTime.dwHighDateTime;
		os << FFileData.ftLastAccessTime.dwLowDateTime;
		os << FFileData.ftLastWriteTime.dwHighDateTime;
		os << FFileData.ftLastWriteTime.dwLowDateTime;
		os << FFileData.nFileSizeHigh;
		os << FFileData.nFileSizeLow;
		os << FLevel;
		size_t len = _tcslen(FFileData.cFileName) * sizeof(FFileData.cFileName[0]); // size in bytes
		assert(len < MAX_PATH);
		os << len;
		os.Write(FFileData.cFileName, len);
	}

	void Deserialize(LogEngine::TStream& is)
	{
		is >> FParent;
		is >> FFileData.dwFileAttributes;
		is >> FFileData.ftCreationTime.dwHighDateTime;
		is >> FFileData.ftCreationTime.dwLowDateTime;
		is >> FFileData.ftLastAccessTime.dwHighDateTime;
		is >> FFileData.ftLastAccessTime.dwLowDateTime;
		is >> FFileData.ftLastWriteTime.dwHighDateTime;
		is >> FFileData.ftLastWriteTime.dwLowDateTime;
		is >> FFileData.nFileSizeHigh;
		is >> FFileData.nFileSizeLow;
		FFullFileSize = MAKE_FULL_FILESIZE(FFileData.nFileSizeHigh, FFileData.nFileSizeLow);
		is >> FLevel;
		size_t lenBytes;
		is >> lenBytes;
		assert(lenBytes < MAX_PATH);
		//FName.resize(lenBytes / sizeof(FName[0])); // convert from bytes to chars
		is.Read(FFileData.cFileName, lenBytes);
	}
};
*/