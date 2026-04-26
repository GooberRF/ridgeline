#pragma once

#include <windows.h>
#include <cstring>
#include <string>

struct CrashHandlerConfig
{
    HMODULE this_module_handle = nullptr;
    char output_dir[MAX_PATH] = {};
    char log_file[MAX_PATH] = {};
    char app_name[256] = {};
    char known_modules[8][32] = {};
    int num_known_modules = 0;

    void add_known_module(const char* module_name)
    {
        std::strcpy(known_modules[num_known_modules++], module_name);
    }
};

void CrashHandlerStubInstall(const CrashHandlerConfig& config);
void CrashHandlerStubUninstall();
void CrashHandlerStubProcessException(PEXCEPTION_POINTERS exception_ptrs, DWORD thread_id);

// Convenience setup. Derives the crash-dump output dir as `<dir of self>\logs`
// (creating it if needed), writes app_name + log_path into the config, and
// installs the unhandled-exception filter. Use this from every Ridgeline
// binary that wants crash dumps:
//   - launcher: install_crash_handler(GetModuleHandle(nullptr), "Ridgeline", launcher_log_path)
//   - injected module DLL: install_crash_handler(g_self_module, "<module>", module_log_path)
// The resulting <app_name>-crash.zip lands in <launcher dir>\logs\.
void install_crash_handler(HMODULE self,
                           const std::string& app_name,
                           const std::string& log_path);
