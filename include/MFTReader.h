#pragma once


#define OPT_R _T("r")   // "by Record ID"
#define OPT_P _T("p")   // "by Path"
#define OPT_S _T("s")   // "collect Statistic"
#define OPT_C _T("c")   // "build Cache for file search"
#define OPT_T _T("t")   // "Testing" - for testing purposes

#define MFT_LOG_CFG_FILENAME "MFTReader.lfg"
#define MFT_LOG_FILENAME "LogMFTReader.log"

void PrintUsage(COptionsList& options);
void DefineOptions(COptionsList& options);
void InitLogger();
