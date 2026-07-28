// Prs — SEGA / "Puyo" PRS compression, the scheme Sakura Wars and many other Saturn /
// Dreamcast games use to pack graphics. These are the PuyoPrsCompressor / PRSCompressor /
// PRSDecompressor classes from Utils/Utils.cpp, vendored here so the cross-platform
// frontend can compress and decompress PRS data without pulling in the Windows-only
// Utils build. The decompressor is bounds-checked and output-capped so it can be pointed
// at arbitrary bytes (e.g. every offset in a file while searching for a compressed
// texture) and fail cleanly rather than read out of bounds or balloon memory.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sfe
{

typedef unsigned char uint8;

// Default cap on a single decompressed block (a real texture block is far smaller). Bounds
// the work/memory when decoding is attempted at an arbitrary/garbage offset during a scan.
constexpr size_t kPrsMaxDecompressBytes = 32u << 20;   // 32 MiB

// Raw PRS encoder (the "Puyo" variant). Produces a compressed byte stream in
// GetCompressedData(). Ported verbatim from Utils/Utils.cpp.
struct PuyoPrsCompressor
{
    void                      CompressData(const void* pInData, unsigned long inDataSize);
    const std::vector<uint8>& GetCompressedData() const { return mCompressedData; }

private:
    void Flush(uint8& controlByte, uint8& bitPos, std::vector<uint8>& data,
               std::vector<uint8>& destination);
    void PutControlBit(int bit, uint8& controlByte, uint8& bitPos, std::vector<uint8>& data,
                       std::vector<uint8>& destination);
    void Copy(int offset, int size, uint8& controlByte, uint8& bitPos,
              std::vector<uint8>& data, std::vector<uint8>& destination);

    std::vector<uint8> mCompressedData;
};

// PRS compressor with optional trailing alignment. Owns a malloc'd compressed buffer.
struct PRSCompressor
{
    enum ECompressOption
    {
        kCompressOption_None,
        kCompressOption_TwoByteAlign,
        kCompressOption_FourByteAlign,
    };

    char*         mpCompressedData = nullptr;
    unsigned long mCompressedSize = 0;

    ~PRSCompressor();
    void CompressData(const void* pInData, unsigned long inDataSize,
                      ECompressOption compressOption = kCompressOption_None);
    void Reset();
};

// PRS decompressor. UncompressData decodes the stream at pInData; on success mpUncompressedData
// / mUncompressedDataSize hold the result and mCompressedSize is how many input bytes the
// stream consumed. `maxOut` caps the decompressed size (0 => a 32 MiB default) so decoding
// arbitrary/garbage input can't be driven to a huge allocation.
struct PRSDecompressor
{
    char*         mpUncompressedData = nullptr;   // reused/grown across calls; freed in dtor
    unsigned long mUncompressedDataSize = 0;      // valid length after a successful call
    size_t        mCompressedSize = 0;

    ~PRSDecompressor();
    // Decode the stream at pInData. The output buffer is retained between calls (only grown
    // when a decode needs more room), so decoding at many offsets in a row is allocation-
    // free after warm-up. mpUncompressedData stays valid until the next call or destruction.
    bool UncompressData(const void* pInData, unsigned int inDataSize, size_t maxOut = 0);

private:
    size_t mCapacity = 0;   // allocated size of mpUncompressedData
};

}  // namespace sfe
