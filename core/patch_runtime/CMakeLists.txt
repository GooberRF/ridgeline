set(SRCS
    CallHook.cpp
    CodeBuffer.cpp
    CodeInjection.cpp
    FunHook.cpp
    MemUtils.cpp
    include/patch_runtime/AsmOpcodes.h
    include/patch_runtime/AsmWriter.h
    include/patch_runtime/CallHook.h
    include/patch_runtime/CallPrePostHook.h
    include/patch_runtime/CodeBuffer.h
    include/patch_runtime/CodeInjection.h
    include/patch_runtime/FunHook.h
    include/patch_runtime/FunPrePostHook.h
    include/patch_runtime/InlineAsm.h
    include/patch_runtime/Installable.h
    include/patch_runtime/MemUtils.h
    include/patch_runtime/ShortTypes.h
    include/patch_runtime/StaticBufferResizePatch.h
    include/patch_runtime/Traits.h
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRCS})

add_library(RidgelinePatchRuntime STATIC ${SRCS})
target_compile_features(RidgelinePatchRuntime PUBLIC cxx_std_20)
set_target_properties(RidgelinePatchRuntime PROPERTIES CXX_EXTENSIONS NO)
enable_warnings(RidgelinePatchRuntime)

target_compile_definitions(RidgelinePatchRuntime PUBLIC SUBHOOK_STATIC)

target_include_directories(RidgelinePatchRuntime PUBLIC
    include
    ${CMAKE_SOURCE_DIR}/vendor/subhook
)

target_link_libraries(RidgelinePatchRuntime subhook Xlog)
