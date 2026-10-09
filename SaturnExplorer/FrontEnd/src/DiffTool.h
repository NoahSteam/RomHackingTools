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

#include <string>

#include "Debug/MemoryCompare.h"

namespace sfe
{

// What the argument template holds until the user changes it: both folders, quoted so a path with
// spaces survives.
extern const char* const kDefaultDiffArgs;

// Substitute {a} and {b} (frame A's and frame B's folders) into 'tmpl'. An empty template means
// kDefaultDiffArgs. Every occurrence is replaced; text that merely looks like a token is not.
std::string BuildDiffArgs(const std::string& tmpl, const std::string& folderA, const std::string& folderB);

// True when 'tmpl' mentions {a} or {b}; a template with neither would run the tool on nothing.
bool DiffArgsUseFrames(const std::string& tmpl);

// "HWRAM_06000000.bin": the region's name made filename-safe, then its bus address when it has one
// (the VDP1 frame buffer is an app-derived image with none).
std::string DiffRegionFileName(RegionId id);

// "A_frame_1234": the folder one side of a comparison is written to, under the diff root.
std::string DiffSideFolderName(char side, uint64_t frameNo);

// Write every region of 'snap' into 'dir' (created if missing) as DiffRegionFileName files. False,
// with 'error' filled, on the first failure.
bool WriteSnapshotFolder(const MemSnapshot& snap, const std::string& dir, std::string& error);

// Remove the folders an earlier run left under 'root'. Anything that is not a folder of plain
// files is left alone (RemoveFlatDirectory refuses it), so a root the user pointed at something
// else is not emptied. Best effort: a folder the diff tool still has open stays.
void PurgeDiffFolders(const std::string& root);

}  // namespace sfe
