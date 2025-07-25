#pragma once

#include <fstream>

#include "BuferedFileStream.h"


enum PRIMES_FILE_FORMATS
{
	none,
	txt,
	txtdiff,
	bin,
	bindiff,
	bindiffvar
};

#define FORMAT_TO_STR(arg) (arg==txt ? "TXT":arg==bin?"BIN":arg==txtdiff?"TXTDIFF":arg==bindiff?"BINDIFF":arg==bindiffvar?"BINDIFFVAR":"<unrecognized>")

#define STR_TO_FORMAT(arg) (arg=="TXT"?txt:arg=="BIN"?bin:arg=="TXTDIFF"?txtdiff:arg=="BINDIFF"?bindiff:arg=="BINDIFFVAR"?bindiffvar:none)

size_t var_len_encode(uint8_t buf[9], uint64_t num);
size_t var_len_decode(const uint8_t buf[], size_t size_max, uint64_t * num);

class PrimesFIO
{
private:
	
public:

	size_t LoadFromTXT(uint64_t* arr, size_t len, std::fstream& f);
	size_t LoadFromTXTDiff(uint64_t* arr, size_t len, uint64_t& lastPrime, std::fstream& f);
	size_t LoadFromBIN(uint64_t* arr, size_t len, std::fstream& f);
	size_t LoadFromBINJava(uint64_t* arr, size_t len, std::fstream& f);
	size_t LoadFromBINDiff(uint64_t* arr, size_t len, uint64_t& lastPrime, std::fstream& f);
	size_t LoadFromBINDiffVar(uint64_t* arr, size_t len, uint64_t& lastPrime, IBuferedFileStream& bf);
	size_t LoadFromBINDiffVarOLD(uint64_t* arr, size_t len, uint64_t& lastPrime, std::fstream& f);

	void SaveAsTXT(uint64_t* arr, size_t len, std::fstream& f);
	void SaveAsTXTDiff(uint64_t* arr, size_t len, uint64_t& lastPrime, uint64_t& maxDiff, std::fstream& f);
	void SaveAsBIN(uint64_t* arr, size_t len, std::fstream& f);
	void SaveAsBINDiff(uint64_t* arr, size_t len, uint64_t& lastPrime, uint64_t& maxDiff, std::fstream& f);
	void SaveAsBINDiffVar(uint64_t* arr, size_t len, uint64_t& lastPrime, uint64_t& maxDiff, std::fstream& f);

	// absolute positioning
	size_t BypassTXT(size_t bypassCount, std::fstream& f);
	size_t BypassTXTDiff(uint64_t bypassCount, uint64_t& lastPrime, std::fstream& f);
	size_t BypassBIN(size_t bypassCount, std::fstream& f);
	size_t BypassBINDiff(size_t bypassCount, uint64_t& lastPrime, std::fstream& f);
	size_t BypassBINDiffVar(uint64_t bypassCount, uint64_t& lastPrime, IBuferedFileStream& bf);
	size_t BypassBINDiffVarOLD(uint64_t bypassCount, uint64_t& lastPrime, std::fstream& f);

	uint64_t readJavaLong(std::fstream& f);

};


