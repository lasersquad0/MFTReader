#pragma once

#include <string>

class IBuferedFileStream
{
public:
    virtual void Open(const std::string& filename) = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() = 0;
    virtual bool Eof() = 0;
    virtual bool ReadByte(uint8_t& data) = 0;
    virtual uint64_t Read(uint8_t* buffer, uint64_t count) = 0;
};

// load file data into memory by big chunks increase io operations that way.
class BuferedFileStream : public IBuferedFileStream
{
private:
    static const size_t BUFFER_SIZE = 100'000'000;
    std::fstream* m_filestream = nullptr;
    uint64_t m_bufsize;
    uint8_t* m_pbegin = nullptr;
    uint8_t* m_pcurr = nullptr;
    uint8_t* m_pend = nullptr;
    bool m_eof;
    bool m_ownstream;

    void fetchmore();
  
public:
    BuferedFileStream(const uint64_t bufferSize = BUFFER_SIZE);
    BuferedFileStream(std::fstream& fin, const uint64_t bufferSize = BUFFER_SIZE);
    BuferedFileStream(const std::string& filename, const uint64_t bufferSize = BUFFER_SIZE); 
    ~BuferedFileStream();

    void Open(const std::string& filename) override;
    void Close() override;
    bool Eof() override;

    bool ReadByte(uint8_t& data) override;

    uint64_t Read(uint8_t* buffer, uint64_t count) override; //TODO this method DOES NOT work properly when count > BUFFER_SIZE 
    
};

#define HI(_) (((_) >> 32) & 0xFFFFFFFF)
#define LO(_) ((_) & 0xFFFFFFFF)

// Uses Windows mapped files to read file data.
class MappedFileStream : public IBuferedFileStream
{
private:
    static const size_t BUFFER_SIZE = 1 << 27; //needs to be rounded to 64K, that is why we use power of two. 134'217'728 bytes
    HANDLE m_hFile = nullptr;       // the file handle
    HANDLE m_hMapFile = nullptr;    // handle for the file's memory-mapped region
    uint64_t m_dwFileSize = 0;      // size of the file
    uint64_t m_dwMapViewSize = 0;   // the size of the view
    uint64_t m_dwFileMapStart = 0;  // where to start the file map view
    uint64_t m_dwViewDelta = 0;
    uint64_t m_dwSysGran = 0;          // system allocation granularity
    uint8_t* m_lpMapAddress = nullptr; // pointer to the base address of the
    uint64_t m_bufsize;
    uint8_t* m_pbegin = nullptr;
    uint8_t* m_pcurr = nullptr;
    uint8_t* m_pend = nullptr;
    bool m_eof;
    //bool m_ownstream;

    void fetchmore();

public:
    MappedFileStream(const uint64_t bufferSize = BUFFER_SIZE);
    MappedFileStream(const std::string& filename, const uint64_t bufferSize = BUFFER_SIZE);
    ~MappedFileStream();

    bool IsOpen() override;
   
    void Open(const std::string& filename) override;
    
    void Close() override;

    bool Eof() override;

    bool ReadByte(uint8_t& data) override;

    uint64_t Read(uint8_t* buffer, uint64_t count) override; //TODO this method DOES NOT work properly when count > BUFFER_SIZE 

};