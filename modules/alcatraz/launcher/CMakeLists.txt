add_library(Module_Alcatraz_Launcher STATIC AlcatrazModule.cpp)

target_compile_features(Module_Alcatraz_Launcher PUBLIC cxx_std_20)
set_target_properties(Module_Alcatraz_Launcher PROPERTIES CXX_EXTENSIONS NO)
enable_warnings(Module_Alcatraz_Launcher)

target_link_libraries(Module_Alcatraz_Launcher PUBLIC
    RidgelineModuleApi
    RidgelineLauncherRuntime
    RidgelineCommon
)
