// ResolveFunctionSymbol: the lookup App hands to the assembler for branch targets and absolute
// PC-relative operands. Known name, the default sub_ spelling, a duplicate, an unknown name.
#include "Debug/FunctionSymbol.h"

#include <cstdio>

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}
}  // namespace

int main()
{
    std::map<uint32_t, std::string> names;
    names[0x06004000u] = "Main";
    names[0x06005000u] = "Twin";
    names[0x06006000u] = "Twin";
    uint32_t a = 0;
    std::string err;

    Check(ResolveFunctionSymbol(names, "Main", a, err) && a == 0x06004000u, "known name resolves");

    err.clear();
    Check(!ResolveFunctionSymbol(names, "Twin", a, err), "a duplicate name is refused");
    Check(err.find("06005000") != std::string::npos && err.find("06006000") != std::string::npos,
          "and the error lists both addresses");

    err.clear();
    Check(!ResolveFunctionSymbol(names, "Nope", a, err) && err.find("Nope") != std::string::npos,
          "an unknown name is refused and named in the message");
    Check(!ResolveFunctionSymbol(names, "main", a, err), "names are case-sensitive");

    // The default spelling NameOf prints: low 24 bits only.
    Check(ResolveFunctionSymbol(names, "sub_00ABCD", a, err) && a == 0x0600ABCDu, "sub_ in high work RAM");
    Check(ResolveFunctionSymbol(names, "SUB_00abcd", a, err) && a == 0x0600ABCDu, "sub_ is case-insensitive");
    Check(ResolveFunctionSymbol(names, "sub_200010", a, err) && a == 0x00200010u, "sub_ in low work RAM");
    Check(ResolveFunctionSymbol(names, "sub_0600ABCD", a, err) && a == 0x0600ABCDu, "eight digits are the full address");
    err.clear();
    Check(!ResolveFunctionSymbol(names, "sub_400000", a, err) && !err.empty(), "an ambiguous 24-bit value is refused");
    Check(!ResolveFunctionSymbol(names, "sub_12", a, err), "wrong digit count is unknown");
    Check(!ResolveFunctionSymbol(names, "sub_00ABCG", a, err), "non-hex digits are unknown");

    // A user name wins over the default spelling.
    names[0x06000100u] = "sub_00ABCD";
    Check(ResolveFunctionSymbol(names, "sub_00ABCD", a, err) && a == 0x06000100u, "a stored name wins over sub_");

    if (gFail == 0) std::printf("FunctionSymbolTests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
