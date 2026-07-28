#include "Prs.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace sfe
{

typedef unsigned char byte;

// ---------------------------------------------------------------------------------------
//  Decompression
//
//  The buffer-based PRS decoder from Utils/prs-decomp.cpp (originally Sylverant, AGPLv3),
//  reduced to the memory-buffer paths (the file-based paths are dropped) and given a hard
//  output cap so scanning arbitrary offsets can't be driven to a huge allocation.
// ---------------------------------------------------------------------------------------
namespace
{
struct PrsDecCxt
{
    uint8_t        flags = 0;
    int            bit_pos = 0;
    const uint8_t* src = nullptr;
    uint8_t*       dst = nullptr;
    size_t         src_len = 0;
    size_t         dst_len = 0;   // current capacity of dst
    size_t         dst_max = 0;   // hard cap on the decompressed size
    size_t         src_pos = 0;
    size_t         dst_pos = 0;
};

int FetchBit(PrsDecCxt* c)
{
    if (!c->bit_pos)
    {
        if (c->src_pos >= c->src_len) return -EBADMSG;
        c->flags = c->src[c->src_pos++];
        c->bit_pos = 8;
    }
    const int rv = c->flags & 1;
    c->flags >>= 1;
    --c->bit_pos;
    return rv;
}

int FetchByte(PrsDecCxt* c)
{
    if (c->src_pos >= c->src_len) return -EBADMSG;
    return c->src[c->src_pos++];
}

int FetchShort(PrsDecCxt* c)
{
    if (c->src_pos + 1 >= c->src_len) return -EBADMSG;
    int rv = c->src[c->src_pos++];
    rv |= c->src[c->src_pos++] << 8;
    return rv;
}

// Grow dst to hold at least one more byte, honoring the hard cap.
bool EnsureRoom(PrsDecCxt* c)
{
    if (c->dst_pos < c->dst_len) return true;
    if (c->dst_pos >= c->dst_max) return false;                 // hit the cap
    size_t next = c->dst_len ? c->dst_len * 2 : 4096;
    if (next > c->dst_max) next = c->dst_max;
    void* tmp = std::realloc(c->dst, next);
    if (!tmp) return false;
    c->dst = static_cast<uint8_t*>(tmp);
    c->dst_len = next;
    return true;
}

int CopyByte(PrsDecCxt* c)
{
    if (c->src_pos >= c->src_len) return -EBADMSG;
    if (!EnsureRoom(c)) return -ENOSPC;
    c->dst[c->dst_pos++] = c->src[c->src_pos++];
    return 0;
}

int OffsetCopy(PrsDecCxt* c, int offset)
{
    const long long from = static_cast<long long>(c->dst_pos) + offset;
    if (from < 0) return -EBADMSG;
    if (!EnsureRoom(c)) return -ENOSPC;
    c->dst[c->dst_pos] = c->dst[static_cast<size_t>(from)];
    ++c->dst_pos;
    return 0;
}

// Core decode loop (Utils/prs-decomp.cpp do_decompress). Returns decompressed size or a
// negative error.
int DoDecompress(PrsDecCxt* c)
{
    for (;;)
    {
        int flag = FetchBit(c);
        if (flag < 0) return flag;

        if (flag)   // literal byte
        {
            if ((flag = CopyByte(c)) < 0) return flag;
            continue;
        }

        int size;
        int32_t offset;
        if ((flag = FetchBit(c)) < 0) return flag;

        if (flag)   // long copy, or end-of-stream
        {
            if ((offset = FetchShort(c)) < 0) return offset;
            if (!offset) return static_cast<int>(c->dst_pos);   // two zero bytes => done
            size = offset & 0x0007;
            offset >>= 3;
            if (!size)
            {
                if ((size = FetchByte(c)) < 0) return size;
                ++size;
            }
            else
            {
                size += 2;
            }
            offset |= static_cast<int32_t>(0xFFFFE000);
        }
        else        // short copy
        {
            int b1 = FetchBit(c);
            if (b1 < 0) return b1;
            int b2 = FetchBit(c);
            if (b2 < 0) return b2;
            size = ((b1 << 1) | b2) + 2;
            if ((offset = FetchByte(c)) < 0) return offset;
            offset |= static_cast<int32_t>(0xFFFFFF00);
        }

        while (size--)
        {
            if ((flag = OffsetCopy(c, offset)) < 0) return flag;
        }
    }
}
}  // namespace

PRSDecompressor::~PRSDecompressor()
{
    std::free(mpUncompressedData);
    mpUncompressedData = nullptr;
}

bool PRSDecompressor::UncompressData(const void* pInData, unsigned int inDataSize, size_t maxOut)
{
    std::free(mpUncompressedData);
    mpUncompressedData = nullptr;
    mUncompressedDataSize = 0;
    mCompressedSize = 0;

    if (!pInData || inDataSize < 3) return false;   // shortest valid PRS stream is 3 bytes
    if (maxOut == 0) maxOut = 32u << 20;

    PrsDecCxt c;
    c.src = static_cast<const uint8_t*>(pInData);
    c.src_len = inDataSize;
    c.dst_max = maxOut;

    const int rv = DoDecompress(&c);
    if (rv < 0)
    {
        std::free(c.dst);
        return false;
    }

    mpUncompressedData = reinterpret_cast<char*>(c.dst);
    mUncompressedDataSize = static_cast<unsigned long>(rv);
    mCompressedSize = c.src_pos;
    return true;
}

// ---------------------------------------------------------------------------------------
//  Compression  (PuyoPrsCompressor / PRSCompressor, ported verbatim from Utils/Utils.cpp)
// ---------------------------------------------------------------------------------------
void PuyoPrsCompressor::CompressData(const void* pInData, const unsigned long inDataSize)
{
    const uint8* pSourceData = (const uint8*)pInData;

    const int sourceLength = static_cast<int>(inDataSize);

    uint8 bitPos = 0;
    uint8 controlByte = 0;

    int position = 0;
    int currentLookBehindPosition, currentLookBehindLength;
    int lookBehindOffset, lookBehindLength;

    std::vector<uint8> data;

    while (position < sourceLength)
    {
        currentLookBehindLength = 0;
        lookBehindOffset = 0;
        lookBehindLength = 0;

        for (currentLookBehindPosition = position - 1;
             (currentLookBehindPosition >= 0) &&
             (currentLookBehindPosition >= position - 0x1FF0) && (lookBehindLength < 256);
             currentLookBehindPosition--)
        {
            currentLookBehindLength = 1;
            if (pSourceData[currentLookBehindPosition] == pSourceData[position])
            {
                do
                {
                    currentLookBehindLength++;
                } while ((currentLookBehindLength <= 256) &&
                         (position + currentLookBehindLength <= (int)inDataSize) &&
                         pSourceData[currentLookBehindPosition + currentLookBehindLength - 1] ==
                             pSourceData[position + currentLookBehindLength - 1]);

                currentLookBehindLength--;
                if (((currentLookBehindLength >= 2 &&
                      currentLookBehindPosition - position >= -0x100) ||
                     currentLookBehindLength >= 3) &&
                    currentLookBehindLength > lookBehindLength)
                {
                    lookBehindOffset = currentLookBehindPosition - position;
                    lookBehindLength = currentLookBehindLength;
                }
            }
        }

        if (lookBehindLength == 0)
        {
            data.push_back(pSourceData[position++]);
            PutControlBit(1, controlByte, bitPos, data, mCompressedData);
        }
        else
        {
            Copy(lookBehindOffset, lookBehindLength, controlByte, bitPos, data, mCompressedData);
            position += lookBehindLength;
        }
    }

    PutControlBit(0, controlByte, bitPos, data, mCompressedData);
    PutControlBit(1, controlByte, bitPos, data, mCompressedData);
    if (bitPos != 0)
    {
        controlByte = (byte)((controlByte << bitPos) >> 8);
        Flush(controlByte, bitPos, data, mCompressedData);
    }

    mCompressedData.push_back(0);
    mCompressedData.push_back(0);
}

void PuyoPrsCompressor::Copy(int offset, int size, uint8& controlByte, uint8& bitPos,
                             std::vector<uint8>& data, std::vector<uint8>& destination)
{
    // `offset` is negative (a back-reference). Shift through an unsigned view of it so the
    // bit twiddling is well-defined; masking to a byte gives output identical to shifting
    // the signed value on a two's-complement machine.
    const unsigned uoff = static_cast<unsigned>(offset);
    if ((offset >= -0x100) && (size <= 5))
    {
        size -= 2;
        PutControlBit(0, controlByte, bitPos, data, destination);
        PutControlBit(0, controlByte, bitPos, data, destination);
        PutControlBit((size >> 1) & 1, controlByte, bitPos, data, destination);
        data.push_back((uint8)(offset & 0xFF));
        PutControlBit(size & 1, controlByte, bitPos, data, destination);
    }
    else
    {
        if (size <= 9)
        {
            PutControlBit(0, controlByte, bitPos, data, destination);
            data.push_back((uint8)(((uoff << 3) & 0xF8) | ((size - 2) & 0x07)));
            data.push_back((uint8)((uoff >> 5) & 0xFF));
            PutControlBit(1, controlByte, bitPos, data, destination);
        }
        else
        {
            PutControlBit(0, controlByte, bitPos, data, destination);
            data.push_back((uint8)((uoff << 3) & 0xF8));
            data.push_back((uint8)((uoff >> 5) & 0xFF));
            data.push_back((uint8)(size - 1));
            PutControlBit(1, controlByte, bitPos, data, destination);
        }
    }
}

void PuyoPrsCompressor::PutControlBit(int bit, uint8& controlByte, uint8& bitPos,
                                      std::vector<uint8>& data, std::vector<uint8>& destination)
{
    controlByte >>= 1;
    controlByte |= (byte)(bit << 7);
    bitPos++;
    if (bitPos >= 8)
    {
        Flush(controlByte, bitPos, data, destination);
    }
}

void PuyoPrsCompressor::Flush(uint8& inControlByte, uint8& inBitPos, std::vector<uint8>& inData,
                              std::vector<uint8>& inDestination)
{
    inDestination.push_back(inControlByte);
    inControlByte = 0;
    inBitPos = 0;

    inDestination.insert(std::end(inDestination), std::begin(inData), std::end(inData));
    inData.clear();
}

PRSCompressor::~PRSCompressor()
{
    Reset();
}

void PRSCompressor::CompressData(const void* pInData, const unsigned long inDataSize,
                                 ECompressOption compressOption)
{
    Reset();

    PuyoPrsCompressor puyoCompressor;
    puyoCompressor.CompressData(pInData, inDataSize);

    mCompressedSize = (unsigned long)puyoCompressor.GetCompressedData().size();
    mpCompressedData = new char[mCompressedSize];
    std::memcpy(mpCompressedData, puyoCompressor.GetCompressedData().data(), mCompressedSize);

    if (compressOption != kCompressOption_None)
    {
        unsigned long newSize = 0;

        if (compressOption == kCompressOption_TwoByteAlign && mCompressedSize % 2 != 0)
        {
            newSize = mCompressedSize + mCompressedSize % 2;
        }
        else if (compressOption == kCompressOption_FourByteAlign && mCompressedSize % 4 != 0)
        {
            const unsigned long paddingAmount =
                mCompressedSize % 4 == 0 ? 0 : (4 - mCompressedSize % 4);
            newSize = mCompressedSize + paddingAmount;
        }

        if (newSize != 0)
        {
            char* pNewData = new char[newSize];
            std::memset(pNewData, 0, newSize);
            std::memcpy(pNewData, mpCompressedData, mCompressedSize);

            delete[] mpCompressedData;
            mpCompressedData = pNewData;
            mCompressedSize = newSize;
        }
    }
}

void PRSCompressor::Reset()
{
    delete[] mpCompressedData;
    mpCompressedData = nullptr;
    mCompressedSize = 0;
}

}  // namespace sfe
