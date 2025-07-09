/*
 * FileStream.cpp
 *
 * Copyright 2025, LogEngine2 Project. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#include <sys/stat.h>
#include <sys/locking.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#ifdef WIN32
#include <io.h>
#else
#include <sys/io.h>
#include <unistd.h>
#endif

#include <cassert>
#include "windows.h"
#include "FileStream.h"


LOGENGINE_NS_BEGIN

//////////////////////////////////////////////////////////////////////
//  TStream Class
//////////////////////////////////////////////////////////////////////
/*
int TStream::ReadChar()
{
	char c;
	if (Read(&c, sizeof(c)) == sizeof(char)) // in case of error Read() throws an exception
		return c;
	else
		return -1; // EOF reached
}


int TStream::ReadWChar()
{
	wchar_t c;
	if (Read(&c, sizeof(c)) == sizeof(wchar_t))
		return c;
	else
		return -1;
}

void TStream::operator >>(string& Value)
{
	Value.clear();
	do
	{
		int c = ReadChar();
		if (c == EndLineChar)
		{
			if (Value[Value.size() - 1] == '\r') Value.resize(Value.size() - 1); // if '\r' has been read right BEFORE '\n' delete it
			return;
		}
		if (c == -1) return; // end of file reached
		Value += static_cast<char>(c);
	} while (true);
}

void TStream::operator >>(wstring& Value)
{
	Value.clear();
	do
	{
		int c = ReadWChar();
		if (c == EndLineChar)
		{
			if (Value[Value.size() - 1] == '\r') Value.resize(Value.size() - 1); // if '\r' has been read right BEFORE '\n' delete it
			return;
		};
		if (c == -1) return; // end of file reached
		Value += static_cast<wchar_t>(c);
	} while (true);
}
*/

LOGENGINE_INLINE std::string TStream::ReadPString()
{
	std::string res;
	uint i; //TODO shall i be size_t type?
	*this >> i;    // reading string size
	res.resize(i); // allocating space in string
	Read(res.data(), i); // res.length());
	return res;
}

LOGENGINE_INLINE void TStream::WritePString(std::string& str)
{
	*this << str.size();
	*this << str.data();
}

//////////////////////////////////////////////////////////////////////
//  TMemoryStream Class
//////////////////////////////////////////////////////////////////////

LOGENGINE_INLINE void TMemoryStream::ResetPos()
{
	FSize = 0;
	FRPos = 0;
	FWPos = 0;
}

LOGENGINE_INLINE int TMemoryStream::Read(void* Buffer, size_t Size)
{
	assert(FRPos <= FSize);
	assert(FWPos <= FSize);

	if (Buffer == nullptr) return -1;
	if (Size == 0) return -1;

	FEOF = Size >= FSize - FRPos;

	if (FRPos + Size > FSize) Size = FSize - FRPos;
	if (Size == 0) return 0; // we've have read everything (EOF reached)

	memcpy(Buffer, FMemory + FRPos, Size);

	FRPos += Size;
	return static_cast<int>(Size);
}

LOGENGINE_INLINE size_t TMemoryStream::Write(const void* Buffer, const size_t Size)
{
   //	_set_errno(EINVAL);
	if (Buffer == nullptr) return 0; // nothing to write
	if (Size == 0) return 0;
	//_set_errno(0);

	if (FWPos + Size > FSize) // not enough space in FMemory buffer
	{
		if(FNeedFree) // we control the buffer, so we can reallocate it
		{
			FMemory = static_cast<uint8_t*>(realloc(FMemory, FWPos + Size));
			FSize = FWPos + Size;
		}
		else // we do not control the buffer, so we cannot reallocate it
		{
		 //	_set_errno(ENOSPC);
			std::string serr = "External buffer size is too small! Cannot write.";
			throw IOException(serr);
		}
	 }

	memcpy(FMemory + FWPos, Buffer, Size);
	FWPos += Size;
	return Size; //static_cast<int>(Size);
}

template<typename T>
T check_range(const T& start, const T& end, const T& offset)
{
	return (start + offset < 0) ? 0 : (start + offset > end) ? end : (start + offset);
}

//#define CHECK_RANGE(start, end, offset) check_range((start), (end), (offset))
#define CHECK_RANGE_OFF_T(start, end, offset) static_cast<pos_type>(check_range(static_cast<off_t>(start), static_cast<off_t>(end), (offset)))


LOGENGINE_INLINE TMemoryStream::pos_type TMemoryStream::SeekR(const off_t Offset, const TSeekMode sMode)
{
	switch (sMode)
	{
	case TSeekMode::smFromBegin:  FRPos = CHECK_RANGE_OFF_T(0,     FSize,  Offset); return FRPos;  //(Offset < 0) ? 0 : (Offset > FSize) ? FSize : Offset; return static_cast<off_t>(FRPos);
	case TSeekMode::smFromEnd:    FRPos = CHECK_RANGE_OFF_T(FSize, FSize, -Offset); return FRPos; //(Offset > FSize) ? 0 : FSize - Offset; return static_cast<off_t>(FRPos);
	case TSeekMode::smFromCurrent:FRPos = CHECK_RANGE_OFF_T(FRPos, FSize,  Offset); return FRPos;   //(static_cast<off_t>(FRPos) + Offset < 0) ? 0 : (FRPos + Offset > FSize) ? FSize : FRPos + Offset; return static_cast<off_t>(FRPos);
	//default:
	//	return -1l;
	}

	throw IOException("Invalid TMemoryStream::SeekR() mode.");
}

LOGENGINE_INLINE TMemoryStream::pos_type TMemoryStream::SeekW(const off_t Offset, const TSeekMode sMode)
{
	switch (sMode)
	{
	case TSeekMode::smFromBegin:  FWPos = CHECK_RANGE_OFF_T(0,     FSize,  Offset); return FWPos;
	case TSeekMode::smFromEnd:    FWPos = CHECK_RANGE_OFF_T(FSize, FSize, -Offset); return FWPos;
	case TSeekMode::smFromCurrent:FWPos = CHECK_RANGE_OFF_T(FWPos, FSize,  Offset); return FWPos;
	//default:
	//	return -1l;
	}

	throw IOException("Invalid TMemoryStream::SeekW() mode.");
}

LOGENGINE_INLINE void TMemoryStream::SetBuffer(uint8_t* Buffer, size_t Size)
{
	ResetPos();
	if (FNeedFree) free(FMemory);
	FNeedFree = false;
	FMemory = Buffer;
	FSize = Size;
}

// call this method when you want to return back to internal buffer
LOGENGINE_INLINE void TMemoryStream::UnsetBuffer()
{
	if (FNeedFree) return; // if we have internal buffer - do nothing

	// otherwise reset all positions and allocate buffer of default size
	ResetPos();
	FNeedFree = true;
	FMemory = new uint8_t[DEFAULT_BUF_SIZE];
	FSize = DEFAULT_BUF_SIZE;
}


//////////////////////////////////////////////////////////////////////
//  TFileStream Class
//////////////////////////////////////////////////////////////////////

#ifdef WIN32
#define mylseek _lseek
#define myread _read
#define mywrite _write
#define myclose _close
#else
#define mylseek lseek
#define myread read
#define mywrite write
#define myclose close
#endif


LOGENGINE_INLINE TFileStream::TFileStream(const std::string& FileName, const TFileMode fMode, const TSharingMode sMode)
{
	FFileName = FileName;
	FFileMode = fMode;
	FSharingMode = sMode;
	hf = 0;


#if defined(WIN32) && !defined(__BORLANDC__)
	errno_t res = 0;
	int activeMode;
	if (sMode == TSharingMode::shDefault) activeMode = DefaultSharingModes[fMode];
	else activeMode = SharingModes[sMode];

	switch (fMode)
	{
	case TFileMode::fmRead:      res = _sopen_s(&hf, FFileName.c_str(), O_RDONLY | O_BINARY,           activeMode, _S_IREAD | _S_IWRITE); break;
	case TFileMode::fmWrite:     res = _sopen_s(&hf, FFileName.c_str(), O_WRONLY | O_CREAT | O_BINARY, activeMode, _S_IREAD | _S_IWRITE); break;
	case TFileMode::fmReadWrite: res = _sopen_s(&hf, FFileName.c_str(), O_RDWR | O_CREAT | O_BINARY,   activeMode, _S_IREAD | _S_IWRITE); break;
	case TFileMode::fmWriteTrunc:res = _sopen_s(&hf, FFileName.c_str(), O_WRONLY | O_CREAT |O_TRUNC | O_BINARY, activeMode, _S_IREAD | _S_IWRITE); break;
	}
#else
	switch (fMode)
	{
	case fmRead:       hf = open(FFileName.c_str(), O_RDONLY); break;
	case fmWrite:      hf = open(FFileName.c_str(), O_WRONLY | O_CREAT, S_IREAD | S_IWRITE); break;
	case fmReadWrite:  hf = open(FFileName.c_str(), O_RDWR | O_CREAT, S_IREAD | S_IWRITE); break;
	case fmWriteTrunc: hf = open(FFileName.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IREAD | S_IWRITE); break;
	}

	int res = errno;
#endif

	if (hf == -1)
	{
		std::string serr;
		if (res == EINVAL)
			serr = "Wrong file name '" + FileName + "'!";
		else if (res == EACCES)
			serr = "Can't get access to file '" + FileName + "'!";
		else
			serr = "Can't open file '" + FileName + "'!";

		throw IOException(serr);
	}
}

LOGENGINE_INLINE TFileStream::~TFileStream()
{
	myclose(hf);
	hf = 0;
}

// If successfull Read returns number of read bytes
// Throws an exception in case of any error during reading
LOGENGINE_INLINE int TFileStream::Read(void* Buffer, size_t Size)
{
	if (FFileMode == TFileMode::fmWrite || FFileMode == TFileMode::fmWriteTrunc)
		throw IOException("File opened in write-only mode. Can't read!");

	int c = myread(hf, Buffer, static_cast<uint>(Size));

	FEOF = (static_cast<size_t>(c) != Size);

	if (c == -1)
	{
		std::string serr = "Cannot read from file '" + FFileName + "'! May be file closed?";
		throw IOException(serr);
	}

	return c;
}

LOGENGINE_INLINE size_t TFileStream::WriteCRLF(void)
{
	return Write(EndLine, strlen(EndLine));
}

LOGENGINE_INLINE size_t TFileStream::Write(const std::string& str)
{
	return Write(str.data(), str.length());
}

// If successfull Write returns number of bytes written
// Throws an exception in case of any error during writing
LOGENGINE_INLINE size_t TFileStream::Write(const void* Buffer, const size_t Size)
{
	if (Buffer == nullptr) return 0; // nothing to write
	if (Size == 0) return 0;

	if (FFileMode == TFileMode::fmRead)
		throw IOException("File opened in read-only mode. Can't write!");

	// mywrite returns -1 in case of an error
	// it returns -1 even when it cannot write entire buffer to disk
	int c = mywrite(hf, Buffer, static_cast<uint>(Size));

	if (c == -1 /*|| c != Size*/)
	{
		std::string serr = "Cannot write to file '" + FFileName + "'! May be disk full?";
		throw IOException(serr);
	}

	assert(static_cast<size_t>(c) == Size);
	return static_cast<size_t>(c);
}

LOGENGINE_INLINE size_t TFileStream::WriteLn(const std::string& str)
{
	auto c = Write(str);
	return WriteCRLF() + c;
}

LOGENGINE_INLINE size_t TFileStream::WriteLn(const void* Buffer, const size_t Size)
{
	auto c = Write(Buffer, Size);
	return WriteCRLF() + c;
}

LOGENGINE_INLINE off_t TFileStream::Seek(off_t Offset, TSeekMode sMode)
{
	long c = -1;
	switch (sMode)
	{
	case TSeekMode::smFromBegin:   c = mylseek(hf, Offset, SEEK_SET); break;
	case TSeekMode::smFromEnd:     c = mylseek(hf, Offset, SEEK_END); break;
	case TSeekMode::smFromCurrent: c = mylseek(hf, Offset, SEEK_CUR); break;
	default:
			throw IOException("Invalid TFileStream::Seek() mode.");
	}

	if (c == -1)
	{
		std::string serr = "lseek returned error for file '" + FFileName + "'!";
		throw IOException(serr);
	}

	return c;
}

LOGENGINE_INLINE size_t TFileStream::Length() const
{
	struct stat st;
	fstat(hf, &st);
	return static_cast<size_t>(st.st_size);
};

LOGENGINE_INLINE void TFileStream::Flush() const
{
#ifdef WIN32
	_commit(hf);
#else
	fsync(hf);
#endif
}

// SIDE EFFECT: Lock looses current file position, moves it to the beginning of the file
LOGENGINE_INLINE void TFileStream::Lock()
{
	Seek(0, TSeekMode::smFromBegin);
	BOOL res = _locking(hf, _LK_LOCK, MAXLONG); // _locking uses current position only for lock/unlock operations, that is why we need Seek() before _locking
	if (res)
	{
		int ecode = errno;
		std::string serr;

		if (ecode == EINVAL) serr = "Wrong argument for _locking() function";
		else if (ecode == EACCES) serr = "Locking violation (file already locked or unlocked).";
		else if (ecode == EBADF) serr = "Invalid file descriptor.";
		else serr = "Cannot lock file. Error in _locking() function.";
		
		throw IOException(serr);
	}
}

// SIDE EFFECT: Unlock looses current file position, moves it to the beginning of the file
LOGENGINE_INLINE void TFileStream::Unlock()
{
	Seek(0, TSeekMode::smFromBegin);
	BOOL res = _locking(hf, _LK_UNLCK, MAXLONG);
	if (res)
	{
		int ecode = errno;
		std::string serr;
		
		if (ecode == EINVAL) serr = "Wrong argument for _locking() function";
		else if (ecode == EACCES) serr = "Unlocking violation (file already locked or unlocked).";
		else if (ecode == EBADF) serr = "Invalid file descriptor.";
		else serr = "Cannot unlock file. Error in _locking() function.";

		throw IOException(serr);
	}
}

LOGENGINE_NS_END
