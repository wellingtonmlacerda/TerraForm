#include "raylib_platform.h"
#include "render_primitives.h"

#include "math_core.h"    // kPi, clamp01
#include "textures.h"     // Tile, UvRect, atlas_uv, kAtlasSizePx
#include "camera.h"       // g_camera.position (for fog distance)
#include "noise.h"        // lerp

#include <cmath>

void render_line_3d(Vec3 a, Vec3 b, float r, float g, float b_col, float alpha) {
    rlBegin(RL_LINES);
    rlColor4f(r, g, b_col, alpha);
    rlVertex3f(a.x, a.y, a.z);
    rlVertex3f(b.x, b.y, b.z);
    rlEnd();
}

void render_beam_3d(Vec3 a, Vec3 b, float width, float r, float g, float b_col, float alpha) {
    Vec3 dir = vec3_sub(b, a);
    float len = vec3_length(dir);
    if (len < 0.001f) return;
    dir = vec3_scale(dir, 1.0f / len);
    Vec3 up = {0.0f, 1.0f, 0.0f};
    Vec3 right = vec3_cross(dir, up);
    if (vec3_length(right) < 0.001f) right = {1.0f, 0.0f, 0.0f};
    right = vec3_normalize(right);
    Vec3 off = vec3_scale(right, width * 0.5f);

    Vec3 a0 = vec3_sub(a, off), a1 = vec3_add(a, off);
    Vec3 b0 = vec3_sub(b, off), b1 = vec3_add(b, off);
    rlColor4f(r, g, b_col, alpha);
    rlBegin(RL_QUADS);
    rlVertex3f(a0.x, a0.y, a0.z);
    rlVertex3f(a1.x, a1.y, a1.z);
    rlVertex3f(b1.x, b1.y, b1.z);
    rlVertex3f(b0.x, b0.y, b0.z);
    rlEnd();
}

void render_glow_disc_3d(Vec3 center, float radius, float r, float g, float b_col, float alpha, int segments) {
    Vec3 to_cam = vec3_sub(g_camera.position, center);
    if (vec3_length(to_cam) < 0.001f) to_cam = {0.0f, 0.0f, 1.0f};
    to_cam = vec3_normalize(to_cam);
    Vec3 up = {0.0f, 1.0f, 0.0f};
    Vec3 right = vec3_cross(up, to_cam);
    if (vec3_length(right) < 0.001f) right = {1.0f, 0.0f, 0.0f};
    right = vec3_normalize(right);
    Vec3 disc_up = vec3_normalize(vec3_cross(to_cam, right));

    rlBegin(RL_TRIANGLES);
    float prev_x = 0.0f, prev_y = 0.0f, prev_z = 0.0f;
    bool have_prev = false;
    for (int i = 0; i <= segments; ++i) {
        float ang = (float)i / (float)segments * 2.0f * kPi;
        float ca = std::cos(ang), sa = std::sin(ang);
        Vec3 rim = vec3_add(center, vec3_add(vec3_scale(right, ca * radius), vec3_scale(disc_up, sa * radius)));
        if (have_prev) {
            rlColor4f(r, g, b_col, alpha);
            rlVertex3f(center.x, center.y, center.z);
            rlColor4f(r, g, b_col, 0.0f);
            rlVertex3f(prev_x, prev_y, prev_z);
            rlColor4f(r, g, b_col, 0.0f);
            rlVertex3f(rim.x, rim.y, rim.z);
        }
        prev_x = rim.x; prev_y = rim.y; prev_z = rim.z;
        have_prev = true;
    }
    rlEnd();
}

// ============= Per-frame fog parameters =============
// Definition of the extern declared in render_primitives.h. main.cpp's render_world() sets
// these once per frame before the terrain loop runs (and clears .enabled once the fogged
// region of the frame ends); this file's render_cube_3d()/render_wall_3d_tex() (and main.cpp's
// own local render_plane_3d()/render_plane_3d_tex()/render_cube_3d_tex()) read them.
FrameFogParams g_frame_fog;
float g_frame_terrain_horizon = 1000.0f;

// Applies the current frame's fog (if enabled) to an already-shaded color, based on the
// distance from the camera to the given world-space position. Reproduces the old GL_LINEAR
// fixed-function fog formula: factor = clamp01((end - dist) / (end - start)); factor=1 near
// (original color unchanged), factor=0 far (fully replaced by the fog color).
static void apply_frame_fog(float wx, float wy, float wz, float& r, float& g, float& b) {
    if (!g_frame_fog.enabled) return;
    float dx = wx - g_camera.position.x;
    float dy = wy - g_camera.position.y;
    float dz = wz - g_camera.position.z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    float span = std::max(0.0001f, g_frame_fog.end - g_frame_fog.start);
    float factor = clamp01((g_frame_fog.end - dist) / span);
    r = lerp(g_frame_fog.r, r, factor);
    g = lerp(g_frame_fog.g, g, factor);
    b = lerp(g_frame_fog.b, b, factor);
}

void render_plane_3d(float x, float y, float z, float size, float r, float g, float b, float a) {
    float half = size * 0.5f;
    apply_frame_fog(x, y, z, r, g, b);
    rlColor4f(r, g, b, a);
    rlBegin(RL_QUADS);
    rlVertex3f(x - half, y, z - half);
    rlVertex3f(x + half, y, z - half);
    rlVertex3f(x + half, y, z + half);
    rlVertex3f(x - half, y, z + half);
    rlEnd();
}

static inline Color color_f(float r, float g, float b, float a) {
    Color c;
    c.r = (unsigned char)std::clamp((int)(r * 255.0f), 0, 255);
    c.g = (unsigned char)std::clamp((int)(g * 255.0f), 0, 255);
    c.b = (unsigned char)std::clamp((int)(b * 255.0f), 0, 255);
    c.a = (unsigned char)std::clamp((int)(a * 255.0f), 0, 255);
    return c;
}

// ============= Rendering Helpers =============
void render_quad(float x, float y, float w, float h, float r, float g, float b, float a) {
    // DrawRectangleRec (not DrawRectangle) - the latter truncates to int pixel coordinates,
    // which would visibly jitter the many sub-pixel float positions used throughout the HUD/
    // menus/progress bars (bounce/hover animations, fine bar-fill widths, etc.).
    DrawRectangleRec({x, y, w, h}, color_f(r, g, b, a));
}

// Quad 2D texturizado (tile do atlas). Migrado para DrawTexturePro: atlas_uv() continua
// devolvendo o retangulo normalizado (0..1) na mesma convencao "origem embaixo" de sempre
// (ver textures.h) - o unico ajuste necessario aqui e converter esse retangulo normalizado
// para um Rectangle em pixels (que DrawTexturePro exige) com altura NEGATIVA, reproduzindo a
// mesma direcao de amostragem v1->v0 que o codigo antigo usava (glTexCoord2f(u0,v1) no vertice
// de tela de cima, glTexCoord2f(u0,v0) no de baixo) sem precisar mudar a convencao de
// coordenadas do atlas em si (ver o comentario de atlas_uv() em textures.h).
void render_quad_tex(float x, float y, float w, float h, Tile tile, float tint_r, float tint_g, float tint_b, float a) {
    UvRect uv = atlas_uv(tile);
    Texture2D atlas_tex{};
    atlas_tex.id = g_tex_atlas;
    atlas_tex.width = kAtlasSizePx;
    atlas_tex.height = kAtlasSizePx;
    atlas_tex.mipmaps = 1;
    atlas_tex.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;

    Rectangle src;
    src.x = uv.u0 * (float)kAtlasSizePx;
    src.width = (uv.u1 - uv.u0) * (float)kAtlasSizePx;
    src.y = uv.v1 * (float)kAtlasSizePx;
    src.height = -(uv.v1 - uv.v0) * (float)kAtlasSizePx;

    Rectangle dst{x, y, w, h};
    DrawTexturePro(atlas_tex, src, dst, {0.0f, 0.0f}, 0.0f, color_f(tint_r, tint_g, tint_b, a));
}

void render_bar(float x, float y, float w, float h, float pct, float r, float g, float b) {
    render_quad(x, y, w, h, 0.0f, 0.0f, 0.0f, 0.55f);
    render_quad(x + 2.0f, y + 2.0f, (w - 4.0f) * clamp01(pct), h - 4.0f, r, g, b, 0.92f);
}

// ============= Astronaut Rendering =============
void render_circle(float cx, float cy, float radius, float r, float g, float b, float a, int segments) {
    // DrawCircleSector: startAngle/endAngle in degrees, segment count matches the old
    // GL_TRIANGLE_FAN's `segments` fan slices exactly (render_rounded_rect calls this with
    // segments=8 for its deliberately low-poly corners - preserved here).
    DrawCircleSector({cx, cy}, radius, 0.0f, 360.0f, segments, color_f(r, g, b, a));
}

void render_ellipse(float cx, float cy, float rx, float ry, float r, float g, float b, float a, int segments) {
    (void)segments; // DrawEllipse has no segment-count parameter; confirmed safe (see plan).
    DrawEllipse((int)cx, (int)cy, rx, ry, color_f(r, g, b, a));
}

void render_rounded_rect(float x, float y, float w, float h, float radius, float r, float g, float b, float a) {
    float roundness = clamp01(radius / (0.5f * std::min(w, h)));
    Rectangle rec{x, y, w, h};
    DrawRectangleRounded(rec, roundness, 8, color_f(r, g, b, a));
}

// ============= Renderizacao 3D (Estilo Minicraft) =============

// Renderizar outline de um cubo (bordas pretas estilo pixel art)
void render_cube_outline_3d(float x, float y, float z, float size, float line_width) {
    (void)line_width; // Sem equivalente confiavel na raylib (depende de driver) - best-effort.
    Vector3 pos{x, y, z};
    Vector3 sz{size, size, size};
    DrawCubeWiresV(pos, sz, color_f(0.0f, 0.0f, 0.0f, 0.8f));
}

// Renderizar um cubo no espaco 3D com iluminacao simples (Minicraft style). Fica manual (rlgl):
// grava sombreamento distinto por face (topo/lado/lado escuro), o que nenhuma funcao de alto
// nivel da raylib (DrawCube usa uma cor unica) consegue expressar. Porte mecanico de
// glBegin(GL_QUADS)/glVertex3f/glColor4f/glEnd para rlBegin(RL_QUADS)/rlVertex3f/rlColor4f/rlEnd.
void render_cube_3d(float x, float y, float z, float size, float r, float g, float b, float a, bool outline) {
    float half = size * 0.5f;

    // Cores com sombreamento por face (iluminacao fake - Minicraft tem 3 niveis)
    float top_shade = 1.0f;      // Face superior - clara
    float side_shade = 0.70f;    // Faces laterais - media
    float dark_shade = 0.50f;    // Faces escuras

    float fog_r = r, fog_g = g, fog_b = b;
    apply_frame_fog(x, y, z, fog_r, fog_g, fog_b);
    r = fog_r; g = fog_g; b = fog_b;

    rlBegin(RL_QUADS);

    // Face superior (Y+) - mais clara
    rlColor4f(r * top_shade, g * top_shade, b * top_shade, a);
    rlVertex3f(x - half, y + half, z - half);
    rlVertex3f(x + half, y + half, z - half);
    rlVertex3f(x + half, y + half, z + half);
    rlVertex3f(x - half, y + half, z + half);

    // Face inferior (Y-) - escura (normalmente nao visivel)
    rlColor4f(r * dark_shade, g * dark_shade, b * dark_shade, a);
    rlVertex3f(x - half, y - half, z + half);
    rlVertex3f(x + half, y - half, z + half);
    rlVertex3f(x + half, y - half, z - half);
    rlVertex3f(x - half, y - half, z - half);

    // Face frontal (Z+) - media
    rlColor4f(r * side_shade, g * side_shade, b * side_shade, a);
    rlVertex3f(x - half, y - half, z + half);
    rlVertex3f(x + half, y - half, z + half);
    rlVertex3f(x + half, y + half, z + half);
    rlVertex3f(x - half, y + half, z + half);

    // Face traseira (Z-) - escura
    rlColor4f(r * dark_shade, g * dark_shade, b * dark_shade, a);
    rlVertex3f(x + half, y - half, z - half);
    rlVertex3f(x - half, y - half, z - half);
    rlVertex3f(x - half, y + half, z - half);
    rlVertex3f(x + half, y + half, z - half);

    // Face direita (X+) - media
    rlColor4f(r * side_shade, g * side_shade, b * side_shade, a);
    rlVertex3f(x + half, y - half, z + half);
    rlVertex3f(x + half, y - half, z - half);
    rlVertex3f(x + half, y + half, z - half);
    rlVertex3f(x + half, y + half, z + half);

    // Face esquerda (X-) - escura
    rlColor4f(r * dark_shade, g * dark_shade, b * dark_shade, a);
    rlVertex3f(x - half, y - half, z - half);
    rlVertex3f(x - half, y - half, z + half);
    rlVertex3f(x - half, y + half, z + half);
    rlVertex3f(x - half, y + half, z - half);

    rlEnd();

    // Desenhar outline se solicitado (estilo pixel art)
    if (outline) {
        render_cube_outline_3d(x, y, z, size, 1.0f);
    }
}

// Esfera solida pequena (capacete/juntas do jogador) - ver comentario completo no header.
void render_sphere_3d(float x, float y, float z, float radius, float r, float g, float b, float a,
                       int lat_seg, int lon_seg, float scale_y, float scale_z) {
    rlSetTexture(0);

    float fog_r = r, fog_g = g, fog_b = b;
    apply_frame_fog(x, y, z, fog_r, fog_g, fog_b);
    r = fog_r; g = fog_g; b = fog_b;

    for (int lat = 0; lat < lat_seg; ++lat) {
        float v0 = (float)lat / (float)lat_seg;
        float v1 = (float)(lat + 1) / (float)lat_seg;
        float p0 = v0 * kPi;
        float p1 = v1 * kPi;
        // y vai de +radius (topo, p=0) a -radius (fundo, p=pi) - cos(p) comeca em 1 e desce.
        float y0 = std::cos(p0), y1 = std::cos(p1);
        float rad0 = std::sin(p0), rad1 = std::sin(p1);
        // Sombra simples por altura (mesmo espirito das 3 sombras de render_cube_3d): topo
        // claro, fundo escuro - nao precisa de normal por vertice pra uma esfera tao pequena.
        float shade0 = 0.62f + 0.38f * (y0 * 0.5f + 0.5f);
        float shade1 = 0.62f + 0.38f * (y1 * 0.5f + 0.5f);

        rlBegin(RL_QUADS);
        for (int lon = 0; lon < lon_seg; ++lon) {
            float u0 = (float)lon / (float)lon_seg * 2.0f * kPi;
            float u1 = (float)(lon + 1) / (float)lon_seg * 2.0f * kPi;
            float cu0 = std::cos(u0), su0 = std::sin(u0);
            float cu1 = std::cos(u1), su1 = std::sin(u1);

            Vec3 v00{x + cu0 * rad0 * radius, y + y0 * radius * scale_y, z + su0 * rad0 * radius * scale_z};
            Vec3 v10{x + cu1 * rad0 * radius, y + y0 * radius * scale_y, z + su1 * rad0 * radius * scale_z};
            Vec3 v11{x + cu1 * rad1 * radius, y + y1 * radius * scale_y, z + su1 * rad1 * radius * scale_z};
            Vec3 v01{x + cu0 * rad1 * radius, y + y1 * radius * scale_y, z + su0 * rad1 * radius * scale_z};

            rlColor4f(r * shade0, g * shade0, b * shade0, a);
            rlVertex3f(v00.x, v00.y, v00.z);
            rlVertex3f(v10.x, v10.y, v10.z);
            rlColor4f(r * shade1, g * shade1, b * shade1, a);
            rlVertex3f(v11.x, v11.y, v11.z);
            rlVertex3f(v01.x, v01.y, v01.z);
        }
        rlEnd();
    }
}

// Escotilha de vidro numa parede cilindrica - ver comentario completo em render_primitives.h.
void render_porthole_3d(Vec3 center, float wall_angle_rad, float glass_radius, float frame_width,
                        float gr, float gg, float gb, float galpha,
                        float fr, float fg, float fb,
                        int segments, int bolts, float glow) {
    if (glass_radius <= 0.0f || segments < 3) return;

    // Base tangente ao cilindro: n = radial pra fora, t = tangente horizontal, up = vertical.
    const float cu = std::cos(wall_angle_rad), su = std::sin(wall_angle_rad);
    const float nx = cu, nz = su;
    const float tx = -su, tz = cu;

    auto pt = [&](float rr, float ang, float out) -> Vec3 {
        float ca = std::cos(ang), sa = std::sin(ang);
        return {center.x + tx * (rr * ca) + nx * out,
                center.y + rr * sa,
                center.z + tz * (rr * ca) + nz * out};
    };

    // "Interior aceso": lerp da cor do vidro pra um branco quente + alpha maior (ver header).
    const float lit = clamp01(glow);
    gr = lerp(gr, 1.00f, lit * 0.72f);
    gg = lerp(gg, 0.90f, lit * 0.72f);
    gb = lerp(gb, 0.66f, lit * 0.72f);
    galpha = lerp(galpha, std::min(1.0f, galpha + 0.32f), lit);

    apply_frame_fog(center.x, center.y, center.z, gr, gg, gb);
    apply_frame_fog(center.x, center.y, center.z, fr, fg, fb);

    const float lip = std::max(0.04f, frame_width * 0.38f);

    // --- Vidro: leque de triangulos. O vertice central compartilhado deixa o centro mais claro
    //     que a borda de graca (reflexo/brilho interno) - um anel de quads nao consegue isso.
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < segments; ++i) {
        float a0 = (float)i / (float)segments * 2.0f * kPi;
        float a1 = (float)(i + 1) / (float)segments * 2.0f * kPi;
        Vec3 r0 = pt(glass_radius, a0, 0.0f);
        Vec3 r1 = pt(glass_radius, a1, 0.0f);
        float s0 = 0.66f + 0.34f * (std::sin(a0) * 0.5f + 0.5f);  // topo do vidro mais claro
        float s1 = 0.66f + 0.34f * (std::sin(a1) * 0.5f + 0.5f);
        rlColor4f(gr, gg, gb, galpha);
        rlVertex3f(center.x, center.y, center.z);
        rlColor4f(gr * s0, gg * s0, gb * s0, galpha);
        rlVertex3f(r0.x, r0.y, r0.z);
        rlColor4f(gr * s1, gg * s1, gb * s1, galpha);
        rlVertex3f(r1.x, r1.y, r1.z);
    }
    rlEnd();

    // --- Aro metalico: anel chato levantado em `lip` + a parede do degrau descendo ate o vidro.
    rlBegin(RL_QUADS);
    for (int i = 0; i < segments; ++i) {
        float a0 = (float)i / (float)segments * 2.0f * kPi;
        float a1 = (float)(i + 1) / (float)segments * 2.0f * kPi;
        float sh0 = 0.60f + 0.40f * (std::sin(a0) * 0.5f + 0.5f);
        float sh1 = 0.60f + 0.40f * (std::sin(a1) * 0.5f + 0.5f);

        Vec3 i0 = pt(glass_radius, a0, lip),               i1 = pt(glass_radius, a1, lip);
        Vec3 o0 = pt(glass_radius + frame_width, a0, lip), o1 = pt(glass_radius + frame_width, a1, lip);
        rlColor4f(fr * sh0, fg * sh0, fb * sh0, 1.0f);
        rlVertex3f(i0.x, i0.y, i0.z); rlVertex3f(o0.x, o0.y, o0.z);
        rlColor4f(fr * sh1, fg * sh1, fb * sh1, 1.0f);
        rlVertex3f(o1.x, o1.y, o1.z); rlVertex3f(i1.x, i1.y, i1.z);

        Vec3 b0 = pt(glass_radius, a0, 0.0f), b1 = pt(glass_radius, a1, 0.0f);
        float d0 = sh0 * 0.52f, d1 = sh1 * 0.52f;
        rlColor4f(fr * d0, fg * d0, fb * d0, 1.0f);
        rlVertex3f(b0.x, b0.y, b0.z); rlVertex3f(i0.x, i0.y, i0.z);
        rlColor4f(fr * d1, fg * d1, fb * d1, 1.0f);
        rlVertex3f(i1.x, i1.y, i1.z); rlVertex3f(b1.x, b1.y, b1.z);
    }
    rlEnd();

    // --- Parafusos no aro.
    if (bolts > 0) {
        float br = glass_radius + frame_width * 0.5f;
        float bs = std::min(0.065f, frame_width * 0.32f);
        rlBegin(RL_QUADS);
        rlColor4f(std::min(1.0f, fr * 1.30f), std::min(1.0f, fg * 1.30f),
                  std::min(1.0f, fb * 1.30f), 1.0f);
        for (int i = 0; i < bolts; ++i) {
            float ang = (float)i / (float)bolts * 2.0f * kPi;
            Vec3 c = pt(br, ang, lip + 0.018f);
            rlVertex3f(c.x - tx * bs, c.y - bs, c.z - tz * bs);
            rlVertex3f(c.x + tx * bs, c.y - bs, c.z + tz * bs);
            rlVertex3f(c.x + tx * bs, c.y + bs, c.z + tz * bs);
            rlVertex3f(c.x - tx * bs, c.y + bs, c.z - tz * bs);
        }
        rlEnd();
    }

    // --- 2 barras cruzadas no vidro (mullion): e' isso que le como "janela de nave/submarino" em
    //     vez de adesivo redondo.
    {
        float hw = std::max(0.020f, glass_radius * 0.045f);
        Vec3 v0 = pt(glass_radius, kPi * 0.5f, 0.012f), v1 = pt(glass_radius, -kPi * 0.5f, 0.012f);
        Vec3 h0 = pt(glass_radius, 0.0f, 0.012f),       h1 = pt(glass_radius, kPi, 0.012f);
        rlBegin(RL_QUADS);
        rlColor4f(fr * 0.72f, fg * 0.72f, fb * 0.72f, 1.0f);
        rlVertex3f(v0.x - tx * hw, v0.y, v0.z - tz * hw);
        rlVertex3f(v0.x + tx * hw, v0.y, v0.z + tz * hw);
        rlVertex3f(v1.x + tx * hw, v1.y, v1.z + tz * hw);
        rlVertex3f(v1.x - tx * hw, v1.y, v1.z - tz * hw);
        rlVertex3f(h0.x, h0.y - hw, h0.z);
        rlVertex3f(h0.x, h0.y + hw, h0.z);
        rlVertex3f(h1.x, h1.y + hw, h1.z);
        rlVertex3f(h1.x, h1.y - hw, h1.z);
        rlEnd();
    }
}

// Hemisferio decorativo (domo geodesico) - ver comentario completo em render_primitives.h.
// Malha em faixas de latitude/longitude (mesma estrutura de render_lit_sphere em sky.cpp,
// usado pro planeta do ceu), so que so a metade de cima (0..pi/2, nao 0..pi) e sem luz
// direcional/especular - cor solida com um leve gradiente de altura (mais claro perto do
// topo) pra dar volume. Um segundo passe em RL_LINES pelas mesmas linhas de malha desenha
// o padrao triangulado/geodesico por cima, sem precisar de geometria icosaedrica de
// verdade. Sem colisao - e so uma chamada de desenho, nao mexe em World/is_solid.
// Angulo minimo entre duas direcoes (-pi..pi) - usado pra saber se uma longitude cai
// dentro do arco da porta, considerando o wraparound em 2*pi.
static float shortest_angle_diff(float a, float b) {
    float d = std::fmod(a - b + kPi, 2.0f * kPi);
    if (d < 0.0f) d += 2.0f * kPi;
    return d - kPi;
}

void render_geodesic_dome(Vec3 base_center, float radius, float r, float g, float b, float a,
                           int lat_seg, int lon_seg,
                           float door_facing_rad, float door_half_angle, float door_height,
                           float skirt_height,
                           int porthole_count, float porthole_radius, float porthole_glow,
                           int door_count) {
    rlSetTexture(rlGetTextureIdDefault()); // NAO rlSetTexture(0): pra id 0 o rlgl nao troca a
                                           // textura ligada, so' esta e' a chamada que realmente
                                           // volta pra textura branca padrao

    // door_count portas IGUALMENTE espacadas a partir de door_facing_rad. Com o complexo modular
    // (4 corredores em N/L/S/O) a saia precisa de 4 aberturas, nao 1: o jogador atravessa o vao REAL
    // da parede de blocos do hub (r 14.5..15.6) e sairia atravessando a saia opaca do domo (r 16)
    // sem nada desenhado ali - exatamente a leitura de "passando direto pela parede" que o jogador
    // reclamou. Uma porta por chamada obrigaria o chamador a chamar a cupula 4x (4 hemisferios
    // sobrepostos) ou a duplicar a geometria da porta - a classe de bug documentada na nota sobre
    // g_shelter_door_x/y em modules_building.h.
    const int door_n = (door_half_angle > 0.0f && door_height > 0.0f)
                     ? std::max(1, door_count) : 0;
    const bool has_door = door_n > 0;
    const float door_step = has_door ? (2.0f * kPi / (float)door_n) : 0.0f;
    auto door_center = [&](int i) { return door_facing_rad + door_step * (float)i; };
    // Menor distancia angular ate QUALQUER porta - usado pelo keep-out das vigias e pelo recorte do
    // hemisferio.
    auto dist_to_nearest_door = [&](float u) -> float {
        float best = kPi;
        for (int i = 0; i < door_n; ++i) {
            float d = std::fabs(shortest_angle_diff(u, door_center(i)));
            if (d < best) best = d;
        }
        return best;
    };
    // y_world aqui e sempre altura ABSOLUTA a partir do chao (base_center.y), nao relativa
    // ao hemisferio - a saia (abaixo) fica entre 0 e skirt_height, e o hemisferio comeca em
    // skirt_height; com door_height <= skirt_height (ver chamada em main.cpp) a porta fica
    // inteira dentro da saia, entao o hemisferio nunca precisa recortar nada de verdade -
    // a checagem continua aqui so por seguranca/generalidade.
    auto in_door = [&](float u, float y_world) -> bool {
        if (!has_door) return false;
        if (y_world > door_height) return false;
        return dist_to_nearest_door(u) <= door_half_angle;
    };

    // Ponto na parede cilindrica, empurrado `out` pra fora do raio. Compartilhado pela saia e por
    // toda a moldura da porta.
    auto wall_pt = [&](float u, float y, float out) -> Vec3 {
        return {base_center.x + std::cos(u) * (radius + out), y,
                base_center.z + std::sin(u) * (radius + out)};
    };
    // Painel retangular CURVO (tiras tangenciais) com gradiente vertical de sombra. Tesselado por
    // arco proprio, INDEPENDENTE de lon_seg - e' isso que permite uma abertura de largura exata: a
    // saia antiga so' sabia pintar/pular segmentos inteiros de lon_seg (0.175 rad com lon_seg 36),
    // entao uma porta de 0.125 rad sempre virava 1-2 segmentos inteiros, ~3x larga demais.
    auto arc_panel = [&](float u_lo, float u_hi, float py0, float py1, float out,
                         float pr, float pg, float pb, float sh_bot, float sh_top, int steps) {
        if (u_hi <= u_lo || py1 <= py0) return;
        rlBegin(RL_QUADS);
        for (int i = 0; i < steps; ++i) {
            float ua = u_lo + (u_hi - u_lo) * (float)i / (float)steps;
            float ub = u_lo + (u_hi - u_lo) * (float)(i + 1) / (float)steps;
            Vec3 b0 = wall_pt(ua, py0, out), b1 = wall_pt(ub, py0, out);
            Vec3 t1 = wall_pt(ub, py1, out), t0 = wall_pt(ua, py1, out);
            rlColor4f(pr * sh_bot, pg * sh_bot, pb * sh_bot, a);
            rlVertex3f(b0.x, b0.y, b0.z);
            rlVertex3f(b1.x, b1.y, b1.z);
            rlColor4f(pr * sh_top, pg * sh_top, pb * sh_top, a);
            rlVertex3f(t1.x, t1.y, t1.z);
            rlVertex3f(t0.x, t0.y, t0.z);
        }
        rlEnd();
    };

    // --- Saia cilindrica (fundacao), com ABERTURAS de verdade nas portas ---
    // Fecha o vao entre o chao e a casca do hemisferio (antes a malha comecava direto no chao, entao
    // em terreno levemente irregular sobrava uma fresta visivel por baixo) e da uma base solida onde
    // a porta encaixa. Tom levemente mais frio/metalico que a casca tan de cima (fundacao vs tecido,
    // como num domo real).
    //
    // MUDANCA: cada porta e' agora um BURACO na saia (a faixa 0..door_height e' omitida no arco da
    // porta, sobrando so' a verga acima dela), nao um alcapao recolorido "sempre fechado". A porta
    // fechada existia porque a colisao era uma barreira cilindrica invisivel de 360 graus - a unica
    // forma de entrar era um teleporte de proximidade, entao desenhar um vao aberto teria sido uma
    // mentira. Com paredes de blocos de verdade e vaos de verdade, o oposto passou a valer: uma
    // porta desenhada fechada onde se ATRAVESSA e' a mentira.
    if (skirt_height > 0.0f) {
        float found_r = r * 0.55f + 0.16f, found_g = g * 0.55f + 0.18f, found_b = b * 0.55f + 0.22f;
        const int kArcSteps = 8;   // tiras por painel de porta (acompanha a curva do cilindro)

        // Meia-largura da moldura (a "alcova") em volta de cada vao. Limitada a meio passo entre
        // portas pra 4 portas nunca terem molduras se encavalando.
        const float hd = door_half_angle;
        const float hb = has_door
            ? std::min(std::min(door_half_angle * 1.75f, kPi * 0.30f), door_step * 0.45f)
            : 0.0f;

        if (!has_door) {
            // Sem porta: um unico anel fechado, como antes.
            arc_panel(0.0f, 2.0f * kPi, base_center.y, base_center.y + skirt_height, 0.0f,
                      found_r, found_g, found_b, 0.55f, 0.85f, lon_seg);
        } else {
            // Trechos CHEIOS entre portas consecutivas + VERGA (so' acima de door_height) sobre cada
            // vao. Tesselacao proporcional ao arco pra a curvatura ficar uniforme.
            for (int i = 0; i < door_n; ++i) {
                float end_i   = door_center(i) + hd;          // fim do vao i
                float start_j = door_center(i + 1) - hd;      // inicio do vao seguinte
                int seg_full = std::max(2, (int)std::lround((start_j - end_i) /
                                                           (2.0f * kPi) * (float)lon_seg));
                arc_panel(end_i, start_j, base_center.y, base_center.y + skirt_height, 0.0f,
                          found_r, found_g, found_b, 0.55f, 0.85f, seg_full);
                // Verga: fecha a saia por cima do vao (senao ficaria uma fenda ate o hemisferio).
                arc_panel(door_center(i) - hd, door_center(i) + hd,
                          base_center.y + door_height, base_center.y + skirt_height, 0.0f,
                          found_r, found_g, found_b, 0.70f, 0.85f, kArcSteps);
            }
        }

        // Linha de fundacao no chao (reforca a leitura de "estrutura construida"). Continua inteira
        // atravessando os vaos - ali ela le como a soleira da porta, que e' o que se quer.
        rlBegin(RL_LINES);
        rlColor4f(0.04f, 0.04f, 0.04f, a * 0.6f);
        for (int lon = 0; lon < lon_seg; ++lon) {
            float u0 = (float)lon / (float)lon_seg * 2.0f * kPi;
            float u1 = (float)(lon + 1) / (float)lon_seg * 2.0f * kPi;
            rlVertex3f(base_center.x + std::cos(u0) * radius, base_center.y, base_center.z + std::sin(u0) * radius);
            rlVertex3f(base_center.x + std::cos(u1) * radius, base_center.y, base_center.z + std::sin(u1) * radius);
        }
        rlEnd();

        // ================= MOLDURA DE ECLUSA (uma por vao) =================
        // O que era o alcapao fechado (folha dupla, painel rebaixado, volante de latao, costura
        // central) virou a MOLDURA de um vao aberto: batentes laterais, verga saliente, soleira com
        // faixas de advertencia, rebites e luzes de status. Os detalhes que so' fazem sentido numa
        // folha fechada sairam - um volante de escotilha no meio de uma passagem por onde se anda
        // leria como bug, nao como acabamento.
        // Desenhada nas DUAS faces da parede (side = +1 fora, -1 dentro) com todos os epsilons
        // ASSINADOS: a saia e' opaca e fica exatamente no raio, entao painéis so' do lado de fora
        // ficam escondidos por ela quando vistos de dentro - era o bug "a porta do lado de dentro
        // sumiu", e sem ela nao havia como achar a saida estando dentro da base.
        for (int di = 0; di < door_n; ++di) {
            const float uc = door_center(di);
            for (float side : {1.0f, -1.0f}) {
                const float kEpsJamb = 0.030f * side;  // batente/verga, saliente sobre a saia
                const float kEpsTrim = 0.070f * side;  // faixas/rebites/luzes, por cima do batente

                const float y0 = base_center.y;
                const float y1 = base_center.y + door_height;
                const float jamb_top = std::min(base_center.y + skirt_height, y1 + 0.42f);

                // (1) Batentes laterais: os dois trechos entre a borda do vao e a borda da moldura.
                for (float sgn : {-1.0f, 1.0f}) {
                    float a_in  = uc + sgn * hd;
                    float a_out = uc + sgn * hb;
                    arc_panel(std::min(a_in, a_out), std::max(a_in, a_out), y0, jamb_top, kEpsJamb,
                              0.385f, 0.415f, 0.465f, 0.66f, 0.98f, 4);
                }

                // (2) Verga saliente sobre o vao (a viga que "carrega" a abertura).
                arc_panel(uc - hb, uc + hb, y1, jamb_top, kEpsJamb,
                          0.330f, 0.355f, 0.400f, 0.80f, 1.00f, kArcSteps);
                // Nervura clara na face de baixo da verga - marca a altura livre da passagem.
                arc_panel(uc - hb, uc + hb, y1 - 0.09f, y1, kEpsTrim,
                          0.565f, 0.595f, 0.645f, 1.00f, 1.00f, kArcSteps);

                // (3) Faixas de advertencia amarelo/preto na SOLEIRA, dentro da largura do vao.
                {
                    const int stripes = 7;
                    for (int i = 0; i < stripes; ++i) {
                        float ua = uc - hd + (2.0f * hd) * (float)i / (float)stripes;
                        float ub = uc - hd + (2.0f * hd) * (float)(i + 1) / (float)stripes;
                        bool yellow = (i % 2) == 0;
                        arc_panel(ua, ub, y0 + 0.02f, y0 + 0.20f, kEpsTrim,
                                  yellow ? 0.880f : 0.100f,
                                  yellow ? 0.720f : 0.100f,
                                  yellow ? 0.140f : 0.120f, 1.00f, 1.00f, 2);
                    }
                }

                // (4) Rebites: 2 fileiras verticais nos batentes + 1 fileira sob a verga.
                {
                    auto wall_stud = [&](float u, float y, float size, float out,
                                         float sr2, float sg2, float sb2, float sa) {
                        float cu = std::cos(u), su = std::sin(u);
                        float tdx = -su * size, tdz = cu * size;
                        Vec3 c = wall_pt(u, y, out);
                        rlBegin(RL_QUADS);
                        rlColor4f(sr2, sg2, sb2, sa);
                        rlVertex3f(c.x - tdx, c.y - size, c.z - tdz);
                        rlVertex3f(c.x + tdx, c.y - size, c.z + tdz);
                        rlVertex3f(c.x + tdx, c.y + size, c.z + tdz);
                        rlVertex3f(c.x - tdx, c.y + size, c.z - tdz);
                        rlEnd();
                    };
                    const int nv = 6;
                    float u_l = uc - (hd + hb) * 0.5f, u_r = uc + (hd + hb) * 0.5f;
                    for (int i = 0; i <= nv; ++i) {
                        float ry = y0 + 0.28f + (door_height - 0.56f) * (float)i / (float)nv;
                        wall_stud(u_l, ry, 0.042f, kEpsTrim, 0.66f, 0.68f, 0.72f, a);
                        wall_stud(u_r, ry, 0.042f, kEpsTrim, 0.66f, 0.68f, 0.72f, a);
                    }
                    const int nh = 5;
                    for (int i = 0; i <= nh; ++i) {
                        float ru = uc - hb * 0.85f + (2.0f * hb * 0.85f) * (float)i / (float)nh;
                        wall_stud(ru, jamb_top - 0.11f, 0.042f, kEpsTrim, 0.66f, 0.68f, 0.72f, a);
                    }
                }

                // (5) Contorno em linha do vao e da moldura - registra as bordas reais da abertura.
                rlBegin(RL_LINES);
                rlColor4f(0.02f, 0.02f, 0.03f, a * 0.90f);
                for (float hh : {hd, hb}) {
                    float ey = (hh == hb) ? jamb_top : y1;
                    float eo = kEpsTrim + 0.006f * side;
                    Vec3 l0 = wall_pt(uc - hh, y0, eo), l1 = wall_pt(uc - hh, ey, eo);
                    Vec3 r0 = wall_pt(uc + hh, y0, eo), r1 = wall_pt(uc + hh, ey, eo);
                    rlVertex3f(l0.x, l0.y, l0.z); rlVertex3f(l1.x, l1.y, l1.z);
                    rlVertex3f(r0.x, r0.y, r0.z); rlVertex3f(r1.x, r1.y, r1.z);
                    for (int i = 0; i < kArcSteps; ++i) {
                        float ua = uc - hh + (2.0f * hh) * (float)i / (float)kArcSteps;
                        float ub = uc - hh + (2.0f * hh) * (float)(i + 1) / (float)kArcSteps;
                        Vec3 p0 = wall_pt(ua, ey, eo), p1 = wall_pt(ub, ey, eo);
                        rlVertex3f(p0.x, p0.y, p0.z); rlVertex3f(p1.x, p1.y, p1.z);
                    }
                }
                rlEnd();

                // (6) 2 luzes de status ambar acima da verga - "eclusa energizada", visivel de longe.
                {
                    float light_y = std::min(jamb_top - 0.16f, y1 + 0.30f);
                    float lo = hb * 0.62f;
                    for (float sgn : {-1.0f, 1.0f}) {
                        float au = uc + sgn * lo;
                        render_cube_3d(base_center.x + std::cos(au) * (radius + kEpsTrim),
                                       light_y,
                                       base_center.z + std::sin(au) * (radius + kEpsTrim),
                                       0.13f, 0.95f, 0.70f, 0.20f, a);
                    }
                }
            }
        }

        // --- Anel de escotilhas de vidro na saia (janelas tipo submarino) ---
        // Fase de meio-passo de proposito: door_facing_rad = pi/2 e' o caso real, e um anel com
        // fase 0 poria uma vigia exatamente em cima da porta. O keep-out abaixo e' calculado de
        // verdade (nao confia na fase) pra continuar correto se a porta mudar de lado/tamanho.
        if (porthole_count > 0 && porthole_radius > 0.0f) {
            const float frame_w = porthole_radius * 0.31f;
            const float py = base_center.y +
                std::min(skirt_height * 0.55f, skirt_height - porthole_radius - frame_w - 0.55f);
            const float step = 2.0f * kPi / (float)porthole_count;
            // Meio-arco proibido em volta de CADA vao: meia-largura da moldura + raio do aro +
            // folga. Com 4 portas, checar so' door_facing_rad deixaria uma vigia em cima das outras
            // 3 - e' por isso que o keep-out usa a distancia a porta MAIS PROXIMA, nao a primeira.
            const float keep_out = has_door
                ? (hb + (porthole_radius + frame_w) / std::max(1.0f, radius) + 0.05f)
                : 0.0f;
            for (int i = 0; i < porthole_count; ++i) {
                float u = step * 0.5f + step * (float)i;
                if (has_door && dist_to_nearest_door(u) < keep_out) continue;
                Vec3 c{base_center.x + std::cos(u) * (radius + 0.03f), py,
                       base_center.z + std::sin(u) * (radius + 0.03f)};
                render_porthole_3d(c, u, porthole_radius, frame_w,
                                   0.42f, 0.62f, 0.78f, 0.62f,  // vidro azul-esverdeado
                                   0.50f, 0.53f, 0.58f,         // aro metalico
                                   16, 10, porthole_glow);
            }
        }
    }

    Vec3 dome_center = base_center;
    dome_center.y += skirt_height;

    // --- Casca solida (hemisferio) ---
    for (int lat = 0; lat < lat_seg; ++lat) {
        float v0 = (float)lat / (float)lat_seg;
        float v1 = (float)(lat + 1) / (float)lat_seg;
        float p0 = v0 * (kPi * 0.5f);
        float p1 = v1 * (kPi * 0.5f);
        float y0 = std::sin(p0), y1 = std::sin(p1);
        float rad0 = std::cos(p0), rad1 = std::cos(p1);

        rlBegin(RL_QUADS);
        Vec3 prev_top{}, prev_bot{};
        float prev_top_shade = 0.0f, prev_bot_shade = 0.0f;
        float prev_u = 0.0f;
        bool have_prev = false;
        for (int lon = 0; lon <= lon_seg; ++lon) {
            float u = (float)lon / (float)lon_seg * 2.0f * kPi;
            float cu = std::cos(u), su = std::sin(u);

            Vec3 cur_top{dome_center.x + cu * rad1 * radius, dome_center.y + y1 * radius, dome_center.z + su * rad1 * radius};
            Vec3 cur_bot{dome_center.x + cu * rad0 * radius, dome_center.y + y0 * radius, dome_center.z + su * rad0 * radius};
            float top_shade = 0.72f + 0.28f * y1;
            float bot_shade = 0.72f + 0.28f * y0;

            bool skip_quad = in_door(u, skirt_height + y1 * radius) || in_door(prev_u, skirt_height + y1 * radius);
            if (have_prev && !skip_quad) {
                rlColor4f(r * prev_top_shade, g * prev_top_shade, b * prev_top_shade, a);
                rlVertex3f(prev_top.x, prev_top.y, prev_top.z);
                rlColor4f(r * prev_bot_shade, g * prev_bot_shade, b * prev_bot_shade, a);
                rlVertex3f(prev_bot.x, prev_bot.y, prev_bot.z);
                rlColor4f(r * bot_shade, g * bot_shade, b * bot_shade, a);
                rlVertex3f(cur_bot.x, cur_bot.y, cur_bot.z);
                rlColor4f(r * top_shade, g * top_shade, b * top_shade, a);
                rlVertex3f(cur_top.x, cur_top.y, cur_top.z);
            }
            prev_top = cur_top; prev_bot = cur_bot;
            prev_top_shade = top_shade; prev_bot_shade = bot_shade;
            prev_u = u;
            have_prev = true;
        }
        rlEnd();
    }

    // --- Linhas de latitude (paralelos) ---
    for (int lat = 0; lat <= lat_seg; ++lat) {
        float v = (float)lat / (float)lat_seg;
        float p = v * (kPi * 0.5f);
        float y = std::sin(p) * radius;
        float rad = std::cos(p) * radius;

        rlBegin(RL_LINES);
        rlColor4f(0.05f, 0.05f, 0.05f, a * 0.55f);
        for (int lon = 0; lon < lon_seg; ++lon) {
            float u0 = (float)lon / (float)lon_seg * 2.0f * kPi;
            float u1 = (float)(lon + 1) / (float)lon_seg * 2.0f * kPi;
            if (in_door(u0, skirt_height + y) || in_door(u1, skirt_height + y)) continue;
            rlVertex3f(dome_center.x + std::cos(u0) * rad, dome_center.y + y, dome_center.z + std::sin(u0) * rad);
            rlVertex3f(dome_center.x + std::cos(u1) * rad, dome_center.y + y, dome_center.z + std::sin(u1) * rad);
        }
        rlEnd();
    }

    // --- Linhas de longitude (meridianos) ---
    for (int lon = 0; lon < lon_seg; ++lon) {
        float u = (float)lon / (float)lon_seg * 2.0f * kPi;
        float cu = std::cos(u), su = std::sin(u);

        rlBegin(RL_LINES);
        rlColor4f(0.05f, 0.05f, 0.05f, a * 0.55f);
        for (int lat = 0; lat < lat_seg; ++lat) {
            float v0 = (float)lat / (float)lat_seg;
            float v1 = (float)(lat + 1) / (float)lat_seg;
            float p0 = v0 * (kPi * 0.5f), p1 = v1 * (kPi * 0.5f);
            float y0 = std::sin(p0) * radius, y1 = std::sin(p1) * radius;
            float rad0 = std::cos(p0) * radius, rad1 = std::cos(p1) * radius;
            if (in_door(u, skirt_height + y0) && in_door(u, skirt_height + y1)) continue;
            rlVertex3f(dome_center.x + cu * rad0, dome_center.y + y0, dome_center.z + su * rad0);
            rlVertex3f(dome_center.x + cu * rad1, dome_center.y + y1, dome_center.z + su * rad1);
        }
        rlEnd();
    }
}

// Renderizar parede vertical texturizada (para laterais do terreno em altura). Fica manual
// (rlgl): geometria de UV customizado por face (parede extrudada entre duas alturas,
// eixo/sinal parametrizado) que nenhum primitivo de alto nivel da raylib cobre. Porte mecanico
// da mesma logica de switch por face, so trocando glBegin/glVertex3f/glColor4f/glTexCoord2f/
// glEnd por rlBegin(RL_QUADS)/rlVertex3f/rlColor4f/rlTexCoord2f/rlEnd.
void render_wall_3d_tex(WallFace face, float x, float z, float y0, float y1, Tile tile,
                         float tint_r, float tint_g, float tint_b, float a, float shade,
                         bool flat) {
    if (y1 <= y0) return;
    constexpr float half = 0.5f;
    UvRect uv = atlas_uv(tile);
    if (flat) {
        // Colapsa o retangulo pro seu proprio centro - os 4 rlTexCoord2f abaixo (u0,v0/u1,v0/
        // u1,v1/u0,v1) todos caem no MESMO texel, entao a face inteira vira uma cor solida
        // (sem gradiente de textura nenhum pra "tremer" numa faixa fina de poucos pixels).
        float cu = (uv.u0 + uv.u1) * 0.5f;
        float cv = (uv.v0 + uv.v1) * 0.5f;
        uv.u0 = uv.u1 = cu;
        uv.v0 = uv.v1 = cv;
    }

    float r = tint_r * shade, g = tint_g * shade, b = tint_b * shade;
    apply_frame_fog(x, (y0 + y1) * 0.5f, z, r, g, b);

    rlColor4f(r, g, b, a);
    rlBegin(RL_QUADS);
    switch (face) {
        case WallFace::XPos: {
            // Original: render_wall_3d_tex_xpos
            float xf = x + half;
            float z0 = z - half;
            float z1 = z + half;
            rlTexCoord2f(uv.u0, uv.v0); rlVertex3f(xf, y0, z0);
            rlTexCoord2f(uv.u1, uv.v0); rlVertex3f(xf, y0, z1);
            rlTexCoord2f(uv.u1, uv.v1); rlVertex3f(xf, y1, z1);
            rlTexCoord2f(uv.u0, uv.v1); rlVertex3f(xf, y1, z0);
            break;
        }
        case WallFace::XNeg: {
            // Original: render_wall_3d_tex_xneg
            float xf = x - half;
            float z0 = z - half;
            float z1 = z + half;
            rlTexCoord2f(uv.u0, uv.v0); rlVertex3f(xf, y0, z1);
            rlTexCoord2f(uv.u1, uv.v0); rlVertex3f(xf, y0, z0);
            rlTexCoord2f(uv.u1, uv.v1); rlVertex3f(xf, y1, z0);
            rlTexCoord2f(uv.u0, uv.v1); rlVertex3f(xf, y1, z1);
            break;
        }
        case WallFace::ZPos: {
            // Original: render_wall_3d_tex_zpos
            float zf = z + half;
            float x0 = x - half;
            float x1 = x + half;
            rlTexCoord2f(uv.u0, uv.v0); rlVertex3f(x0, y0, zf);
            rlTexCoord2f(uv.u1, uv.v0); rlVertex3f(x1, y0, zf);
            rlTexCoord2f(uv.u1, uv.v1); rlVertex3f(x1, y1, zf);
            rlTexCoord2f(uv.u0, uv.v1); rlVertex3f(x0, y1, zf);
            break;
        }
        case WallFace::ZNeg: {
            // Original: render_wall_3d_tex_zneg
            float zf = z - half;
            float x0 = x - half;
            float x1 = x + half;
            rlTexCoord2f(uv.u0, uv.v0); rlVertex3f(x1, y0, zf);
            rlTexCoord2f(uv.u1, uv.v0); rlVertex3f(x0, y0, zf);
            rlTexCoord2f(uv.u1, uv.v1); rlVertex3f(x0, y1, zf);
            rlTexCoord2f(uv.u0, uv.v1); rlVertex3f(x1, y1, zf);
            break;
        }
    }
    rlEnd();
}

// Ver comentario da declaracao em render_primitives.h.
void render_airlock_hatch_3d(Vec3 center, float wall_angle_rad, float radius, float glow) {
    float nx = std::cos(wall_angle_rad), nz = std::sin(wall_angle_rad);
    // 1) Chapa externa: clara, aro metalico, 16 parafusos.
    render_porthole_3d(center, wall_angle_rad, radius, radius * 0.22f,
                       0.80f, 0.81f, 0.84f, 1.0f,
                       0.62f, 0.64f, 0.68f, 22, 16, 0.0f);
    // 2) Disco interno: ESCURO, saliente, com aro proprio e as barras cruzadas.
    Vec3 leaf{center.x + nx * (radius * 0.05f), center.y, center.z + nz * (radius * 0.05f)};
    render_porthole_3d(leaf, wall_angle_rad, radius * 0.68f, radius * 0.10f,
                       0.30f, 0.33f, 0.38f, 1.0f,
                       0.55f, 0.57f, 0.62f, 20, 12, glow);
}

// Ver comentario da declaracao em render_primitives.h.
void render_box_oriented_3d(Vec3 center, float sx, float sy, float sz, float yaw_rad,
                            float r, float g, float b, float a) {
    float cs = std::cos(yaw_rad), sn = std::sin(yaw_rad);
    // Base local: frente = (sn, cos), direita = (cs, -sn) - a mesma do corpo do personagem.
    float rx = cs * (sx * 0.5f), rz = -sn * (sx * 0.5f);
    float fx = sn * (sz * 0.5f), fz = cs * (sz * 0.5f);
    float hy = sy * 0.5f;

    float cr = r, cg = g, cb = b;
    apply_frame_fog(center.x, center.y, center.z, cr, cg, cb);

    // 8 cantos: [frente/tras][direita/esquerda][topo/baixo]
    auto V = [&](float f, float rr, float u) {
        return Vec3{center.x + fx * f + rx * rr, center.y + hy * u, center.z + fz * f + rz * rr};
    };
    Vec3 ftr = V( 1.0f,  1.0f,  1.0f), ftl = V( 1.0f, -1.0f,  1.0f);
    Vec3 fbr = V( 1.0f,  1.0f, -1.0f), fbl = V( 1.0f, -1.0f, -1.0f);
    Vec3 btr = V(-1.0f,  1.0f,  1.0f), btl = V(-1.0f, -1.0f,  1.0f);
    Vec3 bbr = V(-1.0f,  1.0f, -1.0f), bbl = V(-1.0f, -1.0f, -1.0f);

    const float kTop = 1.00f, kSide = 0.72f, kDark = 0.52f;
    rlBegin(RL_QUADS);
    // Topo
    rlColor4f(cr * kTop, cg * kTop, cb * kTop, a);
    rlVertex3f(btl.x, btl.y, btl.z); rlVertex3f(btr.x, btr.y, btr.z);
    rlVertex3f(ftr.x, ftr.y, ftr.z); rlVertex3f(ftl.x, ftl.y, ftl.z);
    // Fundo
    rlColor4f(cr * kDark, cg * kDark, cb * kDark, a);
    rlVertex3f(fbl.x, fbl.y, fbl.z); rlVertex3f(fbr.x, fbr.y, fbr.z);
    rlVertex3f(bbr.x, bbr.y, bbr.z); rlVertex3f(bbl.x, bbl.y, bbl.z);
    // Frente
    rlColor4f(cr * kSide, cg * kSide, cb * kSide, a);
    rlVertex3f(fbl.x, fbl.y, fbl.z); rlVertex3f(fbr.x, fbr.y, fbr.z);
    rlVertex3f(ftr.x, ftr.y, ftr.z); rlVertex3f(ftl.x, ftl.y, ftl.z);
    // Tras
    rlColor4f(cr * kDark, cg * kDark, cb * kDark, a);
    rlVertex3f(bbr.x, bbr.y, bbr.z); rlVertex3f(bbl.x, bbl.y, bbl.z);
    rlVertex3f(btl.x, btl.y, btl.z); rlVertex3f(btr.x, btr.y, btr.z);
    // Direita
    rlColor4f(cr * kSide, cg * kSide, cb * kSide, a);
    rlVertex3f(fbr.x, fbr.y, fbr.z); rlVertex3f(bbr.x, bbr.y, bbr.z);
    rlVertex3f(btr.x, btr.y, btr.z); rlVertex3f(ftr.x, ftr.y, ftr.z);
    // Esquerda
    rlColor4f(cr * kDark, cg * kDark, cb * kDark, a);
    rlVertex3f(bbl.x, bbl.y, bbl.z); rlVertex3f(fbl.x, fbl.y, fbl.z);
    rlVertex3f(ftl.x, ftl.y, ftl.z); rlVertex3f(btl.x, btl.y, btl.z);
    rlEnd();
}

// Ver comentario da declaracao em render_primitives.h.
void render_box_tilted_3d(Vec3 center, float sx, float sy, float sz, float yaw_rad, float pitch_rad,
                          float r, float g, float b, float a) {
    float cy = std::cos(yaw_rad), sy_ = std::sin(yaw_rad);
    float cp = std::cos(pitch_rad), sp = std::sin(pitch_rad);

    // Base local: direita (nao afetada pelo pitch), frente e cima giradas pelo pitch em volta da
    // direita. Mesma convencao de frente/direita de render_box_oriented_3d.
    Vec3 right = { cy, 0.0f, -sy_ };
    Vec3 fwd   = { sy_ * cp, sp, cy * cp };
    Vec3 up    = { -sy_ * sp, cp, -cy * sp };

    float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
    float cr = r, cg = g, cb = b;
    apply_frame_fog(center.x, center.y, center.z, cr, cg, cb);

    auto V = [&](float f, float rr, float u) {
        return Vec3{ center.x + fwd.x * (hz * f) + right.x * (hx * rr) + up.x * (hy * u),
                     center.y + fwd.y * (hz * f) + right.y * (hx * rr) + up.y * (hy * u),
                     center.z + fwd.z * (hz * f) + right.z * (hx * rr) + up.z * (hy * u) };
    };
    Vec3 ftr = V( 1.0f,  1.0f,  1.0f), ftl = V( 1.0f, -1.0f,  1.0f);
    Vec3 fbr = V( 1.0f,  1.0f, -1.0f), fbl = V( 1.0f, -1.0f, -1.0f);
    Vec3 btr = V(-1.0f,  1.0f,  1.0f), btl = V(-1.0f, -1.0f,  1.0f);
    Vec3 bbr = V(-1.0f,  1.0f, -1.0f), bbl = V(-1.0f, -1.0f, -1.0f);

    const float kTop = 1.00f, kSide = 0.72f, kDark = 0.52f;
    rlBegin(RL_QUADS);
    rlColor4f(cr * kTop, cg * kTop, cb * kTop, a);
    rlVertex3f(btl.x, btl.y, btl.z); rlVertex3f(btr.x, btr.y, btr.z);
    rlVertex3f(ftr.x, ftr.y, ftr.z); rlVertex3f(ftl.x, ftl.y, ftl.z);
    rlColor4f(cr * kDark, cg * kDark, cb * kDark, a);
    rlVertex3f(fbl.x, fbl.y, fbl.z); rlVertex3f(fbr.x, fbr.y, fbr.z);
    rlVertex3f(bbr.x, bbr.y, bbr.z); rlVertex3f(bbl.x, bbl.y, bbl.z);
    rlColor4f(cr * kSide, cg * kSide, cb * kSide, a);
    rlVertex3f(fbl.x, fbl.y, fbl.z); rlVertex3f(fbr.x, fbr.y, fbr.z);
    rlVertex3f(ftr.x, ftr.y, ftr.z); rlVertex3f(ftl.x, ftl.y, ftl.z);
    rlColor4f(cr * kDark, cg * kDark, cb * kDark, a);
    rlVertex3f(bbr.x, bbr.y, bbr.z); rlVertex3f(bbl.x, bbl.y, bbl.z);
    rlVertex3f(btl.x, btl.y, btl.z); rlVertex3f(btr.x, btr.y, btr.z);
    rlColor4f(cr * kSide, cg * kSide, cb * kSide, a);
    rlVertex3f(fbr.x, fbr.y, fbr.z); rlVertex3f(bbr.x, bbr.y, bbr.z);
    rlVertex3f(btr.x, btr.y, btr.z); rlVertex3f(ftr.x, ftr.y, ftr.z);
    rlColor4f(cr * kSide * 0.88f, cg * kSide * 0.88f, cb * kSide * 0.88f, a);
    rlVertex3f(bbl.x, bbl.y, bbl.z); rlVertex3f(fbl.x, fbl.y, fbl.z);
    rlVertex3f(ftl.x, ftl.y, ftl.z); rlVertex3f(btl.x, btl.y, btl.z);
    rlEnd();
}
