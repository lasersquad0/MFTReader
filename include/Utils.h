#pragma once

#include <iostream>
#include <string>

#include "strutils/include/string_utils.h"
#include "logengine2/DynamicArrays.h"

//TODO think how can we report here error in general way for non-console applications
#define LOG_CHECK_ERROR(_msg) std::cout << (_msg)
//#define LOG_CHECK_ERROR(_msg) logger.Error(_msg)

#define MAKE_LOG_TSTR(_msg) string_t(_T(__FILE__)).append(_T(" : ")).append(_msg)
#define MAKE_LOG_STR(_msg) std::string(__FILE__).append(" : ").append(__func__).append(" : ").append(std::to_string(__LINE__)).append(" - ").append(_msg)

#define HR_CHECK(_hr) {if(FAILED(_hr)) LOG_CHECK_ERROR( MAKE_LOG_STR(std::system_category().message(_hr)));}
#define BOOL_CHECK(_res) {if(!(_res)) LOG_CHECK_ERROR( MAKE_LOG_STR(std::system_category().message(GetLastError())));}


void WCHARtoChar(char* dest, wchar_t* src);
void CharToWCHAR(wchar_t* dest, const char* src);
std::string GetErrorMessageTextA(ulong lastError, const std::string& errorPlace);
string_t GetErrorMessageText(ulong lastError, const string_t& errorPlace);
//void PrintWindowsErrorMessage(const std::string& functionName);


