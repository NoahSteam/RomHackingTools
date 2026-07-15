// Context — the C++ core object behind the opaque se_context* handle. The C ABI
// shim in host_abi.cpp is a thin translation layer over this class; all real
// logic lives on the C++ side (ARCHITECTURE.md §3, the hybrid boundary).
#pragma once

#include <cstring>
#include <vector>

#include "saturnexplorer/se_host.h"
#include "hardware_snapshot.h"
#include "vdp1_parser.h"

namespace se
{

class Context
{
public:
    Context(const se_data_source& dataSource, const se_config& config)
        : mDs(dataSource), mCfg(config)
    {
    }

    ~Context()
    {
        if (mDs.close)
        {
            mDs.close(mDs.user);
        }
    }

    // Snapshot state for the current frame, then parse the VDP1 command list.
    se_result BeginFrame()
    {
        if (!mSnapshot.Capture(mDs))
        {
            return SE_ERR_NO_DATA;
        }
        Vdp1Parser::Parse(mSnapshot.Vdp1Vram(), mCommands);
        return SE_OK;
    }

    // --- Query surface. ---

    size_t CommandCount() const { return mCommands.size(); }

    se_result GetCommand(size_t index, se_command* out) const
    {
        if (index >= mCommands.size())
        {
            return SE_ERR_OUT_OF_RANGE;
        }
        *out = mCommands[index];
        return SE_OK;
    }

    size_t SpriteCount() const { return 0; }
    size_t VramRegionCount() const { return 0; }

    bool HasSnapshot() const { return mSnapshot.Valid(); }

    const se_data_source& DataSource() const { return mDs; }
    const se_config& Config() const { return mCfg; }

private:
    se_data_source          mDs;
    se_config               mCfg;
    HardwareSnapshot        mSnapshot;
    std::vector<se_command> mCommands;
};

}  // namespace se
