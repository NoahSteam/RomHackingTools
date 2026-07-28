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
    char*         mpUncompressedData = nullptr;
    unsigned long mUncompressedDataSize = 0;
    size_t        mCompressedSize = 0;

    ~PRSDecompressor();
    bool UncompressData(const void* pInData, unsigned int inDataSize, size_t maxOut = 0);
};

}  // namespace sfe
