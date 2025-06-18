
// this is to remove defines min, max in windows headers because they conflict with std::min std::max 
#define NOMINMAX

#include <Windows.h>

#include "Utils.h"

// assuming that dest buffer is large enough
void WCHARtoChar(char* dest, wchar_t* src)
{
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
