// Alcatraz module — launcher side.
//
// Implements ridgeline::IModule and self-registers via RIDGELINE_REGISTER_MODULE
// at static-init time. The launcher links this static lib with /WHOLEARCHIVE so
// the registrar object is preserved.
//
// Game-side code (the injected DLL Ridgeline.Alcatraz.dll) lives in
// modules/alcatraz/patch/. Launcher side and patch side share only the INI
// section name "module.alcatraz" and the schema's setting keys (kept in sync
// by hand — eight strings).

#include <ridgeline/Module.h>
#include <launcher_runtime/PatchedAppLauncher.h>
#include <common/config/IniFile.h>
#include <array>
#include <span>
#include <string>
#include <windows.h>

namespace {

constexpr const char* kInternalName    = "alcatraz";
constexpr const char* kDisplayName     = "Alcatraz";
constexpr const char* kSettingsSection = "module.alcatraz";
constexpr const char* kPatchDllName    = "Ridgeline.Alcatraz.dll";

constexpr std::array<const char*, 1> kDefaultExeNames{"Alcatraz.exe"};

// Settings schema. The keys MUST match those read by the patch DLL's
// load_config() (modules/alcatraz/patch/main.cpp).
constexpr std::array<const char*, 3> kWindowModeOptions{"Fullscreen", "Windowed", "Borderless"};
constexpr std::array<const char*, 4> kTextureQualityOptions{"High", "Medium", "Low", "VeryLow"};

const ridgeline::SettingDef kSchema[] = {
    {
        .key = "FpsFix",
        .label = "FPS uncap fix",
        .group = "Performance",
        .type = ridgeline::SettingType::Bool,
        .default_value = true,
    },
    {
        .key = "FastLoading",
        .label = "Faster level loads (prefetch + sequential I/O)",
        .group = "Performance",
        .type = ridgeline::SettingType::Bool,
        .default_value = true,
    },
    {
        .key = "TextureQuality",
        .label = "Texture quality",
        .group = "Performance",
        .type = ridgeline::SettingType::Enum,
        .default_value = std::string("High"),
        .enum_options = {kTextureQualityOptions.data(), kTextureQualityOptions.size()},
    },
    {
        .key = "ResWidth",
        .label = "Resolution width (0 = game default)",
        .group = "Display",
        .type = ridgeline::SettingType::Int,
        .default_value = 0,
        .int_range = std::pair{0, 7680},
    },
    {
        .key = "ResHeight",
        .label = "Resolution height (0 = game default)",
        .group = "Display",
        .type = ridgeline::SettingType::Int,
        .default_value = 0,
        .int_range = std::pair{0, 4320},
    },
    {
        .key = "WindowMode",
        .label = "Window mode",
        .group = "Display",
        .type = ridgeline::SettingType::Enum,
        .default_value = std::string("Fullscreen"),
        .enum_options = {kWindowModeOptions.data(), kWindowModeOptions.size()},
    },
    {
        .key = "SkipLauncher",
        .label = "Skip the game's resolution-picker launcher dialog",
        .group = "Startup",
        .type = ridgeline::SettingType::Bool,
        .default_value = false,
    },
    {
        .key = "DeveloperConsole",
        .label = "Enable developer console (~ key)",
        .group = "Debug",
        .type = ridgeline::SettingType::Bool,
        .default_value = false,
    },
};

// Bridge subclass — supplies the executable path from the module's INI section.
class AlcatrazAppLauncher : public PatchedAppLauncher {
public:
    AlcatrazAppLauncher() : PatchedAppLauncher(kPatchDllName) {}

protected:
    std::string get_default_app_path() override
    {
        ridgeline::IniSection sec{ridgeline::get_ridgeline_ini_path(nullptr), kSettingsSection};
        return sec.get_string("GameExecutablePath", "");
    }
    // No hash whitelist — Alcatraz versions vary; the SHA1 check is opt-in
    // (see PatchedAppLauncher base class) and we accept any executable.
};

class AlcatrazModule : public ridgeline::IModule {
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
        AlcatrazAppLauncher launcher;
        launcher.launch();
    }
};

} // namespace

RIDGELINE_REGISTER_MODULE(AlcatrazModule)
