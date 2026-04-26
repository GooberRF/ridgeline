#include "SchemaSettingsPanel.h"
#include "MainWindow.h"

#include <commdlg.h>
#include <commctrl.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace ridgeline_launcher {

namespace {
// Layout constants. One ridgeline.ini knob away from being themable later.
constexpr int kRowHeight = 26;
constexpr int kRowSpacing = 6;
constexpr int kGroupHeaderHeight = 22;
constexpr int kGroupTopGap = 14;
constexpr int kBrowseButtonWidth = 80;
constexpr int kBrowseButtonGap = 6;

// We assign sequential control IDs starting here so they don't collide with
// IDs the launcher's main window uses for its own controls.
constexpr int kFirstSchemaCtrlId = 30000;

// Convert std::variant default to a string usable for control init, when the
// INI section doesn't already have a value for the key.
std::string default_as_string(const ridgeline::SettingDef& def)
{
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) return v ? "1" : "0";
        else if constexpr (std::is_same_v<T, int>) return std::to_string(v);
        else return v;
    }, def.default_value);
}

bool default_as_bool(const ridgeline::SettingDef& def)
{
    if (auto* b = std::get_if<bool>(&def.default_value)) return *b;
    if (auto* i = std::get_if<int>(&def.default_value)) return *i != 0;
    if (auto* s = std::get_if<std::string>(&def.default_value)) return *s == "1" || *s == "true";
    return false;
}

int default_as_int(const ridgeline::SettingDef& def)
{
    if (auto* i = std::get_if<int>(&def.default_value)) return *i;
    if (auto* b = std::get_if<bool>(&def.default_value)) return *b ? 1 : 0;
    if (auto* s = std::get_if<std::string>(&def.default_value)) {
        try { return std::stoi(*s); } catch (...) { return 0; }
    }
    return 0;
}
} // namespace

void SchemaSettingsPanel::apply_font(HWND hwnd)
{
    if (HFONT f = MainWindow::ui_font())
        SendMessageA(hwnd, WM_SETFONT, (WPARAM)f, TRUE);
}

SchemaSettingsPanel::SchemaSettingsPanel(HWND parent,
                                         int origin_x, int origin_y,
                                         int max_width,
                                         std::span<const ridgeline::SettingDef> schema,
                                         ridgeline::IniSection ini)
    : m_parent(parent), m_ini(std::move(ini)), m_origin_x(origin_x)
{
    m_rows.reserve(schema.size());
    for (const auto& def : schema) {
        Row row;
        row.def = &def;
        m_rows.push_back(row);
    }

    // Roomy label column so the verbose schema labels fit on one line. Labels
    // longer than this still truncate; tooltips for full description are TODO.
    const int label_width = std::clamp(max_width / 2, 220, 360);
    const int control_x = origin_x + label_width + 8;
    const int control_width = max_width - (label_width + 8);

    int y = origin_y;
    int next_id = kFirstSchemaCtrlId;
    const char* current_group = nullptr;
    HINSTANCE hinst = (HINSTANCE)GetWindowLongPtr(m_parent, GWLP_HINSTANCE);

    for (auto& row : m_rows) {
        // Render a group header above rows that introduce a new group value.
        const char* g = row.def->group ? row.def->group : "";
        bool group_changed = (current_group == nullptr && g[0] != '\0') ||
                             (current_group != nullptr && std::strcmp(current_group, g) != 0);
        if (group_changed && g[0] != '\0') {
            if (current_group) y += kGroupTopGap;
            HWND header = CreateWindowExA(0, "STATIC", g,
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                origin_x, y + 2, max_width, kGroupHeaderHeight - 4,
                m_parent, nullptr, hinst, nullptr);
            apply_font(header);
            m_decorations.push_back(header);
            // Light visual: a thin separator line just below the header.
            HWND rule = CreateWindowExA(0, "STATIC", "",
                WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                origin_x, y + kGroupHeaderHeight - 2, max_width, 1,
                m_parent, nullptr, hinst, nullptr);
            m_decorations.push_back(rule);
            y += kGroupHeaderHeight;
            current_group = g;
        }

        if (row.def->type == ridgeline::SettingType::Path)
            row.browse_button_id = next_id++;
        create_row(row, y, label_width, control_x, control_width);
        load_initial_value(row);
    }
    m_total_height = y - origin_y;
}

SchemaSettingsPanel::~SchemaSettingsPanel()
{
    // Parent window destruction would clean these up too, but be explicit so
    // a panel swap (selection change) doesn't leave stale child windows.
    for (auto& row : m_rows) {
        if (row.label) DestroyWindow(row.label);
        if (row.control) DestroyWindow(row.control);
        if (row.browse_button) DestroyWindow(row.browse_button);
    }
    for (HWND h : m_decorations) {
        if (h) DestroyWindow(h);
    }
}

void SchemaSettingsPanel::create_row(Row& row, int& y, int label_width, int control_x, int control_width)
{
    const HINSTANCE hinst = (HINSTANCE)GetWindowLongPtr(m_parent, GWLP_HINSTANCE);

    row.label = CreateWindowExA(0, "STATIC", row.def->label,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        m_origin_x, y + 4, label_width, kRowHeight - 4,
        m_parent, nullptr, hinst, nullptr);
    apply_font(row.label);

    int effective_control_width = control_width;
    if (row.def->type == ridgeline::SettingType::Path) {
        effective_control_width = control_width - kBrowseButtonWidth - kBrowseButtonGap;
    }

    switch (row.def->type) {
    case ridgeline::SettingType::Bool:
        row.control = CreateWindowExA(0, "BUTTON", "",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            control_x, y + 4, effective_control_width, kRowHeight - 4,
            m_parent, nullptr, hinst, nullptr);
        break;

    case ridgeline::SettingType::Int:
        row.control = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
            control_x, y + 2, effective_control_width, kRowHeight - 4,
            m_parent, nullptr, hinst, nullptr);
        break;

    case ridgeline::SettingType::String:
    case ridgeline::SettingType::Path:
    case ridgeline::SettingType::KeyBinding:
        row.control = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            control_x, y + 2, effective_control_width, kRowHeight - 4,
            m_parent, nullptr, hinst, nullptr);
        break;

    case ridgeline::SettingType::Enum:
    case ridgeline::SettingType::DynamicEnum:
        row.control = CreateWindowExA(0, "COMBOBOX", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
            control_x, y + 2, effective_control_width, kRowHeight * 8,
            m_parent, nullptr, hinst, nullptr);
        if (row.def->type == ridgeline::SettingType::Enum) {
            for (const char* opt : row.def->enum_options) {
                SendMessageA(row.control, CB_ADDSTRING, 0, (LPARAM)opt);
            }
        } else if (row.def->dynamic_options) {
            auto opts = row.def->dynamic_options();
            for (const auto& opt : opts) {
                SendMessageA(row.control, CB_ADDSTRING, 0, (LPARAM)opt.c_str());
            }
        }
        break;
    }
    if (row.control) apply_font(row.control);

    if (row.def->type == ridgeline::SettingType::Path) {
        row.browse_button = CreateWindowExA(0, "BUTTON", "Browse...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            control_x + effective_control_width + kBrowseButtonGap, y + 2,
            kBrowseButtonWidth, kRowHeight - 4,
            m_parent, (HMENU)(intptr_t)row.browse_button_id, hinst, nullptr);
        apply_font(row.browse_button);
    }

    y += kRowHeight + kRowSpacing;
}

std::string SchemaSettingsPanel::read_string_value(const ridgeline::SettingDef& def) const
{
    return m_ini.get_string(def.key, default_as_string(def));
}

void SchemaSettingsPanel::load_initial_value(const Row& row)
{
    if (!row.control) return;

    switch (row.def->type) {
    case ridgeline::SettingType::Bool: {
        bool v = m_ini.get_bool(row.def->key, default_as_bool(*row.def));
        SendMessageA(row.control, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0);
        break;
    }
    case ridgeline::SettingType::Int: {
        int v = m_ini.get_int(row.def->key, default_as_int(*row.def));
        SetWindowTextA(row.control, std::to_string(v).c_str());
        break;
    }
    case ridgeline::SettingType::String:
    case ridgeline::SettingType::Path:
    case ridgeline::SettingType::KeyBinding: {
        SetWindowTextA(row.control, read_string_value(*row.def).c_str());
        break;
    }
    case ridgeline::SettingType::Enum:
    case ridgeline::SettingType::DynamicEnum: {
        auto v = read_string_value(*row.def);
        int idx = (int)SendMessageA(row.control, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)v.c_str());
        if (idx == CB_ERR) idx = 0;
        SendMessageA(row.control, CB_SETCURSEL, idx, 0);
        break;
    }
    }
}

void SchemaSettingsPanel::save()
{
    char buf[4096];
    for (const auto& row : m_rows) {
        if (!row.control) continue;
        switch (row.def->type) {
        case ridgeline::SettingType::Bool: {
            bool v = SendMessageA(row.control, BM_GETCHECK, 0, 0) == BST_CHECKED;
            m_ini.set_bool(row.def->key, v);
            break;
        }
        case ridgeline::SettingType::Int: {
            GetWindowTextA(row.control, buf, sizeof(buf));
            int v = 0;
            try { v = std::stoi(buf); } catch (...) { v = default_as_int(*row.def); }
            m_ini.set_int(row.def->key, v);
            break;
        }
        case ridgeline::SettingType::String:
        case ridgeline::SettingType::Path:
        case ridgeline::SettingType::KeyBinding: {
            GetWindowTextA(row.control, buf, sizeof(buf));
            m_ini.set_string(row.def->key, buf);
            break;
        }
        case ridgeline::SettingType::Enum:
        case ridgeline::SettingType::DynamicEnum: {
            int idx = (int)SendMessageA(row.control, CB_GETCURSEL, 0, 0);
            if (idx == CB_ERR) {
                m_ini.set_string(row.def->key, default_as_string(*row.def));
            } else {
                SendMessageA(row.control, CB_GETLBTEXT, idx, (LPARAM)buf);
                m_ini.set_string(row.def->key, buf);
            }
            break;
        }
        }
    }
}

bool SchemaSettingsPanel::handle_command(WPARAM wparam, LPARAM /*lparam*/)
{
    int id = LOWORD(wparam);
    int code = HIWORD(wparam);
    if (code != BN_CLICKED) return false;

    for (const auto& row : m_rows) {
        if (row.browse_button_id != 0 && id == row.browse_button_id) {
            char filename[MAX_PATH] = "";
            GetWindowTextA(row.control, filename, MAX_PATH);
            OPENFILENAMEA ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = m_parent;
            ofn.lpstrFile = filename;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = "All Files\0*.*\0";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) {
                SetWindowTextA(row.control, filename);
            }
            return true;
        }
    }
    return false;
}

} // namespace ridgeline_launcher
