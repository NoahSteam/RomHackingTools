// ---- round 6 reviewer's driver (scratch, not committed) ----
namespace {
const char* KindName(TypeKind k)
{
    switch (k) { case TypeKind::Int: return "int"; case TypeKind::Uint: return "uint"; case TypeKind::Bool: return "bool";
    case TypeKind::Pointer: return "ptr"; case TypeKind::Void: return "void"; default: return "unk"; }
}
std::string Shape(const TypeShape& s)
{
    char b[64];
    std::snprintf(b, sizeof b, "%s%u", KindName(s.kind), unsigned(s.size));
    return b;
}
void PrintProto(const DecompileResult& r)
{
    const Prototype& p = r.prototype;
    std::printf("  PROTO known=%d ret=%s params=%zu (", int(p.known), Shape(p.ret).c_str(), p.params.size());
    for (auto& s : p.params) std::printf("%s ", Shape(s).c_str());
    std::printf(") preserved=%08X spDelta=%d stores=%08X out=%08X leaks=%08X frame=%08X withheld='%s'\n", p.effects.preserved,
                p.effects.spDelta, p.effects.storesThrough, p.effects.outOrigins, p.effects.leaks, p.effects.frameValues,
                r.prototypeWithheld.c_str());
}
struct Spec
{
    std::string name;
    std::vector<std::pair<uint32_t, std::vector<uint16_t>>> code;
    std::vector<std::pair<uint32_t, uint32_t>> longs;
    std::vector<uint32_t> lend;
    std::vector<uint32_t> lendUnknown;
    uint32_t entry = 0x06009000u;
    std::vector<std::vector<uint32_t>> args;
    std::vector<std::map<std::string, uint32_t>> argRegs;
    std::map<std::string, uint32_t> regs;
    bool noResult = false;
    bool quiet = false;
};
std::shared_ptr<MemorySnapshot> SpecSnapshot(const Spec& s, uint64_t serial)
{
    std::vector<uint8_t> hw;
    for (auto& c : s.code)
    {
        size_t off = c.first - 0x06000000u;
        for (uint16_t w : c.second) { dts::Put16(hw, off, w); off += 2; }
    }
    for (auto& l : s.longs) dts::Put32(hw, l.first - 0x06000000u, l.second);
    for (uint32_t vec = 0; vec < 0x100; ++vec) dts::Put32(hw, kRunVbr - 0x06000000u + vec * 4, kRunHandler);
    dts::Put16(hw, kRunHandler - 0x06000000u, 0x002B);
    dts::Put16(hw, kRunHandler - 0x06000000u + 2, 0x0009);
    dts::Put16(hw, kRunSentinel - 0x06000000u, 0x0009);
    return MakeSnapshot(hw, serial);
}
void RunSpec(Sh2Decompiler& d, const Spec& s, uint64_t serial)
{
    std::printf("==== %s\n", s.name.c_str());
    auto snap = SpecSnapshot(s, serial);
    std::vector<std::pair<uint32_t, Prototype>> protos;
    for (uint32_t a : s.lendUnknown)
    {
        Prototype p;
        p.known = false;
        protos.emplace_back(a, p);
    }
    for (uint32_t a : s.lend)
    {
        DecompileRequest q = Request(a);
        q.prototypes = protos;
        const DecompileResult r = d.Decompile(snap, q);
        char lbl[64];
        std::snprintf(lbl, sizeof lbl, "  callee %08X", a);
        if (!s.quiet) Print(lbl, r);
        else std::printf("%s\n", lbl);
        PrintProto(r);
        protos.emplace_back(a, r.prototype);
    }
    DecompileRequest q = Request(s.entry);
    q.prototypes = protos;
    const DecompileResult r = d.Decompile(snap, q);
    Print("  ENTRY", r);
    PrintProto(r);
    for (const auto& args : s.args)
    {
        std::map<std::string, uint32_t> regs = RunEntryRegisters(args);
        for (auto& kv : s.regs) regs[kv.first] = kv.second;
        for (auto& kv : s.argRegs[&args - &s.args[0]]) regs[kv.first] = kv.second;
        uint32_t r0 = 0, r1 = 0;
        bool ok = false;
        try
        {
            RunMachine m(snap);
            for (const auto& kv : regs) m.Set(kv.first.c_str(), kv.second);
            ok = m.Run(s.entry, kRunSentinel, 100000);
            r0 = m.Get("r0");
            r1 = m.Get("r1");
        }
        catch (ghidra::LowlevelError& e)
        {
            std::printf("  RUN error %s\n", e.explain.c_str());
        }
        std::string why;
        PseudoEval::Entry entry;
        entry.regs = regs;
        entry.snap = snap;
        entry.sp = regs["r15"];
        const bool agree = ok && MayReturn(r.Text(), args, r0, why, !s.noResult, &entry);
        std::printf("  RUN %s args=", [&]{ static std::string t; t.clear(); for (auto& kv : s.argRegs[&args - &s.args[0]]) t += kv.first + "=" + std::to_string(kv.second) + " "; return t.c_str(); }());
        for (uint32_t a : args) std::printf("%08X ", a);
        std::printf("-> %s r0=%08X r1=%08X ORACLE %s %s\n", ok ? "ret" : "NORET", r0, r1, agree ? "agrees" : "REJECTS", why.c_str());
    }
}
std::vector<uint16_t> Words(std::istringstream& in)
{
    std::vector<uint16_t> w;
    std::string t;
    while (in >> t)
    {
        if (t[0] == '#') break;
        size_t star = t.find('*');   // "N*XXXX" repeats
        if (star != std::string::npos)
        {
            int n = std::atoi(t.substr(0, star).c_str());
            uint16_t v = uint16_t(std::strtoul(t.substr(star + 1).c_str(), nullptr, 16));
            for (int i = 0; i < n; ++i) w.push_back(v);
        }
        else w.push_back(uint16_t(std::strtoul(t.c_str(), nullptr, 16)));
    }
    return w;
}
}   // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: drv <scratch> <spec>...\n");
        return 2;
    }
    dts::PrepareProcess();
    const std::string scratch = argv[1];
    dts::RemoveTree(scratch);
    sfe::MakeDirectory(scratch);
    gConfig = dts::Join(scratch, "config");
    sfe::MakeDirectory(gConfig);
    Sh2Decompiler d(gConfig);
    if (std::string(argv[2]) == "--dis" && argc >= 6)
    {
        std::vector<uint8_t> hw(0x100000u);
        FILE* f = std::fopen(argv[3], "rb");
        if (!f) return 1;
        const size_t got = std::fread(hw.data(), 1, hw.size(), f);
        std::fclose(f);
        (void)got;
        auto snap = MakeSnapshot(hw);
        for (const std::string& l : d.Disassemble(snap, std::strtoul(argv[4], nullptr, 16), std::atoi(argv[5]))) std::printf("    %s\n", l.c_str());
        return 0;
    }
    uint64_t serial = 1000;
    const char* only = std::getenv("ONLY");
    for (int ai = 2; ai < argc; ++ai)
    {
        std::ifstream f(argv[ai]);
        std::string line;
        Spec s;
        bool open = false;
        while (std::getline(f, line))
        {
            std::istringstream in(line);
            std::string k;
            if (!(in >> k) || k[0] == '#') continue;
            if (k == "name")
            {
                s = Spec();
                open = true;
                std::getline(in, s.name);
            }
            else if (k == "code")
            {
                std::string a;
                in >> a;
                const uint32_t addr = std::strtoul(a.c_str(), nullptr, 16);
                s.code.emplace_back(addr, Words(in));
            }
            else if (k == "long")
            {
                std::string a, v;
                in >> a >> v;
                s.longs.emplace_back(std::strtoul(a.c_str(), nullptr, 16), std::strtoul(v.c_str(), nullptr, 16));
            }
            else if (k == "lend")
            {
                std::string a;
                while (in >> a) s.lend.push_back(std::strtoul(a.c_str(), nullptr, 16));
            }
            else if (k == "unknown")
            {
                std::string a;
                while (in >> a) s.lendUnknown.push_back(std::strtoul(a.c_str(), nullptr, 16));
            }
            else if (k == "entry")
            {
                std::string a;
                in >> a;
                s.entry = std::strtoul(a.c_str(), nullptr, 16);
            }
            else if (k == "args")
            {
                std::vector<uint32_t> v;
                std::map<std::string, uint32_t> ar;
                std::string a;
                while (in >> a)
                {
                    size_t eq = a.find('=');
                    if (eq != std::string::npos) ar[a.substr(0, eq)] = std::strtoul(a.c_str() + eq + 1, nullptr, 16);
                    else v.push_back(std::strtoul(a.c_str(), nullptr, 16));
                }
                s.args.push_back(v);
                s.argRegs.push_back(ar);
            }
            else if (k == "reg")
            {
                std::string n, v;
                in >> n >> v;
                s.regs[n] = std::strtoul(v.c_str(), nullptr, 16);
            }
            else if (k == "noresult") s.noResult = true;
            else if (k == "quiet") s.quiet = true;
            else if (k == "end" && open)
            {
                if (s.args.empty()) { s.args = { { 0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u }, { 0, 1, 0x7FFFFFFFu, 0x80000000u } }; s.argRegs.resize(2); }
                if (!only || s.name.find(only) != std::string::npos) RunSpec(d, s, ++serial);
                open = false;
            }
        }
    }
    return 0;
}
