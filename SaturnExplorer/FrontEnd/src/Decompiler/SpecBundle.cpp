#include "Decompiler/SpecBundle.h"

#include "Decompiler/Sh2SpecData.h"
#include "FileRead.h"
#include "FileWrite.h"
#include "Sha256.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace sfe
{
namespace decomp
{
namespace
{

// ---- Files ---------------------------------------------------------------------------------

std::string Join(const std::string& dir, const std::string& name)
{
    if (dir.empty()) return name;
    const char last = dir[dir.size() - 1];
    if (last == '/' || last == '\\') return dir + name;
    return dir + PathSeparator() + name;
}

// The manifest is a pure function of the embedded files, so verifying it is a byte compare
// with what this build would write -- no JSON parser, and a manifest from any other build
// (or a hand-edited one) can never pass.
std::string ManifestText(const std::string& hash, const std::vector<SpecFileData>& files)
{
    std::string m = "{\n  \"bundle\": \"" + hash + "\",\n  \"files\": [\n";
    for (size_t i = 0; i < files.size(); ++i)
    {
        m += "    { \"name\": \"" + files[i].name + "\", \"size\": " + std::to_string(files[i].size) +
             ", \"sha256\": \"" + Sha256Hex(files[i].data, files[i].size) + "\" }";
        m += (i + 1 < files.size()) ? ",\n" : "\n";
    }
    m += "  ]\n}\n";
    return m;
}

const char kManifestName[] = "manifest.json";

std::atomic<void (*)(const std::string&)> gStagingHook(nullptr);

// Empty when 'dir' holds exactly what the manifest promises, else what is wrong with it. The
// expected bytes are in memory, so each file is compared with them directly; the manifest's
// hashes are what makes a directory from another build fail the manifest compare first.
std::string Verify(const std::string& dir, const std::string& manifest,
                   const std::vector<SpecFileData>& files)
{
    // Each file is read no further than one byte past what it should hold, and only if it is a regular file: a FIFO put
    // where a language file goes blocked the start-up for good, and a link to /dev/zero never ended (A9-5).
    std::string bytes;
    auto read = [&](const std::string& path, size_t expected, std::string& why) {
        switch (ReadFileCapped(path, expected + 1, bytes))
        {
        case CappedRead::Ok: return true;
        case CappedRead::TooLarge: why = "has the wrong size"; return false;
        case CappedRead::NotRegular: why = "is not a regular file"; return false;
        default: why = "is missing"; return false;
        }
    };
    std::string why;
    if (!read(Join(dir, kManifestName), manifest.size(), why)) return std::string(kManifestName) + (why == "is missing" ? " is missing" : " " + why);
    if (bytes != manifest) return "manifest.json does not match this build";
    for (const SpecFileData& f : files)
    {
        if (!read(Join(dir, f.name), f.size, why)) return f.name + " " + why;
        if (bytes.size() != f.size) return f.name + " has the wrong size";
        if (std::memcmp(bytes.data(), f.data, f.size) != 0) return f.name + " does not match its contents";
    }
    return std::string();
}

// Delete whatever sits at 'path': the flat directory this module writes, a stray file, or a
// link -- which is removed itself, never followed, so a planted symlink cannot point the
// repair at somebody else's directory.
bool RemoveEntry(const std::string& path)
{
    if (!PathEntryExists(path)) return true;
    if (IsLink(path)) return RemoveLink(path);
    if (ExistsAsNonRegularFile(path)) return RemoveFlatDirectory(path);
    return RemoveFile(path);
}

// Write every file into a fresh '<dir>.tmp-<pid>', verify the staged copy, then move the whole
// directory to 'dir', so nothing unverified is ever published under the real name. Empty on
// success, else the reason. The caller verifies the published directory again: that is the
// copy the engine will read.
std::string Extract(const std::string& dir, const std::string& manifest,
                    const std::vector<SpecFileData>& files)
{
    const std::string tmp = dir + ".tmp-" + std::to_string(ProcessId());
    if (!RemoveEntry(tmp)) return "cannot clear the staging directory " + tmp;
    // Every path this function will create inside the staging directory, made BEFORE the directory exists: a failure
    // making them leaves nothing staged, and the guard below can then remove what was staged with unlink and rmdir
    // alone, which allocate nothing. Its removal used to be RemoveEntry (a directory listing, path strings), and with
    // an allocation failure that stays (sustained exhaustion) none could be made: the staging directory stayed on disk
    // for good, up to 19 KB, because no later process removes another pid's (A6-37).
    std::vector<std::string> staged;
    staged.reserve(files.size() + 1);
    for (const SpecFileData& f : files) staged.push_back(Join(tmp, f.name));
    staged.push_back(Join(tmp, kManifestName));
    if (!MakeDirectory(tmp)) return "cannot create " + tmp;
    // Whichever way this function is left, the staging directory goes with it unless it was moved into place: an
    // exception (a failed allocation in the staged Verify's read, a path in an error return) used to leave it for good
    // (A6-33). The removal is best effort and must not throw out of a destructor that may be running during unwinding.
    struct StagingGuard
    {
        const std::string& dir;
        const std::vector<std::string>& staged;
        bool armed;
        ~StagingGuard()
        {
            if (!armed) return;
            // The names are known, so nothing here allocates: unlink each and remove the directory.
            for (const std::string& path : staged) RemoveFile(path);
            if (RemoveEmptyDirectory(dir)) return;
            // Something not on the list is in it (the '.separt-' temporary of a write an exception interrupted):
            // a listing finds it, where the heap allows one.
            try
            {
                RemoveEntry(dir);
            }
            catch (...)
            {
            }
        }
    } guard{ tmp, staged, true };

    // A failure below returns and the guard removes the staging directory: nothing on a failure path allocates before it
    // does (A6-43; the removal used to be RemoveEntry here, which lists the directory).
    std::string error;
    for (size_t i = 0; i < files.size(); ++i)
    {
        const std::string& path = staged[i];
        if (!WriteFileAtomically(path, files[i].data, files[i].size, error))
        {
            return "cannot write " + path + ": " + error;
        }
    }
    if (!WriteFileAtomically(staged.back(), manifest.data(), manifest.size(), error))
    {
        return "cannot write the manifest in " + tmp + ": " + error;
    }
    if (void (*hook)(const std::string&) = gStagingHook.load()) hook(tmp);
    const std::string problem = Verify(tmp, manifest, files);
    if (!problem.empty())
    {
        return tmp + " did not verify after writing (" + problem + ")";
    }
    if (!MovePath(tmp, dir))
    {
        return "cannot move " + tmp + " to " + dir;
    }
    guard.armed = false;   // published: the directory is no longer the staging one
    return std::string();
}

}  // namespace

void SetSpecBundleStagingHookForTests(void (*hook)(const std::string& stagingDir))
{
    gStagingHook.store(hook);
}

std::string SpecBundleHash(const std::vector<SpecFileData>& files)
{
    Sha256 h;
    for (const SpecFileData& f : files)
    {
        const std::string header = f.name + '\0' + std::to_string(f.size) + '\0';
        h.Update(header.data(), header.size());
        h.Update(f.data, f.size);
    }
    return h.HexDigest().substr(0, 16);
}

const std::vector<SpecFileData>& Sh2SpecFiles()
{
    static const std::vector<SpecFileData> files = [] {
        std::vector<SpecFileData> v;
        for (size_t i = 0; i < kSh2SpecFileCount; ++i)
            v.push_back(SpecFileData{ kSh2SpecFiles[i].name, kSh2SpecFiles[i].data, kSh2SpecFiles[i].size });
        return v;
    }();
    return files;
}

SpecBundleResult MaterialiseSh2SpecBundle(const std::string& configDir)
{
    const std::vector<SpecFileData>& files = Sh2SpecFiles();
    SpecBundleResult r;
    if (configDir.empty())
    {
        r.error = "Decompiler unavailable: no configuration directory";
        return r;
    }
    const std::string root = Join(configDir, "decompiler");
    const std::string hash = SpecBundleHash(files);
    const std::string dir = Join(root, hash);
    const std::string manifest = ManifestText(hash, files);

    const bool haveRoot = MakeDirectory(configDir) && MakeDirectory(root);
    // Verification, repair and publication are serialised across instances: two repairing the
    // same damaged directory would otherwise race, the slower one acting on its stale verdict
    // and deleting the copy the faster one has just verified and handed to its engine.
    FileLock lock(Join(root, hash + ".lock"));

    // Verified under the lock, so the verdict cannot be overtaken by another instance's repair.
    // Without the lock (a config dir that cannot be written) a valid directory is still used --
    // reading it is all the engine needs -- but a damaged one cannot be repaired.
    std::string problem = "not extracted yet";
    if (PathEntryExists(dir))
    {
        problem = Verify(dir, manifest, files);
        if (problem.empty())
        {
            r.ok = true;
            r.dir = dir;
            return r;
        }
        r.repaired = true;
    }
    if (!haveRoot)
    {
        r.error = "Decompiler unavailable: cannot create " + root;
        return r;
    }
    if (!lock.Held())
    {
        r.error = "Decompiler unavailable: " + dir + " cannot be " +
                  (r.repaired ? "repaired (" + problem + ")" : "created") +
                  ": the lock file in " + root + " cannot be opened";
        return r;
    }
    if (r.repaired && !RemoveEntry(dir))
    {
        r.error = "Decompiler unavailable: " + dir + " is damaged (" + problem + ") and cannot be removed";
        return r;
    }
    const std::string failure = Extract(dir, manifest, files);
    if (!failure.empty())
    {
        r.error = "Decompiler unavailable: " + failure;
        return r;
    }
    r.extracted = true;

    // Extract once, then trust nothing: what is on disk now is what the engine will read.
    problem = Verify(dir, manifest, files);
    if (!problem.empty())
    {
        r.error = "Decompiler unavailable: " + dir + " failed verification after extraction (" + problem + ")";
        return r;
    }
    r.ok = true;
    r.dir = dir;
    return r;
}

}  // namespace decomp
}  // namespace sfe
