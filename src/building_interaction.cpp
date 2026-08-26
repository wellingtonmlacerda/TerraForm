#include "building_interaction.h"

#include "raylib_platform.h"
#include "math_core.h"
#include "noise.h"              // lerp
#include "blocks.h"
#include "textures.h"           // block_color
#include "config_types.h"       // MiningConfig/PlayerVisualConfig (types of the extern globals below)
#include "world.h"
#include "camera.h"
#include "game_state.h"
#include "player_physics.h"
#include "items_particles.h"
#include "ui_hud.h"             // g_hud_pointer_over_button
#include "modules_building.h"
#include "inventory_crafting.h"
#include "render_primitives.h"
#include "font.h"
#include "objectives.h"         // notify_module_built
#include "creatures.h"          // try_fire_laser_pistol

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

// ============= Building Interaction (Build Menu / Mining / Placement) =============
// Extracted verbatim from main.cpp - see building_interaction.h for the full
// extraction-stage description of the two areas covered (build-menu render, build-menu
// input + mining/placement raycast).

// Globals still defined in main.cpp (owner stays main.cpp - not part of this extraction),
// same "own local extern declaration" pattern used by every other extracted .cpp file
// (e.g. g_base_energy/etc. in modules_building.cpp, g_settings_selection/etc. in
// ui_menu.cpp).
//
// g_show_build_menu/g_build_menu_selection/g_base_x/g_base_y/g_has_target/g_target_x/
// g_target_y/g_target_in_range/g_terraform/g_surface_dirty/g_mining_cfg/
// g_player_visual_cfg were already non-static in main.cpp (needed by other extracted
// modules already) - this file just adds its own extern declarations for them too.
//
// g_has_place_target/g_place_x/g_place_y/g_place_in_range/g_place_cd/g_prev_lmb/
// g_prev_rmb/g_prev_e/g_mine_block_x/g_mine_block_y/g_mine_progress/g_mine_hits/
// g_mine_hit_timer lost "static" in main.cpp for this stage: they used to be touched only
// by update_game()'s own mining/placement code (now moved here), so this is the first
// cross-translation-unit use of each - same reasoning as every other "lost static" comment
// in this codebase's extraction stages.
extern bool g_show_build_menu;
extern int g_build_menu_selection;
extern int g_base_x;
extern int g_base_y;
extern float g_base_energy;
extern float g_base_water;
extern float g_base_oxygen;
extern float g_base_food;
extern float g_base_integrity;
extern bool g_has_target;
extern int g_target_x;
extern int g_target_y;
extern bool g_target_in_range;
extern bool g_has_place_target;
extern int g_place_x;
extern int g_place_y;
extern bool g_place_in_range;
extern float g_place_cd;
extern bool g_prev_lmb;
extern bool g_prev_rmb;
extern bool g_prev_e;
extern int g_mine_block_x;
extern int g_mine_block_y;
extern float g_mine_progress;
extern int g_mine_hits;
extern float g_mine_hit_timer;
extern float g_terraform;
extern bool g_surface_dirty;
extern MiningConfig g_mining_cfg;
extern PlayerVisualConfig g_player_visual_cfg;

// key_down() stays defined in main.cpp (input polling - out of scope for this stage), but
// both functions below call it, so it lost the "static" it had in main.cpp. Plain forward
// declaration here, same pattern as add_alert()/update_shooting_stars() in
// modules_building.cpp.
bool key_down(int vk);

// kBaseEnergyMax/kBaseWaterMax/kBaseOxygenMax/kBaseFoodMax/kBaseIntegrityMax are
// compile-time literals (not mutable state) defined in main.cpp; kept here as this file's
// own copy rather than shared via extern - same pattern already used by
// modules_building.cpp/ui_hud.cpp/minimap.cpp/sky.cpp/lighting.cpp.
static constexpr float kBaseEnergyMax = 500.0f;
static constexpr float kBaseWaterMax = 200.0f;
static constexpr float kBaseOxygenMax = 200.0f;
static constexpr float kBaseFoodMax = 200.0f;
static constexpr float kBaseIntegrityMax = 100.0f;

// ============= Named raycast/placement helpers (formerly local [&]-capturing lambdas) =====
// The five functions below used to be local lambdas inside update_game()'s mining/placement
// block, capturing their enclosing scope by reference ([&]). This is the highest-value
// mechanical change of this extraction stage: converting them to named, explicit-parameter
// functions is what a later redesign of the construction/placement system builds on.

// Formerly "auto placeable_tile = [&](Block b) -> bool { ... }" - body only ever touched
// its parameter (no captures actually used), so it converts directly to a plain function.
static bool placeable_tile(Block b) {
    if (is_base_structure(b)) return false;
    if (is_module(b)) return false;
    // Colocar um bloco solido na agua desloca/preenche ela (estilo Minecraft) - a coluna vira
    // o bloco colocado, sem "fundacao flutuante" porque nao ha mais agua ali depois.
    if (b == Block::Air || b == Block::Water) return true;
    return !is_solid(b); // walkable pode ser substituido
}

// Formerly "auto blocks_raycast = [&](Block b) -> bool { ... }" - same treatment, also
// purely a function of Block.
static bool blocks_raycast(Block b) {
    if (b == Block::Air) return false;
    if (is_ground_like(b)) return true; // permite selecionar/minerar o chao corretamente
    if (b == Block::Water) return true;
    if (b == Block::Leaves) return true;
    if (is_base_structure(b)) return true;
    if (is_module(b)) return true;
    return is_solid(b);
}

// Formerly "auto ray_aabb_hit = [&](const Vec3& bmin, const Vec3& bmax, float& out_t) -> bool
// { ... }" - captured ray_o/ray_d/ray_max from the enclosing scope; those are now explicit
// parameters. The nested "axis_test" lambda only captures locals of this function
// (tmin/tmax/eps by reference) - it doesn't cross the function boundary, so it stays a
// nested lambda exactly as before.
static bool ray_aabb_hit(Vec3 ray_o, Vec3 ray_d, float ray_max, const Vec3& bmin, const Vec3& bmax, float& out_t) {
    float tmin = 0.0f;
    float tmax = ray_max;
    const float eps = 1e-6f;
    auto axis_test = [&](float ro, float rd, float mn, float mx) -> bool {
        if (std::fabs(rd) < eps) {
            return ro >= mn && ro <= mx;
        }
        float inv = 1.0f / rd;
        float t1 = (mn - ro) * inv;
        float t2 = (mx - ro) * inv;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        return tmin <= tmax;
    };

    if (!axis_test(ray_o.x, ray_d.x, bmin.x, bmax.x)) return false;
    if (!axis_test(ray_o.y, ray_d.y, bmin.y, bmax.y)) return false;
    if (!axis_test(ray_o.z, ray_d.z, bmin.z, bmax.z)) return false;

    out_t = (tmin >= 0.0f) ? tmin : tmax;
    return out_t >= 0.0f && out_t <= ray_max;
}

// Formerly "auto ray_hits_tile = [&](int tx, int tz, Block b, float& out_t) -> bool { ... }"
// - captured g_world (a global, so not really a capture) plus ray_o/ray_d/ray_max (via the
// call to ray_aabb_hit below), which are now explicit parameters forwarded to the named
// ray_aabb_hit() above.
static bool ray_hits_tile(const World& world, Vec3 ray_o, Vec3 ray_d, float ray_max, int tx, int tz, Block b, float& out_t) {
    float base_y = (float)world.height_at(tx, tz) * kHeightScale;
    Vec3 bmin = {tile_min(tx), base_y - 0.05f, tile_min(tz)};
    Vec3 bmax = {tile_max(tx), base_y + 1.05f, tile_max(tz)};

    int stack_h = world.stack_height_at(tx, tz);
    if (stack_h > 0) {
        // Coluna tem blocos empilhados pelo jogador (torre/parede): eles sempre ficam por
        // cima de tudo, entao essa unica AABB combinada (terreno ate o topo da pilha) tem
        // prioridade sobre os branches de agua/folha/chao-like abaixo - mirar/minerar
        // sempre acerta o bloco mais alto da pilha, nunca o que esta embaixo dela.
        bmax.y = base_y + 1.05f + (float)stack_h * 1.0f;
    } else if (b == Block::Water) {
        bmin.y = base_y - 0.30f;
        bmax.y = base_y + 0.08f;
    } else if (b == Block::Leaves) {
        bmin.y = base_y + 0.45f;
        bmax.y = base_y + 0.82f;
    } else if (is_ground_like(b)) {
        // Coluna baixa para permitir selecionar topo e paredes de buracos.
        bmin.y = base_y - 1.05f;
        bmax.y = base_y + 0.10f;
    }
    return ray_aabb_hit(ray_o, ray_d, ray_max, bmin, bmax, out_t);
}

// Formerly "auto placeable_tile_for_place = [&](Block b) -> bool { ... }" - identical body
// to placeable_tile() above (pre-existing duplication in the original code, not something
// this mechanical stage fixes), so it stays a separate named function with the same name it
// had as a lambda.
// Empilhamento: quando o alvo mirado ja e solido mas a coluna ainda tem espaco na pilha
// (World::kMaxStackExtra), a colocacao empilha em cima em vez de procurar uma coluna vazia
// adjacente. So usado dentro deste arquivo (mira e acao de colocar estao na mesma unidade
// de traducao), por isso fica static local em vez de seguir o padrao extern/main.cpp usado
// pelos outros globais de mira/colocacao acima.
static bool g_place_is_stack = false;

static bool placeable_tile_for_place(Block b) {
    if (is_base_structure(b)) return false;
    if (is_module(b)) return false;
    if (b == Block::Air || b == Block::Water) return true;
    return !is_solid(b); // chao/walkable pode ser substituido
}

// ============= Build menu rendering =============
// Extracted verbatim from render_world() (original lines ~1735-1933): the
// "if (g_show_build_menu && g_state == GameState::Playing) { ... }" guard now lives inside
// this function (same pattern as render_menus() checking g_state internally) instead of
// wrapping a single call at the render_world() call site.
void render_build_menu(int win_w, int win_h) {
    if (!(g_show_build_menu && g_state == GameState::Playing)) return;

    float menu_w = 850.0f;
    float menu_h = 650.0f;
    float menu_x = win_w * 0.5f - menu_w * 0.5f;
    float menu_y = win_h * 0.5f - menu_h * 0.5f;

    // Background
    render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.0f, 0.0f, 0.0f, 0.70f);
    render_quad(menu_x, menu_y, menu_w, menu_h, 0.05f, 0.07f, 0.10f, 0.98f);

    // Border
    render_quad(menu_x, menu_y, menu_w, 3.0f, 0.30f, 0.55f, 0.85f, 1.0f);
    render_quad(menu_x, menu_y + menu_h - 3.0f, menu_w, 3.0f, 0.30f, 0.55f, 0.85f, 1.0f);
    render_quad(menu_x, menu_y, 3.0f, menu_h, 0.30f, 0.55f, 0.85f, 1.0f);
    render_quad(menu_x + menu_w - 3.0f, menu_y, 3.0f, menu_h, 0.30f, 0.55f, 0.85f, 1.0f);

    // Title
    std::string title = "MENU DE CONSTRUCAO";
    draw_text(menu_x + menu_w * 0.5f - estimate_text_w_px(title) * 0.5f, menu_y + 25.0f, title, 0.95f, 0.95f, 0.95f, 1.0f);
    draw_text(menu_x + menu_w * 0.5f - 150.0f, menu_y + 45.0f, "Tab/B: Fechar  |  W/S: Selecionar  |  Enter: Construir", 0.55f, 0.60f, 0.70f, 0.85f);

    // Module types available
    Block module_types[] = {
        Block::SolarPanel,
        Block::EnergyGenerator,
        Block::OxygenGenerator,
        Block::WaterExtractor,
        Block::Greenhouse,
        Block::Workshop,
        Block::CO2Factory,
        Block::Habitat,
        Block::TerraformerBeacon,
    };
    const int module_count = 9;

    // Clamp selection
    if (g_build_menu_selection < 0) g_build_menu_selection = 0;
    if (g_build_menu_selection >= module_count) g_build_menu_selection = module_count - 1;

    float list_x = menu_x + 15.0f;
    float list_y = menu_y + 65.0f;
    float row_h = 58.0f;
    float list_w = menu_w - 250.0f;

    for (int i = 0; i < module_count; ++i) {
        Block mtype = module_types[i];
        ModuleStats stats = get_module_stats(mtype);
        CraftCost cost = get_module_cost(mtype);
        bool unlocked = is_unlocked(mtype);
        bool affordable = can_afford(cost);
        bool selected = (i == g_build_menu_selection);

        // Check if under construction
        bool building = false;
        float build_progress = 0.0f;
        for (const auto& job : g_construction_queue) {
            if (job.active && job.module_type == mtype) {
                building = true;
                build_progress = 1.0f - (job.time_remaining / job.total_time);
                break;
            }
        }

        // Count how many of this module we have
        int count = 0;
        for (const auto& mod : g_modules) {
            if (mod.type == mtype) count++;
        }

        // Determine status - unlock gating checked before affordability, since a locked
        // module that's technically affordable would otherwise misleadingly read
        // "DISPONIVEL" here while still being rejected at actual placement time
        // (building_interaction.cpp's is_unlocked() check further up this same file).
        const char* status_str;
        float stat_r, stat_g, stat_b;
        bool buildable = unlocked && affordable;
        if (building) {
            status_str = "CONSTRUINDO";
            stat_r = 0.95f; stat_g = 0.75f; stat_b = 0.20f;
        } else if (!unlocked) {
            status_str = "BLOQUEADO";
            stat_r = 0.80f; stat_g = 0.40f; stat_b = 0.35f;
        } else if (affordable) {
            status_str = "DISPONIVEL";
            stat_r = 0.30f; stat_g = 0.90f; stat_b = 0.40f;
        } else {
            status_str = "SEM RECURSOS";
            stat_r = 0.85f; stat_g = 0.60f; stat_b = 0.25f;
        }

        // Row background
        float bg_alpha = selected ? 0.40f : 0.15f;
        float bg_r = selected ? 0.12f : 0.08f;
        float bg_g = selected ? 0.22f : 0.10f;
        float bg_b = selected ? 0.38f : 0.15f;
        render_quad(list_x, list_y, list_w, row_h - 3.0f, bg_r, bg_g, bg_b, bg_alpha);

        // Selection indicator
        if (selected) {
            render_quad(list_x, list_y, 4.0f, row_h - 3.0f, 0.35f, 0.75f, 0.95f, 1.0f);
        }

        // Build progress bar if building
        if (building) {
            render_quad(list_x + 4.0f, list_y + row_h - 8.0f, (list_w - 8.0f) * build_progress, 4.0f, 0.30f, 0.80f, 0.50f, 0.90f);
        }

        // Module name and count
        float name_r = buildable ? 0.95f : 0.60f;
        float name_g = buildable ? 0.95f : 0.60f;
        float name_b = buildable ? 0.95f : 0.65f;
        std::string name_str = std::string(stats.name);
        if (count > 0) name_str += " [" + std::to_string(count) + " ativo]";
        draw_text(list_x + 12.0f, list_y + 16.0f, name_str, name_r, name_g, name_b, 1.0f);

        // Description
        draw_text(list_x + 12.0f, list_y + 32.0f, stats.description, 0.55f, 0.60f, 0.70f, 0.80f);

        // Production/Consumption info
        std::string prod_str;
        if (stats.energy_production > 0.0f) prod_str += "+" + std::to_string((int)stats.energy_production) + " Energia/min ";
        if (stats.oxygen_production > 0.0f) prod_str += "+" + std::to_string((int)(stats.oxygen_production*10)/10.0f).substr(0,3) + " O2/min ";
        if (stats.water_production > 0.0f) prod_str += "+" + std::to_string((int)(stats.water_production*10)/10.0f).substr(0,3) + " Agua/min ";
        if (stats.food_production > 0.0f) prod_str += "+" + std::to_string((int)(stats.food_production*10)/10.0f).substr(0,3) + " Comida/min ";
        if (stats.integrity_bonus > 0.0f) prod_str += "+" + std::to_string((int)stats.integrity_bonus) + " Reparo/min ";
        if (prod_str.empty()) prod_str = "Terraformacao";

        std::string cons_str;
        if (stats.energy_consumption > 0.0f) cons_str = "-" + std::to_string((int)(stats.energy_consumption*10)/10.0f).substr(0,3) + " Energia/min";

        draw_text(list_x + 220.0f, list_y + 16.0f, prod_str, 0.35f, 0.80f, 0.45f, 0.85f);
        if (!cons_str.empty()) {
            draw_text(list_x + 220.0f, list_y + 32.0f, cons_str, 0.85f, 0.55f, 0.35f, 0.80f);
        }

        // Status
        draw_text(list_x + list_w - 95.0f, list_y + 16.0f, status_str, stat_r, stat_g, stat_b, 0.95f);

        // Cost - or, while locked, the unlock requirement progress instead (matches the
        // hotbar's existing unlock_progress_string() display, see ui_hud.cpp).
        std::string cost_str = unlocked ? module_cost_string(cost) : ("Desbloqueio: " + unlock_progress_string(mtype));
        float cost_r = buildable ? 0.50f : 0.75f;
        float cost_g = buildable ? 0.80f : 0.50f;
        float cost_b = buildable ? 0.55f : 0.45f;
        draw_text(list_x + 12.0f, list_y + 46.0f, cost_str, cost_r, cost_g, cost_b, 0.75f);

        // Construction time
        std::string time_str = "Tempo: " + std::to_string((int)stats.construction_time) + "s";
        draw_text(list_x + list_w - 95.0f, list_y + 32.0f, time_str, 0.60f, 0.65f, 0.70f, 0.75f);

        list_y += row_h;
    }

    // === RIGHT SIDE: BASE STATUS ===
    float status_x = menu_x + menu_w - 225.0f;
    float status_y = menu_y + 65.0f;

    render_quad(status_x - 5.0f, status_y - 5.0f, 220.0f, 250.0f, 0.08f, 0.10f, 0.14f, 0.90f);
    draw_text(status_x + 55.0f, status_y + 12.0f, "STATUS DA BASE", 0.85f, 0.90f, 0.95f, 0.95f);
    status_y += 30.0f;

    // Base resources with detailed bars
    auto draw_status_bar = [&](const char* label, float value, float max_val, float r, float g, float b) {
        float pct = std::clamp(value / max_val, 0.0f, 1.0f);
        render_quad(status_x, status_y, 200.0f, 18.0f, 0.12f, 0.12f, 0.18f, 0.85f);
        render_quad(status_x + 1.0f, status_y + 1.0f, 198.0f * pct, 16.0f, r, g, b, 0.90f);
        std::string txt = std::string(label) + ": " + std::to_string((int)value) + "/" + std::to_string((int)max_val);
        draw_text(status_x + 5.0f, status_y + 13.0f, txt, 0.95f, 0.95f, 0.95f, 0.98f);
        status_y += 24.0f;
    };

    draw_status_bar("Energia", g_base_energy, kBaseEnergyMax, 0.95f, 0.80f, 0.20f);
    draw_status_bar("Agua", g_base_water, kBaseWaterMax, 0.25f, 0.60f, 0.95f);
    draw_status_bar("Oxigenio", g_base_oxygen, kBaseOxygenMax, 0.25f, 0.90f, 0.50f);
    draw_status_bar("Comida", g_base_food, kBaseFoodMax, 0.85f, 0.60f, 0.25f);

    // Integrity bar
    float int_r = g_base_integrity > 50.0f ? 0.30f : (g_base_integrity > 25.0f ? 0.90f : 0.95f);
    float int_g = g_base_integrity > 50.0f ? 0.85f : (g_base_integrity > 25.0f ? 0.70f : 0.30f);
    float int_b = g_base_integrity > 50.0f ? 0.40f : 0.20f;
    draw_status_bar("Integridade", g_base_integrity, kBaseIntegrityMax, int_r, int_g, int_b);

    // Consumption info
    status_y += 10.0f;
    draw_text(status_x, status_y, "CONSUMO CONSTANTE:", 0.70f, 0.75f, 0.85f, 0.80f);
    status_y += 18.0f;
    draw_text(status_x, status_y, "-1 O2/min  -2 Energia/min  -1 Agua/min", 0.85f, 0.55f, 0.45f, 0.75f);

    // === BOTTOM: INVENTORY ===
    float bottom_y = menu_y + menu_h - 90.0f;
    render_quad(menu_x + 10.0f, bottom_y, menu_w - 20.0f, 80.0f, 0.08f, 0.10f, 0.14f, 0.90f);
    draw_text(menu_x + 20.0f, bottom_y + 15.0f, "SEU INVENTARIO:", 0.80f, 0.85f, 0.95f, 0.92f);

    std::string res_line1 =
        "Pedra: " + std::to_string(g_inventory[(int)Block::Stone]) +
        "   Ferro: " + std::to_string(g_inventory[(int)Block::Iron]) +
        "   Cobre: " + std::to_string(g_inventory[(int)Block::Copper]) +
        "   Gelo: " + std::to_string(g_inventory[(int)Block::Ice]);
    std::string res_line2 =
        "Carvao: " + std::to_string(g_inventory[(int)Block::Coal]) +
        "   Cristal: " + std::to_string(g_inventory[(int)Block::Crystal]) +
        "   Metal: " + std::to_string(g_inventory[(int)Block::Metal]) +
        "   Organico: " + std::to_string(g_inventory[(int)Block::Organic]) +
        "   Comp: " + std::to_string(g_inventory[(int)Block::Components]);
    draw_text(menu_x + 20.0f, bottom_y + 38.0f, res_line1, 0.90f, 0.92f, 0.95f, 0.95f);
    draw_text(menu_x + 20.0f, bottom_y + 58.0f, res_line2, 0.90f, 0.92f, 0.95f, 0.95f);
}

// ============= Build menu input (navigation/actions) =============
// Extracted verbatim from update_game() (original lines ~2167-2263): the
// "if (g_show_build_menu) { ...; return; }" block became this function - the guard now
// lives at the top (returning false immediately when the menu isn't open, so update_game()
// falls through to its own Playing-state code), and the unconditional "return;" at the end
// became "return true;" - same "return true consumes the frame" convention as ui_menu.cpp's
// update_menu_input().
bool update_build_menu_input() {
    if (!g_show_build_menu) return false;

    static bool prev_w = false, prev_s = false, prev_enter = false;
    bool w_now = key_down(KEY_W) || key_down(KEY_UP);
    bool s_now = key_down(KEY_S) || key_down(KEY_DOWN);
    bool enter_now = key_down(KEY_ENTER);

    // Module types list (matches render order)
    const Block module_types[] = {
        Block::SolarPanel, Block::EnergyGenerator, Block::OxygenGenerator,
        Block::WaterExtractor, Block::Greenhouse, Block::Workshop,
        Block::CO2Factory, Block::Habitat, Block::TerraformerBeacon
    };
    const int module_count = 9;

    // Navigate up (W ou seta para cima)
    if (w_now && !prev_w) {
        g_build_menu_selection--;
        if (g_build_menu_selection < 0)
            g_build_menu_selection = module_count - 1;
        bounce_hotbar_slot(g_build_menu_selection);  // Feedback visual
    }
    // Navigate down (S ou seta para baixo)
    if (s_now && !prev_s) {
        g_build_menu_selection++;
        if (g_build_menu_selection >= module_count)
            g_build_menu_selection = 0;
        bounce_hotbar_slot(g_build_menu_selection);  // Feedback visual
    }
    // Build action with construction time
    if (enter_now && !prev_enter && g_build_menu_selection >= 0 &&
        g_build_menu_selection < module_count) {

        Block module_type = module_types[g_build_menu_selection];
        CraftCost cost = get_module_cost(module_type);

        // Check if already under construction
        bool already_building = false;
        for (const auto& job : g_construction_queue) {
            if (job.active && job.module_type == module_type) {
                already_building = true;
                break;
            }
        }

        if (already_building) {
            show_error("Ja em construcao!");
        } else if (can_afford(cost)) {
            // Find a free slot for this module (or create at base)
            int slot_index = -1;

            // Try to find an empty slot
            for (int si = 0; si < (int)g_build_slots.size(); ++si) {
                if (g_build_slots[si].assigned_module == Block::Air) {
                    slot_index = si;
                    break;
                }
            }

            // If no slot, create a new one near base
            if (slot_index < 0) {
                // Find empty spot near base
                for (int dx = -30; dx <= 30; ++dx) {
                    int tx = g_base_x + dx;
                    if (tx < 0 || tx >= g_world->w) continue;
                    int ty = g_base_y - 1;
                    Block current = g_world->get(tx, ty);
                    if (current == Block::Air || current == Block::BuildSlot) {
                        BuildSlotInfo new_slot;
                        new_slot.x = tx;
                        new_slot.y = ty;
                        new_slot.assigned_module = Block::Air;
                        new_slot.label = "Auto";
                        g_build_slots.push_back(new_slot);
                        slot_index = (int)g_build_slots.size() - 1;
                        break;
                    }
                }
            }

            if (slot_index >= 0) {
                // Start construction (will take time!)
                start_construction(module_type, slot_index);
                g_build_slots[slot_index].assigned_module = module_type;
            } else {
                show_error("Sem espaco para construir!");
            }
        } else {
            show_error("Recursos insuficientes!");
        }
    }

    prev_w = w_now;
    prev_s = s_now;
    prev_enter = enter_now;
    return true;  // Don't process other inputs while in menu
}

// ============= Mining / placement raycast + actions =============
// Extracted verbatim from update_game() (original lines ~2440-2899): mouse targeting,
// the mining/placement raycast (using the named helpers above instead of local lambdas),
// the mining action (progress/hits/particles/block breaking/drops), item pickup, the
// placement action (RMB), and the particle simulation step.
void update_mining_and_placement(float dt) {
    // Mouse targeting (raylib: mouse position is already client-area-relative, no HWND needed)
    Vector2 cursor = GetMousePosition();

    int win_w = GetScreenWidth();
    int win_h = GetScreenHeight();

    // Atualizar camera antes do targeting, para a mira (+) do centro bater com o raycast.
    update_camera_for_frame();

    // ============= TARGETING 3D (Estilo Minicraft) =============
    // A mira (+) segue o mouse; fazemos raycast a partir da camera na direcao do mouse.
    const float kReach = 4.2f; // alcance de interacao (minerar/colocar)

    g_has_target = false;
    g_target_in_range = false;
    g_has_place_target = false;
    g_place_in_range = false;
    g_place_is_stack = false;
    g_target_drop = -1;

    // Ray da camera (mira) - agora baseado na posicao do mouse
    Vec3 ray_o = g_camera.position;
    Vec3 ray_d = get_mouse_ray_direction(cursor.x, cursor.y, win_w, win_h);
    float ray_max = std::clamp(g_camera.effective_distance + kReach + 3.0f, 8.0f, 55.0f);

    // Pistola de Laser equipada: dispara em vez de minerar/construir - intercepta ANTES de
    // qualquer alvo/g_has_target ser calculado (nao faz sentido mirar um bloco com a arma na
    // mao; g_has_target fica false esse frame, o que ja e' o comportamento certo pro HUD/R-F-G).
    if (g_selected == Block::LaserPistol) {
        try_fire_laser_pistol(ray_o, ray_d, dt);
        // Sem isso, o "return" abaixo pulava update_item_drops() (coleta por proximidade,
        // la' embaixo no fim desta funcao) inteiro - com a arma equipada, os drops no chao
        // simplesmente nunca eram coletados (bug real reportado pelo jogador). Mineracao/
        // construcao/mira de bloco continuam puladas de proposito (nao faz sentido mirar um
        // bloco com a arma na mao) - so' a coleta de item precisa continuar rodando.
        update_item_drops(dt);
        return;
    }

    // Primeiro: tentar mirar um drop (para facilitar coleta visual)
    {
        float best_t = std::numeric_limits<float>::infinity();
        float best_perp2 = 0.0f;
        for (int i = 0; i < (int)g_drops.size(); ++i) {
            const ItemDrop& d = g_drops[(size_t)i];
            Vec3 c = {d.x, d.y, d.z};
            Vec3 rel = vec3_sub(c, ray_o);
            float t = vec3_dot(rel, ray_d);
            if (t < 0.2f || t > ray_max) continue;
            Vec3 closest = vec3_add(ray_o, vec3_scale(ray_d, t));
            Vec3 diff = vec3_sub(c, closest);
            float perp2 = vec3_dot(diff, diff);

            // "hitbox" da mira para o drop (um pouco generoso)
            if (perp2 <= 0.26f * 0.26f) {
                // E so considera se o drop nao estiver muito longe do player (alcance real)
                float dx = d.x - g_player.pos.x;
                float dz = d.z - g_player.pos.y;
                float d2 = dx * dx + dz * dz;
                if (d2 <= (kReach + 1.5f) * (kReach + 1.5f)) {
                    if (t < best_t || (std::fabs(t - best_t) < 0.15f && perp2 < best_perp2)) {
                        best_t = t;
                        best_perp2 = perp2;
                        g_target_drop = i;
                    }
                }
            }
        }
    }

    int last_place_x = -1;
    int last_place_y = -1;

    // Raymarch por tiles + teste preciso de intersecao com AABB do alvo.
    int prev_tx = std::numeric_limits<int>::min();
    int prev_tz = std::numeric_limits<int>::min();
    for (float t = 0.20f; t <= ray_max; t += 0.05f) {
        Vec3 p = vec3_add(ray_o, vec3_scale(ray_d, t));
        int tx = world_to_tile(p.x);
        int tz = world_to_tile(p.z);
        if (tx == prev_tx && tz == prev_tz) continue;
        prev_tx = tx;
        prev_tz = tz;

        if (!g_world->in_bounds(tx, tz)) break;

        Block top_b = g_world->get(tx, tz);
        Block b = (top_b == Block::Air) ? surface_block_at(*g_world, tx, tz) : top_b;

        if (placeable_tile(top_b)) {
            last_place_x = tx;
            last_place_y = tz;
        }

        float hit_t = 0.0f;
        if (!blocks_raycast(b) || !ray_hits_tile(*g_world, ray_o, ray_d, ray_max, tx, tz, b, hit_t)) continue;

        g_target_x = tx;
        g_target_y = tz;
        g_has_target = true;

        float dx = tile_center(g_target_x) - g_player.pos.x;
        float dz = tile_center(g_target_y) - g_player.pos.y;
        float dist = std::sqrt(dx * dx + dz * dz);
        g_target_in_range = (dist <= kReach);

        // Alvo de colocacao: tile atual se substituivel, ou empilhar em cima dele se ja
        // solido mas com espaco na pilha, ou o ultimo substituivel antes do hit.
        bool top_stackable = !placeable_tile(top_b) && g_world->stack_height_at(tx, tz) < World::kMaxStackExtra;
        if (placeable_tile(top_b)) {
            g_place_x = tx;
            g_place_y = tz;
            g_has_place_target = true;
            g_place_in_range = g_target_in_range;
        } else if (top_stackable) {
            g_place_x = tx;
            g_place_y = tz;
            g_has_place_target = true;
            g_place_is_stack = true;
            g_place_in_range = g_target_in_range;
        } else if (last_place_x != -1) {
            g_place_x = last_place_x;
            g_place_y = last_place_y;
            g_has_place_target = true;
            float pdx = tile_center(g_place_x) - g_player.pos.x;
            float pdz = tile_center(g_place_y) - g_player.pos.y;
            g_place_in_range = (std::sqrt(pdx * pdx + pdz * pdz) <= kReach);
        }

        if (!g_onboarding.shown_first_mine && is_mineable(b)) {
            show_tip("Segure clique esquerdo (ou E) para minerar blocos", g_onboarding.shown_first_mine);
        }
        break;
    }

    // Sem hit: mantem selecao apenas no ultimo tile substituivel realmente visto.
    if (!g_has_target && last_place_x != -1) {
        g_target_x = last_place_x;
        g_target_y = last_place_y;
        g_has_target = true;
        float dx = tile_center(g_target_x) - g_player.pos.x;
        float dz = tile_center(g_target_y) - g_player.pos.y;
        g_target_in_range = (std::sqrt(dx * dx + dz * dz) <= kReach);

        g_place_x = g_target_x;
        g_place_y = g_target_y;
        g_has_place_target = true;
        g_place_in_range = g_target_in_range;
    }

    // Fallback: se nao houver tile de colocacao direto, tenta um adjacente do alvo atual.
    if (!g_has_place_target && g_has_target) {
        float best_d2 = std::numeric_limits<float>::infinity();
        int best_x = -1;
        int best_y = -1;
        for (int oz = -1; oz <= 1; ++oz) {
            for (int ox = -1; ox <= 1; ++ox) {
                if (ox == 0 && oz == 0) continue;
                int tx = g_target_x + ox;
                int tz = g_target_y + oz;
                if (!g_world->in_bounds(tx, tz)) continue;
                Block nb = g_world->get(tx, tz);
                if (!placeable_tile(nb)) continue;
                float dx = tile_center(tx) - g_player.pos.x;
                float dz = tile_center(tz) - g_player.pos.y;
                float d2 = dx * dx + dz * dz;
                if (d2 < best_d2) {
                    best_d2 = d2;
                    best_x = tx;
                    best_y = tz;
                }
            }
        }
        if (best_x != -1) {
            g_place_x = best_x;
            g_place_y = best_y;
            g_has_place_target = true;
            g_place_in_range = (best_d2 <= kReach * kReach);
        }
    }

    // Cooldowns (apenas colocacao)
    if (g_place_cd > 0.0f) g_place_cd -= dt;

    // VK_LBUTTON/VK_RBUTTON are not keyboard keys in raylib - key_down()/IsKeyDown() has no
    // equivalent for them (it only worked before because GetAsyncKeyState happens to accept
    // mouse VK codes too). Use IsMouseButtonDown() directly for just these 2 sites.
    // !g_hud_pointer_over_button: clicar num botao redondo da HUD nao pode minerar/construir no mundo,
    // pelo mesmo motivo que nao pode disparar (ver ui_hud.h).
    bool lmb = IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !g_hud_pointer_over_button;
    bool rmb = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);

    bool e_key = key_down(KEY_E);
    g_prev_e = e_key;

    // Mining com progresso (segurar LMB ou E)
    bool mine_input = (lmb || e_key);
    bool has_mine_target = g_has_target && g_target_in_range && g_world->in_bounds(g_target_x, g_target_y);
    int target_stack_h = has_mine_target ? g_world->stack_height_at(g_target_x, g_target_y) : 0;
    Block mine_block;
    if (target_stack_h > 0) {
        // Coluna tem blocos empilhados: sempre minera o topo da pilha primeiro, nunca o
        // que esta embaixo dela.
        mine_block = g_world->stack_block_at(g_target_x, g_target_y, target_stack_h - 1);
    } else {
        mine_block = has_mine_target ? g_world->get(g_target_x, g_target_y) : Block::Air;
        if (has_mine_target && mine_block == Block::Air) {
            mine_block = surface_block_at(*g_world, g_target_x, g_target_y);
        }
    }

    // Feedback ao tentar minerar estrutura da base
    if (mine_input && has_mine_target && is_base_structure(mine_block)) {
        static float base_warn_cd = 0.0f;
        base_warn_cd -= dt;
        if (base_warn_cd <= 0.0f) {
            show_error("Nao pode destruir estruturas da base!");
            base_warn_cd = 1.0f;
        }
    }

    bool mine_ok = mine_input && has_mine_target && is_mineable(mine_block);
    if (mine_ok && is_ground_like(mine_block)) {
        int16_t h = g_world->height_at(g_target_x, g_target_y);
        if (h <= 0) mine_ok = false; // bedrock local: evita mineracao infinita
    }
    static float mining_particle_timer = 0.0f;
    if (mine_ok) {
        g_player.is_mining = true;
        g_player.mine_anim = std::max(g_player.mine_anim - dt * 7.5f, 0.0f);

        // Virar na direcao do alvo
        float dx = tile_center(g_target_x) - g_player.pos.x;
        float dz = tile_center(g_target_y) - g_player.pos.y;
        g_player.target_rotation = std::atan2(-dx, -dz) * (180.0f / kPi);
        if (g_player.target_rotation < 0.0f) g_player.target_rotation += 360.0f;

        // Se mudou de bloco alvo, resetar progresso/hits.
        if (g_target_x != g_mine_block_x || g_target_y != g_mine_block_y) {
            g_mine_block_x = g_target_x;
            g_mine_block_y = g_target_y;
            g_mine_progress = 0.0f;
            g_mine_hits = 0;
            g_mine_hit_timer = 0.0f;
        }

        int req_hits = std::max(1, block_hits_required(mine_block));
        float stage = clamp01(g_terraform / 100.0f);
        float speed_mult = lerp(g_mining_cfg.early_game_speed_mult, g_mining_cfg.late_game_speed_mult, stage);
        speed_mult = std::max(0.25f, speed_mult);
        float hit_interval = std::max(g_mining_cfg.hit_interval_min, g_mining_cfg.hit_interval / speed_mult);

        g_mine_hit_timer -= dt;
        if (g_mine_hit_timer <= 0.0f) {
            g_mine_hit_timer = hit_interval;
            g_mine_hits = std::min(req_hits, g_mine_hits + 1);
            g_mine_progress = (float)g_mine_hits / (float)req_hits;

            // Impacto visivel por hit.
            g_player.mine_anim = std::max(g_player.mine_anim, g_player_visual_cfg.mine_impact_amp);
            g_screen_flash_green = std::max(g_screen_flash_green, 0.07f);
            // Beep(880, 1) removed (raylib migration): Win32's Beep() has no raylib
            // equivalent without loading an actual audio asset/device (InitAudioDevice +
            // LoadSound), which is out of scope here - the visual feedback above (mine_anim
            // flash + screen flash) still fires on every hit.

            // Particulas por golpe.
            for (int i = 0; i < 4; ++i) {
                Particle p;
                p.pos.x = tile_center(g_target_x) + (rand() % 100 - 50) / 100.0f * 0.42f;
                p.pos.y = tile_center(g_target_y) + (rand() % 100 - 50) / 100.0f * 0.42f;
                p.vel.x = (rand() % 100 - 50) / 45.0f;
                p.vel.y = (rand() % 100 - 50) / 45.0f - 1.2f;
                p.life = 0.22f + (rand() % 16) / 100.0f;
                float br, bg, bb, ba;
                block_color(mine_block, g_target_y, g_world->h, br, bg, bb, ba);
                p.r = br * 0.85f + 0.15f;
                p.g = bg * 0.85f + 0.15f;
                p.b = bb * 0.85f + 0.15f;
                p.a = 0.95f;
                g_particles.push_back(p);
            }
        }

        // Particulas de mineracao (feedback visual constante)
        mining_particle_timer += dt;
        if (mining_particle_timer >= 0.08f) {
            mining_particle_timer = 0.0f;
            if (mine_block != Block::Air) {
                for (int i = 0; i < 2; ++i) {
                    Particle p;
                    p.pos.x = tile_center(g_target_x) + (rand() % 100 - 50) / 100.0f * 0.4f;
                    p.pos.y = tile_center(g_target_y) + (rand() % 100 - 50) / 100.0f * 0.4f;
                    p.vel.x = (rand() % 100 - 50) / 50.0f;
                    p.vel.y = (rand() % 100 - 50) / 50.0f - 1.0f;
                    p.life = 0.3f + (rand() % 20) / 100.0f;
                    float br, bg, bb, ba;
                    block_color(mine_block, g_target_y, g_world->h, br, bg, bb, ba);
                    p.r = br * 0.9f + 0.1f;
                    p.g = bg * 0.9f + 0.1f;
                    p.b = bb * 0.9f + 0.1f;
                    p.a = 0.9f;
                    g_particles.push_back(p);
                }
            }
        }

        // Quebrar bloco ao completar progresso
        if (g_mine_hits >= req_hits || g_mine_progress >= 0.999f) {
            Block b = mine_block;
            spawn_block_particles(b, tile_center(g_target_x), tile_center(g_target_y), g_world->h);

            if (target_stack_h > 0) {
                // Bloco empilhado (torre/parede construida pelo jogador): remove so o topo
                // da pilha - o que estava embaixo (terreno/objeto original) fica intocado e
                // e revelado automaticamente assim que stack_height volta a 0. Modulos nunca
                // empilham (ver colocacao acima), entao nao ha necessidade de checar
                // is_module aqui.
                float spawn_y = stack_top_height_at(*g_world, g_target_x, g_target_y);
                g_world->stack_pop(g_target_x, g_target_y);
                g_surface_dirty = true;
                Block drop = drop_item_for_block(b);
                spawn_item_drop(drop, tile_center(g_target_x), tile_center(g_target_y), spawn_y + drop_spawn_y_for_block(b));
            } else {
                // Para blocos de terreno, remover 1 bloco "inteiro" em altura de mundo.
                // Como o heightmap usa kHeightScale, convertemos 1.0 mundo -> unidades do heightmap.
                if (is_ground_like(b)) {
                    // LAVA: RECOLHER, nao cavar. Baixar a coluna 4 unidades como qualquer solo
                    // abriria um poco dentro do rio de lava; o que faz sentido e' tirar a lava e
                    // deixar a rocha na MESMA cota. E a lava em volta escorre pra ca depois, pelo
                    // proprio fluxo (lava_flood_from abaixo) - o buraco nao fica seco de graca.
                    if (b == Block::Lava) {
                        g_world->set_ground(g_target_x, g_target_y, Block::Dirt);
                        g_world->set(g_target_x, g_target_y, Block::Dirt);
                        g_surface_dirty = true;
                        float sy = (float)g_world->height_at(g_target_x, g_target_y) * kHeightScale
                                   + drop_spawn_y_for_block(b);
                        spawn_item_drop(Block::Lava, tile_center(g_target_x), tile_center(g_target_y), sy);
                        // A lava vizinha volta a escorrer pro tile que acabou de esvaziar.
                        const int nx4[4] = {1, -1, 0, 0}, nz4[4] = {0, 0, 1, -1};
                        lava_flood_from(*g_world, g_target_x, g_target_y);
                        for (int k = 0; k < 4; ++k)
                            lava_flood_from(*g_world, g_target_x + nx4[k], g_target_y + nz4[k]);
                        water_flood_from(*g_world, g_target_x, g_target_y);
                    } else {
                    constexpr float kWorldBlockHeight = 1.0f;
                    int dig_units = std::max(1, (int)std::lround(kWorldBlockHeight / std::max(0.01f, kHeightScale)));
                    int16_t h = g_world->height_at(g_target_x, g_target_y);
                    int nh = std::max(0, (int)h - dig_units);
                    g_world->set_height(g_target_x, g_target_y, (int16_t)nh);

                    // Mantem material de solo (ground-like) para permitir mineracao sequencial.
                    Block prev_ground = g_world->get_ground(g_target_x, g_target_y);
                    Block next_ground = Block::Dirt;
                    if (prev_ground == Block::Sand) next_ground = Block::Sand;
                    else if (prev_ground == Block::Snow || prev_ground == Block::Ice) next_ground = Block::Ice;
                    g_world->set_ground(g_target_x, g_target_y, next_ground);
                    g_world->set(g_target_x, g_target_y, next_ground);

                    // Cavou: se ha agua vizinha acima desta cota, ela comeca a entrar no buraco
                    // (ver water_flood_from em world.h). Semeia tambem a partir dos 4 vizinhos: sem
                    // isso, cavar um tile que ja estava abaixo da linha d'agua mas cujo vizinho AGUA
                    // so' apareceu depois nao disparava nada.
                    water_flood_from(*g_world, g_target_x, g_target_y);
                    {
                        const int nx4[4] = {1, -1, 0, 0}, nz4[4] = {0, 0, 1, -1};
                        for (int k = 0; k < 4; ++k)
                            water_flood_from(*g_world, g_target_x + nx4[k], g_target_y + nz4[k]);
                        // ... e a LAVA tambem escorre pra buracos novos, no ritmo dela (~9x mais
                        // lento que a agua - ver lava_flood_from em world.h).
                        lava_flood_from(*g_world, g_target_x, g_target_y);
                        for (int k = 0; k < 4; ++k)
                            lava_flood_from(*g_world, g_target_x + nx4[k], g_target_y + nz4[k]);
                    }
                    }   // fim do ramo "solo comum" (o ramo da lava esta acima)
                } else {
                    g_world->set(g_target_x, g_target_y, Block::Air);
                }

                g_surface_dirty = true;

                if (is_module(b)) {
                    refund_cost(get_module_cost(b));
                    g_modules.erase(std::remove_if(g_modules.begin(), g_modules.end(),
                        [](const Module& m) { return m.x == g_target_x && m.y == g_target_y; }), g_modules.end());
                } else if (b != Block::Lava) {
                    // Lava nao cai aqui: o ramo de recolher acima ja soltou o drop dela. Sem esta
                    // excecao o jogador ganharia 2 lavas por tile.
                    Block drop = drop_item_for_block(b);
                    float sy = (float)g_world->height_at(g_target_x, g_target_y) * kHeightScale + drop_spawn_y_for_block(b);
                    spawn_item_drop(drop, tile_center(g_target_x), tile_center(g_target_y), sy);
                }
            }

            // Reset apos quebrar
            g_mine_progress = 0.0f;
            g_mine_block_x = -1;
            g_mine_block_y = -1;
            g_mine_hits = 0;
            g_mine_hit_timer = 0.0f;
            mining_particle_timer = 0.0f;
        }
    } else {
        g_player.is_mining = false;
        g_player.mine_anim = std::max(g_player.mine_anim - dt * 8.0f, 0.0f);
        mining_particle_timer = 0.0f;
        g_mine_progress = 0.0f;
        g_mine_block_x = -1;
        g_mine_block_y = -1;
        g_mine_hits = 0;
        g_mine_hit_timer = 0.0f;
    }

    g_prev_lmb = lmb;

    // Placing (RMB)
    if (rmb && !g_prev_rmb && g_has_place_target && g_place_in_range && g_place_cd <= 0.0f) {
        Block cur = g_world->get(g_place_x, g_place_y);
        if (placeable_tile_for_place(cur)) {
            // Check player collision
            float pl = g_player.pos.x - g_player.w * 0.5f;
            float pr = g_player.pos.x + g_player.w * 0.5f;
            float pt = g_player.pos.y - g_player.h * 0.5f;
            float pb = g_player.pos.y + g_player.h * 0.5f;
            float tl = tile_min(g_place_x);
            float tr = tile_max(g_place_x);
            float tf = tile_min(g_place_y);
            float tb = tile_max(g_place_y);
            bool overlaps_player = !(tr <= pl || tl >= pr || tb <= pt || tf >= pb);

            if (!overlaps_player) {
                if (is_module(g_selected)) {
                    // Check if module is unlocked first
                    if (!is_unlocked(g_selected)) {
                        set_toast("Modulo nao desbloqueado! Colete mais recursos.");
                    } else {
                        // Antes usava module_cost() (custos ~10x mais baratos que o menu de
                        // construcao, ex. Painel Solar ferro 3/pedra 2 vs o real 30/10) - um
                        // exploit real, ja que colocar direto pelo hotbar saia muito mais
                        // barato que usar o menu. Agora cobra o mesmo preco real dos dois
                        // jeitos; so continua sem fila/tempo de construcao (conveniencia, nao
                        // o bug).
                        CraftCost cost = get_module_cost(g_selected);
                        if (can_afford(cost)) {
                            spend_cost(cost);
                            if (cur == Block::Water) g_world->set_ground(g_place_x, g_place_y, Block::Dirt);
                            g_world->set(g_place_x, g_place_y, g_selected);
                            g_modules.push_back(Module{g_place_x, g_place_y, g_selected, 0.0f});
                            notify_module_built(g_selected);
                            g_surface_dirty = true;
                            g_place_cd = 0.25f;

                            // Special messages for certain modules
                            if (g_selected == Block::CO2Factory) {
                                set_toast("Fabrica de CO2 colocada! Aquecendo o planeta...", 3.0f);
                            } else if (g_selected == Block::TerraformerBeacon) {
                                show_success("Terraformador ativo! (Requer fase de Degelo)");
                            }
                        } else {
                            show_error("Recursos insuficientes!");
                        }
                    }
                } else if (g_inventory[(int)g_selected] > 0) {
                    // BLOCO DE SOLO (Terra/Pedra/Areia/Grama/Neve/Gelo) NAO cabe na slot de OBJETO.
                    // get_block_height() testa is_ground_like ANTES de tudo e devolve altura ZERO,
                    // entao "world.set(tile, Terra)" gravava um objeto de altura 0: invisivel, sem
                    // colisao - e o item era debitado do inventario do mesmo jeito. Era exatamente o
                    // bug relatado ("a quantidade de blocos e' zerada e nao consigo construir"), e so'
                    // com solo: Madeira/Metal/Carvao nao sao ground-like, ganham altura 1.0 e
                    // apareciam normalmente.
                    //
                    // A slot certa pra "mais uma camada de terreno" e' a PILHA - altura fixa 1.0 em
                    // render E colisao, o mesmo caminho que constroi torres e muros. A MIRA ja
                    // classificava terreno solido como empilhamento, mas a ACAO redecidia pela slot de
                    // objeto (que em terreno natural e' Air) e discordava dela; por isso o ramo de
                    // empilhamento logo abaixo praticamente nunca rodava.
                    Block gr = g_world->get_ground(g_place_x, g_place_y);
                    bool on_water = (cur == Block::Water || gr == Block::Water);
                    if (is_ground_like(g_selected) && on_water) {
                        // Aterrar agua com solo: troca o CHAO pelo material escolhido em vez de
                        // empilhar em cima da agua (o que deixaria um bloco sobre o lago). set_ground
                        // fixo em Dirt, como era antes, aterraria areia como terra.
                        g_world->set_ground(g_place_x, g_place_y, g_selected);
                        if (cur == Block::Water) g_world->set(g_place_x, g_place_y, Block::Air);
                        g_inventory[(int)g_selected]--;
                        g_surface_dirty = true;
                        g_place_cd = 0.12f;
                    } else if (is_ground_like(g_selected)) {
                        // Debita SO' se a pilha aceitou: kMaxStackExtra e' um teto real e gastar o
                        // item numa recusa e' a mesma falha que este bloco esta consertando.
                        if (g_world->stack_push(g_place_x, g_place_y, g_selected)) {
                            g_inventory[(int)g_selected]--;
                            g_surface_dirty = true;
                            g_place_cd = 0.12f;
                        } else {
                            set_toast("Nao da pra empilhar mais alto aqui.");
                        }
                    } else {
                        g_inventory[(int)g_selected]--;
                        // Preencher agua com um bloco solido tem que atualizar o "chao" por baixo
                        // tambem (nao so o objeto) - senao surface_block_at/stack_top_block_at
                        // continuam enxergando Agua ali (ground array nunca mudou) e o jogador
                        // ficaria "nadando" em cima de um bloco solido que acabou de preencher o lago.
                        if (cur == Block::Water) g_world->set_ground(g_place_x, g_place_y, Block::Dirt);
                        g_world->set(g_place_x, g_place_y, g_selected);
                        g_surface_dirty = true;
                        g_place_cd = 0.12f;
                    }
                }
            }
        } else if (g_place_is_stack && !is_module(g_selected) && g_inventory[(int)g_selected] > 0) {
            // Empilhamento: alvo ja e solido (recusado por placeable_tile_for_place acima),
            // mas a coluna ainda tem espaco na pilha - empilha em cima em vez de recusar.
            // Modulos ficam de fora de proposito (nao empilham nesta v1).
            float pl = g_player.pos.x - g_player.w * 0.5f;
            float pr = g_player.pos.x + g_player.w * 0.5f;
            float pt = g_player.pos.y - g_player.h * 0.5f;
            float pb = g_player.pos.y + g_player.h * 0.5f;
            float tl = tile_min(g_place_x);
            float tr = tile_max(g_place_x);
            float tf = tile_min(g_place_y);
            float tb = tile_max(g_place_y);
            bool overlaps_player = !(tr <= pl || tl >= pr || tb <= pt || tf >= pb);
            if (!overlaps_player && g_world->stack_push(g_place_x, g_place_y, g_selected)) {
                g_inventory[(int)g_selected]--;
                g_surface_dirty = true;
                g_place_cd = 0.12f;
            }
        }
    }
    g_prev_rmb = rmb;

    // Atualizar drops e coletar por proximidade
    update_item_drops(dt);

    // Update particles
    for (auto& p : g_particles) {
        p.vel.y += 15.0f * dt;
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        p.life -= dt;
    }
    g_particles.erase(std::remove_if(g_particles.begin(), g_particles.end(),
        [](const Particle& p) { return p.life <= 0.0f; }), g_particles.end());
}
