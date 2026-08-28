#pragma once

#include <string>

// ============= Font =============
// Migrated to raylib's DrawText/MeasureText (see font.cpp). This header needs no raylib
// include itself: every signature here uses only float/std::string. init_font() dropped its
// HDC parameter now that there is no GDI/WGL context to hand it - raylib's default font needs
// no setup handle at all.
void init_font();
void draw_text(float x, float y, const std::string& s, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
float estimate_text_w_px(const std::string& s);
