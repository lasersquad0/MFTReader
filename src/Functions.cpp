
// this is to remove defines min, max in windows headers because they conflict with std::min std::max 
#define NOMINMAX

#include "Debug.h"
#include <windows.h>
#include <shlwapi.h>
#include <string>

#include "strutils/include/string_utils.h"
#include "logengine2/LogEngine.h"
#include "Functions.h"

//TODO think of better solution of FUNC logger
LogEngine::Logger& GetLoggerFunc()
{
    LogEngine::Logger& logger = LogEngine::GetFileLogger(MFT_LOGGER_NAME_FUNC, "LogMFTReaderFUNC.log");
    //logger.SetAsyncMode(true);
    logger.SetLogLevel(LogEngine::Levels::llInfo);
    return logger;
}

// removes all leading and trailing \n\t\r and space symbols from string
// works with std::wstring ONLY because std::string version already present inLogEngine
static std::wstring TrimSPCRLF(std::wstring str) // str passed by value here intentionally
{
    // remove any leading and traling spaces, tabs and \n, \r.
    size_t strBegin = str.find_first_not_of(L" \t\r\n");
    if (strBegin == std::string::npos) return L"";

    size_t strEnd = str.find_last_not_of(L" \t\r\n");
    assert(strEnd != std::string::npos);

    str.erase(strEnd + 1 /*, S.size() - strEnd*/); // erase till end of string
    str.erase(0, strBegin);

    return str;
}

// Supports both 10based mftRecID and hex format. 
MFTRecIndex StringToMFTRecID(const string_t& strMFTRecID)
{
    auto recIdStr = TrimSPCRLF(strMFTRecID);
    
    if (recIdStr.size() > 1 && recIdStr[0] == '0' && (recIdStr[1] == 'x' || recIdStr[1] == 'X'))
        return std::stoul(recIdStr, nullptr, 16); // exception will be thrown if option value cannot be converted into uint
    else
        return std::stoul(recIdStr);
}

//This function is not fully compatible with FILE_ATTR_FLAGS enum
std::string FormatFileAttributes(uint32_t a)
{
    std::string s = "-----------------"; // 17 chars

    if (a & FILE_ATTRIBUTE_READONLY)               s[0] = 'R'; // READONLY
    if (a & FILE_ATTRIBUTE_HIDDEN)                 s[1] = 'H'; // HIDDEN
    if (a & FILE_ATTRIBUTE_SYSTEM)                 s[2] = 'S'; // SYSTEM
    
    if ((a & FILE_ATTRIBUTE_DIRECTORY) ||
       (a & (uint32_t)FILE_ATTR_FLAGS::DIRECTORY)) s[3] = 'D'; // DIRECTORY

    if (a & FILE_ATTRIBUTE_ARCHIVE)                s[4] = 'A'; // ARCHIVE
    if (a & FILE_ATTRIBUTE_NORMAL)                 s[5] = 'N'; // NORMAL
    if (a & FILE_ATTRIBUTE_TEMPORARY)              s[6] = 'T'; // TEMPORARY
    if (a & FILE_ATTRIBUTE_SPARSE_FILE)            s[7] = 's'; // SPARSE (lowercase for diff)
    if (a & FILE_ATTRIBUTE_REPARSE_POINT)          s[8] = 'r'; // REPARSE (lowercase for diff)
    if (a & FILE_ATTRIBUTE_COMPRESSED)             s[9] = 'C'; // COMPRESSED
    if (a & FILE_ATTRIBUTE_OFFLINE)                s[10] = 'O'; // OFFLINE
    if (a & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED)    s[11] = 'I'; // NOT INDEXED
    if (a & FILE_ATTRIBUTE_ENCRYPTED)              s[12] = 'E'; // ENCRYPTED
    if (a & FILE_ATTRIBUTE_INTEGRITY_STREAM)       s[13] = 'P'; // INTEGRITY The directory or user data stream is configured with integrity (only supported on ReFS volumes)
    if (a & FILE_ATTRIBUTE_NO_SCRUB_DATA)          s[14] = 'U'; // NO SCRUB
    if (a & FILE_ATTRIBUTE_EA)                     s[15] = 'L'; // EA
    if (a & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)  s[16] = 'X'; // RECALL ON ACCESS
    /* FILE_ATTRIBUTE_DEVICE
         FILE_ATTRIBUTE_PINNED
         FILE_ATTRIBUTE_UNPINNED
         FILE_ATTRIBUTE_VIRTUAL
         FILE_ATTRIBUTE_RECALL_ON_OPEN
         FILE_ATTRIBUTE_STRICTLY_SEQUENTIAL*/
    return s;
}

#define HIDWORD(_) static_cast<uint32_t>(((uint64_t)(_)) >> 32)
#define LODWORD(_) static_cast<uint32_t>(_)

string_t FileDateToString(uint64_t dateTime)
{
    if (dateTime == 0)
        return _T("--never--");

    const uint BUF_SZ = 100;
    char_t buf[BUF_SZ];
    DWORD dateTimeFlags = /*FDTF_DEFAULT */ FDTF_SHORTDATE | FDTF_LONGTIME | FDTF_NOAUTOREADINGORDER;
    FILETIME ft{ 0 };

    ft.dwLowDateTime = LODWORD(dateTime);
    ft.dwHighDateTime = HIDWORD(dateTime);
    SHFormatDateTime(&ft, &dateTimeFlags, buf, BUF_SZ);

    return buf;
}


string_t GetVolumeName(const string_t& path)
{
    string_t vol;
    vol.resize(MAX_PATH);

    BOOL r = GetVolumePathName(path.c_str(), vol.data(), MAX_PATH);
    UNREFERENCED_PARAMETER(r);
    assert(r != 0);

    vol.resize(vol.find(_T('\0')));
    if (vol[vol.size() - 1] == '\\')
    {
        vol[vol.size() - 1] = _T('\0');
        vol.resize(vol.size() - 1);
    }

    return vol;
}

string_t AttrFlagToString(uint32_t attrFlag)
{
    if (attrFlag == 0) return _T(""); // this standard attribute

    string_t res;

    if ((attrFlag & ATTR_FLAG_COMPRESSED) > 0) res += _T(" COMPRESSED");
    if ((attrFlag & ATTR_FLAG_SPARSED) > 0)    res += _T(" SPARSED");
    if ((attrFlag & ATTR_FLAG_ENCRYPTED) > 0)  res += _T(" ENCRYPTED");

    return res;
}