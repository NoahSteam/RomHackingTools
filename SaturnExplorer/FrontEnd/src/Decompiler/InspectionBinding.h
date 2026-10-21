// Engine-side adapter used while markup and Funcdata are alive. Never retain its inputs.
// Keep this separate from the owned result/snapshot/UI interfaces.
#pragma once
#include "Decompiler/Sh2Decompiler.h"
#include <map>
#include <set>
namespace ghidra { class Funcdata; class Varnode; class PcodeOp; class AddrSpaceManager; }
namespace sfe { namespace decomp {
using PrintedOperations = std::map<const ghidra::PcodeOp*, std::pair<std::set<size_t>, int>>;
// Decode owned line/inspection metadata from the emitter's packed markup.
std::vector<Line> DecodeDecompilerMarkup(const std::string& packed, const ghidra::Funcdata& function,
                                        const ghidra::AddrSpaceManager* spaces,
                                        std::vector<InspectionTarget>& targets, PrintedOperations* printed = nullptr);
bool ResolvePrintedInspection(const ghidra::Varnode* variable, const ghidra::PcodeOp* operation,
                              const ghidra::AddrSpaceManager* spaces, InspectionTarget& target);
} }
