// US Most Wanted module — launcher side.
//
// Game-side (Ridgeline.UsMostWanted.dll) lives in modules/us_mostwanted/patch/.
// Schema setting keys here MUST match those read by the patch DLL.
//
// US Most Wanted is dual-process: the patch DLL is injected into MostWanted.exe
// (the game's launcher process); from inside, the patch hooks the engine-launch
// function and itself spawns Engine.exe + injects the same DLL into it. The
// IModule's launch() therefore only kicks off MostWanted.exe — Engine.exe is
// owned by the patch.

#include <ridgeline/Module.h>
#include <launcher_runtime/PatchedAppLauncher.h>
#include <common/config/IniFile.h>
#include <array>
#include <span>
#include <string>
#include <windows.h>

namespace {

constexpr const char* kInternalName    = "us_mostwanted";
constexpr const char* kDisplayName     = "US Most Wanted";
constexpr const char* kSettingsSection = "module.us_mostwanted";
constexpr const char* kPatchDllName    = "Ridgeline.UsMostWanted.dll";

constexpr std::array<const char*, 1> kDefaultExeNames{"MostWanted.exe"};

const ridgeline::SettingDef kSchema[] = {
    {
        .key = "WindowMode",
        .label = "Run windowed (instead of fullscreen)",
        .group = "Display",
        .type = ridgeline::SettingType::Bool,
        .default_value = false,
    },
    {
        .key = "SkipLauncher",
        .label = "Skip the game launcher (launch Engine.exe directly)",
        .group = "Startup",
        .type = ridgeline::SettingType::Bool,
        .default_value = false,
    },
    {
        .key = "SkipIntros",
        .label = "Skip game intro videos",
        .group = "Startup",
        .type = ridgeline::SettingType::Bool,
        .default_value = false,
    },
    {
        .key = "EnableCheats",
        .label = "Enable cheats (-cheats command line flag)",
        .group = "Gameplay",
        .type = ridgeline::SettingType::Bool,
        .default_value = false,
    },
    {
        .key = "QuickSaveKey",
        .label = "Quick save hotkey",
        .group = "Hotkeys",
        .type = ridgeline::SettingType::KeyBinding,
        .default_value = std::string("F5"),
    },
    {
        .key = "QuickLoadKey",
        .label = "Quick load hotkey",
        .group = "Hotkeys",
        .type = ridgeline::SettingType::KeyBinding,
        .default_value = std::string("F8"),
    },
};

class UsMwAppLauncher : public PatchedAppLauncher {
public:
    UsMwAppLauncher() : PatchedAppLauncher(kPatchDllName) {}

protected:
    std::string get_default_app_path() override
    {
        ridgeline::IniSection sec{ridgeline::get_ridgeline_ini_path(nullptr), kSettingsSection};
        return sec.get_string("GameExecutablePath", "");
    }
    // No SHA1 whitelist — accept any MostWanted.exe.
};

class UsMwModule : public ridgeline::IModule {
public:
    const char* internal_name() const override { return kInternalName; }
    const char* display_name() const override { return kDisplayName; }
    const char* settings_section() const override { return kSettingsSection; }

    std::span<const char* const> default_exe_filenames() const override
    {
        return {kDefaultExeNames.data(), kDefaultExeNames.size()};
    }

    bool is_configured() const override
    {
        ridgeline::IniSection sec{ridgeline::get_ridgeline_ini_path(nullptr), kSettingsSection};
        std::string path = sec.get_string("GameExecutablePath", "");
        if (path.empty()) return false;
        DWORD attrs = GetFileAttributesA(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
    }

    std::span<const ridgeline::SettingDef> settings_schema() const override
    {
        return std::span{kSchema};
    }

    void launch() override
    {
        UsMwAppLauncher launcher;
        launcher.launch();
    }
};

} // namespace

RIDGELINE_REGISTER_MODULE(UsMwModule)
