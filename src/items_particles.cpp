#include "items_particles.h"

#include "blocks.h"        // is_module, is_solid, block_name
#include "textures.h"      // block_color
#include "game_state.h"    // rng_next_f01, add_collect_popup, show_tip, show_unlock_popup, g_onboarding, OnboardingState, UnlockProgress
#include "world.h"         // World, g_world, surface_height_at
#include "player_physics.h" // g_player
#include "modules_building.h" // check_unlocks
#include "inventory_crafting.h" // g_inventory

#include <algorithm>
#include <cmath>
#include <string>

// Globais de estado de jogo ainda definidas em main.cpp (dono continua sendo main.cpp -
// nao fazem parte desta etapa de extracao). Todas ja eram nao-static em main.cpp
// (extraidas/expostas em fases anteriores); so precisamos da declaracao aqui tambem,
// mesmo padrao de g_physics_cfg em camera.cpp.
extern float g_player_water;
extern float g_player_food;
extern UnlockProgress g_unlocks;

// ============= Particles / Item Drops (state) =============
// O unico vetor de particulas/drops do jogo. Definidos (nao-static) aqui: main.cpp
// continua usando g_particles/g_drops/g_target_drop diretamente (efeitos de update_game,
// clear() no respawn/novo jogo/load_game) atraves da declaracao extern em
// items_particles.h - mesmo padrao de g_world/g_camera nos estagios anteriores.
std::vector<Particle> g_particles;
std::vector<ItemDrop> g_drops;
int g_target_drop = -1; // indice em g_drops sob a mira (se houver)

// ============= Effects =============
void spawn_block_particles(Block b, float cx, float cy, int world_h) {
    float r, g, bl, a;
    block_color(b, (int)cy, world_h, r, g, bl, a);
    for (int i = 0; i < 12; ++i) {
        float ang = rng_next_f01() * 6.2831853f;
        float spd = 2.0f + rng_next_f01() * 4.5f;
        Particle p;
        p.pos = {cx + (rng_next_f01() - 0.5f) * 0.15f, cy + (rng_next_f01() - 0.5f) * 0.15f};
        p.vel = {std::cos(ang) * spd, std::sin(ang) * spd - 2.0f};
        p.life = 0.55f + rng_next_f01() * 0.35f;
        p.r = r;
        p.g = g;
        p.b = bl;
        p.a = 1.0f;
        g_particles.push_back(p);
    }
}

Block drop_item_for_block(Block broken) {
    // Simplifica drops para itens realmente uteis no prototipo.
    switch (broken) {
        case Block::Grass:  return Block::Dirt;
        case Block::Leaves: return Block::Organic;
        case Block::Sand:   return Block::Dirt;
        case Block::Snow:   return Block::Ice;
        case Block::Basalt: return Block::Stone;   // quebrar a crosta rende pedra util
        default:            return broken;
    }
}

float drop_spawn_y_for_block(Block broken) {
    if (broken == Block::Leaves) return 0.70f;
    if (is_module(broken)) return 1.15f;
    if (is_solid(broken)) return 0.95f;
    return 0.35f;
}

void spawn_item_drop(Block item, float x, float z, float spawn_y) {
    ItemDrop d;
    d.item = item;
    d.x = x;
    d.z = z;
    d.y = spawn_y;
    d.vy = 2.8f + rng_next_f01() * 1.2f;
    d.t = rng_next_f01() * 10.0f;
    d.pickup_delay = 0.12f;
    g_drops.push_back(d);

    // Limit simples para evitar crescimento infinito em casos extremos
    if (g_drops.size() > 500u) {
        g_drops.erase(g_drops.begin(), g_drops.begin() + 100);
    }
}

static void on_pickup_item(Block item, float x, float z) {
    g_inventory[(int)item]++;

    // Bonus de sobrevivencia (reforça onboarding: gelo -> agua)
    if (item == Block::Ice) {
        g_player_water = std::min(100.0f, g_player_water + 25.0f);
    } else if (item == Block::Organic) {
        g_player_food = std::min(100.0f, g_player_food + 8.0f);
    }

    // Unlock tracking: total "coletado" (agora no pickup, nao no break)
    switch (item) {
        case Block::Stone: g_unlocks.total_stone++; break;
        case Block::Iron:  g_unlocks.total_iron++; break;
        case Block::Coal:  g_unlocks.total_coal++; break;
        case Block::Copper: g_unlocks.total_copper++; break;
        case Block::Wood:  g_unlocks.total_wood++; break;
        case Block::Ice:   g_unlocks.total_ice++; break;
        case Block::Crystal: g_unlocks.total_crystal++; break;
        case Block::Metal: g_unlocks.total_metal++; break;
        case Block::Organic: g_unlocks.total_organic++; break;
        case Block::Components: g_unlocks.total_components++; break;
        default: break;
    }

    // Popup leve de coleta (feedback no HUD, estilo Minicraft)
    {
        float cr, cg, cb, ca;
        block_color(item, (int)std::floor(g_player.pos.y), g_world->h, cr, cg, cb, ca);
        float jitter_x = (rng_next_f01() - 0.5f) * 90.0f;

        std::string txt = "+1 ";
        txt += block_name(item);
        if (item == Block::Ice) txt += " (+25 Agua)";
        else if (item == Block::Organic) txt += " (+8 Comida)";

        add_collect_popup(jitter_x, 0.0f, txt, cr, cg, cb, item, 1);
    }
    (void)x; (void)z;

    if (!g_onboarding.shown_first_collect) {
        show_tip("Tab para abrir menu de construcao", g_onboarding.shown_first_collect);
    }

    bool had_solar = g_unlocks.solar_unlocked;
    bool had_water = g_unlocks.water_extractor_unlocked;
    bool had_o2 = g_unlocks.o2_generator_unlocked;
    bool had_greenhouse = g_unlocks.greenhouse_unlocked;
    bool had_co2 = g_unlocks.co2_factory_unlocked;
    bool had_habitat = g_unlocks.habitat_unlocked;
    bool had_terraform = g_unlocks.terraformer_unlocked;

    check_unlocks();

    if (!had_solar && g_unlocks.solar_unlocked)
        show_unlock_popup("DESBLOQUEADO!", "Painel Solar - Tab para construir");
    if (!had_water && g_unlocks.water_extractor_unlocked)
        show_unlock_popup("DESBLOQUEADO!", "Extrator de Agua - Tab para construir");
    if (!had_o2 && g_unlocks.o2_generator_unlocked)
        show_unlock_popup("DESBLOQUEADO!", "Gerador de O2 - Tab para construir");
    if (!had_greenhouse && g_unlocks.greenhouse_unlocked)
        show_unlock_popup("DESBLOQUEADO!", "Estufa - Tab para construir");
    if (!had_co2 && g_unlocks.co2_factory_unlocked)
        show_unlock_popup("DESBLOQUEADO!", "Fabrica de CO2 - Comece a aquecer!");
    if (!had_habitat && g_unlocks.habitat_unlocked)
        show_unlock_popup("DESBLOQUEADO!", "Habitat - Lar doce lar");
    if (!had_terraform && g_unlocks.terraformer_unlocked)
        show_unlock_popup("DESBLOQUEADO!", "Terraformador - Transforme o planeta!");
}

void update_item_drops(float dt) {
    static constexpr float kRestOffset = 0.22f; // Altura do centro do drop acima do solo
    static constexpr float kGravity = 9.5f;
    static constexpr float kPickupRadius = 1.25f;   // estilo Minicraft: coleta "perto", sem precisar pisar exatamente
    static constexpr float kMagnetRadius = 2.75f;   // leve "imã" para o player (facilita coleta em 3D)
    static constexpr float kMagnetSpeed = 7.5f;     // tiles/s
    static constexpr float kAimPickupRadius = 1.65f;   // mirando no drop: coleta um pouco mais "fácil"
    static constexpr float kAimMagnetRadius = 4.25f;   // mirando: imã mais forte (melhora sensação de "vou pegar isso")
    static constexpr float kAimMagnetSpeed = 18.0f;

    const float pickup_r2 = kPickupRadius * kPickupRadius;
    const float magnet_r2 = kMagnetRadius * kMagnetRadius;
    const float aim_pickup_r2 = kAimPickupRadius * kAimPickupRadius;
    const float aim_magnet_r2 = kAimMagnetRadius * kAimMagnetRadius;

    for (size_t di = 0; di < g_drops.size(); ++di) {
        ItemDrop& d = g_drops[di];
        d.t += dt;
        d.pickup_delay -= dt;

        // Leve atracao ao jogador (apenas apos um pequeno delay, para dar feedback visual do drop)
        if (d.pickup_delay <= 0.0f) {
            float dx = g_player.pos.x - d.x;
            float dz = g_player.pos.y - d.z;
            float dist2 = dx * dx + dz * dz;

            bool aimed = ((int)di == g_target_drop);
            float use_magnet_r2 = aimed ? aim_magnet_r2 : magnet_r2;
            float use_magnet_speed = aimed ? kAimMagnetSpeed : kMagnetSpeed;

            if (dist2 <= use_magnet_r2 && dist2 > 1e-6f) {
                float dist = std::sqrt(dist2);
                float step = std::min(use_magnet_speed * dt, dist);
                float inv = 1.0f / dist;
                d.x += dx * inv * step;
                d.z += dz * inv * step;
            }
        }

        // Fisica simples (queda/bounce)
        d.vy -= kGravity * dt;
        d.y += d.vy * dt;

        // Repouso no chao REAL (altura do heightmap), nao em Y constante (montanhas!)
        float rest_y = kRestOffset;
        if (g_world) {
            int tx = world_to_tile(d.x);
            int tz = world_to_tile(d.z);
            if (g_world->in_bounds(tx, tz)) {
                rest_y = stack_top_height_at(*g_world, tx, tz) + kRestOffset;
            }
        }

        if (d.y < rest_y) {
            d.y = rest_y;
            if (std::fabs(d.vy) < 0.8f) d.vy = 0.0f;
            else d.vy = -d.vy * 0.28f;
        }
    }

    // Coleta por proximidade (considera distancia 2D e altura)
    for (size_t i = 0; i < g_drops.size();) {
        ItemDrop& d = g_drops[i];
        if (d.pickup_delay <= 0.0f) {
            float dx = d.x - g_player.pos.x;
            float dz = d.z - g_player.pos.y;
            float dy = d.y - g_player.pos_y;  // Diferenca de altura
            float dist2_horizontal = dx * dx + dz * dz;
            float height_diff = std::fabs(dy);

            float use_pickup_r2 = ((int)i == g_target_drop) ? aim_pickup_r2 : pickup_r2;

            // Coleta se estiver proximo horizontalmente E verticalmente (dentro de 2 blocos de altura)
            if (dist2_horizontal <= use_pickup_r2 && height_diff < 2.5f) {
                on_pickup_item(d.item, d.x, d.z);
                int removed_idx = (int)i;
                int last_idx = (int)g_drops.size() - 1;
                g_drops[i] = g_drops.back();
                g_drops.pop_back();
                if (g_target_drop == removed_idx) {
                    g_target_drop = -1;
                } else if (g_target_drop == last_idx) {
                    g_target_drop = removed_idx;
                }
                continue;
            }
        }
        ++i;
    }
}
