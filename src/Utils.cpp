
// this is to remove defines min, max in windows headers because they conflict with std::min std::max 
#define NOMINMAX

#include <string>
#include <windows.h>
#include <strsafe.h>

#include "Utils.h"


std::string GetErrorMessageTextA(ulong lastErrorCode, const std::string& errorPlace)
{
    //std::string errMsg = std::system_category().message(lastErrorCode);

    return std::format("[{}] failed with error code {}", errorPlace, lastErrorCode);

   /* const uint32_t BUF_SIZE = 2048; // should be enough for all error messages
    std::string buf, buf2;
    buf.resize(BUF_SIZE);
    buf2.resize(BUF_SIZE);

    BOOL_CHECK(FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, lastError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)buf.data(), BUF_SIZE, nullptr));

    //TODO better solution: return std::format(_T("%s failed with error code %d as follows:\n%s"), errorPlace, lastError, buf);

    HR_CHECK(StringCchPrintfA(buf2.data(), buf2.size(), "%s failed with error code %d as follows:\n%s", errorPlace.c_str(), lastError, buf.data()));

    return buf2;*/
}

string_t GetErrorMessageText(ulong lastErrorCode, const string_t& errorPlace)
{
    const uint32_t BUF_SIZE = 2048; // should be enough for all error messages
    string_t buf, buf2;
    buf.resize(BUF_SIZE);
    buf2.resize(BUF_SIZE);

    BOOL_CHECK(FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, lastErrorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)buf.data(), BUF_SIZE, nullptr));

    //TODO better solution: return std::format(_T("%s failed with error code %d as follows:\n%s"), errorPlace, lastError, buf);

    HR_CHECK(StringCchPrintf(buf2.data(), buf2.size(), TEXT("%s failed with error code %d as follows:\n%s"), errorPlace.c_str(), lastErrorCode, buf.data()));

    return buf2;
}

/*
void PrintWindowsErrorMessage(const std::string& errorPlace)
{
    LPSTR lpMsgBuf;
    DWORD err = GetLastError();

    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&lpMsgBuf,
        0, NULL);

    //wcout << (TCHAR*)lpMsgBuf << endl;

    auto res = std::format("[{}] failed with error {}: {}", errorPlace, err, lpMsgBuf);

    //LPVOID lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));
    //StringCchPrintf((LPTSTR)lpDisplayBuf, LocalSize(lpDisplayBuf) / sizeof(TCHAR), _T("%s failed with error %d: %s"), lpszFunction, err, lpMsgBuf);

    GET_LOGGER;
    logger.Error(res);
    //wcout << (TCHAR*)lpDisplayBuf << endl;

    //LocalFree(lpDisplayBuf);
    LocalFree(lpMsgBuf);
}
*/

// assuming that dest buffer is large enough
void WCHARtoChar(char* dest, wchar_t* src)
{
   // int len = WideCharToMultiByte(CP_UTF8, 0 /*WC_NO_BEST_FIT_CHARS*/, src, -1, dest, MAX_PATH, nullptr, nullptr);

    //size_t len;
    WideCharToMultiByte(CP_ACP, 0, src, -1, dest, MAX_PATH, nullptr, nullptr);
    //wcstombs_s(&len, dest, MAX_PATH, src, wcslen(src));
    //if (len > 0u) dest[len] = '\0';
}

// assuming that dest buffer is large enough
void CharToWCHAR(wchar_t* dest, const char* src)
{
    // size_t len;
    MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, src, -1, dest, MAX_PATH);
    // mbstowcs_s(&len, dest, MAX_PATH, src, strlen(src));
    // if (len > 0u) dest[len] = '\0';
}

