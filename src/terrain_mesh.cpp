#include "terrain_mesh.h"

#include "raylib_platform.h"
#include "math_core.h"    // Vec3, kPi, clamp01, kHeightScale
#include "noise.h"        // lerp
#include "blocks.h"       // Block, is_ground_like
#include "textures.h"     // Tile, UvRect, atlas_uv, BlockTex, block_tex, block_color, g_tex_atlas
#include "world.h"        // World, g_world, surface_block_at
#include "render_primitives.h" // FrameFogParams, g_frame_fog
#include "camera.h"       // GameCamera, g_camera
#include "lighting.h"     // LightingSettings, g_lighting, sample_lightmap, compute_depth_factor, apply_color_grading
#include "player_physics.h" // get_player_render_y (for compute_depth_factor's player_height param)

#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

extern float g_day_time; // main.cpp - dono; usado so pra congelar o frame de agua no bake

namespace {

// Um chunk cru guarda os buffers CPU (fonte da verdade pra reconstrucao) + o Mesh (GPU) ja
// enviado. has_mesh=false = nunca construido (chunk fora do alcance ate agora ou mundo novo).
struct TerrainChunk {
    Mesh mesh{};
    bool has_mesh = false;
    bool dirty = true;
    double built_time = -1000.0;
    int built_player_tx = -1000000;
    int built_player_tz = -1000000;
};

std::vector<TerrainChunk> g_chunks;
int g_chunks_w = 0;
int g_chunks_h = 0;
const World* g_chunks_world = nullptr;
Material g_terrain_material{};
bool g_material_ready = false;

// Reconstroi no maximo isso por frame - evita um soluco visivel quando muitos chunks ficam
// obsoletos de uma vez (ex.: acabou de decolar de jetpack e o raio de visao cresceu rapido).
constexpr int kMaxRebuildsPerFrame = 4;
// Nevoa/iluminacao dependem de posicao da camera/hora do dia (mudam todo frame) - um chunk
// distante e' reconstruido de novo depois desse tempo OU se o jogador andou o bastante perto
// dele, pra nao "congelar" a neblina/luz no valor de quando foi construido pela ultima vez.
constexpr float kChunkRefreshSeconds = 3.0f;
constexpr float kChunkRefreshPlayerMoveTiles = 24.0f;

int chunk_index(int cx, int cz) { return cz * g_chunks_w + cx; }

void ensure_chunks_for_world() {
    if (g_chunks_world == g_world && !g_chunks.empty()) return;
    // Mundo trocado (Novo Jogo/Carregar) ou primeira vez - descarta tudo e recomeca. g_world
    // sempre tem as mesmas dimensoes (kWorldWidth/kWorldHeight), so o CONTEUDO muda, mas o
    // ponteiro tambem muda (save_load.cpp/ui_menu.cpp sempre fazem "delete g_world; g_world =
    // new World(...)"), entao comparar o ponteiro e' suficiente pra detectar isso.
    for (auto& c : g_chunks) {
        if (c.has_mesh) UnloadMesh(c.mesh);
    }
    g_chunks.clear();
    if (!g_world) { g_chunks_world = nullptr; return; }

    g_chunks_w = (g_world->w + kTerrainChunkTiles - 1) / kTerrainChunkTiles;
    g_chunks_h = (g_world->h + kTerrainChunkTiles - 1) / kTerrainChunkTiles;
    g_chunks.assign((size_t)g_chunks_w * (size_t)g_chunks_h, TerrainChunk{});
    g_chunks_world = g_world;

    if (!g_material_ready) {
        g_terrain_material = LoadMaterialDefault();
        // Mesmo truque de reconstrucao de Texture2D a partir do id cru que
        // render_quad_tex() ja usa (render_primitives.cpp) - g_tex_atlas so guarda o id
        // (unsigned int), o resto dos campos (dimensao/formato) sao conhecidos de sobra
        // (init_texture_atlas() sempre gera o mesmo atlas kAtlasSizePx x kAtlasSizePx
        // RGBA8).
        Texture2D atlas_tex{};
        atlas_tex.id = g_tex_atlas;
        atlas_tex.width = kAtlasSizePx;
        atlas_tex.height = kAtlasSizePx;
        atlas_tex.mipmaps = 1;
        atlas_tex.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        g_terrain_material.maps[MATERIAL_MAP_DIFFUSE].texture = atlas_tex;
        g_material_ready = true;
    }
}

void apply_baked_fog(float wx, float wy, float wz, float& r, float& g, float& b) {
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

// Buffers de construcao (CPU), reusados a cada chunk pra nao realocar toda hora.
struct MeshBuild {
    std::vector<float> positions;
    std::vector<float> texcoords;
    std::vector<unsigned char> colors;
    std::vector<unsigned short> indices;

    void clear() {
        positions.clear();
        texcoords.clear();
        colors.clear();
        indices.clear();
    }

    void push_quad(const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec3& v3,
                   const UvRect& uv, float r, float g, float b, float a) {
        unsigned short base = (unsigned short)(positions.size() / 3);
        const Vec3* verts[4] = {&v0, &v1, &v2, &v3};
        const float us[4] = {uv.u0, uv.u1, uv.u1, uv.u0};
        const float vs[4] = {uv.v0, uv.v0, uv.v1, uv.v1};
        unsigned char cr = (unsigned char)std::clamp((int)(r * 255.0f), 0, 255);
        unsigned char cg = (unsigned char)std::clamp((int)(g * 255.0f), 0, 255);
        unsigned char cb = (unsigned char)std::clamp((int)(b * 255.0f), 0, 255);
        unsigned char ca = (unsigned char)std::clamp((int)(a * 255.0f), 0, 255);
        for (int i = 0; i < 4; ++i) {
            positions.push_back(verts[i]->x);
            positions.push_back(verts[i]->y);
            positions.push_back(verts[i]->z);
            texcoords.push_back(us[i]);
            texcoords.push_back(vs[i]);
            colors.push_back(cr);
            colors.push_back(cg);
            colors.push_back(cb);
            colors.push_back(ca);
        }
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }
};

// Reproduz fielmente o bloco "SOLO (top)" + "LATERAIS (paredes)" de render_world()
// (main.cpp) - mesmas formulas de tint/edge-blend/shading/iluminacao/neblina - so que
// escrevendo num MeshBuild em vez de emitir rlVertex3f na hora. Water usa sempre o quadro 0
// (sem ciclo de animacao) e sem o brilho de sol - simplificacao aceitavel pra agua distante,
// ja congelada num "snapshot" que so' atualiza a cada kChunkRefreshSeconds mesmo.
void bake_tile(MeshBuild& mb, int tx, int tz, float rpy) {
    float base_y = (float)g_world->height_at(tx, tz) * kHeightScale;
    Block surface = surface_block_at(*g_world, tx, tz);
    float world_x = (float)tx;
    float world_z = (float)tz;

    BlockTex gtex = block_tex(surface);
    if (gtex.is_water) {
        gtex.top = Tile::Water0;
        gtex.side = gtex.top;
        gtex.bottom = gtex.top;
    }
    // Lava distante: quadro fixo (Lava0), sem ciclo de animacao - mesma simplificacao que a
    // agua ja usa aqui, o chunk so' e' reconstruido a cada ~3s de qualquer jeito.
    if (surface == Block::Lava) {
        gtex.top = Tile::Lava0;
        gtex.side = gtex.top;
        gtex.bottom = gtex.top;
    }

    float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f, a = 1.0f;
    if (gtex.uses_tint || gtex.transparent) {
        float cr, cg, cb, ca;
        block_color(surface, tz, g_world->h, cr, cg, cb, ca);
        if (gtex.uses_tint) { tint_r = cr; tint_g = cg; tint_b = cb; }
        if (gtex.transparent) a = ca;
    }
    // camera_occluder_alpha_for_tile() nao se aplica aqui de proposito - so cobre o segmento
    // curto camera<->jogador (tiles bem perto), nunca alcanca o raio "longe" que este modulo
    // cobre.

    float neigh_r = 0.0f, neigh_g = 0.0f, neigh_b = 0.0f;
    int neigh_count = 0, diff_count = 0;
    const int nx[4] = {1, -1, 0, 0};
    const int nz[4] = {0, 0, 1, -1};
    for (int ni = 0; ni < 4; ++ni) {
        int sx = tx + nx[ni], sz = tz + nz[ni];
        if (!g_world->in_bounds(sx, sz)) continue;
        Block sb = surface_block_at(*g_world, sx, sz);
        BlockTex sbtex = block_tex(sb);
        float sr = 1.0f, sg = 1.0f, sbb = 1.0f;
        if (sbtex.uses_tint || sbtex.transparent) {
            float cr, cg, cb, ca;
            block_color(sb, sz, g_world->h, cr, cg, cb, ca);
            if (sbtex.uses_tint) { sr = cr; sg = cg; sbb = cb; }
        }
        neigh_r += sr; neigh_g += sg; neigh_b += sbb;
        neigh_count++;
        if (sb != surface) diff_count++;
    }
    if (neigh_count > 0 && diff_count > 0) {
        float inv = 1.0f / (float)neigh_count;
        neigh_r *= inv; neigh_g *= inv; neigh_b *= inv;
        float edge_blend = ((float)diff_count / 4.0f) * 0.34f;
        tint_r = lerp(tint_r, neigh_r, edge_blend);
        tint_g = lerp(tint_g, neigh_g, edge_blend);
        tint_b = lerp(tint_b, neigh_b, edge_blend);
    }

    float h_here = base_y;
    float h_e = (tx < g_world->w - 1) ? (float)g_world->height_at(tx + 1, tz) * kHeightScale : h_here;
    float h_w = (tx > 0) ? (float)g_world->height_at(tx - 1, tz) * kHeightScale : h_here;
    float h_s = (tz < g_world->h - 1) ? (float)g_world->height_at(tx, tz + 1) * kHeightScale : h_here;
    float h_n = (tz > 0) ? (float)g_world->height_at(tx, tz - 1) * kHeightScale : h_here;

    float dhx = h_e - h_w, dhz = h_s - h_n;
    float slope = std::sqrt(dhx * dhx + dhz * dhz);
    float slope_shade = 1.0f - std::clamp(slope * 0.22f, 0.0f, 0.28f);
    float alt_shade = 0.90f + 0.10f * clamp01(base_y / 18.0f);
    float shade = slope_shade * alt_shade;
    tint_r *= shade; tint_g *= shade; tint_b *= shade;

    // Lava emissiva - MESMA regra do loop de perto (main.cpp): nao escurece por
    // inclinacao/altitude nem pela noite, senao um campo de lava distante ficava marrom
    // apagado enquanto o de perto brilhava, com uma costura obvia entre os dois.
    bool is_lava = (surface == Block::Lava);
    if (is_lava) {
        float cr, cg, cb, ca;
        block_color(surface, tz, g_world->h, cr, cg, cb, ca);
        tint_r = std::min(1.0f, cr * 1.35f);
        tint_g = std::min(1.0f, cg * 1.25f);
        tint_b = std::min(1.0f, cb * 1.20f);
    }

    if (g_lighting.enabled && !is_lava) {
        float light_r, light_g, light_b;
        sample_lightmap(world_x, world_z, light_r, light_g, light_b);
        float depth_factor = compute_depth_factor(base_y, rpy);
        light_r *= depth_factor; light_g *= depth_factor; light_b *= depth_factor;
        tint_r *= light_r; tint_g *= light_g; tint_b *= light_b;
        apply_color_grading(tint_r, tint_g, tint_b);
    }

    constexpr float half = 0.5f;
    UvRect uv_top = atlas_uv(gtex.top);

    if (surface == Block::Water) {
        float water_y = base_y - 0.18f;
        float fr = tint_r, fg = tint_g, fb = tint_b;
        apply_baked_fog(world_x, water_y, world_z, fr, fg, fb);
        mb.push_quad({world_x - half, water_y, world_z - half}, {world_x + half, water_y, world_z - half},
                     {world_x + half, water_y, world_z + half}, {world_x - half, water_y, world_z + half},
                     uv_top, fr, fg, fb, a);
    } else {
        constexpr float kTopEps = 0.01f;
        float top_y = base_y + kTopEps;
        float fr = tint_r, fg = tint_g, fb = tint_b;
        apply_baked_fog(world_x, top_y, world_z, fr, fg, fb);
        mb.push_quad({world_x - half, top_y, world_z - half}, {world_x + half, top_y, world_z - half},
                     {world_x + half, top_y, world_z + half}, {world_x - half, top_y, world_z + half},
                     uv_top, fr, fg, fb, a);
    }

    // Paredes (mesma logica de render_world(): sempre fecha a lacuna entre os topos dos 2
    // tiles vizinhos - so' NAO usar textura esticada quando a diferenca e' pequena, ver
    // kFlatWallThreshold, mesmo raciocinio/valor de main.cpp: uma parede pulada de vez deixava
    // uma fresta de verdade visivel entre os topos ("grade/buraquinhos"); textura esticada
    // numa faixa fina alias/treme com o movimento da camera ("chao piscando"). Diferencas
    // pequenas usam uma UV degenerada (1 ponto so', ver uv_flat) - cor solida sem gradiente,
    // sem lacuna nenhuma.
    constexpr float side_shade = 0.72f;
    constexpr float dark_shade = 0.52f;
    constexpr float kFlatWallThreshold = 1.2f;
    // PAREDE DE TILE DE LAVA = ROCHA. Mesma correcao do loop de perto (main.cpp): a lateral usava a
    // textura/tint do proprio solo, e num tile de lava em encosta isso desenhava um paredao de lava
    // brilhante sem nada o sustentando ("represa no ar do nada"). A lava e' uma camada; o penhasco
    // embaixo e' rocha. Precisa estar nos DOIS caminhos, senao aparece costura entre perto e longe.
    Tile side_tile = gtex.side;
    float wtint_r = tint_r, wtint_g = tint_g, wtint_b = tint_b;
    if (is_lava) {
        side_tile = block_tex(Block::Basalt).side;
        float br, bg, bb, ba;
        block_color(Block::Basalt, tz, g_world->h, br, bg, bb, ba);
        wtint_r = br * shade; wtint_g = bg * shade; wtint_b = bb * shade;
        if (g_lighting.enabled) {
            float lr2, lg2, lb2;
            sample_lightmap(world_x, world_z, lr2, lg2, lb2);
            float df2 = compute_depth_factor(base_y, rpy);
            wtint_r *= lr2 * df2; wtint_g *= lg2 * df2; wtint_b *= lb2 * df2;
            apply_color_grading(wtint_r, wtint_g, wtint_b);
        }
        wtint_r = std::min(1.0f, wtint_r + 0.10f);
        wtint_g = std::min(1.0f, wtint_g + 0.03f);
    }
    // Idem pra AGUA/GELO: a borda de um lago e' barranco de terra, nao um paredao de agua. Precisa
    // estar nos dois caminhos (perto e longe) pelo mesmo motivo do basalto acima - senao aparece
    // costura na fronteira entre o modo imediato e o chunk cacheado.
    if (surface == Block::Water || surface == Block::Ice) {
        side_tile = block_tex(Block::Dirt).side;
        float br, bg, bb, ba;
        block_color(Block::Dirt, tz, g_world->h, br, bg, bb, ba);
        wtint_r = br * shade; wtint_g = bg * shade; wtint_b = bb * shade;
        if (g_lighting.enabled) {
            float lr2, lg2, lb2;
            sample_lightmap(world_x, world_z, lr2, lg2, lb2);
            float df2 = compute_depth_factor(base_y, rpy);
            wtint_r *= lr2 * df2; wtint_g *= lg2 * df2; wtint_b *= lb2 * df2;
            apply_color_grading(wtint_r, wtint_g, wtint_b);
        }
        wtint_r *= 0.72f; wtint_g *= 0.76f; wtint_b *= 0.82f;
    }
    UvRect uv_side = atlas_uv(side_tile);
    UvRect uv_flat = uv_side;
    uv_flat.u1 = uv_flat.u0 = (uv_side.u0 + uv_side.u1) * 0.5f;
    uv_flat.v1 = uv_flat.v0 = (uv_side.v0 + uv_side.v1) * 0.5f;

    auto wall_color = [&](float shade_mult, float wy0, float wy1, float& r, float& g, float& b) {
        r = wtint_r * shade_mult; g = wtint_g * shade_mult; b = wtint_b * shade_mult;
        apply_baked_fog(world_x, (wy0 + wy1) * 0.5f, world_z, r, g, b);
    };

    if (h_e < h_here) {
        float r, g, b; wall_color(side_shade, h_e, h_here, r, g, b);
        const UvRect& uv = ((h_here - h_e) <= kFlatWallThreshold) ? uv_flat : uv_side;
        mb.push_quad({world_x + half, h_e, world_z - half}, {world_x + half, h_e, world_z + half},
                     {world_x + half, h_here, world_z + half}, {world_x + half, h_here, world_z - half},
                     uv, r, g, b, a);
    }
    if (h_w < h_here) {
        float r, g, b; wall_color(dark_shade, h_w, h_here, r, g, b);
        const UvRect& uv = ((h_here - h_w) <= kFlatWallThreshold) ? uv_flat : uv_side;
        mb.push_quad({world_x - half, h_w, world_z + half}, {world_x - half, h_w, world_z - half},
                     {world_x - half, h_here, world_z - half}, {world_x - half, h_here, world_z + half},
                     uv, r, g, b, a);
    }
    if (h_s < h_here) {
        float r, g, b; wall_color(side_shade, h_s, h_here, r, g, b);
        const UvRect& uv = ((h_here - h_s) <= kFlatWallThreshold) ? uv_flat : uv_side;
        mb.push_quad({world_x - half, h_s, world_z + half}, {world_x + half, h_s, world_z + half},
                     {world_x + half, h_here, world_z + half}, {world_x - half, h_here, world_z + half},
                     uv, r, g, b, a);
    }
    if (h_n < h_here) {
        float r, g, b; wall_color(dark_shade, h_n, h_here, r, g, b);
        const UvRect& uv = ((h_here - h_n) <= kFlatWallThreshold) ? uv_flat : uv_side;
        mb.push_quad({world_x + half, h_n, world_z - half}, {world_x - half, h_n, world_z - half},
                     {world_x - half, h_here, world_z - half}, {world_x + half, h_here, world_z - half},
                     uv, r, g, b, a);
    }
}

void build_chunk(TerrainChunk& chunk, int cx, int cz, int player_tile_x, int player_tile_z) {
    static MeshBuild mb; // reusado entre chamadas - evita realocar os vectors a cada chunk
    mb.clear();

    int tx0 = cx * kTerrainChunkTiles;
    int tz0 = cz * kTerrainChunkTiles;
    int tx1 = std::min(g_world->w, tx0 + kTerrainChunkTiles);
    int tz1 = std::min(g_world->h, tz0 + kTerrainChunkTiles);

    float rpy = get_player_render_y();
    for (int tz = tz0; tz < tz1; ++tz) {
        for (int tx = tx0; tx < tx1; ++tx) {
            bake_tile(mb, tx, tz, rpy);
        }
    }

    if (chunk.has_mesh) {
        UnloadMesh(chunk.mesh);
        chunk.has_mesh = false;
    }

    if (mb.positions.empty()) {
        // Chunk vazio (fora dos limites do mundo, ex. ultima linha/coluna parcial) - sem
        // geometria nenhuma pra enviar.
        chunk.mesh = Mesh{};
        chunk.dirty = false;
        chunk.built_time = GetTime();
        chunk.built_player_tx = player_tile_x;
        chunk.built_player_tz = player_tile_z;
        return;
    }

    Mesh mesh{};
    mesh.vertexCount = (int)(mb.positions.size() / 3);
    mesh.triangleCount = (int)(mb.indices.size() / 3);
    mesh.vertices = (float*)malloc(mb.positions.size() * sizeof(float));
    mesh.texcoords = (float*)malloc(mb.texcoords.size() * sizeof(float));
    mesh.colors = (unsigned char*)malloc(mb.colors.size() * sizeof(unsigned char));
    mesh.indices = (unsigned short*)malloc(mb.indices.size() * sizeof(unsigned short));
    std::memcpy(mesh.vertices, mb.positions.data(), mb.positions.size() * sizeof(float));
    std::memcpy(mesh.texcoords, mb.texcoords.data(), mb.texcoords.size() * sizeof(float));
    std::memcpy(mesh.colors, mb.colors.data(), mb.colors.size() * sizeof(unsigned char));
    std::memcpy(mesh.indices, mb.indices.data(), mb.indices.size() * sizeof(unsigned short));

    UploadMesh(&mesh, false);

    chunk.mesh = mesh;
    chunk.has_mesh = true;
    chunk.dirty = false;
    chunk.built_time = GetTime();
    chunk.built_player_tx = player_tile_x;
    chunk.built_player_tz = player_tile_z;
}

bool chunk_needs_rebuild(const TerrainChunk& c, int player_tile_x, int player_tile_z) {
    if (!c.has_mesh) return true;
    if (c.dirty) return true;
    if (GetTime() - c.built_time > kChunkRefreshSeconds) return true;
    float dx = (float)(player_tile_x - c.built_player_tx);
    float dz = (float)(player_tile_z - c.built_player_tz);
    if (dx * dx + dz * dz > kChunkRefreshPlayerMoveTiles * kChunkRefreshPlayerMoveTiles) return true;
    return false;
}

} // namespace

bool terrain_mesh_tile_is_far(int tile_x, int tile_z, int player_tile_x, int player_tile_z, int near_radius) {
    int cx = tile_x / kTerrainChunkTiles;
    int cz = tile_z / kTerrainChunkTiles;
    float chunk_center_x = (float)(cx * kTerrainChunkTiles + kTerrainChunkTiles / 2);
    float chunk_center_z = (float)(cz * kTerrainChunkTiles + kTerrainChunkTiles / 2);
    float ddx = chunk_center_x - (float)player_tile_x;
    float ddz = chunk_center_z - (float)player_tile_z;
    float d2 = ddx * ddx + ddz * ddz;
    return d2 >= (float)near_radius * (float)near_radius;
}

void terrain_mesh_mark_dirty(int tile_x, int tile_y) {
    if (g_chunks.empty() || g_chunks_world != g_world) return; // ainda nao inicializado - ok, primeiro desenho ja constroi tudo do zero
    int cx = tile_x / kTerrainChunkTiles;
    int cz = tile_y / kTerrainChunkTiles;
    if (cx < 0 || cz < 0 || cx >= g_chunks_w || cz >= g_chunks_h) return;
    g_chunks[chunk_index(cx, cz)].dirty = true;
}

int terrain_mesh_render_far(int player_tile_x, int player_tile_z, int near_radius, int far_radius) {
    if (!g_world || g_tex_atlas == 0) return 0;
    ensure_chunks_for_world();
    if (g_chunks.empty()) return 0;

    int far_r2 = far_radius * far_radius;

    int cx0 = std::max(0, (player_tile_x - far_radius) / kTerrainChunkTiles);
    int cx1 = std::min(g_chunks_w - 1, (player_tile_x + far_radius) / kTerrainChunkTiles);
    int cz0 = std::max(0, (player_tile_z - far_radius) / kTerrainChunkTiles);
    int cz1 = std::min(g_chunks_h - 1, (player_tile_z + far_radius) / kTerrainChunkTiles);

    // Passo 1: coletar os chunks visiveis que precisam (re)construcao, com sua distancia -
    // reconstroi os MAIS PERTO primeiro (mais visiveis/maiores na tela), deixando os distantes
    // pra proximos frames se o limite por frame estourar. Sem isso (construir na ordem crua
    // do loop) um "estouro" de chunks novos de uma vez (ex.: subindo de jetpack, o raio de
    // visao cresce e revela uma area nova enorme de repente) constroi em ordem arbitraria,
    // as vezes deixando buracos bem na frente do jogador enquanto reconstroi coisa distante.
    struct PendingRebuild { int cx, cz; float d2; };
    static std::vector<PendingRebuild> pending;
    pending.clear();

    struct VisibleChunk { int cx, cz; };
    static std::vector<VisibleChunk> visible;
    visible.clear();

    for (int cz = cz0; cz <= cz1; ++cz) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            // "far?" decidido via terrain_mesh_tile_is_far() - a MESMA funcao que
            // render_world() (main.cpp) usa pra decidir se desenha aquele tile em modo
            // imediato ou pula - garante que os 2 caminhos nunca se sobreponham (ver
            // comentario completo no header). Passa o tile de origem do chunk como
            // representante (qualquer tile dele cai no mesmo indice de chunk, mesmo
            // resultado). O teto far_radius continua sendo um corte simples por
            // centro-do-chunk (sem risco de sobreposicao - so' limita ate onde a malha
            // cacheada se estende).
            int rep_tx = cx * kTerrainChunkTiles;
            int rep_tz = cz * kTerrainChunkTiles;
            if (!terrain_mesh_tile_is_far(rep_tx, rep_tz, player_tile_x, player_tile_z, near_radius)) continue;
            float chunk_center_x = (float)(cx * kTerrainChunkTiles + kTerrainChunkTiles / 2);
            float chunk_center_z = (float)(cz * kTerrainChunkTiles + kTerrainChunkTiles / 2);
            float ddx = chunk_center_x - (float)player_tile_x;
            float ddz = chunk_center_z - (float)player_tile_z;
            float d2 = ddx * ddx + ddz * ddz;
            if (d2 > (float)far_r2) continue;

            visible.push_back({cx, cz});
            TerrainChunk& chunk = g_chunks[chunk_index(cx, cz)];
            if (chunk_needs_rebuild(chunk, player_tile_x, player_tile_z)) {
                pending.push_back({cx, cz, d2});
            }
        }
    }

    std::sort(pending.begin(), pending.end(), [](const PendingRebuild& a, const PendingRebuild& b) {
        return a.d2 < b.d2;
    });
    int rebuild_count = std::min((int)pending.size(), kMaxRebuildsPerFrame);
    for (int i = 0; i < rebuild_count; ++i) {
        TerrainChunk& chunk = g_chunks[chunk_index(pending[i].cx, pending[i].cz)];
        build_chunk(chunk, pending[i].cx, pending[i].cz, player_tile_x, player_tile_z);
    }

    // Passo 2: desenhar tudo que ja tem malha pronta (inclusive as recem-reconstruidas acima).
    int drawn = 0;
    Matrix identity = { 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 };
    for (const auto& v : visible) {
        TerrainChunk& chunk = g_chunks[chunk_index(v.cx, v.cz)];
        if (chunk.has_mesh) {
            DrawMesh(chunk.mesh, g_terrain_material, identity);
            drawn++;
        }
    }

    return drawn;
}

void terrain_mesh_shutdown() {
    for (auto& c : g_chunks) {
        if (c.has_mesh) UnloadMesh(c.mesh);
    }
    g_chunks.clear();
    g_chunks_world = nullptr;
    // Nao chama UnloadMaterial aqui de proposito: material.maps[DIFFUSE].texture aponta pro
    // MESMO g_tex_atlas usado pelo resto do jogo (rlSetTexture(g_tex_atlas) em toda parte) -
    // esse atlas nunca e' explicitamente descarregado em lugar nenhum hoje (so' morre junto
    // com o contexto GL quando CloseWindow() roda logo depois desta chamada) - descarrega-lo
    // aqui arriscaria um double-free se algo mais tentar usa-lo antes do CloseWindow real.
    g_material_ready = false;
}
