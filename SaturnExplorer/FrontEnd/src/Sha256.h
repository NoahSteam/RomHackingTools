// Sha256 -- FIPS 180-4 SHA-256 with no dependencies, built on every desktop build.
//
// It started life inside the decompiler's SpecBundle, which is compiled only when
// SE_ENABLE_DECOMPILER is on. The AI investigation packages hash attachments and responses
// whether or not the decompiler ships, so the hash lives here and SpecBundle uses it like
// anyone else.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace sfe
{

// Streaming form, for files and ranges too large to hold twice. One digest per object:
// HexDigest() pads and finishes the message, so Update() after it is a programming error.
class Sha256
{
public:
    void Update(const void* data, size_t size);
    std::string HexDigest();   // lower-case, 64 characters

private:
    void Compress();

    uint32_t mState[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    unsigned char mBlock[64] = {};
    size_t mUsed = 0;
    uint64_t mLength = 0;   // bytes; 64-bit so a >4 GiB stream is hashed correctly on 32-bit hosts
};

// Lower-case hex SHA-256 of one buffer.
std::string Sha256Hex(const void* data, size_t size);

// True for exactly 64 lower-case hex digits: the only spelling this tool writes or accepts.
bool IsSha256Hex(const std::string& s);

}  // namespace sfe
