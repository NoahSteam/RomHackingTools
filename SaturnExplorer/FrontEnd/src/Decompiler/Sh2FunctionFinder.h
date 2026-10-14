// Sh2FunctionFinder — which function an address belongs to (DESIGN.md 6.4).
//
// The decompiler needs an entry point; the user points at an instruction. Entries come from a
// ladder of sources, most trusted first:
//
//   User      an entry the user set ("Set function start here", saturn_function_bounds.txt)
//   Symbol    a FunctionNames entry or an imported symbol
//   CallSite  the target of a call the call stack shows (bsr, or jsr @rN loaded from a literal)
//   RtsScan   scanning back from the address to the previous rts/rte and its delay slot,
//             skipping nop padding and the previous function's literal pool, then checking
//             that what follows decodes as instructions
//   Callee    a call target found while decompiling another function
//
// The nearest known entry at or below the address wins, the more trusted source naming it when
// several know the same entry. A known entry is a function start, so a nearer one -- of any
// source -- marks a later function and is never overridden by an older entry further back. An
// rts between the nearest known entry and the address means either the address is in a later,
// unknown function, or the rts is an early return; flow from the known entry decides (the scan
// answers only when that flow provably never reaches the address). A user-set entry is never
// overridden: user corrections exist precisely for the functions the heuristics get wrong.
//
// The rts scan passes over words that only read as rts (a literal), the previous function's
// literal pool and mova-addressed jump table, and -- when the code after the rts never reaches the
// address -- a function that ends in a tail jump rather than rts. Whatever it cannot settle is
// said in EntryGuess::doubt.
//
// Pure functions over a MemorySnapshot: no engine, no ImGui, testable with byte vectors.
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "Decompiler/MemorySnapshot.h"

namespace sfe
{
namespace decomp
{

enum class EntrySource : uint8_t { None, User, Symbol, CallSite, RtsScan, Callee };

// "user-set entry", "symbol", ... for the panel's "entry guessed from rts scan".
const char* EntrySourceName(EntrySource source);

struct EntryGuess
{
    bool ok = false;
    uint32_t entry = 0;            // canonical
    EntrySource source = EntrySource::None;
    int confidence = 0;            // 0..100, for display
    std::string why;               // when !ok
    std::string doubt;             // when ok: why the entry may be wrong, or empty
};

// Known entries, all canonical or not (they are canonicalised here).
struct FinderHints
{
    std::set<uint32_t> userEntries;
    std::set<uint32_t> symbols;
    std::set<uint32_t> callSiteTargets;
    std::set<uint32_t> callees;
};

// How far back a function entry is looked for. Real Saturn functions are far shorter; the cap
// keeps a scan from wandering across megabytes of data.
const uint32_t kMaxFunctionSpan = 0x10000;

EntryGuess FindFunctionContaining(const MemorySnapshot& snapshot, uint32_t address, const FinderHints& hints);

// The rts-scan rung alone. False, with 'why', when no boundary is found within kMaxFunctionSpan
// or what follows it does not decode as code. 'doubt', when not empty, says why the entry found
// may still be wrong (a jump table of unknown length skipped, code that never reaches the address).
bool RtsScanEntry(const MemorySnapshot& snapshot, uint32_t address, uint32_t& entry, std::string* why = nullptr,
                  std::string* doubt = nullptr);

// The function a call instruction at 'site' calls, when the code says so: bsr encodes it, and a
// jsr @rN whose rN was loaded by mov.l @(disp,PC),rN in the instructions just before reads it
// from that literal. False for bsrf, jmp and register calls whose value the code does not show.
bool CallTargetAt(const MemorySnapshot& snapshot, uint32_t site, uint32_t& target);

// Every call target CallTargetAt finds in [entry, end), canonical, sorted, unique.
std::vector<uint32_t> CallTargetsFromListing(const MemorySnapshot& snapshot, uint32_t entry, uint32_t end);

// The user's function-start corrections, persisted next to saturn_functions.txt as one hex
// address per line.
class FunctionBounds
{
public:
    static const char* const kDefaultFile;   // "saturn_function_bounds.txt"

    const std::set<uint32_t>& Entries() const { return mEntries; }
    void SetEntry(uint32_t address);           // canonicalised
    void ClearEntry(uint32_t address);

    // Replaces the set from 'path'; a missing file is an empty set. Lines that are not a hex
    // address (comments, blanks) are skipped.
    bool Load(const std::string& path = kDefaultFile);
    bool Save(const std::string& path = kDefaultFile) const;

private:
    std::set<uint32_t> mEntries;
};

}  // namespace decomp
}  // namespace sfe
