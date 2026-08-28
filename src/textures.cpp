#include "textures.h"

#include "raylib_platform.h"
#include "math_core.h"   // clamp01
#include "noise.h"       // lerp

#include <algorithm>
#include <cstdint>
#include <vector>

// Globais de estado de jogo ainda definidas em main.cpp (extracao de game_state.h e uma
// fase posterior do plano de refatoracao). Removido o "static" delas em main.cpp para dar
// linkage externo, ja que block_color() (abaixo) precisa le-las de outra unidade de traducao.
extern float g_oxygen;
extern float g_water_res;
extern float g_temperature;
extern float g_atmosphere;

// ============= Block Colors =============
void block_color(Block b, int y, int world_h, float& r, float& g, float& bl, float& a) {
    a = 1.0f;
    float life = clamp01((g_oxygen * 0.75f + g_water_res * 0.25f) / 100.0f);
    float temp_factor = clamp01((g_temperature + 60.0f) / 80.0f); // -60 to +20 mapped to 0-1

    switch (b) {
        case Block::Grass: {
            // CORES MAIS ESCURAS E CONTRASTANTES
            float br = 0.45f, bg = 0.35f, bb = 0.18f;  // Dead/brown
            float gr = 0.20f, gg = 0.55f, gb = 0.15f;  // Alive/green (mais escuro)
            r = lerp(br, gr, life);
            g = lerp(bg, gg, life);
            bl = lerp(bb, gb, life);
            break;
        }
        case Block::Dirt:  r = 0.55f; g = 0.35f; bl = 0.18f; break;  // Mais claro
        case Block::Stone: r = 0.35f; g = 0.38f; bl = 0.42f; break;  // Mais escuro
        case Block::Sand:  r = 0.95f; g = 0.80f; bl = 0.45f; break;  // Mais amarelo
        case Block::Water: {
            float w0r = 0.15f, w0g = 0.20f, w0b = 0.35f;  // Murky escuro
            float w1r = 0.08f, w1g = 0.30f, w1b = 0.70f;  // Clear blue saturado
            float clarity = clamp01(g_atmosphere / 70.0f);
            r = lerp(w0r, w1r, clarity);
            g = lerp(w0g, w1g, clarity);
            bl = lerp(w0b, w1b, clarity);
            a = 0.80f;
            break;
        }
        case Block::Lava: {
            r = 0.95f; g = 0.35f; bl = 0.04f;  // Laranja/vermelho incandescente
            a = 0.92f;
            break;
        }
        case Block::Ice: {
            // Ice color - mais azulado e brilhante
            r = 0.65f; g = 0.88f; bl = 1.0f;
            a = 0.90f - temp_factor * 0.2f;
            break;
        }
        case Block::Snow: r = 1.0f; g = 0.98f; bl = 1.0f; break;  // Branco puro
        case Block::Wood:  r = 0.50f; g = 0.32f; bl = 0.18f; break;  // Mais escuro
        case Block::Leaves: {
            float lr = 0.22f, lg = 0.30f, lb = 0.15f;  // Dead
            float gr = 0.12f, gg = 0.60f, gb = 0.15f;  // Alive (mais saturado)
            r = lerp(lr, gr, life);
            g = lerp(lg, gg, life);
            bl = lerp(lb, gb, life);
            a = 0.75f;
            break;
        }
        case Block::Coal:   r = 0.12f; g = 0.12f; bl = 0.14f; break;  // Mais escuro
        case Block::Iron:   r = 0.58f; g = 0.40f; bl = 0.34f; break;  // Rust escuro (hematita)
        case Block::Copper: r = 1.00f; g = 0.52f; bl = 0.16f; break;  // Laranja-cobre vivo
        case Block::Crystal: r = 0.70f; g = 0.25f; bl = 1.0f; break;  // Roxo brilhante
        case Block::Metal: r = 0.75f; g = 0.78f; bl = 0.82f; break;  // Metal mais claro
        // Antes era verde puro (0.30,0.75,0.18) - lia como grama de Terra num planeta que
        // ainda nao foi terraformado (o usuario reportou isso). Reaproveitado como liquen/
        // musgo alienigena bioluminescente (ciano-teal) pros bolsoes de flora do Passo 6 de
        // world.cpp nao parecerem grama precoce - Block::Grass continua reservado so pra
        // depois que terraform_step() converter Terra->Grama de verdade (oxigenio/agua).
        case Block::Organic: r = 0.12f; g = 0.58f; bl = 0.62f; break;  // Ciano-teal alienigena
        case Block::Components: r = 0.15f; g = 0.60f; bl = 0.15f; break;  // Circuit green vivo

        // Modules - CORES MAIS VIBRANTES
        case Block::SolarPanel:      r = 0.10f; g = 0.20f; bl = 0.50f; break;  // Azul escuro
        case Block::EnergyGenerator: r = 1.0f; g = 0.80f; bl = 0.15f; break;   // Amarelo vivo
        case Block::WaterExtractor:  r = 0.15f; g = 0.55f; bl = 0.85f; break;  // Azul vivo
        case Block::OxygenGenerator: r = 0.18f; g = 0.90f; bl = 0.30f; break;  // Verde vivo
        case Block::Greenhouse:      r = 0.25f; g = 0.85f; bl = 0.25f; break;  // Verde claro
        case Block::CO2Factory:      r = 0.80f; g = 0.40f; bl = 0.15f; break;  // Laranja industrial
        case Block::Habitat:         r = 0.92f; g = 0.92f; bl = 0.95f; break;  // Branco brilhante
        case Block::Workshop:        r = 0.60f; g = 0.40f; bl = 0.25f; break;  // Ferrugem
        case Block::TerraformerBeacon: r = 0.85f; g = 0.25f; bl = 0.95f; break; // Magenta
        // Base structures
        case Block::RocketHull:      r = 0.95f; g = 0.95f; bl = 0.98f; break;  // Branco puro
        case Block::RocketEngine:    r = 0.30f; g = 0.32f; bl = 0.35f; break;  // Metal escuro
        case Block::RocketWindow:    r = 0.15f; g = 0.35f; bl = 0.75f; a = 0.85f; break;  // Azul
        case Block::RocketNose:      r = 1.0f; g = 0.20f; bl = 0.10f; break;   // Vermelho vivo
        case Block::RocketFin:       r = 0.80f; g = 0.82f; bl = 0.85f; break;  // Prata
        case Block::RocketDoor:      r = 0.45f; g = 0.47f; bl = 0.50f; break;  // Cinza
        case Block::DomeGlass:       r = 0.65f; g = 0.85f; bl = 1.0f; a = 0.45f; break;  // Transparente azul
        case Block::DomeFrame:       r = 0.55f; g = 0.58f; bl = 0.62f; break;  // Metal
        case Block::LandingPad:      r = 0.35f; g = 0.37f; bl = 0.40f; break;  // Concreto escuro
        case Block::BuildSlot:       r = 0.20f; g = 0.40f; bl = 0.55f; a = 0.65f; break;  // Slot azul
        case Block::PipeH:           r = 0.50f; g = 0.55f; bl = 0.60f; break;  // Metal
        case Block::PipeV:           r = 0.50f; g = 0.55f; bl = 0.60f; break;  // Metal
        case Block::Antenna:         r = 0.75f; g = 0.77f; bl = 0.80f; break;  // Metal claro
        case Block::RefinedAlloy:    r = 0.85f; g = 0.65f; bl = 0.15f; break;  // Liga dourada - distingue do Metal cru
        case Block::LaserPistol:     r = 0.20f; g = 0.85f; bl = 0.95f; break;  // Ciano laser - distingue de tudo mais
        case Block::BaseFloor:       r = 0.58f; g = 0.60f; bl = 0.64f; break;  // Chapa metalica clara
        case Block::PlanterBed:      r = 0.30f; g = 0.22f; bl = 0.14f; break;  // Terra revirada escura
        // Blocos de colisao de mobilia: nunca desenhados (ver os skips em main.cpp). Estes casos
        // sao rede de seguranca, so' pra nao cair no magenta do default se algum caminho de render
        // for esquecido no futuro.
        case Block::FurnitureLow:
        case Block::FurnitureMid:
        case Block::FurnitureTall:   r = 0.50f; g = 0.52f; bl = 0.56f; break;
        case Block::FurnitureHuge:   r = 0.44f; g = 0.46f; bl = 0.50f; break;
        case Block::Basalt:          r = 0.20f; g = 0.19f; bl = 0.21f; break;  // basalto: quase preto
        case Block::BaseShell:       r = 0.90f; g = 0.90f; bl = 0.93f; break;  // rede de seguranca: nunca desenhado (is_invisible_collider)
        default: r = 1.0f; g = 0.0f; bl = 1.0f; break;
    }

    // REMOVIDO: sombreamento por profundidade Y que escurecia tudo
    // O mundo agora tem cores consistentes sem escurecimento artificial
}

// ============= TEXTURAS (ESTILO MINICRAFT / PIXEL ART) =============
// Sem assets externos: atlas gerado proceduralmente em tempo de execucao.
unsigned int g_tex_atlas = 0;
static constexpr int kAtlasTileSize = 16;
static constexpr int kAtlasTilesPerRow = 16;
// kAtlasSizePx (256 = kAtlasTileSize * kAtlasTilesPerRow) is now the single copy exposed via
// textures.h (render_primitives.cpp needs it too); this file just uses that same constant.
static_assert(kAtlasSizePx == kAtlasTileSize * kAtlasTilesPerRow, "kAtlasSizePx mismatch");

struct Color8 {
    uint8_t r, g, b, a;
};

static Color8 c8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return {r, g, b, a}; }

static uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static uint32_t noise2_u32(int x, int y, uint32_t seed) {
    uint32_t h = seed;
    h ^= (uint32_t)x * 374761393u;
    h ^= (uint32_t)y * 668265263u;
    return hash_u32(h);
}

static uint8_t clamp_u8(int v) { return (uint8_t)std::clamp(v, 0, 255); }

UvRect atlas_uv(Tile t) {
    int id = (int)t;
    int tx = id % kAtlasTilesPerRow;
    int ty = id / kAtlasTilesPerRow;

    // Half-texel inset para evitar bleeding entre tiles.
    float inset = 0.5f;
    float u0 = (tx * kAtlasTileSize + inset) / (float)kAtlasSizePx;
    float v0 = (ty * kAtlasTileSize + inset) / (float)kAtlasSizePx;
    float u1 = (tx * kAtlasTileSize + (kAtlasTileSize - inset)) / (float)kAtlasSizePx;
    float v1 = (ty * kAtlasTileSize + (kAtlasTileSize - inset)) / (float)kAtlasSizePx;
    return {u0, v0, u1, v1};
}

static void atlas_set_px(std::vector<uint8_t>& atlas, int x, int y, Color8 c) {
    if (x < 0 || y < 0 || x >= kAtlasSizePx || y >= kAtlasSizePx) return;
    size_t idx = (size_t)(y * kAtlasSizePx + x) * 4u;
    atlas[idx + 0] = c.r;
    atlas[idx + 1] = c.g;
    atlas[idx + 2] = c.b;
    atlas[idx + 3] = c.a;
}

static void tile_set_px(std::vector<uint8_t>& atlas, Tile t, int x, int y_top, Color8 c) {
    int id = (int)t;
    int tx = id % kAtlasTilesPerRow;
    int ty = id / kAtlasTilesPerRow;

    // Converter y_top (0 = topo) -> y_bottom (0 = base) e escrever no atlas (OpenGL: origem embaixo).
    int y_bottom = (kAtlasTileSize - 1) - y_top;
    int gx = tx * kAtlasTileSize + x;
    int gy = ty * kAtlasTileSize + y_bottom;
    atlas_set_px(atlas, gx, gy, c);
}

static void tile_fill(std::vector<uint8_t>& atlas, Tile t, Color8 c) {
    for (int y = 0; y < kAtlasTileSize; ++y)
        for (int x = 0; x < kAtlasTileSize; ++x)
            tile_set_px(atlas, t, x, y, c);
}

static void tile_noise(std::vector<uint8_t>& atlas, Tile t, Color8 base, int amp, uint32_t seed) {
    for (int y = 0; y < kAtlasTileSize; ++y) {
        for (int x = 0; x < kAtlasTileSize; ++x) {
            uint32_t n = noise2_u32(x, y, seed);
            int d = (int)(n & 255u) % (amp * 2 + 1) - amp;
            tile_set_px(atlas, t, x, y, c8(
                clamp_u8((int)base.r + d),
                clamp_u8((int)base.g + d),
                clamp_u8((int)base.b + d),
                base.a));
        }
    }
}

// Ruido em 2 camadas (blotches grandes + granulado fino) - da uma textura mais organica/
// "com grumos" (terra/pedra/areia de verdade tem manchas maiores, nao so' um chiado fino
// uniforme) sem precisar desenhar formas a mao. Usado nos 3 tiles de chao mais vistos
// (Dirt/Stone/Sand) - pedido do jogador: "melhore... textura dos elementos".
static void tile_noise_2layer(std::vector<uint8_t>& atlas, Tile t, Color8 base, int fine_amp, int coarse_amp, uint32_t seed) {
    for (int y = 0; y < kAtlasTileSize; ++y) {
        for (int x = 0; x < kAtlasTileSize; ++x) {
            uint32_t nf = noise2_u32(x, y, seed);
            int df = (int)(nf & 255u) % (fine_amp * 2 + 1) - fine_amp;
            uint32_t nc = noise2_u32(x / 3, y / 3, seed + 999u);
            int dc = (int)(nc & 255u) % (coarse_amp * 2 + 1) - coarse_amp;
            int d = df + dc;
            tile_set_px(atlas, t, x, y, c8(
                clamp_u8((int)base.r + d),
                clamp_u8((int)base.g + d),
                clamp_u8((int)base.b + d),
                base.a));
        }
    }
}

static void tile_add_specks(std::vector<uint8_t>& atlas, Tile t, Color8 speck, int count, uint32_t seed) {
    for (int i = 0; i < count; ++i) {
        uint32_t h = noise2_u32(i, i * 7, seed);
        int x = (int)(h % (uint32_t)kAtlasTileSize);
        int y = (int)((h >> 8) % (uint32_t)kAtlasTileSize);
        tile_set_px(atlas, t, x, y, speck);
    }
}

static void tile_draw_rect(std::vector<uint8_t>& atlas, Tile t, int x0, int y0, int w, int h, Color8 c) {
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x)
            if (x >= 0 && y >= 0 && x < kAtlasTileSize && y < kAtlasTileSize)
                tile_set_px(atlas, t, x, y, c);
}

static void tile_generate_all(std::vector<uint8_t>& atlas) {
    atlas.assign((size_t)kAtlasSizePx * (size_t)kAtlasSizePx * 4u, 0);

    // Missing: checker magenta/black
    for (int y = 0; y < kAtlasTileSize; ++y) {
        for (int x = 0; x < kAtlasTileSize; ++x) {
            bool on = ((x / 4) ^ (y / 4)) & 1;
            tile_set_px(atlas, Tile::Missing, x, y, on ? c8(255, 0, 255) : c8(0, 0, 0));
        }
    }

    // Grama/folhas/agua: textura "valor" (quase cinza) + tint dinamico via block_color().
    tile_noise(atlas, Tile::GrassTop, c8(225, 225, 225), 18, 0x11u);
    tile_noise(atlas, Tile::GrassSide, c8(220, 220, 220), 18, 0x12u);
    // Faixa superior de "grama" no side (mais clara)
    tile_draw_rect(atlas, Tile::GrassSide, 0, 0, kAtlasTileSize, 5, c8(245, 245, 245));

    tile_noise(atlas, Tile::Leaves, c8(220, 220, 220, 210), 22, 0x13u);
    // Alguns pixels transparentes para folhas
    for (int y = 0; y < kAtlasTileSize; ++y) {
        for (int x = 0; x < kAtlasTileSize; ++x) {
            uint32_t n = noise2_u32(x, y, 0xBEEF1234u);
            if ((n % 23u) == 0u) tile_set_px(atlas, Tile::Leaves, x, y, c8(0, 0, 0, 0));
        }
    }

    // Terra, pedra, areia - ruido em 2 camadas (blotches + granulado fino, ver
    // tile_noise_2layer) em vez de um chiado uniforme so' - le como grumos/veios de
    // verdade, nao um "TV sem sinal".
    tile_noise_2layer(atlas, Tile::Dirt, c8(132, 88, 48), 16, 20, 0x20u);
    tile_noise_2layer(atlas, Tile::Stone, c8(110, 114, 120), 14, 16, 0x21u);
    tile_noise_2layer(atlas, Tile::Sand, c8(222, 194, 104), 12, 10, 0x22u);

    // Agua (4 frames)
    for (int f = 0; f < 4; ++f) {
        Tile tf = (Tile)((int)Tile::Water0 + f);
        tile_noise(atlas, tf, c8(235, 235, 235, 210), 12, 0x30u + (uint32_t)f);
        for (int y = 0; y < kAtlasTileSize; ++y) {
            for (int x = 0; x < kAtlasTileSize; ++x) {
                // Ondas simples (linhas diagonais)
                int v = (x + y + f * 2) & 7;
                if (v == 0) tile_set_px(atlas, tf, x, y, c8(255, 255, 255, 235));
                if (v == 1) tile_set_px(atlas, tf, x, y, c8(205, 205, 205, 210));
            }
        }
    }

    // Lava (4 frames) - crosta escura rachada com veios incandescentes.
    // O tile e' desenhado em tons NEUTROS (cinza) porque block_color(Lava) multiplica por um
    // tint laranja depois (uses_tint), igual todo o resto do atlas: valor BAIXO = crosta
    // escura/fria, valor ALTO = veio brilhante/derretido. A rachadura anda de frame em frame,
    // dando a impressao de crosta se movendo sobre a lava.
    for (int f = 0; f < 4; ++f) {
        Tile tf = (Tile)((int)Tile::Lava0 + f);
        // Base bem escura = crosta solidificada (contraste alto com os veios abaixo, e' isso
        // que faz ler como "algo derretido por dentro" em vez de tinta laranja lisa).
        tile_noise(atlas, tf, c8(66, 66, 66), 16, 0x90u + (uint32_t)f);
        for (int y = 0; y < kAtlasTileSize; ++y) {
            for (int x = 0; x < kAtlasTileSize; ++x) {
                // Duas familias de rachaduras cruzadas, deslocadas por frame - onde elas
                // passam, a "lava por baixo" aparece (valor alto).
                int a1 = (x + y * 2 + f * 3) % 11;
                int a2 = (x * 2 - y + f * 2 + 32) % 13;
                uint32_t n = noise2_u32(x, y, 0x90u + (uint32_t)f);
                if (a1 == 0 || a2 == 0) {
                    // Nucleo do veio: quase branco (vira amarelo-branco depois do tint) -
                    // e' o pixel que da a sensacao de calor.
                    tile_set_px(atlas, tf, x, y, c8(255, 250, 240));
                } else if (a1 == 1 || a2 == 1) {
                    // Beirada do veio, um degrau abaixo (transicao crosta -> derretido).
                    tile_set_px(atlas, tf, x, y, c8(190, 170, 150));
                } else if ((n % 17u) == 0u) {
                    // Brasas isoladas na crosta.
                    tile_set_px(atlas, tf, x, y, c8(150, 120, 100));
                } else if ((n % 7u) == 0u) {
                    // Manchas ainda mais frias/escuras - quebra a uniformidade da crosta.
                    tile_set_px(atlas, tf, x, y, c8(42, 42, 44));
                }
            }
        }
    }

    // Gelo / neve (tendem a ficar neutros, com leve detalhe)
    tile_noise(atlas, Tile::Ice, c8(210, 238, 255, 235), 10, 0x40u);
    for (int y = 0; y < kAtlasTileSize; ++y) {
        for (int x = 0; x < kAtlasTileSize; ++x) {
            uint32_t n = noise2_u32(x, y, 0x40u);
            if ((n % 19u) == 0u) tile_set_px(atlas, Tile::Ice, x, y, c8(255, 255, 255, 240));
        }
    }
    tile_noise(atlas, Tile::Snow, c8(245, 248, 255), 8, 0x41u);

    // Madeira (top com "aneis", side com listras)
    tile_fill(atlas, Tile::WoodSide, c8(128, 84, 48));
    for (int x = 0; x < kAtlasTileSize; ++x) {
        int stripe = (x + (x / 3)) & 3;
        uint8_t add = (stripe == 0) ? 20 : (stripe == 1 ? 8 : 0);
        for (int y = 0; y < kAtlasTileSize; ++y) {
            Color8 c = c8(clamp_u8(128 + add), clamp_u8(84 + add), clamp_u8(48 + add));
            tile_set_px(atlas, Tile::WoodSide, x, y, c);
        }
    }
    tile_fill(atlas, Tile::WoodTop, c8(140, 92, 52));
    for (int y = 0; y < kAtlasTileSize; ++y) {
        for (int x = 0; x < kAtlasTileSize; ++x) {
            float dx = (x + 0.5f) - kAtlasTileSize * 0.5f;
            float dy = (y + 0.5f) - kAtlasTileSize * 0.5f;
            float d = std::sqrt(dx * dx + dy * dy);
            int ring = ((int)std::floor(d)) & 3;
            uint8_t add = (ring == 0) ? 18 : (ring == 1 ? 10 : 0);
            tile_set_px(atlas, Tile::WoodTop, x, y, c8(clamp_u8(140 + add), clamp_u8(92 + add), clamp_u8(52 + add)));
        }
    }

    // Minerios: pedra + specks
    tile_noise(atlas, Tile::CoalOre, c8(110, 114, 120), 20, 0x50u);
    tile_add_specks(atlas, Tile::CoalOre, c8(18, 18, 20), 38, 0x501u);

    // FERRO x COBRE: as duas artes eram a MESMA rocha cinza com pintas laranja quase iguais -
    // ferro (202,128,70) e cobre (235,135,55). Diferenca de ~33 no vermelho e 7 no verde: no tamanho
    // de um bloco, com luz e neblina, sao indistinguiveis. Com o ferro agora 13x mais comum, tudo que
    // era laranja passou a ler como ferro e o jogador reportou "o cobre agora e' ferro".
    //
    // Ferro = hematita: pintas rust ESCURAS e desaturadas sobre rocha cinza.
    tile_noise(atlas, Tile::IronOre, c8(108, 112, 118), 20, 0x51u);
    tile_add_specks(atlas, Tile::IronOre, c8(148, 90, 72), 34, 0x511u);

    // Cobre = cobre nativo + PATINA. As duas passadas sao o que torna o cobre inconfundivel: o
    // laranja-cobre vivo por si so' ainda brigaria com o rust do ferro, mas verde-malaquita nao
    // aparece em nenhum outro minerio do jogo, entao o olho separa na hora.
    tile_noise(atlas, Tile::CopperOre, c8(112, 116, 122), 20, 0x52u);
    tile_add_specks(atlas, Tile::CopperOre, c8(246, 132, 44), 30, 0x521u);
    tile_add_specks(atlas, Tile::CopperOre, c8(52, 168, 118), 18, 0x522u);

    tile_noise(atlas, Tile::CrystalOre, c8(110, 114, 120), 20, 0x53u);
    tile_add_specks(atlas, Tile::CrystalOre, c8(200, 80, 255), 26, 0x531u);

    // MINERIO DE METAL BRUTO: matriz de rocha ESCURA (mais escura que a dos outros minerios, pra
    // ler como rocha rica em metal) com veios/laminas metalicas irregulares por cima, em 2 tons.
    // Antes Block::Metal reusava Tile::Metal - a chapa clara lisa (200,205,212, ruido 10) do piso da
    // base - e no chao aquilo parecia placa industrial polida em vez de minerio.
    tile_noise_2layer(atlas, Tile::MetalOre, c8(82, 84, 90), 16, 20, 0x63u);
    // Veios: tracos curtos em direcoes alternadas (nao specks redondos como os outros minerios -
    // metal nativo aparece em laminas/filetes, e e' isso que da a leitura de "bruto").
    {
        uint32_t s = 0x631u;
        auto rnd = [&s]() { s = s * 1664525u + 1013904223u; return (s >> 16) & 0x7fffu; };
        for (int v = 0; v < 22; ++v) {
            int vx = (int)(rnd() % (uint32_t)kAtlasTileSize);
            int vy = (int)(rnd() % (uint32_t)kAtlasTileSize);
            int len = 2 + (int)(rnd() % 3u);
            bool horiz = (rnd() & 1u) != 0;
            bool bright = (rnd() & 1u) != 0;
            Color8 c = bright ? c8(196, 202, 210) : c8(140, 146, 155);
            for (int k = 0; k < len; ++k) {
                int px = horiz ? vx + k : vx;
                int py = horiz ? vy : vy + k;
                if (px < 0 || px >= kAtlasTileSize || py < 0 || py >= kAtlasTileSize) continue;
                tile_set_px(atlas, Tile::MetalOre, px, py, c);
            }
        }
    }
    tile_noise(atlas, Tile::Metal, c8(200, 205, 212), 10, 0x60u);
    tile_noise(atlas, Tile::Organic, c8(90, 200, 80), 26, 0x61u);
    tile_noise(atlas, Tile::Components, c8(40, 130, 55), 20, 0x62u);
    // Trilhas de circuito
    for (int y = 2; y < kAtlasTileSize; y += 4) {
        tile_draw_rect(atlas, Tile::Components, 1, y, kAtlasTileSize - 2, 1, c8(15, 75, 20));
    }
    for (int x = 2; x < kAtlasTileSize; x += 5) {
        tile_draw_rect(atlas, Tile::Components, x, 1, 1, kAtlasTileSize - 2, c8(15, 75, 20));
    }

    // Modulos: padroes simples (icones pixel)
    tile_noise(atlas, Tile::SolarPanel, c8(25, 45, 110), 12, 0x70u);
    tile_draw_rect(atlas, Tile::SolarPanel, 2, 3, 12, 2, c8(180, 190, 215));
    tile_draw_rect(atlas, Tile::SolarPanel, 2, 7, 12, 2, c8(180, 190, 215));
    tile_draw_rect(atlas, Tile::SolarPanel, 2, 11, 12, 2, c8(180, 190, 215));

    tile_noise(atlas, Tile::EnergyGenerator, c8(240, 205, 60), 18, 0x71u);
    tile_draw_rect(atlas, Tile::EnergyGenerator, 6, 3, 4, 10, c8(40, 40, 40));

    tile_noise(atlas, Tile::WaterExtractor, c8(40, 150, 220), 18, 0x72u);
    tile_draw_rect(atlas, Tile::WaterExtractor, 3, 4, 10, 8, c8(15, 50, 120));

    tile_noise(atlas, Tile::OxygenGenerator, c8(60, 220, 100), 18, 0x73u);
    tile_draw_rect(atlas, Tile::OxygenGenerator, 4, 4, 8, 8, c8(15, 80, 35));

    tile_noise(atlas, Tile::Greenhouse, c8(70, 220, 70), 18, 0x74u);
    tile_draw_rect(atlas, Tile::Greenhouse, 2, 4, 12, 8, c8(200, 240, 255, 220));

    tile_noise(atlas, Tile::CO2Factory, c8(200, 110, 45), 18, 0x75u);
    tile_draw_rect(atlas, Tile::CO2Factory, 5, 2, 6, 12, c8(55, 55, 60));

    tile_noise(atlas, Tile::Habitat, c8(235, 235, 242), 10, 0x76u);
    tile_draw_rect(atlas, Tile::Habitat, 3, 5, 10, 6, c8(35, 80, 180, 220));

    tile_noise(atlas, Tile::Workshop, c8(160, 110, 70), 18, 0x77u);
    tile_draw_rect(atlas, Tile::Workshop, 3, 3, 10, 10, c8(60, 45, 30));

    tile_noise(atlas, Tile::Terraformer, c8(200, 80, 230), 18, 0x78u);
    tile_draw_rect(atlas, Tile::Terraformer, 7, 2, 2, 12, c8(255, 255, 255, 230));

    // Estruturas base (bem simples)
    tile_noise(atlas, Tile::RocketHull, c8(235, 235, 242), 8, 0x80u);
    tile_noise(atlas, Tile::RocketEngine, c8(70, 75, 85), 12, 0x81u);
    tile_noise(atlas, Tile::RocketWindow, c8(120, 170, 255, 210), 8, 0x82u);
    tile_noise(atlas, Tile::RocketNose, c8(255, 70, 55), 10, 0x83u);
    tile_noise(atlas, Tile::RocketFin, c8(210, 215, 222), 10, 0x84u);
    tile_noise(atlas, Tile::RocketDoor, c8(120, 124, 130), 10, 0x85u);
    tile_noise(atlas, Tile::DomeGlass, c8(160, 210, 255, 150), 8, 0x86u);
    tile_noise(atlas, Tile::DomeFrame, c8(150, 155, 165), 10, 0x87u);
    tile_noise(atlas, Tile::LandingPad, c8(85, 88, 95), 10, 0x88u);
    tile_noise(atlas, Tile::BuildSlot, c8(60, 130, 170, 200), 10, 0x89u);
    tile_noise(atlas, Tile::Pipe, c8(155, 165, 175), 8, 0x8Au);
    tile_noise(atlas, Tile::Antenna, c8(205, 210, 220), 8, 0x8Bu);

    // Piso INTERNO da base: chapa metalica CLARA com junta em cruz, escovado diagonal e rebites
    // nos cantos. Deliberadamente o oposto do LandingPad (concreto escuro) - o pedido do jogador
    // foi "o piso tem que ser diferente o la de fora", entao o contraste de VALOR (claro dentro /
    // escuro fora) tem que ler na hora, nao so' a textura.
    tile_noise(atlas, Tile::BaseFloor, c8(150, 152, 158), 8, 0x8Cu);
    tile_draw_rect(atlas, Tile::BaseFloor, 0, 0, kAtlasTileSize, 1, c8(96, 99, 105));
    tile_draw_rect(atlas, Tile::BaseFloor, 0, 0, 1, kAtlasTileSize, c8(96, 99, 105));
    tile_draw_rect(atlas, Tile::BaseFloor, 0, 7, kAtlasTileSize, 1, c8(114, 117, 124));
    tile_draw_rect(atlas, Tile::BaseFloor, 7, 0, 1, kAtlasTileSize, c8(114, 117, 124));
    for (int y = 0; y < kAtlasTileSize; ++y) {
        for (int x = 0; x < kAtlasTileSize; ++x) {
            if (((x + y) & 7) == 0) tile_set_px(atlas, Tile::BaseFloor, x, y, c8(168, 171, 178));
        }
    }
    {
        const int rivets[4][2] = {{2, 2}, {13, 2}, {2, 13}, {13, 13}};
        for (int i = 0; i < 4; ++i) {
            tile_set_px(atlas, Tile::BaseFloor, rivets[i][0], rivets[i][1], c8(196, 199, 206));
            tile_set_px(atlas, Tile::BaseFloor, rivets[i][0], rivets[i][1] + 1, c8(108, 111, 118));
        }
    }

    // Canteiro da estufa: terra revirada escura em SULCOS + brotos. O tom teal dos brotos segue a
    // paleta de Block::Organic de proposito - verde-Terra saturado num planeta ainda nao
    // terraformado ja foi reclamado antes nesta sessao.
    tile_noise_2layer(atlas, Tile::PlanterBed, c8(76, 54, 36), 12, 16, 0x8Du);
    for (int y = 1; y < kAtlasTileSize; y += 4) {
        tile_draw_rect(atlas, Tile::PlanterBed, 0, y, kAtlasTileSize, 1, c8(52, 36, 24));
    }
    for (int i = 0; i < 24; ++i) {
        uint32_t hn = noise2_u32(i * 3, i * 5, 0x8D1u);
        int sx = (int)(hn % (uint32_t)kAtlasTileSize);
        int sy = 2 + (int)((hn >> 8) % (uint32_t)(kAtlasTileSize - 4));
        tile_set_px(atlas, Tile::PlanterBed, sx, sy, c8(42, 168, 172));
        tile_set_px(atlas, Tile::PlanterBed, sx, sy - 1, c8(74, 212, 202));
    }

    // Cracks: linhas pretas sobre alpha
    for (int i = 0; i < 8; ++i) {
        Tile t = (Tile)((int)Tile::Crack1 + i);
        tile_fill(atlas, t, c8(0, 0, 0, 0));
        uint8_t a = (uint8_t)(40 + i * 22);
        // desenho simples: alguns riscos diagonais
        for (int y = 1; y < kAtlasTileSize - 1; ++y) {
            int x = (y + i * 2) % (kAtlasTileSize - 2) + 1;
            tile_set_px(atlas, t, x, y, c8(0, 0, 0, a));
            if ((y & 3) == 0) tile_set_px(atlas, t, std::max(1, x - 1), y, c8(0, 0, 0, a));
        }
        for (int x = 2; x < kAtlasTileSize - 2; x += 5) {
            tile_set_px(atlas, t, x, (x + i) % (kAtlasTileSize - 2) + 1, c8(0, 0, 0, a));
        }
    }
    // ============= ARTE DE ITEM (icone de inventario) =============
    // Ver o comentario do bloco ItemIron..ItemPistol em textures.h pro motivo. Resumo: os 5
    // minerios compartilham a mesma matriz de rocha cinza (correto no mundo - minerio esta dentro
    // da pedra), e como icone de 26px isso virava 5 quadrados iguais. Aqui cada item ganha uma
    // SILHUETA propria com fundo transparente.
    {
        // Fundo vazado. tile_fill com alpha 0 - o atlas e' RGBA, entao da' pra ter silhueta de
        // verdade em vez de um quadrado cheio.
        auto clear_tile = [&](Tile t) { tile_fill(atlas, t, c8(0, 0, 0, 0)); };

        // Escreve uma linha horizontal de pixels (x0..x1 inclusive) numa linha y.
        auto row = [&](Tile t, int y, int x0, int x1, Color8 c) {
            for (int x = x0; x <= x1; ++x) tile_set_px(atlas, t, x, y, c);
        };

        // LINGOTE: a forma compartilhada por Ferro/Cobre/Metal. Face de topo mais estreita que a
        // base (perspectiva de lingote fundido), realce em cima, sombra embaixo, contorno escuro.
        // Compartilhar a forma e variar a COR e' de proposito: os tres sao metais em barra, e o que
        // precisa diferenciar e' a cor, que era exatamente o que faltava.
        auto ingot = [&](Tile t, Color8 base, Color8 hi, Color8 lo, Color8 edge) {
            clear_tile(t);
            row(t, 4,  6, 10, edge);
            row(t, 5,  5, 11, hi);
            row(t, 6,  4, 12, hi);
            row(t, 7,  3, 13, base);
            row(t, 8,  2, 14, base);
            row(t, 9,  2, 14, base);
            row(t, 10, 2, 14, lo);
            row(t, 11, 3, 13, lo);
            row(t, 12, 5, 11, edge);
            // Contorno lateral.
            for (int y = 5; y <= 11; ++y) {
                tile_set_px(atlas, t, 2, y, edge);
                tile_set_px(atlas, t, 14, y, edge);
            }
            // Brilho especular pequeno na face de topo.
            tile_set_px(atlas, t, 6, 6, c8(255, 255, 255, 210));
            tile_set_px(atlas, t, 7, 6, c8(255, 255, 255, 150));
        };

        ingot(Tile::ItemIron,   c8(126, 82, 66), c8(168, 112, 90), c8(88, 56, 44), c8(44, 28, 22));
        ingot(Tile::ItemCopper, c8(214, 118, 46), c8(246, 164, 84), c8(150, 76, 26), c8(70, 34, 12));
        ingot(Tile::ItemMetal,  c8(178, 186, 198), c8(226, 232, 240), c8(120, 128, 140), c8(52, 56, 64));

        // CARVAO: massa angular preta com facetas mais claras - nada de barra, pra nao confundir
        // com os metais.
        {
            Tile t = Tile::ItemCoal;
            clear_tile(t);
            Color8 body = c8(38, 38, 44), face = c8(74, 74, 84), edge = c8(14, 14, 18);
            row(t, 3,  6, 9,  edge);
            row(t, 4,  5, 10, body);
            row(t, 5,  4, 11, body);
            row(t, 6,  3, 12, body);
            row(t, 7,  3, 12, body);
            row(t, 8,  2, 13, body);
            row(t, 9,  3, 12, body);
            row(t, 10, 4, 11, body);
            row(t, 11, 5, 10, edge);
            row(t, 12, 7, 9,  edge);
            // Facetas: dois planos claros dao volume a uma massa que seria uma bolha preta.
            row(t, 5,  6, 8,  face);
            row(t, 6,  5, 7,  face);
            tile_set_px(atlas, t, 10, 8, face);
            tile_set_px(atlas, t, 11, 8, face);
        }

        // CRISTAL: gema facetada em losango - a unica silhueta pontuda do conjunto.
        {
            Tile t = Tile::ItemCrystal;
            clear_tile(t);
            Color8 body = c8(150, 72, 214), lit = c8(214, 158, 250), edge = c8(66, 26, 104);
            row(t, 2,  7, 8,  edge);
            row(t, 3,  6, 9,  body);
            row(t, 4,  5, 10, body);
            row(t, 5,  4, 11, body);
            row(t, 6,  3, 12, body);
            row(t, 7,  3, 12, body);
            row(t, 8,  4, 11, body);
            row(t, 9,  5, 10, body);
            row(t, 10, 6, 9,  body);
            row(t, 11, 7, 8,  edge);
            // Faceta iluminada na metade esquerda + reflexo.
            row(t, 4,  6, 7,  lit);
            row(t, 5,  5, 7,  lit);
            row(t, 6,  4, 6,  lit);
            row(t, 7,  4, 5,  lit);
            tile_set_px(atlas, t, 9, 5, c8(255, 255, 255, 220));
        }

        // PISTOLA: a arma usava a arte de ANTENA tintada de ciano (Tile::Antenna) - o icone nao
        // parecia nem de longe uma pistola. Silhueta lateral simples: ferrolho, cano, cabo e um
        // ponto de emissor aceso na ponta.
        {
            Tile t = Tile::ItemPistol;
            clear_tile(t);
            Color8 body = c8(88, 96, 110), dark = c8(44, 50, 60), lit = c8(120, 232, 255);
            row(t, 4,  3, 11, dark);
            row(t, 5,  3, 12, body);   // ferrolho
            row(t, 6,  3, 12, body);
            row(t, 7,  3, 6,  body);   // corpo
            row(t, 8,  4, 6,  body);
            row(t, 9,  4, 6,  body);   // cabo
            row(t, 10, 4, 7,  body);
            row(t, 11, 5, 7,  dark);
            // Guarda-mato.
            tile_set_px(atlas, t, 7, 8, dark);
            tile_set_px(atlas, t, 8, 8, dark);
            // Celula de energia e emissor acesos - o que faz ler como arma de energia.
            row(t, 5,  8, 10, lit);
            tile_set_px(atlas, t, 13, 5, lit);
            tile_set_px(atlas, t, 13, 6, lit);
            tile_set_px(atlas, t, 12, 5, c8(220, 250, 255, 255));
        }
    }
}

void init_texture_atlas() {
    if (g_tex_atlas != 0) return;

    std::vector<uint8_t> pixels;
    tile_generate_all(pixels);

    Image img{};
    img.data = pixels.data();
    img.width = kAtlasSizePx;
    img.height = kAtlasSizePx;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;

    Texture2D tex = LoadTextureFromImage(img); // copies pixels into a GPU texture
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);   // matches the old GL_NEAREST
    SetTextureWrap(tex, TEXTURE_WRAP_CLAMP);       // matches the old GL_CLAMP
    g_tex_atlas = tex.id;
}

BlockTex block_tex(Block b) {
    BlockTex t{};
    switch (b) {
        case Block::Grass: t = {Tile::GrassTop, Tile::GrassSide, Tile::Dirt, true, false, false}; break;
        case Block::Dirt:  t = {Tile::Dirt, Tile::Dirt, Tile::Dirt, false, false, false}; break;
        case Block::Stone: t = {Tile::Stone, Tile::Stone, Tile::Stone, false, false, false}; break;
        case Block::Sand:  t = {Tile::Sand, Tile::Sand, Tile::Sand, false, false, false}; break;
        case Block::Water: t = {Tile::Water0, Tile::Water0, Tile::Water0, true, true, true}; break;
        // Arte propria (Lava0..3), nao mais o tile da AGUA com tint laranja - ver comentario
        // no enum Tile (textures.h). is_water fica FALSE de proposito: aquele flag forcava o
        // tile Water0+frame de volta e afundava o desenho 0.18 como se fosse agua; a animacao
        // da lava e' feita com seu proprio contador de frame no loop de terreno (main.cpp).
        // transparent=false: lava e' opaca (antes vinha com alpha 0.92 do ramo da agua).
        case Block::Lava:  t = {Tile::Lava0, Tile::Lava0, Tile::Lava0, true, false, false}; break;
        case Block::Ice:   t = {Tile::Ice, Tile::Ice, Tile::Ice, false, true, false}; break;
        case Block::Snow:  t = {Tile::Snow, Tile::Snow, Tile::Snow, false, false, false}; break;
        case Block::Wood:  t = {Tile::WoodTop, Tile::WoodSide, Tile::WoodTop, false, false, false}; break;
        case Block::Leaves:t = {Tile::Leaves, Tile::Leaves, Tile::Leaves, true, true, false}; break;
        case Block::Coal:  t = {Tile::CoalOre, Tile::CoalOre, Tile::CoalOre, false, false, false}; break;
        case Block::Iron:  t = {Tile::IronOre, Tile::IronOre, Tile::IronOre, false, false, false}; break;
        case Block::Copper:t = {Tile::CopperOre, Tile::CopperOre, Tile::CopperOre, false, false, false}; break;
        case Block::Crystal:t = {Tile::CrystalOre, Tile::CrystalOre, Tile::CrystalOre, false, false, false}; break;
        case Block::Metal: t = {Tile::MetalOre, Tile::MetalOre, Tile::MetalOre, false, false, false}; break;
        case Block::Organic:t = {Tile::Organic, Tile::Organic, Tile::Organic, false, false, false}; break;
        case Block::Components:t = {Tile::Components, Tile::Components, Tile::Components, false, false, false}; break;

        // Modules
        case Block::SolarPanel: t = {Tile::SolarPanel, Tile::SolarPanel, Tile::SolarPanel, false, false, false}; break;
        case Block::EnergyGenerator: t = {Tile::EnergyGenerator, Tile::EnergyGenerator, Tile::EnergyGenerator, false, false, false}; break;
        case Block::WaterExtractor: t = {Tile::WaterExtractor, Tile::WaterExtractor, Tile::WaterExtractor, false, false, false}; break;
        case Block::OxygenGenerator: t = {Tile::OxygenGenerator, Tile::OxygenGenerator, Tile::OxygenGenerator, false, false, false}; break;
        case Block::Greenhouse: t = {Tile::Greenhouse, Tile::Greenhouse, Tile::Greenhouse, false, true, false}; break;
        case Block::CO2Factory: t = {Tile::CO2Factory, Tile::CO2Factory, Tile::CO2Factory, false, false, false}; break;
        case Block::Habitat: t = {Tile::Habitat, Tile::Habitat, Tile::Habitat, false, true, false}; break;
        case Block::Workshop: t = {Tile::Workshop, Tile::Workshop, Tile::Workshop, false, false, false}; break;
        case Block::TerraformerBeacon: t = {Tile::Terraformer, Tile::Terraformer, Tile::Terraformer, false, false, false}; break;

        // Base structures
        case Block::RocketHull: t = {Tile::RocketHull, Tile::RocketHull, Tile::RocketHull, false, false, false}; break;
        case Block::RocketEngine: t = {Tile::RocketEngine, Tile::RocketEngine, Tile::RocketEngine, false, false, false}; break;
        case Block::RocketWindow: t = {Tile::RocketWindow, Tile::RocketWindow, Tile::RocketWindow, false, true, false}; break;
        case Block::RocketNose: t = {Tile::RocketNose, Tile::RocketNose, Tile::RocketNose, false, false, false}; break;
        case Block::RocketFin: t = {Tile::RocketFin, Tile::RocketFin, Tile::RocketFin, false, false, false}; break;
        case Block::RocketDoor: t = {Tile::RocketDoor, Tile::RocketDoor, Tile::RocketDoor, false, false, false}; break;
        case Block::DomeGlass: t = {Tile::DomeGlass, Tile::DomeGlass, Tile::DomeGlass, false, true, false}; break;
        case Block::DomeFrame: t = {Tile::DomeFrame, Tile::DomeFrame, Tile::DomeFrame, false, false, false}; break;
        case Block::LandingPad: t = {Tile::LandingPad, Tile::LandingPad, Tile::LandingPad, false, false, false}; break;
        case Block::BuildSlot: t = {Tile::BuildSlot, Tile::BuildSlot, Tile::BuildSlot, false, true, false}; break;
        case Block::PipeH:
        case Block::PipeV: t = {Tile::Pipe, Tile::Pipe, Tile::Pipe, false, false, false}; break;
        case Block::Antenna: t = {Tile::Antenna, Tile::Antenna, Tile::Antenna, false, false, false}; break;
        // Reaproveita a arte de Metal (uses_tint=true aplica o dourado de block_color() por
        // cima) - nunca colocado como tile do mundo, so existe pra icone/HUD/popup de coleta.
        case Block::RefinedAlloy: t = {Tile::Metal, Tile::Metal, Tile::Metal, true, false, false}; break;
        // Reaproveita a arte da Antena (uses_tint=true aplica o ciano de block_color() por
        // cima) - nunca colocado como tile do mundo, so existe pra icone do hotbar.
        case Block::LaserPistol: t = {Tile::Antenna, Tile::Antenna, Tile::Antenna, true, false, false}; break;
        // uses_tint=false: a arte ja e' colorida (nao um tile "valor" cinza pra ser tintado
        // depois), igual LandingPad/DomeFrame. O campo `side` importa: o complexo da base fica 1
        // unidade de heightmap acima da planicie em volta, e o terreno desenha as faces laterais
        // por diferenca de altura de vizinho - e' o que forma a mureta da borda.
        case Block::BaseFloor:  t = {Tile::BaseFloor, Tile::BaseFloor, Tile::BaseFloor, false, false, false}; break;
        case Block::PlanterBed: t = {Tile::PlanterBed, Tile::PlanterBed, Tile::PlanterBed, false, false, false}; break;
        // Idem: rede de seguranca, estes nunca deveriam chegar a ser desenhados.
        case Block::FurnitureLow:
        case Block::FurnitureMid:
        case Block::FurnitureTall: t = {Tile::Metal, Tile::Metal, Tile::Metal, false, false, false}; break;
        case Block::FurnitureHuge: t = {Tile::Metal, Tile::Metal, Tile::Metal, false, false, false}; break;
        // Basalto reaproveita a arte de Stone com tint escuro - mesmo padrao de Lava (Water0) e
        // RefinedAlloy (Metal): sem asset novo.
        case Block::Basalt: t = {Tile::Stone, Tile::Stone, Tile::Stone, true, false, false}; break;
        case Block::BaseShell: t = {Tile::Metal, Tile::Metal, Tile::Metal, false, false, false}; break;

        default: t = {Tile::Missing, Tile::Missing, Tile::Missing, false, false, false}; break;
    }
    return t;
}

// Ver comentario da declaracao em textures.h.
BlockTex block_icon_tex(Block b) {
    BlockTex t = block_tex(b);   // herda is_water/transparent/uses_tint e serve de fallback
    Tile item = Tile::Missing;
    switch (b) {
        case Block::Iron:        item = Tile::ItemIron; break;
        case Block::Copper:      item = Tile::ItemCopper; break;
        case Block::Metal:       item = Tile::ItemMetal; break;
        case Block::Coal:        item = Tile::ItemCoal; break;
        case Block::Crystal:     item = Tile::ItemCrystal; break;
        case Block::LaserPistol: item = Tile::ItemPistol; break;
        // Liga Refinada reusa o lingote de Metal com o tint dourado que block_color ja da' -
        // mesma tecnica de antes, mas agora sobre uma forma de lingote em vez de chapa lisa.
        case Block::RefinedAlloy: item = Tile::ItemMetal; break;
        default: return t;
    }
    t.top = item;
    t.side = item;
    t.bottom = item;
    // A silhueta ja traz a cor: tintar de novo (o caso de LaserPistol/RefinedAlloy em block_tex)
    // lavaria o desenho. Excecao: Liga Refinada, cujo dourado E' o que a distingue do Metal.
    t.uses_tint = (b == Block::RefinedAlloy);
    return t;
}
