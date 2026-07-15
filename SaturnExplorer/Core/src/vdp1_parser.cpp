#include "vdp1_parser.h"

namespace se
{

namespace
{

constexpr uint32_t kCommandSize = 0x20;   // bytes per command table entry
constexpr size_t   kMaxCommands = 0x4000; // safety cap on a runaway list

// CMDCTRL jump-select field (bits 14-12).
enum Jp
{
    JP_JUMP_NEXT   = 0,
    JP_JUMP_ASSIGN = 1,
    JP_JUMP_CALL   = 2,
    JP_JUMP_RETURN = 3,
    JP_SKIP_NEXT   = 4,
    JP_SKIP_ASSIGN = 5,
    JP_SKIP_CALL   = 6,
    JP_SKIP_RETURN = 7
};

uint16_t ReadU16(const std::vector<uint8_t>& vram, uint32_t offset)
{
    // Saturn VRAM is big-endian.
    return static_cast<uint16_t>((vram[offset] << 8) | vram[offset + 1]);
}

int16_t ReadS16(const std::vector<uint8_t>& vram, uint32_t offset)
{
    return static_cast<int16_t>(ReadU16(vram, offset));
}

se_command_type CommandType(uint16_t comm)
{
    switch (comm)
    {
    case 0x0: return SE_CMD_NORMAL_SPRITE;
    case 0x1: return SE_CMD_SCALED_SPRITE;
    case 0x2: return SE_CMD_DISTORTED_SPRITE;
    case 0x4: return SE_CMD_POLYGON;
    case 0x5: return SE_CMD_POLYLINE;
    case 0x6: return SE_CMD_LINE;
    case 0x8: return SE_CMD_USER_CLIP;
    case 0x9: return SE_CMD_SYSTEM_CLIP;
    case 0xA: return SE_CMD_LOCAL_COORD;
    default:  return SE_CMD_UNKNOWN;
    }
}

se_draw_mode DrawMode(uint16_t pmod, uint16_t colorCalc)
{
    if ((pmod >> 8) & 0x1)  // Mesh
    {
        return SE_DRAW_MESH;
    }
    switch (colorCalc)
    {
    case SE_CC_SHADOW:           return SE_DRAW_SHADOW;
    case SE_CC_HALF_LUMINANCE:   return SE_DRAW_HALF_LUM;
    case SE_CC_HALF_TRANSPARENT: return SE_DRAW_HALF_TRANS;
    default:                     return SE_DRAW_NORMAL;
    }
}

// Decode one command table entry at 'address' into 'cmd'.
void DecodeCommand(const std::vector<uint8_t>& vram, uint32_t address,
                   uint32_t index, se_command& cmd)
{
    const uint16_t ctrl = ReadU16(vram, address + 0x00);
    const uint16_t link = ReadU16(vram, address + 0x02);
    const uint16_t pmod = ReadU16(vram, address + 0x04);
    const uint16_t colr = ReadU16(vram, address + 0x06);
    const uint16_t srca = ReadU16(vram, address + 0x08);
    const uint16_t size = ReadU16(vram, address + 0x0A);
    const int16_t  xa   = ReadS16(vram, address + 0x0C);
    const int16_t  ya   = ReadS16(vram, address + 0x0E);
    const uint16_t grda = ReadU16(vram, address + 0x1C);

    const uint16_t end     = (ctrl >> 15) & 0x1;
    const uint16_t jp      = (ctrl >> 12) & 0x7;
    const uint16_t comm    = ctrl & 0xF;
    const uint16_t colorMode = (pmod >> 3) & 0x7;
    const uint16_t colorCalc = pmod & 0x7;

    cmd = se_command {};
    cmd.index = index;
    cmd.table_address = address;
    cmd.link_address = static_cast<uint32_t>(link) * 8;
    cmd.type = CommandType(comm);

    if (end)
    {
        cmd.status = SE_CMDSTAT_END;
    }
    else if (jp >= JP_SKIP_NEXT)
    {
        cmd.status = SE_CMDSTAT_SKIP;
    }
    else
    {
        cmd.status = SE_CMDSTAT_NORMAL;
    }

    cmd.color_mode = static_cast<se_color_mode>(colorMode);
    cmd.color_calc_mode = static_cast<se_color_calc>(colorCalc);
    cmd.draw_mode = DrawMode(pmod, colorCalc);

    cmd.texture_address = static_cast<uint32_t>(srca) * 8;
    cmd.gouraud_table = static_cast<uint32_t>(grda) * 8;
    if (cmd.color_mode == SE_COLOR_LUT_16)
    {
        cmd.clut_address = static_cast<uint32_t>(colr) * 8;
        cmd.palette_bank = 0;
    }
    else
    {
        cmd.clut_address = 0;
        cmd.palette_bank = colr;
    }

    cmd.width = ((size >> 8) & 0x3F) * 8;
    cmd.height = size & 0xFF;
    cmd.x = xa;
    cmd.y = ya;

    cmd.scale_x = 1.0f;   // resolved for scaled/distorted sprites in M3
    cmd.scale_y = 1.0f;
    cmd.rotation_deg = 0.0f;

    cmd.flip_x = (ctrl >> 4) & 0x1;
    cmd.flip_y = (ctrl >> 5) & 0x1;
    cmd.priority = 0;     // from VDP2 SPCTL — resolved later
    cmd.gouraud = (colorCalc & 0x4) ? 1 : 0;
    cmd.color_calc = (colorCalc != SE_CC_REPLACE) ? 1 : 0;

    const uint16_t spd = (pmod >> 6) & 0x1;  // transparent-pixel disable
    cmd.transparency = spd ? SE_TRANSP_NONE : SE_TRANSP_PER_PIXEL;

    cmd.raw_cmdctrl = ctrl;
    cmd.raw_cmdpmod = pmod;
    cmd.description[0] = '\0';
}

}  // namespace

void Vdp1Parser::Parse(const std::vector<uint8_t>& vdp1Vram,
                       std::vector<se_command>& out)
{
    out.clear();
    if (vdp1Vram.size() < kCommandSize)
    {
        return;
    }

    std::vector<uint32_t> callStack;
    std::vector<uint8_t> visited(vdp1Vram.size() / kCommandSize, 0);

    uint32_t address = 0;
    uint32_t index = 0;
    while (index < kMaxCommands)
    {
        if (address + kCommandSize > vdp1Vram.size())
        {
            break;  // link ran past VRAM
        }
        const uint32_t slot = address / kCommandSize;
        if (visited[slot])
        {
            break;  // cycle — stop before looping forever
        }
        visited[slot] = 1;

        out.emplace_back();
        DecodeCommand(vdp1Vram, address, index, out.back());
        ++index;

        const uint16_t ctrl = ReadU16(vdp1Vram, address);
        if ((ctrl >> 15) & 0x1)  // END bit
        {
            break;
        }

        const uint16_t jp = (ctrl >> 12) & 0x7;
        const uint32_t linkAddr = static_cast<uint32_t>(ReadU16(vdp1Vram, address + 0x02)) * 8;
        switch (jp)
        {
        case JP_JUMP_NEXT:
        case JP_SKIP_NEXT:
            address += kCommandSize;
            break;
        case JP_JUMP_ASSIGN:
        case JP_SKIP_ASSIGN:
            address = linkAddr;
            break;
        case JP_JUMP_CALL:
        case JP_SKIP_CALL:
            callStack.push_back(address + kCommandSize);
            address = linkAddr;
            break;
        case JP_JUMP_RETURN:
        case JP_SKIP_RETURN:
            if (callStack.empty())
            {
                return;
            }
            address = callStack.back();
            callStack.pop_back();
            break;
        default:
            address += kCommandSize;
            break;
        }
    }
}

}  // namespace se
