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
    // Idem Lava0..3: acrescentados no FIM. O resto do codigo faz aritmetica de indice sobre
    // Water0..3 / Crack1..8 / Lava0..3 - mexer na ordem quebraria aquilo. O atlas tem 256 slots
    // e menos de 60 em uso, sobra de sobra.
    // Idem: acrescentado no FIM. Block::Metal usava Tile::Metal - a MESMA chapa clara e lisa
    // (c8(200,205,212), ruido 10) do piso da base, da mobilia e da Liga Refinada. Como minerio
    // bruto no chao aquilo lia como placa industrial polida, nao como veio de metal na rocha
    // (reclamacao do jogador: "queria que parecesse algo mais bruto"). Tile proprio em vez de
    // mexer em Tile::Metal, que continua servindo o material REFINADO.
    BaseFloor,    // Chapa metalica clara do piso interno da base
    PlanterBed,   // Terra revirada em sulcos, com brotos, para os canteiros da estufa
    MetalOre,     // Rocha escura com veios metalicos irregulares - minerio bruto

    // ---- ARTE DE ITEM (icone de inventario) ----
    // Acrescentados no FIM, como todo o resto. Existem porque o ICONE de inventario nao pode ser a
    // mesma arte do BLOCO no mundo: Carvao, Ferro, Cobre, Cristal e Metal sao todos "rocha cinza
    // com pintas de cor" (proposital no mundo - minerio esta dentro da pedra), e num icone de 26px
    // isso da CINCO quadrados cinza indistinguiveis na barra e no menu de construcao. Reclamacao
    // direta do jogador: "a imagem dos itens nao combina com os que coleto".
    // Sao silhuetas com FUNDO TRANSPARENTE (o atlas e' RGBA) - so' usadas por block_icon_tex(),
    // nunca como face de bloco, onde um fundo vazado ficaria errado.
    ItemIron,
    ItemCopper,
    ItemMetal,
    ItemCoal,
    ItemCrystal,
    ItemPistol,
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

// ============= Arte de ICONE (inventario/UI) =============
// Igual a block_tex() para quase tudo, MAS devolve a silhueta de item (ItemIron..ItemPistol) pros
// blocos cujo icone nao pode ser a arte do mundo. Motivo medido, nao estetico: Carvao/Ferro/Cobre/
// Cristal/Metal compartilham a matriz de rocha cinza (correto num bloco - o minerio esta dentro da
// pedra) e num icone de 26px isso dava cinco quadrados cinza iguais na hotbar e no menu de
// construcao. A Pistola de Laser usava a arte de ANTENA.
//
// Funcao separada em vez de mudar block_tex(): a arte de item tem fundo TRANSPARENTE e como face de
// cubo no mundo ficaria vazada. Quem desenha no mundo continua em block_tex(), quem desenha icone
// usa esta.
BlockTex block_icon_tex(Block b);
