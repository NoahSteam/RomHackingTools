// CdSector — encode a 2048-byte logical sector as a raw MODE1/2352 CD-ROM sector, so Saturn
// Explorer can write a real .bin data track (with correct EDC/ECC) rather than only a
// 2048-byte .iso. This is the "ISO -> raw" step of Build Disc Image; audio tracks are copied
// verbatim and never pass through here.
//
// A MODE1/2352 sector is:
//   [0..11]    sync            00 FF FF FF FF FF FF FF FF FF FF 00
//   [12..14]   address (MSF)   minute, second, frame of (lba + 150), each BCD
//   [15]       mode            0x01
//   [16..2063] user data       2048 bytes
//   [2064..67] EDC             CRC of bytes [0..2063], little-endian
//   [2068..75] reserved        8 zero bytes
//   [2076..247] ECC P          172 bytes
//   [2248..351] ECC Q          104 bytes
// EDC and ECC use the standard CD-ROM algorithms (the ubiquitous "ecm" formulation).
#pragma once

#include <cstdint>

namespace sfe
{

// Encode one 2048-byte 'user' payload as the raw 2352-byte MODE1 sector for logical block 'lba'
// into 'out'. 'out' must have room for 2352 bytes.
void EncodeMode1Sector(uint32_t lba, const uint8_t* user, uint8_t* out);

}  // namespace sfe
