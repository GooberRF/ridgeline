#include "PatchedAppLauncher.h"
#include "sha1.h"
#include "Win32Handle.h"
#include "Process.h"
#include "Thread.h"
#include "DllInjector.h"
#include "InjectingProcessLauncher.h"
#include <common/error/Exception.h>
#include <common/error/Win32Error.h>
#include <common/utils/os-utils.h>
#include <xlog/xlog.h>
#include <windows.h>
#include <shlwapi.h>
#include <fstream>

// Needed by MinGW
#ifndef ERROR_ELEVATION_REQUIRED
#define ERROR_ELEVATION_REQUIRED 740
#endif

#define INIT_TIMEOUT 10000

inline bool is_no_gui_mode()
{
    return GetSystemMetrics(SM_CMONITORS) == 0;
}

std::string PatchedAppLauncher::get_patch_dll_path()
{
    auto buf = get_module_pathname(nullptr);

    // Get GetFinalPathNameByHandleA function address dynamically in order to support Windows XP
    using GetFinalPathNameByHandleA_Type = decltype(GetFinalPathNameByHandleA);
    HMODULE kernel32_module = GetModuleHandleA("kernel32");
    auto* GetFinalPathNameByHandleA_ptr = reinterpret_cast<GetFinalPathNameByHandleA_Type*>(reinterpret_cast<void(*)()>(
        GetProcAddress(kernel32_module, "GetFinalPathNameByHandleA")));
    // Make sure path is pointing to an actual module and not a symlink
    if (GetFinalPathNameByHandleA_ptr) {
        HANDLE file_handle = CreateFileA(buf.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file_handle != INVALID_HANDLE_VALUE) {
            buf.resize(MAX_PATH);

            DWORD result = GetFinalPathNameByHandleA(file_handle, buf.data(), buf.size(), FILE_NAME_NORMALIZED);
            if (result > buf.size()) {
                buf.resize(result);
                result = GetFinalPathNameByHandleA(file_handle, buf.data(), buf.size(), FILE_NAME_NORMALIZED);
            }
            if (result == 0 || result > buf.size()) {
                THROW_WIN32_ERROR();
            }
            // Windows Vista returns number of characters including the terminating null character
            if (buf[result - 1] == '\0') {
                --result;
            }
            buf.resize(result);
            CloseHandle(file_handle);
        }
    }

    // GetFinalPathNameByHandleA returns paths in the \\?\ long-path namespace
    // (e.g. \\?\C:\foo or \\?\UNC\server\share\foo). LoadLibraryA — used by
    // both the local and remote DLL load steps — does not accept that prefix
    // for normal drive-letter paths, so strip it. UNC paths get an extra
    // adjustment so \\?\UNC\server\share -> \\server\share.
    if (buf.compare(0, 4, "\\\\?\\") == 0) {
        if (buf.compare(4, 4, "UNC\\") == 0) {
            buf.erase(0, 6);          // "\\?\UNC\..." -> "\\..."
            buf[0] = '\\';
            buf[1] = '\\';
        } else {
            buf.erase(0, 4);          // "\\?\C:\..." -> "C:\..."
        }
    }

    std::string dir = get_dir_from_path(buf);
    xlog::info("Determined Ridgeline directory: {}", dir);
    return dir + "\\" + m_patch_dll_name;
}

std::string PatchedAppLauncher::get_app_path()
{
    if (m_forced_app_exe_path) {
        return m_forced_app_exe_path.value();
    }
    return get_default_app_path();
}

void PatchedAppLauncher::launch()
{
    std::string app_path = get_app_path();
    std::string work_dir = get_dir_from_path(app_path);
    verify_before_launch();

    STARTUPINFO si;
    setup_startup_info(si);

    std::string cmd_line = build_cmd_line(app_path);

    xlog::info("Starting the process: {}", app_path);
    try {
        InjectingProcessLauncher proc_launcher{app_path.c_str(), work_dir.c_str(), cmd_line.c_str(), si, INIT_TIMEOUT};

        std::string patch_dll_path = get_patch_dll_path();
        xlog::info("Injecting {}", patch_dll_path);
        proc_launcher.inject_dll(patch_dll_path.c_str(), "Init", INIT_TIMEOUT);

        xlog::info("Resuming app main thread");
        proc_launcher.resume_main_thread();
        xlog::info("Process launched successfully");

        // Wait for child process in Wine No GUI mode
        if (is_no_gui_mode()) {
            xlog::info("Waiting for app to close");
            proc_launcher.wait(INFINITE);
        }
    }
    catch (const Win32Error& e)
    {
        if (e.error() == ERROR_ELEVATION_REQUIRED)
            throw PrivilegeElevationRequiredException();
        throw;
    }
    catch (const ProcessTerminatedError&)
    {
        throw LauncherError(
            "Game process has terminated before injection!\n"
            "Check the game executable path configured for this module.");
    }
}

void PatchedAppLauncher::verify_before_launch()
{
    std::string app_path = get_app_path();
    std::ifstream file(app_path, std::fstream::in | std::fstream::binary);
    if (!file.is_open()) {
        throw FileNotFoundException(app_path);
    }

    // Hash check is opt-in. Modules whose check_app_hash returns true for any
    // input skip verification entirely (avoid the SHA1 hash for large EXEs).
    if (check_app_hash("")) {
        xlog::info("App hash verification skipped (module accepts any hash)");
        return;
    }

    xlog::info("Verifying {} SHA1", app_path);
    SHA1 sha1;
    sha1.update(file);
    auto hash = sha1.final();
    if (!check_app_hash(hash)) {
        throw FileHashVerificationException(app_path, hash);
    }
    xlog::info("SHA1 is valid");
}

void PatchedAppLauncher::setup_startup_info(_STARTUPINFOA& startup_info)
{
    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);

    if (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE))) {
        // Redirect std handles - fixes nohup logging
        startup_info.dwFlags |= STARTF_USESTDHANDLES;
        startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup_info.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup_info.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    }
}

static std::string escape_cmd_line_arg(const std::string& arg)
{
    std::string result;
    bool enclose_in_quotes = arg.find(' ') != std::string::npos;
    result.reserve(arg.size());
    if (enclose_in_quotes) {
        result += '"';
    }
    for (char c : arg) {
        if (c == '\\' || c == '"') {
            result += '\\';
        }
        result += c;
    }
    if (enclose_in_quotes) {
        result += '"';
    }
    return result;
}

std::string PatchedAppLauncher::build_cmd_line(const std::string& app_path)
{
    std::vector<std::string> all_args;
    all_args.push_back(app_path);
    all_args.insert(all_args.end(), m_args.begin(), m_args.end());

    std::string cmd_line;
    for (const auto& arg : all_args) {
        if (!cmd_line.empty()) {
            cmd_line += ' ';
        }
        cmd_line += escape_cmd_line_arg(arg);
    }
    return cmd_line;
}
