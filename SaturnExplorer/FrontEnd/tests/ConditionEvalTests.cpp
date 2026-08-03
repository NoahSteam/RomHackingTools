// Unit tests for the conditional-breakpoint / gated-tracepoint expression evaluator.
#include "Debug/ConditionEval.h"
#include "Debug/FormatString.h"

#include <cstdio>
#include <map>
#include <string>

using namespace sfe;

namespace
{
struct Mock : IFormatContext
{
    std::map<std::string, uint32_t> reg;
    std::map<uint32_t, uint8_t>     mem;
    bool GetValue(const std::string& n, uint32_t& o) const override
    {
        auto it = reg.find(n);
        if (it == reg.end()) return false;
        o = it->second; return true;
    }
    bool ReadMem(uint32_t a, uint32_t sz, uint32_t& o) const override
    {
        o = 0;
        for (uint32_t k = 0; k < sz; ++k)
        {
            auto it = mem.find(a + k);
            if (it == mem.end()) return false;
            o = (o << 8) | it->second;   // big-endian
        }
        return true;
    }
    bool ReadString(uint32_t, std::string&, size_t) const override { return false; }
};

int gFails = 0;
void chk(const Mock& m, const char* e, bool exp)
{
    const bool r = ConditionEval(e, m);
    if (r != exp) { std::printf("FAIL: '%s' got %d exp %d\n", e, r, exp); ++gFails; }
}
void chkValid(const char* e, bool wantErr)
{
    const bool has = !ConditionValidate(e).empty();
    if (has != wantErr) { std::printf("FAIL validate: '%s' err=%d exp=%d\n", e, has, wantErr); ++gFails; }
}
}  // namespace

int main()
{
    Mock m;
    m.reg = { {"r0",5}, {"r1",8}, {"r2",0x80}, {"r4",0}, {"pc",0x06001234} };
    m.mem = { {0x1000,0xDE}, {0x1001,0xAD}, {0x1002,0xBE}, {0x1003,0xEF} };

    chk(m, "r0 == 5", true);   chk(m, "r0 == 6", false);
    chk(m, "r4 != 0", false);  chk(m, "r0 != 0", true);
    chk(m, "r0 < 10 && r1 > 5", true);   chk(m, "r0 < 10 && r1 > 50", false);
    chk(m, "r0 == 5 || r0 == 9", true);
    chk(m, "r2 & 0x80", true);  chk(m, "r2 & 0x40", false);
    chk(m, "r2 & 0x80 == 0x80", true);    // comparison below bitwise (debugger DWIM)
    chk(m, "(r0 + r1) == 13", true);
    chk(m, "pc == 0x06001234", true);
    chk(m, "!r4", true);  chk(m, "!r0", false);
    chk(m, "~r0 == 0xFFFFFFFA", true);
    chk(m, "*0x1000 == 0xDEADBEEF", true);          // long, big-endian
    chk(m, "*0x1000:b == 0xDE", true);              // byte = high byte at addr
    chk(m, "*0x1000:w == 0xDEAD", true);
    chk(m, "r0 << 2 == 20", true);   chk(m, "r1 >> 1 == 4", true);
    chk(m, "", true);                               // empty guard = always true

    chkValid("r0 == 5", false);   chkValid("r0 ==", true);
    chkValid("*0x1000:z", true);  chkValid("(r0+1", true);
    chkValid("r0 & 3 == 3", false);

    std::printf(gFails ? "ConditionEval: %d FAILED\n" : "ConditionEval: all passed\n", gFails);
    return gFails ? 1 : 0;
}
