#pragma once

// ============= Input (raw keyboard polling) =============
// Extracted verbatim from main.cpp (original lines ~1787-1796): the two lowest-level
// keyboard-polling primitives used throughout gameplay/menu/building-interaction input
// code. This is the second-to-last extraction stage of the whole refactor plan.
//
// This file deliberately does NOT own the ~18 g_prev_<key> per-key debounce-state globals
// (g_prev_esc/g_prev_enter/g_prev_f5/g_prev_f9/g_prev_l/g_prev_q/g_prev_f3/g_prev_f6/
// g_prev_f7/g_prev_h/g_prev_tab/g_prev_b/g_prev_m/g_prev_r/g_prev_c/g_prev_lmb/g_prev_rmb/
// g_prev_e) - those stay defined in main.cpp, near the top, since they are read/written
// almost entirely by update_game()'s hotkey-polling code, which itself stays in main.cpp
// (the intentional final orchestrator, per the plan). This module only supplies the raw,
// stateless polling primitives; main.cpp still owns the per-key state and passes it in.
//
// key_down() was already non-static before this stage: building_interaction.cpp's
// update_build_menu_input()/update_mining_and_placement() (an earlier extraction stage)
// already call it from another translation unit. key_pressed() loses "static" here for the
// same reason every other function in this codebase's extraction stages does: it is now
// defined in a different translation unit than its only caller (update_game(), which stays
// in main.cpp) - grep across all of src/ confirms no other call sites exist anywhere.
bool key_down(int vk);
bool key_pressed(int vk, bool& prev);
