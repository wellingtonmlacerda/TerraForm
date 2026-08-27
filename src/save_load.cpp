#include "save_load.h"

#include "blocks.h"              // Block, kBlockTypeCount, is_ground_like
#include "math_core.h"           // Vec2
#include "world.h"               // World, g_world, surface_height_at
#include "camera.h"              // Camera3D, g_camera, reset_camera_near_player
#include "player_physics.h"      // Player, g_player, reset_player_physics_runtime, world_to_tile is math_core.h
#include "items_particles.h"     // Particle/ItemDrop types, g_particles, g_drops
#include "inventory_crafting.h"  // g_inventory, g_selected
#include "modules_building.h"    // rebuild_modules_from_world
#include "game_state.h"          // UnlockProgress (type)
#include "config_types.h"        // MiniMapRuntime, MapWaypoint (types of g_minimap)
#include "objectives.h"          // kObjectiveCount, objectives_ever_built_snapshot/objectives_current_index/objectives_load_state
#include "creatures.h"           // creature_kills/creature_stats_load (bloco v11)
#include "minimap.h"             // poi_ever_found/poi_discovery_load (bloco v11)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

// Globais de estado de jogo ainda definidas em main.cpp (dono continua sendo main.cpp -
// nao fazem parte desta etapa de extracao). Todas ja eram nao-static em main.cpp
// (extraidas/expostas em fases anteriores); so precisamos da declaracao aqui tambem,
// mesmo padrao de g_physics_cfg em camera.cpp / g_terrain_cfg em world.cpp.
extern float g_energy;        // Deprecated, use g_base_energy
extern float g_water_res;     // Deprecated, maps to g_player_water
extern float g_oxygen;        // Deprecated, maps to g_player_oxygen
extern float g_food;          // Deprecated, maps to g_player_food
extern float g_day_time;
extern float g_temperature;
extern float g_co2_level;
extern float g_atmosphere;
extern float g_terraform;
extern float g_suit_integrity;
extern bool g_victory;
extern bool g_show_build_menu;
extern bool g_surface_dirty;
extern int g_base_x;
extern int g_base_y;
extern TerraPhase g_phase;
extern UnlockProgress g_unlocks;
extern MiniMapRuntime g_minimap;
extern CameraConfig g_camera_cfg;

// g_cam_pos lost its "static" in main.cpp specifically because load_game() below (moved
// to this other translation unit) needs to write it on load - same reasoning as
// g_shooting_stars above. main.cpp keeps the definition (still the "legacy 2D camera
// position" used for smoothing elsewhere in that file, per its own comment there).
extern Vec2 g_cam_pos;

// g_shooting_stars lost its "static" in main.cpp specifically because load_game() below
// (moved to this other translation unit) needs to clear it on load - same pattern as
// g_day_time/g_alerts/g_base_cfg losing "static" for modules_building.cpp's
// update_modules(). Its type (ShootingStar) is defined in items_particles.h, but the
// vector instance itself stays owned/defined by main.cpp (sky/day-night system, a separate
// future extraction stage), so this is a plain extern here, not a header declaration.
extern std::vector<ShootingStar> g_shooting_stars;

// Meteoros em voo precisam ser descartados na carga de um save: um meteoro foi mirado no
// mundo/base ANTIGOS e, se sobrevivesse, cairia num ponto arbitrario do mundo novo escavando
// a cratera (agora bem maior, raio ~10) possivelmente na periferia da base. Como o tipo
// FallingMeteor e' definido dentro de main.cpp (nao num header), aqui vai uma funcao
// minuscula definida la' em vez de um extern do vector - um vector de tipo incompleto nao
// deixaria chamar clear().
void clear_falling_meteors();

// kEnergyMax is a compile-time literal (not mutable state) defined in main.cpp, which
// keeps its own copy too (other deprecated-resource clamps there still use it). Since it's
// a literal, not state, this file keeps its own static constexpr copy rather than sharing
// it via extern - same pattern as kDayLength/kTempThawing in modules_building.cpp/world.cpp.
static constexpr float kEnergyMax = 500.0f;

// Versao atual do formato de save. Era um literal solto em save_game() (10) enquanto load_game()
// tinha um limite SUPERIOR FIXO de 5 no seu if/else de versao - ou seja, load_game() caia no
// "else { return false; }" e REJEITAVA toda save escrita por qualquer build desde a v6. Carregar
// jogo ("Continuar", ui_menu.cpp) nunca funcionou, e os blocos "if (version >= 6..10)" mais abaixo
// eram codigo morto inalcancavel. Uma constante unica pros dois lados torna impossivel os limites
// se separarem de novo.
static constexpr uint32_t kSaveVersion = 11;

bool save_game(const char* path) {
    if (!g_world) return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    const char magic[4] = {'T', 'F', '3', 'D'};  // Atualizado para 3D
    uint32_t version = kSaveVersion;  // v11 - adds creature kills + wreck-discovery latch (missoes)
    uint32_t w = (uint32_t)g_world->w;
    uint32_t h = (uint32_t)g_world->h;
    uint32_t seed = (uint32_t)g_world->seed;

    f.write(magic, 4);
    f.write((const char*)&version, sizeof(version));
    f.write((const char*)&w, sizeof(w));
    f.write((const char*)&h, sizeof(h));
    f.write((const char*)&seed, sizeof(seed));

    f.write((const char*)&g_player.pos, sizeof(g_player.pos));
    f.write((const char*)&g_player.vel, sizeof(g_player.vel));
    int32_t hp = (int32_t)g_player.hp;
    f.write((const char*)&hp, sizeof(hp));

    uint8_t sel = (uint8_t)g_selected;
    f.write((const char*)&sel, sizeof(sel));

    uint32_t inv_count = (uint32_t)kBlockTypeCount;
    f.write((const char*)&inv_count, sizeof(inv_count));
    for (uint32_t i = 0; i < inv_count; ++i) {
        int32_t c = (int32_t)g_inventory[(size_t)i];
        f.write((const char*)&c, sizeof(c));
    }

    f.write((const char*)&g_energy, sizeof(g_energy));
    f.write((const char*)&g_water_res, sizeof(g_water_res));
    f.write((const char*)&g_oxygen, sizeof(g_oxygen));
    f.write((const char*)&g_day_time, sizeof(g_day_time));

    // Version 3 additions
    f.write((const char*)&g_base_x, sizeof(g_base_x));
    f.write((const char*)&g_base_y, sizeof(g_base_y));
    f.write((const char*)&g_food, sizeof(g_food));
    f.write((const char*)&g_temperature, sizeof(g_temperature));
    f.write((const char*)&g_co2_level, sizeof(g_co2_level));
    f.write((const char*)&g_atmosphere, sizeof(g_atmosphere));
    f.write((const char*)&g_terraform, sizeof(g_terraform));
    uint8_t phase = (uint8_t)g_phase;
    f.write((const char*)&phase, sizeof(phase));

    // Unlocks
    f.write((const char*)&g_unlocks.total_stone, sizeof(g_unlocks.total_stone));
    f.write((const char*)&g_unlocks.total_iron, sizeof(g_unlocks.total_iron));
    f.write((const char*)&g_unlocks.total_coal, sizeof(g_unlocks.total_coal));
    f.write((const char*)&g_unlocks.total_copper, sizeof(g_unlocks.total_copper));
    f.write((const char*)&g_unlocks.total_wood, sizeof(g_unlocks.total_wood));
    uint8_t unlocks_flags =
        (g_unlocks.solar_unlocked ? 1 : 0) |
        (g_unlocks.water_extractor_unlocked ? 2 : 0) |
        (g_unlocks.o2_generator_unlocked ? 4 : 0) |
        (g_unlocks.greenhouse_unlocked ? 8 : 0) |
        (g_unlocks.co2_factory_unlocked ? 16 : 0) |
        (g_unlocks.habitat_unlocked ? 32 : 0) |
        (g_unlocks.terraformer_unlocked ? 64 : 0);
    f.write((const char*)&unlocks_flags, sizeof(unlocks_flags));

    // Version 4: Camera 3D settings
    f.write((const char*)&g_camera.distance, sizeof(g_camera.distance));
    f.write((const char*)&g_camera.yaw, sizeof(g_camera.yaw));
    f.write((const char*)&g_camera.pitch, sizeof(g_camera.pitch));
    f.write((const char*)&g_camera.sensitivity, sizeof(g_camera.sensitivity));
    f.write((const char*)&g_player.rotation, sizeof(g_player.rotation));

    static_assert(sizeof(Block) == 1, "Block must be 1 byte for save format.");
    static_assert(sizeof(int16_t) == 2, "int16_t must be 2 bytes for save format.");

    // Ground layer (solo), heightmap e depois top layer (objetos/overrides)
    f.write((const char*)g_world->ground.data(), (std::streamsize)g_world->ground.size());
    f.write((const char*)g_world->heightmap.data(), (std::streamsize)(g_world->heightmap.size() * sizeof(int16_t)));
    f.write((const char*)g_world->tiles.data(), (std::streamsize)g_world->tiles.size());

    // Version 6: Fog of war e waypoints
    // Fog of war
    uint32_t explored_size = (uint32_t)g_minimap.explored.size();
    f.write((const char*)&explored_size, sizeof(explored_size));
    if (explored_size > 0) {
        f.write((const char*)g_minimap.explored.data(), (std::streamsize)explored_size);
    }

    // Waypoints
    uint32_t waypoint_count = (uint32_t)g_minimap.waypoints.size();
    f.write((const char*)&waypoint_count, sizeof(waypoint_count));
    for (const auto& wp : g_minimap.waypoints) {
        f.write((const char*)&wp.x, sizeof(wp.x));
        f.write((const char*)&wp.y, sizeof(wp.y));
        f.write((const char*)&wp.r, sizeof(wp.r));
        f.write((const char*)&wp.g, sizeof(wp.g));
        f.write((const char*)&wp.b, sizeof(wp.b));
        uint8_t visible = wp.visible ? 1 : 0;
        f.write((const char*)&visible, sizeof(visible));
        uint32_t label_len = (uint32_t)wp.label.size();
        f.write((const char*)&label_len, sizeof(label_len));
        if (label_len > 0) {
            f.write(wp.label.c_str(), (std::streamsize)label_len);
        }
    }

    // Version 7: Objetivos do jogador (marco atual + quais modulos ja foram construidos
    // ao menos uma vez).
    uint32_t obj_current = (uint32_t)objectives_current_index();
    f.write((const char*)&obj_current, sizeof(obj_current));
    const bool* ever_built = objectives_ever_built_snapshot();
    uint32_t ever_built_count = (uint32_t)kBlockTypeCount;
    f.write((const char*)&ever_built_count, sizeof(ever_built_count));
    for (uint32_t i = 0; i < ever_built_count; ++i) {
        uint8_t v = ever_built[i] ? 1 : 0;
        f.write((const char*)&v, sizeof(v));
    }

    // Version 8: pilhas de blocos construidos (empilhamento) - formato esparso, so
    // colunas com stack_height > 0 (a maioria fica vazia).
    std::vector<uint32_t> stacked_columns;
    uint32_t total_columns = (uint32_t)(g_world->w * g_world->h);
    for (uint32_t i = 0; i < total_columns; ++i) {
        if (g_world->stack_height[i] > 0) stacked_columns.push_back(i);
    }
    uint32_t stack_entry_count = (uint32_t)stacked_columns.size();
    f.write((const char*)&stack_entry_count, sizeof(stack_entry_count));
    for (uint32_t col : stacked_columns) {
        uint8_t sh = g_world->stack_height[col];
        f.write((const char*)&col, sizeof(col));
        f.write((const char*)&sh, sizeof(sh));
        f.write((const char*)&g_world->stack_blocks[(size_t)col * (size_t)World::kMaxStackExtra], (std::streamsize)sh);
    }

    // Version 9: integridade do traje + posicoes dos modulos aprimorados (tecla R).
    f.write((const char*)&g_suit_integrity, sizeof(g_suit_integrity));
    std::vector<std::pair<int32_t, int32_t>> upgraded_positions;
    for (const Module& m : g_modules) {
        if (m.upgraded) upgraded_positions.push_back({(int32_t)m.x, (int32_t)m.y});
    }
    uint32_t upgraded_count = (uint32_t)upgraded_positions.size();
    f.write((const char*)&upgraded_count, sizeof(upgraded_count));
    for (const auto& pos : upgraded_positions) {
        f.write((const char*)&pos.first, sizeof(pos.first));
        f.write((const char*)&pos.second, sizeof(pos.second));
    }

    // Version 10: novos totais de desbloqueio (gelo/cristal/metal/organico/componentes) -
    // UnlockProgress e um blob de layout fixo (nao autodescritivo como g_inventory), entao
    // saves antigos genuinamente nao tem esses bytes.
    f.write((const char*)&g_unlocks.total_ice, sizeof(g_unlocks.total_ice));
    f.write((const char*)&g_unlocks.total_crystal, sizeof(g_unlocks.total_crystal));
    f.write((const char*)&g_unlocks.total_metal, sizeof(g_unlocks.total_metal));
    f.write((const char*)&g_unlocks.total_organic, sizeof(g_unlocks.total_organic));
    f.write((const char*)&g_unlocks.total_components, sizeof(g_unlocks.total_components));

    // Version 11: progresso das missoes de combate e exploracao. As duas condicoes precisam ser
    // MONOTONICAS e sobreviver a um reload, senao o marco atual poderia desconcluir.
    // O NIVEL DA ARMA nao entra aqui de proposito: ele mora em g_inventory[LaserPistol] (ver
    // weapon_level em creatures.h), e o inventario ja e' salvo inteiro logo no comeco deste arquivo.
    uint32_t creature_kills_v = (uint32_t)creature_kills();
    f.write((const char*)&creature_kills_v, sizeof(creature_kills_v));
    uint8_t poi_found_v = poi_ever_found() ? 1 : 0;
    f.write((const char*)&poi_found_v, sizeof(poi_found_v));

    return (bool)f;
}

bool load_game(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    char magic[4] = {};
    f.read(magic, 4);
    // Aceitar tanto TF2D (versoes antigas) quanto TF3D (versao 4+)
    bool valid_magic = (magic[0] == 'T' && magic[1] == 'F' &&
                        (magic[2] == '2' || magic[2] == '3') && magic[3] == 'D');
    if (!f || !valid_magic) return false;

    uint32_t version = 0;
    uint32_t w = 0, h = 0, seed = 0;
    f.read((char*)&version, sizeof(version));
    f.read((char*)&w, sizeof(w));
    f.read((char*)&h, sizeof(h));
    f.read((char*)&seed, sizeof(seed));
    if (!f || w == 0 || h == 0 || w > 4096 || h > 4096) return false;

    Vec2 pos{}, vel{};
    int32_t hp = 100;
    uint8_t sel = (uint8_t)Block::Dirt;
    f.read((char*)&pos, sizeof(pos));
    f.read((char*)&vel, sizeof(vel));
    f.read((char*)&hp, sizeof(hp));
    f.read((char*)&sel, sizeof(sel));
    if (!f) return false;

    std::array<int, kBlockTypeCount> inv = {};
    inv.fill(0);

    float energy = 0.0f, water_res = 100.0f, oxygen = 100.0f, day_time = 0.0f;
    float food = 100.0f, temperature = -60.0f, co2_level = 0.0f, atmosphere = 5.0f, terraform = 0.0f;
    int base_x = 0, base_y = 0;
    uint8_t phase = 0;
    UnlockProgress unlocks{};
    float placeholder_fuel = 100.0f; // Placeholder para compatibilidade (era jetpack_fuel)

    // Camera 3D settings (Version 4+)
    float cam_distance = g_camera_cfg.spawn_distance, cam_yaw = g_camera_cfg.spawn_yaw, cam_pitch = g_camera_cfg.spawn_pitch, cam_sensitivity = 0.25f;
    float player_rotation = 180.0f;

    if (version == 1) {
        constexpr uint32_t kV1InvCount = (uint32_t)Block::Iron + 1;
        for (uint32_t i = 0; i < kV1InvCount; ++i) {
            int32_t c = 0;
            f.read((char*)&c, sizeof(c));
            if (i < (uint32_t)kBlockTypeCount) inv[(size_t)i] = (int)c;
        }
    // ATENCAO: o limite superior era 5 fixo, o que fazia toda save v6+ (ou seja, TODAS - save_game
    // escreve kSaveVersion = 10) cair no "else { return false; }" la embaixo. Este bloco le o
    // cabecalho COMUM a todas as versoes >= 2 (inventario, recursos, base, unlocks, camera) na
    // mesma ordem em que save_game() escreve; os campos que so existem a partir da v6 sao lidos
    // pelos blocos "if (version >= 6..10)" depois do mundo, mais abaixo.
    } else if (version >= 2 && version <= kSaveVersion) {
        uint32_t inv_count = 0;
        f.read((char*)&inv_count, sizeof(inv_count));
        if (!f || inv_count > 4096) return false;
        for (uint32_t i = 0; i < inv_count; ++i) {
            int32_t c = 0;
            f.read((char*)&c, sizeof(c));
            if (i < (uint32_t)kBlockTypeCount) inv[(size_t)i] = (int)c;
        }
        f.read((char*)&energy, sizeof(energy));
        f.read((char*)&water_res, sizeof(water_res));
        f.read((char*)&oxygen, sizeof(oxygen));
        f.read((char*)&day_time, sizeof(day_time));

        if (version >= 3) {
            // Version 3 additions
            f.read((char*)&base_x, sizeof(base_x));
            f.read((char*)&base_y, sizeof(base_y));
            f.read((char*)&food, sizeof(food));
            f.read((char*)&temperature, sizeof(temperature));
            f.read((char*)&co2_level, sizeof(co2_level));
            f.read((char*)&atmosphere, sizeof(atmosphere));
            f.read((char*)&terraform, sizeof(terraform));
            f.read((char*)&phase, sizeof(phase));

            // Unlocks
            f.read((char*)&unlocks.total_stone, sizeof(unlocks.total_stone));
            f.read((char*)&unlocks.total_iron, sizeof(unlocks.total_iron));
            f.read((char*)&unlocks.total_coal, sizeof(unlocks.total_coal));
            f.read((char*)&unlocks.total_copper, sizeof(unlocks.total_copper));
            f.read((char*)&unlocks.total_wood, sizeof(unlocks.total_wood));
            uint8_t unlocks_flags = 0;
            f.read((char*)&unlocks_flags, sizeof(unlocks_flags));
            unlocks.solar_unlocked = (unlocks_flags & 1) != 0;
            unlocks.water_extractor_unlocked = (unlocks_flags & 2) != 0;
            unlocks.o2_generator_unlocked = (unlocks_flags & 4) != 0;
            unlocks.greenhouse_unlocked = (unlocks_flags & 8) != 0;
            unlocks.co2_factory_unlocked = (unlocks_flags & 16) != 0;
            unlocks.habitat_unlocked = (unlocks_flags & 32) != 0;
            unlocks.terraformer_unlocked = (unlocks_flags & 64) != 0;

            if (version == 3) {
                f.read((char*)&placeholder_fuel, sizeof(placeholder_fuel)); // Compatibilidade v3
            }
        }

        // Version 4: Camera 3D settings
        if (version >= 4) {
            f.read((char*)&cam_distance, sizeof(cam_distance));
            f.read((char*)&cam_yaw, sizeof(cam_yaw));
            f.read((char*)&cam_pitch, sizeof(cam_pitch));
            f.read((char*)&cam_sensitivity, sizeof(cam_sensitivity));
            f.read((char*)&player_rotation, sizeof(player_rotation));
        }
    } else {
        return false;
    }
    if (!f) return false;

    World* nw = new World((int)w, (int)h, (unsigned)seed);
    size_t tile_count = (size_t)w * (size_t)h;
    nw->tiles.assign(tile_count, Block::Air);
    nw->ground.assign(tile_count, Block::Dirt);
    nw->heightmap.assign(tile_count, 0);

    if (version >= 5) {
        // v5+: ground layer + heightmap + top layer
        std::vector<uint8_t> raw_ground(tile_count, 0);
        std::vector<int16_t> raw_h(tile_count, 0);
        std::vector<uint8_t> raw_tiles(tile_count, 0);

        f.read((char*)raw_ground.data(), (std::streamsize)raw_ground.size());
        f.read((char*)raw_h.data(), (std::streamsize)(raw_h.size() * sizeof(int16_t)));
        f.read((char*)raw_tiles.data(), (std::streamsize)raw_tiles.size());
        if (!f) {
            delete nw;
            return false;
        }

        for (size_t i = 0; i < tile_count; ++i) {
            uint8_t gv = raw_ground[i];
            uint8_t tv = raw_tiles[i];

            nw->ground[i] = (gv < (uint8_t)kBlockTypeCount) ? (Block)gv : Block::Dirt;
            nw->tiles[i] = (tv < (uint8_t)kBlockTypeCount) ? (Block)tv : Block::Air;

            int16_t hh = raw_h[i];
            nw->heightmap[i] = (int16_t)std::clamp((int)hh, 0, 256);
        }
    } else {
        // v1..v4: apenas tiles (mundo era "plano").
        std::vector<uint8_t> raw(tile_count, 0);
        f.read((char*)raw.data(), (std::streamsize)raw.size());
        if (!f) {
            delete nw;
            return false;
        }
        for (size_t i = 0; i < tile_count; ++i) {
            uint8_t v = raw[i];
            nw->tiles[i] = (v < (uint8_t)kBlockTypeCount) ? (Block)v : Block::Air;
            // Melhor esforco: manter solo coerente com tiles walkable; objetos ficam no top layer.
            nw->ground[i] = (nw->tiles[i] != Block::Air && is_ground_like(nw->tiles[i])) ? nw->tiles[i] : Block::Dirt;
            nw->heightmap[i] = 0;
        }
    }
    nw->rebuild_surface_cache();

    delete g_world;
    g_world = nw;
    g_player.pos = pos;
    g_player.vel = vel;
    g_player.vel_y = 0.0f;
    {
        int tx = world_to_tile(g_player.pos.x);
        int tz = world_to_tile(g_player.pos.y);
        if (g_world->in_bounds(tx, tz)) {
            g_player.pos_y = surface_height_at(*g_world, tx, tz);
        } else {
            g_player.pos_y = 0.0f;
        }
        g_player.ground_height = g_player.pos_y;
        g_player.on_ground = true;
        g_player.can_jump = true;
    }
    g_player.hp = std::clamp((int)hp, 0, 100);
    g_selected = ((int)sel >= 0 && (int)sel < kBlockTypeCount) ? (Block)sel : Block::Dirt;
    g_inventory = inv;
    g_particles.clear();
    g_shooting_stars.clear();
    g_drops.clear();
    clear_falling_meteors(); // ver comentario da declaracao no topo deste arquivo

    g_energy = std::clamp(energy, 0.0f, kEnergyMax);
    g_water_res = std::clamp(water_res, 0.0f, 100.0f);
    g_oxygen = std::clamp(oxygen, 0.0f, 100.0f);
    g_day_time = std::fmax(0.0f, day_time);

    // Version 3 data
    g_base_x = base_x;
    g_base_y = base_y;
    g_food = std::clamp(food, 0.0f, 100.0f);
    g_temperature = std::clamp(temperature, -100.0f, 100.0f);
    g_co2_level = std::clamp(co2_level, 0.0f, 100.0f);
    g_atmosphere = std::clamp(atmosphere, 0.0f, 100.0f);
    g_terraform = std::clamp(terraform, 0.0f, 100.0f);
    g_phase = (phase < 5) ? (TerraPhase)phase : TerraPhase::Frozen;
    g_unlocks = unlocks;

    // Camera 3D settings (Version 4)
    g_camera.distance = std::clamp(cam_distance, g_camera.min_distance, g_camera.max_distance);
    g_camera.effective_distance = g_camera.distance;
    g_camera.yaw = cam_yaw;
    g_camera.pitch = std::clamp(cam_pitch, g_camera.min_pitch, g_camera.max_pitch);
    g_camera.sensitivity = std::clamp(cam_sensitivity, 0.05f, 1.0f);
    g_player.rotation = player_rotation;
    g_player.target_rotation = player_rotation;
    reset_camera_near_player(false);

    g_cam_pos = g_player.pos;
    reset_player_physics_runtime(true);
    g_surface_dirty = true;
    g_victory = false;
    g_show_build_menu = false;
    g_suit_integrity = 100.0f; // default pra saves pre-v9; sobrescrito abaixo se version>=9
    rebuild_modules_from_world();
    // g_build_slots nunca e' salvo - reconstruir a partir dos tiles BuildSlot do mundo, senao
    // depois de carregar o menu de construcao nao encontra nenhum slot (ou usa os da partida
    // anterior) e cria slots soltos perto da base. Ver modules_building.h.
    rebuild_build_slots_from_world();

    // Version 6: Carregar fog of war e waypoints
    if (version >= 6) {
        // Fog of war
        uint32_t explored_size = 0;
        f.read((char*)&explored_size, sizeof(explored_size));
        if (f && explored_size > 0 && explored_size == (uint32_t)(g_world->w * g_world->h)) {
            g_minimap.explored.resize(explored_size);
            f.read((char*)g_minimap.explored.data(), (std::streamsize)explored_size);
        } else {
            // Inicializar fog of war se nao carregou corretamente
            g_minimap.explored.clear();
            g_minimap.explored.resize((size_t)g_world->w * (size_t)g_world->h, 0);
        }

        // Waypoints
        uint32_t waypoint_count = 0;
        f.read((char*)&waypoint_count, sizeof(waypoint_count));
        g_minimap.waypoints.clear();
        if (f && waypoint_count > 0 && waypoint_count <= 100) {
            for (uint32_t i = 0; i < waypoint_count; ++i) {
                MapWaypoint wp;
                f.read((char*)&wp.x, sizeof(wp.x));
                f.read((char*)&wp.y, sizeof(wp.y));
                f.read((char*)&wp.r, sizeof(wp.r));
                f.read((char*)&wp.g, sizeof(wp.g));
                f.read((char*)&wp.b, sizeof(wp.b));
                uint8_t visible = 0;
                f.read((char*)&visible, sizeof(visible));
                wp.visible = (visible != 0);
                uint32_t label_len = 0;
                f.read((char*)&label_len, sizeof(label_len));
                if (label_len > 0 && label_len < 256) {
                    wp.label.resize(label_len);
                    f.read(&wp.label[0], (std::streamsize)label_len);
                }
                g_minimap.waypoints.push_back(wp);
            }
        }
    } else {
        // Versao antiga - inicializar fog of war e waypoints
        g_minimap.explored.clear();
        g_minimap.explored.resize((size_t)g_world->w * (size_t)g_world->h, 0);
        g_minimap.waypoints.clear();

        // Revelar area ao redor da base
        int reveal_radius = 20;
        for (int dy = -reveal_radius; dy <= reveal_radius; ++dy) {
            for (int dx = -reveal_radius; dx <= reveal_radius; ++dx) {
                int x = g_base_x + dx;
                int y = g_base_y + dy;
                if (g_world->in_bounds(x, y)) {
                    float dist = std::sqrt((float)(dx * dx + dy * dy));
                    if (dist <= (float)reveal_radius) {
                        g_minimap.explored[(size_t)y * (size_t)g_world->w + (size_t)x] = 255;
                    }
                }
            }
        }
    }

    g_minimap.dirty_full = true;
    g_minimap.world_map_open = false;

    // Version 7: objetivos do jogador. Saves antigos (sem essa secao) recomecam a trilha
    // do zero - nao ha como reconstruir retroativamente "quais marcos ja tinham sido
    // atingidos", entao um reset limpo (current=0, nada construido ainda) e o fallback
    // mais honesto, ao inves de tentar adivinhar.
    if (version >= 7) {
        uint32_t obj_current = 0;
        f.read((char*)&obj_current, sizeof(obj_current));
        uint32_t ever_built_count = 0;
        f.read((char*)&ever_built_count, sizeof(ever_built_count));
        bool ever_built_arr[kBlockTypeCount] = {};
        int n = std::min((int)ever_built_count, (int)kBlockTypeCount);
        for (int i = 0; i < n; ++i) {
            uint8_t v = 0;
            f.read((char*)&v, sizeof(v));
            ever_built_arr[i] = (v != 0);
        }
        // Skip over any extra entries beyond kBlockTypeCount (forward-compat: a future
        // build with more block types would write more than we can read here).
        for (uint32_t i = (uint32_t)n; i < ever_built_count; ++i) {
            uint8_t v = 0;
            f.read((char*)&v, sizeof(v));
        }
        if (f) {
            objectives_load_state((int)obj_current, ever_built_arr, n);
        } else {
            reset_objectives();
        }
    } else {
        reset_objectives();
    }
    if (objectives_all_complete()) {
        g_victory = true;
    }

    // Version 8: pilhas de blocos construidos (empilhamento). Saves antigos (sem essa
    // secao) ja tem stack_height/stack_blocks zerados pelo construtor de World - nao
    // precisa de reset explicito, so pular esta leitura.
    if (version >= 8) {
        uint32_t stack_entry_count = 0;
        f.read((char*)&stack_entry_count, sizeof(stack_entry_count));
        if (f && stack_entry_count <= tile_count) {
            for (uint32_t i = 0; i < stack_entry_count; ++i) {
                uint32_t col = 0;
                uint8_t sh = 0;
                f.read((char*)&col, sizeof(col));
                f.read((char*)&sh, sizeof(sh));
                if (f && col < tile_count && sh <= World::kMaxStackExtra) {
                    g_world->stack_height[col] = sh;
                    f.read((char*)&g_world->stack_blocks[(size_t)col * (size_t)World::kMaxStackExtra], (std::streamsize)sh);
                } else {
                    break; // malformado - para aqui, deixa o resto das colunas vazio
                }
            }
        }
    }

    // Version 9: integridade do traje + modulos aprimorados (tecla R). rebuild_modules_
    // from_world() (acima) ja recriou g_modules do zero com upgraded=false em todos - so
    // marca de volta os que a save diz que estavam aprimorados.
    if (version >= 9) {
        float suit_integrity = 100.0f;
        f.read((char*)&suit_integrity, sizeof(suit_integrity));
        if (f) g_suit_integrity = std::clamp(suit_integrity, 0.0f, 100.0f);

        uint32_t upgraded_count = 0;
        f.read((char*)&upgraded_count, sizeof(upgraded_count));
        if (f && upgraded_count <= (uint32_t)g_modules.size() + 16) {
            for (uint32_t i = 0; i < upgraded_count; ++i) {
                int32_t ux = 0, uy = 0;
                f.read((char*)&ux, sizeof(ux));
                f.read((char*)&uy, sizeof(uy));
                if (!f) break;
                for (Module& m : g_modules) {
                    if (m.x == ux && m.y == uy) { m.upgraded = true; break; }
                }
            }
        }
    }

    // Version 10: novos totais de desbloqueio. Saves antigos (<10) ficam em 0 (unlocks{}
    // ja zera por default) - equivale a "nunca coletou ainda", mesmo fallback honesto da v7.
    if (version >= 10) {
        f.read((char*)&g_unlocks.total_ice, sizeof(g_unlocks.total_ice));
        f.read((char*)&g_unlocks.total_crystal, sizeof(g_unlocks.total_crystal));
        f.read((char*)&g_unlocks.total_metal, sizeof(g_unlocks.total_metal));
        f.read((char*)&g_unlocks.total_organic, sizeof(g_unlocks.total_organic));
        f.read((char*)&g_unlocks.total_components, sizeof(g_unlocks.total_components));
    }

    // Version 11: abates e descoberta de destroco. Saves <11 ficam em 0/false - "ainda nao cacou,
    // ainda nao explorou", mesmo fallback honesto da v7. As duas funcoes tambem limpam g_creatures
    // (criaturas sao efemeras, nunca salvas - padrao de g_meteors).
    if (version >= 11) {
        uint32_t creature_kills_v = 0;
        f.read((char*)&creature_kills_v, sizeof(creature_kills_v));
        uint8_t poi_found_v = 0;
        f.read((char*)&poi_found_v, sizeof(poi_found_v));
        creature_stats_load((int)creature_kills_v);
        poi_discovery_load(poi_found_v != 0);
    } else {
        creature_stats_load(0);
        poi_discovery_load(false);
    }

    return true;
}
