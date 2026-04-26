set(SRCS
    include/common/HttpRequest.h
    include/common/ComPtr.h
    include/common/DynamicLinkLibrary.h
    include/common/config/BuildConfig.h
    include/common/config/CfgVar.h
    include/common/config/IniFile.h
    include/common/config/RegKey.h
    include/common/error/error-utils.h
    include/common/error/Exception.h
    include/common/error/d3d-error.h
    include/common/error/Win32Error.h
    include/common/utils/enum-bitwise-operators.h
    include/common/utils/iterable-utils.h
    include/common/utils/list-utils.h
    include/common/utils/mem-pool.h
    include/common/utils/os-utils.h
    include/common/utils/perf-utils.h
    include/common/utils/string-utils.h
    include/common/utils/bool-utils.h
    include/common/version/version.h
    src/HttpRequest.cpp
    src/config/IniFile.cpp
    src/error/d3d-error.cpp
    src/utils/os-utils.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRCS})

add_library(RidgelineCommon STATIC ${SRCS})

target_compile_features(RidgelineCommon PUBLIC cxx_std_20)
set_target_properties(RidgelineCommon PROPERTIES CXX_EXTENSIONS NO)
enable_warnings(RidgelineCommon)

target_include_directories(RidgelineCommon PRIVATE
    include/common
    ${CMAKE_SOURCE_DIR}/vendor/d3d8
)

# Make sure Windows min/max macro don't conflict with std::min/std::max
target_compile_definitions(RidgelineCommon PUBLIC NOMINMAX)

target_include_directories(RidgelineCommon PUBLIC include)

target_link_libraries(RidgelineCommon
    shlwapi
    wininet
    version
    Xlog
)
