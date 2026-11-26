// Watch panel: the row menu's "View as Struct..." item, driven headlessly. It must hand the App the
// watch's resolved address and its name (the App forwards both to the Structs panel) and nothing else.
#include <cstdio>
#include <iostream>
#include <vector>

#include "ImGuiHarness.h"
#include "imgui_internal.h"   // popup windows have no public accessor
#include "Theme.h"
#include "WatchPanel.h"

using namespace sfe;

namespace
{
int gFailures = 0;
void Check(bool condition, const char* expression, int line)
{
    if (condition) return;
    std::cerr << "CHECK failed at line " << line << ": " << expression << '\n';
    ++gFailures;
}
}  // namespace
#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

namespace
{
constexpr uint32_t kBase = 0x06000000u;

class MockBackend : public IMemoryBackend
{
public:
    bool Connected() const override { return true; }
    uint64_t SourceId() const override { return 1; }
    bool CanWrite(uint32_t) const override { return false; }
    std::string WriteRefusal(uint32_t) const override { return "read only"; }
    WriteDest WriteDestination(uint32_t) const override { return WriteDest::ViewOnly; }
    std::vector<MemoryReadResult> ReadMemoryBatch(const std::vector<MemoryReadRequest>& rs) override
    {
        std::vector<MemoryReadResult> out;
        for (const MemoryReadRequest& r : rs)
        {
            MemoryReadResult res;
            res.success = true;
            res.bytes.assign(r.size, 0x5A);
            out.push_back(std::move(res));
        }
        return out;
    }
    size_t WriteMemory(uint32_t, const uint8_t*, size_t) override { return 0; }
};

class StubPlatform : public IPlatform
{
public:
    bool Initialize(const PlatformConfig&) override { return true; }
    void Shutdown() override {}
    bool PumpEvents() override { return true; }
    void BeginFrame() override {}
    void EndFrame() override {}
    TextureHandle CreateTexture(int, int) override { return 0; }
    void UpdateTexture(TextureHandle, const void*, int, int) override {}
    void DestroyTexture(TextureHandle) override {}
    bool OpenFileDialog(std::string&) override { return false; }
    SaveOutcome SaveFile(const char*, const void*, size_t) override { return SaveOutcome::Cancelled; }
};

struct Fixture
{
    MockBackend   backend;
    SimpleExpressionResolver resolver;
    StubPlatform  platform;
    BreakpointManager bps;
    WatchPanel    panel;
    WatchPanel::ViewAsStructRequest req;
    uint32_t      hexJump = 0;
    bool          offerStruct = true;
    ImGuiHarness  h;

    Fixture() : h([this] { Ui(); })
    {
        ApplyTheme(ImGui::GetStyle());
        LoadFonts(ImGui::GetIO());
        panel.AddWatch("player_hp", "0x06001230", WatchType::U16);
        h.Settle();
    }
    void Ui()
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(1000, 400), ImGuiCond_Always);
        panel.Draw(backend, resolver, platform, bps, 0.2f, &hexJump, offerStruct ? &req : nullptr);
    }
};

ImGuiWindow* OpenPopup()
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* pop = nullptr;
    for (ImGuiWindow* w : g.Windows)
        if (w->WasActive && (w->Flags & ImGuiWindowFlags_Popup)) pop = w;
    return pop;
}

// Open the first row's "..." menu. The button's rect is not exported, so sweep the right edge of the
// first data row until a popup appears; a stray click lands on an edit box or empty cell, which is harmless.
ImGuiWindow* OpenRowMenu(Fixture& f)
{
    for (float y = 40.0f; y < 120.0f; y += 6.0f)
        for (float x = 990.0f; x > 900.0f; x -= 5.0f)
        {
            const ImVec2 p(x, y);
            f.h.Hover(p);
            f.h.Click(p);
            f.h.Settle();
            if (ImGuiWindow* pop = OpenPopup()) return pop;
        }
    return nullptr;
}

// Items before it: Edit, Delete, Disable, Copy Address, Copy Value, Break x3, View in Memory (9), with
// three separators; a separator takes a smaller step than an item.
bool ClickViewAsStruct(Fixture& f)
{
    ImGuiWindow* pop = OpenRowMenu(f);
    if (!pop) return false;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    const float step = g.FontSize + g.Style.ItemSpacing.y;
    const float sepStep = 1.0f + g.Style.ItemSpacing.y;
    const ImVec2 p(pop->Pos.x + 20.0f, pop->Pos.y + g.Style.WindowPadding.y + 9 * step + 3 * sepStep + g.FontSize * 0.5f);
    f.h.Hover(p);
    f.h.Click(p);
    f.h.Settle();
    return true;
}

void TestViewAsStructHandsOverAddressAndName()
{
    Fixture f;
    CHECK(ClickViewAsStruct(f));
    CHECK(f.req.pending);
    CHECK(f.req.address == 0x06001230u);
    CHECK(f.req.label == "player_hp");
    CHECK(f.hexJump == 0);   // it is not "View in Memory"
}

// A host that passes no request pointer (the default) gets a disabled item: nothing is written anywhere.
void TestNoHookNoCrash()
{
    Fixture f;
    f.offerStruct = false;
    CHECK(ClickViewAsStruct(f));
    CHECK(!f.req.pending);
}
}  // namespace

int main()
{
    TestViewAsStructHandsOverAddressAndName();
    TestNoHookNoCrash();
    if (gFailures) { std::cerr << gFailures << " check(s) failed\n"; return 1; }
    std::cout << "WatchPanelTests passed\n";
    return 0;
}
