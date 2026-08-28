#include "input.h"

#include "raylib_platform.h"

// ============= Input (raw keyboard polling) =============
// Extracted verbatim from main.cpp (original lines ~1787-1796) - see input.h for the full
// extraction-stage description of why key_pressed() loses "static" and why the g_prev_*
// debounce-state globals stay behind in main.cpp.

bool key_down(int vk) {
    return IsKeyDown(vk);
}

bool key_pressed(int vk, bool& prev) {
    bool cur = key_down(vk);
    bool pressed = cur && !prev;
    prev = cur;
    return pressed;
}
