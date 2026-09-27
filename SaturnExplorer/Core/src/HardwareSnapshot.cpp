#include "HardwareSnapshot.h"

namespace se
{

namespace
{

// Read `size` bytes from a Seam A bulk reader into `dst`, in chunks, tolerating
// short reads. Returns total bytes copied.
size_t ReadAll(size_t (*reader)(void*, uint32_t, void*, size_t), void* user,
               uint32_t base, uint8_t* dst, size_t size)
{
    if (!reader)
    {
        return 0;
    }

    size_t done = 0;
    while (done < size)
    {
        size_t got = reader(user, base + static_cast<uint32_t>(done),
                            dst + done, size - done);
        if (got == 0)
        {
            break;  // driver reported no progress; stop.
        }
        done += got;
    }
    return done;
}

}  // namespace

bool HardwareSnapshot::Capture(const se_data_source& dataSource)
{
    // Valid if the source gave us anything at all, rather than VDP1 VRAM specifically: a
    // VDP2-only source still has background layers and the tile/palette viewers, and a
    // work-RAM-only dump has the memory viewer, watches and RAM search. Tying validity to VDP1
    // made all of that unreachable, because a failed Capture means BeginFrame returns
    // SE_ERR_NO_DATA and the context never comes up.
    //
    // Set below as each region lands, rather than tested as one expression at the end: an
    // expression has to restate the list of regions this function fills, and a hand-written
    // copy of that list drifts -- the first attempt at it already omitted the SCSP slots and
    // CD status captured near the bottom.
    mbValid = false;
    mbHasVdp1Regs = false;
    mbHasVdp2Regs = false;
    mVdp1Vram.clear();
    mVdp2Vram.clear();
    mCram.clear();
    mWramLow.clear();
    mWramHigh.clear();
    mVdp1Fb.clear();
    mSoundRam.clear();
    mVdp1Regs.clear();
    mVdp2Regs.clear();

    if (dataSource.capabilities & SE_CAP_VDP1_VRAM)
    {
        mVdp1Vram.resize(kVdp1VramSize);
        size_t got = ReadAll(dataSource.read_vdp1_vram, dataSource.user, 0,
                             mVdp1Vram.data(), mVdp1Vram.size());
        mVdp1Vram.resize(got);
        mbValid = mbValid || got != 0;
    }
    if (dataSource.capabilities & SE_CAP_VDP2_VRAM)
    {
        mVdp2Vram.resize(kVdp2VramSize);
        size_t got = ReadAll(dataSource.read_vdp2_vram, dataSource.user, 0,
                             mVdp2Vram.data(), mVdp2Vram.size());
        mVdp2Vram.resize(got);
        mbValid = mbValid || got != 0;
    }
    if (dataSource.capabilities & SE_CAP_CRAM)
    {
        mCram.resize(kCramSize);
        size_t got = ReadAll(dataSource.read_cram, dataSource.user, 0,
                             mCram.data(), mCram.size());
        mCram.resize(got);
        mbValid = mbValid || got != 0;
    }
    // Work RAM: low @ 0x00200000, high @ 0x06000000 (each 1 MiB). read_main_ram is
    // addressed by bus address, so read from those bases.
    if ((dataSource.capabilities & SE_CAP_MAIN_RAM) && dataSource.read_main_ram)
    {
        mWramLow.resize(kWramSize);
        mWramLow.resize(ReadAll(dataSource.read_main_ram, dataSource.user,
                                kWramLowBase, mWramLow.data(), mWramLow.size()));
        mWramHigh.resize(kWramSize);
        mWramHigh.resize(ReadAll(dataSource.read_main_ram, dataSource.user,
                                 kWramHighBase, mWramHigh.data(), mWramHigh.size()));
        mbValid = mbValid || !mWramLow.empty() || !mWramHigh.empty();
    }

    if ((dataSource.capabilities & SE_CAP_VDP1_FB) && dataSource.read_vdp1_fb)
    {
        mVdp1Fb.resize(kVdp1FbSize);
        mVdp1Fb.resize(ReadAll(dataSource.read_vdp1_fb, dataSource.user, 0,
                               mVdp1Fb.data(), mVdp1Fb.size()));
        mbValid = mbValid || !mVdp1Fb.empty();
    }

    // SCSP sound RAM (0-based offset within the 512 KiB block).
    if ((dataSource.capabilities & SE_CAP_SOUND_RAM) && dataSource.read_sound_ram)
    {
        mSoundRam.resize(kSoundRamSize);
        mSoundRam.resize(ReadAll(dataSource.read_sound_ram, dataSource.user, 0,
                                 mSoundRam.data(), mSoundRam.size()));
        mbValid = mbValid || !mSoundRam.empty();
    }

    // Capture the VDP1 register file (0x00..0x1E) if the driver supplies it.
    if ((dataSource.capabilities & SE_CAP_VDP1_REGS) && dataSource.read_vdp1_reg)
    {
        constexpr uint32_t kVdp1RegMax = 0x1E;
        mVdp1Regs.resize((kVdp1RegMax >> 1) + 1);
        for (uint32_t hw = 0; hw <= kVdp1RegMax; hw += 2)
        {
            mVdp1Regs[hw >> 1] = dataSource.read_vdp1_reg(dataSource.user, hw);
        }
        mbHasVdp1Regs = true;
        mbValid = true;
    }

    // Capture the VDP2 register file (0x000..0x11E) into an immutable copy, so
    // the compositor reads a consistent snapshot rather than re-hitting the
    // driver per pixel.
    if ((dataSource.capabilities & SE_CAP_VDP2_REGS) && dataSource.read_vdp2_reg)
    {
        constexpr uint32_t kVdp2RegMax = 0x11E;
        mVdp2Regs.resize((kVdp2RegMax >> 1) + 1);
        for (uint32_t hw = 0; hw <= kVdp2RegMax; hw += 2)
        {
            mVdp2Regs[hw >> 1] = dataSource.read_vdp2_reg(dataSource.user, hw);
        }
        mbHasVdp2Regs = true;
        mbValid = true;
    }

    // Capture the SH-2 master/slave register files if the driver supplies them.
    if ((dataSource.capabilities & SE_CAP_SH2_REGS) && dataSource.read_sh2_regs)
    {
        for (int cpu = 0; cpu < 2; ++cpu)
        {
            mbHasSh2[cpu] = dataSource.read_sh2_regs(dataSource.user, cpu, &mSh2[cpu]) != 0;
            mbValid = mbValid || mbHasSh2[cpu];
        }
    }

    // Decoded SCSP voices (live driver only; empty on savestates).
    mScspSlots.clear();
    if ((dataSource.capabilities & SE_CAP_SCSP_SLOTS) && dataSource.read_scsp_slots)
    {
        se_scsp_slot tmp[SE_SCSP_SLOT_COUNT] = {};
        int n = dataSource.read_scsp_slots(dataSource.user, tmp);
        if (n < 0) n = 0;
        if (n > SE_SCSP_SLOT_COUNT) n = SE_SCSP_SLOT_COUNT;
        mScspSlots.assign(tmp, tmp + n);
        mbValid = mbValid || !mScspSlots.empty();
    }

    // Live CD-block state (live driver only; absent on savestates).
    mHasCdStatus = false;
    mCdStatus = se_cd_status{};
    if ((dataSource.capabilities & SE_CAP_CD_STATUS) && dataSource.read_cd_status)
        mHasCdStatus = dataSource.read_cd_status(dataSource.user, &mCdStatus) != 0;
    if (mHasCdStatus) mbValid = true;

    // CRAM color mode from VDP2 RAMCTL (offset 0x0E), bits 12-13.
    mCramMode = SE_CRAM_RGB555_1024;
    if (mbHasVdp2Regs)
    {
        const uint16_t ramctl = Vdp2Reg(0x0E);
        switch ((ramctl >> 12) & 0x3)
        {
        case 1:  mCramMode = SE_CRAM_RGB555_2048; break;
        case 2:  mCramMode = SE_CRAM_RGB888_1024; break;
        default: mCramMode = SE_CRAM_RGB555_1024; break;
        }
    }

    return mbValid;
}

}  // namespace se
