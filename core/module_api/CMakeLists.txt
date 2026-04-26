set(SRCS
    include/ridgeline/Module.h
    src/ModuleRegistry.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRCS})

add_library(RidgelineModuleApi STATIC ${SRCS})

target_compile_features(RidgelineModuleApi PUBLIC cxx_std_20)
set_target_properties(RidgelineModuleApi PROPERTIES CXX_EXTENSIONS NO)
enable_warnings(RidgelineModuleApi)

target_include_directories(RidgelineModuleApi PUBLIC include)
