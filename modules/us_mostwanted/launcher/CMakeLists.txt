add_library(Module_UsMostWanted_Launcher STATIC UsMwModule.cpp)

target_compile_features(Module_UsMostWanted_Launcher PUBLIC cxx_std_20)
set_target_properties(Module_UsMostWanted_Launcher PROPERTIES CXX_EXTENSIONS NO)
enable_warnings(Module_UsMostWanted_Launcher)

target_link_libraries(Module_UsMostWanted_Launcher PUBLIC
    RidgelineModuleApi
    RidgelineLauncherRuntime
    RidgelineCommon
)
