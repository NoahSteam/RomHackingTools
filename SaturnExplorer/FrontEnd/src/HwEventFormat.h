// HwEventFormat -- turn one hardware event record (SeHwEvents.h) into a Log entry: a one-line
// message, a category, an optional clickable source PC and, on demand, the decoded arguments.
//
// Pure: no ImGui, no driver, no clock. The text lives here and nowhere else (the emulator only
// sends numbers), so a newer emulator that adds a kind degrades to "kind 0x.. a0 a1 a2 a3" in an
// older front end instead of failing. Name tables come from the Mednafen sources
// (scu.inc IntNames/sf_to_int_tab, sh7095.h VECNUM_*, cdb.cpp GetCommandDetails/STATUS_*,
// smpc.cpp CMD_*), not from memory.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "LogPanel.h"
#include "SeHwEvents.h"

namespace sfe
{

// The entry for 'e': category, message, source PC (when SE_HWEV_F_HAS_PC), frame. The decoded
// arguments are NOT built here -- a log of tens of thousands of rows would hold a vector of string
// pairs per row for text nobody reads -- the record is kept on the entry (LogEvent::hw) and
// HwEventDetail() decodes it when the row is selected.
LogEvent FormatHwEvent(const SeHwEvent& e);

// "N hardware events lost (emulator ring / client queue)". Shown where the gap occurred.
LogEvent FormatHwLost(uint64_t count, uint32_t frame);

// The decoded arguments of a record as key/value rows (the detail pane's bullets).
std::vector<std::pair<std::string, std::string>> HwEventDetail(const SeHwEvent& e);

// The Log chip a record belongs to, from its kind, or its source bit for a kind this build does
// not know (a SUPPRESSED record carries the source of the events it stands for).
LogCategory HwEventCategory(const SeHwEvent& e);

// ---- Name tables (exposed so the tests can walk every entry) ----

// SCU interrupt source bit 0..13 and 16..31 (A-bus external 0..15); nullptr for bits 14, 15.
const char* HwScuInterruptName(unsigned bit);
// SCU DMA start factor 0..7 (sf_to_int_tab, 7 = the DMA enable bit written by software).
const char* HwScuDmaFactorName(unsigned factor);
// SCU DMA bus 0..2; nullptr otherwise (0xF is what an unmappable address encodes to).
const char* HwScuBusName(unsigned bus);
// SH-2 vector number: the fixed vectors (power-on .. user break), TRAPA 32..63, the SCU's 0x40..0x4D
// and 0x50..0x5F. nullptr for the rest (the on-chip peripherals' vectors are programmable).
std::string HwSh2VectorName(unsigned vector);
// CD block command byte; nullptr when unknown. cdb.cpp's names.
const char* HwCdCommandName(unsigned command);
// CD block status code (low nibble of the status byte); nullptr when unknown.
const char* HwCdStatusName(unsigned code);
// SMPC command; nullptr when unknown.
const char* HwSmpcCommandName(unsigned command);
// SeHwEvent.source bit index 0..7 as shown in a SUPPRESSED row; nullptr otherwise.
const char* HwSourceName(unsigned sourceBit);

}  // namespace sfe
