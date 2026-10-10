// The PRS search's bounds, and that they did not cost it any real matches (ROM-04).
//
// The search attempts a decompression at every byte offset in the file, so its cost is quadratic
// in the worst case and a highly compressible stream makes that worst case cheap to reach. Two
// bounds now hold it: a candidate filter that rejects an offset whose first control bit cannot
// begin a stream that emits anything, and a per-file budget on total decompressed output.
//
// The filter is the part that could quietly break the feature -- a filter that is not sound drops
// real matches and nothing says so -- which is why the first test compresses data with this
// codebase's own PRS encoder and requires the search to find it.
#include "DataSearch.h"
#include "Prs.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) ::_mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) ::mkdir(p, 0777)
#endif

using namespace sfe;

namespace {

int gFail = 0;
void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; } }

void WriteBytes(const std::string& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

std::vector<uint8_t> Compress(const std::vector<uint8_t>& plain)
{
    PuyoPrsCompressor c;
    c.CompressData(plain.data(), static_cast<unsigned long>(plain.size()));
    const std::vector<uint8>& out = c.GetCompressedData();
    return std::vector<uint8_t>(out.begin(), out.end());
}

}  // namespace

int main()
{
    const std::string dir = "prssearch_test_tmp";
    MKDIR(dir.c_str());

    // The payload to hide: something with enough structure to compress, and a distinctive run in
    // the middle to search for.
    std::vector<uint8_t> plain;
    for (int i = 0; i < 4096; ++i) plain.push_back(static_cast<uint8_t>(i * 7));
    const uint8_t needle[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04 };
    std::memcpy(plain.data() + 1000, needle, sizeof(needle));

    const std::vector<uint8_t> compressed = Compress(plain);
    Check(!compressed.empty(), "the encoder produced a stream");

    // The candidate filter's premise, stated as a test: a real stream's first control bit selects
    // a literal, because a copy at output position 0 has nothing behind it to copy from. If the
    // encoder ever changed so that a stream could begin with a clear bit, the filter would start
    // dropping real matches -- so assert it here rather than leaving it as a comment.
    Check(!compressed.empty() && (compressed[0] & 1u) != 0,
          "a real PRS stream's first control bit selects a literal");

    // And the stream round-trips, so a failure below is the search's and not the codec's.
    {
        PRSDecompressor dec;
        Check(dec.UncompressData(compressed.data(), static_cast<unsigned int>(compressed.size())),
              "the stream decompresses");
        Check(dec.mUncompressedDataSize == plain.size() &&
              std::memcmp(dec.mpUncompressedData, plain.data(), plain.size()) == 0,
              "and round-trips to the original bytes");
    }

    // A reused decoder keeps the buffer an earlier call grew. Each call's own cap still has to
    // hold: the retained capacity used to be checked first, so a later, smaller cap was ignored.
    {
        const std::vector<uint8_t> as(1024, 'A');
        const std::vector<uint8_t> z = Compress(as);
        const std::vector<uint8_t> cut(z.begin(), z.end() - 2);   // no end-of-stream marker
        PRSDecompressor dec;
        Check(dec.UncompressData(z.data(), unsigned(z.size()), 1024) && dec.mUncompressedDataSize == 1024,
              "1024 A bytes decode under a 1024-byte cap");
        Check(!dec.UncompressData(z.data(), unsigned(z.size()), 1) && dec.mLastOutputBytes <= 1,
              "a reused decoder refuses the same stream under a 1-byte cap");
        Check(!dec.UncompressData(z.data(), unsigned(z.size()), 100) && dec.mLastOutputBytes <= 100,
              "and writes no more than a 100-byte cap");
        Check(!dec.UncompressData(cut.data(), unsigned(cut.size()), 1) && dec.mLastOutputBytes <= 1,
              "a truncated stream stays within the cap too");
        PRSDecompressor fresh;
        Check(!fresh.UncompressData(z.data(), unsigned(z.size()), 1), "a fresh decoder refuses it as well");
    }

    // --- The search finds the needle inside the compressed block, at the block's offset. ---
    {
        std::vector<uint8_t> file;
        for (int i = 0; i < 300; ++i) file.push_back(static_cast<uint8_t>(i));   // leading junk
        const uint64_t blockAt = file.size();
        file.insert(file.end(), compressed.begin(), compressed.end());
        for (int i = 0; i < 300; ++i) file.push_back(0x5A);                      // trailing junk
        WriteBytes(dir + "/BLOCK.PRS", file);

        std::vector<DataSearchHit> hits;
        SearchProgress prog;
        const size_t scanned = SearchData({dir + "/BLOCK.PRS"}, needle, sizeof(needle),
                                          SearchCompression::Prs, hits, 256, &prog);
        Check(scanned == 1, "one file scanned");
        Check(hits.size() == 1, "the file is reported as a hit");
        bool foundBlock = false;
        if (!hits.empty())
            for (uint64_t off : hits[0].offsets) if (off == blockAt) foundBlock = true;
        Check(foundBlock, "the compressed block's own offset is among the hits");
        Check(prog.filesBudgetExhausted.load() == 0, "a small file does not exhaust the budget");
        Check(prog.filesSkipped.load() == 0, "and is not skipped");
    }

    // --- A needle that is not there is not found. ---
    {
        const uint8_t absent[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
        std::vector<DataSearchHit> hits;
        SearchProgress prog;
        SearchData({dir + "/BLOCK.PRS"}, absent, sizeof(absent), SearchCompression::Prs, hits,
                   256, &prog);
        Check(hits.empty(), "a needle that is not in the block is not reported");
    }

    // --- The budget stops a file and says so, rather than reporting a partial scan as complete. ---
    //
    // Highly compressible input is what makes the pathological case cheap to construct: a run of
    // identical bytes compresses to long copies, and a long copy emits up to 256 bytes for about
    // three input bytes. Every offset inside such a stream decodes into a great deal of output.
    {
        std::vector<uint8_t> zeros(48 * 1024, 0);
        std::vector<uint8_t> file = Compress(zeros);
        WriteBytes(dir + "/BIG.PRS", file);

        const uint8_t absent[4] = { 0xAB, 0xCD, 0xEF, 0x99 };

        // With the real budget this file finishes, so the counter must stay clear.
        {
            std::vector<DataSearchHit> hits;
            SearchProgress prog;
            SearchData({dir + "/BIG.PRS"}, absent, sizeof(absent), SearchCompression::Prs, hits,
                       256, &prog);
            Check(prog.filesBudgetExhausted.load() == 0,
                  "the file completes within the default budget");
        }

        // With a budget too small for it, the scan stops early and reports that it did.
        {
            std::vector<DataSearchHit> hits;
            SearchProgress prog;
            prog.prsOutputBudget.store(64 * 1024);
            SearchData({dir + "/BIG.PRS"}, absent, sizeof(absent), SearchCompression::Prs, hits,
                       256, &prog);
            Check(prog.filesBudgetExhausted.load() == 1,
                  "a budget smaller than the work needed is reported as exhausted");
        }
    }

#ifndef _WIN32
    std::string rm = "rm -rf '" + dir + "'";
    (void)!std::system(rm.c_str());
#endif

    if (gFail == 0) std::printf("All PrsSearch tests passed.\n");
    return gFail ? 1 : 0;
}
