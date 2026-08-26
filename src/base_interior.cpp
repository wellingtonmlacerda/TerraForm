#include "base_interior.h"

#include "raylib_platform.h"
#include "math_core.h"          // Vec3, kPi, clamp01, kHeightScale
#include "noise.h"              // lerp
#include "blocks.h"
#include "world.h"              // World, g_world
#include "camera.h"             // g_camera (neblina)
#include "player_physics.h"     // get_player_render_pos
#include "lighting.h"           // g_lighting, sample_lightmap, apply_color_grading
#include "render_primitives.h"  // g_frame_fog, render_sphere_3d, render_glow_disc_3d, render_porthole_3d
#include "interiors.h"          // kInteriors, interior_at, interior_floor_y
#include "modules_building.h"   // g_greenhouse_output

#include <algorithm>
#include <cmath>

extern float g_day_time;

// Indices em kInteriors - a ordem da tabela em interiors.cpp.
enum : int {
    kHall = 0, kCorrN = 1, kCorrS = 2, kCorrL = 3, kCorrO = 4, kRamNE = 5, kRamSO = 6,
    kAirlock = 7, kLab = 8, kCtrl = 9, kDorm = 10, kGreen = 11, kShop = 12
};

// ============= MOBILIA / MAQUINARIO =============
// dx/dz relativos ao CENTRO DO DISTRITO (mesmo referencial dos retangulos de kInteriors).
// ESCALA: com pe-direito de 9 a 16, os moveis de 2.10 da versao anterior liam como brinquedo. Agora
// as salas tecnicas tem maquinario FurnitureHuge (4.20), e a densidade e' menor - sala grande vazia le
// como instalacao, sala grande lotada de moveis pequenos le como depósito.
const FurnPiece kFurniture[] = {
    // ---------- SAGUAO CENTRAL: pouco no chao, de proposito. O volume e' o protagonista. ----------
    { kHall,  -8,  -8, 3, 3, 4.20f, Block::FurnitureHuge, 0.42f, 0.44f, 0.48f, false },  // torre tecnica
    { kHall,   8,  -8, 3, 3, 4.20f, Block::FurnitureHuge, 0.42f, 0.44f, 0.48f, false },
    { kHall,  -8,   8, 3, 3, 4.20f, Block::FurnitureHuge, 0.42f, 0.44f, 0.48f, false },
    { kHall,   8,   8, 3, 3, 4.20f, Block::FurnitureHuge, 0.42f, 0.44f, 0.48f, false },
    { kHall,   0,   9, 7, 1, 1.10f, Block::FurnitureMid,  0.32f, 0.34f, 0.38f, false },  // balcao
    { kHall,  -4,   9, 1, 1, 0.95f, Block::FurnitureLow,  0.24f, 0.62f, 0.74f, true  },  // tela
    { kHall,   4,   9, 1, 1, 0.95f, Block::FurnitureLow,  0.26f, 0.70f, 0.56f, true  },
    { kHall,   0,  -9, 5, 3, 0.75f, Block::FurnitureMid,  0.50f, 0.42f, 0.32f, false },  // mesa comum
    { kHall,  -3,  -9, 1, 1, 0.45f, Block::FurnitureLow,  0.36f, 0.37f, 0.40f, false },
    { kHall,   3,  -9, 1, 1, 0.45f, Block::FurnitureLow,  0.36f, 0.37f, 0.40f, false },

    // ---------- ECLUSA (17x12, h10): bancadas de traje + racks ----------
    { kAirlock, -6, 32, 1, 5, 2.10f, Block::FurnitureTall, 0.84f, 0.85f, 0.88f, false },
    { kAirlock,  6, 32, 1, 5, 2.10f, Block::FurnitureTall, 0.84f, 0.85f, 0.88f, false },
    { kAirlock,  0, 33, 9, 1, 1.10f, Block::FurnitureMid,  0.44f, 0.46f, 0.50f, false },
    { kAirlock, -6, 25, 3, 3, 4.20f, Block::FurnitureHuge, 0.40f, 0.42f, 0.46f, false },  // compressor
    { kAirlock,  6, 25, 3, 3, 4.20f, Block::FurnitureHuge, 0.40f, 0.42f, 0.46f, false },

    // ---------- LABORATORIO (19x19, h13) ----------
    { kLab, 27,  -6, 3, 3, 4.20f, Block::FurnitureHuge, 0.38f, 0.42f, 0.48f, false },  // centrifuga
    { kLab, 27,   6, 3, 3, 4.20f, Block::FurnitureHuge, 0.38f, 0.42f, 0.48f, false },
    { kLab, 34,   0, 1, 13, 1.10f, Block::FurnitureMid,  0.28f, 0.30f, 0.34f, false }, // bancada longa
    { kLab, 34,  -4, 1, 1, 0.95f, Block::FurnitureLow,  0.24f, 0.66f, 0.78f, true  },
    { kLab, 34,   0, 1, 1, 0.95f, Block::FurnitureLow,  0.28f, 0.74f, 0.60f, true  },
    { kLab, 34,   4, 1, 1, 0.95f, Block::FurnitureLow,  0.74f, 0.54f, 0.24f, true  },
    { kLab, 30,   0, 3, 3, 1.10f, Block::FurnitureMid,  0.36f, 0.38f, 0.42f, false }, // mesa central
    { kLab, 39,  -7, 1, 3, 2.10f, Block::FurnitureTall, 0.86f, 0.87f, 0.90f, false },
    { kLab, 39,   7, 1, 3, 2.10f, Block::FurnitureTall, 0.86f, 0.87f, 0.90f, false },

    // ---------- SALA DE CONTROLE (19x14, h13) ----------
    { kCtrl,  0, -35, 13, 1, 1.10f, Block::FurnitureMid, 0.26f, 0.28f, 0.32f, false }, // console
    { kCtrl, -5, -35, 1, 1, 0.95f, Block::FurnitureLow, 0.26f, 0.60f, 0.78f, true },
    { kCtrl,  0, -35, 1, 1, 0.95f, Block::FurnitureLow, 0.30f, 0.72f, 0.60f, true },
    { kCtrl,  5, -35, 1, 1, 0.95f, Block::FurnitureLow, 0.72f, 0.52f, 0.24f, true },
    { kCtrl, -6, -31, 1, 3, 1.10f, Block::FurnitureMid, 0.26f, 0.28f, 0.32f, false },
    { kCtrl,  6, -31, 1, 3, 1.10f, Block::FurnitureMid, 0.26f, 0.28f, 0.32f, false },
    { kCtrl, -3, -32, 1, 1, 0.45f, Block::FurnitureLow, 0.36f, 0.37f, 0.40f, false },
    { kCtrl,  3, -32, 1, 1, 0.45f, Block::FurnitureLow, 0.36f, 0.37f, 0.40f, false },
    { kCtrl, -7, -25, 3, 3, 4.20f, Block::FurnitureHuge, 0.36f, 0.38f, 0.44f, false }, // rack de servidor
    { kCtrl,  7, -25, 3, 3, 4.20f, Block::FurnitureHuge, 0.36f, 0.38f, 0.44f, false },

    // ---------- DORMITORIO (18x19, h9): 4 beliches + armarios ----------
    { kDorm, -36, -5, 5, 3, 0.70f, Block::FurnitureLow,  0.40f, 0.42f, 0.46f, false },
    { kDorm, -36,  5, 5, 3, 0.70f, Block::FurnitureLow,  0.40f, 0.42f, 0.46f, false },
    { kDorm, -27, -5, 5, 3, 0.70f, Block::FurnitureLow,  0.40f, 0.42f, 0.46f, false },
    { kDorm, -27,  5, 5, 3, 0.70f, Block::FurnitureLow,  0.40f, 0.42f, 0.46f, false },
    { kDorm, -32,  0, 3, 1, 1.10f, Block::FurnitureMid,  0.44f, 0.46f, 0.50f, false },
    { kDorm, -39, -8, 1, 1, 2.10f, Block::FurnitureTall, 0.88f, 0.88f, 0.92f, false },
    { kDorm, -39,  8, 1, 1, 2.10f, Block::FurnitureTall, 0.88f, 0.88f, 0.92f, false },

    // ---------- ESTUFA (19x15, h14): tanques industriais + bancada ----------
    { kGreen, 17, -25, 3, 3, 4.20f, Block::FurnitureHuge, 0.32f, 0.48f, 0.58f, false }, // tanque de agua
    { kGreen, 17, -13, 3, 3, 4.20f, Block::FurnitureHuge, 0.32f, 0.48f, 0.58f, false },
    { kGreen, 31, -19, 1, 5, 1.10f, Block::FurnitureMid,  0.44f, 0.46f, 0.50f, false },

    // ---------- OFICINA (18x15, h12): maquinario pesado ----------
    { kShop, -29, 14, 3, 3, 4.20f, Block::FurnitureHuge, 0.44f, 0.40f, 0.34f, false },
    { kShop, -22, 14, 3, 3, 4.20f, Block::FurnitureHuge, 0.44f, 0.40f, 0.34f, false },
    { kShop, -29, 24, 3, 3, 4.20f, Block::FurnitureHuge, 0.44f, 0.40f, 0.34f, false },
    { kShop, -18, 19, 1, 9, 1.10f, Block::FurnitureMid,  0.42f, 0.40f, 0.36f, false }, // bancada
    { kShop, -24, 19, 3, 1, 1.10f, Block::FurnitureMid,  0.42f, 0.40f, 0.36f, false },
};
const int kFurnitureCount = (int)(sizeof(kFurniture) / sizeof(kFurniture[0]));

void base_interior_stamp_furniture(World& world) {
    int ox, oz;
    interior_district_center(ox, oz);
    for (int i = 0; i < kFurnitureCount; ++i) {
        const FurnPiece& f = kFurniture[i];
        if (f.interior < 0 || f.interior >= kInteriorCount) continue;
        const InteriorDef& d = kInteriors[f.interior];
        int spawn_x = ox + d.spawn_x, spawn_z = oz + d.spawn_z;
        int exit_x  = ox + d.exit_x,  exit_z  = oz + d.exit_z;

        int hx = (f.w_dx - 1) / 2, hz = (f.w_dz - 1) / 2;
        for (int ddx = -hx; ddx <= hx; ++ddx)
            for (int ddz = -hz; ddz <= hz; ++ddz) {
                int tx = ox + f.dx + ddx, tz = oz + f.dz + ddz;
                if (!world.in_bounds(tx, tz)) continue;
                // ZONA LIVRE de raio 2 em volta do spawn e da SAIDA. Antes eu excluia so' o tile
                // exato, e medido no mundo gerado a Eclusa e a Sala de Controle tinham 4 moveis cada
                // a <=2 tiles da saida - encostados no portao, entupindo a soleira. E' o "movel colado
                // com a porta". 2 tiles deixa a passagem e o apron amarelo desimpedidos.
                auto too_close = [&](int qx, int qz) {
                    int a = tx - qx, b = tz - qz;
                    return a * a + b * b <= 4;
                };
                if (too_close(spawn_x, spawn_z)) continue;
                if (too_close(exit_x, exit_z)) continue;
                if (world.get_ground(tx, tz) != Block::BaseFloor) continue;  // nunca em canteiro/slot
                if (world.stack_height_at(tx, tz) > 0) continue;             // nunca dentro de parede
                world.set(tx, tz, f.collider);
            }
    }
}

// ============= LUMINARIAS =============
// Altura casada com o pe-direito de cada ambiente (a luz mora no teto, nao 3 acima do chao).
// Mais que o dobro da versao anterior: com 1-2 luminarias num ambiente de 19x19 sobravam cantos sem
// nenhuma poca de luz. O teto de 32 luzes (lighting.cpp) so' recebe as do ambiente ATUAL, entao 18
// entradas por ambiente ainda cabem folgadas.
const BaseLamp kBaseLamps[] = {
    // Saguao: grade 3x3, so' que sem a central (o centro fica pro anel de piso e pra vista da
    // passarela). Fileiras a 14.5, altura do teto de 16.
    { kHall,   -8.0f,  -8.0f, 14.5f, 1.00f, 0.96f, 0.86f, 11.0f, 0.34f },
    { kHall,    0.0f,  -8.0f, 14.5f, 1.00f, 0.96f, 0.86f, 11.0f, 0.34f },
    { kHall,    8.0f,  -8.0f, 14.5f, 1.00f, 0.96f, 0.86f, 11.0f, 0.34f },
    { kHall,   -8.0f,   0.0f, 14.5f, 1.00f, 0.96f, 0.86f, 11.0f, 0.34f },
    { kHall,    8.0f,   0.0f, 14.5f, 1.00f, 0.96f, 0.86f, 11.0f, 0.34f },
    { kHall,   -8.0f,   8.0f, 14.5f, 1.00f, 0.96f, 0.86f, 11.0f, 0.34f },
    { kHall,    0.0f,   8.0f, 14.5f, 1.00f, 0.96f, 0.86f, 11.0f, 0.34f },
    { kHall,    8.0f,   8.0f, 14.5f, 1.00f, 0.96f, 0.86f, 11.0f, 0.34f },
    // Corredores: 3 por corredor, ritmo de calha corrida.
    { kCorrN,   0.0f, -15.0f,  8.0f, 0.78f, 0.90f, 1.00f,  8.0f, 0.30f },
    { kCorrN,   0.0f, -18.0f,  8.0f, 0.78f, 0.90f, 1.00f,  8.0f, 0.30f },
    { kCorrN,   0.0f, -21.0f,  8.0f, 0.78f, 0.90f, 1.00f,  8.0f, 0.30f },
    { kCorrS,   0.0f,  15.0f,  8.0f, 0.78f, 0.90f, 1.00f,  8.0f, 0.30f },
    { kCorrS,   0.0f,  18.0f,  8.0f, 0.78f, 0.90f, 1.00f,  8.0f, 0.30f },
    { kCorrS,   0.0f,  21.0f,  8.0f, 0.78f, 0.90f, 1.00f,  8.0f, 0.30f },
    { kCorrL,  15.0f,   0.0f,  8.0f, 0.78f, 0.90f, 1.00f,  8.0f, 0.30f },
    { kCorrL,  18.0f,   0.0f,  8.0f, 0.78f, 0.90f, 1.00f,  8.0f, 0.30f },
    { kCorrL,  21.0f,   0.0f,  8.0f, 0.78f, 0.90f, 1.00f,  8.0f, 0.30f },
    { kCorrO, -15.0f,   0.0f,  8.0f, 0.78f, 0.90f, 1.00f,  8.0f, 0.30f },
    { kCorrO, -18.0f,   0.0f,  8.0f, 0.78f, 0.90f, 1.00f,  8.0f, 0.30f },
    { kCorrO, -21.0f,   0.0f,  8.0f, 0.78f, 0.90f, 1.00f,  8.0f, 0.30f },
    { kRamNE,   6.0f, -17.0f,  7.0f, 0.78f, 0.90f, 1.00f,  7.0f, 0.28f },
    { kRamNE,  12.0f, -17.0f,  7.0f, 0.78f, 0.90f, 1.00f,  7.0f, 0.28f },
    { kRamSO,  -6.0f,  17.0f,  7.0f, 0.78f, 0.90f, 1.00f,  7.0f, 0.28f },
    { kRamSO, -12.0f,  17.0f,  7.0f, 0.78f, 0.90f, 1.00f,  7.0f, 0.28f },
    // Eclusa
    { kAirlock, -5.0f, 26.0f,  9.0f, 0.86f, 0.92f, 1.00f,  9.0f, 0.32f },
    { kAirlock,  5.0f, 26.0f,  9.0f, 0.86f, 0.92f, 1.00f,  9.0f, 0.32f },
    { kAirlock, -5.0f, 31.0f,  9.0f, 0.86f, 0.92f, 1.00f,  9.0f, 0.32f },
    { kAirlock,  5.0f, 31.0f,  9.0f, 0.86f, 0.92f, 1.00f,  9.0f, 0.32f },
    // Laboratorio
    { kLab,    28.0f,  -5.0f, 11.5f, 0.90f, 0.96f, 1.00f, 10.0f, 0.34f },
    { kLab,    28.0f,   5.0f, 11.5f, 0.90f, 0.96f, 1.00f, 10.0f, 0.34f },
    { kLab,    36.0f,  -5.0f, 11.5f, 0.90f, 0.96f, 1.00f, 10.0f, 0.34f },
    { kLab,    36.0f,   5.0f, 11.5f, 0.90f, 0.96f, 1.00f, 10.0f, 0.34f },
    // Sala de Controle
    { kCtrl,   -5.0f, -34.0f, 11.5f, 0.84f, 0.92f, 1.00f, 10.0f, 0.34f },
    { kCtrl,    5.0f, -34.0f, 11.5f, 0.84f, 0.92f, 1.00f, 10.0f, 0.34f },
    { kCtrl,   -5.0f, -27.0f, 11.5f, 0.84f, 0.92f, 1.00f, 10.0f, 0.34f },
    { kCtrl,    5.0f, -27.0f, 11.5f, 0.84f, 0.92f, 1.00f, 10.0f, 0.34f },
    // Dormitorio (mais quente - e' o unico ambiente de descanso)
    { kDorm,  -35.0f,  -5.0f,  8.0f, 1.00f, 0.90f, 0.72f,  9.0f, 0.30f },
    { kDorm,  -35.0f,   5.0f,  8.0f, 1.00f, 0.90f, 0.72f,  9.0f, 0.30f },
    { kDorm,  -27.0f,  -5.0f,  8.0f, 1.00f, 0.90f, 0.72f,  9.0f, 0.30f },
    { kDorm,  -27.0f,   5.0f,  8.0f, 1.00f, 0.90f, 0.72f,  9.0f, 0.30f },
    // Estufa: lampadas de cultivo magenta - leitura instantanea de "estufa".
    { kGreen,  19.0f, -23.0f, 12.5f, 1.00f, 0.42f, 0.85f, 10.0f, 0.38f },
    { kGreen,  29.0f, -23.0f, 12.5f, 1.00f, 0.42f, 0.85f, 10.0f, 0.38f },
    { kGreen,  19.0f, -15.0f, 12.5f, 1.00f, 0.42f, 0.85f, 10.0f, 0.38f },
    { kGreen,  29.0f, -15.0f, 12.5f, 1.00f, 0.42f, 0.85f, 10.0f, 0.38f },
    { kGreen,  24.0f, -19.0f, 12.5f, 1.00f, 0.96f, 0.90f, 10.0f, 0.30f },  // corredor central: branca
    // Oficina
    { kShop,  -28.0f,  15.0f, 10.5f, 1.00f, 0.90f, 0.72f, 10.0f, 0.34f },
    { kShop,  -20.0f,  15.0f, 10.5f, 1.00f, 0.90f, 0.72f, 10.0f, 0.34f },
    { kShop,  -28.0f,  23.0f, 10.5f, 1.00f, 0.90f, 0.72f, 10.0f, 0.34f },
    { kShop,  -20.0f,  23.0f, 10.5f, 1.00f, 0.90f, 0.72f, 10.0f, 0.34f },
};
const int kBaseLampCount = (int)(sizeof(kBaseLamps) / sizeof(kBaseLamps[0]));

namespace {

void fog(float wx, float wy, float wz, float& r, float& g, float& b) {
    if (!g_frame_fog.enabled) return;
    float dx = wx - g_camera.position.x, dy = wy - g_camera.position.y, dz = wz - g_camera.position.z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    float span = std::max(0.0001f, g_frame_fog.end - g_frame_fog.start);
    float f = clamp01((g_frame_fog.end - dist) / span);
    r = lerp(g_frame_fog.r, r, f);
    g = lerp(g_frame_fog.g, g, f);
    b = lerp(g_frame_fog.b, b, f);
}

void lit(float wx, float wz, float& r, float& g, float& b) {
    if (!g_lighting.enabled) return;
    float lr, lg, lb;
    sample_lightmap(wx, wz, lr, lg, lb);
    r *= lr; g *= lg; b *= lb;
    apply_color_grading(r, g, b);
}

// Quad vertical arbitrario (parede/painel). Single-sided; com backface culling desabilitado le dos
// dois lados.
void wall_quad(float x0, float z0, float x1, float z1, float y0, float y1,
               float r, float g, float b, float a, float sh_bot, float sh_top) {
    float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f, cy = (y0 + y1) * 0.5f;
    lit(cx, cz, r, g, b);
    float rb = r * sh_bot, gb = g * sh_bot, bb = b * sh_bot;
    float rt = r * sh_top, gt = g * sh_top, bt = b * sh_top;
    fog(cx, y0, cz, rb, gb, bb);
    fog(cx, y1, cz, rt, gt, bt);
    (void)cy;
    rlBegin(RL_QUADS);
    rlColor4f(rb, gb, bb, a);
    rlVertex3f(x0, y0, z0); rlVertex3f(x1, y0, z1);
    rlColor4f(rt, gt, bt, a);
    rlVertex3f(x1, y1, z1); rlVertex3f(x0, y1, z0);
    rlEnd();
}

void quad_y(float x0, float z0, float x1, float z1, float y,
            float r, float g, float b, float a, bool emissive = false) {
    float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
    if (!emissive) { lit(cx, cz, r, g, b); fog(cx, y, cz, r, g, b); }
    rlBegin(RL_QUADS);
    rlColor4f(r, g, b, a);
    rlVertex3f(x0, y, z0); rlVertex3f(x1, y, z0); rlVertex3f(x1, y, z1); rlVertex3f(x0, y, z1);
    rlEnd();
}

// Caixa AABB com as 3 sombras por face de render_cube_3d (mobilia quase nunca e' cubica).
void box(float x0, float y0, float z0, float x1, float y1, float z1,
         float r, float g, float b, float a = 1.0f, bool with_bottom = false, bool emissive = false) {
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, cz = (z0 + z1) * 0.5f;
    if (!emissive) lit(cx, cz, r, g, b);
    fog(cx, cy, cz, r, g, b);
    rlBegin(RL_QUADS);
    rlColor4f(r, g, b, a);
    rlVertex3f(x0, y1, z0); rlVertex3f(x1, y1, z0); rlVertex3f(x1, y1, z1); rlVertex3f(x0, y1, z1);
    rlColor4f(r * 0.74f, g * 0.74f, b * 0.74f, a);
    rlVertex3f(x0, y0, z1); rlVertex3f(x1, y0, z1); rlVertex3f(x1, y1, z1); rlVertex3f(x0, y1, z1);
    rlColor4f(r * 0.52f, g * 0.52f, b * 0.52f, a);
    rlVertex3f(x1, y0, z0); rlVertex3f(x0, y0, z0); rlVertex3f(x0, y1, z0); rlVertex3f(x1, y1, z0);
    rlColor4f(r * 0.74f, g * 0.74f, b * 0.74f, a);
    rlVertex3f(x1, y0, z1); rlVertex3f(x1, y0, z0); rlVertex3f(x1, y1, z0); rlVertex3f(x1, y1, z1);
    rlColor4f(r * 0.52f, g * 0.52f, b * 0.52f, a);
    rlVertex3f(x0, y0, z0); rlVertex3f(x0, y0, z1); rlVertex3f(x0, y1, z1); rlVertex3f(x0, y1, z0);
    if (with_bottom) {
        rlColor4f(r * 0.44f, g * 0.44f, b * 0.44f, a);
        rlVertex3f(x0, y0, z1); rlVertex3f(x1, y0, z1); rlVertex3f(x1, y0, z0); rlVertex3f(x0, y0, z0);
    }
    rlEnd();
}

void box_c(float cx, float cy, float cz, float sx, float sy, float sz,
           float r, float g, float b, float a = 1.0f, bool with_bottom = false, bool emissive = false) {
    box(cx - sx * 0.5f, cy, cz - sz * 0.5f, cx + sx * 0.5f, cy + sy, cz + sz * 0.5f,
        r, g, b, a, with_bottom, emissive);
}

float hash01(int i, float salt) {
    float x = std::sin((float)i * 12.9898f + salt * 78.233f) * 43758.5453f;
    return x - std::floor(x);
}

} // namespace

bool base_interior_ambient(float world_x, float world_z, float& out_r, float& out_g, float& out_b) {
    int i = interior_at(world_x, world_z);
    if (i < 0) return false;
    // Base absoluta de iluminacao artificial. As luminarias somam por cima disto (collect_lights), o
    // que ainda deixa poca de luz debaixo delas - a base so' garante que nada renderize preto.
    // 0.38, nao 0.50: o lightmap SOMA as luminarias por cima disto e e' clampado em 1.0
    // (lighting.cpp). Com 0.50 a soma saturava em quase todo lugar e o interior ficava chapado -
    // sem poca de luz, sem sombra. 0.38 deixa ~0.6 de espaco pras luminarias desenharem contraste.
    float a = 0.38f;
    float r = 1.00f, g = 0.98f, b = 0.95f;   // branco levemente quente
    // Tom por ambiente: corredor mais frio, estufa magenta, oficina ambar. E' o que faz cada ala ter
    // identidade sem precisar de textura nova.
    if (!kInteriors[i].is_room) { r = 0.88f; g = 0.95f; b = 1.00f; a = 0.33f; }
    else if (i == kGreen)       { r = 1.00f; g = 0.82f; b = 0.96f; a = 0.40f; }
    else if (i == kShop)        { r = 1.00f; g = 0.94f; b = 0.84f; a = 0.36f; }
    else if (i == kLab || i == kCtrl) { r = 0.94f; g = 0.98f; b = 1.00f; a = 0.40f; }
    out_r = a * r; out_g = a * g; out_b = a * b;
    return true;
}

// ============= DESENHO DE UM AMBIENTE =============
namespace {

// Paredes: um quad do piso ao teto por trecho de borda que NAO da pra outro ambiente. Aberturas entre
// ambientes vizinhos aparecem sozinhas (a parede so' existe onde ha bloco). Um quad de 1 tile de
// largura por N de altura - com pe-direito de 16 isso e' bem mais barato que 16 cubos.
void draw_walls(int idx, int ox, int oz, float fy) {
    const InteriorDef& d = kInteriors[idx];
    const float top = fy + d.ceiling;
    // 0.68, nao 0.88: o lightmap e' clampado em 1.0 mas 0.88 ainda chega em branco puro na poca da
    // luminaria, apagando o sombreamento vertical do painel. 0.68 mantem o casco claro e deixa a
    // variacao visivel - era o "tudo lavado de branco" dos screenshots.
    const float pr = 0.68f, pg = 0.69f, pb = 0.72f;   // painel claro

    // Percorre as 4 bordas. Pra cada tile de borda, se o vizinho de FORA tem pilha (parede), desenha.
    struct Edge { int ax, az; };   // normal apontando pra fora
    const Edge edges[4] = { {0,-1}, {0,1}, {-1,0}, {1,0} };
    for (int e = 0; e < 4; ++e) {
        const Edge& ed = edges[e];
        int x_lo = d.x0, x_hi = d.x1, z_lo = d.z0, z_hi = d.z1;
        if (ed.az < 0) z_hi = z_lo; else if (ed.az > 0) z_lo = z_hi;
        if (ed.ax < 0) x_hi = x_lo; else if (ed.ax > 0) x_lo = x_hi;
        for (int dz = z_lo; dz <= z_hi; ++dz) {
            for (int dx = x_lo; dx <= x_hi; ++dx) {
                int nx = ox + dx + ed.ax, nz = oz + dz + ed.az;
                if (!g_world->in_bounds(nx, nz)) continue;
                if (g_world->stack_height_at(nx, nz) == 0) continue;   // passagem: nao desenha parede
                // Face interna do tile de parede: meio tile pra fora do centro do tile de piso.
                float bx = (float)(ox + dx) + (float)ed.ax * 0.5f;
                float bz = (float)(oz + dz) + (float)ed.az * 0.5f;
                float tx0, tz0, tx1, tz1;
                if (ed.ax != 0) { tx0 = tx1 = bx; tz0 = bz - 0.5f; tz1 = bz + 0.5f; }
                else            { tz0 = tz1 = bz; tx0 = bx - 0.5f; tx1 = bx + 0.5f; }
                wall_quad(tx0, tz0, tx1, tz1, fy, top, pr, pg, pb, 1.0f, 0.62f, 1.00f);
                // Rodape escuro + 2 faixas horizontais. E' o que da leitura de ESCALA: sem
                // referencia horizontal, uma parede de 16 de altura parece uma parede de 3.
                wall_quad(tx0, tz0, tx1, tz1, fy, fy + 0.55f, 0.30f, 0.32f, 0.36f, 1.0f, 0.8f, 1.0f);
                float band1 = fy + d.ceiling * 0.42f;
                wall_quad(tx0, tz0, tx1, tz1, band1 - 0.10f, band1 + 0.10f,
                          0.52f, 0.55f, 0.60f, 1.0f, 1.0f, 1.0f);
                float band2 = fy + d.ceiling - 0.85f;
                wall_quad(tx0, tz0, tx1, tz1, band2 - 0.14f, band2 + 0.14f,
                          0.44f, 0.47f, 0.52f, 1.0f, 1.0f, 1.0f);
            }
        }
    }
}

// Teto: plano + VIGAS + tubulacao + bandeja de cabos. As estruturas ficam nos 2-3 de cima, deixando o
// resto de ar livre - a leitura pedida ("tubulacao/iluminacao/cabos ... ESPACO LIVRE ... personagem").
void draw_ceiling(int idx, int ox, int oz, float fy) {
    const InteriorDef& d = kInteriors[idx];
    const float top = fy + d.ceiling;
    float x0 = (float)(ox + d.x0) - 0.5f, x1 = (float)(ox + d.x1) + 0.5f;
    float z0 = (float)(oz + d.z0) - 0.5f, z1 = (float)(oz + d.z1) + 0.5f;

    // Teto em PEDACOS de ~5 tiles, nao um quad unico. lit()/fog() amostram no CENTRO do quad, entao
    // um teto de 25x25 inteiro recebia uma unica cor - com o ambiente escuro isso saia como um grande
    // poligono preto chapado (o bug do screenshot). Subdividido, ele pega a variacao das luminarias e
    // da neblina, e le como teto de verdade.
    const float kStep = 5.0f;
    for (float cz2 = z0; cz2 < z1 - 0.01f; cz2 += kStep) {
        for (float cx2 = x0; cx2 < x1 - 0.01f; cx2 += kStep) {
            float ex = std::min(cx2 + kStep, x1), ez = std::min(cz2 + kStep, z1);
            // Xadrez leve: painel de teto le como painel, nao como plano infinito.
            bool alt = ((int)((cx2 - x0) / kStep) + (int)((cz2 - z0) / kStep)) & 1;
            float t = alt ? 0.62f : 0.56f;
            quad_y(cx2, cz2, ex, ez, top, t, t + 0.01f, t + 0.05f, 1.0f);
        }
    }

    const bool along_x = (d.x1 - d.x0) > (d.z1 - d.z0);
    // Vigas transversais a cada 4 tiles, com 0.45 de altura - vistas de baixo elas dao ritmo e escala.
    if (along_x) {
        for (float x = (float)(ox + d.x0) + 2.0f; x < (float)(ox + d.x1) - 1.0f; x += 4.0f)
            box(x - 0.28f, top - 0.55f, z0, x + 0.28f, top, z1, 0.40f, 0.42f, 0.47f, 1.0f, true);
    } else {
        for (float z = (float)(oz + d.z0) + 2.0f; z < (float)(oz + d.z1) - 1.0f; z += 4.0f)
            box(x0, top - 0.55f, z - 0.28f, x1, top, z + 0.28f, 0.40f, 0.42f, 0.47f, 1.0f, true);
    }

    // Tubulacao: 2 dutos grossos + bandeja de cabos, correndo no eixo longo, ligeiramente abaixo das
    // vigas. Nas salas grandes ganham um 3o duto.
    float cxm = (x0 + x1) * 0.5f, czm = (z0 + z1) * 0.5f;
    const int ducts = d.is_room ? 3 : 2;
    for (int k = 0; k < ducts; ++k) {
        float off = ((float)k - (float)(ducts - 1) * 0.5f) * 1.5f;
        float dr = (k == 1) ? 0.30f : 0.46f, dg = (k == 1) ? 0.34f : 0.44f, db = (k == 1) ? 0.40f : 0.42f;
        float y = top - 1.05f - (k == 1 ? 0.25f : 0.0f);
        if (along_x) box(x0, y, czm + off - 0.22f, x1, y + 0.44f, czm + off + 0.22f, dr, dg, db, 1.0f, true);
        else         box(cxm + off - 0.22f, y, z0, cxm + off + 0.22f, y + 0.44f, z1, dr, dg, db, 1.0f, true);
    }
}

// Passarela suspensa: so' nas salas altas. E' o elemento que mais vende "instalacao industrial" -
// uma estrutura na metade da altura obriga o olho a ler dois niveis.
// Sem colisao de proposito: o piso dela e' geometria desenhada e o motor nao faz piso suspenso (uma
// pilha de bloco iria do chao ate lá, virando uma parede solida no meio da sala).
void draw_catwalk(int idx, int ox, int oz, float fy) {
    const InteriorDef& d = kInteriors[idx];
    if (!d.is_room || d.ceiling < 11.0f) return;
    float y = fy + d.ceiling * 0.52f;
    bool along_x = (d.x1 - d.x0) > (d.z1 - d.z0);
    float x0 = (float)(ox + d.x0) + 1.0f, x1 = (float)(ox + d.x1) - 1.0f;
    float z0 = (float)(oz + d.z0) + 1.0f, z1 = (float)(oz + d.z1) - 1.0f;
    float cxm = (x0 + x1) * 0.5f, czm = (z0 + z1) * 0.5f;

    if (along_x) {
        box(x0, y, czm - 0.9f, x1, y + 0.18f, czm + 0.9f, 0.46f, 0.47f, 0.50f, 1.0f, true);
        for (float g2 : {-0.9f, 0.9f}) {   // guarda-corpo
            box(x0, y + 0.18f, czm + g2 - 0.06f, x1, y + 1.05f, czm + g2 + 0.06f, 0.34f, 0.36f, 0.40f, 0.85f, true);
        }
        for (float x = x0 + 2.0f; x < x1; x += 4.0f)   // pendurais ate o teto
            box(x - 0.08f, y + 1.05f, czm - 0.08f, x + 0.08f, fy + d.ceiling, czm + 0.08f,
                0.30f, 0.31f, 0.34f, 1.0f, false);
    } else {
        box(cxm - 0.9f, y, z0, cxm + 0.9f, y + 0.18f, z1, 0.46f, 0.47f, 0.50f, 1.0f, true);
        for (float g2 : {-0.9f, 0.9f}) {
            box(cxm + g2 - 0.06f, y + 0.18f, z0, cxm + g2 + 0.06f, y + 1.05f, z1, 0.34f, 0.36f, 0.40f, 0.85f, true);
        }
        for (float z = z0 + 2.0f; z < z1; z += 4.0f)
            box(cxm - 0.08f, y + 1.05f, z - 0.08f, cxm + 0.08f, fy + d.ceiling, z + 0.08f,
                0.30f, 0.31f, 0.34f, 1.0f, false);
    }
}

// Marcacoes de piso: faixas de circulacao nos corredores, anel no centro do saguao.
void draw_floor_marks(int idx, int ox, int oz, float fy) {
    const InteriorDef& d = kInteriors[idx];
    const float my = fy + 0.030f;   // acima do kTopEps (0.01) do terreno - sem z-fighting
    if (idx == kHall) {
        for (float rr : {6.0f, 6.5f}) {
            const int seg = 40;
            for (int i = 0; i < seg; ++i) {
                float a0 = (float)i / (float)seg * 2.0f * kPi, a1 = (float)(i + 1) / (float)seg * 2.0f * kPi;
                float ax = (float)ox + std::cos(a0) * rr, az = (float)oz + std::sin(a0) * rr;
                float bx = (float)ox + std::cos(a1) * rr, bz = (float)oz + std::sin(a1) * rr;
                quad_y(std::min(ax, bx) - 0.07f, std::min(az, bz) - 0.07f,
                       std::max(ax, bx) + 0.07f, std::max(az, bz) + 0.07f, my,
                       0.24f, 0.52f, 0.62f, 0.9f);
            }
        }
        return;
    }
    if (d.is_room) return;
    // Corredor: 2 faixas amarelas paralelas ao eixo longo, delimitando a via de circulacao.
    bool along_x = (d.x1 - d.x0) > (d.z1 - d.z0);
    float cxm = (float)(ox + (d.x0 + d.x1) / 2), czm = (float)(oz + (d.z0 + d.z1) / 2);
    for (float s : {-1.0f, 1.0f}) {
        if (along_x)
            quad_y((float)(ox + d.x0) - 0.5f, czm + s * 2.2f - 0.14f,
                   (float)(ox + d.x1) + 0.5f, czm + s * 2.2f + 0.14f, my, 0.72f, 0.60f, 0.16f, 0.9f);
        else
            quad_y(cxm + s * 2.2f - 0.14f, (float)(oz + d.z0) - 0.5f,
                   cxm + s * 2.2f + 0.14f, (float)(oz + d.z1) + 0.5f, my, 0.72f, 0.60f, 0.16f, 0.9f);
    }
}


// Cor de identidade de cada ambiente. Usada na sinalizacao: a verga de cada portal ganha a cor do
// ambiente que ele leva, e uma faixa da mesma cor corre no piso apontando pra lá. Sem isso o
// complexo e' um labirinto branco - o jogador construiu a estufa e nao a encontrou.
void interior_color(int i, float& r, float& g, float& b) {
    switch (i) {
        case kGreen:   r = 0.28f; g = 0.82f; b = 0.34f; break;  // verde   - estufa
        case kLab:     r = 0.30f; g = 0.72f; b = 0.92f; break;  // ciano   - laboratorio
        case kCtrl:    r = 0.90f; g = 0.72f; b = 0.20f; break;  // ambar   - controle
        case kDorm:    r = 0.86f; g = 0.46f; b = 0.66f; break;  // rosa    - dormitorio
        case kShop:    r = 0.92f; g = 0.52f; b = 0.20f; break;  // laranja - oficina
        case kAirlock: r = 0.88f; g = 0.24f; b = 0.22f; break;  // vermelho- eclusa/saida
        case kHall:    r = 0.72f; g = 0.76f; b = 0.82f; break;  // cinza   - saguao
        default:       r = 0.55f; g = 0.62f; b = 0.70f; break;  // corredores
    }
}

// Qual ambiente contem este tile do distrito (-1 = nenhum).
int cell_at(int dx, int dz) {
    for (int i = 0; i < kInteriorCount; ++i) {
        const InteriorDef& c = kInteriors[i];
        if (dx >= c.x0 && dx <= c.x1 && dz >= c.z0 && dz <= c.z1) return i;
    }
    return -1;
}

// Atravessando uma passagem, qual e' o DESTINO final? Se der num corredor, segue o corredor ate um
// ambiente de verdade - e' isso que faz a cor no piso do saguao apontar pra ESTUFA, e nao pro
// "Corredor Norte" (que nao diz nada ao jogador).
int destination_through(int from, int dx, int dz, int ax, int az) {
    int cur = cell_at(dx + ax, dz + az);
    int guard = 0;
    while (cur >= 0 && !kInteriors[cur].is_room && guard++ < 6) {
        const InteriorDef& c = kInteriors[cur];
        int cmx = (c.x0 + c.x1) / 2, cmz = (c.z0 + c.z1) / 2;
        // Sai do corredor pelas BORDAS dele (nao pelo centro +/- meia-medida: com largura par a conta
        // caia dentro do proprio corredor e a busca nao andava), e tentando PRIMEIRO seguir reto na
        // direcao de entrada. Sem preferir o reto, a ordem dos eixos decidia o destino no sorteio - o
        // Corredor Norte apontaria pra Estufa (que fica num ramal lateral) em vez da Sala de Controle,
        // que e' o que esta de fato em frente.
        const int dirs[5][2] = { {ax, az}, {1,0}, {-1,0}, {0,1}, {0,-1} };
        int best = -1, best_ax = ax, best_az = az;
        for (int k = 0; k < 5; ++k) {
            int nx = dirs[k][0], nz = dirs[k][1];
            if (nx == 0 && nz == 0) continue;
            int px = (nx > 0) ? c.x1 + 1 : (nx < 0 ? c.x0 - 1 : cmx);
            int pz = (nz > 0) ? c.z1 + 1 : (nz < 0 ? c.z0 - 1 : cmz);
            int n = cell_at(px, pz);
            if (n < 0 || n == from || n == cur) continue;
            // O SAGUAO ganha de qualquer outro candidato: quando um corredor tem mais de uma saida
            // valida, uma instalacao de verdade sinaliza o hub, nao a primeira sala que a busca achou.
            // Sem isso a Oficina apontava pra Eclusa (medido) so' porque o eixo +z foi testado antes.
            if (best < 0 || n == kHall) { best = n; best_ax = nx; best_az = nz; }
            if (n == kHall) break;
            if (k == 0) break;   // o reto ja serve; so' segue procurando se ele falhou
        }
        ax = best_ax; az = best_az;
        if (best < 0) break;
        from = cur; cur = best;
    }
    return cur;
}

// Detalhe de ambiente: portais nas passagens (com sinalizacao por cor), dutos verticais nas quinas,
// consoles de parede e faixas de piso. Sao esses elementos que fazem um volume grande ler como
// INSTALACAO em vez de caixa grande - o pedido de "mais detalhes".
void draw_details(int idx, int ox, int oz, float fy) {
    const InteriorDef& d = kInteriors[idx];
    const float top = fy + d.ceiling;

    // ---- PORTAIS: moldura em cada PASSAGEM (trecho de borda sem parede) ----
    // Sem isso a ligacao entre dois ambientes e' um rasgo sem nada em volta, e a arquitetura perde a
    // leitura de "atravessei uma porta".
    struct Edge { int ax, az; };
    const Edge edges[4] = { {0,-1}, {0,1}, {-1,0}, {1,0} };
    for (int e = 0; e < 4; ++e) {
        const Edge& ed = edges[e];
        int x_lo = d.x0, x_hi = d.x1, z_lo = d.z0, z_hi = d.z1;
        if (ed.az < 0) z_hi = z_lo; else if (ed.az > 0) z_lo = z_hi;
        if (ed.ax < 0) x_hi = x_lo; else if (ed.ax > 0) x_lo = x_hi;
        const bool scan_x = (ed.ax == 0);
        int s_lo = scan_x ? x_lo : z_lo, s_hi = scan_x ? x_hi : z_hi;
        int run_start = -99999, prev = -99999;
        int run_start_saved = -99999, prev_saved = -99999;
        for (int s = s_lo; s <= s_hi + 1; ++s) {
            bool open = false;
            if (s <= s_hi) {
                int dx = scan_x ? s : x_lo, dz = scan_x ? z_lo : s;
                int nx = ox + dx + ed.ax, nz = oz + dz + ed.az;
                open = g_world->in_bounds(nx, nz) && g_world->stack_height_at(nx, nz) == 0;
            }
            if (open && run_start == -99999) run_start = s;
            if (!open && run_start != -99999) {
                // Passagem de run_start..prev. Moldura: 2 pilastras + verga, na linha da parede.
                float a = (float)run_start - 0.5f, b = (float)prev + 0.5f;
                float lint = fy + std::min(d.ceiling - 0.6f, 5.2f);   // verga alta: passagem ampla
                if (scan_x) {
                    float wz = (float)(oz + z_lo) + (float)ed.az * 0.5f;
                    float ax0 = (float)ox + a, ax1 = (float)ox + b;
                    box(ax0 - 0.30f, fy, wz - 0.22f, ax0 + 0.08f, lint + 0.45f, wz + 0.22f,
                        0.46f, 0.48f, 0.53f, 1.0f, false);
                    box(ax1 - 0.08f, fy, wz - 0.22f, ax1 + 0.30f, lint + 0.45f, wz + 0.22f,
                        0.46f, 0.48f, 0.53f, 1.0f, false);
                    box(ax0 - 0.30f, lint, wz - 0.22f, ax1 + 0.30f, lint + 0.45f, wz + 0.22f,
                        0.40f, 0.42f, 0.47f, 1.0f, true);
                    // Faixa de advertencia no piso da soleira.
                    for (float t = ax0; t < ax1 - 0.01f; t += 0.7f) {
                        bool y2 = ((int)((t - ax0) / 0.7f) & 1) == 0;
                        quad_y(t, wz - 0.35f, std::min(t + 0.7f, ax1), wz + 0.35f, fy + 0.055f,
                               y2 ? 0.74f : 0.14f, y2 ? 0.62f : 0.14f, y2 ? 0.18f : 0.16f, 0.9f);
                    }
                } else {
                    float wx = (float)(ox + x_lo) + (float)ed.ax * 0.5f;
                    float az0 = (float)oz + a, az1 = (float)oz + b;
                    box(wx - 0.22f, fy, az0 - 0.30f, wx + 0.22f, lint + 0.45f, az0 + 0.08f,
                        0.46f, 0.48f, 0.53f, 1.0f, false);
                    box(wx - 0.22f, fy, az1 - 0.08f, wx + 0.22f, lint + 0.45f, az1 + 0.30f,
                        0.46f, 0.48f, 0.53f, 1.0f, false);
                    box(wx - 0.22f, lint, az0 - 0.30f, wx + 0.22f, lint + 0.45f, az1 + 0.30f,
                        0.40f, 0.42f, 0.47f, 1.0f, true);
                    for (float t = az0; t < az1 - 0.01f; t += 0.7f) {
                        bool y2 = ((int)((t - az0) / 0.7f) & 1) == 0;
                        quad_y(wx - 0.35f, t, wx + 0.35f, std::min(t + 0.7f, az1), fy + 0.055f,
                               y2 ? 0.74f : 0.14f, y2 ? 0.62f : 0.14f, y2 ? 0.18f : 0.16f, 0.9f);
                    }
                }
                // ---- SINALIZACAO POR COR ----
                // Verga da cor do DESTINO + faixa no piso apontando pra lá. E' a resposta direta pra
                // "coloque o comodo da estufa": ela sempre existiu (110 tiles de canteiro, medido),
                // mas num complexo todo branco nao havia como achar. Agora a estufa e' verde e a cor
                // corre do saguao ate a porta dela.
                {
                    int mid = (run_start_saved + prev_saved) / 2;
                    int gdx = scan_x ? mid : x_lo, gdz = scan_x ? z_lo : mid;
                    int dest = destination_through(idx, gdx, gdz, ed.ax, ed.az);
                    if (dest >= 0 && dest != idx) {
                        float sr, sg, sb;
                        interior_color(dest, sr, sg, sb);
                        float lint2 = fy + std::min(d.ceiling - 0.6f, 5.2f);
                        float a2 = (float)run_start_saved - 0.5f, b2 = (float)prev_saved + 0.5f;
                        // Guia de piso: FAIXA ESTREITA (0.9 de largura, 3 tiles de comprimento) no
                        // eixo da passagem. Antes era uma laje de ~5x7 com alpha 0.55, e com 2-3
                        // portais por corredor as lajes se sobrepunham e cobriam o chao de manchas
                        // amarelas/verdes/rosas - o "cores estranhas no chao" do screenshot. Estreita
                        // e curta, ela le como seta de sinalizacao em vez de tinta derramada.
                        const float kGuideY = fy + 0.075f;   // acima das faixas de corredor (0.030) e
                                                             // da soleira (0.055): alturas separadas
                                                             // matam o z-fighting entre os decais
                        if (scan_x) {
                            float wz = (float)(oz + z_lo) + (float)ed.az * 0.5f;
                            float mx = (float)ox + (a2 + b2) * 0.5f;
                            // Faixa acesa na verga.
                            box((float)ox + a2, lint2 + 0.46f, wz - 0.26f,
                                (float)ox + b2, lint2 + 0.72f, wz + 0.26f, sr, sg, sb, 1.0f, true, true);
                            float dir = -(float)ed.az;
                            float gz0 = wz + dir * 0.2f, gz1 = wz + dir * 3.2f;
                            quad_y(mx - 0.45f, std::min(gz0, gz1), mx + 0.45f, std::max(gz0, gz1),
                                   kGuideY, sr, sg, sb, 0.85f);
                        } else {
                            float wx = (float)(ox + x_lo) + (float)ed.ax * 0.5f;
                            float mz = (float)oz + (a2 + b2) * 0.5f;
                            box(wx - 0.26f, lint2 + 0.46f, (float)oz + a2,
                                wx + 0.26f, lint2 + 0.72f, (float)oz + b2, sr, sg, sb, 1.0f, true, true);
                            float dir = -(float)ed.ax;
                            float gx0 = wx + dir * 0.2f, gx1 = wx + dir * 3.2f;
                            quad_y(std::min(gx0, gx1), mz - 0.45f, std::max(gx0, gx1), mz + 0.45f,
                                   kGuideY, sr, sg, sb, 0.85f);
                        }
                    }
                }
                run_start = -99999;
            }
            if (open && run_start != -99999) { run_start_saved = run_start; prev_saved = s; }
            prev = s;
        }
    }

    // ---- DUTOS VERTICAIS nas 4 quinas internas: piso -> teto ----
    // Amarram piso e teto visualmente. Sem eles, com 13-16 de pe-direito, o teto parece descolado.
    for (int sx = 0; sx < 2; ++sx)
        for (int sz = 0; sz < 2; ++sz) {
            float cx2 = (float)(ox + (sx ? d.x1 : d.x0)) + (sx ? -0.65f : 0.65f);
            float cz2 = (float)(oz + (sz ? d.z1 : d.z0)) + (sz ? -0.65f : 0.65f);
            box(cx2 - 0.20f, fy, cz2 - 0.20f, cx2 + 0.20f, top, cz2 + 0.20f, 0.40f, 0.42f, 0.47f, 1.0f, false);
            box(cx2 - 0.34f, fy, cz2 - 0.10f, cx2 - 0.20f, top, cz2 + 0.10f, 0.30f, 0.32f, 0.36f, 1.0f, false);
            // Braçadeiras a cada 2.5 - marcam a altura, dando escala ao duto.
            for (float y = fy + 1.2f; y < top - 0.4f; y += 2.5f)
                box(cx2 - 0.30f, y, cz2 - 0.30f, cx2 + 0.30f, y + 0.16f, cz2 + 0.30f,
                    0.54f, 0.56f, 0.60f, 1.0f, true);
        }

    if (!d.is_room) return;   // corredores param aqui: eles ja tem calha, dutos e faixas de piso

    // ---- CONSOLES DE PAREDE ----
    // SO' onde existe parede atras. Antes eu punha 3 consoles em posicoes fixas da borda (centro e
    // +/-5), e o CENTRO da borda e' justamente onde fica a passagem: o console ficava pendurado no
    // meio do vao, flutuando no corredor - o bug "decoracao da parede flutuando" do screenshot.
    // Agora percorre a parede e usa apenas os tiles cujo vizinho externo e' bloco de verdade.
    {
        bool along_x = (d.x1 - d.x0) > (d.z1 - d.z0);
        int placed = 0;
        int s_lo = along_x ? d.x0 : d.z0, s_hi = along_x ? d.x1 : d.z1;
        for (int s = s_lo + 2; s <= s_hi - 2 && placed < 4; s += 4) {
            int wx_in = along_x ? s : d.x0;                 // tile de piso rente a parede
            int wz_in = along_x ? d.z0 : s;
            int wx_out = along_x ? s : d.x0 - 1;            // tile de parede atras dele
            int wz_out = along_x ? d.z0 - 1 : s;
            if (g_world->stack_height_at(ox + wx_out, oz + wz_out) == 0) continue;  // e' passagem
            if (g_world->stack_height_at(ox + wx_in, oz + wz_in) > 0) continue;     // nao e' piso
            float px  = (float)(ox + wx_in) + (along_x ? 0.0f : 0.34f);
            float pz2 = (float)(oz + wz_in) + (along_x ? 0.34f : 0.0f);
            float sxw = along_x ? 2.4f : 0.34f, szw = along_x ? 0.34f : 2.4f;
            box_c(px, fy + 1.30f, pz2, sxw, 1.50f, szw, 0.26f, 0.28f, 0.32f);
            // Tira acesa: e' o unico jeito de por "informacao" numa parede sem texto 3D.
            box_c(px, fy + 2.05f, pz2, sxw * 0.82f, 0.22f, szw * 0.82f,
                  0.30f + 0.20f * (float)(placed % 2), 0.72f, 0.86f, 1.0f, false, true);
            placed++;
        }
    }
}

// Estufa: gantry de cultivo sobre cada fileira de canteiro - calha de luz magenta + tubo de irrigacao
// com bicos. Fica a 3.4 de altura (acima da cabeca de 1.80), entao nao atrapalha a passagem, e e' o
// que faz a sala ler como ESTUFA de verdade e nao como sala com terra no chao.
void draw_grow_gantries(int ox, int oz, float fy) {
    const InteriorDef& d = kInteriors[kGreen];
    const float y = fy + 3.40f;
    for (int dx = d.x0; dx <= d.x1; ++dx) {
        // Uma gantry por COLUNA de canteiro (varre em z pra achar se esta coluna tem canteiro).
        bool has = false;
        for (int dz = d.z0; dz <= d.z1 && !has; ++dz)
            if (g_world->in_bounds(ox + dx, oz + dz) &&
                g_world->get_ground(ox + dx, oz + dz) == Block::PlanterBed) has = true;
        if (!has) continue;
        if (((dx - d.x0) % 3) != 0) continue;   // 1 gantry a cada 3 colunas
        float px = (float)(ox + dx);
        float z0 = (float)(oz + d.z0) + 1.0f, z1 = (float)(oz + d.z1) - 1.0f;
        // Trilho estrutural.
        box(px - 0.16f, y, z0, px + 0.16f, y + 0.24f, z1, 0.44f, 0.46f, 0.50f, 1.0f, true);
        // Calha de cultivo (emissiva magenta) por baixo do trilho.
        box(px - 0.30f, y - 0.20f, z0, px + 0.30f, y - 0.04f, z1, 1.00f, 0.34f, 0.80f, 1.0f, true, true);
        // Tubo de irrigacao ao lado + bicos.
        box(px + 0.42f, y - 0.05f, z0, px + 0.62f, y + 0.15f, z1, 0.34f, 0.52f, 0.62f, 1.0f, true);
        for (float z = z0 + 1.0f; z < z1; z += 2.0f)
            box(px + 0.46f, y - 0.35f, z - 0.05f, px + 0.58f, y - 0.05f, z + 0.05f,
                0.30f, 0.46f, 0.56f, 1.0f, true);
        // Pendurais ate o teto.
        for (float z = z0 + 1.5f; z < z1; z += 5.0f)
            box(px - 0.07f, y + 0.24f, z - 0.07f, px + 0.07f, fy + d.ceiling - 1.3f, z + 0.07f,
                0.32f, 0.34f, 0.38f, 1.0f, false);
    }
}
} // namespace

void render_base_interior() {
    if (!g_world) return;

    Vec2 rp = get_player_render_pos();
    int ox, oz;
    interior_district_center(ox, oz);

    // Gate por DISTANCIA ao distrito, nao por "o jogador esta dentro de um ambiente".
    //
    // O piso do complexo sao 2787 tiles de TERRENO de verdade (medido), e o loop de terreno os
    // desenha sempre, de qualquer lugar. As paredes sao blocos INVISIVEIS. Logo, no instante em que
    // esta funcao nao rodava - por exemplo estando no anel de parede, ou num ponto que interior_at
    // classificava como fora - nao havia parede nem teto desenhados e a PLANTA BAIXA ficava exposta:
    // era o "bug de ver o interior pelo lado de fora". Com o gate por distancia, parede e teto
    // existem sempre que o complexo pode estar em vista, entao nao ha instante nenhum em que o piso
    // apareca sem eles. Custa zero no resto do mundo: o distrito fica a ~1200 tiles da base.
    // O gate tem que ser NO MINIMO o alcance do terreno, nunca menos: o piso e' terreno de verdade,
    // desenhado ate g_frame_terrain_horizon (= view_radius, dinamico de 110 a 380). O 150 FIXO que
    // estava aqui era menor que o horizonte quando se voa alto - o piso apareceria a 200 tiles sem
    // parede nem teto, ressuscitando exatamente o bug que este gate existe para impedir. A folga de
    // 50 e' porque a medida e' ate o CENTRO do distrito, mas o piso se estende ~45 tiles a partir
    // dele. (Este e' o inverso do corte de base_exterior.cpp, que tem que ser no MAXIMO o horizonte:
    // la o modelo nao pode existir onde nao ha terreno para ocluir; aqui ele nao pode FALTAR onde ha.)
    {
        float ddx = rp.x - (float)ox, ddz = rp.y - (float)oz;
        float gate = std::max(150.0f, g_frame_terrain_horizon + 50.0f);
        if (ddx * ddx + ddz * ddz > gate * gate) return;
    }

    rlSetTexture(rlGetTextureIdDefault());  // NAO rlSetTexture(0) - pra id 0 o rlgl nao troca nada

    const float fy = interior_floor_y();

    // Desenha o ambiente atual E os proximos: com tudo conectado, e' preciso ver o corredor a partir
    // do saguao. Corte generoso por distancia ao centro do retangulo.
    for (int i = 0; i < kInteriorCount; ++i) {
        const InteriorDef& d = kInteriors[i];
        float mx = (float)(ox + (d.x0 + d.x1) / 2), mz = (float)(oz + (d.z0 + d.z1) / 2);
        float ddx = rp.x - mx, ddz = rp.y - mz;
        float span = (float)std::max(d.x1 - d.x0, d.z1 - d.z0);
        if (ddx * ddx + ddz * ddz > (58.0f + span) * (58.0f + span)) continue;

        draw_walls(i, ox, oz, fy);
        draw_ceiling(i, ox, oz, fy);
        draw_catwalk(i, ox, oz, fy);
        draw_floor_marks(i, ox, oz, fy);
        draw_details(i, ox, oz, fy);
        if (i == kGreen) draw_grow_gantries(ox, oz, fy);

        // Escotilha externa (nos ambientes que tem porta): portao redondo GRANDE na parede, com faixa
        // de soleira. Escala revisada: 1.9 de raio de folha contra um jogador de 1.8 - le como portao
        // de eclusa, nao como janelinha.
        if (d.door_facing_deg > -900.0f) {
            float ex = (float)(ox + d.exit_x), ez = (float)(oz + d.exit_z);
            // Direcao da parede: do centro do ambiente pro tile de saida.
            float cxm = (float)(ox + (d.x0 + d.x1) / 2), czm = (float)(oz + (d.z0 + d.z1) / 2);
            float vx = ex - cxm, vz = ez - czm;
            float vl = std::max(0.001f, std::sqrt(vx * vx + vz * vz));
            vx /= vl; vz /= vl;
            // Snap pro eixo dominante (as paredes sao alinhadas aos eixos).
            if (std::fabs(vx) > std::fabs(vz)) { vx = (vx > 0 ? 1.0f : -1.0f); vz = 0.0f; }
            else                               { vz = (vz > 0 ? 1.0f : -1.0f); vx = 0.0f; }
            float wall_ang = std::atan2(vz, vx);
            float px2 = -vz, pz2 = vx;   // tangente da parede
            // PORTAO: a MESMA funcao compartilhada que o exterior usa (render_airlock_hatch_3d), com o
            // MESMO raio. Antes o interior tinha receita propria - e com as cores invertidas (folha
            // clara 0.60 sobre parede clara 0.68) a porta literalmente desaparecia na parede, sobrando
            // so' a placa de fundo azulada. Era o "a porta de saida nao parece com a porta de entrada".
            // A placa de fundo, batentes e verga que eu tinha posto aqui SAIRAM: o exterior nao tem
            // nada disso e le muito melhor - o painel de parede ja e' o fundo.
            // Centro em +2.05: o raio EXTERNO do conjunto e' 1.89 (chapa 1.55 + aro 0.34), entao com o
            // centro em 1.45 o terco de baixo do portao ficava ENTERRADO no piso e sobrava meia-lua -
            // nao lia como escotilha. 2.05 poe o disco inteiro acima do chao (0.16 de folga).
            render_airlock_hatch_3d({ex + vx * 1.42f, fy + 2.05f, ez + vz * 1.42f},
                                    wall_ang, 1.55f, 0.30f);
            // Soleira: apron amarelo continuo, igual ao do lado de fora (era xadrez amarelo/preto -
            // outra coisa que fazia as duas portas parecerem diferentes).
            {
                float ax = ex + vx * 0.10f, az = ez + vz * 0.10f;
                float sr = 0.72f, sg = 0.60f, sb = 0.18f;
                quad_y(std::min(ax + px2 * -1.6f, ax + px2 * 1.6f - vx * 2.4f),
                       std::min(az + pz2 * -1.6f, az + pz2 * 1.6f - vz * 2.4f),
                       std::max(ax + px2 * -1.6f, ax + px2 * 1.6f - vx * 2.4f),
                       std::max(az + pz2 * -1.6f, az + pz2 * 1.6f - vz * 2.4f),
                       fy + 0.058f, sr, sg, sb, 0.92f);
            }
        }
    }

    // ================= MOBILIA / MAQUINARIO =================
    for (int i = 0; i < kFurnitureCount; ++i) {
        const FurnPiece& f = kFurniture[i];
        float wx = (float)(ox + f.dx), wz = (float)(oz + f.dz);
        float ddx = rp.x - wx, ddz = rp.y - wz;
        if (ddx * ddx + ddz * ddz > 60.0f * 60.0f) continue;
        float sx = std::max(0.35f, (float)f.w_dx - 0.12f);
        float sz = std::max(0.35f, (float)f.w_dz - 0.12f);
        box_c(wx, fy + 0.02f, wz, sx, f.height, sz, f.r, f.g, f.b, 1.0f, false, f.emissive);

        if (f.collider == Block::FurnitureHuge) {
            // Maquinario: tampa clara, aro de inspecao e 2 dutos subindo pro teto. Sao os dutos que
            // amarram a maquina a arquitetura, em vez de ela parecer uma caixa solta na sala.
            box_c(wx, fy + f.height, wz, sx + 0.10f, 0.22f, sz + 0.10f, 0.62f, 0.64f, 0.68f);
            box_c(wx, fy + f.height * 0.55f, wz, sx + 0.08f, 0.16f, sz + 0.08f, 0.30f, 0.32f, 0.36f);
            const InteriorDef& d = kInteriors[f.interior];
            for (float s : {-0.6f, 0.6f}) {
                box(wx + s - 0.13f, fy + f.height + 0.22f, wz - 0.13f,
                    wx + s + 0.13f, fy + d.ceiling - 1.2f, wz + 0.13f, 0.34f, 0.36f, 0.40f, 1.0f, false);
            }
        } else if (f.collider == Block::FurnitureMid) {
            box_c(wx, fy + f.height, wz, sx + 0.06f, 0.06f, sz + 0.06f,
                  std::min(1.0f, f.r * 1.25f), std::min(1.0f, f.g * 1.25f), std::min(1.0f, f.b * 1.25f));
        } else if (f.collider == Block::FurnitureLow && f.w_dx >= 3) {
            box_c(wx, fy + f.height, wz, sx - 0.10f, 0.16f, sz - 0.10f, 0.72f, 0.70f, 0.64f);
            box_c(wx, fy + f.height + 0.16f, wz, sx * 0.55f, 0.07f, sz - 0.14f, 0.24f, 0.40f, 0.44f);
        } else if (f.collider == Block::FurnitureTall) {
            box_c(wx, fy + f.height * 0.5f, wz, 0.06f, f.height * 0.9f, 0.06f, 0.30f, 0.31f, 0.34f);
        }
    }

    // ================= LUMINARIAS =================
    for (int i = 0; i < kBaseLampCount; ++i) {
        const BaseLamp& lp = kBaseLamps[i];
        float lx = (float)ox + lp.dx, lz = (float)oz + lp.dz;
        float ddx = rp.x - lx, ddz = rp.y - lz;
        if (ddx * ddx + ddz * ddz > 70.0f * 70.0f) continue;
        // Pendural + calha: uma luminaria a 14 de altura precisa de haste, senao le como mancha.
        const InteriorDef& d = kInteriors[lp.interior];
        box(lx - 0.07f, fy + lp.y + 0.14f, lz - 0.07f, lx + 0.07f, fy + d.ceiling, lz + 0.07f,
            0.30f, 0.31f, 0.34f, 1.0f, false);
        box_c(lx, fy + lp.y, lz, 1.60f, 0.16f, 0.44f, 0.34f, 0.35f, 0.38f, 1.0f, true);
        quad_y(lx - 0.72f, lz - 0.18f, lx + 0.72f, lz + 0.18f, fy + lp.y - 0.01f,
               lp.r, lp.g, lp.b, 0.95f, true);
    }
    rlSetBlendMode(RL_BLEND_ADDITIVE);
    rlDisableDepthMask();
    for (int i = 0; i < kBaseLampCount; ++i) {
        const BaseLamp& lp = kBaseLamps[i];
        float lx = (float)ox + lp.dx, lz = (float)oz + lp.dz;
        float ddx = rp.x - lx, ddz = rp.y - lz;
        if (ddx * ddx + ddz * ddz > 70.0f * 70.0f) continue;
        // 1.9 -> 1.0 e alpha 0.24 -> 0.15: contra um interior escuro o halo antigo lia como uma bola
        // amarela flutuando (screenshot). Com o ambiente corrigido ele volta a ser um brilho de calha.
        render_glow_disc_3d({lx, fy + lp.y - 0.12f, lz}, 1.0f, lp.r, lp.g, lp.b, 0.15f, 12);
    }
    rlEnableDepthMask();
    rlSetBlendMode(RL_BLEND_ALPHA);

    // ================= ESTUFA: plantas + producao visivel =================
    {
        const InteriorDef& gh = kInteriors[kGreen];
        float gmx = (float)(ox + (gh.x0 + gh.x1) / 2), gmz = (float)(oz + (gh.z0 + gh.z1) / 2);
        float gdx = rp.x - gmx, gdz = rp.y - gmz;
        if (gdx * gdx + gdz * gdz < 55.0f * 55.0f) {
            float prod = clamp01(g_greenhouse_output * 0.5f);
            bool producing = g_greenhouse_output > 0.0f;
            int plant_idx = 0;
            for (int dz = gh.z0; dz <= gh.z1; ++dz)
                for (int dx = gh.x0; dx <= gh.x1; ++dx) {
                    int tx = ox + dx, tz = oz + dz;
                    if (!g_world->in_bounds(tx, tz)) continue;
                    if (g_world->get_ground(tx, tz) != Block::PlanterBed) continue;
                    float px = (float)tx, pz = (float)tz;
                    plant_idx++;
                    box_c(px, fy + 0.02f, pz, 0.92f, 0.20f, 0.92f, 0.30f, 0.22f, 0.14f);
                    if (((tx + tz) & 1) != 0) continue;

                    float hs = hash01(plant_idx, 7.3f), h2 = hash01(plant_idx, 19.1f);
                    int species = (int)(hs * 3.0f) % 3;
                    float height = (0.38f + hs * 0.52f) * (producing ? 1.0f : 0.55f);
                    float lean = (hs - 0.5f) * 0.20f;
                    float sat = producing ? 1.0f : 0.55f;
                    float sr = 0.22f * sat, sg2 = 0.42f * sat, sb = 0.20f * sat;
                    float lr = lerp(0.30f, 0.42f, h2) * sat;
                    float lg = lerp(0.62f, 0.78f, h2) * sat;
                    float lb = lerp(0.26f, 0.32f, h2) * sat;
                    if (!producing) { lr += 0.16f; lg += 0.12f; lb += 0.06f; }
                    box_c(px + lean, fy + 0.20f, pz, 0.055f, height, 0.055f, sr, sg2, sb);
                    for (int L = 0; L < 4; ++L) {
                        float la = (float)L * (kPi * 0.5f) + hs * 1.7f;
                        float lrad = 0.20f + h2 * 0.12f;
                        float ly = fy + 0.20f + height * (0.55f + 0.30f * hash01(plant_idx * 4 + L, 2.2f));
                        box_c(px + lean + std::cos(la) * lrad, ly, pz + std::sin(la) * lrad,
                              0.30f, 0.035f, 0.16f, lr, lg, lb);
                    }
                    if (producing && species == 2 && h2 > 0.45f)
                        render_sphere_3d(px + lean, fy + 0.20f + height * 0.72f, pz, 0.085f,
                                         0.82f, 0.30f, 0.22f, 1.0f, 4, 7);
                }

            if (producing) {
                rlSetBlendMode(RL_BLEND_ADDITIVE);
                rlDisableDepthMask();
                int bubbles = 18 + (int)(prod * 18.0f);
                for (int i = 0; i < bubbles; ++i) {
                    float bx = gmx + (hash01(i, 47.0f) * 2.0f - 1.0f) * (float)(gh.x1 - gh.x0) * 0.5f;
                    float bz = gmz + (hash01(i, 53.0f) * 2.0f - 1.0f) * (float)(gh.z1 - gh.z0) * 0.5f;
                    float t = std::fmod(g_day_time * 0.30f + hash01(i, 11.0f), 1.0f);
                    float by = fy + 0.6f + t * (gh.ceiling - 1.4f);
                    render_glow_disc_3d({bx, by, bz}, 0.12f + hash01(i, 67.0f) * 0.10f,
                                        0.62f, 0.92f, 1.00f, (1.0f - t) * 0.40f, 8);
                }
                rlEnableDepthMask();
                rlSetBlendMode(RL_BLEND_ALPHA);
            }

            // Painel de status na parede norte da estufa - escala revisada pro pe-direito de 14.
            {
                float px = gmx, pz = (float)(oz + gh.z0) - 0.42f, py = fy + 2.2f;
                box_c(px, py, pz, 5.20f, 2.60f, 0.24f, 0.22f, 0.24f, 0.28f);
                float bar_food = producing ? (0.25f + prod * 0.75f) : 0.06f;
                float bar_o2   = producing ? (0.20f + prod * 0.62f) : 0.04f;
                float f2 = pz + 0.16f;
                box(px - 2.20f, py + 1.45f, f2, px + 2.20f, py + 1.85f, f2, 0.10f, 0.12f, 0.12f, 0.95f, false, true);
                box(px - 2.20f, py + 0.80f, f2, px + 2.20f, py + 1.20f, f2, 0.10f, 0.12f, 0.12f, 0.95f, false, true);
                box(px - 2.16f, py + 1.49f, f2 + 0.02f, px - 2.16f + 4.32f * bar_food, py + 1.81f, f2 + 0.02f,
                    0.32f, 0.88f, 0.34f, 0.98f, false, true);
                box(px - 2.16f, py + 0.84f, f2 + 0.02f, px - 2.16f + 4.32f * bar_o2, py + 1.16f, f2 + 0.02f,
                    0.36f, 0.86f, 0.96f, 0.98f, false, true);
                float blink = 0.65f + 0.35f * std::sin(g_day_time * 2.6f);
                box(px + 1.85f, py + 2.05f, f2 + 0.02f, px + 2.20f, py + 2.40f, f2 + 0.02f,
                    producing ? 0.26f : 0.90f, producing ? 0.92f : 0.22f, producing ? 0.34f : 0.18f,
                    blink, false, true);
            }
        }
    }

    rlSetBlendMode(RL_BLEND_ALPHA);
    rlEnableDepthMask();
    rlSetTexture(rlGetTextureIdDefault());
}
