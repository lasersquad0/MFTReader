// The following ifdef block is the standard way of creating macros which make exporting
// from a DLL simpler. All files within this DLL are compiled with the MFTREADERDLL_EXPORTS
// symbol defined on the command line. This symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see
// MFTREADERDLL_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
// this macro is defined in project settings
#ifdef MFTREADERDLL_EXPORTS
#define MFTREADERDLL_API extern "C" __declspec(dllexport)
#else
#define MFTREADERDLL_API __declspec(dllimport)
#endif

#include <cstdint>

//extern MFTREADERDLL_API int nMFTReaderDLL;

//#define LOGGER_NAME "mftlogdll"

//#define LOG_INFO(_)  LogEngine::GetLogger(LOGGER_NAME).Info(_)
//#define LOG_WARN(_)  LogEngine::GetLogger(LOGGER_NAME).Warn(_)

struct TError
{
	int32_t ErrCode;
	wchar_t ErrText[256];
};

MFTREADERDLL_API TError ReadVolume(wchar_t* volume, uint64_t* volSize, uint32_t* count, uint32_t** data);
