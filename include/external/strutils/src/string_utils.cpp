
#include <Windows.h>
#include <format>
#include <stdexcept>

#include "../include/string_utils.h"


// special non-template function for wchar_t*
template<>
std::string wtos<wchar_t*>(const pwchar_t& wstr)
{
    std::wstring wwstr(wstr);
    int len = WideCharToMultiByte(CP_UTF8, 0 /*WC_NO_BEST_FIT_CHARS*/, wwstr.data(), (int)wwstr.length(), nullptr, 0, nullptr, nullptr);

    if (len == 0)
        throw std::runtime_error(std::format("Error in WideCharToMultiByte 1. Error code: {}", GetLastError()));

    std::string dest;
    dest.resize(len);
    int err = WideCharToMultiByte(CP_UTF8, 0 /*WC_NO_BEST_FIT_CHARS*/, wwstr.data(), (int)wwstr.size(), dest.data(), len, nullptr, nullptr);

    if (!err)
        throw std::runtime_error(std::format("Error in WideCharToMultiByte 2. Error code: {}", GetLastError()));

    return dest;
}

std::wstring stow(const std::string& str)
{
    int bufferSize = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0); // Get the required buffer size
    if (bufferSize == 0)
        throw std::runtime_error(std::format("Error in WideCharToMultiByte 1. Error code: {}", GetLastError()));

    std::wstring wstr(bufferSize, 0);

    int result = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], bufferSize);
    if (result == 0)
        throw std::runtime_error(std::format("Error in WideCharToMultiByte 2. Error code: {}", GetLastError()));

    wstr.resize(bufferSize - 1);
    return wstr;
}

