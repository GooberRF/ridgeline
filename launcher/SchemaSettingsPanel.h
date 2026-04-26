#pragma once

#include <ridgeline/Module.h>
#include <common/config/IniFile.h>
#include <vector>
#include <windows.h>

namespace ridgeline_launcher {

// SchemaSettingsPanel
//
// Generic settings UI driven by an IModule's settings_schema(). Creates one
// row per SettingDef (label + control) inside a parent HWND, and binds each
// control two-way to a section of ridgeline.ini.
//
// Lifecycle:
//   1. Construct, passing the parent HWND, x/y origin within the parent, max
//      width, the schema, and the IniSection that backs it.
//   2. The constructor creates all child controls and loads initial values.
//   3. Call save() to flush all controls back to the INI section.
//   4. Destroy: the destructor destroys all created child windows.
//
// One instance per visible panel. When the user picks a different module in
// the launcher's left pane, destroy the old SchemaSettingsPanel and construct
// a new one for the new selection.

class SchemaSettingsPanel {
public:
    SchemaSettingsPanel(HWND parent,
                        int origin_x, int origin_y,
                        int max_width,
                        std::span<const ridgeline::SettingDef> schema,
                        ridgeline::IniSection ini);

    ~SchemaSettingsPanel();

    SchemaSettingsPanel(const SchemaSettingsPanel&) = delete;
    SchemaSettingsPanel& operator=(const SchemaSettingsPanel&) = delete;

    // Read every control's current value and write to the INI section.
    void save();

    // Total height (in pixels) consumed by the rendered controls. Useful so
    // the parent window can place follow-up widgets below the panel.
    int height_used() const { return m_total_height; }

    // Forward Browse-button clicks here. Returns true if handled.
    bool handle_command(WPARAM wparam, LPARAM lparam);

private:
    struct Row {
        const ridgeline::SettingDef* def = nullptr;
        HWND label = nullptr;
        HWND control = nullptr;
        HWND browse_button = nullptr;  // only for Path
        int browse_button_id = 0;      // assigned ctrl id, used by handle_command
    };

    HWND m_parent;
    ridgeline::IniSection m_ini;
    std::vector<Row> m_rows;
    // Group headers + separator-rule statics, tracked separately because
    // they're owned by the panel but not associated with any row. Destroyed
    // in the destructor so panel swaps don't leak controls.
    std::vector<HWND> m_decorations;
    int m_origin_x = 0;
    int m_total_height = 0;

    void create_row(Row& row, int& y, int label_width, int control_x, int control_width);
    void load_initial_value(const Row& row);
    std::string read_string_value(const ridgeline::SettingDef& def) const;
    void apply_font(HWND hwnd);
};

} // namespace ridgeline_launcher
