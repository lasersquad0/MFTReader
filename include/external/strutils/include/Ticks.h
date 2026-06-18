#pragma once

#include <unordered_map>
#include <string>
#include <chrono>
#include <iostream>
#include "string_utils.h"

//std::string millisecToStr(long long ms);

class Ticks
{
public:
	typedef std::chrono::steady_clock::time_point timepoint;

private:
	static inline std::unordered_map<string_t, timepoint> s;
	static inline std::unordered_map<string_t, timepoint> f;

public:

	static void Start(const string_t& tickName)
	{
		s[tickName] = std::chrono::steady_clock::now();
	}
	static long long Finish(const string_t& tickName)
	{
		f[tickName] = std::chrono::steady_clock::now();
		return GetTick(tickName);
	}
	static long long GetTick(const string_t& tickName)
	{
		timepoint& fin = f.at(tickName);
		return std::chrono::duration_cast<std::chrono::milliseconds>(fin - s.at(tickName)).count();
	}
	static void PrintCon(long factor)
	{
		for (auto& item : f)
		{
			std::wcout << item.first << U(" = ") << std::chrono::duration_cast<std::chrono::milliseconds>(item.second - s.at(item.first)).count() / factor << std::endl;
		}
	}
	static void Print(std::basic_iostream<char_t>& stream, long factor)
	{
		for (auto& item : f)
		{
			stream << item.first << U(" = ") << std::chrono::duration_cast<std::chrono::milliseconds>(item.second - s.at(item.first)).count() / factor << std::endl;
		}
	}
	
};

