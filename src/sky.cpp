#include "raylib_platform.h"
#include "sky.h"

#include "math_core.h"          // Vec3, kPi, clamp01, smoothstep01, compute_daylight, compute_night_alpha
#include "noise.h"              // lerp, perlin
#include "camera.h"             // GameCamera, g_camera
#include "config_types.h"       // SkyConfig (type of g_sky_cfg below)
#include "world.h"              // kWorldWidth/kWorldHeight (ancoragem do ceu, ver sky_anchor)
#include "items_particles.h"    // ShootingStar (type only - g_shooting_stars itself is NOT
                                 // declared extern there, see comment below)
#include "game_state.h"         // rng_next_u32, rng_next_f01
#include "render_primitives.h"  // g_frame_fog (glDisable(GL_FOG) replacement)

#include <algorithm>
#include <cmath>
#include <vector>

// Globais de estado de jogo ainda definidas em main.cpp (dono continua sendo main.cpp - nao
// fazem parte desta etapa de extracao). g_day_time/g_sky_cfg ja eram nao-static em main.cpp
// (g_day_time desde o estagio modules_building; g_sky_cfg desde sempre, config_io.cpp ja usa
// esta mesma extern) - so precisamos da declaracao aqui tambem, mesmo padrao de
// g_physics_cfg em camera.cpp / g_terrain_cfg em world.cpp. g_shooting_stars (o vetor,
// std::vector<ShootingStar>) ja era nao-static em main.cpp antes desta etapa (perdeu
// "static" para save_load.cpp's load_game()), mas nenhum header ainda o declarava extern -
// items_particles.h so define o tipo ShootingStar, nao a variavel. Precisamos da nossa
// propria declaracao aqui, mesmo padrao das demais globais desta secao.
extern float g_day_time;
extern SkyConfig g_sky_cfg;
extern std::vector<ShootingStar> g_shooting_stars;

// kDayLength agora vem de game_state.h (era uma copia local aqui).

// ============= ALIEN SKY SYSTEM (esferas reais + parallax) =============

static float hash01(float v) {
    float h = std::sin(v * 12.9898f + 78.233f) * 43758.5453f;
    return std::fmod(std::fabs(h), 1.0f);
}

// ============= LUZ DA LUA =============
// Ver a nota longa em sky.h. A orbita das duas luas vive AQUI e em nenhum outro lugar:
// render_alien_sky() chama esta funcao pra desenhar, lighting.cpp chama pra iluminar.
MoonState sky_moon_state() {
    float sky_days = g_day_time / kDayLength;
    float moon_phase = sky_days * std::max(0.0f, g_sky_cfg.moon_orbit_speed) * 0.08f;
    float moon2_phase = sky_days * std::max(0.0f, g_sky_cfg.moon2_orbit_speed) * 0.10f;
    float a1 = moon_phase * 2.0f * kPi + 1.1f;
    float a2 = moon2_phase * 2.0f * kPi + 2.7f;
    MoonState m;
    m.az1 = 1.7f + std::sin(a1) * 0.75f;
    m.el1 = 0.38f + std::sin(a1 * 0.83f) * 0.15f;
    m.az2 = 2.5f + std::sin(a2) * 0.92f;
    m.el2 = 0.46f + std::sin(a2 * 0.71f) * 0.13f;
    return m;
}

float sky_moonlight() {
    MoonState m = sky_moon_state();
    // sin(elevacao) = quanto da luz chega no chao (lei do cosseno). Ambas as elevacoes ficam em
    // 0.23..0.59 rad neste ceu (as luas nunca se poem, por construcao da orbita), entao a variacao
    // e real mas suave - a noite tem noites mais e menos claras, sem apagar de vez.
    float l1 = clamp01(std::sin(std::max(0.0f, m.el1)));
    float l2 = clamp01(std::sin(std::max(0.0f, m.el2)));
    // Peso por porte: a lua maior (raio 36, dist 940) domina; a menor (raio 22, dist 1010)
    // contribui menos. Razao de area aparente ~0.43 - e' de onde vem o 0.45.
    return clamp01((l1 + l2 * 0.45f) * 0.62f);
}

float sky_moonlight_azimuth() {
    MoonState m = sky_moon_state();
    float l1 = clamp01(std::sin(std::max(0.0f, m.el1)));
    float l2 = clamp01(std::sin(std::max(0.0f, m.el2))) * 0.45f;
    return (l1 >= l2) ? m.az1 : m.az2;
}

SkyPalette compute_sky_palette(float day_phase, float atmos_factor) {
    float daylight = compute_daylight(day_phase);
    float night = compute_night_alpha(day_phase);
    float sun_warm = smoothstep01(0.05f, 0.45f, daylight) * (1.0f - smoothstep01(0.75f, 1.0f, daylight));
    float atmos = clamp01(atmos_factor);

    SkyPalette p{};
    float night_hz_r = 0.05f, night_hz_g = 0.06f, night_hz_b = 0.11f;
    float night_zn_r = 0.02f, night_zn_g = 0.03f, night_zn_b = 0.07f;
    float day_hz_r = lerp(0.48f, 0.36f, atmos);
    float day_hz_g = lerp(0.37f, 0.52f, atmos);
    float day_hz_b = lerp(0.25f, 0.70f, atmos);
    float day_zn_r = lerp(0.18f, 0.19f, atmos);
    float day_zn_g = lerp(0.23f, 0.38f, atmos);
    float day_zn_b = lerp(0.35f, 0.74f, atmos);

    p.hz_r = lerp(night_hz_r, day_hz_r, daylight);
    p.hz_g = lerp(night_hz_g, day_hz_g, daylight);
    p.hz_b = lerp(night_hz_b, day_hz_b, daylight);
    p.zn_r = lerp(night_zn_r, day_zn_r, daylight);
    p.zn_g = lerp(night_zn_g, day_zn_g, daylight);
    p.zn_b = lerp(night_zn_b, day_zn_b, daylight);

    p.hz_r += sun_warm * g_sky_cfg.atmosphere_horizon_boost * 0.32f;
    p.hz_g += sun_warm * g_sky_cfg.atmosphere_horizon_boost * 0.16f;
    p.hz_b += sun_warm * g_sky_cfg.atmosphere_horizon_boost * 0.07f;

    p.zn_r += daylight * g_sky_cfg.atmosphere_zenith_boost * 0.05f;
    p.zn_g += daylight * g_sky_cfg.atmosphere_zenith_boost * 0.11f;
    p.zn_b += daylight * g_sky_cfg.atmosphere_zenith_boost * 0.18f;

    // Horizon fade at night for more depth.
    float fade = night * g_sky_cfg.horizon_fade;
    p.hz_r = lerp(p.hz_r, p.zn_r, fade * 0.45f);
    p.hz_g = lerp(p.hz_g, p.zn_g, fade * 0.45f);
    p.hz_b = lerp(p.hz_b, p.zn_b, fade * 0.45f);

    p.hz_r = clamp01(p.hz_r); p.hz_g = clamp01(p.hz_g); p.hz_b = clamp01(p.hz_b);
    p.zn_r = clamp01(p.zn_r); p.zn_g = clamp01(p.zn_g); p.zn_b = clamp01(p.zn_b);
    return p;
}

// GL_TRIANGLE_STRIP decomposition: buffer the previous (outer,inner) vertex pair, then from
// the 2nd angle step onward emit 2 explicit RL_TRIANGLES matching the exact vertex order a
// GL_TRIANGLE_STRIP of this same vertex sequence would have produced - see the migration
// plan's "GL_TRIANGLE_STRIP -> same pair-buffering, decomposed into 2 explicit RL_TRIANGLES
// per step" guidance.
static void render_sky_gradient_dome(float cam_x, float cam_z, const SkyPalette& p) {
    constexpr int kRings = 18;
    constexpr int kSegs = 64;
    constexpr float kRadius = 1850.0f;
    constexpr float kBaseY = -120.0f;

    rlSetTexture(0);
    rlDisableDepthTest();

    for (int ring = 0; ring < kRings; ++ring) {
        float t0 = (float)ring / (float)kRings;
        float t1 = (float)(ring + 1) / (float)kRings;
        float e0 = t0 * (kPi * 0.5f);
        float e1 = t1 * (kPi * 0.5f);
        float y0 = kBaseY + std::sin(e0) * kRadius;
        float y1 = kBaseY + std::sin(e1) * kRadius;
        float r0 = std::cos(e0) * kRadius;
        float r1 = std::cos(e1) * kRadius;

        float c0 = smoothstep01(0.0f, 1.0f, t0);
        float c1 = smoothstep01(0.0f, 1.0f, t1);
        float c0r = lerp(p.hz_r, p.zn_r, c0);
        float c0g = lerp(p.hz_g, p.zn_g, c0);
        float c0b = lerp(p.hz_b, p.zn_b, c0);
        float c1r = lerp(p.hz_r, p.zn_r, c1);
        float c1g = lerp(p.hz_g, p.zn_g, c1);
        float c1b = lerp(p.hz_b, p.zn_b, c1);

        rlBegin(RL_TRIANGLES);
        float prev_ox = 0, prev_oy = 0, prev_oz = 0, prev_or = 0, prev_og = 0, prev_ob = 0;
        float prev_ix = 0, prev_iy = 0, prev_iz = 0, prev_ir = 0, prev_ig = 0, prev_ib = 0;
        bool have_prev = false;
        for (int i = 0; i <= kSegs; ++i) {
            float a = (float)i / (float)kSegs * 2.0f * kPi;
            float ca = std::cos(a);
            float sa = std::sin(a);
            float ox = cam_x + ca * r1, oy = y1, oz = cam_z + sa * r1; // outer (era c1)
            float ix = cam_x + ca * r0, iy = y0, iz = cam_z + sa * r0; // inner (era c0)

            if (have_prev) {
                // Triangulo 1: outer_prev, inner_prev, outer_cur
                rlColor4f(prev_or, prev_og, prev_ob, 1.0f); rlVertex3f(prev_ox, prev_oy, prev_oz);
                rlColor4f(prev_ir, prev_ig, prev_ib, 1.0f); rlVertex3f(prev_ix, prev_iy, prev_iz);
                rlColor4f(c1r, c1g, c1b, 1.0f); rlVertex3f(ox, oy, oz);
                // Triangulo 2: inner_prev, outer_cur, inner_cur
                rlColor4f(prev_ir, prev_ig, prev_ib, 1.0f); rlVertex3f(prev_ix, prev_iy, prev_iz);
                rlColor4f(c1r, c1g, c1b, 1.0f); rlVertex3f(ox, oy, oz);
                rlColor4f(c0r, c0g, c0b, 1.0f); rlVertex3f(ix, iy, iz);
            }
            prev_ox = ox; prev_oy = oy; prev_oz = oz; prev_or = c1r; prev_og = c1g; prev_ob = c1b;
            prev_ix = ix; prev_iy = iy; prev_iz = iz; prev_ir = c0r; prev_ig = c0g; prev_ib = c0b;
            have_prev = true;
        }
        rlEnd();
    }
}

// GL_TRIANGLE_FAN decomposition: buffer the fan's vertices (position+color) first, then
// re-emit as explicit RL_TRIANGLES triples (center, v[i], v[i+1]) for each consecutive pair -
// per the migration plan.
static void render_billboard_disc(const Vec3& center, float radius, float r, float g, float b, float a, int segments = 28) {
    Vec3 to_cam = vec3_sub(g_camera.position, center);
    if (vec3_length(to_cam) < 0.001f) to_cam = {0.0f, 0.0f, 1.0f};
    to_cam = vec3_normalize(to_cam);
    Vec3 up = {0.0f, 1.0f, 0.0f};
    Vec3 right = vec3_cross(up, to_cam);
    if (vec3_length(right) < 0.001f) right = {1.0f, 0.0f, 0.0f};
    right = vec3_normalize(right);
    Vec3 disc_up = vec3_normalize(vec3_cross(to_cam, right));

    std::vector<Vec3> rim(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        float ang = (float)i / (float)segments * 2.0f * kPi;
        float ca = std::cos(ang);
        float sa = std::sin(ang);
        rim[i] = vec3_add(center, vec3_add(vec3_scale(right, ca * radius), vec3_scale(disc_up, sa * radius)));
    }

    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < segments; ++i) {
        rlColor4f(r, g, b, a);
        rlVertex3f(center.x, center.y, center.z);
        rlColor4f(r, g, b, 0.0f);
        rlVertex3f(rim[i].x, rim[i].y, rim[i].z);
        rlColor4f(r, g, b, 0.0f);
        rlVertex3f(rim[i + 1].x, rim[i + 1].y, rim[i + 1].z);
    }
    rlEnd();
}

// GL_QUAD_STRIP decomposition: buffer the previous (top,bottom) vertex pair, then from the
// 2nd longitude step onward emit an RL_QUADS block using (prev_pair, current_pair) in the
// exact same vertex order a GL_QUAD_STRIP of this sequence would have produced - per the
// migration plan.
static void render_lit_sphere(const Vec3& center, float radius, const Vec3& light_dir, const Vec3& view_pos,
                              float base_r, float base_g, float base_b, float alpha,
                              float ambient, float diffuse_mul, float spec_mul,
                              float noise_freq = 0.0f, float noise_amp = 0.0f,
                              int lat_seg = 18, int lon_seg = 24,
                              // Bandas de nuvem tipo gigante gasoso (Jupiter/Polyphemus):
                              // opcional, so ativa se band_freq>0 - mistura entre a cor base
                              // e band_r/g/b conforme a latitude do vertice (yy, ja calculado
                              // pelo mesh abaixo), com turbulencia de baixa frequencia por
                              // cima pra as faixas nao ficarem geometricas demais. Default 0
                              // preserva o comportamento antigo pro sol/luas.
                              float band_freq = 0.0f, float band_amp = 0.0f,
                              float band_r = 0.0f, float band_g = 0.0f, float band_b = 0.0f) {
    Vec3 ldir = vec3_normalize(light_dir);
    for (int lat = 0; lat < lat_seg; ++lat) {
        float v0 = -0.5f + (float)lat / (float)lat_seg;
        float v1 = -0.5f + (float)(lat + 1) / (float)lat_seg;
        float p0 = v0 * kPi;
        float p1 = v1 * kPi;
        float y0 = std::sin(p0);
        float y1 = std::sin(p1);
        float r0 = std::cos(p0);
        float r1 = std::cos(p1);

        rlBegin(RL_QUADS);
        Vec3 prev_top{}, prev_bot{};
        float prev_top_c[4] = {0,0,0,0}, prev_bot_c[4] = {0,0,0,0};
        bool have_prev = false;
        for (int lon = 0; lon <= lon_seg; ++lon) {
            float u = (float)lon / (float)lon_seg * 2.0f * kPi;
            float cu = std::cos(u);
            float su = std::sin(u);

            Vec3 cur_top{}, cur_bot{};
            float cur_top_c[4], cur_bot_c[4];

            auto compute = [&](float rr, float yy, Vec3& out_p, float out_c[4]) {
                Vec3 n = vec3_normalize({cu * rr, yy, su * rr});
                Vec3 p = vec3_add(center, vec3_scale(n, radius));
                float ndl = std::max(0.0f, vec3_dot(n, ldir));
                Vec3 vdir = vec3_normalize(vec3_sub(view_pos, p));
                Vec3 h = vec3_normalize(vec3_add(ldir, vdir));
                float spec = std::pow(std::max(0.0f, vec3_dot(n, h)), 26.0f) * spec_mul;
                float nvar = 0.0f;
                if (noise_freq > 0.00001f) {
                    nvar = (perlin(p.x * noise_freq + 133.0f, p.z * noise_freq + 617.0f) - 0.5f) * noise_amp;
                }
                float lit = std::max(0.0f, ambient + ndl * diffuse_mul + nvar);

                float cr = base_r, cg = base_g, cb = base_b;
                if (band_freq > 0.00001f) {
                    float band_wave = std::sin(yy * band_freq * kPi) * 0.5f + 0.5f;
                    float turb = perlin(p.x * 0.006f + 1300.0f, p.z * 0.006f + 1300.0f);
                    float band_t = clamp01(band_wave * 0.7f + turb * 0.3f) * band_amp;
                    cr = lerp(base_r, band_r, band_t);
                    cg = lerp(base_g, band_g, band_t);
                    cb = lerp(base_b, band_b, band_t);
                }

                out_c[0] = clamp01(cr * lit + spec);
                out_c[1] = clamp01(cg * lit + spec * 0.95f);
                out_c[2] = clamp01(cb * lit + spec * 0.90f);
                out_c[3] = alpha;
                out_p = p;
            };

            compute(r1, y1, cur_top, cur_top_c);
            compute(r0, y0, cur_bot, cur_bot_c);

            if (have_prev) {
                rlColor4f(prev_top_c[0], prev_top_c[1], prev_top_c[2], prev_top_c[3]); rlVertex3f(prev_top.x, prev_top.y, prev_top.z);
                rlColor4f(prev_bot_c[0], prev_bot_c[1], prev_bot_c[2], prev_bot_c[3]); rlVertex3f(prev_bot.x, prev_bot.y, prev_bot.z);
                rlColor4f(cur_bot_c[0], cur_bot_c[1], cur_bot_c[2], cur_bot_c[3]); rlVertex3f(cur_bot.x, cur_bot.y, cur_bot.z);
                rlColor4f(cur_top_c[0], cur_top_c[1], cur_top_c[2], cur_top_c[3]); rlVertex3f(cur_top.x, cur_top.y, cur_top.z);
            }
            prev_top = cur_top; prev_top_c[0]=cur_top_c[0]; prev_top_c[1]=cur_top_c[1]; prev_top_c[2]=cur_top_c[2]; prev_top_c[3]=cur_top_c[3];
            prev_bot = cur_bot; prev_bot_c[0]=cur_bot_c[0]; prev_bot_c[1]=cur_bot_c[1]; prev_bot_c[2]=cur_bot_c[2]; prev_bot_c[3]=cur_bot_c[3];
            have_prev = true;
        }
        rlEnd();
    }
}

// Anel planetario (tipo Saturno) - nao existia nenhuma geometria de anel antes desta mudanca
// (so discos billboard sempre de frente pra camera, usados como halo de brilho). Este e um
// anulus PLANO com eixo fixo (inclinado por tilt_deg em torno do eixo X, nao billboard -
// senao pareceria sempre "de frente", nunca um disco visto de lado). Desenhado sem teste de
// profundidade (igual ao resto do ceu, ver rlDisableDepthTest() em render_alien_sky) - o anel
// inteiro e pintado depois da esfera do planeta, entao visualmente fica sempre "por cima",
// uma simplificacao aceitavel pra um ceu estilizado (sem oclusao correta do lado de tras).
// Alpha decai da borda externa (cheio) pra interna (mais fraco) pra parecer um leque de
// particulas/poeira em vez de um disco solido uniforme.
// Anel do planeta, desenhado em METADES SEPARADAS.
//
// `half`: 0 = tudo (compatibilidade), 1 = so a metade DISTANTE da camera, 2 = so a metade PROXIMA.
//
// Por que dividir: o ceu inteiro e' pintado de tras pra frente com o teste de profundidade
// DESLIGADO (render_alien_sky). O anel era um passe unico depois do corpo do planeta, entao a parte
// dele que passa POR TRAS do planeta era pintada por cima do disco - o anel atravessava o planeta
// como se fosse transparente. Bug relatado: "os aneis deveriam sumir ao passar por tras dele".
//
// Ligar o teste de profundidade so' pra esses dois objetos resolveria, mas mexer no estado de
// profundidade no meio de um passe que existe justamente por nao ter profundidade e' arriscado (o
// domo, as estrelas e a nebulosa dependem de nao ter). Dividir em metades da o mesmo resultado sem
// tocar em estado nenhum: a metade distante e' desenhada ANTES do corpo (o disco a cobre onde se
// sobrepoem, e ela continua visivel fora da silhueta, que e' o certo) e a metade proxima DEPOIS.
//
// A classificacao e' por profundidade ao longo do eixo camera->planeta: um ponto do anel esta atras
// quando a projecao dele nesse eixo passa da projecao do centro do planeta.
static void render_planet_ring(const Vec3& center, float inner_r, float outer_r, float tilt_deg,
                               float r, float g, float b, float alpha,
                               const Vec3& cam_pos, int half, int segments = 120) {
    float tilt = tilt_deg * (kPi / 180.0f);
    float ct = std::cos(tilt), st = std::sin(tilt);
    auto ring_point = [&](float radius, float angle) -> Vec3 {
        float x = radius * std::cos(angle);
        float z0 = radius * std::sin(angle);
        return vec3_add(center, Vec3{x, z0 * st, z0 * ct});
    };
    Vec3 axis = vec3_sub(center, cam_pos);
    float center_depth = vec3_length(axis);
    if (center_depth < 1e-4f) return;
    axis = vec3_scale(axis, 1.0f / center_depth);
    // Um segmento pertence a metade distante quando o MEIO dele esta mais longe que o centro do
    // planeta ao longo do eixo de visao.
    auto seg_is_far = [&](float a0, float a1) -> bool {
        float am = (a0 + a1) * 0.5f;
        Vec3 mid = ring_point((inner_r + outer_r) * 0.5f, am);
        return vec3_dot(vec3_sub(mid, cam_pos), axis) > center_depth;
    };

    rlBegin(RL_TRIANGLES);
    Vec3 prev_in{}, prev_out{};
    float prev_a = 0.0f;
    bool have_prev = false;
    for (int i = 0; i <= segments; ++i) {
        float a = (float)i / (float)segments * 2.0f * kPi;
        Vec3 pin = ring_point(inner_r, a);
        Vec3 pout = ring_point(outer_r, a);
        if (have_prev && (half == 0 || (seg_is_far(prev_a, a) ? (half == 1) : (half == 2)))) {
            rlColor4f(r, g, b, alpha * 0.20f); rlVertex3f(prev_in.x, prev_in.y, prev_in.z);
            rlColor4f(r, g, b, alpha);         rlVertex3f(prev_out.x, prev_out.y, prev_out.z);
            rlColor4f(r, g, b, alpha);         rlVertex3f(pout.x, pout.y, pout.z);

            rlColor4f(r, g, b, alpha * 0.20f); rlVertex3f(prev_in.x, prev_in.y, prev_in.z);
            rlColor4f(r, g, b, alpha);         rlVertex3f(pout.x, pout.y, pout.z);
            rlColor4f(r, g, b, alpha * 0.20f); rlVertex3f(pin.x, pin.y, pin.z);
        }
        prev_in = pin; prev_out = pout; prev_a = a; have_prev = true;
    }
    rlEnd();
}

// GL_POINTS decomposition: each star becomes a tiny camera-facing RL_QUADS billboard quad
// sized to roughly match the original glPointSize(1.4f) - per the migration plan.
static void render_point_billboard(float x, float y, float z, float half_size, float r, float g, float b, float a) {
    Vec3 center{x, y, z};
    Vec3 to_cam = vec3_sub(g_camera.position, center);
    if (vec3_length(to_cam) < 0.001f) to_cam = {0.0f, 0.0f, 1.0f};
    to_cam = vec3_normalize(to_cam);
    Vec3 up = {0.0f, 1.0f, 0.0f};
    Vec3 right = vec3_cross(up, to_cam);
    if (vec3_length(right) < 0.001f) right = {1.0f, 0.0f, 0.0f};
    right = vec3_normalize(right);
    Vec3 disc_up = vec3_normalize(vec3_cross(to_cam, right));

    Vec3 p0 = vec3_add(center, vec3_add(vec3_scale(right, -half_size), vec3_scale(disc_up, -half_size)));
    Vec3 p1 = vec3_add(center, vec3_add(vec3_scale(right, half_size), vec3_scale(disc_up, -half_size)));
    Vec3 p2 = vec3_add(center, vec3_add(vec3_scale(right, half_size), vec3_scale(disc_up, half_size)));
    Vec3 p3 = vec3_add(center, vec3_add(vec3_scale(right, -half_size), vec3_scale(disc_up, half_size)));

    rlColor4f(r, g, b, a);
    rlVertex3f(p0.x, p0.y, p0.z);
    rlVertex3f(p1.x, p1.y, p1.z);
    rlVertex3f(p2.x, p2.y, p2.z);
    rlVertex3f(p3.x, p3.y, p3.z);
}

// ============= ANCORAGEM DO CEU =============
// Todo corpo e toda camada do ceu era ancorada em `cam * parallax`. A intencao era dar um parallax
// leve (o ceu se desloca um pouquinho quando o jogador anda), mas o efeito real e' o OPOSTO: com
// parallax 0.028 o corpo acompanha a camera a 2.8% da velocidade dela, ou seja fica praticamente
// PREGADO NA ORIGEM DO MUNDO. Num mapa de 3072x1536, a 1472 tiles da origem o planeta ficava 1428
// unidades atras da camera.
//
// Medido, camera em (1472, 19, 757):
//   planeta em (-640, 478, 853)  <- x NEGATIVO, do outro lado do mapa
//   distancia camera->planeta = 2163, plano de corte (far) = 2200
//   borda distante do planeta  = 2463  -> CORTADA
// Com a distancia parada em cima do limite, qualquer giro de camera fazia o planeta cruzar o far
// plane e piscar - o bug de "o planeta some quando viro a camera".
//
// A correcao ancora na CAMERA e desloca de leve rumo ao CENTRO DO MUNDO. Assim:
//   - a distancia camera->corpo fica constante (= `dist`), longe do plano de corte;
//   - ainda existe parallax de verdade, mas LIMITADO: no centro do mapa o deslocamento e' zero e na
//     borda e' parallax * meia-extensao (0.028 * 1536 = ~43 unidades) em vez de crescer sem teto.
static float sky_anchor(float cam, float parallax, float world_center) {
    return cam + (world_center - cam) * parallax;
}
static float sky_anchor_x(float cam_x, float parallax) {
    return sky_anchor(cam_x, parallax, (float)kWorldWidth * 0.5f);
}
static float sky_anchor_z(float cam_z, float parallax) {
    return sky_anchor(cam_z, parallax, (float)kWorldHeight * 0.5f);
}

static void render_star_layer(float cam_x, float cam_z, float day_phase, float night_alpha) {
    if (night_alpha < 0.03f) return;
    int star_count = (int)std::lround(g_sky_cfg.stars_density);
    star_count = std::clamp(star_count, 120, 4000);

    float origin_x = sky_anchor_x(cam_x, g_sky_cfg.stars_parallax);
    float origin_z = sky_anchor_z(cam_z, g_sky_cfg.stars_parallax);

    constexpr float kStarHalfSize = 0.9f; // roughly matches the old glPointSize(1.4f) look
    rlBegin(RL_QUADS);
    for (int i = 0; i < star_count; ++i) {
        float u = hash01((float)i * 1.11f + 13.0f);
        float v = hash01((float)i * 1.71f + 31.0f);
        float w = hash01((float)i * 2.47f + 79.0f);
        float theta = u * 2.0f * kPi;
        float y01 = 0.22f + v * 0.76f;
        float rr = std::sqrt(std::max(0.0f, 1.0f - y01 * y01));
        float dist = 1300.0f + w * 900.0f;
        float sx = origin_x + std::cos(theta) * rr * dist;
        float sy = 190.0f + y01 * 980.0f;
        float sz = origin_z + std::sin(theta) * rr * dist;
        // Antes usava day_phase (muda devagar ao longo do "dia" inteiro, ~240s) pro
        // piscar - a estrela levava dezenas de segundos pra completar 1 ciclo, lendo como
        // parada. g_day_time (segundos reais decorridos) com uma velocidade por estrela
        // (via w, cada uma pisca num ritmo levemente diferente) da o cintilar de verdade.
        float twinkle_speed = 1.4f + w * 2.6f;
        float twinkle = 0.5f + 0.5f * std::sin(g_day_time * twinkle_speed + (float)i * 12.9898f);
        bool bright_star = (i % 31) == 0; // umas poucas estrelas "de destaque", maiores/mais fixas
        float a = night_alpha * (bright_star ? (0.85f + 0.15f * twinkle) : (0.35f + 0.65f * twinkle)) * 0.95f;
        float sr = 0.82f + 0.16f * u;
        float sg = 0.82f + 0.16f * v;
        float sb = 0.90f + 0.10f * w;
        float star_size = bright_star ? kStarHalfSize * 1.8f : kStarHalfSize;
        render_point_billboard(sx, sy, sz, star_size, sr, sg, sb, a);
    }
    rlEnd();
}

static void render_nebula_layer(float cam_x, float cam_z, float day_phase, float night_alpha) {
    float alpha = night_alpha * g_sky_cfg.nebula_alpha;
    if (alpha < 0.01f) return;
    float origin_x = sky_anchor_x(cam_x, g_sky_cfg.nebula_parallax);
    float origin_z = sky_anchor_z(cam_z, g_sky_cfg.nebula_parallax);

    rlSetBlendMode(RL_BLEND_ADDITIVE);
    for (int i = 0; i < 5; ++i) {
        float u = hash01((float)i * 9.3f + 21.0f);
        float v = hash01((float)i * 17.7f + 55.0f);
        float ang = day_phase * 0.35f + u * 2.0f * kPi;
        Vec3 c = {
            origin_x + std::cos(ang) * (900.0f + 420.0f * u),
            260.0f + 260.0f * v,
            origin_z + std::sin(ang) * (780.0f + 380.0f * v)
        };
        float rad = 220.0f + 170.0f * u;
        float nr = 0.30f + 0.30f * u;
        float ng = 0.18f + 0.28f * v;
        float nb = 0.42f + 0.32f * (1.0f - u);
        render_billboard_disc(c, rad, nr, ng, nb, alpha * (0.25f + 0.35f * v), 34);
    }
    rlSetBlendMode(RL_BLEND_ALPHA);
}

static void render_cloud_layer(float cam_x, float cam_z, float day_phase, float atmos_factor) {
    float alpha = g_sky_cfg.cloud_alpha * (0.35f + atmos_factor * 0.65f);
    if (alpha < 0.01f) return;
    float origin_x = sky_anchor_x(cam_x, g_sky_cfg.cloud_parallax);
    float origin_z = sky_anchor_z(cam_z, g_sky_cfg.cloud_parallax);

    rlSetBlendMode(RL_BLEND_ALPHA);
    for (int i = 0; i < 6; ++i) {
        float t = (float)i * 1.71f;
        float u = hash01(t + 17.0f);
        float v = hash01(t + 63.0f);
        float spin = day_phase * 1.8f + u * 2.0f * kPi;
        Vec3 c = {
            origin_x + std::cos(spin) * (460.0f + 380.0f * u),
            320.0f + 160.0f * v,
            origin_z + std::sin(spin) * (420.0f + 320.0f * v)
        };
        float rad = 130.0f + 110.0f * u;
        render_billboard_disc(c, rad, 0.88f, 0.90f, 0.94f, alpha * (0.35f + 0.30f * v), 30);
    }
}

void update_shooting_stars(float dt, float day_phase) {
    for (auto& s : g_shooting_stars) {
        s.life -= dt;
        s.offset = vec3_add(s.offset, vec3_scale(s.vel, dt));
    }
    g_shooting_stars.erase(
        std::remove_if(g_shooting_stars.begin(), g_shooting_stars.end(),
            [](const ShootingStar& s) { return s.life <= 0.0f; }),
        g_shooting_stars.end());

    float night_alpha = compute_night_alpha(day_phase);
    if (night_alpha < 0.55f) return;
    if (g_shooting_stars.size() >= 4) return;

    float spawn_rate = 0.05f + 0.12f * (night_alpha - 0.55f);
    if (rng_next_f01() > dt * spawn_rate) return;

    ShootingStar s;
    s.max_life = 0.75f + rng_next_f01() * 0.55f;
    s.life = s.max_life;
    s.length = 120.0f + rng_next_f01() * 180.0f;
    float start_radius = 1100.0f + rng_next_f01() * 450.0f;
    float start_ang = rng_next_f01() * 2.0f * kPi;
    s.offset.x = std::cos(start_ang) * start_radius;
    s.offset.z = std::sin(start_ang) * start_radius;
    s.offset.y = 420.0f + rng_next_f01() * 520.0f;
    float dir_ang = start_ang + (0.90f + rng_next_f01() * 0.60f) * (((rng_next_u32() & 1u) != 0u) ? 1.0f : -1.0f);
    float spd = 650.0f + rng_next_f01() * 450.0f;
    s.vel.x = std::cos(dir_ang) * spd;
    s.vel.z = std::sin(dir_ang) * spd;
    s.vel.y = -(120.0f + rng_next_f01() * 260.0f);
    float tint = 0.86f + rng_next_f01() * 0.14f;
    s.r = tint;
    s.g = tint;
    s.b = 0.95f + rng_next_f01() * 0.05f;
    g_shooting_stars.push_back(s);
}

static void render_shooting_stars(float cam_x, float cam_y, float cam_z, float night_alpha) {
    if (night_alpha < 0.20f) return;
    if (g_shooting_stars.empty()) return;

    rlDisableDepthTest();
    rlSetBlendMode(RL_BLEND_ADDITIVE);
    rlSetLineWidth(2.0f);
    rlBegin(RL_LINES);
    for (const auto& s : g_shooting_stars) {
        float progress = 1.0f - (s.life / std::max(0.001f, s.max_life));
        float fade_in = smoothstep01(0.00f, 0.12f, progress);
        float fade_out = 1.0f - smoothstep01(0.70f, 1.00f, progress);
        float a = night_alpha * fade_in * fade_out;
        if (a <= 0.01f) continue;
        Vec3 head = {cam_x + s.offset.x, s.offset.y, cam_z + s.offset.z};
        Vec3 dir = vec3_normalize(s.vel);
        Vec3 tail = vec3_sub(head, vec3_scale(dir, s.length));
        rlColor4f(s.r, s.g, s.b, a);
        rlVertex3f(tail.x, tail.y, tail.z);
        rlVertex3f(head.x, head.y, head.z);
    }
    rlEnd();
    rlSetLineWidth(1.0f);
    rlSetBlendMode(RL_BLEND_ALPHA);
}

// Funcao principal para renderizar todo o ceu alienigena
// Montanhas distantes fixas no horizonte - pedido do jogador: ao voar alto, o plano de fundo
// (terreno real, culled/enevoado a partir de view_radius, ver main.cpp) lia como um "oceano"
// liso/escuro sem relevo. Isso e' um pano de fundo puramente decorativo (sem colisao, sem
// ligacao com o heightmap de verdade), no MESMO estilo/raio de parallax=1 da cupula do ceu
// (render_sky_gradient_dome acima) - segue a camera em X/Z (nunca se aproxima, sempre "no
// horizonte") mas o perfil da silhueta depende so' do angulo absoluto no mundo, entao fica
// sempre no mesmo lugar relativo ao horizonte (as "montanhas fixas" pedidas), nao gira com a
// camera nem muda de forma ao voar/andar.
static void render_distant_mountains(float cam_x, float cam_z, float ground_y, const SkyPalette& p) {
    constexpr int kSegs = 96;
    constexpr float kRadius = 780.0f;  // alem do teto real de view_radius (520) - nunca alcancavel
    // A base segue a altura do TERRENO sob o jogador (ground_y), NAO a altitude da camera -
    // um relevo distante de verdade fica grudado no chao; ao voar, ele deve recuar/afundar no
    // campo de visao (comportamento esperado de olhar pra baixo de cima), nao subir junto com
    // a camera. A 1a tentativa usava cam_y (altura da camera) e piorava exatamente o bug
    // reportado: as montanhas "subiam" voando, lendo como se estivessem flutuando no ceu.
    float kBaseY = ground_y - 4.0f;

    // Silhueta um pouco mais escura que a cor do horizonte, pra ler como montanha contra o
    // ceu (perspectiva atmosferica simplificada: mais escura que o ceu, mas nao preta).
    float mr = p.hz_r * 0.50f;
    float mg = p.hz_g * 0.55f;
    float mb = p.hz_b * 0.62f;

    rlSetTexture(0);
    rlDisableDepthTest();
    rlBegin(RL_TRIANGLES);

    float prev_bx = 0, prev_bz = 0, prev_peak = 0;
    for (int i = 0; i <= kSegs; ++i) {
        float ang = (float)i / (float)kSegs * 2.0f * kPi;
        // Perfil determinado so pelo angulo absoluto (nao pela posicao/direcao da camera) -
        // varias frequencias de seno somadas pra parecer uma cordilheira serrilhada, nao uma
        // onda unica e lisa.
        float peak = 55.0f
            + 45.0f * (0.5f + 0.5f * std::sin(ang * 3.0f + 1.7f))
            + 25.0f * (0.5f + 0.5f * std::sin(ang * 7.0f + 4.1f))
            + 12.0f * (0.5f + 0.5f * std::sin(ang * 13.0f + 0.4f));
        float bx = cam_x + std::cos(ang) * kRadius;
        float bz = cam_z + std::sin(ang) * kRadius;

        if (i > 0) {
            rlColor4f(mr, mg, mb, 1.0f);
            rlVertex3f(prev_bx, kBaseY, prev_bz);
            rlVertex3f(prev_bx, kBaseY + prev_peak, prev_bz);
            rlVertex3f(bx, kBaseY + peak, bz);

            rlVertex3f(prev_bx, kBaseY, prev_bz);
            rlVertex3f(bx, kBaseY + peak, bz);
            rlVertex3f(bx, kBaseY, bz);
        }
        prev_bx = bx; prev_bz = bz; prev_peak = peak;
    }
    rlEnd();
}

void render_alien_sky(float cam_x, float cam_y, float cam_z, float ground_y, float day_phase, float atmos_factor) {
    float night_alpha = compute_night_alpha(day_phase);
    SkyPalette palette = compute_sky_palette(day_phase, atmos_factor);

    rlDisableDepthTest();
    g_frame_fog.enabled = false; // era glDisable(GL_FOG) - o ceu nao recebe fog de terreno.
    rlSetTexture(0);

    render_sky_gradient_dome(cam_x, cam_z, palette);
    render_star_layer(cam_x, cam_z, day_phase, night_alpha);
    render_nebula_layer(cam_x, cam_z, day_phase, night_alpha);

    Vec3 camera_ref = {cam_x, cam_y, cam_z};
    float sun_angle = day_phase * 2.0f * kPi - kPi * 0.5f;
    Vec3 sun_pos = {
        cam_x + std::cos(sun_angle) * g_sky_cfg.sun_distance,
        85.0f + std::sin(sun_angle) * 315.0f,
        cam_z - 200.0f + std::sin(sun_angle * 0.5f) * 100.0f
    };
    Vec3 sun_dir = vec3_normalize(vec3_sub(sun_pos, camera_ref));

    // Movimento muito lento dos astros (em "dias" de jogo), evitando deslocamento brusco no ceu.
    float sky_days = g_day_time / kDayLength;
    float planet_phase = sky_days * std::max(0.0f, g_sky_cfg.planet_orbit_speed) * 0.10f;
    float planet_orbit = planet_phase * 2.0f * kPi;
    float planet_az = 2.25f + std::sin(planet_orbit) * 0.35f;
    float planet_el = 0.30f + std::sin(planet_orbit * 0.43f + 0.9f) * 0.08f;
    auto body_from_spherical = [&](float az, float el, float dist, float parallax) -> Vec3 {
        float cos_el = std::cos(el);
        return {
            sky_anchor_x(cam_x, parallax) + std::cos(az) * cos_el * dist,
            70.0f + std::sin(el) * dist,
            sky_anchor_z(cam_z, parallax) + std::sin(az) * cos_el * dist
        };
    };
    Vec3 planet_pos = body_from_spherical(planet_az, planet_el, g_sky_cfg.planet_distance, g_sky_cfg.planet_parallax);

    // Nao permitir que o planeta principal cruze visualmente o disco do sol.
    Vec3 planet_dir = vec3_normalize(vec3_sub(planet_pos, camera_ref));
    if (vec3_dot(planet_dir, sun_dir) > 0.66f) {
        planet_az += (sun_dir.x >= 0.0f) ? -0.95f : 0.95f;
        planet_pos = body_from_spherical(planet_az, planet_el, g_sky_cfg.planet_distance, g_sky_cfg.planet_parallax);
    }
    // ================= GIGANTE GASOSO ESTILO POLYPHEMUS (Avatar) =================
    // O planeta era um disco de ~15 graus de arco (raio 160 a 1220 de distancia): visivel, mas nao
    // dominante. Na referencia de Pandora o gigante OCUPA o ceu - e' o elemento que faz o lugar
    // parecer outro mundo. Agora sao ~29 graus (raio 300 a 1150), mais quatro camadas que dao a
    // leitura de corpo planetario de verdade em vez de bola pintada:
    //
    //   1) HALO DE ATMOSFERA - casca aditiva um pouco maior que o disco, em ciano-esverdeado. E' o
    //      limbo iluminado por tras: sem ele o planeta tem borda de recorte, como adesivo.
    //   2) BANDAS + TURBULENCIA - ja existia (band_freq no render_lit_sphere), agora com mais
    //      contraste e frequencia maior, e uma segunda passada de bandas finas por cima.
    //   3) GRANDE TEMPESTADE - oval anticiclonico tipo Grande Mancha, deslocado do centro e girando
    //      com a orbita. E' o detalhe que o olho usa pra perceber que o planeta ROTACIONA.
    //   4) CRESCENTE DO TERMINADOR - fio quente na borda virada pro sol. Define a direcao da luz e
    //      amarra o planeta ao mesmo sol que ilumina o terreno.
    float planet_ang_r = g_sky_cfg.planet_radius;

    // Metade DISTANTE do anel, ANTES de tudo do planeta: o disco e o halo desenhados a seguir a
    // cobrem onde se sobrepoem (o anel "sumindo por tras do planeta"), e ela continua visivel fora
    // da silhueta. Ver a nota em render_planet_ring.
    render_planet_ring(planet_pos, g_sky_cfg.planet_radius * 1.55f, g_sky_cfg.planet_radius * 2.55f,
                       22.0f, 0.82f, 0.88f, 0.86f, 0.32f, g_camera.position, 1);

    // (1) halo de atmosfera, por TRAS do disco (desenhado antes, sem depth mask)
    rlSetBlendMode(RL_BLEND_ADDITIVE);
    rlDisableDepthMask();
    render_billboard_disc(planet_pos, planet_ang_r * 1.16f, 0.32f, 0.78f, 0.74f,
                          0.16f + night_alpha * 0.20f, 44);
    render_billboard_disc(planet_pos, planet_ang_r * 1.06f, 0.42f, 0.88f, 0.82f,
                          0.10f + night_alpha * 0.14f, 44);
    rlEnableDepthMask();
    rlSetBlendMode(RL_BLEND_ALPHA);

    // (2) corpo com bandas. lat/lon maiores (28x40) porque com o dobro do tamanho angular a
    // facetacao do mesh antigo (24x32) ficava visivel na silhueta.
    render_lit_sphere(planet_pos, planet_ang_r, sun_dir, g_camera.position,
                      0.08f, 0.34f, 0.40f, 0.99f,
                      0.20f, 0.94f, 0.10f,
                      0.016f, 0.13f, 28, 40,
                      9.0f, 1.05f, 0.86f, 0.95f, 0.90f);
    // bandas finas por cima, meio transparentes: da a impressao de varias camadas de nuvem
    render_lit_sphere(planet_pos, planet_ang_r * 1.002f, sun_dir, g_camera.position,
                      0.16f, 0.52f, 0.56f, 0.34f,
                      0.30f, 0.80f, 0.06f,
                      0.030f, 0.16f, 20, 30,
                      23.0f, 0.70f, 0.74f, 0.88f, 0.86f);

    // (3) grande tempestade: oval achatado na horizontal, deslocado do centro do disco.
    // Posicionado no plano da tela (right/up da camera) pra nao precisar de UV na esfera.
    {
        Vec3 to_cam = vec3_normalize(vec3_sub(g_camera.position, planet_pos));
        Vec3 wup = {0.0f, 1.0f, 0.0f};
        Vec3 pright = vec3_cross(wup, to_cam);
        if (vec3_length(pright) < 0.001f) pright = {1.0f, 0.0f, 0.0f};
        pright = vec3_normalize(pright);
        Vec3 pup = vec3_normalize(vec3_cross(to_cam, pright));
        // Gira devagar com a orbita: e' o que torna a rotacao do planeta perceptivel.
        float spin = planet_orbit * 2.4f;
        float sx = std::cos(spin) * 0.34f;
        float sy = -0.16f + std::sin(spin * 0.5f) * 0.06f;
        Vec3 storm = vec3_add(planet_pos,
                        vec3_add(vec3_scale(pright, sx * planet_ang_r),
                                 vec3_add(vec3_scale(pup, sy * planet_ang_r),
                                          vec3_scale(to_cam, planet_ang_r * 0.06f))));
        // So' aparece na parte iluminada - uma tempestade no lado noturno nao se ve.
        float storm_lit = clamp01(vec3_dot(vec3_normalize(vec3_sub(storm, planet_pos)), sun_dir) * 1.4f + 0.35f);
        if (storm_lit > 0.02f) {
            // Achatado: 3 discos concentricos deslocados no eixo `pright` imitam um oval sem
            // precisar de um primitivo de elipse.
            for (int s = -1; s <= 1; ++s) {
                Vec3 c = vec3_add(storm, vec3_scale(pright, (float)s * planet_ang_r * 0.085f));
                render_billboard_disc(c, planet_ang_r * 0.115f,
                                      0.86f, 0.52f, 0.34f, 0.30f * storm_lit, 22);
            }
            render_billboard_disc(storm, planet_ang_r * 0.062f,
                                  0.95f, 0.70f, 0.48f, 0.26f * storm_lit, 20);
        }
    }

    // (4) crescente do terminador: fio quente na borda virada pro sol.
    {
        Vec3 to_cam = vec3_normalize(vec3_sub(g_camera.position, planet_pos));
        Vec3 limb = vec3_sub(sun_dir, vec3_scale(to_cam, vec3_dot(sun_dir, to_cam)));
        if (vec3_length(limb) > 0.001f) {
            limb = vec3_normalize(limb);
            rlSetBlendMode(RL_BLEND_ADDITIVE);
            rlDisableDepthMask();
            for (int k = 0; k < 7; ++k) {
                float t = (float)k / 6.0f;
                Vec3 wup = {0.0f, 1.0f, 0.0f};
                Vec3 tang = vec3_cross(limb, to_cam);
                if (vec3_length(tang) < 0.001f) tang = wup;
                tang = vec3_normalize(tang);
                float off = (t - 0.5f) * 1.72f;
                Vec3 c = vec3_add(planet_pos,
                            vec3_add(vec3_scale(limb, planet_ang_r * 0.97f * std::cos(off * 0.9f)),
                                     vec3_scale(tang, planet_ang_r * 0.97f * std::sin(off * 0.9f))));
                c = vec3_add(c, vec3_scale(to_cam, planet_ang_r * 0.10f));
                render_billboard_disc(c, planet_ang_r * 0.105f, 1.0f, 0.86f, 0.62f, 0.20f, 18);
            }
            rlEnableDepthMask();
            rlSetBlendMode(RL_BLEND_ALPHA);
        }
    }
    // Metade PROXIMA do anel: por cima do planeta, que e o certo - ela passa na frente dele.
    render_planet_ring(planet_pos, g_sky_cfg.planet_radius * 1.55f, g_sky_cfg.planet_radius * 2.55f,
                       22.0f, 0.82f, 0.88f, 0.86f, 0.32f, g_camera.position, 2);

    // Orbita das luas: vem de sky_moon_state(), a mesma funcao que lighting.cpp le pra iluminar.
    // Antes a orbita era calculada inline AQUI e a iluminacao nao a conhecia; agora e uma fonte so.
    MoonState mstate = sky_moon_state();
    Vec3 moon1_pos = body_from_spherical(mstate.az1, mstate.el1,
                                         g_sky_cfg.moon_distance,
                                         g_sky_cfg.moon_parallax);
    Vec3 moon2_pos = body_from_spherical(mstate.az2, mstate.el2,
                                         g_sky_cfg.moon2_distance,
                                         g_sky_cfg.moon2_parallax);

    auto avoid_sun_cross = [&](Vec3& pos, float min_dot, float yaw_shift) {
        Vec3 dir = vec3_normalize(vec3_sub(pos, camera_ref));
        if (vec3_dot(dir, sun_dir) <= min_dot) return;
        float vx = pos.x - camera_ref.x;
        float vz = pos.z - camera_ref.z;
        float r = std::sqrt(vx * vx + vz * vz);
        if (r < 0.001f) return;
        float yaw = std::atan2(vz, vx) + yaw_shift;
        pos.x = camera_ref.x + std::cos(yaw) * r;
        pos.z = camera_ref.z + std::sin(yaw) * r;
    };
    avoid_sun_cross(moon1_pos, 0.84f, (sun_dir.x >= 0.0f) ? -0.65f : 0.65f);
    avoid_sun_cross(moon2_pos, 0.84f, (sun_dir.x >= 0.0f) ? 0.75f : -0.75f);

    float moon_alpha = 0.35f + night_alpha * 0.65f;
    render_lit_sphere(moon1_pos, g_sky_cfg.moon_radius, sun_dir, g_camera.position,
                      0.64f, 0.58f, 0.54f, moon_alpha,
                      0.12f, 0.95f, 0.10f,
                      0.030f, 0.30f, 16, 22);
    render_lit_sphere(moon2_pos, g_sky_cfg.moon2_radius, sun_dir, g_camera.position,
                      0.58f, 0.68f, 0.82f, moon_alpha * 0.92f,
                      0.12f, 0.95f, 0.14f,
                      0.045f, 0.24f, 14, 20);

    float eclipse_cycle = 0.5f + 0.5f * std::sin((g_day_time / (kDayLength * g_sky_cfg.eclipse_frequency_days)) * 2.0f * kPi);
    float sun_align = vec3_dot(vec3_normalize(vec3_sub(moon1_pos, camera_ref)), sun_dir);
    float eclipse = smoothstep01(0.996f, 0.9998f, sun_align) * smoothstep01(0.78f, 1.0f, eclipse_cycle) * g_sky_cfg.eclipse_strength;

    if (sun_pos.y > 40.0f) {
        float sun_alpha = 1.0f - eclipse;
        render_lit_sphere(sun_pos, g_sky_cfg.sun_radius, sun_dir, g_camera.position,
                          1.0f, 0.84f, 0.50f, sun_alpha,
                          0.95f, 0.55f, 0.05f,
                          0.0f, 0.0f, 18, 24);
        rlSetBlendMode(RL_BLEND_ADDITIVE);
        float halo_mul = g_sky_cfg.sun_halo_size;
        render_billboard_disc(sun_pos, g_sky_cfg.sun_radius * halo_mul, 1.0f, 0.70f, 0.35f, (0.12f + 0.20f * g_sky_cfg.bloom_intensity) * sun_alpha, 34);
        render_billboard_disc(sun_pos, g_sky_cfg.sun_radius * (halo_mul * 1.8f), 1.0f, 0.52f, 0.22f, (0.05f + 0.10f * g_sky_cfg.bloom_intensity) * sun_alpha, 34);
        rlSetBlendMode(RL_BLEND_ALPHA);
    }

    render_cloud_layer(cam_x, cam_z, day_phase, atmos_factor);
    render_shooting_stars(cam_x, cam_y, cam_z, night_alpha);

    // Montanhas distantes: desenhadas por ULTIMO entre as camadas de ceu.
    //
    // Elas ficavam aqui, logo depois do domo de gradiente. Como o ceu e' pintado de tras pra frente
    // sem teste de profundidade, TUDO que vem depois (estrelas, nebulosa, sol, luas e o gigante
    // gasoso) era pintado por CIMA da silhueta - o planeta aparecia na frente das montanhas, que e'
    // o inverso do que a distancia manda. Ficava disfarcado enquanto o planeta era pequeno; com ele
    // ocupando ~29 graus do ceu virou obvio.
    //
    // Montanha e' o elemento de ceu MAIS PROXIMO do jogador (e' relevo real no horizonte, nao corpo
    // celeste), entao tem que ocluir todos os outros.
    render_distant_mountains(cam_x, cam_z, ground_y, palette);

    rlEnableDepthTest();
}
