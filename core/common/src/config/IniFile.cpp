#include <common/config/IniFile.h>
#include <common/utils/os-utils.h>
#include <xlog/Level.h>
#include <xlog/LoggerConfig.h>
#include <windows.h>

namespace ridgeline {

namespace {
// Sentinel default used to detect "key missing" vs "key present and empty"
// when reading via GetPrivateProfileString. Picked to be implausible as a
// real configured value.
constexpr const char* kMissingSentinel = "\x01__RIDGELINE_KEY_MISSING__\x01";
}

std::string IniSection::get_string(const char* key, const std::string& default_value) const
{
    char buf[4096];
    DWORD len = GetPrivateProfileStringA(
        m_section.c_str(), key, kMissingSentinel,
        buf, sizeof(buf), m_path.c_str());

    // GetPrivateProfileStringA returns the number of characters written
    // (not including the null terminator).
    if (len == 0)
        return default_value; // Empty string was actually written
    if (std::string{buf, len} == kMissingSentinel)
        return default_value; // Key absent
    return std::string{buf, len};
}

int IniSection::get_int(const char* key, int default_value) const
{
    return GetPrivateProfileIntA(m_section.c_str(), key, default_value, m_path.c_str());
}

bool IniSection::get_bool(const char* key, bool default_value) const
{
    return GetPrivateProfileIntA(m_section.c_str(), key, default_value ? 1 : 0,
                                 m_path.c_str()) != 0;
}

void IniSection::set_string(const char* key, const std::string& value)
{
    WritePrivateProfileStringA(m_section.c_str(), key, value.c_str(), m_path.c_str());
}

void IniSection::set_int(const char* key, int value)
{
    set_string(key, std::to_string(value));
}

void IniSection::set_bool(const char* key, bool value)
{
    set_string(key, value ? "1" : "0");
}

std::string get_ridgeline_install_dir(HMODULE module)
{
    return get_module_dir(module);
}

std::string get_ridgeline_ini_path(HMODULE module)
{
    return get_ridgeline_install_dir(module) + "ridgeline.ini";
}

void apply_ridgeline_log_level(HMODULE module)
{
    IniSection sec{get_ridgeline_ini_path(module), "ridgeline"};
    auto level_str = sec.get_string("LogLevel", "info");
    xlog::Level level = xlog::Level::info;
    if      (level_str == "trace") level = xlog::Level::trace;
    else if (level_str == "debug") level = xlog::Level::debug;
    else if (level_str == "warn")  level = xlog::Level::warn;
    xlog::LoggerConfig::get().set_default_level(level);
}

} // namespace ridgeline
