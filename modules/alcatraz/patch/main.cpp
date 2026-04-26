// Alcatraz module — game-side (injected DLL).
//
// Built as Ridgeline.Alcatraz.dll; injected into Alcatraz.exe by the launcher
// via the standard PatchedAppLauncher / InjectingProcessLauncher path. Reads
// settings from [module.alcatraz] in ridgeline.ini (sibling of Ridgeline.exe).

#include "rendering.h"
#include "misc.h"
#include <common/config/IniFile.h>
#include <crash_handler_stub.h>
#include <xlog/xlog.h>
#include <xlog/FileAppender.h>
#include <xlog/Win32Appender.h>
#include <windows.h>
#include <string>
#include <unordered_map>

namespace {

constexpr const char* kSettingsSection = "module.alcatraz";

HMODULE g_self_module = nullptr;

std::string get_self_dir()
{
    char buf[MAX_PATH];
    GetModuleFileNameA(g_self_module, buf, MAX_PATH);
    std::string path(buf);
    auto pos = path.find_last_of("\\/");
    return (pos != std::string::npos) ? path.substr(0, pos) : ".";
}

std::string init_logging()
{
    std::string log_dir = get_self_dir() + "\\logs";
    CreateDirectoryA(log_dir.c_str(), nullptr);
    std::string log_path = log_dir + "\\alcatraz.log";
    xlog::LoggerConfig::get().add_appender(std::make_unique<xlog::FileAppender>(log_path, false));
    xlog::LoggerConfig::get().add_appender(std::make_unique<xlog::Win32Appender>());
    xlog::info("Logging initialized -> {}", log_path);
    return log_path;
}

int parse_window_mode(const std::string& s)
{
    // String values match the AlcatrazModule schema's enum_options.
    if (s == "Windowed") return 1;
    if (s == "Borderless") return 2;
    return 0; // Fullscreen
}

int parse_texture_quality(const std::string& s)
{
    if (s == "Medium") return 1;
    if (s == "Low") return 2;
    if (s == "VeryLow") return 3;
    return 0; // High
}

void load_config()
{
    std::string ini_path = ridgeline::get_ridgeline_ini_path(g_self_module);
    ridgeline::IniSection sec{ini_path, kSettingsSection};

    g_fps_fix_enabled  = sec.get_bool("FpsFix", true);
    g_res_width        = sec.get_int("ResWidth", 0);
    g_res_height       = sec.get_int("ResHeight", 0);
    g_window_mode      = parse_window_mode(sec.get_string("WindowMode", "Fullscreen"));
    g_skip_launcher    = sec.get_bool("SkipLauncher", false);
    g_texture_quality  = parse_texture_quality(sec.get_string("TextureQuality", "High"));
    g_fast_loading     = sec.get_bool("FastLoading", true);
    g_developer_console= sec.get_bool("DeveloperConsole", false);

    xlog::info("Config loaded from {}", ini_path);
    xlog::info("  FpsFix: {}", g_fps_fix_enabled);
    xlog::info("  Resolution: {}x{}", g_res_width, g_res_height);
    xlog::info("  WindowMode: {}", g_window_mode);
    xlog::info("  SkipLauncher: {}", g_skip_launcher);
    xlog::info("  TextureQuality: {}", g_texture_quality);
    xlog::info("  FastLoading: {}", g_fast_loading);
    xlog::info("  DeveloperConsole: {}", g_developer_console);
}

void apply_patches()
{
    rendering_apply_patches();
    misc_apply_patches();
}

} // namespace

// Called by the launcher after the DLL is injected (CreateRemoteThread on
// the exported symbol "Init"). Returning 0 signals failure to the injector.
extern "C" __declspec(dllexport) DWORD WINAPI Init([[maybe_unused]] LPVOID param)
{
    std::string log_path = init_logging();
    ridgeline::apply_ridgeline_log_level(g_self_module);
    install_crash_handler(g_self_module, "Alcatraz", log_path);
    xlog::info("Ridgeline.Alcatraz.dll loaded");

    load_config();
    apply_patches();

    xlog::info("All patches applied");
    return 1;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, [[maybe_unused]] LPVOID reserved)
{
    DisableThreadLibraryCalls(module);
    if (reason == DLL_PROCESS_ATTACH)
        g_self_module = module;
    return TRUE;
}
