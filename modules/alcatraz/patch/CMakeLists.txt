set(SRCS
    main.cpp
    rendering.cpp
    rendering.h
    misc.cpp
    misc.h
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRCS})

# Output: bin/Ridgeline.Alcatraz.dll — injected into Alcatraz.exe by the launcher.
add_library(Module_Alcatraz_Patch SHARED ${SRCS})

target_compile_features(Module_Alcatraz_Patch PUBLIC cxx_std_20)
set_target_properties(Module_Alcatraz_Patch PROPERTIES
    PREFIX ""
    OUTPUT_NAME "Ridgeline.Alcatraz"
    CXX_EXTENSIONS NO
)
enable_warnings(Module_Alcatraz_Patch)
setup_debug_info(Module_Alcatraz_Patch)

target_compile_definitions(Module_Alcatraz_Patch PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX BUILD_DLL)

target_link_libraries(Module_Alcatraz_Patch PRIVATE
    RidgelinePatchRuntime
    RidgelineCommon
    Xlog
    CrashHandlerStub
    winmm
)
