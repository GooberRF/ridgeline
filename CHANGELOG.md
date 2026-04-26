# Ridgeline changelog

Loosely follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versioning is [SemVer](https://semver.org/spec/v2.0.0.html).

Each release has one section per affected component — Ridgeline (core), and one per game module — with a flat bulleted list of changes. When releasing: rename the **Unreleased** heading to `## [x.y.z] - YYYY-MM-DD`, leave the empty Unreleased scaffolding for the next cycle, and bump `VERSION_*` macros in [core/common/include/common/version/version.h](core/common/include/common/version/version.h).

---

## [Unreleased]

### Ridgeline (core)
-

### Alcatraz module
-

### US Most Wanted module
-

---

## [0.1.0] - 2026-04-25

Initial release. Ridgeline began as a fork of Alpine Faction (Goober's Red Faction patch) and was re-purposed as a modular framework for patching other games.

### Ridgeline (core)
- Multi-game launcher with a card-style game list and per-module settings panel.
- `IModule` ABI + `ModuleRegistry`; modules self-register at static-init via `RIDGELINE_REGISTER_MODULE`.
- Schema-driven settings UI — modules declare `SettingDef` rows, the launcher renders them. Custom-dialog escape hatch for bespoke needs.
- Single `ridgeline.ini` with one `[module.<name>]` section per module.
- `PatchedAppLauncher` base class with optional SHA1 hash whitelist per module.
- Local-only crash handler: minidump + text dump + log bundled into `<app>-crash.zip` in `logs/`. No online submission.
- Session log written to `logs/Ridgeline.log`.

### Alcatraz module
- FPS uncap (Sleep / Wait short-timeout spin + timer-gate FPU precision fix).
- Custom resolution, windowed / borderless modes, widescreen aspect correction.
- Optional texture-quality reduction via DDS mip-skip and faster level loads via sequential-scan I/O + sibling prefetch.
- Skip the game's resolution-picker launcher dialog.
- Developer console toggle (~ key).

### US Most Wanted module
- Install-path resolver — game runs without the original installer's registry keys.
- CD-check bypass.
- Windowed mode (writes `r_windowed` into `console.cfg`).
- Quick-save / quick-load hotkeys (default F5 / F8, configurable).
- Dual-process injection: launcher patches `MostWanted.exe`, which spawns and re-injects into `Engine.exe`.
