
#include <string>
#include <cassert>
#include "BuferedFileStream.h"
#include "PrimesFIO.h"


//Загружаем либо весь файл либо up to len простых чисел из файла 
size_t PrimesFIO::LoadFromTXT(uint64_t* arr, size_t len, fstream& f)
{
	string line;
	line.reserve(50);

	uint32_t cnt = 0;
	while (cnt < len)
	{
		getline(f, line, ',');
		if (f.eof()) break;
		arr[cnt++] = atoll(line.c_str());
	}

	return cnt;
}

size_t PrimesFIO::BypassTXT(size_t bypassCount, fstream& f)
{
	if (bypassCount == 0) return 0;

	string line;
	line.reserve(50);

	uint32_t cnt = 0;
	while (bypassCount > 0)
	{
		getline(f, line, ',');
		if (f.eof()) break;
		bypassCount--;
		cnt++;
	}

	return cnt;
}

size_t PrimesFIO::LoadFromTXTDiff(uint64_t* arr, size_t len, uint64_t& lastPrime, fstream& f)
{
	string line;
	line.reserve(50);

	uint32_t cnt = 0;

	while (cnt < len)
	{
		getline(f, line, ',');
		if (f.eof()) break;
		arr[cnt] = lastPrime + atoll(line.c_str());
		lastPrime = arr[cnt++];
	}

	return cnt;
}

size_t PrimesFIO::BypassTXTDiff(uint64_t bypassCount, uint64_t& lastPrime, fstream& f)
{
	if (bypassCount == 0) return 0;

	string line;
	line.reserve(50);

	uint32_t cnt = 0;

	while (bypassCount > 0)
	{
		getline(f, line, ',');
		if (f.eof()) break;
		lastPrime = lastPrime + atoll(line.c_str());
		bypassCount--;
		cnt++;
	}

	return cnt;
}

size_t PrimesFIO::LoadFromBIN(uint64_t* arr, size_t len, fstream& f)
{
	f.read((char*)arr, sizeof(uint64_t)*len);

	return f.gcount()/sizeof(uint64_t);

	/*uint32_t cnt = 0;
	while (cnt < len)
	{
	
		f.read((char*)(arr + cnt), sizeof(uint64_t));
		if (f.eof()) break;
		cnt++;
	}

	return cnt;*/
}

size_t PrimesFIO::BypassBIN(size_t bypassCount, fstream& f)
{
	if (bypassCount == 0) return 0;

	f.seekg(bypassCount * sizeof(uint64_t));
	return bypassCount;
}

size_t PrimesFIO::LoadFromBINJava(uint64_t* arr, size_t len, fstream& f)
{
	uint32_t cnt = 0;
	while (cnt < len)
	{
		arr[cnt] = readJavaLong(f);
		if (f.eof()) break;
		cnt++;
	}

	return cnt;
}

size_t PrimesFIO::LoadFromBINDiff(uint64_t* arr, size_t len, uint64_t& lastPrime, fstream& f)
{
	uint32_t cnt = 0;
	while (cnt < len)
	{
		if (lastPrime == 0)
		{
			f.read((char*)(arr + cnt), sizeof(uint64_t));
			if (f.eof()) break;
		}
		else
		{
			uint16_t diff = 0;
			f.read((char*)&diff, sizeof(uint16_t));
			if (f.eof()) break;
			arr[cnt] = (uint64_t)diff + lastPrime;
		}

		lastPrime = arr[cnt++];
	}

	return cnt;
}

size_t PrimesFIO::BypassBINDiff(size_t bypassCount, uint64_t& lastPrime, fstream& f)
{
	if (bypassCount == 0) return 0;

	uint32_t cnt = 0;
	uint16_t diff = 0;
	while (true)
	{
		if (lastPrime == 0)
		{
			f.read((char*)&lastPrime, sizeof(uint64_t));
			if (f.eof()) break;
		}
		else
		{
			f.read((char*)&diff, sizeof(uint16_t));
			if (f.eof()) break;
			lastPrime = (uint64_t)diff + lastPrime;
		}

		cnt++;
	}

	return cnt;
}


void PrimesFIO::SaveAsTXT(uint64_t* arr, size_t len, fstream& f)
{
	uint32_t chunk = 10'000'000;

	string s, ss;
	s.reserve(chunk);

	for (uint64_t i = 0; i < len; ++i)
	{
		ss = to_string(arr[i]);
		s.append(ss);
		s.append(",");

		if (s.size() > chunk - 20) // when we are close to capacity but еще НЕ перепрыгнули ее
		{
			f.write(s.c_str(), s.length());
			// f.flush();
			s.clear(); // очищаем строку но оставляем capacity
		}
	}

	f.write(s.c_str(), s.length());
	f.flush();

	//printf("Primes %llu were saved to file '%s'.\n", cntPrimes, outputFilename.c_str());
}


void PrimesFIO::SaveAsTXTDiff(uint64_t* arr, size_t len, uint64_t& lastPrime, uint64_t& maxDiff, fstream& f)
{
	uint32_t chunk = 10'000'000;

	string s;
	s.reserve(chunk);

	for (uint64_t i = 0; i < len; ++i)
	{
		uint64_t diff = arr[i] - lastPrime;
		if ((maxDiff < diff) && (lastPrime != 0)) maxDiff = diff;

		s.append(to_string(diff));
		lastPrime = arr[i];

		s.append(",");

		if (s.size() > chunk - 20) // when we are close to capacity but еще НЕ перепрыгнули ее
		{
			f.write(s.c_str(), s.length());
			// f.flush();
			s.clear(); // очищаем строку но оставляем capacity
		}
	}

	f.write(s.c_str(), s.length());
	f.flush();
}

void PrimesFIO::SaveAsBIN(uint64_t* arr, size_t len, fstream& f)
{
	f.write((char*)arr, sizeof(uint64_t)*len);

	/*for (uint64_t i = 0; i < len; i++)
	{
		f.write((char*)(arr + i), sizeof(uint64_t));
	}*/

	f.flush();
}

void PrimesFIO::SaveAsBINDiff(uint64_t* arr, size_t len, uint64_t& lastPrime, uint64_t& maxDiff, fstream& f)
{
	maxDiff = 0;

	for (uint64_t i = 0; i < len; i++)
	{
		if (lastPrime == 0)
		{
			f.write((char*)(arr + i), sizeof(uint64_t));
		}
		else
		{
			uint16_t diff = (uint16_t)(arr[i] - lastPrime); // diff должен вместиться в 2байта
			if (maxDiff < diff) maxDiff = diff;

			f.write((char*)&diff, sizeof(uint16_t));
		}

		lastPrime = arr[i];
	}

	f.flush();
}

void PrimesFIO::SaveAsBINDiffVar(uint64_t* arr, size_t len, uint64_t& lastPrime, uint64_t& maxDiff, fstream& f)
{
	uint8_t buf[9];
	uint64_t diff;
	maxDiff = 0;

	for (uint64_t i = 0; i < len; i++)
	{
		diff = arr[i] - lastPrime;

		if (lastPrime > 2) // special case when diff is odd when lastPrime==2 (1=3-2)
		{
			diff /= 2; // делим на 2 всё кроме первого числа так как первое число это полный prime number.
			if (maxDiff < diff) maxDiff = diff;
		}

		size_t difflen = var_len_encode(buf, diff);
		assert(difflen > 0);
		//assert(difflen <= 2);

		f.write((char*)buf, difflen);
		lastPrime = arr[i];
	}

	f.flush();
}


size_t PrimesFIO::LoadFromBINDiffVar(uint64_t* arr, size_t len, uint64_t& lastPrime, IBuferedFileStream& bf)
{
	uint64_t diff;
	uint8_t buf[9]{0};
	size_t maxSize;

	uint32_t cnt = 0;
	while (cnt < len)
	{
		maxSize = 0;

		while (true)
		{
			//f.read((char*)(buf + maxSize), 1);
			bf.ReadByte(*(buf + maxSize));
			if (bf.eof()) break;
			if ((buf[maxSize++] & 0x80) == 0) break;
		}

		if (bf.eof()) break;

		size_t res = var_len_decode(buf, maxSize, &diff);
		if (lastPrime > 0)
		{
			assert(maxSize == 1 || maxSize == 2);
			assert(res == 1 || res == 2);
			assert(diff < 1000);
		}

		if (lastPrime > 2) diff *= 2; // умножаем на 2 все: кроме первого числа, и когда lastPrime==2

		arr[cnt] = diff + lastPrime;
		lastPrime = arr[cnt++];
	}

	return cnt;
}

size_t PrimesFIO::BypassBINDiffVar(uint64_t bypassCount, uint64_t& lastPrime, IBuferedFileStream& bf)
{
	if (bypassCount == 0) return 0;

	uint64_t diff;
	uint8_t buf[9];
	size_t maxSize;
	
	uint64_t cnt = 0;
	while (true)
	{
		maxSize = 0;
		
		while (true)
		{
			//f.read((char*)(buf + maxSize), 1);
			bf.ReadByte(*(buf + maxSize));
			if (bf.eof()) break;
			if ((buf[maxSize++] & 0x80) == 0) break;
		}

		if (bf.eof()) break;

		size_t res = var_len_decode(buf, maxSize, &diff);
		if (lastPrime > 0) 
		{
			assert(maxSize == 1 || maxSize == 2);
			assert(res == 1 || res == 2);
			assert(diff < 1000);
		}

		if (lastPrime > 2) diff *= 2; // умножаем на 2 все: кроме первого числа, и когда lastPrime==2

		lastPrime = diff + lastPrime;
		cnt++;

		if (--bypassCount == 0) break;
	}

	return cnt;
}

size_t PrimesFIO::BypassBINDiffVarOLD(uint64_t bypassCount, uint64_t& lastPrime, fstream& f)
{
	if (bypassCount == 0) return 0;

	uint64_t diff;
	uint8_t buf[9];
	size_t maxSize;

	uint64_t cnt = 0;
	while (true)
	{
		maxSize = 0;

		while (true)
		{
			f.read((char*)(buf + maxSize), 1);
			if (f.eof()) break;
			if ((buf[maxSize++] & 0x80) == 0) break;
		}

		if (f.eof()) break;
		size_t res = var_len_decode(buf, maxSize, &diff);

		assert(res > 0);

		// здесь нету умножения на 2. старый формат

		lastPrime = diff + lastPrime;
		cnt++;

		if (--bypassCount == 0) break;
	}

	return cnt;
}

// ***!!!! старая версия без умножения на 2 !!!! *****
size_t PrimesFIO::LoadFromBINDiffVarOLD(uint64_t* arr, size_t len, uint64_t& lastPrime, fstream& f)
{
	uint64_t diff;
	uint8_t buf[9];
	size_t maxSize;

	uint32_t cnt = 0;
	while (cnt < len)
	{
		maxSize = 0;

		while (true)
		{
			f.read((char*)(buf + maxSize), 1);
			if (f.eof()) break;
			if ((buf[maxSize++] & 0x80) == 0) break;
		}

		if (f.eof()) break;

		size_t res = var_len_decode(buf, maxSize, &diff);

		assert(res > 0);

		arr[cnt] = diff + lastPrime;
		lastPrime = arr[cnt++];
	}

	return cnt;
}

size_t PrimesFIO::var_len_encode(uint8_t buf[9], uint64_t num)
{
	if (num > UINT64_MAX / 2)
		return 0;

	size_t i = 0;

	while (num >= 0x80)
	{
		buf[i++] = (uint8_t)(num) | 0x80;
		num >>= 7;
	}

	buf[i++] = (uint8_t)(num);

	return i;
}

size_t PrimesFIO::var_len_decode(const uint8_t buf[], size_t size_max, uint64_t* num)
{
	if (size_max == 0)
		return 0;

	if (size_max > 9)
		size_max = 9;

	*num = buf[0] & 0x7F;
	size_t i = 0;

	while (buf[i++] & 0x80)
	{
		if (i >= size_max || buf[i] == 0x00)
			return 0;

		*num |= (uint64_t)(buf[i] & 0x7F) << (i * 7);
	}

	return i;
}

uint64_t PrimesFIO::readJavaLong(fstream& f)
{
	uint64_t res = 0;
	unsigned char b = 0;
	for (uint32_t i = 0; i < 8; i++)
	{
		f.read((char*)&b, sizeof(unsigned char));
		res <<= 8;
		res |= b;
	}
	return res;
}

