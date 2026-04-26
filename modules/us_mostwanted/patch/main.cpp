// US Most Wanted module — game-side (injected DLL).
//
// Built as Ridgeline.UsMostWanted.dll. Injected by the launcher into
// MostWanted.exe (the game's own launcher). Inside MostWanted.exe, this DLL's
// patches hook the engine-launch function and themselves spawn Engine.exe +
// inject the same DLL into it (see misc.cpp launch_engine_hook +
// inject_dll_into_process). The DLL's Init() discriminates by host-process
// name and applies the appropriate patch set.

#include "misc.h"
#include <common/config/IniFile.h>
#include <crash_handler_stub.h>
#include <xlog/xlog.h>
#include <xlog/FileAppender.h>
#include <xlog/Win32Appender.h>
#include <windows.h>
#include <cstring>
#include <string>

// Captured in DllMain; misc.cpp uses this to find its own DLL path for the
// remote-injection step into Engine.exe.
HMODULE g_module = nullptr;

namespace {

std::string get_self_dir()
{
    char buf[MAX_PATH];
    GetModuleFileNameA(g_module, buf, MAX_PATH);
    std::string path(buf);
    auto pos = path.find_last_of("\\/");
    return (pos != std::string::npos) ? path.substr(0, pos) : ".";
}

std::string init_logging(const char* filename_suffix)
{
    std::string log_dir = get_self_dir() + "\\logs";
    CreateDirectoryA(log_dir.c_str(), nullptr);
    std::string log_path = log_dir + "\\us_mostwanted_" + filename_suffix + ".log";
    xlog::LoggerConfig::get().add_appender(std::make_unique<xlog::FileAppender>(log_path, false));
    xlog::LoggerConfig::get().add_appender(std::make_unique<xlog::Win32Appender>());
    xlog::info("Logging initialized -> {}", log_path);
    return log_path;
}

bool host_process_is(const char* exe_basename)
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    // Case-insensitive substring match against the basename.
    char lower_path[MAX_PATH];
    char lower_target[MAX_PATH];
    size_t i;
    for (i = 0; path[i] && i + 1 < MAX_PATH; ++i)
        lower_path[i] = (char)tolower((unsigned char)path[i]);
    lower_path[i] = '\0';
    for (i = 0; exe_basename[i] && i + 1 < MAX_PATH; ++i)
        lower_target[i] = (char)tolower((unsigned char)exe_basename[i]);
    lower_target[i] = '\0';
    return std::strstr(lower_path, lower_target) != nullptr;
}

} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI Init([[maybe_unused]] LPVOID param)
{
    // Per-process log file so MostWanted.exe and Engine.exe don't clobber each other.
    std::string log_path;
    std::string app_name;
    if (host_process_is("mostwanted.exe")) {
        log_path = init_logging("mostwanted");
        app_name = "UsMostWanted_MostWanted";
    } else if (host_process_is("engine.exe")) {
        log_path = init_logging("engine");
        app_name = "UsMostWanted_Engine";
    } else {
        log_path = init_logging("unknown");
        app_name = "UsMostWanted";
    }
    ridgeline::apply_ridgeline_log_level(g_module);
    install_crash_handler(g_module, app_name, log_path);
    xlog::info("Ridgeline.UsMostWanted.dll loaded");

    std::string ini_path = ridgeline::get_ridgeline_ini_path(g_module);
    xlog::info("Loading config from {}", ini_path);
    load_misc_config(ini_path);

    if (host_process_is("mostwanted.exe")) {
        xlog::info("Host = MostWanted.exe — applying launcher patches");
        apply_misc_patches();
    } else if (host_process_is("engine.exe")) {
        xlog::info("Host = Engine.exe — applying engine patches");
        apply_engine_patches();
    } else {
        xlog::warn("Unknown host process — no patches applied");
    }

    xlog::info("All patches applied");
    return 1;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, [[maybe_unused]] LPVOID reserved)
{
    DisableThreadLibraryCalls(module);
    if (reason == DLL_PROCESS_ATTACH)
        g_module = module;
    return TRUE;
}
