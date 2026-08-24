#pragma once

#include "blocks.h"      // Block

// ============= TEXTURAS (ESTILO MINICRAFT / PIXEL ART) =============
// Extracted verbatim from main.cpp (original lines ~528-1017).
// Sem assets externos: atlas gerado proceduralmente em tempo de execucao.

enum class Tile : int {
    Missing = 0,
    // Naturais
    GrassTop,
    GrassSide,
    Dirt,
    Stone,
    Sand,
    Water0,
    Water1,
    Water2,
    Water3,
    Ice,
    Snow,
    WoodTop,
    WoodSide,
    Leaves,
    // Recursos
    CoalOre,
    IronOre,
    CopperOre,
    CrystalOre,
    Metal,
    Organic,
    Components,
    // Modulos
    SolarPanel,
    EnergyGenerator,
    WaterExtractor,
    OxygenGenerator,
    Greenhouse,
    CO2Factory,
    Habitat,
    Workshop,
    Terraformer,
    // Estruturas base
    RocketHull,
    RocketEngine,
    RocketWindow,
    RocketNose,
    RocketFin,
    RocketDoor,
    DomeGlass,
    DomeFrame,
    LandingPad,
    BuildSlot,
    Pipe,
    Antenna,
    // Cracks (mining)
    Crack1,
    Crack2,
    Crack3,
    Crack4,
    Crack5,
    Crack6,
    Crack7,
    Crack8,
    // Lava (4 frames animados) - arte propria, nao mais o tile de AGUA reaproveitado com
    // tint laranja. O reuso lia como "agua laranja com ondinhas" e o jogador reclamou que
    // "a lava nao parece incandescente": lava de verdade e' crosta escura QUEBRADA com
    // rachaduras brilhantes por dentro, nao uma superficie clara uniforme. Acrescentados no
    // FIM do enum de proposito - o resto do codigo faz aritmetica de indice em cima de
    // Water0..3 e Crack1..8, mexer na ordem quebraria aquilo.
    Lava0,
    Lava1,
    Lava2,
    Lava3,
};

struct UvRect {
    float u0, v0, u1, v1;
};

// Atlas pixel dimensions (256x256 = 16x16 tiles of 16px each). Exposed so callers who need
// pixel-space rectangles (render_quad_tex's DrawTexturePro path in render_primitives.cpp) can
// convert atlas_uv()'s normalized output without duplicating the atlas layout constant.
constexpr int kAtlasSizePx = 256;

// Returns the tile's texture-atlas rectangle as normalized (0..1) UV coordinates, in the
// same bottom-origin convention the original OpenGL fixed-function code used (see
// tile_set_px() below): v1 corresponds to the top of the tile's artwork, v0 to the bottom.
// This convention is unchanged by the raylib migration and stays the single source of truth
// for atlas coordinates - both the manual rlgl 3D texturing paths (render_wall_3d_tex,
// render_cube_3d_tex, render_plane_3d_tex, all using rlTexCoord2f with these same normalized
// values exactly as before) and the 2D render_quad_tex path (which converts this rect to a
// pixel-space raylib Rectangle with a negative height to reproduce the same v1->v0 sampling
// direction - see render_quad_tex in render_primitives.cpp) read directly from here.
UvRect atlas_uv(Tile t);

void init_texture_atlas();

extern unsigned int g_tex_atlas;

struct BlockTex {
    Tile top = Tile::Missing;
    Tile side = Tile::Missing;
    Tile bottom = Tile::Missing;
    bool uses_tint = false;      // Se true, multiplicar textura por block_color() (vida/atmosfera)
    bool transparent = false;    // Se true, respeitar alpha do block_color()
    bool is_water = false;       // Para pequenas regras de render (altura/anim)
};

BlockTex block_tex(Block b);

// ============= Block Colors =============
void block_color(Block b, int y, int world_h, float& r, float& g, float& bl, float& a);
