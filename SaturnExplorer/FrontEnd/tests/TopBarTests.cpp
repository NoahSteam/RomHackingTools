#include <iostream>

#include "Launcher.h"
#include "Settings.h"
#include "TopBar.h"

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

static void TestEnablementMatrix()
{
    TopBarViewModel state;
    CHECK(TopBarCommandEnabled(TopBarCommandType::ConnectLive, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::DisconnectLive, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::StartRecording, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::TogglePause, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::DumpMemory, state));

    state.source = SourceType::Dump;
    CHECK(TopBarCommandEnabled(TopBarCommandType::ConnectLive, state));
    CHECK(TopBarCommandEnabled(TopBarCommandType::DumpMemory, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::TogglePause, state));

    state.source = SourceType::Live;
    state.connected = true;
    state.frameControl = true;
    CHECK(!TopBarCommandEnabled(TopBarCommandType::ConnectLive, state));
    CHECK(TopBarCommandEnabled(TopBarCommandType::DisconnectLive, state));
    CHECK(TopBarCommandEnabled(TopBarCommandType::StartRecording, state));
    CHECK(TopBarCommandEnabled(TopBarCommandType::TogglePause, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::StepFrame, state));

    state.paused = true;
    CHECK(TopBarCommandEnabled(TopBarCommandType::StepFrame, state));
    state.recording = true;
    CHECK(!TopBarCommandEnabled(TopBarCommandType::StartRecording, state));
    CHECK(TopBarCommandEnabled(TopBarCommandType::StopRecording, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::LoadRawDump, state));
}

static void TestLaunchModel()
{
    CHECK(BuildLaunchArgs("-a -i \"{rom}\"", "C:\\Games\\Saturn Disc.cue") ==
          "-a -i \"C:\\Games\\Saturn Disc.cue\"");
    CHECK(BuildLaunchArgs("--bios {bios} \"{rom}\"", "game.cue", "") == "--bios \"game.cue\"");
    CHECK(PathBasename("C:\\Games\\disc.cue") == "disc.cue");

    Settings settings;
    settings.Set("emulators", "mednafen", "definitely-missing-mednafen.exe");
    settings.Set("launch", "emulator", "mednafen");
    settings.Set("launch", "rom", "definitely-missing-game.cue");
    Launcher launcher;
    launcher.Load(settings);
    const LaunchValidation validation = launcher.Validate();
    CHECK(!validation.valid);
    CHECK(validation.message.find("executable") != std::string::npos);

    launcher.SetRom("a.cue");
    launcher.SetRom("b.cue");
    launcher.SetRom("a.cue");
    CHECK(launcher.Recent().size() == 2);
    CHECK(launcher.Recent()[0] == "a.cue");

    CHECK(ShouldAutoConnectAfterLaunch("mednafen", SourceType::None));
    CHECK(!ShouldAutoConnectAfterLaunch("yabause", SourceType::None));
    CHECK(!ShouldAutoConnectAfterLaunch("mednafen", SourceType::Dump));
    CHECK(!ShouldAutoConnectAfterLaunch("mednafen", SourceType::Live));
}

int main()
{
    TestEnablementMatrix();
    TestLaunchModel();
    if (gFailures != 0)
    {
        std::cerr << gFailures << " top-bar model check(s) failed\n";
        return 1;
    }
    std::cout << "Top-bar model tests passed\n";
    return 0;
}
