# Alcatraz module aggregator. Two targets:
#   Module_Alcatraz_Launcher (STATIC) — IModule impl + settings schema, linked
#                                       into Ridgeline.exe via /WHOLEARCHIVE.
#   Module_Alcatraz_Patch    (SHARED) — Ridgeline.Alcatraz.dll, injected into
#                                       Alcatraz.exe at launch.

add_subdirectory(launcher)
add_subdirectory(patch)
