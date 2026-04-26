#include "misc.h"
#include <patch_runtime/FunHook.h>
#include <patch_runtime/AsmWriter.h>
#include <patch_runtime/MemUtils.h>
#include <xlog/xlog.h>
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>

// Defined in main.cpp — captured in DllMain
extern HMODULE g_module;

// Settings read from ridgeline.ini by the patch DLL
static bool g_skip_intros = false;
static bool g_enable_cheats = false;
static int g_window_mode = 0;
static int g_quicksave_vk = VK_F5;
static int g_quickload_vk = VK_F8;

struct KeyEntry { const char* name; int vk; };

static const KeyEntry g_key_table[] = {
    {"F1", VK_F1}, {"F2", VK_F2}, {"F3", VK_F3}, {"F4", VK_F4},
    {"F5", VK_F5}, {"F6", VK_F6}, {"F7", VK_F7}, {"F8", VK_F8},
    {"F9", VK_F9}, {"F10", VK_F10}, {"F11", VK_F11}, {"F12", VK_F12},
    {"0", '0'}, {"1", '1'}, {"2", '2'}, {"3", '3'}, {"4", '4'},
    {"5", '5'}, {"6", '6'}, {"7", '7'}, {"8", '8'}, {"9", '9'},
    {"A", 'A'}, {"B", 'B'}, {"C", 'C'}, {"D", 'D'}, {"E", 'E'},
    {"F", 'F'}, {"G", 'G'}, {"H", 'H'}, {"I", 'I'}, {"J", 'J'},
    {"K", 'K'}, {"L", 'L'}, {"M", 'M'}, {"N", 'N'}, {"O", 'O'},
    {"P", 'P'}, {"Q", 'Q'}, {"R", 'R'}, {"S", 'S'}, {"T", 'T'},
    {"U", 'U'}, {"V", 'V'}, {"W", 'W'}, {"X", 'X'}, {"Y", 'Y'}, {"Z", 'Z'},
    {"Space", VK_SPACE}, {"Enter", VK_RETURN}, {"Tab", VK_TAB},
    {"Backspace", VK_BACK}, {"Delete", VK_DELETE}, {"Insert", VK_INSERT},
    {"Home", VK_HOME}, {"End", VK_END}, {"PageUp", VK_PRIOR}, {"PageDown", VK_NEXT},
    {"Up", VK_UP}, {"Down", VK_DOWN}, {"Left", VK_LEFT}, {"Right", VK_RIGHT},
    {"NumPad0", VK_NUMPAD0}, {"NumPad1", VK_NUMPAD1}, {"NumPad2", VK_NUMPAD2},
    {"NumPad3", VK_NUMPAD3}, {"NumPad4", VK_NUMPAD4}, {"NumPad5", VK_NUMPAD5},
    {"NumPad6", VK_NUMPAD6}, {"NumPad7", VK_NUMPAD7}, {"NumPad8", VK_NUMPAD8},
    {"NumPad9", VK_NUMPAD9}, {"Num*", VK_MULTIPLY}, {"Num+", VK_ADD},
    {"Num-", VK_SUBTRACT}, {"Num.", VK_DECIMAL}, {"Num/", VK_DIVIDE},
    {"`", VK_OEM_3}, {"-", VK_OEM_MINUS}, {"=", VK_OEM_PLUS},
    {"[", VK_OEM_4}, {"]", VK_OEM_6}, {"\\", VK_OEM_5},
    {";", VK_OEM_1}, {"'", VK_OEM_7}, {",", VK_OEM_COMMA},
    {".", VK_OEM_PERIOD}, {"/", VK_OEM_2},
};
static constexpr int g_key_table_size = sizeof(g_key_table) / sizeof(g_key_table[0]);

// Convert a key name string to a virtual key code
static int key_name_to_vk(const std::string& name)
{
    for (int i = 0; i < g_key_table_size; i++) {
        if (_stricmp(name.c_str(), g_key_table[i].name) == 0)
            return g_key_table[i].vk;
    }
    return 0;
}

// Global install path buffer used by MostWanted.exe at 0x0041F59C.
// The launcher reads the game's install directory from the registry into this buffer.
// We point to it so our hook can populate it directly.
static auto& install_path_buf = addr_as_ref<char[1024]>(0x0041F59C);

// Returns the game root directory (parent of Bin\) from MostWanted.exe's path.
// MostWanted.exe lives in {game_root}\Bin\, but the original launcher stored
// {game_root} in the registry, so all paths (e.g. "Bin\engine.exe") are relative
// to the root.
static std::string get_game_dir()
{
    char exe_path[MAX_PATH];
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);

    // Strip filename → get Bin directory
    char* last_sep = strrchr(exe_path, '\\');
    if (!last_sep)
        last_sep = strrchr(exe_path, '/');
    if (last_sep)
        *last_sep = '\0';

    // Strip "Bin" → get game root
    last_sep = strrchr(exe_path, '\\');
    if (!last_sep)
        last_sep = strrchr(exe_path, '/');
    if (last_sep)
        *last_sep = '\0';

    return exe_path;
}

// Hook for FUN_00401D40 — resolves the game install path.
// Original reads HKLM\SOFTWARE\Activision Value\FUN labs "US Most Wanted" from registry.
// We replace it with the directory of the running executable (MostWanted.exe).
static FunHook<void()> get_install_path_hook{0x00401D40, []() {
    std::string game_dir = get_game_dir();
    strncpy(install_path_buf, game_dir.c_str(), sizeof(install_path_buf) - 1);
    install_path_buf[sizeof(install_path_buf) - 1] = '\0';
    xlog::info("Install path resolved to: {}", install_path_buf);
}};

// Update r_windowed in the game's console.cfg (MW\Cfg\console.cfg).
// Format: "addvar r_windowed int; r_windowed = <value>"
static void write_windowed_config(const std::string& game_dir)
{
    // The engine loads console.cfg from the mod directory: {root}\MW\Cfg\console.cfg
    // (via +mod MW, which sets the file search path to MW\)
    std::string cfg_path = game_dir + "\\MW\\Cfg\\console.cfg";
    std::string windowed_val = std::to_string(g_window_mode);

    std::string contents;
    bool found = false;

    FILE* f = fopen(cfg_path.c_str(), "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            std::string s(line);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
                s.pop_back();
            if (s.find("r_windowed") != std::string::npos) {
                s = "addvar r_windowed int; r_windowed = " + windowed_val;
                found = true;
            }
            contents += s + "\n";
        }
        fclose(f);
    }
    if (!found)
        contents += "addvar r_windowed int; r_windowed = " + windowed_val + "\n";

    f = fopen(cfg_path.c_str(), "w");
    if (f) {
        fputs(contents.c_str(), f);
        fclose(f);
        xlog::info("Set r_windowed = {} in {}", windowed_val, cfg_path);
    } else {
        xlog::warn("Failed to write {}", cfg_path);
    }
}


// Inject GamePatch.dll into a remote process and call Init.
static void inject_dll_into_process(HANDLE process)
{
    // Get our own DLL path
    extern HMODULE g_module;
    char dll_path[MAX_PATH];
    GetModuleFileNameA(g_module, dll_path, MAX_PATH);

    xlog::info("Injecting {} into Engine.exe", dll_path);

    // Allocate memory in the remote process for the DLL path
    size_t path_len = strlen(dll_path) + 1;
    void* remote_buf = VirtualAllocEx(process, nullptr, path_len, MEM_COMMIT, PAGE_READWRITE);
    if (!remote_buf) {
        xlog::warn("VirtualAllocEx failed (error {})", GetLastError());
        return;
    }
    WriteProcessMemory(process, remote_buf, dll_path, path_len, nullptr);

    // Call LoadLibraryA in the remote process
    FARPROC load_lib = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    HANDLE thread = CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(load_lib), remote_buf, 0, nullptr);
    if (!thread) {
        xlog::warn("CreateRemoteThread for LoadLibrary failed (error {})", GetLastError());
        VirtualFreeEx(process, remote_buf, 0, MEM_RELEASE);
        return;
    }
    WaitForSingleObject(thread, 10000);

    // Get the remote HMODULE (return value of LoadLibraryA)
    DWORD remote_hmod = 0;
    GetExitCodeThread(thread, &remote_hmod);
    CloseHandle(thread);
    VirtualFreeEx(process, remote_buf, 0, MEM_RELEASE);

    if (!remote_hmod) {
        xlog::warn("Remote LoadLibrary failed");
        return;
    }

    // Find Init's offset from our local module base, then call it in the remote process.
    // MSVC may decorate __stdcall as "_Init@4" even with extern "C" on 32-bit.
    FARPROC local_init = GetProcAddress(g_module, "Init");
    if (!local_init)
        local_init = GetProcAddress(g_module, "_Init@4");
    if (!local_init) {
        xlog::warn("Could not find Init export");
        return;
    }
    intptr_t init_offset = reinterpret_cast<intptr_t>(local_init) -
                           reinterpret_cast<intptr_t>(g_module);
    FARPROC remote_init = reinterpret_cast<FARPROC>(remote_hmod + init_offset);

    thread = CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_init), nullptr, 0, nullptr);
    if (thread) {
        WaitForSingleObject(thread, 10000);
        DWORD exit_code = 0;
        GetExitCodeThread(thread, &exit_code);
        CloseHandle(thread);
        xlog::info("Engine.exe Init returned {}", exit_code);
    } else {
        xlog::warn("CreateRemoteThread for Init failed (error {})", GetLastError());
    }
}

// --- Engine.exe patches ---
// These run inside Engine.exe's process after DLL injection.

// Function pointer types for EngineDll.dll exports
using ExecFn = int (__thiscall*)(void* console, char* cmd, int flag);
using PrintcFn = int (__cdecl*)(void* console, unsigned color, const char* fmt, ...);
using AddStaticFuncFn = void* (__thiscall*)(void* console, char* name, void(__cdecl* func)());
using FindFirstFn = char* (__thiscall*)(void* fm, char* pattern);
using FindNextFn = char* (__thiscall*)(void* fm);

// Globals resolved in apply_engine_patches
static void** g_pp_console = nullptr;
static void** g_pp_fileManager = nullptr;
static PrintcFn g_printc = nullptr;
static FindFirstFn g_findFirst = nullptr;
static FindNextFn g_findNext = nullptr;

static void __cdecl cmd_listmaps()
{
    void* console = *g_pp_console;
    void* fm = *g_pp_fileManager;
    if (!console || !fm) return;

    g_printc(console, 0xFFFFFFFF, "--- BSP Maps ---\n");
    int count = 0;
    char pattern[] = "Bsp\\*.bsp";
    char* name = g_findFirst(fm, pattern);
    while (name) {
        g_printc(console, 0xFFFFFFFF, "  %s\n", name);
        count++;
        name = g_findNext(fm);
    }
    g_printc(console, 0xFFFFFFFF, "--- %d map(s) found ---\n", count);
}

static ExecFn g_exec_fn = nullptr;
static AddStaticFuncFn g_add_static_func = nullptr;

static DWORD WINAPI hotkey_thread(LPVOID)
{
    xlog::info("Hotkey thread started (save=0x{:02X}, load=0x{:02X})",
               g_quicksave_vk, g_quickload_vk);

    bool initialized = false;
    bool save_was_down = false;
    bool load_was_down = false;

    while (true) {
        Sleep(50);

        void* console = *g_pp_console;
        if (!console)
            continue;

        if (!initialized) {
            initialized = true;
        }

        // Quick save
        bool save_down = (GetAsyncKeyState(g_quicksave_vk) & 0x8000) != 0;
        if (save_down && !save_was_down) {
            char cmd[] = "sv_save quicksave";
            g_exec_fn(console, cmd, 0);
            xlog::info("Quick save triggered");
        }
        save_was_down = save_down;

        // Quick load
        bool load_down = (GetAsyncKeyState(g_quickload_vk) & 0x8000) != 0;
        if (load_down && !load_was_down) {
            char cmd[] = "closegamemenu;sv_load quicksave";
            g_exec_fn(console, cmd, 0);
            xlog::info("Quick load triggered");
        }
        load_was_down = load_down;
    }
}

// Hook for FUN_00402220 — launches Engine.exe.
static FunHook<void()> launch_engine_hook{0x00402220, []() {
    std::string game_dir(install_path_buf);

    // Write windowed mode setting to the game's console.cfg
    write_windowed_config(game_dir);

    // Build command line: {path}\Bin\engine.exe +mod MW [-funlabs]
    // Note: -nointro is NOT passed here. When going through MostWanted.exe,
    // the DLL is injected into a running Engine.exe process. Skipping intros
    // makes the engine initialize too fast, causing injection timing issues.
    // Use "Skip game launcher" + "Skip intros" together for that feature.
    std::string cmd = game_dir + "\\Bin\\engine.exe +mod MW -funlabs";
    if (g_enable_cheats)
        cmd += " -cheats";

    std::string work_dir = game_dir + "\\bin";

    xlog::info("Launching: {} (cwd: {})", cmd, work_dir);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        0, nullptr, work_dir.c_str(), &si, &pi)) {
        xlog::warn("Failed to launch Engine.exe (error {})", GetLastError());
        return;
    }

    // Wait for Engine.exe to finish loading its DLLs
    WaitForInputIdle(pi.hProcess, 10000);

    // Inject GamePatch.dll into Engine.exe for CD bypass, hotkeys, and listmaps.
    // The DLL's apply_engine_patches() handles everything from inside the process.
    inject_dll_into_process(pi.hProcess);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    PostQuitMessage(1);
}};

void load_misc_config(const std::string& ini_path)
{
    g_window_mode = GetPrivateProfileIntA("module.us_mostwanted", "WindowMode", 0, ini_path.c_str());
    g_skip_intros = GetPrivateProfileIntA("module.us_mostwanted", "SkipIntros", 0, ini_path.c_str()) != 0;
    g_enable_cheats = GetPrivateProfileIntA("module.us_mostwanted", "EnableCheats", 0, ini_path.c_str()) != 0;

    char key_buf[32];
    GetPrivateProfileStringA("module.us_mostwanted", "QuickSaveKey", "F5", key_buf, sizeof(key_buf), ini_path.c_str());
    g_quicksave_vk = key_name_to_vk(key_buf);
    if (g_quicksave_vk == 0) g_quicksave_vk = VK_F5;

    GetPrivateProfileStringA("module.us_mostwanted", "QuickLoadKey", "F8", key_buf, sizeof(key_buf), ini_path.c_str());
    g_quickload_vk = key_name_to_vk(key_buf);
    if (g_quickload_vk == 0) g_quickload_vk = VK_F8;

    xlog::info("  WindowMode: {}", g_window_mode);
    xlog::info("  SkipIntros: {}", g_skip_intros);
    xlog::info("  QuickSave: 0x{:02X}, QuickLoad: 0x{:02X}", g_quicksave_vk, g_quickload_vk);
}

void apply_engine_patches()
{
    HMODULE engine_dll = GetModuleHandleA("EngineDll.dll");
    if (!engine_dll) {
        xlog::warn("Engine patches: EngineDll.dll not found");
        return;
    }

    // Bypass CD check: patch CheckTracks to always return 1
    auto check_tracks = reinterpret_cast<uint8_t*>(
        GetProcAddress(engine_dll, "?CheckTracks@@YAHXZ"));
    if (check_tracks) {
        DWORD old_protect;
        VirtualProtect(check_tracks, 6, PAGE_EXECUTE_READWRITE, &old_protect);
        check_tracks[0] = 0xB8; // mov eax, 1
        check_tracks[1] = 0x01;
        check_tracks[2] = 0x00;
        check_tracks[3] = 0x00;
        check_tracks[4] = 0x00;
        check_tracks[5] = 0xC3; // ret
        VirtualProtect(check_tracks, 6, old_protect, &old_protect);
        xlog::info("CD check bypassed (in-process)");
    }

    // Resolve exports
    g_pp_console = reinterpret_cast<void**>(
        GetProcAddress(engine_dll, "?console@@3PAVcConsole@@A"));
    g_pp_fileManager = reinterpret_cast<void**>(
        GetProcAddress(engine_dll, "?fileManager@@3PAVcFileManager@@A"));
    g_exec_fn = reinterpret_cast<ExecFn>(
        GetProcAddress(engine_dll, "?Exec@cConsole@@QAEHPADH@Z"));
    g_printc = reinterpret_cast<PrintcFn>(
        GetProcAddress(engine_dll, "?Printc@cConsole@@QAAHIPADZZ"));
    g_findFirst = reinterpret_cast<FindFirstFn>(
        GetProcAddress(engine_dll, "?FindFirst@cFileManager@@QAEPADPAD@Z"));
    g_findNext = reinterpret_cast<FindNextFn>(
        GetProcAddress(engine_dll, "?FindNext@cFileManager@@QAEPADXZ"));

    g_add_static_func = reinterpret_cast<AddStaticFuncFn>(
        GetProcAddress(engine_dll, "?AddStaticFunc@cConsole@@QAAPAVcConsoleFunc@@PADP6AXXZZZ"));

    if (!g_pp_console || !g_exec_fn) {
        xlog::warn("Engine patches: could not resolve console exports");
        return;
    }

    // Start the hotkey polling thread for quicksave/quickload
    CreateThread(nullptr, 0, hotkey_thread, nullptr, 0, nullptr);
}

void apply_misc_patches()
{
    // Fix 1: Patch the button state refresh function (0x00401550) to always enable buttons.
    //
    // Original logic at 0x401550:
    //   - Reads registry to check if game is "installed"
    //   - Disables buttons when registry key is missing
    //
    // After the prologue (sub esp, push esi, mov esi ecx) at 0x401559,
    // we insert a short jmp to 0x4015CF which always enables the button.
    // This skips the registry check entirely.
    AsmWriter(0x00401559, 0x0040155B).jmp(0x004015CF);
    xlog::info("Launcher button registry check bypassed");

    // Fix 2: Hook the install path resolver so the game can find its own files
    // without depending on the original installer's registry entries.
    get_install_path_hook.install();

    // Fix 3: Hook the Engine.exe launch function to apply window mode,
    // skip intros, and bypass CD protection.
    launch_engine_hook.install();
}
