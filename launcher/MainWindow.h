#pragma once

#include "SchemaSettingsPanel.h"
#include <ridgeline/Module.h>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>

namespace ridgeline_launcher {

// MainWindow — the Ridgeline launcher's main window.
//
// Layout: left pane is a listbox of selectable items ("Ridgeline" first, then
// one entry per registered IModule). Right pane swaps content based on the
// listbox selection: a Ridgeline-app settings panel for the "Ridgeline" item,
// or a per-module pane (Game Executable row + schema-rendered settings panel
// + Launch button) for a module.
//
// All controls are created programmatically; no .rc dialog template is used,
// so the right-pane content can be fully dynamic per module.

class MainWindow {
public:
    // Creates the window, runs the message loop, returns the exit code.
    static int run(HINSTANCE instance);

    // Process-wide UI font (Segoe UI per system metrics). Used by every child
    // control via WM_SETFONT so the launcher doesn't render in the legacy
    // bitmap "MS Sans Serif" default. Created in run(), destroyed at exit.
    static HFONT ui_font();

    // Slightly larger, semibold variant of ui_font(). Used for the listbox
    // card titles (owner-drawn). Same lifetime as ui_font().
    static HFONT title_font();

private:
    explicit MainWindow(HINSTANCE instance);

    static LRESULT CALLBACK static_wnd_proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT wnd_proc(UINT msg, WPARAM wparam, LPARAM lparam);

    void on_create(HWND hwnd);
    void on_size(int cx, int cy);
    void on_selection_changed();
    bool on_command(WPARAM wparam, LPARAM lparam);

    void destroy_right_pane();
    void build_right_pane_for_ridgeline();
    void build_right_pane_for_module(ridgeline::IModule* module);

    void save_current_pane();
    void update_launch_button_state();

    // Owner-draw renderer for one card (item) in the module list. di->itemID
    // 0 = Ridgeline, >= 1 = module at ModuleRegistry::all()[itemID - 1].
    void draw_card(struct tagDRAWITEMSTRUCT* di);

    HINSTANCE m_instance = nullptr;
    HWND m_hwnd = nullptr;
    HWND m_listbox = nullptr;

    // Right-pane controls (recreated on each selection change).
    HWND m_exe_label = nullptr;
    HWND m_exe_edit = nullptr;
    HWND m_browse_button = nullptr;
    HWND m_launch_button = nullptr;
    HWND m_help_button = nullptr;
    HWND m_exit_button = nullptr;
    // Scrollable container that hosts the schema panel + the always-on bullet
    // list. Created per-pane (right pane), destroyed in destroy_right_pane.
    // The Game executable row + Launch button stay outside the scroll area
    // (children of m_hwnd) so they're always visible.
    HWND m_scroll_panel = nullptr;
    std::unique_ptr<SchemaSettingsPanel> m_schema_panel;

    // Owned-by-MainWindow decorations rendered around the schema panel
    // (e.g. the "Always applied" bullet list under a module's settings).
    // Destroyed in destroy_right_pane.
    std::vector<HWND> m_decorations;

    // Set when the active module supplied a custom panel via
    // IModule::create_custom_settings_panel(). The launcher must call
    // destroy_custom_settings_panel() on this module when switching away.
    ridgeline::IModule* m_active_custom_panel_module = nullptr;
    HWND m_active_custom_panel_hwnd = nullptr;

    // Currently-selected listbox index. 0 = "Ridgeline", >= 1 = module at
    // ModuleRegistry::all()[index - 1].
    int m_selected_index = -1;

    std::string m_ini_path;
};

} // namespace ridgeline_launcher
