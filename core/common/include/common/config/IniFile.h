#pragma once

#include <string>
#include <vector>

// Forward decl to avoid pulling windows.h into headers
struct HINSTANCE__;
typedef HINSTANCE__* HMODULE;

namespace ridgeline {

// ---------------------------------------------------------------------------
// IniSection
//
// Section-scoped wrapper around GetPrivateProfileString / WritePrivateProfileString.
// Each instance reads/writes one section of one INI file. Cheap to construct;
// no internal state is cached, so always-fresh reads.
//
// Use cases:
//   - Launcher's schema-driven settings panel: one IniSection per module's
//     [module.<name>] section, plus one for [ridgeline].
//   - Injected module DLL: one IniSection for its own [module.<name>] section
//     to read its settings at Init() time.

class IniSection {
public:
    IniSection(std::string ini_path, std::string section)
        : m_path(std::move(ini_path)), m_section(std::move(section)) {}

    // Reads -- return the value if present, otherwise the supplied default.
    std::string get_string(const char* key, const std::string& default_value = "") const;
    int get_int(const char* key, int default_value = 0) const;
    bool get_bool(const char* key, bool default_value = false) const;

    // Writes -- flush to disk immediately via WritePrivateProfile*.
    void set_string(const char* key, const std::string& value);
    void set_int(const char* key, int value);
    void set_bool(const char* key, bool value);

    const std::string& path() const { return m_path; }
    const std::string& section() const { return m_section; }

private:
    std::string m_path;
    std::string m_section;
};

// ---------------------------------------------------------------------------
// Path helpers

// Returns the directory containing `module` (with trailing '\').
//   - launcher exe: pass NULL/nullptr to get the launcher's directory
//   - injected DLL: pass the DLL's own HMODULE (typically captured in DllMain)
std::string get_ridgeline_install_dir(HMODULE module);

// Convenience: get_ridgeline_install_dir(module) + "ridgeline.ini".
std::string get_ridgeline_ini_path(HMODULE module);

} // namespace ridgeline
