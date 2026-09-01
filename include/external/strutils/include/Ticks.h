#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include <chrono>
#include <iostream>
#include "string_utils.h"

template <class I, class V>
class TTickHash
{
private:
	template<class Hash>
	class TTickHashIter
	{
	public:
		using value_type = std::pair<typename Hash::KeyType&, typename Hash::ValueType&>;

		TTickHashIter(Hash* hash, uint32_t curr) : FHash(hash), FCurIndex(curr) {}

		bool operator==(TTickHashIter const& other) const { return FHash == other.FHash && FCurIndex == other.FCurIndex; }
		TTickHashIter& operator++() { if (FCurIndex < FHash->Count()) ++FCurIndex; return *this; }

		value_type operator*() const { return value_type{ FHash->FAKeys[FCurIndex], FHash->FAValues[FCurIndex] }; }
	private:
		Hash* FHash{ nullptr };
		uint32_t FCurIndex{ 0 };
	};

public:
	using KeyType = I;
	using ValueType = V;
	using KeysType = std::vector<I>;
	using ValuesType = std::vector<V>;
	using iterator = TTickHashIter<TTickHash>;
protected:
	KeysType FAKeys;
	ValuesType FAValues;
public:
	virtual ~TTickHash() {}

	iterator begin() { return iterator(this, 0); }
	iterator end() { return iterator(this, Count()); }

	uint32_t Count() const { return (uint32_t)FAKeys.size(); }

	// operator is used for reading value from hash  
	const V& operator[](const I& Key) const 
	{ 
		auto iter = std::find(FAKeys.begin(), FAKeys.end(), Key);
		if (iter == FAKeys.end())
		{
			throw std::exception("TTickHash<I,V>:operator[Key] : Key not found !");
		}

		return FAValues[std::distance(FAKeys.begin(), iter)];
	}

	// this operator is mostly used for writing values into hash. can be used instead of SetValue()
	// e.g. hash[5] = value;
	V& operator[](const I& Key)
	{
		auto iter = std::find(FAKeys.begin(), FAKeys.end(), Key);
		if (iter == FAKeys.end())
		{
			FAKeys.push_back(Key);
			return FAValues.emplace_back();
		}

		return FAValues[std::distance(FAKeys.begin(), iter)];
	}

	bool IfExists(const I& Key) const { return std::find(FAKeys.begin(), FAKeys.end(), Key) != FAKeys.end(); }
};

class Ticks
{
public:
	typedef std::chrono::steady_clock::time_point timepoint;

private:
	static inline TTickHash<string_t, timepoint> s;
	static inline TTickHash<string_t, timepoint> f;

public:

	static void Start(const string_t& tickName)
	{
		s[tickName] = std::chrono::steady_clock::now();
	}
	//TODO think of performance of this function because too many time we make search in map by Key
	static long long Finish(const string_t& tickName)
	{
		f[tickName] = std::chrono::steady_clock::now();
		if (!s.IfExists(tickName)) s[tickName] = f[tickName];
		return GetTick(tickName);
	}
	// generates an exception if s.at(tickName) does not exist in s map.
	static long long GetTick(const string_t& tickName)
	{
		timepoint& fin = f[tickName];
		return std::chrono::duration_cast<std::chrono::milliseconds>(fin - s[tickName]).count();
	}
	static void PrintCon(long factor)
	{
		for (auto item : f)
		{
			cout_t << item.first << U(" = ") << std::chrono::duration_cast<std::chrono::milliseconds>(item.second - s[item.first]).count() / factor << std::endl;
		}
	}
	static void Print(std::basic_iostream<char_t>& stream, long factor)
	{
		for (auto item : f)
		{
			stream << item.first << U(" = ") << std::chrono::duration_cast<std::chrono::milliseconds>(item.second - s[item.first]).count() / factor << std::endl;
		}
	}

	static void PrintTime()
	{
		size_t maxLen = 0;
		for (auto item : f)
		{
			if (maxLen < item.first.size())
				maxLen = item.first.size();
		}

		for (auto item : f)
		{
			cout_t << std::format(U("{:<{}} = {}"), item.first, maxLen, MillisecToStr<string_t>(std::chrono::duration_cast<std::chrono::milliseconds>(item.second - s[item.first]).count())) << std::endl;
		}
	}
	
};

