# US Most Wanted module aggregator.
#   Module_UsMostWanted_Launcher (STATIC) — IModule + schema, /WHOLEARCHIVE
#                                            into Ridgeline.exe.
#   Module_UsMostWanted_Patch    (SHARED) — Ridgeline.UsMostWanted.dll, injected
#                                            into MostWanted.exe (and from there
#                                            into Engine.exe by the patch itself).

add_subdirectory(launcher)
add_subdirectory(patch)
