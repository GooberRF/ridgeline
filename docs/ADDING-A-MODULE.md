# Adding a new game module

Adding support for a new game in Ridgeline is intentionally cheap. The recipe:

1. Create `modules/<game>/` with two subdirectories:
   - `launcher/` — a static lib that subclasses `ridgeline::IModule`, declares a settings schema, and self-registers via `RIDGELINE_REGISTER_MODULE(YourModule)`.
   - `patch/` — a shared lib (`Ridgeline.<Game>.dll`) with an exported `Init()` function. Reads its `[module.<id>]` section from `ridgeline.ini`, applies hooks via `patch_runtime` (FunHook / CodeInjection / AsmWriter), returns 1.
2. Add one line to the root [CMakeLists.txt](../CMakeLists.txt): `add_subdirectory(modules/<game>)`.
3. Add one line to [launcher/CMakeLists.txt](../launcher/CMakeLists.txt): `$<LINK_LIBRARY:WHOLE_ARCHIVE,Module_<Game>_Launcher>`.

The launcher renders the settings UI from your schema automatically — no Win32 dialog code required for the common case. The schema supports Bool / Int / String / Path / Enum / DynamicEnum / KeyBinding control types. For settings the schema can't express, override `IModule::create_custom_settings_panel()` to return a custom child window.

[modules/alcatraz/](../modules/alcatraz/) is the recommended reference. [modules/us_mostwanted/](../modules/us_mostwanted/) shows the same pattern adapted for a dual-process game (the patch DLL injects itself into a second child process).
