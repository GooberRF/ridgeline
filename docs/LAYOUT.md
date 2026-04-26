# Repository layout

- [core/](../core/) — shared infrastructure used by the launcher and every module DLL: `common/` (config, error utils, HTTP), `xlog/` (logging), `patch_runtime/` (hooking primitives), `launcher_runtime/` (DLL injection, process launching, video device enum), `module_api/` (the `IModule` interface), `crash_handler/` + `crash_handler_stub/`.
- [launcher/](../launcher/) — `Ridgeline.exe` source.
- [modules/](../modules/) — one folder per supported game.
- [vendor/](../vendor/) — third-party deps (subhook, d3d8to9, win32xx, zlib, freetype, nlohmann/json, toml++, xxhash, sha1, ed25519, base64, minibsdiff).
- [research/](../research/) — old separate-distributable per-game source code (kept indefinitely as reference for the modules).
