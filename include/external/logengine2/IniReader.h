/*
 * IniReader.h
 *
 * Copyright 2025, LogEngine2 Project. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#pragma once

#include <string>
#include <fstream>
#include "Common.h"
#include "DynamicArrays.h"


#define FIO_EXCEPTION_PREFIX "Ini file exception : "

class FileException : public std::exception
{
public:
	FileException(const char* Message) { Text = Message; whatText = FIO_EXCEPTION_PREFIX + std::string(Message); }
	FileException(const std::string& Message) { Text = Message; whatText = FIO_EXCEPTION_PREFIX + Message; }
	FileException(const FileException& ex) { Text = ex.Text; whatText = ex.whatText; }
	~FileException() noexcept override {}
	FileException& operator=(const FileException& ex) { Text = ex.Text; whatText = ex.whatText;	return *this; }
	const char* what() const noexcept override { return whatText.c_str(); }
private:
	std::string Text;
	std::string whatText;
};

#define MISSING_SECTION "__MISSING_SECTION__"

/// /////////////////////////////////////////////
/// TODO
/// Limitation to symbols in section names? in parameter name? Can parameter name contain space? 
/// multiline values?
/// proper error reporting? (missing first mandatory section, missing = in a line)
/// comments in the end of line containing parameter, for example:
///      VerParam=v2  #this is version number of xxx

class IniReader
{
public:
	using ValueType = THArray<std::string>;
	using StorageType = THash2<std::string, std::string, ValueType, CompareStringNCase>;
private:
	StorageType FInidata;

	template<class P>
	class IniReaderIterator //: public std::iterator<std::input_iterator_tag, T>
	{
	public:
		using value_type = typename P::item_type;
		using iterator_category = std::random_access_iterator_tag;
		using difference_type = ptrdiff_t;
		using pointer = typename P::pointer;
		using reference = typename P::reference;

		IniReaderIterator(const IniReaderIterator& it) : FIniRd(it.FIniRd), FEdge(it.FEdge), FIter(it.FIter) {}
		IniReaderIterator(P* rd, int edge) : FIniRd(rd), FEdge(edge), FIter(edge==0 ? rd->FInidata.GetAIndexes().begin(): rd->FInidata.GetAIndexes().end()) { }

		bool operator!=(IniReaderIterator const& other) const { return FIniRd != other.FIniRd || FIter != other.FIter; }
		bool operator==(IniReaderIterator const& other) const { return FIniRd == other.FIniRd && FIter == other.FIter; }
		reference operator*() const { return *FIter; }
		IniReaderIterator& operator++() { ++FIter; return *this; }
		//THArrayIterator& operator--() { if (FPtr != FCont->FBegin) --FPtr; return *this; }
	private:
		P* FIniRd{ nullptr };
		int FEdge;
		
		StorageType::KeysArray::iterator FIter;
		pointer FPtr{ nullptr };
	};
private:
	void SetValue(const std::string& Key1, const std::string& Key2, const std::string& Value)
	{
		auto p = FInidata.GetValuePointer(Key1, Key2);
		if (p == nullptr)
		{
			ValueType v;
			v.AddValue(Value);
			FInidata.SetValue(Key1, Key2, v);
		}
		else
		{
			p->AddValue(Value);
			//auto v = inidata.GetValue(Key1, Key2);
		}
	}

public:
	using iterator = IniReaderIterator<IniReader>;
	using const_iterator = IniReaderIterator<const IniReader>;
	using item_type = StorageType::KeyType;
	using pointer = StorageType::KeyType*;
	using reference = StorageType::KeyType&;

	iterator begin() { return iterator(this, 0); }
	iterator end() { return iterator(this, 1); }

	IniReader() { }

	IniReader(std::string& fileName) { LoadIniFile(fileName); }

	void LoadIniFile(const std::string& fileName)
	{
		FInidata.Clear();

		std::ifstream fin(fileName, std::ios::in /* | std::ios::binary*/);

		if (fin.fail())
			throw FileException("Cannot open file '" + fileName + "' for reading.");

		std::string line;
		std::string leftSide, rightSide;
		std::string::size_type length;
		std::string section = MISSING_SECTION;
		uint paramCount = 0;
		
		while (!std::getline(fin, line).fail())
		{
			/* if the beginning of line contains a # or ; then it is a comment
			if we don't find = in the line we assume that entire line is a parameter name with empty value
			lines containing spaces and tabs only are bypassed (empty lines)
			*/

			// checking if line is a comment
			line = Trim(line);
			if (line.size() == 0) continue; // empty line, go to next line
			if (line[0] == '#' || line[0] == ';') continue; // bypass lines starting from # and ; sine they are comments

			if (line[0] == '[')
			{
				if (paramCount == 0 && section != MISSING_SECTION) // sections without parameters may exist, add it to the hash when next section is met
					FInidata.SetValue(section);

				length = line.find(']');
				section = TrimSPCRLF(line.substr(1, length - 1)); // name of the section
				paramCount = 0; // reset parameters counter for the section
				continue;
			}


			// check the command and handle it
			length = line.find('=');
			if (length == std::string::npos)
			{
				leftSide = line; // if line does not contain '=', we consider that entire line is parameter name 
				rightSide = "";
			}
			else
			{
				leftSide = line.substr(0, length);
				rightSide = line.substr(length + 1, line.size() - length);
			}

			// if section 'section' does not exist it will be added by this SetValue call 
			SetValue(section, TrimSPCRLF(leftSide), TrimSPCRLF(rightSide)); // trimSPCRLF is required here for Linux since its \n and \r differ from Windows.
			paramCount++;
		}

		// case when empty section is the last section in a file
		if (paramCount == 0 && section != MISSING_SECTION)
			FInidata.SetValue(section);
	}

	// removes leading/trailing spaces/tabs from section and key parameters before look for the value
	std::string GetValue(const std::string& section, const std::string& key, const std::string& defaultValue = "<no_value>", uint index = 0u)
	{
		ValueType* p = FInidata.GetValuePointer(TrimSPCRLF(section), TrimSPCRLF(key));
		if (p == nullptr)
			return defaultValue;
		else
			return p->GetValue(index);
	}

	uint SectionsCount()
	{
		return FInidata.GetAIndexes().Count();
	}

	StorageType::ValuesHash& GetSection(std::string& section)
	{
		return FInidata.GetValue(TrimSPCRLF(section));
	}

	bool HasSection(const std::string& section)
	{
		return FInidata.GetValuePointer(TrimSPCRLF(section)) != nullptr;
	}

	uint ValuesCount(const std::string& section)
	{
		auto p = FInidata.GetValuePointer(TrimSPCRLF(section));
		if (p == nullptr)
			return 0;  // just return zero if section is not found 
		//else
		//	return p->Count();

		uint result = 0;

		auto keys = p->GetKeys();
		for (uint i = 0; i < keys.Count(); i++)
		{
			auto val = p->GetValue(keys[i]);
			result += val.Count();
		}

		return result;
	}

	bool HasValue(const std::string& section, const std::string& key)
	{
		auto p = FInidata.GetValuePointer(TrimSPCRLF(section), TrimSPCRLF(key));
		return (p != nullptr) && (p->Count() > 0);
	}

};
