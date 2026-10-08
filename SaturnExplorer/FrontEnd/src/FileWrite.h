// FileWrite — the one place a file actually reaches the disk, and the one place that knows
// how to not lose what was already there.
//
// Every save and export in the app used to end in the same three lines: fopen, fwrite,
// fclose — with fclose's result thrown away. That is not a nitpick: a stdio write smaller
// than the buffer does not touch the device at all, so the ENOSPC from a full disk surfaces
// only at the flush inside fclose. Ignoring it reports "saved" for a file that was never
// written. The same applies to std::ofstream, whose destructor flushes after any `if
// (stream)` check the caller wrote.
//
// Opening "wb" also truncates before the first byte is written, so a failed save destroys
// the previous good file. WriteFileAtomically stages into a sibling and renames, which both
// publishes all-or-nothing and leaves the old content alone when the write fails.
//
// Deliberately free of platform headers and of IPlatform, so it can be tested directly —
// including the failure paths, which is what FileWriteTests does against /dev/full.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace sfe
{

// Write 'size' bytes to 'path', reporting every failure stdio can produce -- the open, a
// short write, and the flush that happens inside the close.
//
// Normally staged: the bytes go to a sibling temporary and are renamed over 'path' once they
// are safely on disk, so 'path' either holds the whole new file or is untouched. The staging
// is skipped when 'path' names something that is not a regular file (a device, a FIFO, a
// directory): renaming over those would replace the thing itself rather than its contents,
// which is never what a save meant. Such a target is written directly, and the close is still
// checked -- that is the /dev/full case.
//
// 'error' is filled with a human-readable reason on failure, and cleared on success.
bool WriteFileAtomically(const std::string& path, const void* data, size_t size,
                         std::string& error);

// Create one directory. True if it now exists, including when it already did.
bool MakeDirectory(const std::string& path);

// Delete one file. True if it is now gone, including when it never existed.
bool RemoveFile(const std::string& path);

// Delete one empty directory. True if it is now gone, including when it never existed.
bool RemoveEmptyDirectory(const std::string& path);

// Move 'from' to 'to' on the same filesystem. Both files and directories; 'to' must not
// exist, which is what makes the publish step in LayerExport a swap of two renames rather
// than a replace (POSIX rename refuses a non-empty directory, Windows refuses any).
bool MovePath(const std::string& from, const std::string& to);

// True when 'path' names something that exists, of any kind. Spelled out rather than
// PathExists because DataSearch.h already exports that name for the same thing, and the two
// modules do not share every build target.
bool FileOrDirectoryExists(const std::string& path);

// The entries directly inside 'path', excluding "." and "..", in no particular order.
// False when 'path' cannot be opened. Needed because C++14 has no <filesystem> and the
// publish step has to be able to delete a directory whose contents it did not write (the
// export it is replacing may have held a different set of files).
bool ListDirectory(const std::string& path, std::vector<std::string>& names);

// Delete a directory of plain files and then the directory itself. Refuses -- and changes
// nothing -- if it holds anything that is not a regular file, because every directory this
// is used on is flat by construction, so a subdirectory means something unexpected is there
// and deleting it blind is how a tool eats somebody's work.
bool RemoveFlatDirectory(const std::string& path);

// The platform's path separator, so callers need not repeat the #ifdef.
char PathSeparator();

}  // namespace sfe
