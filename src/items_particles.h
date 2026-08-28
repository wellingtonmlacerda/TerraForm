#pragma once

#include "blocks.h"
#include "math_core.h"

#include <vector>

// ============= Particles / Item Drops =============
// Extracted verbatim from main.cpp (original lines ~300-330, 2139-2346): the small
// value-type structs used by the effects/pickup subsystem, plus the free functions that
// spawn/update them.
//
// struct ShootingStar is defined here too (it was textually interleaved with Particle/
// ItemDrop in main.cpp's original struct block), but its instance (g_shooting_stars) and
// its update function (update_shooting_stars) stay in main.cpp - they belong to the sky/
// day-night system, a separate future extraction stage, not to this
// items_particles/modules_building/inventory_crafting stage. main.cpp keeps
// "static std::vector<ShootingStar> g_shooting_stars;" as before, just now referring to
// the type defined here.

struct Particle {
    Vec2 pos;
    Vec2 vel;
    float life;
    float r, g, b, a;
};

// O unico vetor de particulas do jogo. Definido (nao-static) em items_particles.cpp:
// main.cpp continua usando g_particles diretamente (efeitos de update_game, clear() no
// respawn/novo jogo/load_game) atraves desta declaracao extern - mesmo padrao de
// g_world/g_camera em estagios anteriores.
extern std::vector<Particle> g_particles;

// Eventos do ceu: estrelas cadentes (camera-relative para parecer "longe" do mundo).
struct ShootingStar {
    Vec3 offset;   // relativo ao camera (x/z). y em coordenada absoluta do ceu
    Vec3 vel;      // unidades por segundo (no espaco do offset)
    float life = 0.0f;
    float max_life = 0.0f;
    float length = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f;
};

// Drops coletaveis (estilo Minicraft/Minecraft)
struct ItemDrop {
    Block item = Block::Stone;
    float x = 0.0f;
    float z = 0.0f;
    float y = 0.25f;
    float vy = 0.0f;
    float t = 0.0f;
    float pickup_delay = 0.12f;
};

// O unico vetor de drops do jogo (e o indice do drop mirado). Definidos (nao-static) em
// items_particles.cpp: main.cpp continua usando g_drops/g_target_drop diretamente
// (renderizacao dos drops, raycast de mira, clear() no respawn/novo jogo/load_game)
// atraves desta declaracao extern.
extern std::vector<ItemDrop> g_drops;
extern int g_target_drop; // indice em g_drops sob a mira (se houver)

// ============= Effects / pickup functions =============
// Todas perderam o "static" que tinham em main.cpp: main.cpp ainda as chama (secao de
// mineracao/colocacao de update_game, fora do escopo desta etapa) de outra unidade de
// traducao agora. on_pickup_item() NAO esta declarada aqui - so e chamada internamente
// por update_item_drops() (que se move junto, para o mesmo arquivo), entao continua
// `static` dentro de items_particles.cpp.
void spawn_block_particles(Block b, float cx, float cy, int world_h);
Block drop_item_for_block(Block broken);
float drop_spawn_y_for_block(Block broken);
void spawn_item_drop(Block item, float x, float z, float spawn_y);
void update_item_drops(float dt);
