# Ridgeline

Ridgeline is a modular game patching framework by Goober. One launcher (`Ridgeline.exe`) plus one DLL per supported game; the launcher injects the appropriate DLL into the target game process to apply runtime patches.

Currently supported games:

| Game            | Module ID        | Notes                                           |
| --------------- | ---------------- | ----------------------------------------------- |
| Alcatraz        | `alcatraz`       | FPS uncap, custom resolution, windowed mode, texture-quality control, dev console |
| US Most Wanted  | `us_mostwanted`  | Install-path resolver (no registry), CD bypass, windowed mode, quicksave/quickload hotkeys |

Ridgeline is a fork of Alpine Faction (Goober's Red Faction patch) but does not, and will not, support Red Faction. Alpine Faction continues separately at https://github.com/gooberRF/alpinefaction.

## Using Ridgeline

1. Run `Ridgeline.exe`.
2. Pick a game from the list on the left.
3. Set the `Game executable` path to your installed copy of that game and adjust per-game settings.
4. Click **Launch**.

Settings are stored in `ridgeline.ini` next to `Ridgeline.exe`. Each game has its own `[module.<id>]` section; per-launcher settings live under `[ridgeline]`.

## Adding a new game module

Adding support for a new game is intentionally cheap. The recipe:

1. Create `modules/<game>/` with two subdirectories:
   - `launcher/` — a static lib that subclasses `ridgeline::IModule`, declares a settings schema, and self-registers via `RIDGELINE_REGISTER_MODULE(YourModule)`.
   - `patch/` — a shared lib (`Ridgeline.<Game>.dll`) with an exported `Init()` function. Reads its `[module.<id>]` section from `ridgeline.ini`, applies hooks via `patch_runtime` (FunHook / CodeInjection / AsmWriter), returns 1.
2. Add one line to root [CMakeLists.txt](CMakeLists.txt): `add_subdirectory(modules/<game>)`.
3. Add one line to [launcher/CMakeLists.txt](launcher/CMakeLists.txt): `$<LINK_LIBRARY:WHOLE_ARCHIVE,Module_<Game>_Launcher>`.

The launcher renders the settings UI from your schema automatically — no Win32 dialog code required for the common case. The schema supports Bool / Int / String / Path / Enum / DynamicEnum / KeyBinding control types. For settings the schema can't express, override `IModule::create_custom_settings_panel()` to return a custom child window.

[modules/alcatraz/](modules/alcatraz/) is the recommended reference. [modules/us_mostwanted/](modules/us_mostwanted/) shows the same pattern adapted for a dual-process game (the patch DLL injects itself into a second child process).

## Building

See [docs/BUILDING.md](docs/BUILDING.md). MSVC on Windows or MinGW-w64 on Linux; targets x86 32-bit only (the games are 32-bit).

## Layout

- [core/](core/) — shared infrastructure used by the launcher and every module DLL: `common/` (config, error utils, HTTP), `xlog/` (logging), `patch_runtime/` (hooking primitives), `launcher_runtime/` (DLL injection, process launching, video device enum), `module_api/` (the `IModule` interface), `crash_handler/` + `crash_handler_stub/`.
- [launcher/](launcher/) — `Ridgeline.exe` source.
- [modules/](modules/) — one folder per supported game.
- [vendor/](vendor/) — third-party deps (subhook, d3d8to9, win32xx, zlib, freetype, nlohmann/json, toml++, xxhash, sha1, ed25519, base64, minibsdiff).
- [research/](research/) — old separate-distributable per-game source code (kept indefinitely as reference for the modules).

## License

Ridgeline's source code is licensed under Mozilla Public License 2.0. See [LICENSE.txt](LICENSE.txt). For third-party dependency licensing, see [licensing-info.txt](licensing-info.txt).
