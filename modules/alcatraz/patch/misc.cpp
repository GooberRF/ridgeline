#include "misc.h"
#include "rendering.h"
#include <xlog/xlog.h>
#include <windows.h>
#include <d3d9.h>
#include <patch_runtime/MemUtils.h>
#include <patch_runtime/FunHook.h>
#include <patch_runtime/CodeInjection.h>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <cstring>

bool g_skip_launcher = false;
bool g_fast_loading = false;
bool g_developer_console = false;
int g_texture_quality = 0;

// --- Texture quality reduction with DDS mip-skip + SEQUENTIAL_SCAN ---
// For reduced texture quality, we avoid reading full-resolution mip data from disk:
//   - DDS files with mipmaps: skip directly to smaller mip levels in the file (huge I/O win)
//   - Other files: read with FILE_FLAG_SEQUENTIAL_SCAN for better OS prefetching
//   - Fallback: original D3DXCreateTextureFromFileExA with scaled dimensions

struct D3DXImageInfo {
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT MipLevels;
    int Format;
    int ResourceType;
    int ImageFileFormat;
};

// DDS file format structures
static constexpr DWORD DDS_MAGIC = 0x20534444; // "DDS "
static constexpr DWORD DDPF_FOURCC = 0x4;

struct DdsPixelFormat {
    DWORD dwSize, dwFlags, dwFourCC, dwRGBBitCount;
    DWORD dwRBitMask, dwGBitMask, dwBBitMask, dwABitMask;
};

struct DdsHeader {
    DWORD dwSize, dwFlags, dwHeight, dwWidth;
    DWORD dwPitchOrLinearSize, dwDepth, dwMipMapCount;
    DWORD dwReserved1[11];
    DdsPixelFormat ddspf;
    DWORD dwCaps, dwCaps2, dwCaps3, dwCaps4, dwReserved2;
};

using D3DXGetImageInfoFromFileA_t = HRESULT(__stdcall*)(LPCSTR, D3DXImageInfo*);
using D3DXGetImageInfoFromMemory_t = HRESULT(__stdcall*)(LPCVOID, UINT, D3DXImageInfo*);
using D3DXCreateTexFromMemEx_t = HRESULT(__stdcall*)(
    IDirect3DDevice9*, LPCVOID, UINT,
    UINT, UINT, UINT, DWORD, int, int,
    DWORD, DWORD, DWORD, void*, void*, IDirect3DTexture9**);

static D3DXGetImageInfoFromFileA_t pfn_GetImageInfo = nullptr;
static D3DXGetImageInfoFromMemory_t pfn_GetImageInfoMem = nullptr;
static D3DXCreateTexFromMemEx_t pfn_CreateTexFromMemEx = nullptr;

static int get_mip_skip_levels() {
    switch (g_texture_quality) {
        case 1: return 1;  // Medium: skip 1 level (0.5x)
        case 2: return 2;  // Low: skip 2 levels (0.25x)
        case 3: return 3;  // Very Low: skip 3 levels (0.125x)
        default: return 0;
    }
}

static float get_texture_scale() {
    switch (g_texture_quality) {
        case 1: return 0.5f;
        case 2: return 0.25f;
        case 3: return 0.125f;
        default: return 1.0f;
    }
}

// Calculate byte size of a single DDS mip level for block-compressed formats
static DWORD dds_mip_size(DWORD w, DWORD h, DWORD block_size) {
    DWORD bw = (w + 3) / 4;
    DWORD bh = (h + 3) / 4;
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;
    return bw * bh * block_size;
}

// Calculate byte size of a single mip level for uncompressed formats
static DWORD dds_mip_size_uncompressed(DWORD w, DWORD h, DWORD bits_per_pixel) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    return w * h * bits_per_pixel / 8;
}

// Get block size for FourCC compressed formats. Returns 0 if unknown.
static DWORD get_block_size(DWORD fourcc) {
    // DXT1 = 8 bytes/block, DXT2-5 = 16 bytes/block
    switch (fourcc) {
        case '1TXD': return 8;   // "DXT1"
        case '2TXD': return 16;  // "DXT2"
        case '3TXD': return 16;  // "DXT3"
        case '4TXD': return 16;  // "DXT4"
        case '5TXD': return 16;  // "DXT5"
        default: return 0;
    }
}

// Load a DDS file, skipping high-res mip levels to reduce I/O.
// Reads only the DDS header + the smaller mip levels, constructs a new
// DDS blob in memory with updated header dimensions.
// Returns true on success, fills out_data with the modified DDS.
static bool load_dds_skip_mips(const char* filename, int skip_levels, std::vector<uint8_t>& out_data) {
    HANDLE hfile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hfile == INVALID_HANDLE_VALUE)
        return false;

    // Read magic + header (128 bytes total)
    DWORD magic;
    DdsHeader hdr;
    DWORD bytes_read;
    if (!ReadFile(hfile, &magic, 4, &bytes_read, nullptr) || bytes_read != 4 || magic != DDS_MAGIC) {
        CloseHandle(hfile);
        return false;
    }
    if (!ReadFile(hfile, &hdr, sizeof(hdr), &bytes_read, nullptr) || bytes_read != sizeof(hdr)) {
        CloseHandle(hfile);
        return false;
    }

    // Only handle files with mipmaps
    if (hdr.dwMipMapCount <= 1) {
        CloseHandle(hfile);
        return false;
    }

    // Determine format and per-mip size calculation
    bool is_compressed = (hdr.ddspf.dwFlags & DDPF_FOURCC) != 0;
    DWORD block_size = 0;
    DWORD bits_per_pixel = 0;

    if (is_compressed) {
        block_size = get_block_size(hdr.ddspf.dwFourCC);
        if (block_size == 0) {
            CloseHandle(hfile);
            return false;
        }
    } else {
        bits_per_pixel = hdr.ddspf.dwRGBBitCount;
        if (bits_per_pixel == 0 || bits_per_pixel % 8 != 0) {
            CloseHandle(hfile);
            return false;
        }
    }

    // Don't skip more mips than available (keep at least 1)
    int actual_skip = std::min(skip_levels, static_cast<int>(hdr.dwMipMapCount) - 1);
    if (actual_skip <= 0) {
        CloseHandle(hfile);
        return false;
    }

    // Calculate byte offset to skip past the high-res mip levels
    DWORD skip_bytes = 0;
    DWORD w = hdr.dwWidth;
    DWORD h = hdr.dwHeight;
    for (int i = 0; i < actual_skip; i++) {
        if (is_compressed)
            skip_bytes += dds_mip_size(w, h, block_size);
        else
            skip_bytes += dds_mip_size_uncompressed(w, h, bits_per_pixel);
        w = (w > 1) ? w / 2 : 1;
        h = (h > 1) ? h / 2 : 1;
    }

    // Calculate remaining data size (all remaining mip levels)
    DWORD remaining_bytes = 0;
    DWORD rw = w, rh = h;
    int remaining_mips = static_cast<int>(hdr.dwMipMapCount) - actual_skip;
    for (int i = 0; i < remaining_mips; i++) {
        if (is_compressed)
            remaining_bytes += dds_mip_size(rw, rh, block_size);
        else
            remaining_bytes += dds_mip_size_uncompressed(rw, rh, bits_per_pixel);
        rw = (rw > 1) ? rw / 2 : 1;
        rh = (rh > 1) ? rh / 2 : 1;
    }

    // Seek past the skipped mip data
    LARGE_INTEGER seek_dist;
    seek_dist.QuadPart = static_cast<LONGLONG>(skip_bytes);
    if (!SetFilePointerEx(hfile, seek_dist, nullptr, FILE_CURRENT)) {
        CloseHandle(hfile);
        return false;
    }

    // Build output: magic (4) + modified header (124) + remaining mip data
    DWORD header_size = 4 + sizeof(DdsHeader);
    out_data.resize(header_size + remaining_bytes);

    // Write magic
    std::memcpy(out_data.data(), &magic, 4);

    // Write modified header with new dimensions and mip count
    DdsHeader new_hdr = hdr;
    new_hdr.dwWidth = w;
    new_hdr.dwHeight = h;
    new_hdr.dwMipMapCount = static_cast<DWORD>(remaining_mips);
    if (is_compressed)
        new_hdr.dwPitchOrLinearSize = dds_mip_size(w, h, block_size);
    else
        new_hdr.dwPitchOrLinearSize = w * bits_per_pixel / 8;
    std::memcpy(out_data.data() + 4, &new_hdr, sizeof(DdsHeader));

    // Read remaining mip data
    if (!ReadFile(hfile, out_data.data() + header_size, remaining_bytes, &bytes_read, nullptr)
        || bytes_read != remaining_bytes) {
        CloseHandle(hfile);
        return false;
    }

    CloseHandle(hfile);
    return true;
}

// Read an entire file into memory using FILE_FLAG_SEQUENTIAL_SCAN
static bool read_file_sequential(const char* filename, std::vector<uint8_t>& out_data) {
    HANDLE hfile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hfile == INVALID_HANDLE_VALUE)
        return false;

    DWORD file_size = GetFileSize(hfile, nullptr);
    if (file_size == INVALID_FILE_SIZE || file_size == 0) {
        CloseHandle(hfile);
        return false;
    }

    out_data.resize(file_size);
    DWORD bytes_read;
    if (!ReadFile(hfile, out_data.data(), file_size, &bytes_read, nullptr) || bytes_read != file_size) {
        CloseHandle(hfile);
        return false;
    }

    CloseHandle(hfile);
    return true;
}

// --- Background texture file prefetching ---
// When a texture is loaded, sibling texture files in the same directory are
// prefetched on a background thread. This overlaps disk I/O with GPU processing,
// so the next texture from the same directory is already in memory.

static CRITICAL_SECTION g_prefetch_cs;
static bool g_prefetch_init_done = false;
static std::unordered_map<std::string, std::vector<uint8_t>> g_prefetch_cache;
static std::vector<std::string> g_prefetch_queue;
static HANDLE g_prefetch_event = nullptr;
static HANDLE g_prefetch_thread_handle = nullptr;
static volatile LONG g_prefetch_shutdown = 0;
static std::unordered_set<std::string> g_prefetch_scanned_dirs;

static std::string normalize_path(const char* path) {
    std::string s(path);
    for (auto& c : s) {
        if (c == '/') c = '\\';
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

static DWORD WINAPI prefetch_worker(LPVOID) {
    while (!g_prefetch_shutdown) {
        WaitForSingleObject(g_prefetch_event, INFINITE);
        if (g_prefetch_shutdown) break;

        while (true) {
            std::string filename;
            EnterCriticalSection(&g_prefetch_cs);
            if (g_prefetch_queue.empty()) {
                ResetEvent(g_prefetch_event);
                LeaveCriticalSection(&g_prefetch_cs);
                break;
            }
            filename = std::move(g_prefetch_queue.back());
            g_prefetch_queue.pop_back();
            bool already = g_prefetch_cache.count(filename) > 0;
            LeaveCriticalSection(&g_prefetch_cs);

            if (already) continue;

            std::vector<uint8_t> data;
            if (read_file_sequential(filename.c_str(), data)) {
                EnterCriticalSection(&g_prefetch_cs);
                if (!g_prefetch_cache.count(filename))
                    g_prefetch_cache[filename] = std::move(data);
                LeaveCriticalSection(&g_prefetch_cs);
            }
        }
    }
    return 0;
}

static bool try_get_prefetched(const std::string& key, std::vector<uint8_t>& out) {
    if (!g_prefetch_init_done) return false;
    EnterCriticalSection(&g_prefetch_cs);
    auto it = g_prefetch_cache.find(key);
    if (it != g_prefetch_cache.end()) {
        out = std::move(it->second);
        g_prefetch_cache.erase(it);
        LeaveCriticalSection(&g_prefetch_cs);
        return true;
    }
    LeaveCriticalSection(&g_prefetch_cs);
    return false;
}

static void queue_directory_prefetch(const char* loaded_file) {
    if (!g_prefetch_init_done) return;

    std::string norm = normalize_path(loaded_file);
    auto sep = norm.find_last_of('\\');
    std::string dir = (sep != std::string::npos) ? norm.substr(0, sep) : ".";

    EnterCriticalSection(&g_prefetch_cs);
    if (!g_prefetch_scanned_dirs.insert(dir).second) {
        LeaveCriticalSection(&g_prefetch_cs);
        return;
    }
    LeaveCriticalSection(&g_prefetch_cs);

    // Use original path separators for FindFirstFile
    std::string orig_path(loaded_file);
    for (auto& c : orig_path) if (c == '/') c = '\\';
    auto orig_sep = orig_path.find_last_of('\\');
    std::string orig_dir = (orig_sep != std::string::npos) ? orig_path.substr(0, orig_sep) : ".";

    WIN32_FIND_DATAA fd;
    std::string pattern = orig_dir + "\\*";
    HANDLE hfind = FindFirstFileA(pattern.c_str(), &fd);
    if (hfind == INVALID_HANDLE_VALUE) return;

    int queued = 0;
    EnterCriticalSection(&g_prefetch_cs);
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        const char* ext = strrchr(fd.cFileName, '.');
        if (!ext) continue;
        if (_stricmp(ext, ".dds") != 0 && _stricmp(ext, ".tga") != 0 &&
            _stricmp(ext, ".bmp") != 0 && _stricmp(ext, ".png") != 0) continue;

        std::string full = normalize_path((orig_dir + "\\" + fd.cFileName).c_str());
        if (!g_prefetch_cache.count(full)) {
            g_prefetch_queue.push_back(std::move(full));
            queued++;
        }
    } while (FindNextFileA(hfind, &fd));
    FindClose(hfind);

    if (queued > 0)
        SetEvent(g_prefetch_event);
    LeaveCriticalSection(&g_prefetch_cs);
}

static void prefetch_start() {
    InitializeCriticalSection(&g_prefetch_cs);
    g_prefetch_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    g_prefetch_thread_handle = CreateThread(nullptr, 0, prefetch_worker, nullptr, 0, nullptr);
    if (g_prefetch_thread_handle)
        SetThreadPriority(g_prefetch_thread_handle, THREAD_PRIORITY_BELOW_NORMAL);
    g_prefetch_init_done = true;
}

// In-memory version of load_dds_skip_mips — works on already-loaded file data.
// Extracts smaller mip levels without any disk I/O.
static bool skip_mips_from_memory(const std::vector<uint8_t>& file_data, int skip_levels,
                                   std::vector<uint8_t>& out_data) {
    if (file_data.size() < 4 + sizeof(DdsHeader))
        return false;

    DWORD magic;
    std::memcpy(&magic, file_data.data(), 4);
    if (magic != DDS_MAGIC) return false;

    DdsHeader hdr;
    std::memcpy(&hdr, file_data.data() + 4, sizeof(DdsHeader));

    if (hdr.dwMipMapCount <= 1)
        return false;

    bool is_compressed = (hdr.ddspf.dwFlags & DDPF_FOURCC) != 0;
    DWORD block_size = 0, bits_per_pixel = 0;

    if (is_compressed) {
        block_size = get_block_size(hdr.ddspf.dwFourCC);
        if (block_size == 0) return false;
    } else {
        bits_per_pixel = hdr.ddspf.dwRGBBitCount;
        if (bits_per_pixel == 0 || bits_per_pixel % 8 != 0) return false;
    }

    int actual_skip = std::min(skip_levels, static_cast<int>(hdr.dwMipMapCount) - 1);
    if (actual_skip <= 0) return false;

    DWORD skip_bytes = 0;
    DWORD w = hdr.dwWidth, h = hdr.dwHeight;
    for (int i = 0; i < actual_skip; i++) {
        skip_bytes += is_compressed ? dds_mip_size(w, h, block_size)
                                    : dds_mip_size_uncompressed(w, h, bits_per_pixel);
        w = (w > 1) ? w / 2 : 1;
        h = (h > 1) ? h / 2 : 1;
    }

    DWORD remaining_bytes = 0;
    DWORD rw = w, rh = h;
    int remaining_mips = static_cast<int>(hdr.dwMipMapCount) - actual_skip;
    for (int i = 0; i < remaining_mips; i++) {
        remaining_bytes += is_compressed ? dds_mip_size(rw, rh, block_size)
                                         : dds_mip_size_uncompressed(rw, rh, bits_per_pixel);
        rw = (rw > 1) ? rw / 2 : 1;
        rh = (rh > 1) ? rh / 2 : 1;
    }

    DWORD data_offset = 4 + sizeof(DdsHeader) + skip_bytes;
    if (data_offset + remaining_bytes > file_data.size())
        return false;

    DWORD header_size = 4 + sizeof(DdsHeader);
    out_data.resize(header_size + remaining_bytes);
    std::memcpy(out_data.data(), &magic, 4);

    DdsHeader new_hdr = hdr;
    new_hdr.dwWidth = w;
    new_hdr.dwHeight = h;
    new_hdr.dwMipMapCount = static_cast<DWORD>(remaining_mips);
    new_hdr.dwPitchOrLinearSize = is_compressed ? dds_mip_size(w, h, block_size)
                                                : w * bits_per_pixel / 8;
    std::memcpy(out_data.data() + 4, &new_hdr, sizeof(DdsHeader));
    std::memcpy(out_data.data() + header_size, file_data.data() + data_offset, remaining_bytes);

    return true;
}

static HRESULT __stdcall tex_load_ex_hook_fn(
    IDirect3DDevice9* dev, LPCSTR file,
    UINT width, UINT height, UINT mip_levels,
    DWORD usage, int format, int pool,
    DWORD filter, DWORD mip_filter, DWORD color_key,
    void* src_info, void* palette, IDirect3DTexture9** out);

static FunHook<HRESULT __stdcall(
    IDirect3DDevice9*, LPCSTR,
    UINT, UINT, UINT,
    DWORD, int, int,
    DWORD, DWORD, DWORD,
    void*, void*, IDirect3DTexture9**)>
    tex_load_ex_hook{uintptr_t(0), tex_load_ex_hook_fn};

static HRESULT __stdcall tex_load_ex_hook_fn(
    IDirect3DDevice9* dev, LPCSTR file,
    UINT width, UINT height, UINT mip_levels,
    DWORD usage, int format, int pool,
    DWORD filter, DWORD mip_filter, DWORD color_key,
    void* src_info, void* palette, IDirect3DTexture9** out)
{
    // No optimizations active or missing D3DX functions -> original path
    if ((!g_fast_loading && g_texture_quality == 0) || !pfn_CreateTexFromMemEx || !file) {
        return tex_load_ex_hook.call_target(dev, file, width, height, mip_levels,
                                             usage, format, pool, filter, mip_filter,
                                             color_key, src_info, palette, out);
    }

    bool do_scale = (g_texture_quality > 0);

    // Check prefetch cache first
    std::string norm = normalize_path(file);
    std::vector<uint8_t> file_data;
    bool from_cache = try_get_prefetched(norm, file_data);

    if (from_cache) {
        // Try DDS mip-skip from cached data (zero I/O)
        if (do_scale) {
            int skip = get_mip_skip_levels();
            if (skip > 0) {
                std::vector<uint8_t> mip_data;
                if (skip_mips_from_memory(file_data, skip, mip_data)) {
                    return pfn_CreateTexFromMemEx(dev, mip_data.data(), static_cast<UINT>(mip_data.size()),
                                                  0, 0, mip_levels, usage, format, pool,
                                                  filter, mip_filter, color_key, src_info, palette, out);
                }
            }
        }

        // Use full cached data, scale dimensions if quality reduction is active
        UINT req_w = width, req_h = height;
        if (do_scale && req_w == 0 && req_h == 0 && pfn_GetImageInfoMem) {
            D3DXImageInfo info{};
            if (SUCCEEDED(pfn_GetImageInfoMem(file_data.data(),
                          static_cast<UINT>(file_data.size()), &info))) {
                float scale = get_texture_scale();
                req_w = std::max(1u, static_cast<UINT>(info.Width * scale));
                req_h = std::max(1u, static_cast<UINT>(info.Height * scale));
            }
        }
        return pfn_CreateTexFromMemEx(dev, file_data.data(), static_cast<UINT>(file_data.size()),
                                      req_w, req_h, mip_levels, usage, format, pool,
                                      filter, mip_filter, color_key, src_info, palette, out);
    }

    // Not cached — queue siblings for prefetch, then load this file ourselves
    queue_directory_prefetch(file);

    // DDS mip-skip from file (partial read — only small mips)
    if (do_scale) {
        int skip = get_mip_skip_levels();
        if (skip > 0) {
            std::vector<uint8_t> dds_data;
            if (load_dds_skip_mips(file, skip, dds_data)) {
                return pfn_CreateTexFromMemEx(dev, dds_data.data(), static_cast<UINT>(dds_data.size()),
                                              0, 0, mip_levels, usage, format, pool,
                                              filter, mip_filter, color_key, src_info, palette, out);
            }
        }
    }

    // Read full file with SEQUENTIAL_SCAN, scale dimensions if quality reduction is active
    if (read_file_sequential(file, file_data)) {
        UINT req_w = width, req_h = height;
        if (do_scale && req_w == 0 && req_h == 0 && pfn_GetImageInfoMem) {
            D3DXImageInfo info{};
            if (SUCCEEDED(pfn_GetImageInfoMem(file_data.data(),
                          static_cast<UINT>(file_data.size()), &info))) {
                float scale = get_texture_scale();
                req_w = std::max(1u, static_cast<UINT>(info.Width * scale));
                req_h = std::max(1u, static_cast<UINT>(info.Height * scale));
            }
        }
        return pfn_CreateTexFromMemEx(dev, file_data.data(), static_cast<UINT>(file_data.size()),
                                      req_w, req_h, mip_levels, usage, format, pool,
                                      filter, mip_filter, color_key, src_info, palette, out);
    }

    // Fallback — original D3DX call with dimension scaling
    if (do_scale && width == 0 && height == 0 && pfn_GetImageInfo) {
        D3DXImageInfo info{};
        if (SUCCEEDED(pfn_GetImageInfo(file, &info))) {
            float scale = get_texture_scale();
            width = std::max(1u, static_cast<UINT>(info.Width * scale));
            height = std::max(1u, static_cast<UINT>(info.Height * scale));
        }
    }
    return tex_load_ex_hook.call_target(dev, file, width, height, mip_levels,
                                         usage, format, pool, filter, mip_filter,
                                         color_key, src_info, palette, out);
}

// The game's launcher dialog wrapper at 0x00402E30.
// Normally shows the resolution picker dialog (resource ID 130).
// When skipped, we set the resolution directly in memory.
// void __cdecl show_launcher_dialog(HWND parent)
static FunHook<void __cdecl(HWND)> launcher_dialog_hook{0x00402E30, [](HWND) {
    int width = g_res_width;
    int height = g_res_height;

    // Fall back to desktop resolution if no custom resolution is set
    if (width <= 0 || height <= 0) {
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
    }

    // Write selected resolution to the game's globals.
    // Note: the game stores height at 0x58ED74 and width at 0x58ED78 (reversed).
    // Verified from add_resolution at 0x4028C0 and dialog proc IDOK at 0x402C52.
    write_mem<int>(0x0058ED74, height);
    write_mem<int>(0x0058ED78, width);

    // Calculate and write aspect ratio values used by the engine's renderer.
    // [0x58ED7C] = width / 1024, [0x58ED80] = height / 768.
    // Verified from dialog proc at 0x00402C66.
    float aspect = static_cast<float>(width) / 1024.0f;
    float inverse = static_cast<float>(height) / 768.0f;
    write_mem<float>(0x0058ED7C, aspect);
    write_mem<float>(0x0058ED80, inverse);

    // Clear the dialog-shown flag so the game proceeds
    write_mem<uint8_t>(0x005890C0, 0);

    xlog::info("Launcher skipped, resolution set to {}x{}", width, height);
    // Return without calling original — dialog is never shown
}};

// --- Developer console re-enable ---
// The game has a full developer console (command handler at 0x0041b210) that was
// disabled for retail — no code ever sets the console-open flag. We hook the
// WndProc key dispatcher (0x00427350) to intercept tilde (VK_OEM_3 = 0xC0) and
// toggle the console open. The existing close handler (tilde in console mode at
// 0x0041d070) already works. Console object is at 0x0085B2F0, open flag at +0x4108.
static int __stdcall key_dispatch_hook_fn(int hwnd, int msg, int wparam, int lparam);
static FunHook<int __stdcall(int, int, int, int)>
    key_dispatch_hook{0x00427350, key_dispatch_hook_fn};

static int __stdcall key_dispatch_hook_fn(int hwnd, int msg, int wparam, int lparam) {
    // WM_KEYDOWN + tilde key + console currently closed → open console
    if (msg == 0x100 && wparam == 0xC0) {
        auto* console_flag = reinterpret_cast<uint8_t*>(0x0085F3F8);
        if (*console_flag == 0) {
            *console_flag = 1;
            return 0;
        }
    }
    return key_dispatch_hook.call_target(hwnd, msg, wparam, lparam);
}

void misc_apply_patches()
{
    if (g_skip_launcher) {
        launcher_dialog_hook.install();
        xlog::info("Game launcher skip enabled");
    }

    if (g_developer_console) {
        key_dispatch_hook.install();
        write_mem<uint8_t>(0x0058ed09, 1); // enable developer mode flag
        xlog::info("Developer console enabled (tilde key to open)");
    }

    // Resolve D3DX functions for texture quality optimization
    HMODULE d3dx = GetModuleHandleA("d3dx9_41.dll");
    if (d3dx) {
        auto addr = reinterpret_cast<uintptr_t>(GetProcAddress(d3dx, "D3DXCreateTextureFromFileExA"));
        if (addr) { tex_load_ex_hook.set_addr(addr); tex_load_ex_hook.install(); }

        pfn_GetImageInfo = reinterpret_cast<D3DXGetImageInfoFromFileA_t>(
            GetProcAddress(d3dx, "D3DXGetImageInfoFromFileA"));
        pfn_GetImageInfoMem = reinterpret_cast<D3DXGetImageInfoFromMemory_t>(
            GetProcAddress(d3dx, "D3DXGetImageInfoFromFileInMemory"));
        pfn_CreateTexFromMemEx = reinterpret_cast<D3DXCreateTexFromMemEx_t>(
            GetProcAddress(d3dx, "D3DXCreateTextureFromFileInMemoryEx"));
    }

    if (g_texture_quality > 0) {
        static const char* quality_names[] = {"High", "Medium (0.5x)", "Low (0.25x)", "Very Low (0.125x)"};
        xlog::info("Texture quality: {} — textures will be downscaled", quality_names[g_texture_quality]);
    }

    if (g_fast_loading)
        xlog::info("Faster level loads enabled — prefetch + sequential I/O active");

    if (g_fast_loading || g_texture_quality > 0)
        prefetch_start();
}
