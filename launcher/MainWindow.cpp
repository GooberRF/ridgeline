#include "MainWindow.h"

#include <common/config/IniFile.h>
#include <common/version/version.h>
#include <ridgeline/Module.h>
#include <xlog/xlog.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <array>
#include <span>
#include <string>

namespace ridgeline_launcher {

namespace {

constexpr int ID_LISTBOX      = 100;
constexpr int ID_EXE_EDIT     = 101;
constexpr int ID_BROWSE       = 102;
constexpr int ID_LAUNCH       = 103;
constexpr int ID_HELP         = 104;
constexpr int ID_EXIT         = 105;

constexpr int kListboxWidth   = 240;
constexpr int kPaddingOuter   = 12;
constexpr int kPaddingInner   = 12;
constexpr int kButtonHeight   = 30;
constexpr int kRowHeight      = 26;

constexpr int kCardItemHeight = 52;

constexpr int kInitialWidth   = 980;
constexpr int kInitialHeight  = 660;

HFONT g_ui_font = nullptr;
HFONT g_title_font = nullptr;

void apply_ui_font(HWND hwnd)
{
    if (g_ui_font) SendMessageA(hwnd, WM_SETFONT, (WPARAM)g_ui_font, TRUE);
}

// ---------------------------------------------------------------------------
// Scrollable container window class. Hosts a stack of child controls that
// may exceed the visible height; WS_VSCROLL + SCROLLINFO + ScrollWindow give
// us a smooth scrollable region. The launcher uses one of these per right-
// pane build; the schema panel + always-on bullets are made children of it.

constexpr const char* kScrollPanelClass = "RidgelineScrollPanel";

// Per-window data we stash via SetWindowLongPtr(GWLP_USERDATA): the total
// content height so on_size can recompute the scroll page correctly when the
// container is resized.
struct ScrollPanelData {
    int content_height = 0;
};

void apply_scroll_geometry(HWND hwnd)
{
    auto* data = reinterpret_cast<ScrollPanelData*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!data) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int visible = rc.bottom - rc.top;

    SCROLLINFO si{sizeof(si)};
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = (data->content_height > 0) ? data->content_height - 1 : 0;
    si.nPage = visible;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

    // Clamp scroll position if the resize made the previous offset invalid.
    si.fMask = SIF_POS;
    GetScrollInfo(hwnd, SB_VERT, &si);
    int max_scroll = std::max(0, data->content_height - visible);
    if (si.nPos > max_scroll) {
        int dy = si.nPos - max_scroll;
        si.nPos = max_scroll;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        ScrollWindow(hwnd, 0, dy, nullptr, nullptr);
        UpdateWindow(hwnd);
    }
}

LRESULT CALLBACK scroll_panel_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_NCCREATE: {
        auto* data = new ScrollPanelData{};
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)data);
        break;
    }
    case WM_NCDESTROY: {
        auto* data = reinterpret_cast<ScrollPanelData*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        delete data;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        break;
    }
    case WM_SIZE:
        apply_scroll_geometry(hwnd);
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO si{sizeof(si)};
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        int old_pos = si.nPos;
        int new_pos = old_pos;
        switch (LOWORD(wparam)) {
        case SB_LINEUP:        new_pos -= 20; break;
        case SB_LINEDOWN:      new_pos += 20; break;
        case SB_PAGEUP:        new_pos -= si.nPage; break;
        case SB_PAGEDOWN:      new_pos += si.nPage; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: new_pos = si.nTrackPos; break;
        case SB_TOP:           new_pos = si.nMin; break;
        case SB_BOTTOM:        new_pos = si.nMax; break;
        }
        int max_scroll = std::max(0, (int)si.nMax - (int)si.nPage + 1);
        new_pos = std::max(0, std::min(max_scroll, new_pos));
        if (new_pos != old_pos) {
            int dy = old_pos - new_pos;
            si.fMask = SIF_POS;
            si.nPos = new_pos;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            ScrollWindow(hwnd, 0, dy, nullptr, nullptr);
            UpdateWindow(hwnd);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        // 3 lines per wheel notch (Windows default), driven via WM_VSCROLL
        // so the same path handles thumb math + clamping.
        int delta = GET_WHEEL_DELTA_WPARAM(wparam);
        int notches = delta / WHEEL_DELTA;
        int direction = (notches > 0) ? SB_LINEUP : SB_LINEDOWN;
        int count = std::abs(notches) * 3;
        for (int i = 0; i < count; ++i)
            SendMessageA(hwnd, WM_VSCROLL, MAKEWPARAM(direction, 0), 0);
        return 0;
    }
    case WM_ERASEBKGND: {
        // Fill with COLOR_BTNFACE so the scroll area matches the surrounding
        // dialog look and child controls' transparent backgrounds blend in.
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wparam, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
        return 1;
    }
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

void register_scroll_panel_class(HINSTANCE hinst)
{
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &scroll_panel_wndproc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kScrollPanelClass;
    RegisterClassExA(&wc);
    registered = true;
}

// Marks `panel` as containing `height` pixels of stacked content so its
// scroll bar / page size can be derived from the visible client height.
void set_scroll_panel_content_height(HWND panel, int height)
{
    auto* data = reinterpret_cast<ScrollPanelData*>(GetWindowLongPtr(panel, GWLP_USERDATA));
    if (!data) return;
    data->content_height = height;
    apply_scroll_geometry(panel);
}

// Ridgeline-section settings schema (dogfooded through SchemaSettingsPanel).
const std::array<const char*, 4> kLogLevelOptions{
    "trace", "debug", "info", "warn"
};
const ridgeline::SettingDef kRidgelineSchema[] = {
    {
        .key = "LogLevel",
        .label = "Log level",
        .group = "Logging",
        .type = ridgeline::SettingType::Enum,
        .default_value = std::string("info"),
        .int_range = std::nullopt,
        .enum_options = {kLogLevelOptions.data(), kLogLevelOptions.size()},
        .dynamic_options = nullptr,
    },
    {
        .key = "ConfirmBeforeLaunch",
        .label = "Confirm before launching a game",
        .group = "Launching games",
        .type = ridgeline::SettingType::Bool,
        .default_value = false,
        .int_range = std::nullopt,
        .enum_options = {},
        .dynamic_options = nullptr,
    },
    {
        .key = "KeepRidgelineOpen",
        .label = "Keep Ridgeline open after launching a game",
        .group = "Launching games",
        .type = ridgeline::SettingType::Bool,
        .default_value = false,
        .int_range = std::nullopt,
        .enum_options = {},
        .dynamic_options = nullptr,
    },
    // Note: LastSelectedModule is also read from / written to [ridgeline] in
    // ridgeline.ini, but it's purely internal state (which card to highlight
    // on next startup) and is intentionally NOT exposed in the schema.
};

bool file_exists(const std::string& path)
{
    if (path.empty()) return false;
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// ASCII narrow→wide conversion (good enough for everything we put in the
// Help dialog — all literals are ASCII).
std::wstring widen(const std::string& s)
{
    std::wstring w(s.size(), L'\0');
    for (size_t i = 0; i < s.size(); ++i)
        w[i] = (wchar_t)(unsigned char)s[i];
    return w;
}

// TaskDialog hyperlink handler. Hrefs are arbitrary strings we set in the
// content; we route by exact match.
HRESULT CALLBACK help_dlg_callback(HWND hwnd, UINT msg, WPARAM /*wp*/, LPARAM lp,
                                   LONG_PTR /*ref*/)
{
    if (msg == TDN_HYPERLINK_CLICKED) {
        const wchar_t* href = reinterpret_cast<const wchar_t*>(lp);
        if (std::wcscmp(href, L"github") == 0) {
            ShellExecuteW(hwnd, L"open",
                          L"https://github.com/gooberRF/ridgeline",
                          nullptr, nullptr, SW_SHOW);
        } else if (std::wcscmp(href, L"licensing") == 0) {
            wchar_t exe_path[MAX_PATH];
            GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
            std::wstring path = exe_path;
            auto sep = path.find_last_of(L"\\/");
            if (sep != std::wstring::npos) path.resize(sep + 1);
            path += L"licensing-info.txt";
            ShellExecuteW(hwnd, L"open", path.c_str(),
                          nullptr, nullptr, SW_SHOW);
        }
    }
    return S_OK;
}

void show_help_dialog(HWND parent, HINSTANCE instance)
{
    std::wstring content =
        L"Version " + widen(VERSION_STR) + L"\n"
        L"Copyright (C) 2026 Chris \"Goober\" Parsons. "
        L"Licensed under Mozilla Public License 2.0.\n\n"
        L"Pick a game from the list on the left, set its game executable "
        L"path, then click Launch. Settings are saved to ridgeline.ini next "
        L"to Ridgeline.exe.\n\n"
        L"<a href=\"github\">Ridgeline on GitHub</a>\n"
        L"<a href=\"licensing\">Open-source component licensing</a>";

    TASKDIALOGCONFIG cfg{};
    cfg.cbSize             = sizeof(cfg);
    cfg.hwndParent         = parent;
    cfg.hInstance          = instance;
    cfg.dwFlags            = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION;
    cfg.dwCommonButtons    = TDCBF_OK_BUTTON;
    cfg.pszWindowTitle     = L"About Ridgeline";
    cfg.pszMainIcon        = TD_INFORMATION_ICON;
    cfg.pszMainInstruction = L"Ridgeline";
    cfg.pszContent         = content.c_str();
    cfg.pfCallback         = &help_dlg_callback;
    TaskDialogIndirect(&cfg, nullptr, nullptr, nullptr);
}

// Recursively unwind a nested exception chain into a flat string. Each level
// is indented two spaces deeper so the cause chain reads like a stack trace.
std::string format_nested_exception(const std::exception& e, int level = 0)
{
    std::string out(level * 2, ' ');
    out += e.what();
    try {
        std::rethrow_if_nested(e);
    } catch (const std::exception& nested) {
        out += "\n";
        out += format_nested_exception(nested, level + 1);
    } catch (...) {
        out += "\n";
        out += std::string((level + 1) * 2, ' ');
        out += "(unknown nested exception)";
    }
    return out;
}

} // namespace

HFONT MainWindow::ui_font()
{
    return g_ui_font;
}

HFONT MainWindow::title_font()
{
    return g_title_font;
}

int MainWindow::run(HINSTANCE instance)
{
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    // Use the system message font (Segoe UI on Win Vista+). The default
    // GetStockObject(SYSTEM_FONT) is the legacy bitmap "System" font; we
    // never want that. NONCLIENTMETRICS-driven font respects user font
    // settings (e.g. accessibility large-text mode).
    NONCLIENTMETRICSA ncm{};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        g_ui_font = CreateFontIndirectA(&ncm.lfMessageFont);
        // Title font for owner-drawn listbox cards: ~25% larger and semibold.
        // lfHeight is negative em-height, so a more negative value = bigger.
        LOGFONTA title_lf = ncm.lfMessageFont;
        title_lf.lfHeight = (LONG)(title_lf.lfHeight * 1.25);
        title_lf.lfWeight = FW_SEMIBOLD;
        g_title_font = CreateFontIndirectA(&title_lf);
    }

    register_scroll_panel_class(instance);

    MainWindow self(instance);

    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::static_wnd_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "RidgelineMainWindow";
    if (!RegisterClassExA(&wc)) {
        return 1;
    }

    HWND hwnd = CreateWindowExA(0, "RidgelineMainWindow", PRODUCT_NAME_VERSION,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, kInitialWidth, kInitialHeight,
        nullptr, nullptr, instance, &self);
    if (!hwnd) {
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessage(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (g_title_font) {
        DeleteObject(g_title_font);
        g_title_font = nullptr;
    }
    if (g_ui_font) {
        DeleteObject(g_ui_font);
        g_ui_font = nullptr;
    }
    return (int)msg.wParam;
}

MainWindow::MainWindow(HINSTANCE instance) : m_instance(instance)
{
    m_ini_path = ridgeline::get_ridgeline_ini_path(nullptr);
}

LRESULT CALLBACK MainWindow::static_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lparam);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (self) return self->wnd_proc(msg, wparam, lparam);
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

LRESULT MainWindow::wnd_proc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_CREATE:
        on_create(m_hwnd);
        return 0;

    case WM_SIZE:
        on_size(LOWORD(lparam), HIWORD(lparam));
        return 0;

    case WM_COMMAND:
        if (on_command(wparam, lparam)) return 0;
        break;

    case WM_MEASUREITEM: {
        auto* mi = (MEASUREITEMSTRUCT*)lparam;
        if (mi && wparam == ID_LISTBOX) {
            mi->itemHeight = kCardItemHeight;
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        auto* di = (DRAWITEMSTRUCT*)lparam;
        if (di && wparam == ID_LISTBOX) {
            draw_card(di);
            return TRUE;
        }
        break;
    }

    case WM_MOUSEWHEEL: {
        // WM_MOUSEWHEEL is delivered to the focused window. Forward to the
        // scroll panel when the cursor is hovering anywhere over it (or its
        // child controls), so the wheel works without clicking first.
        if (m_scroll_panel) {
            POINT pt{(SHORT)LOWORD(lparam), (SHORT)HIWORD(lparam)};
            HWND under_cursor = WindowFromPoint(pt);
            for (HWND w = under_cursor; w; w = GetParent(w)) {
                if (w == m_scroll_panel) {
                    return SendMessageA(m_scroll_panel, WM_MOUSEWHEEL, wparam, lparam);
                }
                if (w == m_hwnd) break;
            }
        }
        break;
    }

    case WM_CLOSE:
        save_current_pane();
        DestroyWindow(m_hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(m_hwnd, msg, wparam, lparam);
}

void MainWindow::on_create(HWND hwnd)
{
    // LBS_OWNERDRAWFIXED + LBS_HASSTRINGS lets us own the per-item rendering
    // (the "card" look) while still using LB_GETTEXT etc. for the item names.
    m_listbox = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY |
        LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
        kPaddingOuter, kPaddingOuter, kListboxWidth, 100,
        hwnd, (HMENU)(intptr_t)ID_LISTBOX, m_instance, nullptr);
    apply_ui_font(m_listbox);

    SendMessageA(m_listbox, LB_ADDSTRING, 0, (LPARAM)"Ridgeline");
    auto modules = ridgeline::ModuleRegistry::instance().all();
    for (auto* m : modules) {
        SendMessageA(m_listbox, LB_ADDSTRING, 0, (LPARAM)m->display_name());
    }

    // Restore last selection if present.
    ridgeline::IniSection ridgeline_section{m_ini_path, "ridgeline"};
    auto last = ridgeline_section.get_string("LastSelectedModule", "");
    int sel = 0;
    if (!last.empty()) {
        for (size_t i = 0; i < modules.size(); ++i) {
            if (last == modules[i]->internal_name()) {
                sel = (int)(i + 1);
                break;
            }
        }
    }
    SendMessageA(m_listbox, LB_SETCURSEL, sel, 0);
    m_selected_index = sel;
    on_selection_changed();
}

void MainWindow::on_size(int cx, int cy)
{
    if (!m_listbox) return;
    int listbox_h = cy - 2 * kPaddingOuter;
    SetWindowPos(m_listbox, nullptr,
                 kPaddingOuter, kPaddingOuter,
                 kListboxWidth, listbox_h,
                 SWP_NOZORDER);

    // Right pane geometry (we reposition the persistent right-pane controls
    // and the scroll panel; SchemaSettingsPanel-owned controls are placed at
    // construction time relative to the scroll panel, so they scroll as a
    // unit and don't need reflow on resize).
    int right_x = kPaddingOuter + kListboxWidth + kPaddingInner;
    int right_w = cx - right_x - kPaddingOuter;
    int bottom_button_y = cy - kPaddingOuter - kButtonHeight;

    int scroll_top = kPaddingOuter;            // Ridgeline pane starts at top
    if (m_exe_edit) {
        // Module pane: leave room for the Game executable row above the scroll.
        scroll_top = kPaddingOuter + 22 + kRowHeight + 8;
    }
    int scroll_bottom = bottom_button_y - 8;
    int scroll_h = std::max(0, scroll_bottom - scroll_top);

    if (m_exe_label && !m_exe_edit) {
        // Ridgeline pane has no exe row; the header label scrolls with the
        // schema panel, so don't reposition it here.
    }
    if (m_exe_edit) {
        SetWindowPos(m_exe_label, nullptr, right_x, kPaddingOuter, right_w, 18, SWP_NOZORDER);
        int browse_w = 80;
        int edit_w = right_w - browse_w - kPaddingInner;
        SetWindowPos(m_exe_edit, nullptr,
                     right_x, kPaddingOuter + 22, edit_w, kRowHeight - 2,
                     SWP_NOZORDER);
        if (m_browse_button) {
            SetWindowPos(m_browse_button, nullptr,
                         right_x + edit_w + kPaddingInner, kPaddingOuter + 22,
                         browse_w, kRowHeight - 2, SWP_NOZORDER);
        }
    }
    if (m_scroll_panel) {
        SetWindowPos(m_scroll_panel, nullptr,
                     right_x, scroll_top, right_w, scroll_h, SWP_NOZORDER);
    }
    if (m_launch_button) {
        SetWindowPos(m_launch_button, nullptr,
                     right_x + right_w - 120, bottom_button_y,
                     120, kButtonHeight, SWP_NOZORDER);
    }
    if (m_help_button) {
        SetWindowPos(m_help_button, nullptr,
                     right_x + right_w - 120 - 8 - 80, bottom_button_y,
                     80, kButtonHeight, SWP_NOZORDER);
    }
    if (m_exit_button) {
        SetWindowPos(m_exit_button, nullptr,
                     right_x + right_w - 80, bottom_button_y,
                     80, kButtonHeight, SWP_NOZORDER);
    }
}

void MainWindow::destroy_right_pane()
{
    // Note: callers are responsible for calling save_current_pane() BEFORE
    // touching m_selected_index — destroy_right_pane no longer saves
    // implicitly, because doing so after m_selected_index changed would
    // misroute the old pane's controls' contents into the new section.
    m_schema_panel.reset();
    if (m_active_custom_panel_module) {
        m_active_custom_panel_module->destroy_custom_settings_panel();
        m_active_custom_panel_module = nullptr;
        m_active_custom_panel_hwnd = nullptr;
    }
    auto kill = [](HWND& h) { if (h) { DestroyWindow(h); h = nullptr; } };
    kill(m_exe_label);
    kill(m_exe_edit);
    kill(m_browse_button);
    kill(m_launch_button);
    kill(m_help_button);
    kill(m_exit_button);
    // The scroll panel owns the schema-panel + always-on bullets as its
    // children; destroying it cascades to them. m_decorations entries that
    // were children of m_scroll_panel get cleaned up that way too — but we
    // also clear the vector so we don't leave dangling HWNDs.
    kill(m_scroll_panel);
    m_decorations.clear();
}

void MainWindow::on_selection_changed()
{
    int sel = (int)SendMessageA(m_listbox, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;

    // Save the OUTGOING pane's contents into ITS module's section before
    // m_selected_index is updated to point at the incoming pane. Skipping
    // this step would route the outgoing controls' values into the wrong
    // INI section.
    save_current_pane();

    destroy_right_pane();
    m_selected_index = sel;

    if (sel == 0) {
        build_right_pane_for_ridgeline();
    } else {
        auto modules = ridgeline::ModuleRegistry::instance().all();
        if ((size_t)(sel - 1) < modules.size()) {
            build_right_pane_for_module(modules[sel - 1]);

            // Persist this selection to ridgeline.ini.
            ridgeline::IniSection ridgeline_section{m_ini_path, "ridgeline"};
            ridgeline_section.set_string("LastSelectedModule", modules[sel - 1]->internal_name());
        }
    }

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    on_size(rc.right - rc.left, rc.bottom - rc.top);
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void MainWindow::build_right_pane_for_ridgeline()
{
    int right_x = kPaddingOuter + kListboxWidth + kPaddingInner;
    int right_w = 600;  // initial; on_size repositions

    // Scroll panel covers the whole right area; the header label scrolls with
    // the content. Help/Exit buttons stay outside, fixed at the bottom.
    m_scroll_panel = CreateWindowExA(0, kScrollPanelClass, "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL,
        right_x, kPaddingOuter, right_w, 200,
        m_hwnd, nullptr, m_instance, nullptr);

    int y = 0;
    HWND header = CreateWindowExA(0, "STATIC", "Ridgeline application settings",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, y, right_w, 22, m_scroll_panel, nullptr, m_instance, nullptr);
    apply_ui_font(header);
    m_decorations.push_back(header);
    y += 30;

    ridgeline::IniSection sec{m_ini_path, "ridgeline"};
    m_schema_panel = std::make_unique<SchemaSettingsPanel>(
        m_scroll_panel, 0, y, right_w, kRidgelineSchema, std::move(sec));
    y += m_schema_panel->height_used();

    set_scroll_panel_content_height(m_scroll_panel, y + 8);

    m_help_button = CreateWindowExA(0, "BUTTON", "Help",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 80, kButtonHeight,
        m_hwnd, (HMENU)(intptr_t)ID_HELP, m_instance, nullptr);
    apply_ui_font(m_help_button);
    m_exit_button = CreateWindowExA(0, "BUTTON", "Exit",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 80, kButtonHeight,
        m_hwnd, (HMENU)(intptr_t)ID_EXIT, m_instance, nullptr);
    apply_ui_font(m_exit_button);
}

void MainWindow::build_right_pane_for_module(ridgeline::IModule* module)
{
    int right_x = kPaddingOuter + kListboxWidth + kPaddingInner;
    int right_w = 600;  // initial; on_size repositions

    std::string section_name = module->settings_section();
    ridgeline::IniSection sec{m_ini_path, section_name};

    m_exe_label = CreateWindowExA(0, "STATIC", "Game executable:",
        WS_CHILD | WS_VISIBLE,
        right_x, kPaddingOuter, right_w, 18, m_hwnd, nullptr, m_instance, nullptr);
    apply_ui_font(m_exe_label);
    m_exe_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 0, 0, m_hwnd, (HMENU)(intptr_t)ID_EXE_EDIT, m_instance, nullptr);
    apply_ui_font(m_exe_edit);
    m_browse_button = CreateWindowExA(0, "BUTTON", "Browse...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)(intptr_t)ID_BROWSE, m_instance, nullptr);
    apply_ui_font(m_browse_button);

    auto current_path = sec.get_string("GameExecutablePath", "");
    SetWindowTextA(m_exe_edit, current_path.c_str());

    // Scroll panel for everything below the Game executable row and above the
    // Launch button. on_size sets its real size; this initial geometry is a
    // placeholder.
    int scroll_top = kPaddingOuter + 22 + kRowHeight + 8;
    m_scroll_panel = CreateWindowExA(0, kScrollPanelClass, "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL,
        right_x, scroll_top, right_w, 200,
        m_hwnd, nullptr, m_instance, nullptr);

    // Custom panel takes precedence; fall back to schema if the module
    // declines (returns nullptr from create_custom_settings_panel). Custom
    // panels become children of m_hwnd (no scroll wrapping) since the module
    // owns their layout entirely.
    int content_y = 0;
    HWND custom = module->create_custom_settings_panel(m_hwnd);
    if (custom) {
        m_active_custom_panel_module = module;
        m_active_custom_panel_hwnd = custom;
    } else {
        auto schema = module->settings_schema();
        if (!schema.empty()) {
            m_schema_panel = std::make_unique<SchemaSettingsPanel>(
                m_scroll_panel, 0, content_y, right_w, schema, std::move(sec));
            content_y += m_schema_panel->height_used();
        }
    }

    // "Always applied" section — module's bug-fix bullets. Skipped if empty.
    auto patches = module->always_on_patches();
    if (!patches.empty()) {
        int y = content_y + 14;
        HWND header = CreateWindowExA(0, "STATIC", "Always applied (built-in fixes):",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, y, right_w, 18, m_scroll_panel, nullptr, m_instance, nullptr);
        apply_ui_font(header);
        m_decorations.push_back(header);
        HWND rule = CreateWindowExA(0, "STATIC", "",
            WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
            0, y + 18, right_w, 1, m_scroll_panel, nullptr, m_instance, nullptr);
        m_decorations.push_back(rule);
        y += 22;
        for (const char* line : patches) {
            std::string text = "- ";
            text += line;
            HWND item = CreateWindowExA(0, "STATIC", text.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                8, y, right_w - 8, 32,
                m_scroll_panel, nullptr, m_instance, nullptr);
            apply_ui_font(item);
            m_decorations.push_back(item);
            y += 32;
        }
        content_y = y;
    }

    set_scroll_panel_content_height(m_scroll_panel, content_y + 8);

    m_launch_button = CreateWindowExA(0, "BUTTON", "Launch",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        0, 0, 120, kButtonHeight,
        m_hwnd, (HMENU)(intptr_t)ID_LAUNCH, m_instance, nullptr);
    apply_ui_font(m_launch_button);

    update_launch_button_state();
}

void MainWindow::save_current_pane()
{
    if (m_selected_index < 0) return;

    if (m_schema_panel) {
        m_schema_panel->save();
    }

    if (m_selected_index > 0 && m_exe_edit) {
        // Persist GameExecutablePath. (The schema panel doesn't manage this
        // field; the launcher provides the row above the schema.)
        char buf[MAX_PATH];
        GetWindowTextA(m_exe_edit, buf, MAX_PATH);
        auto modules = ridgeline::ModuleRegistry::instance().all();
        if ((size_t)(m_selected_index - 1) < modules.size()) {
            ridgeline::IniSection sec{m_ini_path,
                                      modules[m_selected_index - 1]->settings_section()};
            sec.set_string("GameExecutablePath", buf);
        }
    }
}

void MainWindow::update_launch_button_state()
{
    if (!m_launch_button || !m_exe_edit) return;
    char buf[MAX_PATH];
    GetWindowTextA(m_exe_edit, buf, MAX_PATH);
    EnableWindow(m_launch_button, file_exists(buf) ? TRUE : FALSE);
}

void MainWindow::draw_card(DRAWITEMSTRUCT* di)
{
    if ((int)di->itemID < 0) return;

    const bool selected = (di->itemState & ODS_SELECTED) != 0;

    // Background. Use the system highlight color for selected; a slightly
    // lighter window background otherwise so cards read as distinct from
    // the blue main-window background.
    HBRUSH bg = (HBRUSH)GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
    FillRect(di->hDC, &di->rcItem, bg);

    // Bottom hairline separator between cards (skip on the selected card so
    // the highlight doesn't get bisected).
    if (!selected) {
        HPEN sep_pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DLIGHT));
        HPEN old_pen = (HPEN)SelectObject(di->hDC, sep_pen);
        MoveToEx(di->hDC, di->rcItem.left, di->rcItem.bottom - 1, nullptr);
        LineTo(di->hDC, di->rcItem.right, di->rcItem.bottom - 1);
        SelectObject(di->hDC, old_pen);
        DeleteObject(sep_pen);
    }

    // Compute title + subtitle for this card.
    std::string title;
    std::string subtitle;
    if (di->itemID == 0) {
        title = "Ridgeline";
        subtitle = "Application settings";
    } else {
        auto modules = ridgeline::ModuleRegistry::instance().all();
        if ((int)(di->itemID - 1) < (int)modules.size()) {
            auto* m = modules[di->itemID - 1];
            title = m->display_name();
            subtitle = m->is_configured() ? "Ready to launch" : "Not configured";
        }
    }

    SetBkMode(di->hDC, TRANSPARENT);

    const int padding_x = 12;
    const int title_top = di->rcItem.top + 6;
    const int subtitle_top = title_top + 22;
    const int text_right = di->rcItem.right - 8;

    // Title.
    HFONT old_font = (HFONT)SelectObject(di->hDC, g_title_font ? g_title_font : g_ui_font);
    SetTextColor(di->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    RECT title_rect{di->rcItem.left + padding_x, title_top, text_right, subtitle_top};
    DrawTextA(di->hDC, title.c_str(), -1, &title_rect,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    // Subtitle.
    if (g_ui_font) SelectObject(di->hDC, g_ui_font);
    SetTextColor(di->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_GRAYTEXT));
    RECT sub_rect{di->rcItem.left + padding_x, subtitle_top, text_right, di->rcItem.bottom - 4};
    DrawTextA(di->hDC, subtitle.c_str(), -1, &sub_rect,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    SelectObject(di->hDC, old_font);

    if (di->itemState & ODS_FOCUS) {
        DrawFocusRect(di->hDC, &di->rcItem);
    }
}

bool MainWindow::on_command(WPARAM wparam, LPARAM lparam)
{
    int id = LOWORD(wparam);
    int code = HIWORD(wparam);
    HWND ctrl = (HWND)lparam;

    if (m_schema_panel && m_schema_panel->handle_command(wparam, lparam)) {
        return true;
    }

    if (id == ID_LISTBOX && code == LBN_SELCHANGE) {
        on_selection_changed();
        return true;
    }

    if (id == ID_EXE_EDIT && code == EN_CHANGE) {
        update_launch_button_state();
        // Refresh the card subtitle ("Ready to launch" / "Not configured")
        // so it reflects the updated path.
        if (m_listbox) InvalidateRect(m_listbox, nullptr, FALSE);
        return true;
    }

    if (id == ID_BROWSE && code == BN_CLICKED) {
        char filename[MAX_PATH];
        GetWindowTextA(m_exe_edit, filename, MAX_PATH);
        OPENFILENAMEA ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = m_hwnd;
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = "Executable\0*.exe\0All Files\0*.*\0";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameA(&ofn)) {
            SetWindowTextA(m_exe_edit, filename);
            update_launch_button_state();
        }
        return true;
    }

    if (id == ID_LAUNCH && code == BN_CLICKED) {
        save_current_pane();
        auto modules = ridgeline::ModuleRegistry::instance().all();
        if (m_selected_index > 0 && (size_t)(m_selected_index - 1) < modules.size()) {
            auto* mod = modules[m_selected_index - 1];

            // Read launcher-wide preferences fresh each time (the user might
            // have edited them on the Ridgeline pane in this same session).
            ridgeline::IniSection ridgeline_sec{m_ini_path, "ridgeline"};
            bool confirm = ridgeline_sec.get_bool("ConfirmBeforeLaunch", false);
            bool keep_open = ridgeline_sec.get_bool("KeepRidgelineOpen", false);

            if (confirm) {
                std::string prompt = std::string("Launch ") + mod->display_name() + "?";
                int rc = MessageBoxA(m_hwnd, prompt.c_str(), "Ridgeline",
                                     MB_YESNO | MB_ICONQUESTION);
                if (rc != IDYES) {
                    xlog::info("Launch of '{}' cancelled by user", mod->internal_name());
                    return true;
                }
            }

            xlog::info("Launch requested: module='{}' ({})",
                       mod->display_name(), mod->internal_name());
            try {
                mod->launch();
                xlog::info("Launch returned successfully");
                if (!keep_open) {
                    xlog::info("Closing launcher (KeepRidgelineOpen=false)");
                    DestroyWindow(m_hwnd);
                }
            } catch (const std::exception& e) {
                std::string detail = format_nested_exception(e);
                xlog::error("Launch FAILED for '{}':\n{}", mod->internal_name(), detail);
                std::string msg = "Launch failed:\n\n" + detail +
                                  "\n\nFull details have been written to Ridgeline.log "
                                  "next to Ridgeline.exe.";
                MessageBoxA(m_hwnd, msg.c_str(), "Ridgeline", MB_ICONERROR);
            }
        }
        return true;
    }

    if (id == ID_HELP && code == BN_CLICKED) {
        show_help_dialog(m_hwnd, m_instance);
        return true;
    }

    if (id == ID_EXIT && code == BN_CLICKED) {
        save_current_pane();
        DestroyWindow(m_hwnd);
        return true;
    }

    (void)ctrl;
    return false;
}

} // namespace ridgeline_launcher
