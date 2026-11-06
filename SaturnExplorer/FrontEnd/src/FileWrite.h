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
#include <cstdint>
#include <string>
#include <vector>

namespace sfe
{

// Write 'size' bytes to 'path', reporting every failure stdio can produce -- the open, a
// short write, and the flush that happens inside the close.
//
// Normally staged: the bytes go to a sibling temporary and are renamed over 'path' once they
// have been flushed to the device (fsync / FlushFileBuffers; the folder is synced after the rename on POSIX, A9-48), so 'path' either holds the whole new file or is untouched. The staging
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
// and deleting it blind is how a tool eats somebody's work. It also refuses when 'path' itself
// is a link (IsLink): listing through a directory symlink or junction would empty whatever
// directory it points at.
bool RemoveFlatDirectory(const std::string& path);

// True when 'path' itself is a symbolic link -- on Windows, any reparse point, which covers
// directory symlinks and junctions. Not followed: a dangling link is still a link.
bool IsLink(const std::string& path);

// Delete the link at 'path' and nothing it points to. True if it is now gone.
bool RemoveLink(const std::string& path);

// The platform's path separator, so callers need not repeat the #ifdef.
char PathSeparator();

// This process's id, for names that must not collide with another instance's (staging files and
// directories), so callers need not repeat the #ifdef either.
unsigned long ProcessId();

// An exclusive advisory lock on the file at 'path' (created empty if missing), held for the
// object's lifetime: flock on POSIX, LockFileEx on Windows. It excludes other processes and
// other FileLocks in this one, and the OS drops it if the process dies, so a crash never
// leaves it held. The descriptor is close-on-exec (Windows handles are not inheritable by
// default): an emulator launched while the lock is held must not inherit it, or the lock would
// stay held until that emulator exits. With wait=false, Held() is false when someone else has it; with wait=true the wait ends
// after kWaitMs (A9-40), so a holder that never lets go cannot freeze the caller, and Held() is false. WhyNotHeld() says why,
// with the OS reason when the file could not be opened (a folder that cannot be written or is gone, A9-42).
class FileLock
{
public:
    // 'tryMs' (with wait=false): retry for that many milliseconds before refusing as "another process holds" (A9-52).
    explicit FileLock(const std::string& path, bool wait = true, unsigned tryMs = 0);
    ~FileLock();
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    bool Held() const { return mHeld; }
    std::string WhyNotHeld() const;   // empty when held
    static constexpr unsigned kWaitMs = 3000;
    static constexpr unsigned kExitWaitMs = 10000;   // the bound once SetClosing(true) has been called (A9-46)
    // Called when the application is shutting down: a waiting lock then waits up to kExitWaitMs, because a change refused now is lost
    // with the window and the freeze costs nothing. The kExitWaitMs is one budget for all the locks taken after the call (A9-53).
    static void SetClosing(bool closing);

private:
    std::string mPath;
    unsigned mWaitMs = kWaitMs;
    int mError = 0;          // errno / GetLastError of the open or the lock
    bool mTimedOut = false;
    bool mHeld = false;
    intptr_t mHandle = -1;   // a POSIX descriptor, or a Windows HANDLE
};

// Tell the user something before the window goes, where the Log panel is no longer there to read: a native message box on Windows,
// a line on stderr elsewhere. For a save refused at exit (A9-46).
void NoticeAtExit(const std::string& text);
// A platform that has a better place for the notice than stderr (the SDL2 builds: SDL_ShowSimpleMessageBox) sets it; null restores the default.
void SetExitNoticeHook(void (*hook)(const std::string&));

// Remove the staging files "<path>.separt-<pid>-<n>" that a writer killed between its write and its rename left beside 'path'
// (A9-37). Call it only while holding the lock that serialises the writers of 'path': then no live writer of that file has one.
// Even so it removes only a regular file (never a link or a folder) whose name is exactly that form, and only when the
// process named in it is gone or the file is over ten minutes old. POSIX only: on Windows it does nothing. Returns the count.
size_t RemoveStaleStagingFiles(const std::string& path);

// Move the file 'from' over 'path', replacing what is there in one step (rename on POSIX,
// MoveFileEx with REPLACE_EXISTING on Windows), so 'path' holds one complete file or the other.
// A symlink at 'path' is itself replaced; the file it pointed to is left alone.
bool PublishFile(const std::string& from, const std::string& path, std::string& error);

// Create a new, empty file beside 'path' whose name did not exist before this call, and return
// its name ("" on failure, with 'error' filled). Exclusive creation is what makes it safe to
// delete later: a name chosen by convention alone may belong to somebody else.
std::string CreateStagingFile(const std::string& path, std::string& error);

// 'path' resolved to an absolute path with every symlink, "." and ".." removed (realpath; on
// Windows the full path). "" when it does not exist or cannot be resolved.
std::string CanonicalPath(const std::string& path);

// True when something exists at 'path' itself -- a dangling symlink included, which
// FileOrDirectoryExists (it follows links) would report as absent.
bool PathEntryExists(const std::string& path);

// True when 'path' exists and is not a regular file (a directory, device, FIFO or socket).
bool ExistsAsNonRegularFile(const std::string& path);

// True when both paths exist and name the same file -- through a symlink, a hard link or a
// different spelling. False when either is missing.
bool SameFile(const std::string& a, const std::string& b);

// True when 'inner' is 'outer' itself or lies beneath it, after both are resolved with
// CanonicalPath. False when either does not exist.
bool PathIsWithin(const std::string& inner, const std::string& outer);

}  // namespace sfe
