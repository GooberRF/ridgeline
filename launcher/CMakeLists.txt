set(SRCS
    CMakeLists.txt
    main.cpp
    MainWindow.cpp
    MainWindow.h
    SchemaSettingsPanel.cpp
    SchemaSettingsPanel.h
    launcher.rc
    launcher.manifest
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRCS})

add_executable(RidgelineLauncher WIN32 ${SRCS})

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
