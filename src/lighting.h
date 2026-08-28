#pragma once

#include <vector>

// ============= Iluminacao 2D (RTX Fake) =============
// Extracted verbatim from main.cpp (original lines ~339-400, 792-1327): the Light2D light-
// source struct, the lightmap/bloom pixel buffers, the LightingSettings config struct + its
// g_lighting instance, the two debug toggles main.cpp's F3 cycle and settings menu still
// use, and the lighting pipeline functions (ambient/natural light, light collection, shadow
// raymarching, lightmap compute/sample, depth darkening, color grading).
//
// Light2D needs a complete definition here (not just a forward declaration): main.cpp's
// render_world() (out of scope for this stage - untouched) reads g_lights[i]'s fields
// directly in its F3 debug overlay (active-lights listing), so the type must be visible
// there too - same "owner keeps the definition, other TUs get it via the header" rule used
// for Module/ModuleStatus in modules_building.h.
struct Light2D {
    float x, y;           // Posicao no mundo 2D (tiles)
    float height;         // Altura Y no espaco 3D
    float radius;         // Raio de influencia (tiles)
    float intensity;      // Intensidade (0-1)
    float r, g, b;        // Cor RGB (0-1)
    float falloff;        // Tipo de atenuacao (1=linear, 2=quadratica)
    bool flicker;         // Luz piscante
    float flicker_speed;  // Velocidade do flicker
    bool is_emissive;     // Se a fonte emite glow/bloom
};

// kLightmapSize is a compile-time literal (not mutable state) needed both by this cluster's
// own lightmap math and by main.cpp's render_world() (F3 lightmap debug overlay reads it
// directly to size its on-screen grid) - so, like kPi in math_core.h, it lives right here in
// the header rather than behind an extern. kLightmapPixels is NOT declared here: grep
// confirms it is only ever used inside this cluster's own .cpp (lightmap/bloom buffer sizing
// and loops), so lighting.cpp keeps its own local copy derived from this constant.
static constexpr int kLightmapSize = 96;      // Resolucao do lightmap (tiles)

// g_lights/g_lightmap_r/g/b lost "static": main.cpp's render_world() (out of scope for this
// stage, untouched) reads them directly in its two F3 debug overlays (lightmap pixel grid +
// active-lights list) - same pattern as g_world/g_camera in earlier extraction stages.
// g_bloom_r/g/b, g_temp_r/g/b, g_lightmap_center_x/z, and g_debug_bloom are NOT declared
// here: grep confirms they are only ever touched inside this cluster's own functions (and,
// for g_debug_bloom, nowhere at all - pre-existing dead code from before this refactor), so
// they stay file-local statics in lighting.cpp.
extern std::vector<Light2D> g_lights;
extern std::vector<float> g_lightmap_r;
extern std::vector<float> g_lightmap_g;
extern std::vector<float> g_lightmap_b;

// LightingSettings/g_lighting lost "static" for the same reason: render_world()'s per-tile
// lighting/vignette/debug-overlay code and update_game()'s F3 toggle + settings-menu input
// handling (both out of scope for this stage, untouched) read/write its fields directly.
struct LightingSettings {
    bool enabled = true;
    bool shadows_enabled = true;
    bool bloom_enabled = true;
    float bloom_intensity = 0.55f;   // Era 0.45 - brilho um pouco mais rico/atmosferico
    float bloom_threshold = 0.75f;
    float shadow_softness = 0.6f;
    int shadow_samples = 8;          // Passos do raymarching
    float ambient_min = 0.06f;       // Luz ambiente minima (piso absoluto, usado pela curva do sol)
    // ---- NOITE ----
    // compute_daylight() e max(0, sin(...)), ou seja EXATAMENTE zero durante metade do ciclo. Com
    // isso o ambiente noturno era ambient_min (0.06) fixo, multiplicado pela cor noturna fria
    // (luminancia ~0.44) = ~0.026 no lightmap. Terreno preto, sem gradiente, sem nada. Os tres
    // campos abaixo dao a noite uma curva PROPRIA em vez de deixa-la ser o resto de zero da curva
    // do sol - continua sendo um termo ambiente, nao um "acende tudo".
    float night_ambient = 0.19f;     // piso da noite fechada, SEM lua nenhuma
    float moon_light = 0.24f;        // quanto a lua soma no ambiente quando esta alta (x sky_moonlight)
    float moon_directional = 0.34f;  // intensidade da fonte lunar deslocada (lado iluminado + sombra)
    float ambient_max = 0.92f;       // Luz ambiente maxima (dia)
    float contrast = 1.18f;          // Era 1.12 - mais contraste/profundidade visual
    float exposure = 1.05f;
    float saturation = 1.18f;        // Era 1.08 - cores mais vivas
    float vignette_intensity = 0.30f; // Era 0.25 - enquadramento levemente mais cinematico
    float vignette_radius = 0.85f;
    float depth_darkening = 0.5f;    // Escurecimento por profundidade
    bool color_grading = true;
    // Escurecimento de AMBIENTE em ambiente fechado (interior da cupula + corredor). O ambiente
    // deste motor e' um termo GLOBAL uniforme (compute_ambient_light -> compute_lightmap): nao
    // existe skylight nem oclusao, entao um interior fechado fica exatamente tao claro quanto campo
    // aberto ao meio-dia e as luminarias de dentro nao aparecem. Este multiplicador e' o UNICO
    // lugar que introduz a nocao de "dentro". 1.0 = desligado (bit-a-bit igual ao de antes).
    float indoor_ambient_mul = 0.45f;
};
extern LightingSettings g_lighting;

// Debug. g_debug_bloom is NOT declared here - see comment above g_lights.
extern bool g_debug_lightmap;
extern bool g_debug_lights;

// ============= Pipeline entry points =============
// Only the four functions render_world() actually calls from outside this cluster are
// declared (non-static) below. Everything else that used to live in this range of main.cpp
// (light_attenuation, get_module_light, compute_ambient_light, get_natural_light_color,
// collect_lights, compute_shadow, world_to_lightmap_index, add_light_to_lightmap,
// blur_lightmap_pass, blur_lightmap, extract_bloom, blur_bloom, and the smoothstep() helper)
// is only ever called from within compute_lightmap()/sample_lightmap() themselves or from
// each other - grep confirms no call sites outside this cluster - so all of it stays static
// in lighting.cpp, same pattern as get_minimap_color() in minimap.cpp. compute_vignette() is
// grep-confirmed dead code (defined, never called anywhere in the codebase before this
// stage) - it stays static too, preserved as-is per the plan's "no behavior changes" rule.
void compute_lightmap();
void sample_lightmap(float world_x, float world_z, float& r, float& g, float& b);
float compute_depth_factor(float tile_height, float player_height);
void apply_color_grading(float& r, float& g, float& b);
