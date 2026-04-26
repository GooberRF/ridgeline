#include "MainWindow.h"

#include <ridgeline/Module.h>
#include <common/config/IniFile.h>
#include <common/version/version.h>
#include <crash_handler_stub.h>
#include <xlog/xlog.h>
#include <xlog/FileAppender.h>
#include <xlog/Win32Appender.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace {

// Returns "<launcher dir>\logs" and ensures the directory exists. Both the
// launcher log and any crash report end up here.
std::string ensure_logs_dir()
{
    char exe_path[MAX_PATH];
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    std::string dir = exe_path;
    auto sep = dir.find_last_of("\\/");
    if (sep != std::string::npos) dir.resize(sep);
    std::string logs_dir = dir + "\\logs";
    CreateDirectoryA(logs_dir.c_str(), nullptr);
    return logs_dir;
}

// Set up xlog so every session writes a fresh Ridgeline.log into <launcher
// dir>\logs (and mirrors to OutputDebugString for IDE attach). Truncated on
// each launch so the file never grows unbounded.
std::string init_logging()
{
    std::string log_path = ensure_logs_dir() + "\\Ridgeline.log";
    xlog::LoggerConfig::get().add_appender(
        std::make_unique<xlog::FileAppender>(log_path, /*append=*/false, /*flush=*/true));
    xlog::LoggerConfig::get().add_appender(std::make_unique<xlog::Win32Appender>());
    return log_path;
}


void log_module_inventory()
{
    auto modules = ridgeline::ModuleRegistry::instance().all();
    xlog::info("Module registry: {} module(s)", modules.size());
    for (auto* m : modules) {
        xlog::info("  - {} ({}): is_configured={}",
                   m->display_name(), m->internal_name(), m->is_configured());
    }
}

} // namespace

// Ridgeline launcher entry point.
//
// Normal start: enumerates ModuleRegistry, launches the main window.
// `--smoke-test` arg: writes the registered module list to a file beside the
// exe and exits with no UI. Useful for verifying that a module's
// /WHOLEARCHIVE static-init registration plumbing actually fires (e.g. when
// adding a new module, run `Ridgeline.exe --smoke-test` and grep the output
// file for the new module's display name).

static std::string format_module_list()
{
    auto modules = ridgeline::ModuleRegistry::instance().all();
    std::string body = std::string(PRODUCT_NAME_VERSION) + "\n\n";
    if (modules.empty()) {
        body += "No modules are registered.\n\n"
                "If you expected modules to appear, the linker likely stripped "
                "their static initializers. Verify that each module's static lib "
                "is linked with WHOLE_ARCHIVE in launcher/CMakeLists.txt.";
    } else {
        body += "Registered modules:\n";
        for (auto* m : modules) {
            body += "  - ";
            body += m->display_name();
            body += "  (";
            body += m->internal_name();
            body += ")\n";
        }
    }
    return body;
}

int APIENTRY WinMain(HINSTANCE instance, HINSTANCE, LPSTR cmd_line, int)
{
    if (cmd_line && std::strstr(cmd_line, "--smoke-test")) {
        char exe_path[MAX_PATH];
        GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
        std::string out_path = exe_path;
        auto sep = out_path.find_last_of("\\/");
        if (sep != std::string::npos) out_path.resize(sep + 1);
        out_path += "smoke-test-output.txt";
        FILE* f = std::fopen(out_path.c_str(), "w");
        if (f) {
            std::fputs(format_module_list().c_str(), f);
            std::fclose(f);
        }
        return 0;
    }

    std::string log_path = init_logging();
    ridgeline::apply_ridgeline_log_level(nullptr);
    install_crash_handler(GetModuleHandleA(nullptr), "Ridgeline", log_path);
    xlog::info("==== {} starting ====", PRODUCT_NAME_VERSION);
    xlog::info("Build: {} {}", get_build_date(), get_build_time());
    xlog::info("Log: {}", log_path);
    log_module_inventory();

    int rc = ridgeline_launcher::MainWindow::run(instance);

    xlog::info("==== Ridgeline exiting (rc={}) ====", rc);
    return rc;
}
