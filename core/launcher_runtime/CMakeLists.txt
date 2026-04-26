set(SRCS
    DllInjector.cpp
    DllInjector.h
    InjectingProcessLauncher.cpp
    InjectingProcessLauncher.h
    PatchedAppLauncher.cpp
    Process.h
    Thread.h
    UpdateChecker.cpp
    VideoDeviceInfoProviderD3D8.cpp
    VideoDeviceInfoProviderD3D9.cpp
    VideoDeviceInfoProviderD3D11.cpp
    Win32Handle.h
    include/launcher_runtime/PatchedAppLauncher.h
    include/launcher_runtime/UpdateChecker.h
    include/launcher_runtime/VideoDeviceInfoProvider.h
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRCS})

add_library(RidgelineLauncherRuntime STATIC ${SRCS})

target_compile_features(RidgelineLauncherRuntime PUBLIC cxx_std_20)
set_target_properties(RidgelineLauncherRuntime PROPERTIES CXX_EXTENSIONS NO)
enable_warnings(RidgelineLauncherRuntime)

target_include_directories(RidgelineLauncherRuntime
    PUBLIC
    include
    PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor/d3d8
    ${CMAKE_CURRENT_SOURCE_DIR}/include/launcher_runtime
)

target_link_libraries(RidgelineLauncherRuntime windowscodecs RidgelineCommon Xlog sha1 ed25519 base64)
