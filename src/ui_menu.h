#pragma once

// ============= Menu System (Paused / Main Menu / Dead / Settings) =============
// Extracted verbatim from main.cpp: the render side (original render_world() lines
// ~1715-1974: the darkened-background overlay, the inline draw_mc_button()/mouse_in_rect()
// lambdas, the Paused-menu button list + hardcoded controls text, the Main-Menu button list,
// the death-screen text, and the Settings panel with its graphics/audio/gameplay rows) and
// the input side (original update_game() lines ~2382-2630: the four
// "if (g_state == GameState::X) { ...; return; }" state-machine blocks for Menu/Paused/
// Settings/Dead - button-click hit-testing, save/load, and state transitions).
//
// Only the state-machine-driven menu overlays moved here. The build menu (g_show_build_menu,
// a separate flag checked only while GameState::Playing) and the victory/alerts/world-map
// overlays that render_world() draws right after this block all stay inline in main.cpp -
// they are not part of the Paused/Menu/Dead/Settings menu system and are in scope for later
// extraction stages (building_interaction, per the refactor plan).
//
// render_menus() lost "static" by construction: it is defined here and called from
// render_world() in main.cpp, which is a different translation unit - same pattern as every
// other render_*()/update_*() entry point declared in the other extracted headers.
void render_menus(int win_w, int win_h);

// update_menu_input() replaces the four inline state-machine blocks that used to live at the
// top of update_game(), right after the F3 debug-toggle check. It takes the hotkey booleans
// update_game() already computed (key_pressed()-debounced, single-frame-true) by value -
// it does not recompute or re-poll any key itself, to avoid double-consuming key state -
// restricted to just the subset this code actually reads (grep-confirmed: esc_pressed/
// enter_pressed/f5_pressed/f9_pressed/l_pressed/q_pressed; f3_pressed/tab_pressed/b_pressed/
// h_pressed/f6_pressed/f7_pressed/m_pressed/r_pressed/c_key_pressed are read by other parts
// of update_game() that stay in main.cpp, out of scope for this stage).
//
// Returns true if g_state matched one of Menu/Paused/Settings/Dead and this frame's input was
// fully handled by that menu screen (mirrors the original code's unconditional "return;" at
// the end of each of those four blocks) - update_game() should itself return immediately when
// this is true, exactly as before. Returns false when g_state is Playing (no menu block
// matched), meaning update_game() should fall through to its own Playing-state input code.
//
// Dropped the HWND parameter (raylib migration): the original signature only carried it to
// mirror update_game()'s own HWND, but grep confirmed the body never used it (`(void)hwnd;`) -
// update_game() itself lost its HWND for the same reason (step 5 of the migration).
bool update_menu_input(float dt, bool esc_pressed, bool enter_pressed,
                        bool f5_pressed, bool f9_pressed, bool l_pressed, bool q_pressed);
