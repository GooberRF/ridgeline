#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// Win32 forward decl (avoid pulling windows.h into every consumer)
struct HWND__;
typedef HWND__* HWND;

namespace ridgeline {

// ---------------------------------------------------------------------------
// Settings schema
//
// Each module declares its settings as a span of SettingDef. The launcher
// renders a generic settings panel from the schema (one Win32 control per
// SettingDef) and binds each control two-way to the module's [module.<name>]
// section in ridgeline.ini.
//
// Modules with truly bespoke UI needs can override IModule::create_custom_
// settings_panel() and ignore the schema. Prefer extending the schema over
// going custom: the third game module benefits from anything we add here.

enum class SettingType {
    Bool,         // checkbox
    Int,          // edit + spinner (optional min/max via SettingDef::int_range)
    String,       // text edit
    Path,         // text edit + Browse button (file picker)
    Enum,         // combo box, options from SettingDef::enum_options
    DynamicEnum,  // combo box, options resolved at panel-build time via dynamic_options()
    KeyBinding,   // key-picker control (stores key name string)
};

struct SettingDef {
    const char* key = nullptr;       // INI key, e.g. "ResWidth"
    const char* label = nullptr;     // shown in the UI
    const char* group = nullptr;     // optional group/section header

    SettingType type = SettingType::Bool;

    // Default value. The active alternative must match `type`:
    //   Bool                                 -> bool
    //   Int                                  -> int
    //   String, Path, Enum, DynamicEnum,     -> std::string
    //   KeyBinding
    std::variant<bool, int, std::string> default_value;

    // Type-specific extras.
    std::optional<std::pair<int, int>> int_range;          // for Int
    std::span<const char* const> enum_options;             // for Enum
    std::function<std::vector<std::string>()> dynamic_options; // for DynamicEnum
};

// ---------------------------------------------------------------------------
// IModule
//
// One per supported game. The module's launcher static lib (linked into
// Ridgeline.exe via /WHOLEARCHIVE) implements this and self-registers from a
// static initializer at program start.

class IModule {
public:
    virtual ~IModule() = default;

    // Stable identifier. Used as the INI section suffix ("module.<internal_name>")
    // and as the symbol prefix; pick once and never change.
    virtual const char* internal_name() const = 0;

    // Human-readable name shown on the launcher's game card.
    virtual const char* display_name() const = 0;

    // INI section this module reads/writes, conventionally "module.<internal_name>".
    virtual const char* settings_section() const = 0;

    // Filenames the module's executable might have (e.g. {"Alcatraz.exe"}).
    // Used by Browse... dialogs as a default filter; non-authoritative.
    virtual std::span<const char* const> default_exe_filenames() const = 0;

    // True iff GameExecutablePath is set in the module's INI section AND the
    // file actually exists on disk. Drives the greying of the Launch button.
    virtual bool is_configured() const = 0;

    // Default path: launcher renders a generic settings panel from this schema.
    // Return an empty span if you provide a custom panel instead.
    virtual std::span<const SettingDef> settings_schema() const = 0;

    // Optional human-readable bullet list of patches this module ALWAYS
    // applies regardless of any setting — bug fixes, install-path resolvers,
    // CD-check bypass, that kind of thing. Shown below the settings panel as
    // a "what this module does for you out of the box" section. Empty span
    // means show nothing.
    virtual std::span<const char* const> always_on_patches() const { return {}; }

    // Escape hatch. Return non-null to supply a fully custom dialog as a
    // child window of `parent`. Default returns nullptr (use schema).
    virtual HWND create_custom_settings_panel(HWND /*parent*/) { return nullptr; }
    virtual void destroy_custom_settings_panel() {}

    // Spawn the game process suspended, inject this module's DLL, resume.
    // Throws ridgeline::LauncherError (or derived) on failure.
    virtual void launch() = 0;
};

// ---------------------------------------------------------------------------
// ModuleRegistry
//
// Process-wide singleton. Every module's launcher static lib registers its
// IModule instance from a static initializer (see RIDGELINE_REGISTER_MODULE
// macro below). The launcher iterates ModuleRegistry::all() to populate the
// game-card list.

class ModuleRegistry {
public:
    static ModuleRegistry& instance();

    // Takes ownership. Safe to call from a static initializer.
    void register_module(std::unique_ptr<IModule> module);

    // All registered modules, in registration order.
    std::span<IModule* const> all() const;

private:
    ModuleRegistry() = default;
    std::vector<std::unique_ptr<IModule>> m_modules;
    std::vector<IModule*> m_view; // raw pointers for span<> view
};

namespace detail {
    // Helper used by the RIDGELINE_REGISTER_MODULE macro. The constructor
    // performs the registration as a side-effect at static-init time.
    struct ModuleRegistrar {
        explicit ModuleRegistrar(std::unique_ptr<IModule> m) {
            ModuleRegistry::instance().register_module(std::move(m));
        }
    };
}

// Drop this in your module's launcher .cpp file (NOT in a header):
//
//   namespace { ridgeline::detail::ModuleRegistrar g_reg{std::make_unique<MyModule>()}; }
//
// The static lib must be linked into Ridgeline.exe with /WHOLEARCHIVE so the
// linker doesn't strip the translation unit containing g_reg. See the module
// CMakeLists.txt template for the link flag.
#define RIDGELINE_REGISTER_MODULE(ModuleType)                                          \
    namespace {                                                                        \
        ::ridgeline::detail::ModuleRegistrar                                           \
            g_ridgeline_module_registrar{std::make_unique<ModuleType>()};              \
    }

} // namespace ridgeline
