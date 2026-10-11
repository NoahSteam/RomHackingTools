#include "Decompiler/SpecBundle.h"

#include "Decompiler/Sh2SpecData.h"
#include "FileWrite.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <process.h>   // _getpid
#else
#include <unistd.h>    // getpid
#endif

namespace sfe
{
namespace decomp
{
namespace
{

// ---- SHA-256 (FIPS 180-4) ------------------------------------------------------------------

const uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

class Sha256
{
public:
    void Update(const void* data, size_t size)
    {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        mLength += size;
        while (size > 0)
        {
            const size_t take = (size < 64 - mUsed) ? size : 64 - mUsed;
            std::memcpy(mBlock + mUsed, p, take);
            mUsed += take;
            p += take;
            size -= take;
            if (mUsed == 64)
            {
                Compress();
                mUsed = 0;
            }
        }
    }

    std::string HexDigest()
    {
        const uint64_t bits = static_cast<uint64_t>(mLength) * 8u;
        const unsigned char pad = 0x80;
        Update(&pad, 1);
        const unsigned char zero = 0;
        while (mUsed != 56) Update(&zero, 1);
        unsigned char len[8];
        for (int i = 0; i < 8; ++i) len[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
        Update(len, 8);
        static const char kHex[] = "0123456789abcdef";
        std::string out;
        for (uint32_t h : mState)
            for (int shift = 28; shift >= 0; shift -= 4) out += kHex[(h >> shift) & 0xF];
        return out;
    }

private:
    static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void Compress()
    {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(mBlock[4 * i]) << 24) | (uint32_t(mBlock[4 * i + 1]) << 16) |
                   (uint32_t(mBlock[4 * i + 2]) << 8) | uint32_t(mBlock[4 * i + 3]);
        for (int i = 16; i < 64; ++i)
        {
            const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = mState[0], b = mState[1], c = mState[2], d = mState[3];
        uint32_t e = mState[4], f = mState[5], g = mState[6], h = mState[7];
        for (int i = 0; i < 64; ++i)
        {
            const uint32_t t1 = h + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
            const uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        mState[0] += a; mState[1] += b; mState[2] += c; mState[3] += d;
        mState[4] += e; mState[5] += f; mState[6] += g; mState[7] += h;
    }

    uint32_t mState[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    unsigned char mBlock[64] = {};
    size_t mUsed = 0;
    size_t mLength = 0;
};

// ---- Files ---------------------------------------------------------------------------------

std::string Join(const std::string& dir, const std::string& name)
{
    if (dir.empty()) return name;
    const char last = dir[dir.size() - 1];
    if (last == '/' || last == '\\') return dir + name;
    return dir + PathSeparator() + name;
}

bool ReadWholeFile(const std::string& path, std::vector<unsigned char>& out)
{
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char buf[16384];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    const bool ok = !std::ferror(f);
    std::fclose(f);
    return ok;
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

// Empty when 'dir' holds exactly what the manifest promises, else what is wrong with it.
std::string Verify(const std::string& dir, const std::string& manifest,
                   const std::vector<SpecFileData>& files)
{
    std::vector<unsigned char> bytes;
    if (!ReadWholeFile(Join(dir, kManifestName), bytes)) return "manifest.json is missing";
    if (std::string(bytes.begin(), bytes.end()) != manifest) return "manifest.json does not match this build";
    for (const SpecFileData& f : files)
    {
        if (!ReadWholeFile(Join(dir, f.name), bytes)) return f.name + " is missing";
        if (bytes.size() != f.size) return f.name + " has the wrong size";
        if (Sha256Hex(bytes.data(), bytes.size()) != Sha256Hex(f.data, f.size))
            return f.name + " does not match its hash";
    }
    return std::string();
}

// Delete whatever sits at 'path': the flat directory this module writes, or a stray file.
bool RemoveEntry(const std::string& path)
{
    if (!PathEntryExists(path)) return true;
    if (ExistsAsNonRegularFile(path)) return RemoveFlatDirectory(path);
    return RemoveFile(path);
}

unsigned long ProcessId()
{
#ifdef _WIN32
    return static_cast<unsigned long>(_getpid());
#else
    return static_cast<unsigned long>(::getpid());
#endif
}

// Write every file into a fresh '<dir>.tmp-<pid>', check each by reading it back, then move
// the whole directory to 'dir'. Empty on success, else the reason.
std::string Extract(const std::string& dir, const std::string& manifest,
                    const std::vector<SpecFileData>& files)
{
    const std::string tmp = dir + ".tmp-" + std::to_string(ProcessId());
    if (!RemoveEntry(tmp)) return "cannot clear the staging directory " + tmp;
    if (!MakeDirectory(tmp)) return "cannot create " + tmp;

    std::string error;
    std::vector<unsigned char> back;
    for (const SpecFileData& f : files)
    {
        const std::string path = Join(tmp, f.name);
        if (!WriteFileAtomically(path, f.data, f.size, error))
        {
            RemoveEntry(tmp);
            return "cannot write " + path + ": " + error;
        }
        if (!ReadWholeFile(path, back) || back.size() != f.size ||
            Sha256Hex(back.data(), back.size()) != Sha256Hex(f.data, f.size))
        {
            RemoveEntry(tmp);
            return path + " did not read back as written";
        }
    }
    if (!WriteFileAtomically(Join(tmp, kManifestName), manifest.data(), manifest.size(), error))
    {
        RemoveEntry(tmp);
        return "cannot write the manifest in " + tmp + ": " + error;
    }

    if (!MovePath(tmp, dir))
    {
        // Another instance published the same bundle between our check and our rename. Its
        // copy is as good as ours if it verifies, which the caller checks next.
        RemoveEntry(tmp);
        if (!PathEntryExists(dir)) return "cannot move " + tmp + " to " + dir;
    }
    return std::string();
}

}  // namespace

std::string Sha256Hex(const void* data, size_t size)
{
    Sha256 h;
    h.Update(data, size);
    return h.HexDigest();
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

std::vector<SpecFileData> Sh2SpecFiles()
{
    std::vector<SpecFileData> files;
    for (size_t i = 0; i < kSh2SpecFileCount; ++i)
        files.push_back(SpecFileData{ kSh2SpecFiles[i].name, kSh2SpecFiles[i].data, kSh2SpecFiles[i].size });
    return files;
}

SpecBundleResult MaterialiseSpecBundle(const std::string& configDir, const std::vector<SpecFileData>& files)
{
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

    // A valid directory is used as it is, even in a config dir that has since become
    // read-only: reading it is all the engine needs.
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
        if (!RemoveEntry(dir))
        {
            r.error = "Decompiler unavailable: " + dir + " is damaged (" + problem + ") and cannot be removed";
            return r;
        }
    }

    if (!MakeDirectory(configDir) || !MakeDirectory(root))
    {
        r.error = "Decompiler unavailable: cannot create " + root;
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

SpecBundleResult MaterialiseSh2SpecBundle(const std::string& configDir)
{
    return MaterialiseSpecBundle(configDir, Sh2SpecFiles());
}

}  // namespace decomp
}  // namespace sfe
