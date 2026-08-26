#pragma once

#include "blocks.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Dimensoes do mundo gerado (tiles). Era duplicado como "static const int WORLD_WIDTH/
// WORLD_HEIGHT" em main.cpp/win32_platform.cpp/ui_menu.cpp (3 copias identicas - risco real
// de mudar so uma e deixar os fluxos de Novo Jogo/regenerar presos no tamanho antigo).
// Consolidado aqui numa unica fonte, ja que world.h e incluido por todos os 3.
static constexpr int kWorldWidth = 3072;
static constexpr int kWorldHeight = 1536;

// ============= World =============
// Extracted verbatim from main.cpp (struct World, original lines ~161-505). The small
// one-line accessors (in_bounds/get/set/get_ground/set_ground/height_at/set_height/
// rebuild_surface_cache) stay inline in the struct exactly as before. World::gen() (the
// heightmap/erosion/biome generator) is the one method converted to an out-of-line
// definition, declared here and defined in world.cpp — see the comment there for details;
// its body is unchanged from the original.
// Definido em terrain_mesh.cpp - World::set/set_ground/set_height (abaixo) chamam isso pra
// invalidar o chunk de malha cacheada dono do tile (ver terrain_mesh.h). Declarado aqui (nao
// incluindo terrain_mesh.h, que dependeria de World e criaria inclusao circular) - mesmo
// padrao de declaracao solta ja usado no projeto pra evitar dependencia cruzada de headers.
// Precisa vir ANTES de "struct World": os metodos inline abaixo chamam esta funcao livre, e
// lookup de nome de funcao livre (sem ADL, so' recebe ints) segue a ordem normal de
// declaracao no arquivo mesmo dentro de um corpo de metodo inline de classe.
void terrain_mesh_mark_dirty(int tile_x, int tile_y);

struct World {
    // Empilhamento de blocos construidos (torres/paredes) - aditivo sobre o modelo de
    // heightmap+objeto-unico existente, ver plano salvo em
    // C:\Users\9173\.claude\plans\quero-refatorar-todo-o-serene-kazoo.md. Cada coluna pode
    // ter ate kMaxStackExtra blocos extras empilhados em cima do que ja existia
    // (terreno/objeto unico), sem vaos - a pilha sempre comeca exatamente no topo do que
    // ja estava la. Buffer plano w*h*kMaxStackExtra (nao vector-de-vector) por localidade
    // de cache e pra evitar overhead de alocacao por coluna (a maioria das colunas fica
    // vazia).
    static constexpr int kMaxStackExtra = 24;

    int w = 0;
    int h = 0;
    unsigned seed = 1337;
    int sea_level = 0;
    std::vector<Block> tiles;
    std::vector<Block> ground;
    std::vector<int16_t> heightmap; // altura do terreno por tile (0 = nivel base)
    std::vector<int> surface_y;
    std::vector<uint8_t> stack_height; // w*h, quantidade de camadas extras por coluna (0 = sem pilha)
    std::vector<Block> stack_blocks;   // w*h*kMaxStackExtra, camada L da coluna idx em stack_blocks[idx*kMaxStackExtra+L]

    World(int W, int H, unsigned s)
        : w(W)
        , h(H)
        , seed(s)
        , tiles((size_t)W * (size_t)H, Block::Air)
        , ground((size_t)W * (size_t)H, Block::Dirt)
        , heightmap((size_t)W * (size_t)H, 0)
        , surface_y((size_t)W, H / 2)
        , stack_height((size_t)W * (size_t)H, 0)
        , stack_blocks((size_t)W * (size_t)H * (size_t)kMaxStackExtra, Block::Air) {
        gen();
    }

    bool in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }

    Block get(int x, int y) const {
        if (!in_bounds(x, y)) return Block::Stone;
        return tiles[(size_t)y * (size_t)w + (size_t)x];
    }

    void set(int x, int y, Block b) {
        if (!in_bounds(x, y)) return;
        tiles[(size_t)y * (size_t)w + (size_t)x] = b;
        terrain_mesh_mark_dirty(x, y);
    }

    Block get_ground(int x, int y) const {
        if (!in_bounds(x, y)) return Block::Dirt;
        return ground[(size_t)y * (size_t)w + (size_t)x];
    }

    void set_ground(int x, int y, Block b) {
        if (!in_bounds(x, y)) return;
        ground[(size_t)y * (size_t)w + (size_t)x] = b;
        terrain_mesh_mark_dirty(x, y);
    }

    int16_t height_at(int x, int y) const {
        if (!in_bounds(x, y)) return 0;
        return heightmap[(size_t)y * (size_t)w + (size_t)x];
    }

    void set_height(int x, int y, int16_t v) {
        if (!in_bounds(x, y)) return;
        heightmap[(size_t)y * (size_t)w + (size_t)x] = v;
        terrain_mesh_mark_dirty(x, y);
    }

    // ---- Pilha de blocos construidos (empilhamento) ----
    int stack_height_at(int x, int y) const {
        if (!in_bounds(x, y)) return 0;
        return (int)stack_height[(size_t)y * (size_t)w + (size_t)x];
    }

    Block stack_block_at(int x, int y, int layer) const {
        int sh = stack_height_at(x, y);
        if (layer < 0 || layer >= sh) return Block::Air;
        size_t col = (size_t)y * (size_t)w + (size_t)x;
        return stack_blocks[col * (size_t)kMaxStackExtra + (size_t)layer];
    }

    bool stack_push(int x, int y, Block b) {
        if (!in_bounds(x, y)) return false;
        size_t col = (size_t)y * (size_t)w + (size_t)x;
        int sh = (int)stack_height[col];
        if (sh >= kMaxStackExtra) return false;
        stack_blocks[col * (size_t)kMaxStackExtra + (size_t)sh] = b;
        stack_height[col] = (uint8_t)(sh + 1);
        return true;
    }

    bool stack_pop(int x, int y) {
        if (!in_bounds(x, y)) return false;
        size_t col = (size_t)y * (size_t)w + (size_t)x;
        int sh = (int)stack_height[col];
        if (sh <= 0) return false;
        stack_blocks[col * (size_t)kMaxStackExtra + (size_t)(sh - 1)] = Block::Air;
        stack_height[col] = (uint8_t)(sh - 1);
        return true;
    }

    void rebuild_surface_cache() {
        surface_y.assign((size_t)w, h - 1);
        for (int x = 0; x < w; ++x) {
            int sy = h - 1;
            for (int y = 0; y < h; ++y) {
                Block b = get(x, y);
                if (b != Block::Air && b != Block::Water && b != Block::Leaves) {
                    sy = y;
                    break;
                }
            }
            surface_y[(size_t)x] = sy;
        }
    }

    // ============= WORLD GENERATION (Macro Heightmap + Erosion + Biomes) =============
    void gen();
};

// O unico World do jogo. A definicao (nao-static) vive em world.cpp: como o tipo World
// agora e definido aqui, world.cpp e o dono natural do ponteiro global tambem (todos os
// usos de g_world continuam em main.cpp, que ganha esta extern via este header).
extern World* g_world;

// Altura adicional de um bloco acima do terreno (para colisao/ground height).
float get_block_height(Block b);

Block surface_block_at(const World& world, int tx, int tz);
Block object_block_at(const World& world, int tx, int tz);
float surface_height_at(const World& world, int tx, int tz);

// Altura/bloco efetivo da coluna INCLUINDO blocos empilhados pelo jogador (ver
// World::kMaxStackExtra acima) - usado no lugar de surface_height_at/object_block_at
// sempre que "o que ha nessa coluna, contando construcoes" importa (fisica do jogador,
// colisao de camera, mira/colocacao, pouso de item derrubado).
float stack_top_height_at(const World& world, int tx, int tz);
Block stack_top_block_at(const World& world, int tx, int tz);

bool is_mineable(Block b);
int block_hits_required(Block b);

// ============= Terraforming Simulation =============
void try_spawn_tree(World& world, int x, int y);
void terraform_step(World& world, int cx, int cy);
void recompute_terraform_score(World& world);
void update_phase();
void melt_ice_around(World& world, int cx, int cy, int radius);

// ============= AGUA FLUIDA =============
// A agua deste motor nao tem volume proprio: um tile de agua e' `ground == Block::Water`, e a
// superficie dele e' derivada do PROPRIO heightmap (base_y - 0.18 no desenho). E' assim que lagos em
// altitudes diferentes coexistem - World::gen nivela cada bacia num `lake_hn` unico.
//
// Consequencia: cavar ao lado de um lago abaixava o heightmap 4 unidades e a agua NAO reagia - ficava
// um buraco seco encostado no lago, com a parede d'agua cortada. Pedido do jogador: "a agua deve ser
// fluida, quando cavar proximo da agua a agua deve preencher o buraco".
//
// water_flood_from() SEMEIA o espalhamento a partir de (x,y): pega o maior nivel de agua entre os
// vizinhos e enfileira os tiles que estao ABAIXO dele. update_water_flow() consome a fila aos poucos,
// alguns tiles por tick - e' isso que faz a agua ENTRAR no buraco visivelmente em vez de aparecer
// pronta num frame.
//
// Preencher = por o heightmap do tile no nivel da agua e marcar o ground como Water. Nao existe outra
// forma num heightmap: o "volume" do buraco nao e' preservado, o buraco simplesmente passa a ser agua
// na cota do lago - que e' exatamente o que se ve.
//
// Barreiras (nao inunda): tile ja de agua/gelo/lava, qualquer coisa is_base_structure (a base nao
// alaga), e coluna com bloco empilhado - uma parede construida pelo jogador REPRESA a agua.
void water_flood_from(World& world, int x, int y);

// Consome a fila de espalhamento. Chamar 1x por frame.
void update_water_flow(World& world, float dt);

// Desenha os efeitos de agua/vapor pendentes (respingo ao encher um tile, baforadas de vapor ao apagar
// lava). Chamar dentro da regiao 3D de render_world(), depois do terreno. Nao usa o vetor g_particles
// de items_particles: ele e' atualizado mas NUNCA desenhado (sobra da era pre-3D), entao efeito jogado
// nele fica invisivel.
void render_water_fx();

// LAVA FLUIDA. Mesma mecanica de water_flood_from, ritmo ~9x mais lento (lava e' espessa): 1 tile a
// cada 0.85s contra 0.09s da agua. Semeia o escorrimento a partir de (x,y) - a lava so' desce (nunca
// entra em tile de cota igual ou maior), nao invade agua/gelo (quem trata esse encontro e' o
// apagamento) e e' represada por parede construida e por estrutura da base, igual a agua.
void lava_flood_from(World& world, int x, int y);
