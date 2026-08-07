#pragma once


#define OPT_M _T("m") // "MFT Record ID"
//#define OPT_V _T("v") // "Volume"
#define OPT_P _T("p") // "Path"
#define OPT_S _T("s") // "Statistic"
#define OPT_C _T("c") // "Cache for file search"
#define OPT_T _T("t") // "Testing" - for testing purposes

#define MFT_LOG_CFG_FILENAME "MFTReader.lfg"
#define MFT_LOG_FILENAME "LogMFTReader.log"

void PrintUsage(COptionsList& options);
void DefineOptions(COptionsList& options);
void InitLogger();
