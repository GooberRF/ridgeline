Building Ridgeline
==================

Ridgeline is built as 32-bit Windows binaries (the games it patches are 32-bit). Use either MSVC on Windows or MinGW-w64 on Linux.

Windows (MSVC)
--------------

On Windows use CMake GUI or the command line to generate project files for Visual Studio. Win32 (x86) is the only supported target — if your generator defaults to x64, change it to Win32.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```

Built binaries land in `build/bin/` (or `build/<config>/bin/` for multi-config generators).

Linux (MinGW-w64 cross-compile)
-------------------------------

Tested on Ubuntu 24.04. Other distributions should work but may require different package names.

Install the toolchain:

```
sudo apt-get install g++-mingw-w64-i686-posix cmake ninja-build
```

Configure and build:

```
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-ubuntu.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Build outputs land in `build/bin/`.
