#include "rendering.h"
#include <xlog/xlog.h>
#include <windows.h>
#include <timeapi.h>
#include <d3d9.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <float.h>
#include <patch_runtime/MemUtils.h>
#include <patch_runtime/CodeInjection.h>
#include <patch_runtime/FunHook.h>

bool g_fps_fix_enabled = false;
int g_res_width = 0;
int g_res_height = 0;
int g_window_mode = 0;

// FunHook<__stdcall(...)> needs a real __stdcall function pointer. MSVC will
// silently convert non-capturing lambdas; MinGW/GCC won't. We forward-declare
// the hook function so the FunHook initializer can take its address, then
// define the body afterwards (so it can refer back to the FunHook for
// call_target). Same shape as the existing patches in misc.cpp.

// --- Sleep ---
static void __stdcall sleep_hook_fn(DWORD ms);
static FunHook<void __stdcall(DWORD)> sleep_hook{
    reinterpret_cast<uintptr_t>(&Sleep), &sleep_hook_fn
};
static void __stdcall sleep_hook_fn(DWORD ms)
{
    if (ms <= 1) {
        SwitchToThread();
        return;
    }
    sleep_hook.call_target(ms);
}

// --- WaitForSingleObject: short-timeout spin instead of timer-tick block ---
static DWORD __stdcall wait_single_hook_fn(HANDLE handle, DWORD timeout);
static FunHook<DWORD __stdcall(HANDLE, DWORD)> wait_single_hook{
    reinterpret_cast<uintptr_t>(&WaitForSingleObject), &wait_single_hook_fn
};
static DWORD __stdcall wait_single_hook_fn(HANDLE handle, DWORD timeout)
{
    if (timeout > 0 && timeout <= 2) {
        LARGE_INTEGER start, now, freq;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
        LONGLONG deadline = start.QuadPart + (freq.QuadPart * timeout / 1000);
        while (true) {
            DWORD result = wait_single_hook.call_target(handle, 0);
            if (result != WAIT_TIMEOUT) return result;
            QueryPerformanceCounter(&now);
            if (now.QuadPart >= deadline) return WAIT_TIMEOUT;
            SwitchToThread();
        }
    }
    return wait_single_hook.call_target(handle, timeout);
}

// --- WaitForMultipleObjects ---
static DWORD __stdcall wait_multi_hook_fn(DWORD count, const HANDLE* handles, BOOL wait_all, DWORD timeout);
static FunHook<DWORD __stdcall(DWORD, const HANDLE*, BOOL, DWORD)> wait_multi_hook{
    reinterpret_cast<uintptr_t>(&WaitForMultipleObjects), &wait_multi_hook_fn
};
static DWORD __stdcall wait_multi_hook_fn(DWORD count, const HANDLE* handles, BOOL wait_all, DWORD timeout)
{
    if (timeout > 0 && timeout <= 2) {
        LARGE_INTEGER start, now, freq;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
        LONGLONG deadline = start.QuadPart + (freq.QuadPart * timeout / 1000);
        while (true) {
            DWORD result = wait_multi_hook.call_target(count, handles, wait_all, 0);
            if (result != WAIT_TIMEOUT) return result;
            QueryPerformanceCounter(&now);
            if (now.QuadPart >= deadline) return WAIT_TIMEOUT;
            SwitchToThread();
        }
    }
    return wait_multi_hook.call_target(count, handles, wait_all, timeout);
}

// Timer gate fix for FUN_004344e0.
//
// Problem: D3D9 sets x87 FPU to 24-bit precision at CreateDevice. The timer gate
// calls FUN_004309d0(4) which returns QPC/QPF (absolute seconds since boot). With
// 24-bit mantissa (~7 decimal digits), after ~3 days of uptime the float32 ULP
// exceeds 31ms, so the 1ms gate threshold can never be resolved → 32 FPS lock.
//
// Fix strategy (two parts):
// 1. Set FPU to 64-bit precision before the QPC/QPF division (CodeInjection at 0x4344e6)
// 2. Subtract a base time captured at game start, so the timer value stays small.
//    Small values (~0-3600s) have plenty of float32 precision (ULP < 0.001ms).
//    This replaces the original FSTP float/FLD float truncation at 0x4344eb with
//    FSUB qword ptr [g_timer_base_time] (6 bytes + 2 NOP = 8 bytes, same footprint).

static double g_timer_base_time = 0.0;
static bool g_timer_base_captured = false;

static CodeInjection timer_fpu_fix{0x004344e6, []() {
    _control87(_PC_64, _MCW_PC);

    // Capture base time on first call so subsequent values are small
    if (!g_timer_base_captured) {
        LARGE_INTEGER qpc, freq;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&qpc);
        g_timer_base_time = static_cast<double>(qpc.QuadPart) / static_cast<double>(freq.QuadPart);
        g_timer_base_captured = true;

        // Reset last_time to 0 so first elapsed is computed relative to base
        addr_as_ref<float>(0x0087cdb4) = 0.0f; // timer_obj (0x87cd90) + 0x24
        xlog::info("Timer base captured: {:.6f}s, last_time reset", g_timer_base_time);
    }
}};

static void apply_fps_fix()
{
    timeBeginPeriod(1);
    sleep_hook.install();
    wait_single_hook.install();
    wait_multi_hook.install();

    // Fix timer gate precision: set FPU to 64-bit before QPC/QPF division,
    // and capture base time on first call
    timer_fpu_fix.install();

    // Replace the FSTP float/FLD float pair at 0x4344eb-0x4344f2 (8 bytes) with:
    //   FSUB qword ptr [&g_timer_base_time]  (DC 25 XX XX XX XX = 6 bytes)
    //   NOP; NOP                              (90 90 = 2 bytes)
    // This subtracts the base time so QPC/QPF becomes relative seconds since game start.
    // The original FSTP/FLD truncation is no longer needed — small values have plenty
    // of float32 precision for the subsequent elapsed time computation.
    auto base_addr = reinterpret_cast<uintptr_t>(&g_timer_base_time);
    write_mem<uint8_t>(0x004344eb, 0xDC);   // FSUB m64real opcode
    write_mem<uint8_t>(0x004344ec, 0x25);   // ModRM: mod=00, reg=/4(sub), rm=101(disp32)
    write_mem<uint32_t>(0x004344ed, static_cast<uint32_t>(base_addr));
    write_mem<uint8_t>(0x004344f1, 0x90);   // NOP
    write_mem<uint8_t>(0x004344f2, 0x90);   // NOP

    xlog::info("FPS fix applied: Sleep/Wait hooks + timer base rebase + FPU precision fix");
}

// Injection at 0x00402AA0 — right after the game's built-in resolution table
// is populated during WM_INITDIALOG in the launcher dialog proc.
// Original instruction: add esp, 0x38 (cleanup from 14 push pairs)
static CodeInjection custom_resolution_injection{0x00402AA0, []() {
    if (g_res_width > 0 && g_res_height > 0) {
        AddrCaller{0x004028C0}.c_call<void>(g_res_width, g_res_height);
        xlog::info("Custom resolution registered: {}x{}", g_res_width, g_res_height);
    }
}};

// Injection at 0x00427968 — inside D3D device creation, before CreateDevice.
// Override D3DPRESENT_PARAMETERS for windowed mode.
// Original instruction: mov edx, [0058ED74] (6 bytes)
static CodeInjection windowed_present_params_injection{0x00427968, [](auto& regs) {
    if (g_window_mode != 0) {
        uintptr_t pp_base = static_cast<uint32_t>(regs.esp) + 0x1C;
        int w = addr_as_ref<int>(0x0058ED78);
        int h = addr_as_ref<int>(0x0058ED74);
        if (w > 0 && h > 0) {
            addr_as_ref<int32_t>(pp_base + 0x00) = w;
            addr_as_ref<int32_t>(pp_base + 0x04) = h;
        }
        addr_as_ref<int32_t>(pp_base + 0x20) = 1; // Windowed = TRUE
        addr_as_ref<int32_t>(pp_base + 0x30) = 0; // RefreshRate = 0 for windowed
        xlog::info("D3D present params: Windowed=TRUE, back buffer {}x{}", w, h);
    }
}};

// Injection at 0x00427BB4 — before Reset with second D3DPRESENT_PARAMETERS.
// Original instructions: mov ecx,[eax]; push edx; push eax; mov eax,[ecx+0x40] (7 bytes)
static CodeInjection windowed_reset_params_injection{0x00427BB4, [](auto& regs) {
    if (g_window_mode != 0) {
        uintptr_t pp = static_cast<uint32_t>(regs.edx);
        int w = addr_as_ref<int>(0x0058ED78);
        int h = addr_as_ref<int>(0x0058ED74);
        if (w > 0 && h > 0) {
            addr_as_ref<int32_t>(pp + 0x00) = w;
            addr_as_ref<int32_t>(pp + 0x04) = h;
        }
        addr_as_ref<int32_t>(pp + 0x20) = 1;
        addr_as_ref<int32_t>(pp + 0x30) = 0;
        xlog::info("D3D Reset present params: Windowed=TRUE, back buffer {}x{}", w, h);
    }
}};

// Patch DirectInput cooperative level for windowed mode.
static void apply_dinput_coop_fix()
{
    write_mem<uint8_t>(0x00418CE7, 0x06); // DISCL_FOREGROUND|DISCL_NONEXCLUSIVE
    xlog::info("DirectInput cooperative level patched to FOREGROUND|NONEXCLUSIVE");
}

// Re-acquire keyboard device before GetDeviceState to handle focus loss.
// Original instructions at 0x00418EAD: mov ecx,[edi]; mov eax,[ecx+0x24] (5 bytes)
static CodeInjection keyboard_reacquire_injection{0x00418EAD, [](auto& regs) {
    auto* device = reinterpret_cast<IDirectInputDevice8A*>(static_cast<uint32_t>(regs.edi));
    if (device) {
        device->Acquire();
    }
}};

// Fix widescreen aspect ratio: M[0][0] = M[1][1] / aspect
// Original instruction at 0x40F854: mov eax, [0x00868B94] (5 bytes)
static CodeInjection aspect_ratio_fix{0x0040F854, [](auto& regs) {
    int w = addr_as_ref<int>(0x0058ED78);
    int h = addr_as_ref<int>(0x0058ED74);
    if (w > 0 && h > 0) {
        uintptr_t matrix_base = static_cast<uint32_t>(regs.esi) + 0x3E4;
        float yScale = addr_as_ref<float>(matrix_base + 0x14);
        float aspect = static_cast<float>(w) / static_cast<float>(h);
        addr_as_ref<float>(matrix_base + 0x00) = yScale / aspect;
    }
}};

// Post-D3D init: re-apply timeBeginPeriod and log D3D state.
// Injection at 0x00427BE6 — right after Reset succeeds.
static CodeInjection post_d3d_init_injection{0x00427BE6, []() {
    if (g_fps_fix_enabled) {
        timeBeginPeriod(1);
    }

    auto* device = addr_as_ref<IDirect3DDevice9*>(0x00868B94);
    if (device) {
        IDirect3DSurface9* back_buffer = nullptr;
        if (SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back_buffer)) && back_buffer) {
            D3DSURFACE_DESC desc{};
            back_buffer->GetDesc(&desc);
            xlog::info("Back buffer: {}x{}, format={}", desc.Width, desc.Height, static_cast<int>(desc.Format));
            back_buffer->Release();
        }
        D3DVIEWPORT9 vp{};
        device->GetViewport(&vp);
        xlog::info("Viewport: x={} y={} w={} h={}", vp.X, vp.Y, vp.Width, vp.Height);
    }
    int gw = addr_as_ref<int>(0x0058ED78);
    int gh = addr_as_ref<int>(0x0058ED74);
    xlog::info("Game resolution: {}x{}", gw, gh);
}};

// --- CreateWindowExA: force windowed / borderless styles ---
static HWND __stdcall create_window_hook_fn(
    DWORD ex_style, LPCSTR class_name, LPCSTR window_name, DWORD style,
    int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param);

static FunHook<HWND __stdcall(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID)>
create_window_hook{
    reinterpret_cast<uintptr_t>(&CreateWindowExA), &create_window_hook_fn
};

static HWND __stdcall create_window_hook_fn(
    DWORD ex_style, LPCSTR class_name, LPCSTR window_name, DWORD style,
    int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param)
{
    if (g_window_mode != 0 && parent == nullptr && (style & WS_VISIBLE)) {
        int cw = g_res_width;
        int ch = g_res_height;
        if (cw <= 0 || ch <= 0) {
            cw = addr_as_ref<int>(0x0058ED78);
            ch = addr_as_ref<int>(0x0058ED74);
        }
        if (cw <= 0 || ch <= 0) {
            cw = GetSystemMetrics(SM_CXSCREEN);
            ch = GetSystemMetrics(SM_CYSCREEN);
        }

        if (g_window_mode == 1) {
            style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
            RECT rc = {0, 0, cw, ch};
            AdjustWindowRectEx(&rc, style, FALSE, ex_style);
            width = rc.right - rc.left;
            height = rc.bottom - rc.top;
            x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
            y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
            xlog::info("Windowed: {}x{} client, window at {},{}", cw, ch, x, y);
        } else {
            style = WS_POPUP | WS_VISIBLE;
            ex_style &= ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME);
            width = cw;
            height = ch;
            x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
            y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
            xlog::info("Borderless: {}x{} at {},{}", width, height, x, y);
        }
    }
    return create_window_hook.call_target(ex_style, class_name, window_name, style,
                                           x, y, width, height, parent, menu, instance, param);
}

void rendering_apply_patches()
{
    if (g_fps_fix_enabled) {
        apply_fps_fix();
    }

    if (g_res_width > 0 && g_res_height > 0) {
        custom_resolution_injection.install();
    }

    post_d3d_init_injection.install();

    if (g_window_mode != 0) {
        windowed_present_params_injection.install();
        windowed_reset_params_injection.install();
        apply_dinput_coop_fix();
        keyboard_reacquire_injection.install();
        create_window_hook.install();
    }

    aspect_ratio_fix.install();
}
