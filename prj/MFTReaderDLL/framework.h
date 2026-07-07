#pragma once

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

#include "Debug.h"

#include <iostream>
#include <string>
#include <cstdint>
#include <algorithm>
#include <stdexcept>

#include <windows.h>
#include <shlwapi.h>
#include <strutils/include/string_utils.h>
#include <strutils/include/Ticks.h>
#include "logengine2/DynamicArrays.h"
#include "logengine2/FileStream.h"
#include "NTFS.h"
#include "Functions.h"
#include "MFTReaderDLL.h"
#include "FileCache.h"
#include "Readers.h"
