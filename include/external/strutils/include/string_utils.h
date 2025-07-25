/* 
*   Various useful strings utilites. 
* 
*/

#pragma once

// this is to remove defines min, max in windows headers because they conflict with std::min std::max 
#define NOMINMAX

#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

std::wstring stow(const std::string& str);
//std::string MillisecToStr(long long ms);

#if defined(UNICODE) || defined(_UNICODE)
#define U(quote) L##quote  
typedef wchar_t char_t;
typedef std::wstring string_t;
typedef std::wstringstream stringstream_t;
#define to_string_t std::to_wstring
#else
#define U(quote) quote  
typedef char char_t; 
typedef std::string string_t;
typedef std::stringstream stringstream_t;
#define to_string_t std::to_string
#endif


template<class WSTRING>
std::string wtos(const WSTRING& wstr)
{
    // make sure that STRING is one of instantiations of std::wstring
    static_assert(std::is_base_of<std::basic_string<typename WSTRING::value_type, typename WSTRING::traits_type>, WSTRING>::value);
    static_assert(std::is_same_v<typename WSTRING::value_type, wchar_t>);

    if (wstr.size() == 0) return "";

    int len = WideCharToMultiByte(CP_UTF8, 0 /*WC_NO_BEST_FIT_CHARS*/, wstr.data(), (int)wstr.length(), nullptr, 0, nullptr, nullptr);

    if (len == 0)
        throw std::runtime_error(std::format("Error in WideCharToMultiByte 1. Error code: {}", GetLastError()));

    std::string dest;
    dest.resize(len);
    int err = WideCharToMultiByte(CP_UTF8, 0 /*WC_NO_BEST_FIT_CHARS*/, wstr.data(), (int)wstr.size(), dest.data(), len, nullptr, nullptr);

    if (!err)
        throw std::runtime_error(std::format("Error in WideCharToMultiByte 2. Error code: {}", GetLastError()));

    return dest;
}

typedef wchar_t* pwchar_t; // needed for proper specialization for wchar_t*

// special non-template function for wchar_t*
template<>
std::string wtos<wchar_t*>(const pwchar_t& wstr);

// makes conversion between string and wstring back and forth
template<typename T>
constexpr std::basic_string<T> convert_string(const std::filesystem::path& str)
{
    if constexpr (std::is_same_v<T, char>)
    {
        return str.string();
    }
    else if (std::is_same_v<T, wchar_t>)
    {
        return str.wstring();
    }
    /*else if (std::is_same_v<T, char16_t>) {
        return str.u16string();
    }
    else if (std::is_same_v<T, char32_t>) {
        return str.u32string();
    } */
}

template<class STRING>
STRING MillisecToStr(uint64_t ms)
{
    // make sure that STRING is one of instantiations of std::string
    static_assert(std::is_base_of<std::basic_string<typename STRING::value_type, typename STRING::traits_type>, STRING>::value);

    uint32_t milliseconds = ms % 1000;
    uint32_t seconds = (ms / 1000) % 60;
    uint32_t minutes = (ms / 60000) % 60;
    uint32_t hours = (ms / 3600000) % 24;

    STRING result;

    if constexpr (std::is_same_v<typename STRING::value_type, char>)
    {
        if (hours > 0)
            result = std::format("{} hours {} minutes {} seconds {} ms", hours, minutes, seconds, milliseconds);
        else if (minutes > 0)
            result = std::format("{} minutes {} seconds {} ms", minutes, seconds, milliseconds);
        else
            result = std::format("{} seconds {} ms", seconds, milliseconds);
    }
    else if constexpr (std::is_same_v<typename STRING::value_type, wchar_t>)
    {
        if (hours > 0)
            result = std::format(L"{} hours {} minutes {} seconds {} ms", hours, minutes, seconds, milliseconds);
        else if (minutes > 0)
            result = std::format(L"{} minutes {} seconds {} ms", minutes, seconds, milliseconds);
        else
            result = std::format(L"{} seconds {} ms", seconds, milliseconds);
    }
    else
        result = "UNKNOWN STRING TYPE";

    return result;
}

/// converts any integer type into a string with group separator applied.
/// group separator is defined by MyGroupSeparator class
/// string_t can be either std::string or std::wstring
template<typename IntType>
std::string toStringSepA(IntType v)
{
    static_assert(std::is_integral<IntType>::value);

    // helper class for integer formatting functions
    struct MyGroupSeparator : std::numpunct<char>
    {
        char do_thousands_sep() const override { return ' '; } // thousands separator
        std::string do_grouping() const override { return "\3"; } // group by 3
    };

    std::stringstream ss;
    ss.imbue(std::locale(ss.getloc(), new MyGroupSeparator()));
    ss << v;  // printing to string stream with formating
    return ss.str();
}

/// converts any integer type into a string with group separator applied.
/// group separator is defined by MyGroupSeparator class
/// string_t can be either std::string or std::wstring
template<typename IntType>
std::wstring toStringSepW(IntType v)
{
    static_assert(std::is_integral<IntType>::value);

    // helper class for integer formatting functions
    struct MyGroupSeparator : std::numpunct<wchar_t>
    {
        wchar_t do_thousands_sep() const override { return L' '; } // thousands separator
        std::string do_grouping() const override { return "\3"; } // group by 3
    };

    std::wstringstream ss;
    ss.imbue(std::locale(ss.getloc(), new MyGroupSeparator()));
    ss << v;  // printing to string stream with formating
    return ss.str();
}

// split string into array of strings using Delim as delimiter
template<class STRING>
void StringToArray(const STRING& str, std::vector<STRING>& arr, const typename STRING::value_type Delim = '\n')
{
    // make sure that STRING is one of instantiations of std::string
    static_assert(std::is_base_of<std::basic_string<typename STRING::value_type, typename STRING::traits_type>, STRING>::value);

    size_t i = 0;
    size_t len = str.length();
    STRING s;
    s.reserve(len);

    while (i < len) //TODO mabe we can use here one while loop instead of two ?
    {
        s.clear();
        while (i < len)
        {
            if (str[i] == Delim)
            {
                i++;
                break;
            }
            s += str[i++];
        }

        if (s.length() > 0)
            arr.push_back(s);
    }
}

// splits string to array of strings using Delim as delimiter
template<class STRING>
void StringToArrayAccum(const STRING& str, std::vector<STRING>& arr, const typename STRING::value_type Delim = '\n')
{
    // make sure that STRING is one of instantiations of std::string
    static_assert(std::is_base_of<std::basic_string<typename STRING::value_type, typename STRING::traits_type>, STRING>::value);

    size_t i = 0;
    size_t len = str.length();
    STRING s;
    s.reserve(len);

    while (i < len)
    {
        if (str[i] == Delim)
            if (s.length() > 0) arr.push_back(s);
        s += str[i++];

    }

    if (s.length() > 0)
        arr.push_back(s);
}


template <typename T>
class Singleton
{
public:
    template <typename... Args>
    static T* getInstance(Args&&... args)
    {
        if (!FInstance)
        {
            FInstance = new T(std::forward<Args>(args)...);
        }
        return FInstance;
    }

    static void Release()
    {
        delete FInstance;
        FInstance = nullptr;
    }

private:
    Singleton() = default;
    static T* FInstance;
};

template <typename T>
T* Singleton<T>::FInstance = nullptr;

