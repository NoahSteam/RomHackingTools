// Shared Saturn-state helpers used by more than one driver (savestate + live):
// big-endian register reads, the Yabause VDP2 register-image reconstruction, and
// CRAM endian normalization. Kept in one place so the savestate and live drivers
// can't drift apart on the (fixed, hardware-defined) VDP2 struct layout.
#ifndef SATURNEXPLORER_DRIVER_SATURNSTATESHARED_H
#define SATURNEXPLORER_DRIVER_SATURNSTATESHARED_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sedrv
{

// Big-endian 16-bit read from a hardware-offset register image (Saturn is BE).
// Returns 0 if the offset is out of range.
uint16_t ReadReg16(const std::vector<uint8_t>& regs, uint32_t reg);

// Rebuild a hardware-offset, big-endian VDP2 register image from the packed
// (little-endian, host-order) Yabause `Vdp2` struct that starts at 'structBase'
// in 'src'. The core's read_vdp2_reg reads big-endian at hardware offsets, so
// this image is directly usable. 'out' is sized to cover every VDP2 register.
void BuildVdp2RegImage(const std::vector<uint8_t>& src, size_t structBase,
                       std::vector<uint8_t>& out);

// Normalize host-endian VDP2 color RAM to Saturn-native big-endian, in place.
// Yabause keeps CRAM in host byte order (T2 access); the core (like hardware and
// VRAM) expects big-endian. 'crmd' is RAMCTL bits 12-13 (2 = RGB888 => 32-bit
// entries, otherwise 16-bit).
void NormalizeCramToBigEndian(std::vector<uint8_t>& cram, unsigned crmd);

}  // namespace sedrv

#endif /* SATURNEXPLORER_DRIVER_SATURNSTATESHARED_H */
