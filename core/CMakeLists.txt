# RidgelineCore - shared infrastructure used by the Ridgeline launcher and by
# every game-module DLL. Each subdirectory below is a static library (or in
# the case of crash_handler, a standalone exe) that modules can link against.

add_subdirectory(common)
add_subdirectory(xlog)
add_subdirectory(patch_runtime)
add_subdirectory(launcher_runtime)
add_subdirectory(module_api)
add_subdirectory(crash_handler)
add_subdirectory(crash_handler_stub)
