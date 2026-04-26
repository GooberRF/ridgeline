set(SRCS
    main.cpp
    misc.cpp
    misc.h
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRCS})

# Output: bin/Ridgeline.UsMostWanted.dll
# Injected into BOTH MostWanted.exe (game launcher) AND Engine.exe (game).
# Init() discriminates by host process name and applies different patches.
add_library(Module_UsMostWanted_Patch SHARED ${SRCS})

target_compile_features(Module_UsMostWanted_Patch PUBLIC cxx_std_20)
set_target_properties(Module_UsMostWanted_Patch PROPERTIES
    PREFIX ""
    OUTPUT_NAME "Ridgeline.UsMostWanted"
    CXX_EXTENSIONS NO
)
enable_warnings(Module_UsMostWanted_Patch)
setup_debug_info(Module_UsMostWanted_Patch)

target_compile_definitions(Module_UsMostWanted_Patch PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX BUILD_DLL)

target_link_libraries(Module_UsMostWanted_Patch PRIVATE
    RidgelinePatchRuntime
    RidgelineCommon
    Xlog
    CrashHandlerStub
)
