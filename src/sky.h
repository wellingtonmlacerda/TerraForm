#pragma once

// ============= Ceu Alienigena (esferas reais + parallax) =============
// Extracted verbatim from main.cpp (original lines ~1329-1755): the SkyPalette struct, the
// day/night sky palette computation, and the full sky renderer (gradient dome, stars,
// nebula, clouds, sun/moons as lit spheres, shooting stars) - render_alien_sky() ties all
// of it together and is the one entry point main.cpp's render_world() calls.
//
// SkyPalette needs a complete definition here (not just a forward declaration): main.cpp's
// render_world() (out of scope for this stage, untouched) declares its own
// "SkyPalette sky_palette = compute_sky_palette(...)" local and reads its hz_*/zn_* fields
// directly to pick the GL clear color - same "owner keeps the definition" rule used for
// Module/ModuleStatus in modules_building.h.
struct SkyPalette {
    float hz_r = 0.0f, hz_g = 0.0f, hz_b = 0.0f;
    float zn_r = 0.0f, zn_g = 0.0f, zn_b = 0.0f;
};

// compute_sky_palette()/render_alien_sky() are called directly by main.cpp's render_world()
// (out of scope for this stage, untouched), so both stay non-static and are declared here.
// update_shooting_stars() was already non-static before this stage (modules_building.cpp's
// update_modules() calls it from another translation unit); it is declared here now that
// this header is its proper home, but modules_building.cpp keeps working unchanged either
// way since it already carries its own matching forward declaration.
//
// hash01() and the rest of the layer renderers (render_sky_gradient_dome,
// render_billboard_disc, render_lit_sphere, render_star_layer, render_nebula_layer,
// render_cloud_layer, render_shooting_stars) are NOT declared here: grep confirms every call
// site is inside render_alien_sky() itself (or one of these helpers calling another), all in
// this same cluster, so they stay static in sky.cpp - same pattern as get_minimap_color() in
// minimap.cpp.
SkyPalette compute_sky_palette(float day_phase, float atmos_factor);
void update_shooting_stars(float dt, float day_phase);
// ground_y: altura do terreno sob o jogador agora (g_player.ground_height, main.cpp) - usada
// so' pelo pano de fundo de montanhas distantes (ver render_distant_mountains em sky.cpp)
// pra manter a silhueta grudada no nivel do chao, nao na altitude de voo da camera (cam_y) -
// senao as montanhas "subiam" junto com o jogador ao voar, lendo como flutuando no ceu em
// vez de recuar corretamente pra baixo no campo de visao (como um relevo distante de verdade
// faria visto de cima).
void render_alien_sky(float cam_x, float cam_y, float cam_z, float ground_y, float day_phase, float atmos_factor);

// ============= LUZ DA LUA (fonte unica de verdade) =============
// As duas luas eram, ate aqui, APENAS geometria de ceu: render_alien_sky as posicionava e desenhava,
// e o pipeline de iluminacao (lighting.cpp) nao sabia que existiam. O ambiente noturno era o valor
// fixo LightingSettings::ambient_min, porque compute_daylight() = max(0, sin(...)) e' exatamente
// ZERO durante metade do ciclo - dai a noite renderizar praticamente preta mesmo com a Lua no ceu.
//
// sky_moon_state() devolve a orbita das duas luas AGORA. render_alien_sky() usa esta mesma funcao
// pra POSICIONAR as luas e lighting.cpp usa pra ILUMINAR: se cada lado calculasse a orbita por
// conta, a luz poderia vir de uma lua que nao esta no ceu. Uma funcao, dois consumidores.
struct MoonState {
    float az1, el1;   // azimute e elevacao (radianos) da lua maior
    float az2, el2;   // idem da lua menor
};
MoonState sky_moon_state();

// Contribuicao de luz combinada das duas luas, 0..1, ponderada por elevacao e porte. NAO inclui o
// fator de noite: quem multiplica por "esta escuro" e' compute_ambient_light(), que tambem conhece
// o sol. Assim esta funcao responde so' "quanta luz de lua ha disponivel".
float sky_moonlight();

// Direcao HORIZONTAL de onde vem a luz da lua dominante (a de maior contribuicao), em radianos.
// Usada por lighting.cpp pra deslocar a fonte de luz lunar e produzir um lado iluminado / sombra
// projetada - a unica nocao de direcionalidade que o pipeline 2D deste motor consegue.
float sky_moonlight_azimuth();
