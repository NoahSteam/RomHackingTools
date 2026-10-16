// The user ops the SH-2 spec calls (superh.sinc): the p-code emulator refuses an unhooked one. They carry a
// value for the decompiler to print and change nothing the emulator models (the register is written by
// the instruction's own p-code), so the hook accepts and does nothing.
#pragma once

#include "emulate.hh"

namespace dts
{

class NoEffectUserop : public ghidra::BreakCallBack
{
public:
    bool pcodeCallback(ghidra::PcodeOpRaw*) override { return true; }
};

// Call once after the break table is made, with the table the emulator will use.
inline void HookSh2Userops(ghidra::BreakTableCallBack& table)
{
    static NoEffectUserop noEffect;
    for (const char* name : { "Write_SR", "Write_GBR", "Write_VBR" }) table.registerPcodeCallback(name, &noEffect);
}

}  // namespace dts
