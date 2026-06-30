
#include <windows.h>
#include <format>
#include <stdexcept>
#include <cassert>

#include "../include/string_utils.h"


// special non-template function for wchar_t*
template<>
std::string wtos<wchar_t*>(const pwchar_t& wstr)
{
    std::wstring wwstr(wstr);
    int len = WideCharToMultiByte(CP_UTF8, 0 /*WC_NO_BEST_FIT_CHARS*/, wwstr.data(), (int)wwstr.length(), nullptr, 0, nullptr, nullptr);

    if (len == 0)
        throw std::runtime_error(std::format("Error in WideCharToMultiByte 1. Error code: {}", GetLastError()));

    std::string dest(len, 0);
    //dest.resize(len);
    int err = WideCharToMultiByte(CP_UTF8, 0 /*WC_NO_BEST_FIT_CHARS*/, wwstr.data(), (int)wwstr.size(), dest.data(), len, nullptr, nullptr);

    if (!err)
        throw std::runtime_error(std::format("Error in WideCharToMultiByte 2. Error code: {}", GetLastError()));

    return dest;
}

// special non-template function for char*
template<>
std::wstring stow<char*>(const pchar_t& str)
{
    int bufferSize = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0); // Get the required buffer size
    if (bufferSize == 0)
        throw std::runtime_error(std::format("Error in MultiByteToWideChar 1. Error code: {}", GetLastError()));

    std::wstring wstr(bufferSize, 0);

    int result = MultiByteToWideChar(CP_UTF8, 0, str, -1, &wstr[0], bufferSize);
    if (result == 0)
        throw std::runtime_error(std::format("Error in MultiByteToWideChar 2. Error code: {}", GetLastError()));

    wstr.resize(bufferSize - 1);
    return wstr;
}

static void Trim(std::string& str) //Note: str will be changed
{
    // remove any leading and traling spaces and tabs.
    size_t strBegin = str.find_first_not_of(" \t");
    if (strBegin == std::string::npos) return;

    size_t strEnd = str.find_last_not_of(" \t");
    assert(strEnd != std::string::npos);

    str.erase(strEnd + 1 /*, str.size() - strEnd*/);
    str.erase(0, strBegin);
}

static char mytoupper(int c) // to eliminate compile warning "warning C4244: '=': conversion from 'int' to 'char', possible loss of data"
{
    return (char)toupper(c);
}

void TrimAndUpper(std::string& str) //Note: str will be changed
{
    Trim(str);

    // to uppercase
    transform(str.begin(), str.end(), str.begin(), ::mytoupper);
}

