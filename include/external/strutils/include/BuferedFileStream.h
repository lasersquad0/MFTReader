#pragma once

#include <string>
#include <fstream>
#include <cassert>

class IBuferedFileStream
{
public:
    virtual void Open(const std::string& filename) = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() = 0;
    virtual bool Eof() = 0;
    virtual bool ReadByte(uint8_t& data) = 0; // true if byte has read successfully
    virtual uint64_t Read(uint8_t* buffer, uint64_t count) = 0; // returns number of bytes actually read, can be less than count parameter
};

// load file data into memory by big chunks increase io operations that way.
class BuferedFileStream : public IBuferedFileStream
{
private:
    static const uint64_t BUFFER_SIZE = 100'000'000;
    std::fstream* m_filestream = nullptr;
    uint64_t m_bufsize;
    uint8_t* m_pbegin = nullptr;
    uint8_t* m_pnext = nullptr; // points to the place in buffer from where next byte of data will be read
    uint8_t* m_pend = nullptr;  // points to the next byte after last byte of data read from stream 
    bool m_eof;
    bool m_ownstream;

    // does nothing if count bytes are available in the buffer
    // tries to read additional bytes to make count bytes available in buffer
    // returns true if was able to make count bytes available in buffer
    // returns false if it was not able to make count bytes available in the buffer (but less than count bytes may still be available in buffer for further reading)
    bool fetchmore(uint64_t count)
    {
        if (count > uint64_t(m_pend - m_pnext)) // whether buffered count bytes are available in the buffer
        {
            assert(count <= BUFFER_SIZE);
            if (m_filestream->eof())
            {
                return false; // no more bytes to read. count bytes are not available in stream and in buffer
            }
            else // try to make sure that count bytes are available in buffer
            {
                memmove(m_pbegin, m_pnext, m_pend - m_pnext); // forget data that have been read by ReadByte and Read functions
                m_pend = m_pbegin + (m_pend - m_pnext);
                m_pnext = m_pbegin;

                // reading new data from stream till end of buffer
                uint64_t bytesToRead = BUFFER_SIZE - (m_pend - m_pbegin);
                m_filestream->read((char*)m_pend, bytesToRead);
                uint64_t bytesRead = m_filestream->gcount();
                assert(bytesRead <= bytesToRead);
                m_pend += bytesRead;

                return count <= uint64_t(m_pend - m_pnext);
            }
        }

        return true;
    }

    // allocates buffer if it not allocated yet and initialises buffer variables into proper values
    // does nothing if buffer allocated already
    void allocatebuf()
    {
        if (!m_pbegin)
        {
            m_pbegin = new uint8_t[BUFFER_SIZE];
            m_pnext = m_pbegin;
            m_pend = m_pbegin; // points to the next byte after last byte of data read from stream. Here we read 0 bytes from stream. 
        }
    }

    void freebuf()
    {
        delete[] m_pbegin;
        m_pbegin = nullptr;
        m_pnext = nullptr;
        m_pend = nullptr;
    }
  
public:
    BuferedFileStream(const uint64_t bufferSize = BUFFER_SIZE)
    {
        m_bufsize = bufferSize;
        m_ownstream = true;
        m_filestream = new std::fstream();
        m_eof = false;
    }

    BuferedFileStream(std::fstream& fin, const uint64_t bufferSize = BUFFER_SIZE)
    {
        m_bufsize = bufferSize;
        m_ownstream = false;
        m_filestream = &fin;
        m_eof = false;
        allocatebuf();
    }

    BuferedFileStream(const std::string& filename, const uint64_t bufferSize = BUFFER_SIZE): BuferedFileStream(bufferSize)
    {
        Open(filename);
    }

    ~BuferedFileStream()
    {
        Close();

        if (m_ownstream)
        {   
            delete m_filestream;
            m_filestream = nullptr;
        }
    }

    // assumes that m_filestream is a valid pointer to ifstream
    void Open(const std::string& filename) override
    {
        if (m_filestream->is_open())
            throw std::invalid_argument("Error: file stream has opened already. Please close file stream before opening again.\n");
        
        m_filestream->open(filename, std::ios::in | std::ios::binary);
        if (m_filestream->fail())
            throw std::invalid_argument("Error: cannot open file '" + filename + "'\n");
        
        allocatebuf();
    }

    void Close() override
    {
        m_filestream->close(); // it does not matter wehether we own stream or not, close it anyway
        freebuf();
    }

    bool IsOpen() override
    {
        return m_filestream->is_open();
    }

    bool Eof() override
    {
        return m_eof;
    }

    bool ReadByte(uint8_t& data) override
    {
        if (m_eof) return false; // no more bytes available

        // if we cannot read event 1 byte from stream then this is end of file
        if (!fetchmore(1))
        {
            m_eof = true;
            return false;
        }

        data = *m_pnext;
        m_pnext++;

        return true;
    }

    uint64_t Read(uint8_t* buffer, uint64_t count) override //TODO this method DOES NOT work properly when count > BUFFER_SIZE 
    {
        if (m_eof) return 0; // no more bytes available

        fetchmore(count); // make sure we have count bytes in buffer
        uint64_t readcount = std::min((uint64_t)(m_pend - m_pnext), count);
        memcpy(buffer, m_pnext, count);
        m_pnext += count;
        m_eof = readcount < count;
        return readcount;
    }
    
};

#define HI(_) (((_) >> 32) & 0xFFFFFFFF)
#define LO(_) ((_) & 0xFFFFFFFF)

// Uses Windows mapped files to read file data.
/*class MappedFileStream : public IBuferedFileStream
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
*/