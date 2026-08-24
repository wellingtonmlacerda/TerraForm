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
                           float skirt_height) {
    rlSetTexture(0);
    bool has_door = door_half_angle > 0.0f && door_height > 0.0f;
    // y_world aqui e sempre altura ABSOLUTA a partir do chao (base_center.y), nao relativa
    // ao hemisferio - a saia (abaixo) fica entre 0 e skirt_height, e o hemisferio comeca em
    // skirt_height; com door_height <= skirt_height (ver chamada em main.cpp) a porta fica
    // inteira dentro da saia, entao o hemisferio nunca precisa recortar nada de verdade -
    // a checagem continua aqui so por seguranca/generalidade.
    auto in_door = [&](float u, float y_world) -> bool {
        if (!has_door) return false;
        if (y_world > door_height) return false;
        return std::fabs(shortest_angle_diff(u, door_facing_rad)) <= door_half_angle;
    };

    // --- Saia cilindrica (fundacao) ---
    // Fecha o vao entre o chao e a casca do hemisferio (antes a malha comecava direto no
    // chao, então em terreno levemente irregular sobrava uma fresta visivel por baixo) e da
    // uma base solida onde a porta "encaixa" de verdade. Tom levemente mais frio/metalico
    // que a casca tan de cima (fundacao vs tecido, como num domo real). A porta (arco em
    // torno de door_facing_rad) NAO e um buraco aqui - e renderizada com um tom metalico
    // distinto (fechada, solida) em vez de pulada, dando o efeito de alcapao sempre fechado
    // sem nenhuma abertura visivel.
    if (skirt_height > 0.0f) {
        float found_r = r * 0.55f + 0.16f, found_g = g * 0.55f + 0.18f, found_b = b * 0.55f + 0.22f;
        float door_r = 0.30f, door_g = 0.32f, door_b = 0.36f;

        rlBegin(RL_QUADS);
        for (int lon = 0; lon < lon_seg; ++lon) {
            float u0 = (float)lon / (float)lon_seg * 2.0f * kPi;
            float u1 = (float)(lon + 1) / (float)lon_seg * 2.0f * kPi;
            float cu0 = std::cos(u0), su0 = std::sin(u0);
            float cu1 = std::cos(u1), su1 = std::sin(u1);
            Vec3 v_b0{base_center.x + cu0 * radius, base_center.y, base_center.z + su0 * radius};
            Vec3 v_b1{base_center.x + cu1 * radius, base_center.y, base_center.z + su1 * radius};
            Vec3 v_t0{base_center.x + cu0 * radius, base_center.y + skirt_height, base_center.z + su0 * radius};
            Vec3 v_t1{base_center.x + cu1 * radius, base_center.y + skirt_height, base_center.z + su1 * radius};

            // OR (nao AND) nos 2 extremos + ponto medio: com o vao da porta so um pouco mais
            // largo que 1 segmento de longitude e centrado bem em cima de uma fronteira entre
            // segmentos, NENHUM segmento tinha os 2 extremos dentro do arco ao mesmo tempo -
            // a porta nunca ficava colorida diferente (bug real: "nao acho a porta" reportado).
            // Checar qualquer sobreposicao (comeco, meio ou fim do segmento) garante que pelo
            // menos os segmentos que tocam o arco da porta pegam a cor metalica distinta.
            float um = (u0 + u1) * 0.5f;
            bool seg_is_door = in_door(u0, 0.0f) || in_door(u1, 0.0f) || in_door(um, 0.0f);
            float sr = seg_is_door ? door_r : found_r;
            float sg = seg_is_door ? door_g : found_g;
            float sb = seg_is_door ? door_b : found_b;

            rlColor4f(sr * 0.55f, sg * 0.55f, sb * 0.55f, a);
            rlVertex3f(v_b0.x, v_b0.y, v_b0.z);
            rlVertex3f(v_b1.x, v_b1.y, v_b1.z);
            rlColor4f(sr * 0.85f, sg * 0.85f, sb * 0.85f, a);
            rlVertex3f(v_t1.x, v_t1.y, v_t1.z);
            rlVertex3f(v_t0.x, v_t0.y, v_t0.z);
        }
        rlEnd();

        // Linha de fundacao no chao (reforca a leitura de "estrutura construida").
        rlBegin(RL_LINES);
        rlColor4f(0.04f, 0.04f, 0.04f, a * 0.6f);
        for (int lon = 0; lon < lon_seg; ++lon) {
            float u0 = (float)lon / (float)lon_seg * 2.0f * kPi;
            float u1 = (float)(lon + 1) / (float)lon_seg * 2.0f * kPi;
            rlVertex3f(base_center.x + std::cos(u0) * radius, base_center.y, base_center.z + std::sin(u0) * radius);
            rlVertex3f(base_center.x + std::cos(u1) * radius, base_center.y, base_center.z + std::sin(u1) * radius);
        }
        rlEnd();

        // Detalhe da porta (moldura + costura central + 2 luzes de status) - so desenhado se
        // houver porta; da o "tipo espaçonave" pedido sem precisar de textura nova.
        if (has_door) {
            float hy0 = base_center.y, hy1 = base_center.y + door_height;
            float um = door_facing_rad;
            float cum = std::cos(um), sum = std::sin(um);
            float ul = door_facing_rad - door_half_angle, ur = door_facing_rad + door_half_angle;

            rlBegin(RL_LINES);
            rlColor4f(0.02f, 0.02f, 0.03f, a * 0.85f);
            // Moldura: 2 verticais (bordas esquerda/direita) + 1 horizontal (topo)
            rlVertex3f(base_center.x + std::cos(ul) * radius, hy0, base_center.z + std::sin(ul) * radius);
            rlVertex3f(base_center.x + std::cos(ul) * radius, hy1, base_center.z + std::sin(ul) * radius);
            rlVertex3f(base_center.x + std::cos(ur) * radius, hy0, base_center.z + std::sin(ur) * radius);
            rlVertex3f(base_center.x + std::cos(ur) * radius, hy1, base_center.z + std::sin(ur) * radius);
            for (int i = 0; i < 6; ++i) {
                float a0 = ul + (ur - ul) * (float)i / 6.0f;
                float a1 = ul + (ur - ul) * (float)(i + 1) / 6.0f;
                rlVertex3f(base_center.x + std::cos(a0) * radius, hy1, base_center.z + std::sin(a0) * radius);
                rlVertex3f(base_center.x + std::cos(a1) * radius, hy1, base_center.z + std::sin(a1) * radius);
            }
            // Costura central (porta dupla, estilo alcapao)
            rlVertex3f(base_center.x + cum * radius, hy0, base_center.z + sum * radius);
            rlVertex3f(base_center.x + cum * radius, hy1, base_center.z + sum * radius);
            rlEnd();

            // 2 luzes de status (pequenos quadrados ambar) perto do topo da porta.
            float light_y = hy0 + door_height * 0.82f;
            float lo = door_half_angle * 0.55f;
            for (float sgn : {-1.0f, 1.0f}) {
                float au = door_facing_rad + sgn * lo;
                float lx = base_center.x + std::cos(au) * (radius + 0.02f);
                float lz = base_center.z + std::sin(au) * (radius + 0.02f);
                render_cube_3d(lx, light_y, lz, 0.14f, 0.95f, 0.70f, 0.20f, a);
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
