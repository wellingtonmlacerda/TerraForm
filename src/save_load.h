#pragma once

// ============= Save/Load =============
// Extracted verbatim from main.cpp (original lines ~453-838): the binary save-game
// serializer/deserializer. save_game()/load_game() lost "static": main.cpp's update_game()
// (out of scope for this stage) still calls both by name (via g_pause_selection/
// g_menu_selection input handling and kSavePath, which stay in main.cpp since nothing here
// needs them) - same pattern as g_world/g_camera in earlier extraction stages. These were
// forward-declared near the top of main.cpp (old lines ~198-199); that forward declaration
// is gone now that this header supplies the real one.
bool save_game(const char* path);
bool load_game(const char* path);
