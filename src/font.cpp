#include "font.h"

#include "raylib_platform.h"

// ============= Font (raylib) =============
// Loads the system's Consolas TTF (the same font the original Win32/GDI renderer used) via
// raylib's LoadFontEx, instead of raylib's built-in default font - the default font is a
// small bitmap face that reads poorly at HUD sizes. Falls back to raylib's default font if
// Consolas isn't found (e.g. running on a machine without it), so the game never fails to
// render text. draw_text()/estimate_text_w_px() keep their exact original signatures (float
// x, y, r/g/b/a in 0..1) so none of the ~40+ call sites elsewhere in the codebase need to
// change.
static constexpr int kFontSize = 16;      // base size the font is loaded/rendered at (matches
                                           // the original GDI font's "-16" pixel size, so the
                                           // existing HUD layout's tight line spacing still fits)
static constexpr float kFontSpacing = 1.0f;
static Font g_ui_font;
static bool g_ui_font_loaded = false;

void init_font() {
    if (g_ui_font_loaded) return;
    Font f = LoadFontEx("C:\\Windows\\Fonts\\consola.ttf", kFontSize, nullptr, 0);
    if (f.texture.id != 0) {
        g_ui_font = f;
        SetTextureFilter(g_ui_font.texture, TEXTURE_FILTER_BILINEAR);
    } else {
        g_ui_font = GetFontDefault();
    }
    g_ui_font_loaded = true;
}

void draw_text(float x, float y, const std::string& s, float r, float g, float b, float a) {
    if (s.empty()) return;
    Color c;
    c.r = (unsigned char)std::clamp((int)(r * 255.0f), 0, 255);
    c.g = (unsigned char)std::clamp((int)(g * 255.0f), 0, 255);
    c.b = (unsigned char)std::clamp((int)(b * 255.0f), 0, 255);
    c.a = (unsigned char)std::clamp((int)(a * 255.0f), 0, 255);
    // The old GDI/wglUseFontBitmapsA renderer positioned text by its BASELINE (glRasterPos2f
    // convention) - every call site across ui_hud.cpp/ui_menu.cpp/building_interaction.cpp/
    // minimap.cpp (~40+ sites) was tuned assuming that anchor. raylib positions text by the
    // TOP of its bounding box instead - shifting up by ~80% of the font size (a typical
    // ascent-to-em-size ratio) restores the original visual position without having to touch
    // every call site.
    float baseline_offset = kFontSize * 0.8f;
    DrawTextEx(g_ui_font, s.c_str(), {x, y - baseline_offset}, (float)kFontSize, kFontSpacing, c);
}

float estimate_text_w_px(const std::string& s) {
    if (s.empty()) return 0.0f;
    return MeasureTextEx(g_ui_font, s.c_str(), (float)kFontSize, kFontSpacing).x;
}
