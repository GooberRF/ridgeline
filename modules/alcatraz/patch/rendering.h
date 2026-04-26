#pragma once

extern bool g_fps_fix_enabled;
extern int g_res_width;
extern int g_res_height;
extern int g_window_mode; // 0=Fullscreen, 1=Windowed, 2=Borderless

void rendering_apply_patches();
