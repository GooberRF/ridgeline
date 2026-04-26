set(SRCS
    CMakeLists.txt
    main.cpp
    MainWindow.cpp
    MainWindow.h
    SchemaSettingsPanel.cpp
    SchemaSettingsPanel.h
    launcher.manifest
)

# Application manifest embedding is per-compiler. With MSVC, CMake/Ninja runs
# its own mt.exe step that embeds a default manifest at resource ID 1; if the
# .rc embeds OUR manifest at the same ID we get CVT1100 "duplicate resource".
# Instead, on MSVC we feed launcher.manifest to the linker via /MANIFEST:EMBED
# /MANIFESTINPUT, which merges with whatever mt.exe would emit. With MinGW
# (windres, no mt.exe), the .rc-based embed is fine.
if(NOT MSVC)
    list(APPEND SRCS launcher.rc)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRCS})

add_executable(RidgelineLauncher WIN32 ${SRCS})

if(MSVC)
    target_link_options(RidgelineLauncher PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/launcher.manifest"
        "/MANIFESTUAC:NO"
    )
endif()

# Output binary is "Ridgeline.exe". The CMake target name keeps the Launcher
# suffix to disambiguate from the project name in the build graph.
set_target_properties(RidgelineLauncher PROPERTIES OUTPUT_NAME Ridgeline)

target_compile_features(RidgelineLauncher PUBLIC cxx_std_20)
set_target_properties(RidgelineLauncher PROPERTIES CXX_EXTENSIONS NO)
enable_warnings(RidgelineLauncher)
setup_debug_info(RidgelineLauncher)

target_link_libraries(RidgelineLauncher PRIVATE
    RidgelineCommon
    RidgelineLauncherRuntime
    RidgelineModuleApi
    Xlog
    CrashHandlerStub
    comdlg32
    comctl32
    # Each module's launcher static lib must be linked with WHOLE_ARCHIVE
    # so the RIDGELINE_REGISTER_MODULE static initializer survives the
    # linker's dead-code strip. Add a new line here per module added under
    # modules/ — the only launcher-side edit required to add a game.
    $<LINK_LIBRARY:WHOLE_ARCHIVE,Module_Alcatraz_Launcher>
    $<LINK_LIBRARY:WHOLE_ARCHIVE,Module_UsMostWanted_Launcher>
)

# Copy LICENSE.txt + licensing-info.txt next to the launcher exe so they ship
# with the binaries. POST_BUILD on the launcher target so the destination
# tracks the per-config output directory automatically (Visual Studio
# multi-config).
add_custom_command(TARGET RidgelineLauncher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/LICENSE.txt
            $<TARGET_FILE_DIR:RidgelineLauncher>/LICENSE.txt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/licensing-info.txt
            $<TARGET_FILE_DIR:RidgelineLauncher>/licensing-info.txt
)
