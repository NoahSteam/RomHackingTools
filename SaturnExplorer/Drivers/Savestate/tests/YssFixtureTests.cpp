// Real Yabause savestates, opened through the same entry point the app uses.
//
// OFF-01 asked for two things: stronger validation of a structurally-recognized .yss, and real
// fixtures from every emulator family the driver claims to support. The two belong together --
// validation tightened without a real file to check it against is how you refuse the states that
// actually work. Writing the checks in SavestateDriver.cpp, one guess did fail here first: a
// "display must be on" test on TVMD, which one of these three states would have failed, because
// it was captured with the display blanked.
//
// The fixtures are Yabause 0.9.x states committed in this repository. Nothing here downloads or
// generates them, and if the directory is absent the test says so and passes rather than
// silently asserting nothing -- so a checkout without the game data still builds and runs green.
//
// Still missing: a real Mednafen/Beetle MDFNSVST state, and a state from Yaba Sanshiro or Kronos.
// Their layouts are asserted from the struct being shared across the lineage, which is an
// argument, not a measurement. SavestateShapeTests covers those paths synthetically; only a real
// file can catch a difference nobody expected.
#include "SavestateDriver.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int gFailures = 0;
int gChecked = 0;

void Check(bool ok, const char* what, int line)
{
    if (ok) return;
    std::printf("CHECK failed at line %d: %s\n", line, what);
    ++gFailures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

bool Exists(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

// Every region a Yabause state carries, so a validation change that quietly drops one is a
// failure here rather than a background that stops rendering with no test to say why.
void CheckYabauseState(const std::string& path)
{
    se_data_source ds{};
    const se_result r = se_savestate_open(path.c_str(), &ds);
    CHECK(r == SE_OK);
    if (r != SE_OK) return;
    ++gChecked;

    const uint32_t wanted = SE_CAP_VDP1_VRAM | SE_CAP_VDP2_VRAM | SE_CAP_CRAM |
                            SE_CAP_VDP2_REGS | SE_CAP_MAIN_RAM | SE_CAP_SH2_REGS;
    CHECK((ds.capabilities & wanted) == wanted);

    // RAMCTL's CRAM mode is what the palette decode hangs off, and it is also half of the
    // register sanity check, so pin the value this family really stores.
    CHECK(ds.read_vdp2_reg != nullptr);
    const uint16_t ramctl = ds.read_vdp2_reg(ds.user, 0x00E);
    CHECK(((ramctl >> 12) & 0x3u) != 0x3u);

    uint8_t probe[16] = {};
    CHECK(ds.read_vdp1_vram && ds.read_vdp1_vram(ds.user, 0, probe, sizeof(probe)) == sizeof(probe));
    CHECK(ds.read_vdp2_vram && ds.read_vdp2_vram(ds.user, 0, probe, sizeof(probe)) == sizeof(probe));
    CHECK(ds.read_cram && ds.read_cram(ds.user, 0, probe, sizeof(probe)) == sizeof(probe));

    if (ds.close) ds.close(ds.user);
}

}  // namespace

int main()
{
    const std::string dir = SE_YSS_FIXTURE_DIR;
    const char* names[] = { "GS-9169_007.yss", "GS-9169_008.yss", "GS-9169_009.yss" };

    int found = 0;
    for (const char* name : names)
    {
        const std::string path = dir + "/" + name;
        if (!Exists(path)) continue;
        ++found;
        CheckYabauseState(path);
    }

    if (found == 0)
    {
        std::printf("YssFixtureTests: no fixtures under %s -- skipped\n", dir.c_str());
        return 0;
    }
    if (gFailures)
    {
        std::printf("YssFixtureTests: %d check(s) failed over %d state(s)\n", gFailures, gChecked);
        return 1;
    }
    std::printf("YssFixtureTests: %d Yabause state(s) opened\n", gChecked);
    return 0;
}
