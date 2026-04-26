# Agent guide for Ridgeline

This file is intended to help Codex, Claude, and other automated tools work in this repository efficiently.

## Basic information
- Ridgeline is a modular game patching framework. One launcher (`Ridgeline.exe`) plus one DLL per supported game.
- Each game's DLL is injected into the target game process to apply patches.
- Ridgeline is a fork of Alpine Faction but does NOT support Red Faction. Alpine Faction lives separately.

## Repository overview
- `core/common/` — shared C++ utilities (config, errors, HTTP, string utils, version info). Used by both the launcher and game-module DLLs.
- `core/patch_runtime/` — hooking/injection primitives (FunHook, CallHook, CodeInjection, AsmWriter, MemUtils) layered on subhook. Used by injected game-module DLLs.
- `core/launcher_runtime/` — launcher-side helpers: DLL injection, process launching, video device enumeration, update checker.
- `core/xlog/` — structured logger with file / console / Win32 appenders.
- `core/crash_handler/` — standalone crash report exe.
- `core/crash_handler_stub/` — vectored exception handler linked into binaries.
- `launcher/` — Ridgeline.exe source (raw Win32 + a manifest-enabled visual style).
- `modules/<game>/` — per-game modules. Each has a `launcher/` static lib (IModule + settings schema) and a `patch/` shared lib (the injected DLL).
- `cmake/`, `CMakeLists.txt` — build system.
- `vendor/` — third-party deps (subhook, d3d8to9, win32xx WTL, zlib, freetype, nlohmann/json, toml++, xxhash, sha1, ed25519, base64, minibsdiff).
- `research/` — old standalone per-game source code (base, alcatraz, usmostwanted) preserved as reference. Do not delete.
- `docs/` — build instructions.

## Build and run
- See `docs/BUILDING.md`.
- Prefer out-of-source CMake builds.

## Core hooking/injection primitives (used by injected game-module DLLs)
- `FunHook`: full function-replacement hook; stores original pointer for call-through.
- `CallHook`: redirect a specific call instruction without touching other call sites of the same function.
- `CodeInjection`: insert custom code at an arbitrary address; can read/write registers via the `Regs` struct.
- `AsmWriter`: low-level x86 instruction emitter used by hook helpers and for custom patches.

