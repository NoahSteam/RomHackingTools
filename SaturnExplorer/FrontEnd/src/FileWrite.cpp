#include "FileWrite.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <system_error>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>        // _unlink, _open
#include <process.h>   // _getpid
#include <windows.h>
#include <sys/stat.h>
#include <sys/types.h>
#define SE_MKDIR(p)  _mkdir(p)
#define SE_RMDIR(p)  _rmdir(p)
#define SE_UNLINK(p) _unlink(p)
#define SE_STAT      struct _stat
#define SE_STAT_FN   _stat
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>    // flock
#include <csignal>
#include <ctime>
#include <sys/types.h>
#include <unistd.h>
#define SE_MKDIR(p)  ::mkdir((p), 0755)
#define SE_RMDIR(p)  ::rmdir(p)
#define SE_UNLINK(p) ::unlink(p)
#define SE_STAT      struct stat
#define SE_STAT_FN   ::stat
#endif

namespace sfe
{

namespace
{

#ifdef _WIN32
const char kSep = '\\';
#else
const char kSep = '/';
#endif

std::string Reason(const char* what, const std::string& path)
{
    // errno is captured by the caller immediately after the failing call; strerror keeps the
    // message specific ("No space left on device") rather than a generic "could not write".
    const int err = errno;
    std::string out = std::string(what) + " " + path;
    if (err != 0) out += ": " + std::string(std::strerror(err));
    return out;
}

// fwrite/fclose with every result checked. This is the whole point of the file: the close is
// where a buffered write to a full device finally fails. Takes ownership of 'f'.
//
// The file is closed BEFORE any text is made. The text is a handful of strings, and an allocation
// that fails in one of them used to leave the FILE and its descriptor open for good (A6-43): a
// disk that is full and a heap that is short at the same moment is rare, but the cost of the
// order is nothing. errno is captured at the failing call and put back for Reason.
bool WriteAndClose(FILE* f, const std::string& path, const void* data, size_t size,
                   std::string& error, bool sync = false, const std::string& target = std::string())
{
    errno = 0;
    const size_t wrote = (size == 0) ? 0 : std::fwrite(data, 1, size, f);
    if (wrote != size)
    {
        const int err = errno;
        std::fclose(f);   // result ignored on purpose: the write already failed
        errno = err;
        error = Reason("could not write", path);
        return false;
    }
    // A9-48: a staged file's bytes are asked to reach the device before it is renamed over the old one, so a power cut shortly after
    // cannot leave a path that holds an empty file (a file system that does not order the data before the rename). Not for a device
    // written in place (/dev/full, a FIFO: fsync refuses those). A file system that cannot sync at all (EINVAL, ENOTSUP) is not a failure.
    if (sync)
    {
        errno = 0;
        bool synced = std::fflush(f) == 0;
        int err = errno;
        bool winError = false;   // err holds a GetLastError code (Windows only)
        (void)winError;
        if (synced)
        {
#ifdef _WIN32
            // A9-55 (uncompiled on Linux): the failure is in GetLastError, not errno; keep it and let Reason show it.
            errno = 0;
            synced = ::FlushFileBuffers(reinterpret_cast<HANDLE>(::_get_osfhandle(::_fileno(f)))) != 0;
            if (!synced)
            {
                const DWORD last = ::GetLastError();
                if (last == ERROR_INVALID_FUNCTION) synced = true;   // a device that cannot sync is not a failure
                else err = static_cast<int>(last), winError = true;
            }
            else err = 0;
#elif defined(__APPLE__)
            // A9-55 (uncompiled on Linux): fall back to fsync only when F_FULLFSYNC is not supported; a real I/O error from it is a failure.
            errno = 0;
            synced = ::fcntl(::fileno(f), F_FULLFSYNC) == 0;
            if (!synced && (errno == ENOTSUP || errno == EINVAL))
                synced = ::fsync(::fileno(f)) == 0 || errno == EINVAL || errno == ENOTSUP;
            err = errno;
#else
            errno = 0;
            synced = ::fsync(::fileno(f)) == 0 || errno == EINVAL || errno == ENOTSUP || errno == EROFS;
            err = errno;
#endif
        }
        if (!synced)
        {
            std::fclose(f);
            // A9-55: 'path' is the staging file, removed before the text is shown; the text names the file the user knows.
            std::string reason;
#ifdef _WIN32
            reason = winError ? std::system_category().message(err) : std::string(std::strerror(err));
#else
            reason = std::strerror(err);
#endif
            error = "could not flush the new " + (target.empty() ? path : target) + " to the device" + (err != 0 ? ": " + reason : std::string());
            return false;
        }
    }
    // THE line this file exists for. fclose flushes, and a small write has not reached the
    // device before this point, so this is where ENOSPC and EIO are reported.
    errno = 0;
    if (std::fclose(f) != 0)
    {
        // fclose has released the descriptor whether or not it succeeded.
        error = Reason("could not finish writing", path);
        return false;
    }
    error.clear();
    return true;
}

bool WriteDirect(const std::string& path, const void* data, size_t size, std::string& error)
{
    errno = 0;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
    {
        error = Reason("could not open", path);
        return false;
    }
    return WriteAndClose(f, path, data, size, error);
}

}  // namespace

unsigned long ProcessId()
{
#ifdef _WIN32
    return (unsigned long)_getpid();
#else
    return (unsigned long)::getpid();
#endif
}

namespace
{

// Create a staging file beside 'path' that did not exist before this call, and open it.
//
// A fixed name ("<path>.separt") cannot be told apart from somebody else's file of that name,
// so clearing it before use deleted whatever was there. O_EXCL makes the file ours by
// construction: only a name this call created is ever written, and so only it is ever removed.
FILE* OpenStagingFile(const std::string& path, std::string& temp, std::string& error)
{
    static std::atomic<unsigned> counter(0);
    const unsigned long pid = ProcessId();
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        temp = path + ".separt-" + std::to_string(pid) + "-" + std::to_string(++counter);
        errno = 0;
#ifdef _WIN32
        const int fd = ::_open(temp.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
                               _S_IREAD | _S_IWRITE);
#else
        const int fd = ::open(temp.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0666);
#endif
        if (fd < 0)
        {
            if (errno == EEXIST) continue;
            error = Reason("could not open", temp);
            temp.clear();
            return nullptr;
        }
#ifdef _WIN32
        FILE* f = ::_fdopen(fd, "wb");
#else
        FILE* f = ::fdopen(fd, "wb");
#endif
        if (f == nullptr)
        {
            // Release the descriptor and the name before any text is made (A6-43).
            const int err = errno;
#ifdef _WIN32
            ::_close(fd);
#else
            ::close(fd);
#endif
            RemoveFile(temp);
            errno = err;
            error = Reason("could not open", temp);
            temp.clear();
        }
        return f;
    }
    errno = EEXIST;
    error = Reason("could not create a staging file for", path);
    temp.clear();
    return nullptr;
}

}  // namespace

bool WriteFileAtomically(const std::string& path, const void* data, size_t size,
                         std::string& error)
{
    error.clear();
    if (path.empty())
    {
        error = "no destination path";
        return false;
    }
    // A device, FIFO or directory is written in place or not at all -- renaming over one
    // would replace the object instead of its contents.
    if (ExistsAsNonRegularFile(path))
    {
        return WriteDirect(path, data, size, error);
    }

    // What the user set on the file survives the save (A9-28). A read-only file is the user's protection: it is not
    // replaced, and the save says so (Windows does the same, MoveFileEx refuses). Its permission bits go to the new file.
    // A hard link to it is not kept: the new file has a new identity, as for a symlink (A9-15, accepted).
    unsigned keepMode = 0;
    bool haveMode = false;
#ifndef _WIN32
    {
        struct stat st;
        if (::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode))
        {
            if ((st.st_mode & 0222) == 0)
            {
                error = path + " is read-only; it was not replaced (clear the read-only bit to save)";
                return false;
            }
            keepMode = static_cast<unsigned>(st.st_mode & 07777);
            haveMode = true;
        }
    }
#endif
    std::string temp;
    FILE* f = OpenStagingFile(path, temp, error);
    if (f == nullptr) return false;
    // The staging file is ours and goes unless it was published, on every way out of here: an allocation failure
    // while an error text is made used to leave it beside the destination for good (A6-43). unlink allocates nothing,
    // so this holds under sustained exhaustion too.
    struct TempGuard
    {
        const std::string& path;
        bool armed;
        ~TempGuard() { if (armed) RemoveFile(path); }
    } guard{ temp, true };
    if (!WriteAndClose(f, temp, data, size, error, true, path)) return false;
#ifndef _WIN32
    if (haveMode) (void)::chmod(temp.c_str(), static_cast<mode_t>(keepMode));   // best effort: the file is still whole if it fails
#else
    (void)keepMode; (void)haveMode;
#endif
    if (!PublishFile(temp, path, error)) return false;
    guard.armed = false;   // the rename took the name
#ifndef _WIN32
    // The rename itself is a change to the folder: ask for it to reach the device too (best effort; the file is already published).
    {
        const size_t slash = path.find_last_of('/');
        const std::string dir = slash == std::string::npos ? std::string(".") : (slash == 0 ? std::string("/") : path.substr(0, slash));
        const int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dfd >= 0) { (void)::fsync(dfd); ::close(dfd); }
    }
#endif
    return true;
}

bool PublishFile(const std::string& from, const std::string& path, std::string& error)
{
    // Publish by replacing, never by deleting first. std::rename does that on POSIX. On
    // Windows std::rename refuses an existing destination, and the obvious workaround --
    // remove it, then rename -- is what this function exists to avoid: if the rename then
    // failed, or the process died in between, the old file would be gone and the new one
    // never published. MoveFileEx with MOVEFILE_REPLACE_EXISTING replaces in one step, so
    // the destination always holds one complete file or the other.
    //
    // MOVEFILE_WRITE_THROUGH asks for the change to reach the disk before it returns, so a
    // power loss cannot leave the directory entry pointing at a file whose contents never
    // landed.
#ifdef _WIN32
    if (::MoveFileExA(from.c_str(), path.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
    {
        // Nothing has been removed, so the previous contents of 'path' are still there.
        error = "could not publish " + path;
        return false;
    }
#else
    errno = 0;
    if (std::rename(from.c_str(), path.c_str()) != 0)
    {
        // Same guarantee: a failed rename leaves the destination untouched.
        error = Reason("could not publish", path);
        return false;
    }
#endif
    error.clear();
    return true;
}

bool MakeDirectory(const std::string& path)
{
    if (path.empty()) return false;
    if (SE_MKDIR(path.c_str()) == 0) return true;
    return errno == EEXIST;
}

bool RemoveFile(const std::string& path)
{
    if (path.empty()) return false;
    errno = 0;
    // unlink, not std::remove: remove() falls back to rmdir, so it deletes an empty
    // DIRECTORY sitting at this path. The staging path is cleared before use, and a
    // directory that happens to share that name belongs to someone else.
    if (SE_UNLINK(path.c_str()) == 0) return true;
    return errno == ENOENT;
}

bool RemoveEmptyDirectory(const std::string& path)
{
    if (path.empty()) return false;
    errno = 0;
    if (SE_RMDIR(path.c_str()) == 0) return true;
    return errno == ENOENT;
}

bool MovePath(const std::string& from, const std::string& to)
{
    if (from.empty() || to.empty()) return false;
    return std::rename(from.c_str(), to.c_str()) == 0;
}

bool FileOrDirectoryExists(const std::string& path)
{
    if (path.empty()) return false;
    SE_STAT st;
    return SE_STAT_FN(path.c_str(), &st) == 0;
}

bool ListDirectory(const std::string& path, std::vector<std::string>& names)
{
    names.clear();
    if (path.empty()) return false;
#ifdef _WIN32
    WIN32_FIND_DATAA find;
    const HANDLE h = ::FindFirstFileA((path + kSep + "*").c_str(), &find);
    if (h == INVALID_HANDLE_VALUE) return false;
    // The name and the list can fail to allocate; the search handle must not be lost with them (A6-38).
    try
    {
        do
        {
            const std::string name = find.cFileName;
            if (name != "." && name != "..") names.push_back(name);
        } while (::FindNextFileA(h, &find) != 0);
    }
    catch (...)
    {
        ::FindClose(h);
        throw;
    }
    ::FindClose(h);
    return true;
#else
    DIR* d = ::opendir(path.c_str());
    if (d == nullptr) return false;
    // The name and the list can fail to allocate; the directory handle (32 KB with its buffer) must not be lost with
    // them (A6-38: a failed first request's staging cleanup, under sustained exhaustion).
    try
    {
        for (struct dirent* e = ::readdir(d); e != nullptr; e = ::readdir(d))
        {
            const std::string name = e->d_name;
            if (name != "." && name != "..") names.push_back(name);
        }
    }
    catch (...)
    {
        ::closedir(d);
        throw;
    }
    ::closedir(d);
    return true;
#endif
}

bool IsLink(const std::string& path)
{
    if (path.empty()) return false;
#ifdef _WIN32
    const DWORD attrs = ::GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    struct stat st;
    return ::lstat(path.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
#endif
}

bool RemoveLink(const std::string& path)
{
    if (!IsLink(path)) return !PathEntryExists(path);
#ifdef _WIN32
    // A directory symlink or junction is removed with RemoveDirectory, which deletes the reparse
    // point itself and leaves the target alone; a file symlink with DeleteFile.
    const DWORD attrs = ::GetFileAttributesA(path.c_str());
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) return ::RemoveDirectoryA(path.c_str()) != 0;
    return ::DeleteFileA(path.c_str()) != 0;
#else
    return ::unlink(path.c_str()) == 0;
#endif
}

namespace
{
std::atomic<bool> gClosing(false);
std::atomic<long long> gExitDeadlineMs(0);   // steady-clock milliseconds at which the exit's waiting ends, for every store together (A9-53)
std::atomic<void (*)(const std::string&)> gNoticeHook(nullptr);
long long NowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}
void FileLock::SetClosing(bool closing)
{
    if (closing) gExitDeadlineMs = NowMs() + kExitWaitMs;
    gClosing = closing;
}

void SetExitNoticeHook(void (*hook)(const std::string&)) { gNoticeHook = hook; }

void NoticeAtExit(const std::string& text)
{
    if (void (*hook)(const std::string&) = gNoticeHook)
    {
        hook(text);   // the SDL2 builds show a message box (A9-53)
        return;
    }
#ifdef _WIN32
    // The window is going: the Log panel goes with it, so a refused save is said in a box the user must close.
    ::MessageBoxA(nullptr, text.c_str(), "Saturn Explorer", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
#else
    std::fprintf(stderr, "Saturn Explorer: %s\n", text.c_str());
#endif
}

FileLock::FileLock(const std::string& path, bool wait, unsigned tryMs)
{
    mPath = path;
    // At exit the window is closing: a longer wait costs nothing and saves the change (A9-46). The 10 s is one budget shared by every
    // store, not 10 s each (A9-53): four stuck locks held the exit 40 s.
    unsigned budgetMs = wait ? mWaitMs : tryMs;   // wait=false with tryMs: a short try (A9-52), refused as "holds", not "held for"
    const bool retry = wait || tryMs > 0;
    if (gClosing && wait)
    {
        mWaitMs = kExitWaitMs;
        const long long left = gExitDeadlineMs - NowMs();
        budgetMs = left > 0 ? static_cast<unsigned>(std::min<long long>(left, kExitWaitMs)) : 0;
    }
    // A wait is bounded (A9-40): try without waiting, retry every few milliseconds until kWaitMs has passed. A holder that is
    // stuck (a hung instance on a stalled network folder, a debugger) no longer freezes the caller; the caller refuses its save.
    const auto start = std::chrono::steady_clock::now();
    auto expired = [&]() {
        return std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(budgetMs);
    };
#ifdef _WIN32
    // A null SECURITY_ATTRIBUTES makes the handle non-inheritable, the counterpart of O_CLOEXEC.
    const HANDLE h = ::CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { mError = static_cast<int>(::GetLastError()); return; }
    mHandle = reinterpret_cast<intptr_t>(h);
    OVERLAPPED whole = {};
    for (;;)
    {
        whole = OVERLAPPED();
        mHeld = ::LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &whole) != 0;
        if (mHeld) break;
        // Only a lock somebody else holds is waited for (A9-47); any other error (a share without byte-range locks) is the answer at once.
        const DWORD err = ::GetLastError();
        if (err != ERROR_LOCK_VIOLATION && err != ERROR_SHARING_VIOLATION) { mError = static_cast<int>(err); break; }
        if (!retry) break;
        if (expired()) { mTimedOut = wait; break; }
        ::Sleep(20);
    }
#else
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) { mError = errno; return; }
    mHandle = fd;
    for (;;)
    {
        const int rc = ::flock(fd, LOCK_EX | LOCK_NB);
        if (rc == 0) { mHeld = true; break; }
        if (errno == EINTR) continue;
        if (errno != EWOULDBLOCK) { mError = errno; break; }   // EWOULDBLOCK is "another process holds it" (mError 0, A9-47)
        if (!retry) break;
        if (expired()) { mTimedOut = wait; break; }
        struct timespec ts = { 0, 20 * 1000 * 1000 };
        ::nanosleep(&ts, nullptr);
    }
#endif
}

std::string FileLock::WhyNotHeld() const
{
    if (mHeld) return std::string();
    if (mTimedOut)
        return "another process has held " + mPath + " for more than " + std::to_string(mWaitMs / 1000) + " seconds";
    if (mError == 0) return "another process holds " + mPath;   // wait=false and someone has it
    return "cannot create or lock " + mPath + ": " + std::system_category().message(mError);
}

size_t RemoveStaleStagingFiles(const std::string& path)
{
#ifdef _WIN32
    (void)path;
    return 0;
#else
    const size_t slash = path.find_last_of('/');
    const std::string dir = slash == std::string::npos ? std::string(".") : (slash == 0 ? std::string("/") : path.substr(0, slash));
    const std::string prefix = (slash == std::string::npos ? path : path.substr(slash + 1)) + ".separt-";
    DIR* d = ::opendir(dir.c_str());
    if (!d) return 0;
    std::vector<std::string> doomed;
    while (const dirent* e = ::readdir(d))
    {
        const std::string name = e->d_name;
        if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0) continue;
        // "<pid>-<n>": digits, a dash, digits.
        const size_t dash = name.find('-', prefix.size());
        if (dash == std::string::npos || dash == prefix.size() || dash + 1 >= name.size()) continue;
        bool shape = true;
        for (size_t i = prefix.size(); i < name.size() && shape; ++i) shape = i == dash || (name[i] >= '0' && name[i] <= '9');
        if (!shape || name.size() - prefix.size() > 30) continue;
        const std::string full = (slash == std::string::npos ? std::string() : dir + (dir == "/" ? "" : "/")) + name;
        struct stat st;
        if (::lstat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        const long long pid = std::atoll(name.c_str() + prefix.size());
        const bool alive = pid > 0 && (::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM);
        if (alive && ::time(nullptr) - st.st_mtime < 600) continue;
        doomed.push_back(full);
    }
    ::closedir(d);
    size_t n = 0;
    for (const std::string& f : doomed) if (::unlink(f.c_str()) == 0) ++n;
    return n;
#endif
}

FileLock::~FileLock()
{
    if (mHandle == -1) return;
#ifdef _WIN32
    ::CloseHandle(reinterpret_cast<HANDLE>(mHandle));   // releases the lock
#else
    ::close(static_cast<int>(mHandle));
#endif
}

bool RemoveFlatDirectory(const std::string& path)
{
    if (path.empty()) return false;
    if (IsLink(path)) return false;
    if (!FileOrDirectoryExists(path)) return true;
    std::vector<std::string> names;
    if (!ListDirectory(path, names)) return false;
    // Checked before anything is deleted, so an unexpected entry leaves the directory whole
    // rather than half emptied.
    for (size_t i = 0; i < names.size(); ++i)
    {
        SE_STAT st;
        if (SE_STAT_FN((path + kSep + names[i]).c_str(), &st) != 0) return false;
        if (!S_ISREG(st.st_mode)) return false;
    }
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (!RemoveFile(path + kSep + names[i])) return false;
    }
    return RemoveEmptyDirectory(path);
}

char PathSeparator()
{
    return kSep;
}

bool PathEntryExists(const std::string& path)
{
    if (path.empty()) return false;
#ifdef _WIN32
    return ::GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return ::lstat(path.c_str(), &st) == 0;
#endif
}

// Staging must not be used for these; see WriteFileAtomically in the header.
bool ExistsAsNonRegularFile(const std::string& path)
{
    SE_STAT st;
    if (SE_STAT_FN(path.c_str(), &st) != 0) return false;   // missing, or unstattable
    return !S_ISREG(st.st_mode);
}

std::string CreateStagingFile(const std::string& path, std::string& error)
{
    std::string temp;
    FILE* f = OpenStagingFile(path, temp, error);
    if (f == nullptr) return std::string();
    errno = 0;
    const bool closed = std::fclose(f) == 0;
    if (!closed)
    {
        const int err = errno;
        RemoveFile(temp);   // before any text is made: the name must go even if the text cannot be built
        errno = err;
        error = Reason("could not create", temp);
        return std::string();
    }
    return temp;
}

std::string CanonicalPath(const std::string& path)
{
    if (path.empty()) return std::string();
#ifdef _WIN32
    // Resolve the final path of the object itself, so a junction or symlink is followed the way
    // the OS would follow it when the file is opened.
    const HANDLE h = ::CreateFileA(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE |
                                   FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::string();
    char buf[4096];
    const DWORD n = ::GetFinalPathNameByHandleA(h, buf, sizeof buf, 0);
    ::CloseHandle(h);
    if (n == 0 || n >= sizeof buf) return std::string();
    std::string out(buf, n);
    if (out.compare(0, 8, "\\\\?\\UNC\\") == 0) out = "\\\\" + out.substr(8);
    else if (out.compare(0, 4, "\\\\?\\") == 0) out = out.substr(4);
    return out;
#else
    char* r = ::realpath(path.c_str(), nullptr);
    if (r == nullptr) return std::string();
    std::string out(r);
    std::free(r);
    return out;
#endif
}

bool SameFile(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty()) return false;
#ifdef _WIN32
    auto ident = [](const std::string& p, BY_HANDLE_FILE_INFORMATION& info) {
        const HANDLE h = ::CreateFileA(p.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE |
                                       FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        const BOOL ok = ::GetFileInformationByHandle(h, &info);
        ::CloseHandle(h);
        return ok != 0;
    };
    BY_HANDLE_FILE_INFORMATION ia, ib;
    if (!ident(a, ia) || !ident(b, ib)) return false;
    return ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber &&
           ia.nFileIndexHigh == ib.nFileIndexHigh && ia.nFileIndexLow == ib.nFileIndexLow;
#else
    struct stat sa, sb;
    if (::stat(a.c_str(), &sa) != 0 || ::stat(b.c_str(), &sb) != 0) return false;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
#endif
}

bool PathIsWithin(const std::string& inner, const std::string& outer)
{
    std::string i = CanonicalPath(inner), o = CanonicalPath(outer);
    if (i.empty() || o.empty()) return false;
#ifdef _WIN32
    for (char& c : i) { if (c == '/') c = '\\'; if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); }
    for (char& c : o) { if (c == '/') c = '\\'; if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); }
#endif
    while (o.size() > 1 && o.back() == kSep) o.pop_back();
    if (i == o) return true;
    if (o.back() == kSep) return i.compare(0, o.size(), o) == 0;   // the filesystem root
    return i.size() > o.size() && i.compare(0, o.size(), o) == 0 && i[o.size()] == kSep;
}

}  // namespace sfe
