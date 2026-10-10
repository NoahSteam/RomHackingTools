// DiffTool — hand two compared frames to an external diff program (Beyond Compare, WinMerge,
// Meld, ...) instead of the built-in Memory Compare panel.
//
// The tool is given two FOLDERS, one per frame, each holding that frame's memory as one raw file
// per captured region (HWRAM_06000000.bin, ...). A folder compare lists the regions that differ
// and opens a hex compare on each, and the byte offset in the file is the offset in the region, so
// nothing is lost against the built-in panel's view. The user's argument template says where the
// two folders go: "{a}" and "{b}".
//
// Free of ImGui and of the platform, so the templating and the files it writes are unit-testable.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Debug/MemoryCompare.h"

namespace sfe
{

// What the argument template holds until the user changes it: both folders, quoted so a path with
// spaces survives.
extern const char* const kDefaultDiffArgs;

// The tool's arguments, one string per argument, for 'tmpl' with {a} and {b} (frame A's and frame B's
// folders) filled in. An empty template means kDefaultDiffArgs.
//
// The template is split FIRST -- whitespace separates arguments, "..." or '...' group them (so a
// quoted {a} stays one argument), nothing is escaped or expanded -- and the folders are substituted
// afterwards as literal text. The result goes to the program as an argv, so a folder path holding
// `$`, a backtick, a quote or a space reaches it unchanged; it is never handed to a shell. Every
// occurrence of a token is replaced, and text that merely looks like one is not.
std::vector<std::string> BuildDiffArgv(const std::string& tmpl, const std::string& folderA,
                                       const std::string& folderB);

// True when 'tmpl' mentions {a} or {b}; a template with neither would run the tool on nothing.
bool DiffArgsUseFrames(const std::string& tmpl);

// "HWRAM_06000000.bin": the region's name made filename-safe, then its bus address when it has one
// (the VDP1 frame buffer is an app-derived image with none).
std::string DiffRegionFileName(RegionId id);

// Every comparison gets folders of its own, so opening a new one never replaces files an earlier diff
// window is still showing (frame numbers are reused after a state load, a branch of history or a
// reconnect, and the same pair of numbers can then name different memory). The id carries the time
// it was made, which is what retention goes by, and a random part so two instances never collide:
// "cmp1760000000-9f3a07c1".
std::string NewDiffComparisonId(uint64_t nowSeconds);

// "cmp1760000000-9f3a07c1_A_frame_1234": the folder one side of a comparison is written to, a sibling
// of the other comparisons' folders under the diff root.
std::string DiffSideFolderName(const std::string& comparisonId, char side, uint64_t frameNo);

// Write every region of 'snap' into 'dir' (created if missing) as DiffRegionFileName files. False,
// with 'error' filled, on the first failure.
bool WriteSnapshotFolder(const MemSnapshot& snap, const std::string& dir, std::string& error);

// How long a comparison's folders are kept. The diff program is deliberately left running when this
// one exits, so a restart must not take away files it has open (on POSIX an open file can still be
// deleted); a week is long past any diff window left open on purpose.
constexpr uint64_t kDiffKeepSeconds = 7u * 24u * 3600u;

// Remove the comparison folders under 'root' made 'keepSeconds' or more before 'nowSeconds'. With
// keepSeconds 0 -- the explicit "delete saved comparison files" -- remove all of them, and the
// un-numbered folders earlier versions wrote too: those have no date in their name to age them by,
// and an upgrade must not delete the inputs of a diff window opened before it, so the automatic cleanup
// leaves them. Folders whose names are not
// ours are never touched, and a folder that holds anything but plain files is left alone
// (RemoveFlatDirectory refuses it), so a root the user pointed at something else is not emptied.
// Best effort: a folder the diff tool still has open on Windows stays.
void PurgeDiffFolders(const std::string& root, uint64_t nowSeconds, uint64_t keepSeconds);

}  // namespace sfe
