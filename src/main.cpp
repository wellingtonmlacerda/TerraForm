#include "raylib_platform.h"
#include "math_core.h"
#include "noise.h"
#include "blocks.h"
#include "textures.h"
#include "config_types.h"
#include "world.h"
#include "camera.h"
#include "config_io.h"
#include "game_state.h"
#include "player_physics.h"
#include "items_particles.h"
#include "inventory_crafting.h"
#include "modules_building.h"
#include "font.h"                // init_font/draw_text/estimate_text_w_px (font extraction stage)
#include "save_load.h"           // save_game/load_game (save_load extraction stage)
#include "minimap.h"             // fog of war, waypoints, minimap/world map render (minimap extraction stage)
#include "render_primitives.h"   // render_quad/render_cube_3d/render_wall_3d_tex/etc. (render_primitives extraction stage)
#include "render_player.h"       // render_player_topdown/render_astronaut (render_player extraction stage)
#include "lighting.h"            // Light2D/g_lighting/compute_lightmap/sample_lightmap/etc. (lighting extraction stage)
#include "sky.h"                 // SkyPalette/compute_sky_palette/render_alien_sky/update_shooting_stars (sky extraction stage)
#include "ui_hud.h"              // render_hud (ui_hud extraction stage)
#include "ui_menu.h"             // render_menus/update_menu_input (ui_menu extraction stage)
#include "building_interaction.h" // update_mining_and_placement (building_interaction extraction stage)
#include "ui_build_menu.h"        // render_build_menu/update_build_menu_input (menu de construcao)
#include "input.h"                // key_down/key_pressed (input extraction stage)
#include "win32_platform.h"       // WindowProc/WinMain (win32_platform extraction stage - see there for why it declares nothing)
#include "objectives.h"           // objectives_victory_celebration_remaining (player objectives feature)
#include "creatures.h"            // update_creatures/render_creatures/try_craft_laser_pistol call site
#include "audio.h"                // play_meteor_impact_sound
#include "terrain_mesh.h"         // terrain_mesh_render_far/terrain_mesh_mark_dirty - cache de terreno distante
#include "base_interior.h"        // render_base_interior (mobilia/plantas/teto das salas)
#include "interiors.h"             // interiors_update/interior_at (exterior selado + interiores)
#include "base_exterior.h"         // render_base_exterior (o modelo proprio do exterior)
#include "module_models.h"         // render_module_models (geometria propria de cada modulo)

// ===========================
// TerraFormer 2D (prototype)
// Win32 + OpenGL (immediate mode)
// ===========================

// ============= SISTEMA DE CORES CENTRALIZADO (UX) =============
// Cores funcionais - cada cor tem significado consistente
static const float kColorHp[]       = {0.90f, 0.14f, 0.18f, 1.0f};   // Vermelho - vida/dano
static const float kColorOxygen[]   = {0.20f, 0.85f, 0.55f, 1.0f};   // Verde - oxigenio
static const float kColorWater[]    = {0.25f, 0.65f, 0.95f, 1.0f};   // Azul - agua
static const float kColorEnergy[]   = {0.95f, 0.84f, 0.25f, 1.0f};   // Amarelo - energia
static const float kColorFood[]     = {0.85f, 0.65f, 0.25f, 1.0f};   // Laranja - comida
static const float kColorDanger[]   = {0.95f, 0.35f, 0.20f, 1.0f};   // Vermelho-laranja - perigo
static const float kColorSuccess[]  = {0.30f, 0.95f, 0.45f, 1.0f};   // Verde brilhante - sucesso
static const float kColorLocked[]   = {0.50f, 0.50f, 0.55f, 1.0f};   // Cinza - bloqueado
static const float kColorWarning[]  = {0.95f, 0.75f, 0.20f, 1.0f};   // Amarelo-laranja - aviso

// Cores de UI - paineis e textos
static const float kColorPanelBg[]      = {0.08f, 0.08f, 0.10f, 0.85f};  // Fundo de painel
static const float kColorPanelBorder[]  = {0.30f, 0.55f, 0.85f, 0.90f};  // Borda azul
static const float kColorTextPrimary[]  = {0.95f, 0.95f, 0.95f, 1.0f};   // Texto principal
static const float kColorTextSecondary[]= {0.70f, 0.70f, 0.75f, 0.90f};  // Texto secundario
static const float kColorHighlight[]    = {0.95f, 0.95f, 0.35f, 0.90f};  // Destaque amarelo
static const float kColorSelection[]    = {0.35f, 0.65f, 0.95f, 0.80f};  // Selecao azul

// ============= Resources & Global State =============
// BASE resources (stored in the base, modules fill these). Lost "static": respawn_player_at_base()/
// spawn_player_new_game() (extracted to player_physics.cpp) need external linkage to read/write
// some of these - same pattern as g_oxygen/g_water_res/etc. in textures.cpp.
float g_base_energy = 50.0f;   // 0..500 (energy stored in base)
float g_base_water = 50.0f;    // 0..200 (water stored in base)
float g_base_oxygen = 50.0f;   // 0..200 (oxygen stored in base)
float g_base_food = 50.0f;     // 0..200 (food stored in base)
float g_base_integrity = 100.0f;  // 0..100 (base structural integrity)

static constexpr float kBaseEnergyMax = 500.0f;
static constexpr float kBaseWaterMax = 200.0f;
static constexpr float kBaseOxygenMax = 200.0f;
static constexpr float kBaseFoodMax = 200.0f;
static constexpr float kBaseIntegrityMax_Global = 100.0f;  // For reference before full declaration

// ConstructionJob struct + g_construction_queue moved to modules_building.h/.cpp
// (verbatim) - this is the items_particles/modules_building/inventory_crafting
// extraction stage. modules_building.h (included above) supplies the "extern
// std::vector<ConstructionJob> g_construction_queue;" declaration this file relies on
// (build-menu render/input, build_physics_test_map). The clear_construction_queue()
// wrapper that used to live here is gone: it existed only because player_physics.cpp had
// just a forward declaration of ConstructionJob (an incomplete type, for which
// std::vector<T>::clear() is not guaranteed to work per the standard); now that
// player_physics.cpp includes modules_building.h directly (see its own comment), it
// calls g_construction_queue.clear() itself.

// Alert struct moved to game_state.h (verbatim); g_alerts (the vector) stays here since
// it's used throughout this file's alert system, not just by the feedback subsystem
// extracted into game_state.cpp. Lost "static": spawn_player_new_game() (extracted to
// player_physics.cpp) also clears it.
std::vector<Alert> g_alerts;

// PLAYER resources (suit tanks, refilled at base). Lost "static": respawn_player_at_base()/
// spawn_player_new_game() (extracted to player_physics.cpp) read/write these.
float g_player_oxygen = 100.0f;  // 0..100 (suit O2 tank)
float g_player_water = 100.0f;   // 0..100 (suit water tank)
float g_player_food = 100.0f;    // 0..100 (carried food)

// Integridade do traje (0..100) - dreno CONTINUO (nao timer-depois-dano como os outros
// perigos) enquanto longe do abrigo da base. So sobe reparando na hora (tecla F, custo em
// Metal/Components/Crystal - a MESMA carteira do upgrade de modulo, tecla R) - de proposito
// NAO gatilhado so na base, senao viraria so mais um item da rotina de recarga que ja
// existe la (ver "PLAYER IS AT BASE" em modules_building.cpp) e perderia a tensao real de
// escolha (upgrade vs. reparo) minerando. Chegar a 0 nao mata na hora - so amplifica o
// dreno de O2/agua/comida (ver suit_use_mult em modules_building.cpp/update_modules()).
float g_suit_integrity = 100.0f;

// Legacy compatibility (these map to player resources now). g_energy/g_food lost
// "static": spawn_player_new_game() (extracted to player_physics.cpp) writes them too.
float g_energy = 0.0f;        // Deprecated, use g_base_energy
float g_water_res = 0.0f;     // Deprecated, maps to g_player_water
float g_oxygen = 0.0f;        // Deprecated, maps to g_player_oxygen
float g_food = 100.0f;        // Deprecated, maps to g_player_food

// g_terraform/g_victory/g_co2_level/g_phase lost "static" here: world.cpp's
// recompute_terraform_score/update_phase/terraform_step/melt_ice_around (extracted from
// this file) need external linkage to read/write them from another translation unit -
// same pattern as g_oxygen/g_water_res/etc. in textures.cpp.
float g_terraform = 0.0f;     // 0..100 (computed)
bool g_victory = false;

// Atmosphere & Temperature (Realistic Terraforming)
float g_temperature = -60.0f;  // Starting temp in Celsius (Mars-like)
float g_co2_level = 0.0f;      // 0..100 (atmospheric CO2)
float g_atmosphere = 0.0f;     // 0..100 (atmosphere density)
TerraPhase g_phase = TerraPhase::Frozen;

static constexpr float kEnergyMax = 500.0f;
static constexpr float kTempFrozen = -20.0f;    // Below this: frozen
static constexpr float kTempThawing = 0.0f;     // Water can be liquid
// kTempThawing used to be needed here too (update_modules()'s global ice-melt timer used
// to compare against it directly), but update_modules() has since moved to
// modules_building.cpp (this stage), which keeps its own file-local copy of this same
// literal value instead (see the comment there) - same pattern already used by
// world.cpp (update_phase()/melt_ice_around()) below. kTempThawing itself is left here
// unused rather than removed, to keep this stage's diff minimal and low-risk.
// kTempHabitable/kTempTarget moved to world.cpp only (defined there, static to that TU):
// they were exclusively used by update_phase(), which moved there too.

// Unlock System - tracks total resources ever collected
// UnlockProgress struct moved to game_state.h (verbatim); g_unlocks (the instance) stays
// here since it's used throughout this file's build/unlock logic, not just by the
// feedback subsystem extracted into game_state.cpp. Lost "static": spawn_player_new_game()
// (extracted to player_physics.cpp) resets it on new game.
UnlockProgress g_unlocks;

// ============= SISTEMA DE ONBOARDING =============
// OnboardingState struct + g_onboarding instance moved to game_state.h/game_state.cpp
// (this is the game state / feedback / onboarding extraction stage).

// ============= CONFIGURACOES DE ACESSIBILIDADE =============
// GameSettings struct moved to game_state.h (verbatim); g_settings (the instance) stays
// owned here (not moved with the feedback subsystem into game_state.cpp), but it lost
// "static": ui_menu.cpp's render_menus()/update_menu_input() (the ui_menu extraction
// stage - Settings screen render + A/D value adjustment) now read/write it from another
// translation unit - same pattern as g_terrain_cfg/g_base_cfg etc. above.
GameSettings g_settings;

// ============= FEEDBACK VISUAL =============
// g_screen_flash_red/green, g_hotbar_bounce(+_slot), the CollectPopup struct +
// g_collect_popups, and the unlock-popup globals all moved to
// game_state.h/game_state.cpp (this is the game state / feedback / onboarding
// extraction stage).

// Base location (landing site). g_base_x/g_base_y/g_show_build_menu/g_build_menu_selection
// lost "static": spawn_player_at_base()/spawn_player_new_game() (extracted to
// player_physics.cpp) read/write them - same pattern as g_oxygen/g_water_res/etc. in
// textures.cpp. Still owned by main.cpp: even though generate_base()/update_modules()
// (modules_building.cpp, this stage) and rebuild_modules_from_world() now read/write
// g_base_x/g_base_y too (via their own extern declarations), this file's build-menu
// render/input and HUD code remain their heaviest users, so they stay here for now.
int g_base_x = 0;
int g_base_y = 0;
bool g_show_build_menu = false;
int g_build_menu_selection = 0;
// g_settings_selection/g_pause_selection/g_menu_selection lost "static" here: ui_menu.cpp's
// render_menus() (button-hover highlighting) and update_menu_input() (click/keyboard
// handling, the ui_menu extraction stage) read/write them from another translation unit now.
int g_settings_selection = 0;  // 0=sensibilidade, 1=inverter Y, 2=brilho, 3=escala UI, 4=iluminacao, 5=sombras, 6=bloom, 7=vinheta, 8=voltar
int g_pause_selection = -1;     // -1=nenhum, 0=continuar, 1=salvar, 2=carregar, 3=config, 4=novo jogo
int g_menu_selection = -1;      // -1=nenhum, 0=novo jogo, 1=carregar, 2=sair

// Posicao do mouse na tela. g_mouse_x/g_mouse_y/g_mouse_left_clicked lost "static" here:
// ui_hud.cpp's render_hud() (extracted this stage - hotbar slot hit-testing, crosshair)
// needs external linkage to read them - same pattern as g_oxygen/g_water_res/etc. in
// textures.cpp.
int g_mouse_x = 0;
int g_mouse_y = 0;
bool g_mouse_left_clicked = false;  // Flag para clique esquerdo (single frame)

// BuildSlotInfo struct + g_build_slots moved to modules_building.h/.cpp (verbatim) - this
// is the items_particles/modules_building/inventory_crafting extraction stage.
// modules_building.h (included above) supplies the "extern std::vector<BuildSlotInfo>
// g_build_slots;" declaration this file relies on (build-menu render/input,
// generate_base(), build_physics_test_map).

// g_terrain_cfg/g_sky_cfg/g_mining_cfg/g_player_visual_cfg (and their *_config_path
// siblings below) lost "static" here: config_io.cpp's reload_terrain_config/
// reload_sky_config/reload_mining_config/reload_player_visual_config (extracted from
// this file) need external linkage to read/write them from another translation unit —
// same pattern as g_oxygen/g_water_res/etc. in textures.cpp. g_camera_cfg also lost
// "static" (it used to stay static here because only reload_camera_config, still defined
// in this file, touched it) now that camera.cpp reads it too (collision probing, mode
// tuning, etc.) via its own extern declaration. g_camera_config_path stays static: only
// reload_camera_config (still in this file) touches it.
TerrainConfig g_terrain_cfg = {};
SkyConfig g_sky_cfg = {};
CameraConfig g_camera_cfg = {};
MiningConfig g_mining_cfg = {};
PlayerVisualConfig g_player_visual_cfg = {};
// g_base_cfg also lost "static" here (same reasoning as g_terrain_cfg etc. above):
// modules_building.cpp's update_modules() (extracted from this file) now reads it
// (safe_radius, recharge_*_rate, repair_player_hp_per_sec, jetpack_refuel_per_sec) from
// another translation unit. Note this is only the instance losing "static" - the
// BaseConfig struct definition itself (config_types.h) is untouched by this stage.
BaseConfig g_base_cfg = {};
// g_map_cfg lost "static" here: minimap.cpp's render_minimap/render_world_map/add_waypoint/
// remove_nearest_waypoint (extracted from this file) need external linkage to read it from
// another translation unit - same pattern as g_terrain_cfg etc. above.
MapConfig g_map_cfg = {};
// g_minimap lost "static": spawn_player_new_game() (extracted to player_physics.cpp)
// resets its fog-of-war/waypoints on new game.
MiniMapRuntime g_minimap = {};
std::string g_terrain_config_path = "terrain_config.json";
std::string g_sky_config_path = "sky_config.json";
static std::string g_camera_config_path = "camera_config.json";
std::string g_mining_config_path = "mining_config.json";
std::string g_player_visual_config_path = "player_visual.json";
static std::string g_base_config_path = "base_config.json";
static std::string g_map_config_path = "map_config.json";

// struct World moved to world.h (see there for the type + its inline accessors); the
// out-of-line World::gen() terrain generator, and the terraforming free functions that
// used to sit near it in this file, moved to world.cpp.

// Forward declarations (gameplay/render below)
// save_game/load_game forward declarations removed: they moved to save_load.h (real,
// non-static declarations there now) as part of the save_load extraction stage.
// rebuild_modules_from_world/generate_base forward declarations removed: they moved to
// modules_building.h (real, non-static declarations there now) as part of the
// items_particles/modules_building/inventory_crafting extraction stage.
// surface_block_at/object_block_at/surface_height_at/get_block_height forward
// declarations removed: they moved to world.h (real, non-static declarations there now)
// as part of the world/terrain extraction.
// render_quad forward declaration removed: it now comes from render_primitives.h (included
// at the top of this file), whose real (non-forward) declaration supplies it - render_quad's
// definition moved out to render_primitives.cpp as part of the render_primitives/render_player
// extraction stage.
// draw_text forward declaration removed: it now comes from font.h (included at the top of
// this file), which is already visible here.
// set_toast forward declaration removed: it now comes from game_state.h (included at
// the top of this file), which is already visible here.
// reset_player_physics_runtime/step_player_physics forward declarations removed: they
// now come from player_physics.h (included at the top of this file), which also
// supplies the real (non-forward) PlayerPhysicsInput definition.
static void build_physics_test_map(World& world);

// ============= Gameplay State =============
// g_quit lost "static" here: ui_menu.cpp's update_menu_input() (Sair/ESC-from-main-menu
// handling, the ui_menu extraction stage) sets it from another translation unit now - the
// WinMain message loop and WindowProc below (still in this file) keep reading/writing it too.
bool g_quit = false;
static constexpr float TILE_PX = 16.0f;

// Sistema de zoom para melhor visibilidade
static float g_zoom = 2.0f;  // Zoom padrao 2x (tiles aparecem 32px)
static constexpr float kMinZoom = 1.5f;
static constexpr float kMaxZoom = 4.0f;

// g_world's definition moved to world.cpp (natural owner of the World type it points to);
// world.h supplies the "extern World* g_world;" declaration this file relies on.
// g_cam_pos lost "static" here: save_load.cpp's load_game() (extracted from this file)
// needs external linkage to write it on load - same pattern as g_shooting_stars below.
// This file keeps the definition (legacy 2D camera position, still used for smoothing
// elsewhere in this file).
Vec2 g_cam_pos = {0.0f, 0.0f};  // Mantido para compatibilidade temporaria

// Camera3D/CameraMode/CameraDebugRay and the camera update/collision/visibility functions
// (reset_camera_near_player, update_camera_position, apply_look_at, apply_perspective,
// get_mouse_ray_direction, check_camera_collision, update_camera_for_frame, etc.) moved to
// camera.h/camera.cpp. g_camera and its adaptive-mode state (g_camera_mode, ...) moved with
// them; camera.h supplies the "extern Camera3D g_camera;" (and friends) this file relies on.
bool g_debug = false;  // General debug toggle (not camera-specific); lost "static" here
                       // because camera.cpp's update_camera_for_frame() also reads it.

// struct Player + g_player, TerrainPhysicsType/PhysicsRayDebug/PhysicsRuntime/
// PlayerPhysicsInput + g_physics, and get_player_render_pos()/get_player_render_y()
// moved to player_physics.h/.cpp (this is the player/physics extraction stage).
// player_physics.h (included at the top of this file) supplies the "extern Player
// g_player;" / "extern PhysicsRuntime g_physics;" declarations this file relies on.

// g_physics_cfg/g_physics_config_path lost "static": config_io.cpp's
// reload_physics_config needs external linkage to read/write them (same pattern as
// g_terrain_cfg above / g_oxygen etc. in textures.cpp).
PhysicsConfig g_physics_cfg = {};
std::string g_physics_config_path = "physics_config.json";

static float get_player_render_rotation() { return g_physics.render_rotation; }

// update_camera_for_frame() moved to camera.cpp (camera.h supplies its declaration).

// g_inventory/g_selected moved to inventory_crafting.h/.cpp (this stage) - their natural
// owner among the modules extracted so far. inventory_crafting.h (included at the top of
// this file) supplies the "extern std::array<int, kBlockTypeCount> g_inventory;"/"extern
// Block g_selected;" declarations this file relies on (mining, HUD, hotbar, save/load,
// build menu); player_physics.cpp's spawn_player_new_game() (which grants the starter kit
// and resets the selected block) now gets them from that header too, instead of its own
// local extern declarations.

// g_prev_lmb/g_prev_rmb/g_prev_e lost "static" here: building_interaction.cpp's
// update_mining_and_placement() (the building_interaction extraction stage) needs external
// linkage to read/write them from another translation unit - same pattern as
// g_oxygen/g_water_res/etc. in textures.cpp.
bool g_prev_lmb = false;
bool g_prev_rmb = false;
static bool g_prev_esc = false;
static bool g_prev_enter = false;
bool g_prev_e = false;  // Tecla de interacao (top-down)
static bool g_prev_f5 = false;
static bool g_prev_f9 = false;
static bool g_prev_l = false;
static bool g_prev_q = false;
static bool g_prev_f3 = false;
static bool g_prev_f6 = false;
static bool g_prev_f7 = false;
static bool g_prev_h = false;
static bool g_prev_tab = false;
static bool g_prev_b = false;
static bool g_prev_v = false;
static bool g_prev_m = false;
static bool g_prev_r = false;
static bool g_prev_c = false;
static bool g_prev_f = false; // Reparar traje (ver g_suit_integrity) - R ja e "remover waypoint" so com o mapa aberto
static bool g_prev_g = false; // Refinar na Oficina (ver try_refine_at_workshop())
static bool g_prev_t = false; // Scanner (ver scan_for_points_of_interest(), minimap.cpp)
static bool g_prev_f4 = false; // Debug de distribuicao geologica no mapa (ver g_geo_debug_mode)
static bool g_prev_p = false; // Fabricar Pistola de Laser (ver try_craft_laser_pistol())
static bool g_prev_u = false; // Aprimorar a Pistola de Laser (ver try_upgrade_weapon())

// g_place_cd lost "static" here: building_interaction.cpp's update_mining_and_placement()
// needs external linkage to read/write it from another translation unit - same pattern as
// g_prev_lmb above.
float g_place_cd = 0.0f;
static float g_drown_accum = 0.0f;

// --- DIAGNOSTICO TEMPORARIO (remover depois de identificar a causa do voo infinito +
// piscar do chao reportado de novo) --- ver uso em render_world()/perto do render_hud().
static int g_debug_view_radius = 0;
static int g_debug_wall_radius = 0;
static int g_debug_wall_draws = 0;
static int g_debug_far_chunks_drawn = 0;

// Escala adaptativa de raio de visao (0.40..1.0): o diagnostico acima confirmou a causa raiz
// real do "voo infinito"/travamento reportado varias vezes nesta sessao - nao e' um bug de
// fisica nem um valor especifico de terreno errado, e' que o loop de terreno (por tile, sem
// culling de verdade) fica caro demais quando o relevo e' muito acidentado (o jogador pediu
// terreno mais dramatico/montanhoso varias vezes - cada versao mais bonita tambem desenha
// mais paredes). Em vez de ficar cortando view_radius/ajustando ruido de terreno as cegas
// toda vez que isso volta a acontecer (ja fizemos isso 2x), esta escala reage ao FPS real em
// tempo real: se o frame rate cai, o raio de visao encolhe sozinho ate' sustentar de novo (e
// volta a crescer quando sobra FPS) - protege contra QUALQUER futura mudanca de terreno que
// fique pesada demais, nao so' a de agora.
static float g_render_quality = 1.0f;
static float g_render_quality_timer = 0.0f;

// Mining progress (estilo Minicraft/Minecraft: segurar para quebrar). All five lost
// "static" here: building_interaction.cpp's update_mining_and_placement() needs external
// linkage to read/write them from another translation unit - same pattern as g_prev_lmb
// above.
int g_mine_block_x = -1;
int g_mine_block_y = -1;
float g_mine_progress = 0.0f; // 0..1
int g_mine_hits = 0;
float g_mine_hit_timer = 0.0f;

// g_has_target/g_target_x/g_target_y/g_target_in_range lost "static" here: ui_hud.cpp's
// render_hud() (extracted this stage - target-info HUD line) needs external linkage to
// read them - same pattern as g_oxygen/g_water_res/etc. in textures.cpp.
bool g_has_target = false;
int g_target_x = 0;
int g_target_y = 0;
bool g_target_in_range = false;

// Target de colocacao (tile onde o RMB vai tentar colocar). All four lost "static" here:
// building_interaction.cpp's update_mining_and_placement() needs external linkage to
// read/write them from another translation unit - same pattern as g_prev_lmb above.
bool g_has_place_target = false;
int g_place_x = 0;
int g_place_y = 0;
bool g_place_in_range = false;

// Particle/ItemDrop structs + g_particles/g_drops/g_target_drop moved to
// items_particles.h/.cpp (verbatim) - this is the items_particles/modules_building/
// inventory_crafting extraction stage. items_particles.h (included above) supplies the
// extern declarations this file relies on (particle/drop rendering, raycast mining/
// placement, clear() on respawn/new-game/load_game).
//
// ShootingStar's struct definition moved to items_particles.h too (it was textually
// interleaved with Particle/ItemDrop here). update_shooting_stars() has since moved out too
// (the sky extraction stage, declared in sky.h now) but g_shooting_stars (the vector) stays
// right here in main.cpp: it is read/written directly by save_load.cpp's load_game() (clear
// on load) and by this file's own new-game/respawn reset code, not just by the sky system.
// g_shooting_stars lost "static" here: save_load.cpp's load_game() (extracted from this
// file) needs external linkage to clear it on load - same pattern as g_day_time/g_alerts/
// g_base_cfg losing "static" for modules_building.cpp's update_modules().
// Eventos do ceu: estrelas cadentes (camera-relative para parecer "longe" do mundo).
std::vector<ShootingStar> g_shooting_stars;

// Meteoro raro que cai de verdade no mundo (nao so um risco no ceu, como a estrela cadente
// acima) perto do jogador, deixando um Cristal (o recurso mais raro/valioso que ja existe
// no jogo - reaproveitado em vez de inventar um material novo) pra coletar. So 1 por vez,
// disparado raramente (ver update_meteors() abaixo).
struct FallingMeteor {
    float x = 0.0f, z = 0.0f;
    float start_y = 0.0f, target_y = 0.0f;
    float t = 0.0f;
    float duration = 1.6f;
};
static std::vector<FallingMeteor> g_meteors;

// Chamado por save_load.cpp::load_game() - ver o comentario da declaracao la'. Um meteoro em
// voo foi mirado no mundo/base ANTIGOS; deixa-lo cair depois de carregar outro save escavaria
// a cratera (agora raio ~10) num ponto arbitrario do mundo novo.
void clear_falling_meteors() { g_meteors.clear(); }

// ModuleStatus enum + Module struct + g_modules moved to modules_building.h/.cpp
// (verbatim) - same stage as above. modules_building.h (included above) supplies the
// extern declarations this file relies on (world/minimap render, HUD, raycast placement/
// removal, build_physics_test_map).

// Light2D struct, the lightmap/bloom pixel buffers, LightingSettings + its g_lighting
// instance, and the debug toggles (g_debug_lightmap/g_debug_lights) moved to
// lighting.h/.cpp (verbatim) - the lighting extraction stage. lighting.h (included at the
// top of this file) supplies the declarations render_world()/update_game() below still
// rely on directly (per-tile lighting/vignette debug overlays, F3 debug cycle, settings-menu
// lighting options). g_debug_bloom did NOT move with them: grep confirms it was already
// dead code (declared, never read anywhere) before this stage, so it now lives as a
// file-local static inside lighting.cpp instead.

// ============= Generate Base (Landing Site) =============
// generate_base() moved to modules_building.h/.cpp (verbatim) - this is the
// items_particles/modules_building/inventory_crafting extraction stage.
// modules_building.h (included at the top of this file) supplies its
// declaration; player_physics.cpp's spawn_player_new_game() (which calls it to
// set up the landing site on a new game) now gets it from that header too,
// instead of its own local forward declaration.

// GameState enum + g_state, the toast/screen-flash/collect-popup/unlock-popup globals,
// the feedback functions (set_toast/show_error/show_success/add_collect_popup/
// show_unlock_popup/bounce_hotbar_slot), the onboarding functions (show_tip/
// update_onboarding), and the small xorshift RNG (g_rng/rng_next_u32/rng_next_f01) all
// moved to game_state.h/game_state.cpp (game state / feedback / onboarding extraction
// stage).

// g_day_time lost "static" here: modules_building.cpp's update_modules() (extracted from
// this file) now reads/writes it from another translation unit - same pattern as
// g_oxygen/g_water_res/etc. in textures.cpp. kDayLength itself lives in game_state.h now
// (was 6 duplicated static constexpr copies across this file/sky.cpp/lighting.cpp/
// minimap.cpp/modules_building.cpp/ui_menu.cpp - consolidated into one shared definition).
float g_day_time = 0.0f;

static float g_stats_timer = 0.0f;
// g_surface_dirty lost "static" here: world.cpp's terraform_step/melt_ice_around
// (extracted from this file) need external linkage to write it from another translation
// unit - same pattern as g_oxygen/g_water_res/etc. in textures.cpp.
bool g_surface_dirty = true;

// ============= Font =============
// init_font()/draw_text()/estimate_text_w_px() (and the file-local g_font_base) moved to
// font.h/font.cpp (verbatim) - the font extraction stage. font.h (included at the top of
// this file) supplies the declarations this file relies on.

// ============= Save/Load =============
static const char* kSavePath = "save_slot0.tf2d";

// rebuild_modules_from_world() moved to modules_building.h/.cpp (verbatim) - same stage
// as generate_base() above. modules_building.h (included at the top of this file)
// supplies its declaration. save_game()/load_game() themselves moved to save_load.h/.cpp
// (verbatim) - the save_load extraction stage; save_load.h (included at the top of this
// file) supplies their declarations.

// approach()/place_player_near()/find_spawn_x()/spawn_player_at_base()/
// respawn_player_at_base()/spawn_player_new_game() moved to player_physics.h/.cpp (this
// is the player/physics extraction stage). player_physics.h supplies the declarations
// this file relies on (place_player_near/find_spawn_x are unused outside that module,
// so they are not declared here).

// update_fog_of_war()/add_waypoint()/remove_nearest_waypoint()/clear_all_waypoints()/
// render_minimap()/render_world_map() (and the file-local get_minimap_color() helper)
// moved to minimap.h/minimap.cpp (verbatim) - the minimap extraction stage. minimap.h
// (included at the top of this file) supplies the declarations this file relies on.

static void build_physics_test_map(World& world) {
    const int cz = world.h / 2;
    const int x0 = 24;
    const int x1 = std::min(world.w - 24, x0 + 380);
    const int z0 = std::max(4, cz - 40);
    const int z1 = std::min(world.h - 5, cz + 40);
    const int16_t base_h = 24;

    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            world.set(x, z, Block::Air);
            world.set_ground(x, z, Block::Stone);
            world.set_height(x, z, base_h);
        }
    }

    // Lanes de material: gelo, areia, pedra e lama.
    for (int x = x0; x <= x1; ++x) {
        for (int z = cz - 34; z <= cz - 26; ++z) world.set_ground(x, z, Block::Ice);
        for (int z = cz - 20; z <= cz - 12; ++z) world.set_ground(x, z, Block::Sand);
        for (int z = cz - 6; z <= cz + 2; ++z) world.set_ground(x, z, Block::Stone);
        for (int z = cz + 8; z <= cz + 16; ++z) world.set_ground(x, z, Block::Organic);
    }

    // Buracos e gaps.
    for (int x = 72; x <= 94; ++x) {
        for (int z = cz - 2; z <= cz + 2; ++z) world.set_height(x, z, 8);
    }
    for (int x = 146; x <= 157; ++x) {
        for (int z = cz + 10; z <= cz + 16; ++z) world.set_height(x, z, 4);
    }

    // Escadas.
    for (int i = 0; i < 10; ++i) {
        int sx = 110 + i * 2;
        int16_t h = (int16_t)(base_h + i * 2);
        for (int x = sx; x < sx + 2; ++x) {
            for (int z = cz + 20; z <= cz + 26; ++z) world.set_height(x, z, h);
        }
    }

    // Rampa longa.
    for (int x = 190; x <= 256; ++x) {
        int16_t h = (int16_t)(base_h + (x - 190) / 3);
        for (int z = cz + 22; z <= cz + 34; ++z) world.set_height(x, z, h);
    }

    // Plataformas altas.
    for (int x = 300; x <= 332; ++x) {
        for (int z = cz - 14; z <= cz - 2; ++z) world.set_height(x, z, base_h + 16);
    }
    for (int x = 334; x <= 366; ++x) {
        for (int z = cz - 14; z <= cz - 2; ++z) world.set_height(x, z, base_h + 24);
    }

    // Obstaculos para testar colisao/step.
    for (int x = 214; x <= 224; x += 2) world.set(x, cz - 1, Block::Stone);
    for (int x = 238; x <= 248; x += 2) world.set(x, cz - 1, Block::Iron);
    world.set(272, cz + 12, Block::Copper);
    world.set(274, cz + 12, Block::Coal);
    world.set(276, cz + 12, Block::Crystal);

    // Degraus baixos de 1 tile para step-climb.
    for (int i = 0; i < 8; ++i) {
        int x = 40 + i * 6;
        int16_t h = (int16_t)(base_h + ((i & 1) ? 2 : 1));
        for (int z = cz - 10; z <= cz - 6; ++z) world.set_height(x, z, h);
    }

    world.rebuild_surface_cache();
    g_surface_dirty = true;
    g_modules.clear();
    g_construction_queue.clear();
    g_alerts.clear();
    g_build_slots.clear();
    rebuild_modules_from_world();

    g_base_x = x0 + 8;
    g_base_y = cz - 1;
    spawn_player_at_base();
    g_cam_pos = g_player.pos;
    set_toast("Mapa de teste de fisica carregado (F6).", 4.0f);
}

// get_block_height/surface_block_at/object_block_at/surface_height_at/is_mineable/
// block_hits_required moved to world.cpp (declarations now in world.h); called from
// many places below unrelated to World generation itself (physics, mining, rendering).


// TerrainPhysicsProfile/GroundProbeResult structs and terrain_type_from_block()...
// step_player_physics() moved to player_physics.h/.cpp (this is the player/physics
// extraction stage). reload_camera_config() below is unrelated to this move (it stays
// here, see comment on its own declaration in config_io.h for why).
// reload_camera_config: special case, NOT moved into config_io.cpp with the other 5
// reload_*_config wrappers. See the comment on its declaration in config_io.h for the
// full reasoning — in short, after loading CameraConfig it also re-clamps the live
// g_camera object. Camera3D (g_camera's type) has since moved to camera.h/.cpp (this
// phase of the plan), so the original blocker (config_io.cpp having no complete Camera3D
// definition) is gone; reload_camera_config just hasn't been relocated too, since doing
// so isn't needed for the camera extraction itself. It still reuses the generic
// reload_config<Cfg> template from config_io.h for the path-search/read/apply-overrides
// plumbing, same as the other 5.
bool reload_camera_config(bool create_if_missing) {
    bool loaded = reload_config<CameraConfig>("camera_config.json", g_camera_cfg, create_if_missing,
                                               write_default_camera_config, apply_camera_config_overrides,
                                               &g_camera_config_path);
    // A faixa de pitch mora no config e e' copiada pra o objeto vivo da camera aqui - g_camera
    // guarda min_pitch/max_pitch porque o input de mouse (win32_platform.cpp) e a projecao os leem
    // toda hora, mas quem MANDA e' o camera_config.json.
    g_camera.min_pitch = g_camera_cfg.min_pitch;
    g_camera.max_pitch = g_camera_cfg.max_pitch;
    g_camera.distance = std::clamp(g_camera.distance, g_camera.min_distance, g_camera.max_distance);
    g_camera.pitch = std::clamp(g_camera.pitch, g_camera.min_pitch, g_camera.max_pitch);
    return loaded;
}


// ============= Crafting =============
// ============================================================================
// MODULE & RESOURCE SYSTEM - Complete Gameplay Loop
// ============================================================================

// CraftCost struct moved to inventory_crafting.h/.cpp (verbatim) - this is the
// items_particles/modules_building/inventory_crafting extraction stage.
// inventory_crafting.h (included at the top of this file) supplies it.

// ModuleStats struct moved to modules_building.h/.cpp (verbatim) - same stage.
// modules_building.h (included at the top of this file) supplies it.

// Construction in progress
// Note: ConstructionJob, Alert, g_construction_queue, g_alerts, g_base_integrity
// are declared earlier in the file with forward declarations

static constexpr float kBaseIntegrityMax = 100.0f;
static constexpr float kBaseIntegrityDecayRate = 0.5f;  // Per minute without workshop

// Cooldown para evitar spam de alertas. Lost "static": modules_building.cpp's
// update_modules() (extracted from this file) now iterates it (cooldown countdown) from
// another translation unit - same pattern as g_oxygen/g_water_res/etc. in textures.cpp.
std::unordered_map<std::string, float> g_alert_cooldowns;

// Lost "static": modules_building.cpp's start_construction()/update_modules() (extracted
// from this file) now call it from another translation unit - same pattern as
// g_oxygen/g_water_res/etc. in textures.cpp.
void add_alert(const std::string& msg, float r, float g, float b, float duration = 3.0f, float cooldown = 5.0f) {
    // Check cooldown
    auto it = g_alert_cooldowns.find(msg);
    if (it != g_alert_cooldowns.end() && it->second > 0.0f) {
        return;  // Still on cooldown
    }
    
    // Don't duplicate alerts
    for (auto& a : g_alerts) {
        if (a.message == msg) {
            a.time_remaining = duration;
            return;
        }
    }
    g_alerts.push_back({msg, r, g, b, duration});
    g_alert_cooldowns[msg] = cooldown;
}

// get_module_stats() moved to modules_building.h/.cpp (verbatim) - this is the
// items_particles/modules_building/inventory_crafting extraction stage.
// modules_building.h (included at the top of this file) supplies its
// declaration.

// get_module_cost() moved to inventory_crafting.h/.cpp (verbatim) - same stage.
// inventory_crafting.h (included at the top of this file) supplies it.

// can_afford()/spend_cost() forward declarations removed: both moved to
// inventory_crafting.h/.cpp (verbatim, with can_afford/spend_cost/refund_cost
// deduplicated into a single pointer-to-member table - see the comment above
// can_afford() in inventory_crafting.cpp), so inventory_crafting.h (included at
// the top of this file) now supplies real declarations instead.

// module_cost_string() moved to inventory_crafting.h/.cpp (verbatim) - same stage.

// get_module_status()/status_string() moved to modules_building.cpp (verbatim,
// both stay static there - grep confirms neither is called anywhere outside this
// module, pre-existing dead code from before this refactor).

// start_construction() moved to modules_building.h/.cpp (verbatim) -
// modules_building.h supplies its declaration.

// UnlockRequirement struct + get_unlock_requirement() moved to
// modules_building.cpp (verbatim, both stay file-local/static there - only used
// internally by is_unlocked/check_unlocks/unlock_progress_string, all in the same
// file).

// is_unlocked()/check_unlocks()/unlock_progress_string() moved to
// modules_building.h/.cpp (verbatim) - modules_building.h supplies their
// declarations. check_unlocks() needs this external linkage for a new reason beyond
// main.cpp's own call site: items_particles.cpp's on_pickup_item() now also calls it,
// from another translation unit.

// cost_string() moved to inventory_crafting.h/.cpp (verbatim) - inventory_crafting.h
// supplies its declaration. module_cost() (the instant right-click placement path's own,
// much cheaper cost function) was removed this session - it was a real cost exploit, not an
// intentional second pricing tier. Both the right-click path and the build-menu/
// construction-queue path now call get_module_cost() exclusively.

// can_afford()/spend_cost()/refund_cost() moved to inventory_crafting.h/.cpp,
// WITH the one deliberate behavior-preserving change of this stage: the three
// near-identical 10-line bodies (each hand-listing the same 10 CraftCost fields
// against g_inventory[(int)Block::X]) are now a single pointer-to-member table
// iterated by all three - see inventory_crafting.cpp for the verification notes.

// spawn_block_particles()/drop_item_for_block()/drop_spawn_y_for_block()/
// spawn_item_drop()/on_pickup_item()/update_item_drops() moved to
// items_particles.h/.cpp (verbatim) - this is the items_particles/
// modules_building/inventory_crafting extraction stage. items_particles.h
// (included at the top of this file) supplies the declarations this file relies
// on (raycast mining/placement in update_game, further below). on_pickup_item()
// is not declared there - it is only called internally by update_item_drops()
// (which moved to the same file), so it stays static inside items_particles.cpp.

// ============= OpenGL Setup =============
// setup_opengl() moved to win32_platform.h/.cpp (verbatim, stays static there) - the
// win32_platform extraction stage. It is only ever called from WinMain (also moved there),
// so it needed no declaration anywhere else.

// render_quad/render_quad_tex/render_bar/render_circle/render_ellipse/render_rounded_rect
// (2D primitives) and render_player_topdown/render_astronaut (top-down player rendering,
// interleaved with the primitives right here in the original file) moved out as part of the
// render_primitives/render_player extraction stage: the first group to render_primitives.h/
// .cpp (included at the top of this file), the player pair to render_player.h/.cpp (also
// included at the top - it depends on both Player from player_physics.h and on
// render_primitives' render_circle/render_ellipse/render_quad).
// try_spawn_tree/terraform_step/recompute_terraform_score/update_phase/melt_ice_around
// moved to world.cpp (declarations now in world.h).

// update_shooting_stars() forward declaration removed from here: update_modules()
// (the only caller in this file, back when this comment was written) moved out of this
// file first, then update_shooting_stars() itself moved to sky.h/.cpp (verbatim) - the sky
// extraction stage. modules_building.cpp's update_modules() keeps calling it exactly as
// before, now via sky.h's declaration instead of its own forward declaration's original
// target in this file.

// update_modules() moved to modules_building.h/.cpp (verbatim) - this is the
// items_particles/modules_building/inventory_crafting extraction stage.
// modules_building.h (included at the top of this file) supplies its
// declaration; update_game() (further below) calls it exactly as before.

// ============= Renderizacao 3D (Estilo Minicraft) =============

// render_cube_outline_3d/render_cube_3d moved to render_primitives.h/.cpp (render_primitives
// extraction stage) - render_cube_3d_tex below stays here for now (not part of that stage's
// list) and keeps calling render_cube_outline_3d via render_primitives.h's declaration.

// Fog manual (raylib/rlgl nao tem equivalente a glFog*): aplica o lerp de cor em direcao a
// g_frame_fog (setado uma vez por frame em render_world(), ver render_primitives.h) baseado
// na distancia da camera ate a posicao dada - mesma formula do fog GL_LINEAR original,
// usada como aproximacao por quad/cubo (nao por vertice) ja que essas funcoes locais so
// recebem uma posicao "centro" por chamada. Compartilhada por render_cube_3d_tex/
// render_plane_3d/render_plane_3d_tex abaixo.
static void apply_frame_fog_local(float wx, float wy, float wz, float& r, float& g, float& b) {
    if (!g_frame_fog.enabled) return;
    float dx = wx - g_camera.position.x, dy = wy - g_camera.position.y, dz = wz - g_camera.position.z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    float span = std::max(0.0001f, g_frame_fog.end - g_frame_fog.start);
    float factor = clamp01((g_frame_fog.end - dist) / span);
    r = lerp(g_frame_fog.r, r, factor);
    g = lerp(g_frame_fog.g, g, factor);
    b = lerp(g_frame_fog.b, b, factor);
}

// Renderizar cubo 3D texturizado (tile do atlas) com iluminacao fake por face.
// Requer rlSetTexture(g_tex_atlas) ativo (equivalente ao antigo glBindTexture).
// Mascara de faces: bit setado = desenhar aquela face. Sem isso, cada camada de pilha desenha as 6
// faces sempre - o comentario do loop de pilhas admitia isso ("culling de face oculta fica
// deliberadamente fora de escopo... pilhas sao curtas na pratica"). A premissa mudou: o EXTERIOR da
// base agora e' volume MACICO (ver interiors.h - a unica forma de o jetpack nao invadir um interior
// e' nao existir vao nenhum pra invadir), e sem culling um modulo 9x9x5 custaria 405 cubos x 6 faces
// = 2430 quads em vez dos 261 da casca visivel. Era exatamente esse custo que forcava a base a ser
// feita de salas ocas - a origem do bug que o jogador reportou.
enum : uint8_t {
    kFaceTop    = 1 << 0,
    kFaceBottom = 1 << 1,
    kFaceZPos   = 1 << 2,
    kFaceZNeg   = 1 << 3,
    kFaceXNeg   = 1 << 4,
    kFaceXPos   = 1 << 5,
    kFaceAll    = 0x3F,
};

static void render_cube_3d_tex(float x, float y, float z, float size, Tile top, Tile side, Tile bottom,
                               float tint_r, float tint_g, float tint_b, float a = 1.0f,
                               bool outline = false, uint8_t faces = kFaceAll) {
    float half = size * 0.5f;

    // Iluminacao fake (3 niveis)
    float top_shade = 1.00f;
    float side_shade = 0.72f;
    float dark_shade = 0.52f;

    UvRect uv_top = atlas_uv(top);
    UvRect uv_side = atlas_uv(side);
    UvRect uv_bottom = atlas_uv(bottom);

    // Fog manual (rlgl nao tem glFog*): aplicado uma vez usando o centro do cubo como
    // aproximacao de distancia (ver render_primitives.h's FrameFogParams).
    apply_frame_fog_local(x, y, z, tint_r, tint_g, tint_b);

    rlBegin(RL_QUADS);

    // Top (Y+)
    if (faces & kFaceTop) {
        rlColor4f(tint_r * top_shade, tint_g * top_shade, tint_b * top_shade, a);
        rlTexCoord2f(uv_top.u0, uv_top.v1); rlVertex3f(x - half, y + half, z - half);
        rlTexCoord2f(uv_top.u1, uv_top.v1); rlVertex3f(x + half, y + half, z - half);
        rlTexCoord2f(uv_top.u1, uv_top.v0); rlVertex3f(x + half, y + half, z + half);
        rlTexCoord2f(uv_top.u0, uv_top.v0); rlVertex3f(x - half, y + half, z + half);
    }

    // Bottom (Y-)
    if (faces & kFaceBottom) {
        rlColor4f(tint_r * dark_shade, tint_g * dark_shade, tint_b * dark_shade, a);
        rlTexCoord2f(uv_bottom.u0, uv_bottom.v0); rlVertex3f(x - half, y - half, z + half);
        rlTexCoord2f(uv_bottom.u1, uv_bottom.v0); rlVertex3f(x + half, y - half, z + half);
        rlTexCoord2f(uv_bottom.u1, uv_bottom.v1); rlVertex3f(x + half, y - half, z - half);
        rlTexCoord2f(uv_bottom.u0, uv_bottom.v1); rlVertex3f(x - half, y - half, z - half);
    }

    // Front (Z+)
    if (faces & kFaceZPos) {
        rlColor4f(tint_r * side_shade, tint_g * side_shade, tint_b * side_shade, a);
        rlTexCoord2f(uv_side.u0, uv_side.v0); rlVertex3f(x - half, y - half, z + half);
        rlTexCoord2f(uv_side.u1, uv_side.v0); rlVertex3f(x + half, y - half, z + half);
        rlTexCoord2f(uv_side.u1, uv_side.v1); rlVertex3f(x + half, y + half, z + half);
        rlTexCoord2f(uv_side.u0, uv_side.v1); rlVertex3f(x - half, y + half, z + half);
    }

    // Back (Z-)
    if (faces & kFaceZNeg) {
        rlColor4f(tint_r * dark_shade, tint_g * dark_shade, tint_b * dark_shade, a);
        rlTexCoord2f(uv_side.u0, uv_side.v0); rlVertex3f(x + half, y - half, z - half);
        rlTexCoord2f(uv_side.u1, uv_side.v0); rlVertex3f(x - half, y - half, z - half);
        rlTexCoord2f(uv_side.u1, uv_side.v1); rlVertex3f(x - half, y + half, z - half);
        rlTexCoord2f(uv_side.u0, uv_side.v1); rlVertex3f(x + half, y + half, z - half);
    }

    // Left (X-)
    if (faces & kFaceXNeg) {
        rlColor4f(tint_r * dark_shade, tint_g * dark_shade, tint_b * dark_shade, a);
        rlTexCoord2f(uv_side.u0, uv_side.v0); rlVertex3f(x - half, y - half, z - half);
        rlTexCoord2f(uv_side.u1, uv_side.v0); rlVertex3f(x - half, y - half, z + half);
        rlTexCoord2f(uv_side.u1, uv_side.v1); rlVertex3f(x - half, y + half, z + half);
        rlTexCoord2f(uv_side.u0, uv_side.v1); rlVertex3f(x - half, y + half, z - half);
    }

    // Right (X+)
    if (faces & kFaceXPos) {
        rlColor4f(tint_r * side_shade, tint_g * side_shade, tint_b * side_shade, a);
        rlTexCoord2f(uv_side.u0, uv_side.v0); rlVertex3f(x + half, y - half, z + half);
        rlTexCoord2f(uv_side.u1, uv_side.v0); rlVertex3f(x + half, y - half, z - half);
        rlTexCoord2f(uv_side.u1, uv_side.v1); rlVertex3f(x + half, y + half, z - half);
        rlTexCoord2f(uv_side.u0, uv_side.v1); rlVertex3f(x + half, y + half, z + half);
    }

    rlEnd();

    if (outline) {
        render_cube_outline_3d(x, y, z, size, 1.0f);
        // render_cube_outline_3d() draws via DrawCubeWiresV -> rlBegin(RL_LINES). rlgl's
        // rlBegin() (rlgl.h) unconditionally resets the NEW draw call's textureId to
        // RLGL.State.defaultTextureId (a hardcoded opaque-white 1x1 texture) whenever the
        // primitive mode changes - it has no memory of whatever texture the app bound via
        // rlSetTexture(). render_world() only calls rlSetTexture(g_tex_atlas) once, before the
        // whole terrain loop starts, so without re-asserting it here, the very next RL_QUADS
        // draw (any later tile's plane/wall/cube in this same frame) silently samples the blank
        // white default texture instead of the atlas for the rest of the frame - this was the
        // root cause of "flat white/gray terrain, zero color variation between block types"
        // (this function is only reached when use_textures is true, so g_tex_atlas is valid).
        rlSetTexture(g_tex_atlas);
    }
}

// light_attenuation()/get_module_light()/compute_ambient_light()/get_natural_light_color()/
// collect_lights()/compute_shadow()/world_to_lightmap_index()/add_light_to_lightmap()/
// blur_lightmap_pass()/blur_lightmap()/extract_bloom()/blur_bloom()/compute_lightmap()/
// sample_lightmap()/compute_depth_factor()/apply_color_grading()/compute_vignette() (and the
// smoothstep() helper) moved to lighting.h/.cpp (verbatim) - the lighting extraction stage.
// lighting.h supplies the four declarations render_world() below still needs
// (compute_lightmap/sample_lightmap/compute_depth_factor/apply_color_grading); everything
// else stays static inside lighting.cpp (grep confirms no call sites outside that file) -
// see the comments there for exactly which pattern applies to each. compute_vignette() in
// particular is grep-confirmed dead code (defined, never called anywhere in the codebase),
// kept static and unchanged.

// SkyPalette struct, compute_sky_palette()/render_sky_gradient_dome()/render_billboard_disc()/
// render_lit_sphere()/render_star_layer()/render_nebula_layer()/render_cloud_layer()/
// update_shooting_stars()/render_shooting_stars()/render_alien_sky() (and the hash01()
// helper) moved to sky.h/.cpp (verbatim) - the sky extraction stage. sky.h (included at the
// top of this file) supplies the three declarations still needed from outside that file:
// SkyPalette/compute_sky_palette() (render_world() below builds its own SkyPalette for the
// GL clear color) and render_alien_sky() (render_world()'s single call site, right after).
// update_shooting_stars() is declared there too - modules_building.cpp's update_modules()
// keeps calling it exactly as before. Everything else stays static inside sky.cpp.
// render_plane_3d() moved to render_primitives.h/.cpp (lost "static" there) - creatures.cpp
// needs it now for the laser-scorch ground decal (mesma tecnica ja usada aqui pra
// chao/agua/poeira de pouso - render_cube_3d lia como "blocos flutuando", nao decal de chao,
// ver memoria da sessao "rendering_vfx_lessons").

// Renderizar plano texturizado (tile do atlas). Requer rlSetTexture(g_tex_atlas) ativo.
static void render_plane_3d_tex(float x, float y, float z, float size, Tile tile,
                                float tint_r, float tint_g, float tint_b, float a = 1.0f) {
    float half = size * 0.5f;
    UvRect uv = atlas_uv(tile);
    apply_frame_fog_local(x, y, z, tint_r, tint_g, tint_b);
    rlColor4f(tint_r, tint_g, tint_b, a);
    rlBegin(RL_QUADS);
    rlTexCoord2f(uv.u0, uv.v0); rlVertex3f(x - half, y, z - half);
    rlTexCoord2f(uv.u1, uv.v0); rlVertex3f(x + half, y, z - half);
    rlTexCoord2f(uv.u1, uv.v1); rlVertex3f(x + half, y, z + half);
    rlTexCoord2f(uv.u0, uv.v1); rlVertex3f(x - half, y, z + half);
    rlEnd();
}

// render_wall_3d_tex_{xpos,xneg,zpos,zneg}/render_sphere_3d moved to render_primitives.h/
// .cpp (render_primitives extraction stage). The 4 near-identical wall functions were
// collapsed there into a single render_wall_3d_tex(WallFace face, ...) parameterized by
// face - the 4 call sites in render_world() below were updated to the new form (this is
// the one deliberate non-verbatim call-site change in this stage, per the refactor plan's
// Fase 1b wall dedup).

// Renderizar cilindro 3D (para corpo do player)
static void render_cylinder_3d(float cx, float cy, float cz, float radius, float height, float r, float g, float b, float a = 1.0f, int segments = 12) {
    float half_h = height * 0.5f;

    // Corpo do cilindro (GL_QUAD_STRIP -> RL_QUADS: buffer do par de vertices anterior,
    // emite o quad (prev_top,prev_bot,cur_bot,cur_top) a partir da 2a iteracao).
    rlBegin(RL_QUADS);
    float prev_tx = 0, prev_ty = 0, prev_tz = 0, prev_r = 0, prev_g = 0, prev_b = 0;
    float prev_bx = 0, prev_by = 0, prev_bz = 0;
    bool have_prev = false;
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * kPi * (float)i / segments;
        float x = std::cos(angle);
        float z = std::sin(angle);
        float shade = 0.7f + 0.3f * std::fabs(x);  // Sombreamento lateral
        float cr = r * shade, cg = g * shade, cb = b * shade;
        float tx = cx + radius * x, ty = cy + half_h, tz = cz + radius * z;
        float bx = cx + radius * x, by = cy - half_h, bz = cz + radius * z;

        if (have_prev) {
            rlColor4f(prev_r, prev_g, prev_b, a); rlVertex3f(prev_tx, prev_ty, prev_tz);
            rlColor4f(prev_r, prev_g, prev_b, a); rlVertex3f(prev_bx, prev_by, prev_bz);
            rlColor4f(cr, cg, cb, a); rlVertex3f(bx, by, bz);
            rlColor4f(cr, cg, cb, a); rlVertex3f(tx, ty, tz);
        }
        prev_tx = tx; prev_ty = ty; prev_tz = tz;
        prev_bx = bx; prev_by = by; prev_bz = bz;
        prev_r = cr; prev_g = cg; prev_b = cb;
        have_prev = true;
    }
    rlEnd();

    // Topo (GL_TRIANGLE_FAN -> RL_TRIANGLES: buffer os vertices da fan, re-emite como
    // triplas (centro, v[i], v[i+1])).
    std::vector<Vector3> rim(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * kPi * (float)i / segments;
        rim[i] = {cx + radius * std::cos(angle), cy + half_h, cz + radius * std::sin(angle)};
    }
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < segments; ++i) {
        rlColor4f(r, g, b, a);
        rlVertex3f(cx, cy + half_h, cz);
        rlVertex3f(rim[i].x, rim[i].y, rim[i].z);
        rlVertex3f(rim[i + 1].x, rim[i + 1].y, rim[i + 1].z);
    }
    rlEnd();
}

static void render_physics_debug_3d() {
    if (!g_debug) return;

    Vec2 rp = get_player_render_pos();
    float ry = get_player_render_y();
    float hw = g_player.w * 0.5f;
    float hd = g_player.h * 0.5f;
    float foot = ry + g_physics_cfg.collision_skin;
    float head = foot + g_physics_cfg.collider_height;

    rlSetTexture(0);
    rlSetLineWidth(1.8f);

    // Collider AABB (GL_LINE_LOOP -> RL_LINES: cada par consecutivo + segmento de fechamento).
    rlBegin(RL_LINES);
    rlColor4f(0.10f, 0.95f, 1.0f, 0.95f);
    rlVertex3f(rp.x - hw, foot, rp.y - hd); rlVertex3f(rp.x + hw, foot, rp.y - hd);
    rlVertex3f(rp.x + hw, foot, rp.y - hd); rlVertex3f(rp.x + hw, foot, rp.y + hd);
    rlVertex3f(rp.x + hw, foot, rp.y + hd); rlVertex3f(rp.x - hw, foot, rp.y + hd);
    rlVertex3f(rp.x - hw, foot, rp.y + hd); rlVertex3f(rp.x - hw, foot, rp.y - hd);
    rlEnd();

    rlBegin(RL_LINES);
    rlColor4f(0.10f, 0.95f, 1.0f, 0.95f);
    rlVertex3f(rp.x - hw, head, rp.y - hd); rlVertex3f(rp.x + hw, head, rp.y - hd);
    rlVertex3f(rp.x + hw, head, rp.y - hd); rlVertex3f(rp.x + hw, head, rp.y + hd);
    rlVertex3f(rp.x + hw, head, rp.y + hd); rlVertex3f(rp.x - hw, head, rp.y + hd);
    rlVertex3f(rp.x - hw, head, rp.y + hd); rlVertex3f(rp.x - hw, head, rp.y - hd);
    rlEnd();

    rlBegin(RL_LINES);
    rlColor4f(0.10f, 0.95f, 1.0f, 0.95f);
    rlVertex3f(rp.x - hw, foot, rp.y - hd); rlVertex3f(rp.x - hw, head, rp.y - hd);
    rlVertex3f(rp.x + hw, foot, rp.y - hd); rlVertex3f(rp.x + hw, head, rp.y - hd);
    rlVertex3f(rp.x + hw, foot, rp.y + hd); rlVertex3f(rp.x + hw, head, rp.y + hd);
    rlVertex3f(rp.x - hw, foot, rp.y + hd); rlVertex3f(rp.x - hw, head, rp.y + hd);
    rlEnd();

    // Ground rays.
    for (int i = 0; i < g_physics.debug_ray_count; ++i) {
        const PhysicsRayDebug& ray = g_physics.debug_rays[(size_t)i];
        rlBegin(RL_LINES);
        if (ray.hit) rlColor4f(0.20f, 1.0f, 0.30f, 0.90f);
        else rlColor4f(1.0f, 0.20f, 0.20f, 0.90f);
        rlVertex3f(ray.from.x, ray.from.y, ray.from.z);
        rlVertex3f(ray.to.x, ray.to.y, ray.to.z);
        rlEnd();
    }

    // Camera rays (obstruction checks).
    for (int i = 0; i < g_camera_debug_ray_count; ++i) {
        const CameraDebugRay& ray = g_camera_debug_rays[(size_t)i];
        rlBegin(RL_LINES);
        if (ray.blocked) rlColor4f(1.0f, 0.35f, 0.20f, 0.92f);
        else rlColor4f(0.35f, 0.78f, 1.0f, 0.85f);
        rlVertex3f(ray.from.x, ray.from.y, ray.from.z);
        rlVertex3f(ray.to.x, ray.to.y, ray.to.z);
        rlEnd();
    }

    // Ground normal.
    Vec3 n0 = {rp.x, g_player.ground_height + 0.03f, rp.y};
    Vec3 n1 = {n0.x + g_physics.ground_normal.x * 1.1f,
               n0.y + g_physics.ground_normal.y * 1.1f,
               n0.z + g_physics.ground_normal.z * 1.1f};
    rlBegin(RL_LINES);
    rlColor4f(0.30f, 0.70f, 1.0f, 1.0f);
    rlVertex3f(n0.x, n0.y, n0.z);
    rlVertex3f(n1.x, n1.y, n1.z);
    rlEnd();

    // Velocity vector.
    Vec3 v0 = {rp.x, ry + 0.90f, rp.y};
    Vec3 v1 = {
        v0.x + g_player.vel.x * 0.20f,
        v0.y + g_player.vel_y * 0.10f,
        v0.z + g_player.vel.y * 0.20f
    };
    rlBegin(RL_LINES);
    rlColor4f(1.0f, 0.85f, 0.25f, 1.0f);
    rlVertex3f(v0.x, v0.y, v0.z);
    rlVertex3f(v1.x, v1.y, v1.z);
    rlEnd();

    // Collision normal.
    if (g_physics.hit_x || g_physics.hit_z) {
        Vec3 c0 = {rp.x, foot + 0.15f, rp.y};
        Vec3 c1 = {c0.x + g_physics.collision_normal.x * 0.7f, c0.y, c0.z + g_physics.collision_normal.y * 0.7f};
        rlBegin(RL_LINES);
        rlColor4f(1.0f, 0.2f, 1.0f, 1.0f);
        rlVertex3f(c0.x, c0.y, c0.z);
        rlVertex3f(c1.x, c1.y, c1.z);
        rlEnd();
    }

    rlSetLineWidth(1.0f);
}

// ============= Rendering =============
// render_world() loses "static" here for the same reason update_game() does just below in
// this file: win32_platform.cpp's WinMain() (the win32_platform extraction stage) calls it
// from another translation unit now, via its own forward declaration.
void render_world(int win_w, int win_h) {
    if (!g_world) return;

    // === SETUP 3D ===
    rlViewport(0, 0, win_w, win_h);
    rlEnableDepthTest(); // GL_LESS e o depth func padrao da raylib/rlgl, nao precisa setar.
    // IMPORTANTE: rlglInit() (chamado dentro de InitWindow) habilita GL_CULL_FACE por padrao
    // (glCullFace(GL_BACK)/glFrontFace(GL_CCW)/glEnable(GL_CULL_FACE) - ver rlgl.h). O jogo
    // original (OpenGL 1.x fixo) NUNCA usava GL_CULL_FACE em lugar nenhum - toda a geometria
    // manual deste arquivo/render_primitives.cpp/sky.cpp (glVertex3f/rlVertex3f por face) foi
    // desenhada sem se preocupar com winding/orientacao consistente, contando com o default
    // real do OpenGL (culling desabilitado). Sem este disable, ~metade das faces manuais
    // (chao/paredes do terreno, cubos do player e dos modulos da base, dome do ceu) somem
    // dependendo do angulo de camera, porque o winding delas nunca foi pensado para culling.
    rlDisableBackfaceCulling();
    rlClearColor(13, 15, 20, 255); // 0.05,0.06,0.08 * 255 - cor de fundo inicial (antes do ceu)
    rlClearScreenBuffers();

    // Projecao perspectiva
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    float aspect = (float)win_w / (float)win_h;
    apply_perspective(kCameraFovDegrees, aspect, 0.1f, 2200.0f);

    // Atualizar camera (target + colisao) para o frame atual
    update_camera_for_frame();

    // Aplicar view matrix
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    apply_look_at();

    // Calcular area visivel baseada na posicao do jogador (culling) - movido pra ANTES do
    // ceu/cupula (ver logo abaixo) porque a cupula da base precisa do mesmo view_radius pra
    // saber quando parar de se desenhar: ela era desenhada incondicionalmente (sem nenhum
    // corte de distancia), entao a partir de uma certa distancia o terreno ao redor sumia
    // (fora do view_radius) mas a cupula continuava aparecendo, lendo como "base flutuando
    // no nada" (feedback do jogador).
    Vec2 rpos = get_player_render_pos();
    float rpy = get_player_render_y();
    int player_tile_x = world_to_tile(rpos.x);
    int player_tile_z = world_to_tile(rpos.y);  // Y do 2D = Z no 3D
    // Teto de 200 fazia o horizonte parecer bem proximo assim que a camera se afastava (o
    // mapa agora e 1536x768 - bem maior que quando esse teto foi escolhido), dando a sensacao
    // de "parede" no fim do terreno renderizado em vez de um planeta grande pra explorar.
    //
    // Bonus de altitude (pedido do jogador: "quero ver muito longe quando voo, o planeta
    // parece gigante"): sem isso, view_radius so dependia do zoom da camera (g_camera.
    // distance) - voar bem alto de jetpack nao revelava mais terreno nenhum, so a mesma
    // "parede" de sempre, so vista de cima. Acima de ~8 unidades do chao (o suficiente pra
    // nao disparar em pulos normais) o raio cresce com a altitude, saturando por volta de
    // ~68 unidades (voo alto de verdade) - o teto geral tambem sobe (340 -> 520) so pra
    // essa faixa, o andar normal no chao continua limitado por zoom de camera como antes.
    float altitude_above_ground = std::max(0.0f, rpy - g_player.ground_height);
    float altitude_bonus = clamp01((altitude_above_ground - 8.0f) / 60.0f) * 260.0f;
    // Teto de 520 (voo bem alto) causava uma queda severa de FPS - o jogador ficava "preso"
    // subindo de jetpack porque o jogo travava, nao porque a fisica tivesse algum problema
    // (confirmado: combustivel/velocidade continuavam corretos num teste com debug ao vivo).
    // O loop de terreno e' por tile (nao ha frustum culling de verdade) - a area cresce com
    // o QUADRADO do raio, entao 520 (~850mil tiles em volta do jogador) e' caro demais pra
    // esse jeito de desenhar. Reduzido pra 380 (~450mil tiles no pior caso) - ainda bem mais
    // longe que o teto original de 340 de antes desta sessao, so' que sustentavel.
    int view_radius = (int)std::clamp(g_camera.distance * 3.8f + 55.0f + altitude_bonus, 110.0f, 380.0f);

    // Escala adaptativa (ver g_render_quality acima): checa o FPS real a cada 0.5s (nao todo
    // frame - GetFPS() ja e' uma media do raylib, checar com mais frequencia so reagiria a
    // ruido) e ajusta a escala com passos pequenos + zona morta entre os limiares de descida
    // e subida (24 vs 50) pra nao ficar oscilando o raio de visao pra frente e pra tras.
    g_render_quality_timer += GetFrameTime();
    if (g_render_quality_timer >= 0.5f) {
        g_render_quality_timer = 0.0f;
        int fps_now = GetFPS();
        if (fps_now > 0 && fps_now < 24) {
            g_render_quality = std::max(0.40f, g_render_quality - 0.10f);
        } else if (fps_now > 50) {
            g_render_quality = std::min(1.0f, g_render_quality + 0.05f);
        }
    }
    view_radius = std::max(90, (int)((float)view_radius * g_render_quality));

    // Paredes/objetos (4 desenhos extras por tile pras paredes, +1 pro objeto) sao a parte
    // mais cara do loop - cortar o raio deles pra 80% do raio do terreno (em vez de igual,
    // como ficou depois do pedido "sem pop" nesta sessao) da uma folga real de performance
    // sem reintroduzir aquele pop visivel: em vez de um corte binario duro, os ultimos 20%
    // do raio agora desvanecem (fade de alpha) ate sumir, ver wall_fade_alpha()/dist2 mais
    // abaixo - "some suavemente" em vez de "aparece do nada" ao se aproximar.
    int wall_radius = (int)((float)view_radius * 0.80f);
    int obj_radius = wall_radius;
    int view_radius2 = view_radius * view_radius;

    // --- DIAGNOSTICO TEMPORARIO (remover depois) ---
    // Jogador reportou voo infinito + piscar do chao de novo, mesmo depois do ajuste de
    // smooth_passes/detail_weight - precisa medir de verdade em vez de ajustar as cegas de
    // novo: quantas paredes estao sendo desenhadas por frame (cada diferenca de altura entre
    // tiles vizinhos, por menor que seja, desenha uma) e o FPS real na hora do bug.
    g_debug_view_radius = view_radius;
    // Horizonte de terreno deste frame (ver g_frame_terrain_horizon em render_primitives.h): o laco
    // de perto corta em view_radius e terrain_mesh_render_far recebe view_radius como far_radius,
    // entao ESTE e' o alcance total do terreno. Quem desenha estrutura fixa fora do laco (a base)
    // corta pelo mesmo numero - senao aparece onde nao ha mundo para esconder.
    g_frame_terrain_horizon = (float)view_radius;
    g_debug_wall_radius = wall_radius;
    g_debug_wall_draws = 0;
    int wall_radius2 = wall_radius * wall_radius;
    int obj_radius2 = obj_radius * obj_radius;

    // Terreno CACHEADO (ver terrain_mesh.h) cobre tudo ALEM disso, como Mesh reconstruida so
    // quando muda em vez de redesenhada em modo imediato todo frame - e' o que elimina o
    // custo medido (34 mil quads de parede/frame) que causava o travamento reportado. Dentro
    // de near_radius continua tudo em modo imediato exatamente como sempre foi (fidelidade
    // total - agua animada, brilho de sol, blend com o cursor de mineracao) - 60 tiles ainda
    // e' bem maior que qualquer alcance de interacao (mineracao ~4, pistola ~35). Reduzido de
    // 90 pra 60 depois de medir ao vivo que so' a regiao "perto" ja desenhava 22 mil paredes
    // em modo imediato num trecho de terreno bem acidentado - 60 corta esse pior caso bastante
    // sem encostar no alcance de nenhuma interacao.
    int near_radius = std::min(view_radius, 60);
    // Zona de fade (ultimos 25% do wall_radius) - alem de wall_radius nao desenha nada
    // (economia real); dentro da zona, o alpha cai linearmente ate 0 na borda.
    float wall_fade_start = (float)wall_radius * 0.75f;
    float wall_fade_start2 = wall_fade_start * wall_fade_start;

    float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
    float atmos_factor = clamp01(g_atmosphere / 100.0f);
    SkyPalette sky_palette = compute_sky_palette(day_phase, atmos_factor);
    float sky_r = lerp(sky_palette.hz_r, sky_palette.zn_r, 0.35f);
    float sky_g = lerp(sky_palette.hz_g, sky_palette.zn_g, 0.35f);
    float sky_b = lerp(sky_palette.hz_b, sky_palette.zn_b, 0.35f);

    // Clear com cor do ceu alienigena
    rlClearColor((unsigned char)std::clamp((int)(sky_r * 255.0f), 0, 255),
                 (unsigned char)std::clamp((int)(sky_g * 255.0f), 0, 255),
                 (unsigned char)std::clamp((int)(sky_b * 255.0f), 0, 255), 255);
    rlClearScreenBuffers();

    // === RENDERIZAR ELEMENTOS DO CEU (sol, luas, estrelas, anel) ===
    render_alien_sky(g_camera.position.x, g_camera.position.y, g_camera.position.z, g_player.ground_height, day_phase, atmos_factor);

    // === COMPUTAR LIGHTMAP 2D (RTX FAKE) ===
    compute_lightmap();

    // Fog de distancia por bioma para profundidade e esconder limite do mapa.
    {
        Block fog_surface = Block::Dirt;
        if (g_world->in_bounds(player_tile_x, player_tile_z)) {
            fog_surface = surface_block_at(*g_world, player_tile_x, player_tile_z);
        }

        float fog_mul_r = 1.0f, fog_mul_g = 1.0f, fog_mul_b = 1.0f;
        float fog_start_mul = 1.0f, fog_end_mul = 1.0f;
        switch (fog_surface) {
            case Block::Ice:
            case Block::Snow:
                fog_mul_r = 0.95f; fog_mul_g = 1.02f; fog_mul_b = 1.12f;
                fog_start_mul = 0.86f; fog_end_mul = 0.86f;
                break;
            case Block::Sand:
                fog_mul_r = 1.08f; fog_mul_g = 1.00f; fog_mul_b = 0.86f;
                fog_start_mul = 0.92f; fog_end_mul = 0.93f;
                break;
            case Block::Stone:
            case Block::Coal:
            case Block::Iron:
                fog_mul_r = 0.88f; fog_mul_g = 0.92f; fog_mul_b = 0.98f;
                fog_start_mul = 0.84f; fog_end_mul = 0.88f;
                break;
            case Block::Water:
                fog_mul_r = 0.82f; fog_mul_g = 0.95f; fog_mul_b = 1.08f;
                fog_start_mul = 0.80f; fog_end_mul = 0.84f;
                break;
            default:
                break;
        }

        float fog_col[4] = {
            clamp01(sky_r * fog_mul_r),
            clamp01(sky_g * fog_mul_g),
            clamp01(sky_b * fog_mul_b),
            1.0f
        };

        float fog_start = std::max(70.0f, (float)view_radius * g_sky_cfg.fog_start_factor * fog_start_mul);
        float fog_end = std::max(fog_start + 110.0f,
                                 (float)view_radius * g_sky_cfg.fog_end_factor * fog_end_mul + g_sky_cfg.fog_distance_bonus);

        // Fog manual (rlgl/raylib nao tem glFog*): parametros setados uma vez aqui, lidos por
        // render_cube_3d()/render_wall_3d_tex() (render_primitives.cpp) e pelas funcoes locais
        // render_plane_3d()/render_plane_3d_tex()/render_cube_3d_tex() (acima, neste arquivo)
        // via g_frame_fog (ver render_primitives.h) - mesmos valores de cor/inicio/fim que os
        // antigos glFogfv/glFogf usavam.
        g_frame_fog.enabled = true;
        g_frame_fog.start = fog_start;
        g_frame_fog.end = fog_end;
        g_frame_fog.r = fog_col[0];
        g_frame_fog.g = fog_col[1];
        g_frame_fog.b = fog_col[2];
    }
    // === MODELO DO EXTERIOR DA BASE ===
    // A cupula geodesica solta que ficava aqui foi substituida pelo modelo completo da instalacao
    // (base_exterior.h/.cpp): domo central, modulos cilindricos com topo em domo, corredores-tubo,
    // tanques, paineis solares, mastros e as escotilhas. Era a reclamacao "nao parece a base espacial
    // que pedi" - uma cupula sozinha sobre volumes de cubo nao le como instalacao.
    //
    // Fica AQUI (depois do setup de fog/lightmap, antes do bind do atlas de textura) pelo mesmo
    // motivo que a cupula: desenhado antes disso, o modelo nao receberia nem neblina nem iluminacao e
    // de noite ficaria um objeto claro em brilho pleno cercado de terreno preto. O corte por
    // distancia mora dentro de render_base_exterior().
    render_base_exterior();

    // Modelos proprios dos modulos (module_models.h). Fica AQUI pelo mesmo motivo do exterior da
    // base: depois do setup de fog/lightmap e antes do bind do atlas de textura - os modelos usam
    // a textura branca padrao, e desenhados depois do bind sairiam pintados com o atlas.
    render_module_models();

    // === RENDERIZACAO 3D DO MUNDO ===
    
    int start_x = std::max(0, player_tile_x - view_radius);
    int end_x = std::min(g_world->w - 1, player_tile_x + view_radius);
    int start_z = std::max(0, player_tile_z - view_radius);
    int end_z = std::min(g_world->h - 1, player_tile_z + view_radius);
    
    // Texturas
    bool use_textures = (g_tex_atlas != 0);
    if (use_textures) {
        rlSetTexture(g_tex_atlas);
    } else {
        rlSetTexture(rlGetTextureIdDefault());   // NAO rlSetTexture(0): pra id 0 o rlgl nao troca nada
    }
    int water_frame = ((int)std::floor(g_day_time * 4.0f)) & 3;

    // Renderizar terreno com altura (montanhas/vales/desfiladeiros) + objetos sobre o solo
    {
        constexpr float side_shade = 0.72f;
        constexpr float dark_shade = 0.52f;
        constexpr float kTopEps = 0.01f;
        // Diferencas de altura de 1 unidade de heightmap (0.25 mundo) entre tiles vizinhos sao
        // ruido de terreno normal (mais comum em relevo dramatico/com ridge forte, tipo
        // montanha/neve) - SEMPRE precisam de uma parede pra fechar a lacuna entre os topos
        // dos 2 tiles vizinhos (uma tentativa anterior so' PULAVA a parede pra diferencas
        // pequenas - isso sim deixava uma fresta de verdade visivel, lendo como "grade/
        // buraquinhos no chao", bug reportado). O que causava o "piscando" original nao era a
        // parede existir, e' ela vir com TEXTURA esticada numa faixa fina de poucos pixels na
        // tela - isso alias/treme (minificacao sem mipmap) conforme a camera se move. Por
        // isso: parede SEMPRE (fecha a lacuna), mas so' com textura esticada quando a
        // diferenca e' grande o bastante pra realmente parecer um penhasco de verdade -
        // diferencas pequenas usam render_wall_3d_tex(..., flat=true) (1 amostra de cor solida,
        // sem gradiente pra tremer).
        constexpr float kFlatWallThreshold = 1.2f;

        for (int tz = start_z; tz <= end_z; ++tz) {
            for (int tx = start_x; tx <= end_x; ++tx) {
                int ddx = tx - player_tile_x;
                int ddz = tz - player_tile_z;
                int dist2 = ddx * ddx + ddz * ddz;
                if (dist2 > view_radius2) continue; // culling circular

                float base_y = (float)g_world->height_at(tx, tz) * kHeightScale;

                Block surface = surface_block_at(*g_world, tx, tz);
                Block obj = object_block_at(*g_world, tx, tz);

                float world_x = (float)tx;
                float world_z = (float)tz;

                // === SOLO (top) === (so em modo imediato pra tiles cujo CHUNK terrain_mesh
                // considera "perto" - usa a MESMA funcao que terrain_mesh_render_far() usa
                // pra escolher os chunks "longe" cobertos pela malha cacheada, garantindo que
                // nenhum tile seja desenhado nos 2 caminhos ao mesmo tempo. Um corte por
                // dist2 <= near_radius2 aqui, diferente do criterio por-centro-de-chunk do
                // outro lado, causava sobreposicao perto da fronteira - 2 geometrias na
                // mesma posicao/altura brigando no z-buffer (bug real: "o chao voltou a
                // piscar").
                if (!terrain_mesh_tile_is_far(tx, tz, player_tile_x, player_tile_z, near_radius)) {
                    BlockTex gtex = block_tex(surface);
                    if (gtex.is_water) {
                        gtex.top = (Tile)((int)Tile::Water0 + water_frame);
                        gtex.side = gtex.top;
                        gtex.bottom = gtex.top;
                    }
                    // Lava tem seu proprio ciclo de animacao (nao entra mais no ramo is_water
                    // da agua) - mais lento que a agua, lava escorre devagar.
                    bool is_lava = (surface == Block::Lava);
                    if (is_lava) {
                        int lava_frame = ((int)std::floor(g_day_time * 2.2f)) & 3;
                        gtex.top = (Tile)((int)Tile::Lava0 + lava_frame);
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
                    a *= camera_occluder_alpha_for_tile(tx, tz);

                    // Edge blending entre terrenos adjacentes (transicao visual suave).
                    float neigh_r = 0.0f, neigh_g = 0.0f, neigh_b = 0.0f;
                    int neigh_count = 0;
                    int diff_count = 0;
                    const int nx[4] = {1, -1, 0, 0};
                    const int nz[4] = {0, 0, 1, -1};
                    for (int ni = 0; ni < 4; ++ni) {
                        int sx = tx + nx[ni];
                        int sz = tz + nz[ni];
                        if (!g_world->in_bounds(sx, sz)) continue;
                        Block sb = surface_block_at(*g_world, sx, sz);
                        BlockTex sbtex = block_tex(sb);
                        float sr = 1.0f, sg = 1.0f, sbb = 1.0f;
                        if (sbtex.uses_tint || sbtex.transparent) {
                            float cr, cg, cb, ca;
                            block_color(sb, sz, g_world->h, cr, cg, cb, ca);
                            if (sbtex.uses_tint) { sr = cr; sg = cg; sbb = cb; }
                        }
                        neigh_r += sr;
                        neigh_g += sg;
                        neigh_b += sbb;
                        neigh_count++;
                        if (sb != surface) diff_count++;
                    }
                    if (neigh_count > 0 && diff_count > 0) {
                        float inv = 1.0f / (float)neigh_count;
                        neigh_r *= inv;
                        neigh_g *= inv;
                        neigh_b *= inv;
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

                    // Shading leve por inclinacao/altura para destacar montanhas/vales.
                    float dhx = h_e - h_w;
                    float dhz = h_s - h_n;
                    float slope = std::sqrt(dhx * dhx + dhz * dhz);
                    float slope_shade = 1.0f - std::clamp(slope * 0.22f, 0.0f, 0.28f);
                    float alt_shade = 0.90f + 0.10f * clamp01(base_y / 18.0f);
                    float shade = slope_shade * alt_shade;
                    tint_r *= shade;
                    tint_g *= shade;
                    tint_b *= shade;
                    
                    // Lava e' EMISSIVA: emite a propria luz, entao nao escurece com
                    // inclinacao/altitude nem com a noite (o jogador reclamou que "no escuro
                    // ela deveria ser luminosa, pois e' lava" - antes ela recebia o mesmo
                    // escurecimento de qualquer chao e a noite virava um vermelho apagado).
                    // Refaz o tint do zero, sem shade, e ainda sobe o brilho.
                    if (is_lava) {
                        float cr, cg, cb, ca;
                        block_color(surface, tz, g_world->h, cr, cg, cb, ca);
                        // Pulso lento de brilho: da a sensacao de massa derretida se movendo.
                        float pulse = 0.88f + 0.12f * std::sin(g_day_time * 1.7f + world_x * 0.35f + world_z * 0.27f);
                        tint_r = std::min(1.0f, cr * 1.35f * pulse);
                        tint_g = std::min(1.0f, cg * 1.25f * pulse);
                        tint_b = std::min(1.0f, cb * 1.20f * pulse);
                    }

                    // === ILUMINACAO 2D (RTX FAKE) ===
                    if (g_lighting.enabled && !is_lava) {
                        float light_r, light_g, light_b;
                        sample_lightmap((float)tx, (float)tz, light_r, light_g, light_b);

                        // Escurecimento por profundidade
                        float depth_factor = compute_depth_factor(base_y, rpy);
                        light_r *= depth_factor;
                        light_g *= depth_factor;
                        light_b *= depth_factor;

                        // Aplicar iluminacao
                        tint_r *= light_r;
                        tint_g *= light_g;
                        tint_b *= light_b;

                        // Color grading
                        apply_color_grading(tint_r, tint_g, tint_b);
                    }

                    if (surface == Block::Water) {
                        float water_y = base_y - 0.18f + 0.05f * std::sin(g_day_time * 2.0f + world_x * 0.5f + world_z * 0.3f);

                        // O "sun glint" (flash aditivo branco em tiles aleatorios da agua) foi
                        // REMOVIDO a pedido do jogador. Ele piscava como manchas claras espalhadas
                        // pela superficie em vez de ler como reflexo do sol, e num lago grande a tela
                        // ficava salpicada de retangulos creme. A ondulacao de water_y acima ja da
                        // movimento a superficie.

                        if (use_textures) render_plane_3d_tex(world_x, water_y, world_z, 1.0f, gtex.top, tint_r, tint_g, tint_b, a);
                        else render_plane_3d(world_x, water_y, world_z, 1.0f, tint_r, tint_g, tint_b, 0.75f);
                    } else {
                        float top_y = base_y + kTopEps;
                        if (use_textures) render_plane_3d_tex(world_x, top_y, world_z, 1.0f, gtex.top, tint_r, tint_g, tint_b, a);
                        else render_plane_3d(world_x, top_y, world_z, 1.0f, tint_r, tint_g, tint_b, a);

                        // === BRILHO / FUMACA / BRASAS DA LAVA ===
                        // Pedido do jogador: "nao tem fumaca saindo, a lava nao parece
                        // incandescente, no escuro deveria ser luminosa". O tile emissivo (ver
                        // is_lava acima) resolve a cor; aqui vem o volume: um halo aditivo
                        // (que acende de verdade a noite), fumaca subindo e brasas voando.
                        // Tudo deterministico por tile (hash da coordenada) - sem sistema de
                        // particulas novo e sem tremer de frame em frame.
                        if (is_lava) {
                            float lh = std::sin(world_x * 12.9898f + world_z * 78.233f) * 43758.5453f;
                            lh -= std::floor(lh);

                            rlSetTexture(0);
                            rlSetBlendMode(RL_BLEND_ADDITIVE);
                            rlDisableDepthMask();

                            // Halo de calor rente ao chao - o que faz a lava "acender" no escuro.
                            float glow_pulse = 0.70f + 0.30f * std::sin(g_day_time * 2.1f + lh * 6.28f);
                            render_glow_disc_3d({world_x, top_y + 0.04f, world_z}, 0.95f,
                                                 1.0f, 0.45f, 0.10f, 0.34f * glow_pulse, 10);

                            // Brasas: pontinhos subindo devagar, em ~1/6 dos tiles (senao vira
                            // um enxame). Sobem e desaparecem ciclicamente.
                            if (lh > 0.83f) {
                                float ember_t = std::fmod(g_day_time * 0.55f + lh * 3.1f, 1.0f);
                                float ember_y = top_y + 0.15f + ember_t * 1.9f;
                                float ember_a = (1.0f - ember_t) * 0.85f;
                                float sway = std::sin(g_day_time * 1.6f + lh * 9.0f) * 0.16f;
                                render_cube_3d(world_x + sway, ember_y, world_z + sway * 0.6f,
                                               0.10f, 1.0f, 0.62f, 0.16f, ember_a, false);
                            }

                            rlEnableDepthMask();
                            rlSetBlendMode(RL_BLEND_ALPHA);

                            // === FUMACA: so' na CRATERA e na PONTA do rio de lava ===
                            // Pedido do jogador: "a fumaca deve ficar apenas no pico do vulcao
                            // e nao no rio de lava, mas pode ficar no fim do rio de lava".
                            // Antes qualquer tile de lava sorteado por hash fumava, o que
                            // enfumacava o rio inteiro. Aqui a decisao e' GEOMETRICA (nao ha
                            // lista de crateras disponivel na renderizacao - volcano_centers e'
                            // local da geracao): conta vizinhos de lava e classifica o tile.
                            //  - Cratera/poca grande: quase todo o bloco 7x7 em volta e' lava
                            //    (a cratera e' um disco de raio 9). E' o "pico do vulcao".
                            //  - Meio do rio: faixa estreita - poucos vizinhos no 7x7, mas o rio
                            //    CONTINUA (2+ vizinhos imediatos). Nao fuma.
                            //  - Ponta do rio: o fluxo morre ali, so' tem lava de um lado
                            //    (<= 1 vizinho imediato). Fuma (lava esfriando na ponta).
                            int lava_ring = 0;   // lava no bloco 7x7 (tamanho do corpo de lava)
                            int lava_adj = 0;    // lava nos 8 vizinhos imediatos (continuidade)
                            for (int oz = -3; oz <= 3; ++oz) {
                                for (int ox = -3; ox <= 3; ++ox) {
                                    if (ox == 0 && oz == 0) continue;
                                    int sx2 = tx + ox, sz2 = tz + oz;
                                    if (!g_world->in_bounds(sx2, sz2)) continue;
                                    if (g_world->get_ground(sx2, sz2) != Block::Lava) continue;
                                    lava_ring++;
                                    if (ox >= -1 && ox <= 1 && oz >= -1 && oz <= 1) lava_adj++;
                                }
                            }
                            bool crater_like = (lava_ring >= 40); // 7x7 tem 48 vizinhos
                            bool flow_tip = (lava_adj <= 1);
                            // Na cratera, so' ~1/5 dos tiles emitem (senao seriam centenas de
                            // colunas no mesmo lugar); na ponta do rio emitem sempre, ja que
                            // existe no maximo uma ponta por riacho.
                            bool emit_smoke = (crater_like && lh < 0.20f) || flow_tip;
                            if (emit_smoke) {
                                // Pluma da cratera e' bem mais alta/grossa (e' o pico do
                                // vulcao, tem que dar pra ver de longe); a da ponta do rio e'
                                // um fiozinho baixo de lava esfriando.
                                float plume = crater_like ? 1.0f : 0.45f;
                                int puffs = crater_like ? 5 : 3;
                                for (int sp = 0; sp < puffs; ++sp) {
                                    float st = std::fmod(g_day_time * 0.28f + lh * 5.0f + (float)sp * (1.0f / (float)puffs), 1.0f);
                                    float sy = top_y + 0.5f + st * (5.0f + 7.0f * plume);
                                    float ssway = std::sin(g_day_time * 0.7f + lh * 12.0f + st * 3.0f) * (0.5f + st * 1.4f);
                                    float ssize = (0.34f + st * 0.85f) * (0.7f + 0.8f * plume);
                                    float sa = (1.0f - st) * 0.30f;
                                    float sg = 0.30f + st * 0.16f; // esfria de escuro pra cinza claro
                                    render_cube_3d(world_x + ssway, sy, world_z + ssway * 0.5f,
                                                   ssize, sg, sg * 0.94f, sg * 0.92f, sa, false);
                                }
                            }

                            if (use_textures) rlSetTexture(g_tex_atlas);
                        }
                    }

                    // === PAREDES DE TILE DE LAVA = ROCHA, nao lava ===
                    // A parede de desnivel usava a textura e o tint do PROPRIO solo. Num tile de lava
                    // numa encosta isso desenhava um PAREDAO DE LAVA brilhante de varias unidades de
                    // altura, sem nada o sustentando - foi isso que o jogador viu como "represa no ar
                    // do nada". Fisicamente a lava e' uma CAMADA fina escorrendo; o penhasco embaixo
                    // dela e' rocha. Entao: topo continua lava emissiva, laterais viram basalto.
                    Tile wall_tile = gtex.side;
                    float wtint_r = tint_r, wtint_g = tint_g, wtint_b = tint_b;
                    if (is_lava) {
                        wall_tile = block_tex(Block::Basalt).side;
                        float br, bg, bb, ba;
                        block_color(Block::Basalt, tz, g_world->h, br, bg, bb, ba);
                        wtint_r = br * shade; wtint_g = bg * shade; wtint_b = bb * shade;
                        // Rocha nao e' emissiva: leva a iluminacao normal, ao contrario do topo.
                        if (g_lighting.enabled) {
                            float lr2, lg2, lb2;
                            sample_lightmap((float)tx, (float)tz, lr2, lg2, lb2);
                            float df2 = compute_depth_factor(base_y, rpy);
                            wtint_r *= lr2 * df2; wtint_g *= lg2 * df2; wtint_b *= lb2 * df2;
                            apply_color_grading(wtint_r, wtint_g, wtint_b);
                        }
                        // Brasa fraca na borda de cima da parede: a lava escorrendo pela beirada.
                        wtint_r = std::min(1.0f, wtint_r + 0.10f);
                        wtint_g = std::min(1.0f, wtint_g + 0.03f);
                    }


                    // === PAREDE DE AGUA/GELO = MARGEM DE TERRA, nao agua ===
                    // Mesma classe do penhasco de lava logo acima: a parede de desnivel usava a
                    // textura do PROPRIO solo, entao uma coluna de agua mais alta que a vizinha
                    // desenhava literalmente uma "parede de agua" de varias unidades de altura
                    // (sintoma reportado). Fisicamente a borda de um lago e' um barranco de terra
                    // submersa - a agua e' a superficie, nao o paredao.
                    //
                    // Isto e' rede de seguranca visual: com o gelo virando fonte de inundacao
                    // (water_flood_from) o buraco enche e o desnivel desaparece, mas ENQUANTO enche
                    // - e em qualquer caso onde a agua fique represada acima do vizinho - a parede
                    // agora le como barranco.
                    if (surface == Block::Water || surface == Block::Ice) {
                        wall_tile = block_tex(Block::Dirt).side;
                        float br, bg, bb, ba;
                        block_color(Block::Dirt, tz, g_world->h, br, bg, bb, ba);
                        wtint_r = br * shade; wtint_g = bg * shade; wtint_b = bb * shade;
                        if (g_lighting.enabled) {
                            float lr2, lg2, lb2;
                            sample_lightmap((float)tx, (float)tz, lr2, lg2, lb2);
                            float df2 = compute_depth_factor(base_y, rpy);
                            wtint_r *= lr2 * df2; wtint_g *= lg2 * df2; wtint_b *= lb2 * df2;
                            apply_color_grading(wtint_r, wtint_g, wtint_b);
                        }
                        // Escurece um pouco: barranco molhado/submerso e' mais escuro que terra seca.
                        wtint_r *= 0.72f; wtint_g *= 0.76f; wtint_b *= 0.82f;
                    }
                    // === LATERAIS (paredes) para diferenca de altura ===
                    bool do_walls = (dist2 <= wall_radius2);
                    if (!do_walls) {
                        float max_drop = std::max(std::max(h_here - h_e, h_here - h_w), std::max(h_here - h_s, h_here - h_n));
                        if (max_drop > 1.40f) do_walls = true; // manter grandes penhascos visiveis ao longe
                    }
                    // Desvanece (nao corta de repente) nos ultimos 25% do wall_radius - ver
                    // comentario em wall_fade_start acima. Penhascos grandes (max_drop>1.40,
                    // mantidos vivos alem do raio normal) ficam sempre 100% opacos, de proposito
                    // - sao poucos e ja eram uma excecao deliberada antes desta mudanca.
                    float wall_a = a;
                    if (do_walls && dist2 > wall_fade_start2 && dist2 <= wall_radius2) {
                        float fade_span = std::max(1.0f, (float)(wall_radius2 - (int)wall_fade_start2));
                        wall_a *= clamp01(1.0f - (float)(dist2 - (int)wall_fade_start2) / fade_span);
                    }

                    if (do_walls) {
                        bool wall_e = h_e < h_here;
                        bool wall_w = h_w < h_here;
                        bool wall_s = h_s < h_here;
                        bool wall_n = h_n < h_here;
                        // DIAGNOSTICO TEMPORARIO - ver declaracao/limpeza no topo do arquivo.
                        g_debug_wall_draws += wall_e + wall_w + wall_s + wall_n;
                        if (use_textures) {
                            if (wall_e) render_wall_3d_tex(WallFace::XPos, world_x, world_z, h_e, h_here, wall_tile, wtint_r, wtint_g, wtint_b, wall_a, side_shade, (h_here - h_e) <= kFlatWallThreshold);
                            if (wall_w) render_wall_3d_tex(WallFace::XNeg, world_x, world_z, h_w, h_here, wall_tile, wtint_r, wtint_g, wtint_b, wall_a, dark_shade, (h_here - h_w) <= kFlatWallThreshold);
                            if (wall_s) render_wall_3d_tex(WallFace::ZPos, world_x, world_z, h_s, h_here, wall_tile, wtint_r, wtint_g, wtint_b, wall_a, side_shade, (h_here - h_s) <= kFlatWallThreshold);
                            if (wall_n) render_wall_3d_tex(WallFace::ZNeg, world_x, world_z, h_n, h_here, wall_tile, wtint_r, wtint_g, wtint_b, wall_a, dark_shade, (h_here - h_n) <= kFlatWallThreshold);
                        } else {
                            // Fallback sem texturas: quads coloridos
                            auto wall_col = [&](float s) {
                                float wr = wtint_r * s, wg = wtint_g * s, wb = wtint_b * s;
                                apply_frame_fog_local(world_x, (h_here) , world_z, wr, wg, wb);
                                rlColor4f(wr, wg, wb, wall_a);
                            };
                            constexpr float half = 0.5f;
                            if (wall_e) {
                                rlBegin(RL_QUADS);
                                wall_col(side_shade);
                                rlVertex3f(world_x + half, h_e, world_z - half);
                                rlVertex3f(world_x + half, h_e, world_z + half);
                                rlVertex3f(world_x + half, h_here, world_z + half);
                                rlVertex3f(world_x + half, h_here, world_z - half);
                                rlEnd();
                            }
                            if (wall_w) {
                                rlBegin(RL_QUADS);
                                wall_col(dark_shade);
                                rlVertex3f(world_x - half, h_w, world_z + half);
                                rlVertex3f(world_x - half, h_w, world_z - half);
                                rlVertex3f(world_x - half, h_here, world_z - half);
                                rlVertex3f(world_x - half, h_here, world_z + half);
                                rlEnd();
                            }
                            if (wall_s) {
                                rlBegin(RL_QUADS);
                                wall_col(side_shade);
                                rlVertex3f(world_x - half, h_s, world_z + half);
                                rlVertex3f(world_x + half, h_s, world_z + half);
                                rlVertex3f(world_x + half, h_here, world_z + half);
                                rlVertex3f(world_x - half, h_here, world_z + half);
                                rlEnd();
                            }
                            if (wall_n) {
                                rlBegin(RL_QUADS);
                                wall_col(dark_shade);
                                rlVertex3f(world_x + half, h_n, world_z - half);
                                rlVertex3f(world_x - half, h_n, world_z - half);
                                rlVertex3f(world_x - half, h_here, world_z - half);
                                rlVertex3f(world_x + half, h_here, world_z - half);
                                rlEnd();
                            }
                        }
                    }
                }

                // === OBJETOS sobre o solo (rochas/minerios/modulos/estruturas) ===
                // is_furniture_collider: blocos de colisao de mobilia sao INVISIVEIS de proposito -
                // eles existem so' pra dar fisica aos moveis, cuja aparencia e' desenhada por
                // base_interior.cpp. Desenhar o cubo aqui poria uma caixa cinza em cima da cama.
                // MODULOS NAO SAO DESENHADOS AQUI. Eram cubos texturizados - um Painel Solar aparecia
                // como caixa azul listrada ("nao parece um painel solar"). Agora cada um tem geometria
                // propria em module_models.cpp, desenhada num passe separado (mesmo arranjo do
                // exterior da base: o bloco no mundo continua sendo colisao/ancora, a aparencia vem
                // de um modelo). O tile do modulo continua no mundo, so' nao virá cubo.
                if (obj != Block::Air && !is_invisible_collider(obj) && !is_module(obj) &&
                    dist2 <= obj_radius2) {
                    BlockTex tex = block_tex(obj);
                    if (tex.is_water) {
                        tex.top = (Tile)((int)Tile::Water0 + water_frame);
                        tex.side = tex.top;
                        tex.bottom = tex.top;
                    }

                    float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f, a = 1.0f;
                    if (tex.uses_tint || tex.transparent) {
                        float cr, cg, cb, ca;
                        block_color(obj, tz, g_world->h, cr, cg, cb, ca);
                        if (tex.uses_tint) { tint_r = cr; tint_g = cg; tint_b = cb; }
                        if (tex.transparent) a = ca;
                    }
                    a *= camera_occluder_alpha_for_tile(tx, tz);
                    
                    // === ILUMINACAO 2D PARA OBJETOS (RTX FAKE) ===
                    if (g_lighting.enabled) {
                        float light_r, light_g, light_b;
                        sample_lightmap((float)tx, (float)tz, light_r, light_g, light_b);
                        
                        // Objetos emissivos (modulos, cristais) recebem boost de luz
                        bool is_emissive = is_module(obj) || obj == Block::Crystal;
                        if (is_emissive) {
                            light_r = std::max(light_r, 0.7f);
                            light_g = std::max(light_g, 0.7f);
                            light_b = std::max(light_b, 0.7f);
                        }
                        
                        // Escurecimento por profundidade
                        float depth_factor = compute_depth_factor(base_y, rpy);
                        light_r *= depth_factor;
                        light_g *= depth_factor;
                        light_b *= depth_factor;
                        
                        tint_r *= light_r;
                        tint_g *= light_g;
                        tint_b *= light_b;
                        
                        apply_color_grading(tint_r, tint_g, tint_b);
                    }

                    if (obj == Block::Leaves) {
                        // Folhas como plano elevado acima do terreno
                        float leaf_y = base_y + 0.60f;
                        if (use_textures) render_plane_3d_tex(world_x, leaf_y, world_z, 1.0f, tex.top, tint_r, tint_g, tint_b, a);
                        else render_plane_3d(world_x, leaf_y, world_z, 1.0f, tint_r, tint_g, tint_b, 0.85f);
                    } else if (obj == Block::Water) {
                        // Agua como plano levemente abaixo, com animacao
                        float water_y = base_y - 0.18f + 0.05f * std::sin(g_day_time * 2.0f + world_x * 0.5f + world_z * 0.3f);
                        if (use_textures) render_plane_3d_tex(world_x, water_y, world_z, 1.0f, tex.top, tint_r, tint_g, tint_b, a);
                        else render_plane_3d(world_x, water_y, world_z, 1.0f, tint_r, tint_g, tint_b, 0.75f);
                    } else {
                        bool use_outline = is_module(obj) || (obj == Block::Crystal || obj == Block::Coal || obj == Block::Iron || obj == Block::Copper);
                        float center_y = base_y + 0.5f;
                        if (use_textures) render_cube_3d_tex(world_x, center_y, world_z, 1.0f, tex.top, tex.side, tex.bottom, tint_r, tint_g, tint_b, a, use_outline);
                        else render_cube_3d(world_x, center_y, world_z, 1.0f, tint_r, tint_g, tint_b, a, use_outline);
                    }
                }

                // === PILHA DE BLOCOS CONSTRUIDOS (empilhamento - torres/paredes/volumes) ===
                // Aditivo sobre o "obj" unico acima: um cubo por camada, com Y incremental a partir
                // do topo do que ja existia (terreno, ou o obj unico se houver).
                //
                // CULLING DE FACE OCULTA: cada camada agora recebe uma mascara de faces (ver
                // kFaceTop... acima de render_cube_3d_tex). Antes as 6 faces saiam sempre, o que
                // tornava volume MACICO proibitivo - e volume macico e' justamente o que impede o
                // jetpack de invadir a base pelo teto (ver interiors.h). Uma face lateral e' pulada
                // quando a coluna vizinha tem, NAQUELA altura, um bloco opaco; o topo, quando ha
                // outra camada opaca em cima. O fundo e' pulado sempre: a camada 0 assenta no topo
                // do terreno desta coluna e as outras assentam na camada de baixo, entao o fundo
                // nunca fica visivel.
                int stack_h = g_world->stack_height_at(tx, tz);
                if (stack_h > 0 && dist2 <= obj_radius2) {
                    float stack_base_y = base_y + get_block_height(obj);

                    // Perfil vertical de uma coluna vizinha, pra decidir se ela tapa uma camada
                    // desta. Devolve o bloco que ocupa a altura `probe_y` (Air = nada ali).
                    auto neighbor_block_at_height = [&](int nx, int nz, float probe_y) -> Block {
                        if (!g_world->in_bounds(nx, nz)) return Block::Air;
                        int nsh = g_world->stack_height_at(nx, nz);
                        if (nsh <= 0) return Block::Air;
                        Block nobj = object_block_at(*g_world, nx, nz);
                        float nbase = (float)g_world->height_at(nx, nz) * kHeightScale +
                                      get_block_height(nobj);
                        int idx = (int)std::floor(probe_y - nbase + 0.5f);
                        if (idx < 0 || idx >= nsh) return Block::Air;
                        return g_world->stack_block_at(nx, nz, idx);
                    };
                    // Opaco pra fins de culling. Um vizinho de vidro (DomeGlass) NAO tapa - senao a
                    // parede atras dele desapareceria e se veria o vazio pelo vidro. Mobilia
                    // tambem nao: e' invisivel de proposito, entao nao pode tapar nada.
                    auto occludes = [&](Block nb) -> bool {
                        if (nb == Block::Air || is_invisible_collider(nb)) return false;
                        BlockTex nt = block_tex(nb);
                        return !nt.transparent && !nt.is_water;
                    };

                    for (int layer = 0; layer < stack_h; ++layer) {
                        Block sb = g_world->stack_block_at(tx, tz, layer);
                        // is_furniture_collider: invisivel de proposito, ver o loop de objetos acima.
                        if (sb == Block::Air || is_invisible_collider(sb)) continue;

                        BlockTex stex = block_tex(sb);
                        if (stex.is_water) {
                            stex.top = (Tile)((int)Tile::Water0 + water_frame);
                            stex.side = stex.top;
                            stex.bottom = stex.top;
                        }

                        float stint_r = 1.0f, stint_g = 1.0f, stint_b = 1.0f, sa = 1.0f;
                        if (stex.uses_tint || stex.transparent) {
                            float cr, cg, cb, ca;
                            block_color(sb, tz, g_world->h, cr, cg, cb, ca);
                            if (stex.uses_tint) { stint_r = cr; stint_g = cg; stint_b = cb; }
                            if (stex.transparent) sa = ca;
                        }
                        sa *= camera_occluder_alpha_for_tile(tx, tz);

                        if (g_lighting.enabled) {
                            float light_r, light_g, light_b;
                            sample_lightmap((float)tx, (float)tz, light_r, light_g, light_b);
                            bool is_emissive = is_module(sb) || sb == Block::Crystal;
                            if (is_emissive) {
                                light_r = std::max(light_r, 0.7f);
                                light_g = std::max(light_g, 0.7f);
                                light_b = std::max(light_b, 0.7f);
                            }
                            float depth_factor = compute_depth_factor(stack_base_y + (float)layer, rpy);
                            light_r *= depth_factor;
                            light_g *= depth_factor;
                            light_b *= depth_factor;
                            stint_r *= light_r;
                            stint_g *= light_g;
                            stint_b *= light_b;
                            apply_color_grading(stint_r, stint_g, stint_b);
                        }

                        float scenter_y = stack_base_y + (float)layer * 1.0f + 0.5f;

                        // Mascara: fundo nunca; topo so' se nao houver camada opaca em cima; cada
                        // lateral so' se a coluna vizinha nao a tapar naquela altura.
                        uint8_t faces = kFaceAll & ~kFaceBottom;
                        if (layer + 1 < stack_h && occludes(g_world->stack_block_at(tx, tz, layer + 1)))
                            faces &= ~kFaceTop;
                        if (occludes(neighbor_block_at_height(tx, tz + 1, scenter_y))) faces &= ~kFaceZPos;
                        if (occludes(neighbor_block_at_height(tx, tz - 1, scenter_y))) faces &= ~kFaceZNeg;
                        if (occludes(neighbor_block_at_height(tx - 1, tz, scenter_y))) faces &= ~kFaceXNeg;
                        if (occludes(neighbor_block_at_height(tx + 1, tz, scenter_y))) faces &= ~kFaceXPos;
                        // Cubo totalmente interno ao volume: nao gera nem um quad. E' o que faz um
                        // modulo macico custar a casca, nao o volume.
                        if (faces == 0) continue;

                        bool suse_outline = is_module(sb) || (sb == Block::Crystal || sb == Block::Coal || sb == Block::Iron || sb == Block::Copper);
                        if (use_textures) render_cube_3d_tex(world_x, scenter_y, world_z, 1.0f, stex.top, stex.side, stex.bottom, stint_r, stint_g, stint_b, sa, suse_outline, faces);
                        else render_cube_3d(world_x, scenter_y, world_z, 1.0f, stint_r, stint_g, stint_b, sa, suse_outline);
                    }
                }
            }
        }
    }

    // Terreno distante (alem de near_radius, ate view_radius) - malha cacheada por chunk em
    // vez de modo imediato (ver terrain_mesh.h/.cpp). Precisa do atlas ja ligado (esta' desde
    // a linha "rlSetTexture(g_tex_atlas)" no topo deste bloco) - so roda com use_textures,
    // mesma condicao do resto do loop acima.
    g_debug_far_chunks_drawn = use_textures ? terrain_mesh_render_far(player_tile_x, player_tile_z, near_radius, view_radius) : 0;
    if (use_textures) rlSetTexture(g_tex_atlas); // DrawMesh troca o shader ativo - reafirma o atlas pro resto do frame

    // Drops coletaveis
    if (use_textures && !g_drops.empty()) {
        for (size_t di = 0; di < g_drops.size(); ++di) {
            const auto& d = g_drops[di];
            // Culling simples no grid visivel
            if (d.x < (float)start_x - 2.0f || d.x >(float)end_x + 2.0f ||
                d.z < (float)start_z - 2.0f || d.z >(float)end_z + 2.0f) continue;

            BlockTex tex = block_tex(d.item);
            if (tex.is_water) {
                tex.top = (Tile)((int)Tile::Water0 + water_frame);
                tex.side = tex.top;
                tex.bottom = tex.top;
            }

            float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f, a = 1.0f;
            if (tex.uses_tint || tex.transparent) {
                float cr, cg, cb, ca;
                block_color(d.item, (int)d.z, g_world->h, cr, cg, cb, ca);
                if (tex.uses_tint) { tint_r = cr; tint_g = cg; tint_b = cb; }
                if (tex.transparent) a = ca;
            }
            
            // Iluminacao 2D para drops
            if (g_lighting.enabled) {
                float light_r, light_g, light_b;
                sample_lightmap(d.x, d.z, light_r, light_g, light_b);
                tint_r *= light_r;
                tint_g *= light_g;
                tint_b *= light_b;
                apply_color_grading(tint_r, tint_g, tint_b);
            }

            bool aimed = ((int)di == g_target_drop);
            float bob = 0.03f * std::sin(d.t * 4.0f);
            float size = aimed ? 0.42f : 0.34f;
            float aa = aimed ? 1.0f : a;
            render_cube_3d_tex(d.x, d.y + bob, d.z, size, tex.top, tex.side, tex.bottom, tint_r, tint_g, tint_b, aa, true);
        }
    }

    if (use_textures) {
        rlSetTexture(rlGetTextureIdDefault());   // NAO rlSetTexture(0): pra id 0 o rlgl nao troca nada
    }

    // Meteoro raro caindo (ver update_meteors/FallingMeteor) - cubo incandescente
    // interpolando do alto do ceu ate o chao, agora com rastro/entrada atmosferica (antes
    // era so um cubo "teleportando" sem sensacao nenhuma de movimento/velocidade).
    for (const auto& m : g_meteors) {
        float u = clamp01(m.t / m.duration);
        float y = lerp(m.start_y, m.target_y, u);
        float glow = 0.85f + 0.15f * std::sin(m.t * 20.0f);

        // Rastro incandescente (posicao um pouco atras, pela mesma trajetoria) + halo
        // aditivo ao redor do nucleo - da sensacao real de entrada atmosferica em alta
        // velocidade, nao um cubo teleportando de posicao em posicao.
        float trail_u = clamp01(u - 0.06f);
        float trail_y = lerp(m.start_y, m.target_y, trail_u);
        rlSetTexture(rlGetTextureIdDefault());   // NAO rlSetTexture(0): pra id 0 o rlgl nao troca nada
        rlSetBlendMode(RL_BLEND_ADDITIVE);
        rlDisableDepthMask();
        render_beam_3d({m.x, trail_y, m.z}, {m.x, y, m.z}, 0.30f, 1.0f, 0.55f, 0.15f, 0.55f);
        render_glow_disc_3d({m.x, y, m.z}, 0.70f, 1.0f, 0.62f, 0.22f, 0.45f, 12);
        rlEnableDepthMask();
        rlSetBlendMode(RL_BLEND_ALPHA);

        render_cube_3d(m.x, y, m.z, 0.55f, 1.0f * glow, 0.55f * glow, 0.15f * glow, 1.0f, true);
    }

    // Criaturas alienigenas perambulantes (ver update_creatures/render_creatures, creatures.cpp).
    render_creatures();

    // Interior/anexos decorativos da base (mobilia, terminais, luminarias, tubulacao, marcacoes de
    // piso, arco do corredor, abobada da estufa, canteiros e plantas) - ver base_interior.h.
    // Aqui, e nao junto da cupula la em cima: precisa vir DEPOIS do loop de terreno (a geometria e'
    // opaca e testa profundidade contra o chao/paredes) e depois de compute_lightmap()/g_frame_fog,
    // que ela consome. ANTES do jogador de proposito: o vidro da estufa desenha com depth mask
    // desligada, entao quem esta atras dele continua aparecendo.
    // Efeitos de agua/vapor (respingo ao encher buraco, vapor ao apagar lava - ver world.h).
    render_water_fx();

    render_base_interior();

    // TEXTURA BRANCA PADRAO antes do jogador. O corpo e' desenhado com render_cube_3d/
    // render_sphere_3d, que NAO emitem rlTexCoord2f: se o atlas continuar ligado, cada vertice usa a
    // coordenada de textura RESIDUAL do ultimo desenho texturizado e o personagem inteiro sai
    // multiplicado por um texel arbitrario do atlas - as vezes escuro, dai "de noite o boneco fica
    // escuro". Os rlSetTexture(0) logo acima NAO resolvem: pra id 0 o rlgl nao troca nada (ja
    // documentado em render_cube_3d_tex neste arquivo). E render_base_interior(), que terminava
    // resetando a textura, agora RETORNA CEDO quando o jogador esta longe do distrito - entao nao da
    // pra depender dele.
    rlSetTexture(rlGetTextureIdDefault());

    // === RENDERIZAR PLAYER 3D (Estilo Minicraft - Blocky) ===
    {
        float px = rpos.x;
        float pz = rpos.y;  // Y do 2D = Z no 3D

        int surf_tx = world_to_tile(px);
        int surf_tz = world_to_tile(pz);
        Block surf = Block::Dirt;
        if (g_world && g_world->in_bounds(surf_tx, surf_tz)) {
            surf = surface_block_at(*g_world, surf_tx, surf_tz);
        }

        // OFFSET PARA ELEVAR O JOGADOR ACIMA DO SOLO (evita pes afundados); nadando, o
        // personagem afunda parcialmente na agua em vez de flutuar por cima dela (o piso de
        // colisao continua sendo o fundo do lago - so este offset visual muda).
        // Le g_physics.in_water (fonte unica, calculada COM altura na fisica) em vez de
        // "surf == Block::Water": aquele teste e' verdadeiro em QUALQUER altitude sobre agua,
        // entao voando sobre o mar o corpo era desenhado 0.57 afundado e SEM sombra.
        bool swimming = g_physics.in_water;
        float player_y_offset = swimming ? -0.42f : 0.15f;
        float py = rpy + player_y_offset;  // Altura real + offset

        // Indicador de perigo (player pisca vermelho quando HP ou O2 baixo)
        bool in_danger = (g_player.hp < 30 || g_player_oxygen < 20.0f);
        float danger_pulse = in_danger ? (0.5f + 0.5f * std::sin(g_player.anim_frame * 8.0f)) : 0.0f;

        // Sombra no chao (maior e mais visivel)
        rlDisableDepthTest();
        if (!swimming) render_plane_3d(px, g_player.ground_height + 0.02f, pz, 0.9f, 0.0f, 0.0f, 0.0f, 0.55f);

        // Circulo de indicador de perigo
        if (in_danger) {
            render_plane_3d(px, g_player.ground_height + 0.03f, pz, 1.2f,
                kColorDanger[0], kColorDanger[1], kColorDanger[2], danger_pulse * 0.3f);
        }
        rlEnableDepthTest();

        // Usar rotacao continua para orientar o personagem
        float rot_rad = get_player_render_rotation() * (kPi / 180.0f);
        float sin_rot = std::sin(rot_rad);
        float cos_rot = std::cos(rot_rad);

        float temp_cold = smoothstep01(-35.0f, -65.0f, g_temperature);
        float frost = temp_cold * g_player_visual_cfg.suit_frost_strength;
        if (surf == Block::Snow || surf == Block::Ice) frost = std::min(1.0f, frost + 0.26f);
        float dirt = 0.0f;
        if (surf == Block::Dirt || surf == Block::Sand || surf == Block::Stone) dirt = g_player_visual_cfg.suit_dirt_strength;
        float damage = clamp01((100.0f - (float)g_player.hp) / 100.0f) * g_player_visual_cfg.suit_damage_strength;
        float wear = g_player_visual_cfg.suit_wear_strength;

        // Animacao de movimento (andar com peso + respiracao + idle).
        float breath = std::sin(g_player.anim_frame * g_player_visual_cfg.breathing_speed) * g_player_visual_cfg.breathing_amp;
        float idle_sway = std::sin(g_player.anim_frame * g_player_visual_cfg.idle_sway_speed) * g_player_visual_cfg.idle_sway_amp;
        float walk_wave = std::sin(g_player.walk_timer * g_player_visual_cfg.walk_bob_speed);
        float walk_bob = walk_wave * g_player_visual_cfg.walk_bob_amp * g_player.walk_blend;
        float walk_weight = std::fabs(std::sin(g_player.walk_timer * 0.5f * g_player_visual_cfg.walk_bob_speed)) * g_player_visual_cfg.walk_weight_amp * g_player.walk_blend;
        float mine_impact = g_player.is_mining ? g_player.mine_anim : 0.0f;
        float bob = breath + walk_bob - walk_weight - mine_impact * 0.06f;
        float leg_swing = walk_wave * 0.12f * g_player.walk_blend;

        float suit_r = 0.92f - wear * 0.12f - dirt * 0.16f - damage * 0.18f + frost * 0.12f;
        float suit_g = 0.93f - wear * 0.11f - dirt * 0.14f - damage * 0.17f + frost * 0.12f;
        float suit_b = 0.96f - wear * 0.08f - dirt * 0.10f - damage * 0.12f + frost * 0.16f;
        suit_r = clamp01(suit_r);
        suit_g = clamp01(suit_g);
        suit_b = clamp01(suit_b);
        
        // === CHAMA DO JETPACK (renderizar primeiro, atras do jogador) ===
        // Sem "&& jetpack_fuel > 0" de proposito: a rajada de pouso automatica
        // (landing_assist_active, ver player_physics.cpp) liga jetpack_active mesmo com
        // combustivel zerado (e' um mecanismo de seguranca independente de combustivel, nao
        // impulso manual) - a checagem extra de combustivel escondia o foguinho bem na hora
        // que o jogador mais esperava ve-lo (pousando sem combustivel), bug reportado.
        // jetpack_active ja e' a condicao certa sozinho (so fica true por impulso manual COM
        // combustivel OU pela rajada de pouso, nunca à toa).
        if (g_player.jetpack_active) {
            // A chama sai dos DOIS BOCAIS, nas coordenadas exatas em que a mochila os desenha
            // (ver o bloco MOCHILA A JATO mais abaixo: pack_dist 0.255 + profundidade 0.09,
            // lateral +/-0.105). Antes saia de UM ponto central um pouco a frente e acima deles -
            // a chama nao encostava em bocal nenhum e lia como fogo saindo das costas.
            const float pack_dist = 0.255f + 0.09f;
            const float nozzle_side = 0.105f;
            const float nzl_perp_x = cos_rot, nzl_perp_z = -sin_rot;
            for (float nsgn : {-1.0f, 1.0f}) {
            float flame_x = px - sin_rot * pack_dist + nzl_perp_x * (nozzle_side * nsgn);
            float flame_z = pz - cos_rot * pack_dist + nzl_perp_z * (nozzle_side * nsgn);

            // Rajada de pouso: chama bem maior/mais intensa e mais branca (nucleo mais
            // quente) que o voo manual normal, pra deixar visivel que esta freando de
            // verdade (pedido do jogador) - mais camadas tambem, nao so maior.
            bool braking = g_player.landing_assist_active;
            float flame_boost = braking ? 2.0f : 1.0f;

            // Animacao da chama (flicker)
            float flame_flicker = (0.8f + 0.4f * std::sin(g_player.jetpack_flame_anim * 2.0f)) * flame_boost;
            // x0.72: sao DUAS chamas agora (uma por bocal); com o tamanho antigo o volume total
            // dobrava e virava uma bola de fogo atras do personagem.
            float flame_size = (0.15f + 0.05f * std::sin(g_player.jetpack_flame_anim * 3.0f)) * flame_boost * 0.72f;

            // Glow aditivo por baixo da chama (disco billboard, ver render_glow_disc_3d
            // adicionado nesta sessao pro flash da pistola/meteoro) - da uma luz/brilho de
            // verdade ao redor da chama em vez de so cubos solidos empilhados.
            rlSetTexture(0);
            rlSetBlendMode(RL_BLEND_ADDITIVE);
            rlDisableDepthMask();
            render_glow_disc_3d({flame_x, py + 0.02f, flame_z}, flame_size * (braking ? 2.2f : 1.6f),
                                 1.0f, braking ? 0.75f : 0.55f, 0.15f, 0.55f * flame_flicker, 10);
            rlEnableDepthMask();
            rlSetBlendMode(RL_BLEND_ALPHA);

            int flame_layers = braking ? 4 : 3;
            // Chama principal (laranja/amarela, quase branca no nucleo quando freando)
            for (int i = 0; i < flame_layers; ++i) {
                float flame_y = py + 0.10f - i * 0.15f;
                float size = flame_size * (1.0f - i * 0.2f);
                float intensity = flame_flicker * (1.0f - i * 0.18f);

                // Nucleo (branco-amarelado na rajada, so amarelo no voo normal)
                render_cube_3d(flame_x, flame_y, flame_z, size * 0.6f,
                    1.0f * intensity, (braking ? 0.98f : 0.95f) * intensity, (braking ? 0.70f : 0.3f) * intensity,
                    0.95f, false);
                // Chama laranja
                render_cube_3d(flame_x, flame_y - 0.08f, flame_z, size * 0.8f,
                    1.0f * intensity, 0.55f * intensity, 0.1f * intensity, 0.85f, false);
                // Borda vermelha
                render_cube_3d(flame_x, flame_y - 0.15f, flame_z, size,
                    0.95f * intensity, 0.25f * intensity, 0.05f * intensity, 0.7f, false);
            }

            // Particulas de fogo (pequenos cubos caindo) - mais e maiores na rajada
            int fire_particles = braking ? 8 : 4;
            for (int i = 0; i < fire_particles; ++i) {
                float particle_offset = std::sin(g_player.jetpack_flame_anim * 5.0f + i * 1.5f) * 0.08f * flame_boost;
                float particle_y = py - 0.1f - std::fmod(g_player.jetpack_flame_anim * 0.5f + i * 0.25f, 0.5f);
                float alpha = 0.8f - std::fmod(g_player.jetpack_flame_anim * 0.5f + i * 0.25f, 0.5f) * 1.5f;
                if (alpha > 0.0f) {
                    render_cube_3d(flame_x + particle_offset, particle_y, flame_z + particle_offset * 0.5f,
                        0.05f * flame_boost, 1.0f, 0.6f, 0.1f, alpha, false);
                }
            }
            }   // fim do loop dos 2 bocais
        }
        
        // === ONDA DE CHOQUE DE POUSO (ver g_physics.landing_dust_timer/landing_dust_pos,
        // disparado em apply_single_physics_step quando a rajada instantanea liga).
        // Layout novo (2a reformulacao - nem cor nem "nuvem espalhada" de cubos convenceu):
        // um disco/anel de fragmentos ACHATADOS (render_plane_3d - planos sem altura
        // nenhuma, nao cubos) rente ao chao, que se expande rapido pra fora feito uma onda
        // de impacto, com o miolo preenchido (senao vira uma rosquinha vazia por dentro).
        // Fumaca so um resto discreto subindo (poucos fios, pouca altura) - o foco e a
        // onda no chao, nao algo subindo, matching "poeira baixa e larga, sem nada subindo
        // muito" (layout pedido pelo usuario).
        if (g_physics.landing_dust_timer > 0.0f) {
            constexpr float kLandingDustDuration = 1.1f; // deve bater com player_physics.cpp
            float t = 1.0f - clamp01(g_physics.landing_dust_timer / kLandingDustDuration);
            Vec3 base = g_physics.landing_dust_pos;

            // Hash pseudo-aleatorio deterministico (mesmo indice sempre da o mesmo valor -
            // aleatoriedade de verdade piscaria frame a frame).
            auto hash01 = [](int i, float salt) -> float {
                float x = std::sin((float)i * 12.9898f + salt * 78.233f) * 43758.5453f;
                return x - std::floor(x);
            };

            // Escala pelo tamanho da queda (ver g_physics.landing_dust_intensity,
            // calculado em apply_single_physics_step a partir da velocidade de impacto no
            // instante da rajada) - um pulo pequeno da so um respingo, uma queda de
            // verdade da uma onda bem maior. Piso de 0.55 (nao 0, ja que a intensidade
            // crua ja tem piso 0.25) pra um pulo pequeno ainda deixar algo visivel.
            float scale = 0.55f + 0.45f * g_physics.landing_dust_intensity;

            // --- Anel da onda: se expande rapido (kWaveLife curto) e some - o "estouro"
            // do impacto, nao uma nuvem persistente. ---
            const float kWaveLife = 0.45f;
            float wave_t = clamp01(t / kWaveLife);
            if (wave_t < 1.0f) {
                float wave_radius = (0.20f + wave_t * 2.3f) * scale;
                float wave_alpha = (1.0f - wave_t) * 0.90f;
                float wave_y = base.y + 0.02f; // colado no chao
                const int kWaveChunks = 20;
                for (int i = 0; i < kWaveChunks; ++i) {
                    float ang = ((float)i / (float)kWaveChunks) * 6.2831853f + hash01(i, 1.0f) * 0.35f;
                    float r = wave_radius * (0.85f + hash01(i, 2.0f) * 0.30f);
                    float dx = std::cos(ang) * r;
                    float dz = std::sin(ang) * r;
                    float size = (0.22f + hash01(i, 3.0f) * 0.18f) * (1.0f - wave_t * 0.3f) * scale;
                    float shade = 0.74f + hash01(i, 4.0f) * 0.18f;
                    render_plane_3d(base.x + dx, wave_y, base.z + dz, size,
                        shade, shade * 0.60f, shade * 0.36f, wave_alpha);
                }
            }

            // --- Fumaca branca se espalhando JUNTO com a onda (nao so subindo parada no
            // lugar) - mesma ideia da onda de poeira, um pouco mais alta/mais lenta pra
            // durar um tico mais e ler como fumaca por cima da poeira, nao afogada nela. ---
            const float kSmokeWaveLife = kWaveLife * 1.7f;
            float smoke_wave_t = clamp01(t / kSmokeWaveLife);
            if (smoke_wave_t < 1.0f) {
                float smoke_wave_radius = (0.15f + smoke_wave_t * 1.9f) * scale;
                float smoke_wave_alpha = (1.0f - smoke_wave_t) * 0.55f;
                const int kSmokeWaveChunks = 16;
                for (int i = 0; i < kSmokeWaveChunks; ++i) {
                    float ang = ((float)i / (float)kSmokeWaveChunks) * 6.2831853f + hash01(i, 30.0f) * 0.4f;
                    float r = smoke_wave_radius * (0.7f + hash01(i, 31.0f) * 0.45f);
                    float dx = std::cos(ang) * r;
                    float dz = std::sin(ang) * r;
                    float smoke_wave_y = base.y + 0.06f + smoke_wave_t * 0.30f; // sobe um pouco enquanto se espalha
                    float size = (0.24f + hash01(i, 32.0f) * 0.18f) * (0.7f + smoke_wave_t * 0.5f) * scale;
                    float shade = 0.92f + hash01(i, 33.0f) * 0.08f;
                    render_cube_3d(base.x + dx, smoke_wave_y, base.z + dz, size,
                        shade, shade, shade, smoke_wave_alpha, false);
                }
            }

            // --- Miolo: poeira baixa preenchendo o centro por um pouco mais de tempo, pra
            // nao sobrar um buraco vazio dentro do anel enquanto ele se expande/some. ---
            float core_t = clamp01(t / 0.75f);
            float core_alpha = (1.0f - core_t) * 0.55f;
            const int kCorePuffs = 12;
            for (int i = 0; i < kCorePuffs; ++i) {
                float ang = hash01(i, 5.0f) * 6.2831853f;
                float r = hash01(i, 6.0f) * (0.25f + core_t * 1.0f) * scale;
                float dx = std::cos(ang) * r;
                float dz = std::sin(ang) * r;
                float size = (0.20f + hash01(i, 7.0f) * 0.14f) * scale;
                float shade = 0.70f + hash01(i, 8.0f) * 0.16f;
                render_plane_3d(base.x + dx, base.y + 0.015f, base.z + dz, size,
                    shade, shade * 0.58f, shade * 0.34f, core_alpha);
            }

            // --- Fumaca: poucos fios discretos subindo pouco - resto secundario, nao o
            // foco (o foco e a onda no chao). ---
            const int kSmokeWisps = 4;
            for (int i = 0; i < kSmokeWisps; ++i) {
                float phase = hash01(i, 20.0f) * 0.3f;
                float local_t = clamp01((t - phase) / (1.0f - phase));
                if (local_t <= 0.0f) continue;
                float ang = hash01(i, 21.0f) * 6.2831853f;
                float r = (0.15f + local_t * 0.40f) * scale;
                float dx = std::cos(ang) * r;
                float dz = std::sin(ang) * r;
                float smoke_y = base.y + 0.15f + local_t * 0.55f; // sobe pouco de proposito
                float smoke_size = (0.14f + local_t * 0.16f) * scale;
                float smoke_shade = 0.92f + hash01(i, 22.0f) * 0.08f;
                float smoke_alpha = (1.0f - local_t) * (1.0f - local_t) * 0.55f;
                render_cube_3d(base.x + dx, smoke_y, base.z + dz,
                    smoke_size, smoke_shade, smoke_shade, smoke_shade, smoke_alpha, false);
            }
        }

        // === CORPO (torso branco do astronauta - elipsoide, nao mais cubo. "personagem
        // muito quadrado", pedido do jogador: um torso e' mais alto/fundo que largo, entao
        // scale_y>1 (mais alto) e scale_z<1 (um pouco mais fino de frente pra tras) em vez de
        // uma esfera perfeita) ===
        render_sphere_3d(px, py + 0.30f + bob, pz, 0.24f, suit_r, suit_g, suit_b, 1.0f, 8, 12, 1.35f, 0.95f);

        // Ombros arredondados (2 esferas pequenas nos cantos superiores do torso) - quebra a
        // silhueta reta do cubo do torso ("personagem muito quadrado", pedido do jogador) sem
        // precisar redesenhar o traje inteiro.
        // Raio+offset pequenos de proposito: um raio de 0.15 a 0.20 do centro deixava a
        // esfera espiando bem alem da lateral do torso (meia-largura 0.225), lendo como
        // "gordinho"/ombreira inchada em vez de so' arredondar o canto - jogador reportou
        // isso depois do primeiro ajuste. Com raio 0.09 a 0.15 do centro, a borda da esfera
        // (0.15+0.09=0.24) fica só um pouco alem da lateral do torso, um arredondado sutil.
        float shoulder_side = 0.15f;
        // perp_x/perp_z de verdade (usado por tubos/mochila/painel) so' e' declarado mais
        // abaixo nesta funcao - mesma formula (perpendicular a direcao que o jogador olha),
        // calculada aqui de novo em vez de mover a declaracao original.
        float shoulder_perp_x = cos_rot, shoulder_perp_z = -sin_rot;
        render_sphere_3d(px - shoulder_perp_x * shoulder_side, py + 0.46f + bob, pz - shoulder_perp_z * shoulder_side,
                          0.09f, suit_r * 0.94f, suit_g * 0.94f, suit_b * 0.96f, 1.0f);
        render_sphere_3d(px + shoulder_perp_x * shoulder_side, py + 0.46f + bob, pz + shoulder_perp_z * shoulder_side,
                          0.09f, suit_r * 0.94f, suit_g * 0.94f, suit_b * 0.96f, 1.0f);

        // === CABECA (Capacete redondo de verdade, nao mais um cubo - mesmo pedido acima:
        // um capacete de astronauta e' a peca mais obviamente "deveria ser redonda" do
        // personagem) ===
        render_sphere_3d(px, py + 0.68f + bob, pz, 0.21f, suit_r * 0.98f, suit_g * 0.98f, suit_b, 1.0f);

        // Visor (bloco azul na frente da cabeca - vidro plano encaixado no capacete redondo,
        // continua reto de proposito, visor de capacete de verdade e' uma placa plana).
        float visor_dist = 0.12f;
        float vx = px + sin_rot * visor_dist;
        float vz = pz + cos_rot * visor_dist;
        render_cube_3d(vx, py + 0.68f + bob, vz, 0.22f, 0.10f, 0.35f, 0.75f, 0.95f, false);

        // Reflexo no visor.
        float refl_x = vx + sin_rot * 0.03f + cos_rot * 0.03f;
        float refl_z = vz + cos_rot * 0.03f - sin_rot * 0.03f;
        render_cube_3d(refl_x, py + 0.73f + bob, refl_z, 0.08f, 0.95f, 0.98f, 1.0f, g_player_visual_cfg.visor_reflect_alpha, false);
        
        // === MOCHILA A JATO ===
        // 3a versao. A 1a era UM cubo cinza ("esta so um quadrado cinza"); a 2a virou um conjunto,
        // mas TODAS as pecas ainda eram cubos - render_cube_3d so' aceita um `size` unico, e cubos
        // posicionados por sin/cos continuam alinhados aos EIXOS DO MUNDO, entao a mochila ficava
        // torta quando o personagem olhava na diagonal. Agora usa render_box_oriented_3d
        // (render_primitives.h, adicionado pra isto): dimensoes independentes e giro em torno de Y,
        // igual ao resto do corpo. Isso e' o que permite chassi achatado, ombreiras, aletas e bocais
        // com proporcao de equipamento em vez de bloquinhos.
        {
            const float yaw = std::atan2(sin_rot, cos_rot);   // mesma orientacao do corpo
            const float perp_x = cos_rot, perp_z = -sin_rot;  // "direita" do personagem
            const float pack_dist = 0.255f;
            const float pack_x = px - sin_rot * pack_dist;
            const float pack_z = pz - cos_rot * pack_dist;
            const float pack_y = py + 0.36f + bob;
            const bool  active = g_player.jetpack_active;
            const float fuel = clamp01(g_player.jetpack_fuel / 100.0f);

            // Ponto no referencial da mochila: (lado, altura, profundidade) -> mundo.
            auto P = [&](float side, float up, float depth) {
                return Vec3{pack_x + perp_x * side - sin_rot * depth,
                            pack_y + up,
                            pack_z + perp_z * side - cos_rot * depth};
            };

            // --- 1) Placa de encosto: fina e larga, colada nas costas. E' ela que faz a mochila
            //        parecer VESTIDA em vez de um bloco flutuando atras do torso. ---
            render_box_oriented_3d(P(0.0f, 0.02f, -0.01f), 0.34f, 0.30f, 0.06f, yaw,
                                   0.62f, 0.63f, 0.66f, 1.0f);

            // --- 2) Chassi principal: ACHATADO (largo e raso), com uma tampa mais clara em cima. ---
            float body_r = active ? 0.86f : 0.80f;
            float body_g = active ? 0.83f : 0.81f;
            float body_b = active ? 0.79f : 0.84f;
            render_box_oriented_3d(P(0.0f, 0.00f, 0.085f), 0.30f, 0.34f, 0.16f, yaw,
                                   body_r, body_g, body_b, 1.0f);
            render_box_oriented_3d(P(0.0f, 0.185f, 0.085f), 0.32f, 0.04f, 0.18f, yaw,
                                   0.62f, 0.64f, 0.68f, 1.0f);
            // Grade de ventilacao: 3 ripas horizontais na face de tras.
            for (int k = 0; k < 3; ++k) {
                render_box_oriented_3d(P(0.0f, 0.06f - (float)k * 0.055f, 0.17f),
                                       0.20f, 0.022f, 0.02f, yaw, 0.34f, 0.35f, 0.38f, 1.0f);
            }

            // --- 3) 2 tanques cilindricos, com cinta escura no meio e valvula em cima. A capsula
            //        (esfera esticada) le como cilindro melhor que qualquer pilha de caixas. ---
            for (float sgn : {-1.0f, 1.0f}) {
                Vec3 t = P(0.145f * sgn, 0.015f, 0.11f);
                render_sphere_3d(t.x, t.y, t.z, 0.075f, 0.68f, 0.70f, 0.74f, 1.0f, 6, 9, 2.15f, 1.0f);
                // Cinta de reforco.
                render_box_oriented_3d({t.x, t.y, t.z}, 0.17f, 0.032f, 0.17f, yaw,
                                       0.26f, 0.27f, 0.30f, 1.0f);
                // Valvula + saida.
                render_box_oriented_3d({t.x, t.y + 0.165f, t.z}, 0.07f, 0.05f, 0.07f, yaw,
                                       0.34f, 0.36f, 0.40f, 1.0f);
                render_box_oriented_3d({t.x, t.y + 0.20f, t.z}, 0.035f, 0.035f, 0.035f, yaw,
                                       0.72f, 0.60f, 0.24f, 1.0f);
            }

            // --- 4) Coletor: tubo horizontal ligando as valvulas dos 2 tanques. ---
            render_box_oriented_3d(P(0.0f, 0.185f, 0.11f), 0.30f, 0.045f, 0.045f, yaw,
                                   0.40f, 0.42f, 0.46f, 1.0f);

            // --- 5) Aletas de dissipacao nas laterais: 2 de cada lado, finas e salientes. ---
            for (float sgn : {-1.0f, 1.0f}) {
                for (int k = 0; k < 2; ++k) {
                    render_box_oriented_3d(P(0.185f * sgn, 0.03f - (float)k * 0.09f, 0.075f),
                                           0.03f, 0.055f, 0.15f, yaw, 0.36f, 0.38f, 0.42f, 1.0f);
                }
            }

            // --- 6) BOCAIS: haste + sino flarado (3 aneis de largura crescente pra baixo) e um
            //        anel interno escuro. E' a peca que mais vende "propulsor". ---
            for (float sgn : {-1.0f, 1.0f}) {
                float side = 0.105f * sgn;
                render_box_oriented_3d(P(side, -0.185f, 0.09f), 0.075f, 0.07f, 0.075f, yaw,
                                       0.30f, 0.31f, 0.34f, 1.0f);
                const float bw[3] = {0.085f, 0.105f, 0.125f};
                for (int k = 0; k < 3; ++k) {
                    render_box_oriented_3d(P(side, -0.225f - (float)k * 0.035f, 0.09f),
                                           bw[k], 0.035f, bw[k], yaw,
                                           0.24f - (float)k * 0.03f, 0.25f - (float)k * 0.03f,
                                           0.28f - (float)k * 0.03f, 1.0f);
                }
                // Garganta: escura quando frio, incandescente quando ligado.
                float th = active ? 1.0f : 0.0f;
                render_box_oriented_3d(P(side, -0.30f, 0.09f), 0.075f, 0.022f, 0.075f, yaw,
                                       lerp(0.10f, 1.00f, th), lerp(0.10f, 0.62f, th),
                                       lerp(0.12f, 0.22f, th), 1.0f);
            }

            // --- 7) Alcas: 2 tiras passando por cima dos ombros e descendo pro peito. ---
            for (float sgn : {-1.0f, 1.0f}) {
                render_box_oriented_3d(P(0.115f * sgn, 0.145f, -0.06f), 0.05f, 0.05f, 0.20f, yaw,
                                       0.42f, 0.44f, 0.48f, 1.0f);
                render_box_oriented_3d(P(0.115f * sgn, 0.02f, -0.24f), 0.045f, 0.28f, 0.05f, yaw,
                                       0.42f, 0.44f, 0.48f, 1.0f);
            }

            // --- 8) Medidor de combustivel: barra que ENCURTA conforme o tanque baixa, sobre um
            //        trilho escuro. Le o estado real sem precisar olhar o HUD. ---
            {
                const float rail_w = 0.16f;
                render_box_oriented_3d(P(-0.02f, 0.115f, 0.175f), rail_w, 0.030f, 0.02f, yaw,
                                       0.10f, 0.11f, 0.12f, 1.0f);
                float lit = std::max(0.012f, rail_w * fuel);
                // Cresce a partir da ponta esquerda do trilho: centro desloca com o comprimento.
                float cx2 = -0.02f - (rail_w - lit) * 0.5f;
                bool low = fuel < 0.25f;
                render_box_oriented_3d(P(cx2, 0.115f, 0.185f), lit, 0.022f, 0.02f, yaw,
                                       low ? 0.95f : 0.25f, low ? 0.45f : 0.90f, low ? 0.15f : 0.40f, 1.0f);
            }

            // --- 9) Luz de estado: verde com combustivel, vermelha piscando quando acaba. ---
            {
                bool fuel_ok = g_player.jetpack_fuel > 15.0f;
                float pulse = fuel_ok ? 1.0f : (0.45f + 0.55f * std::sin(g_player.anim_frame * 9.0f));
                render_box_oriented_3d(P(0.10f, 0.115f, 0.185f), 0.035f, 0.035f, 0.02f, yaw,
                                       fuel_ok ? 0.20f : 0.95f, fuel_ok ? 0.90f : 0.20f, 0.25f, pulse);
            }

            // --- 10) Calor nos bocais quando ligado: halo aditivo pequeno em cada sino. Separado da
            //         chama grande (que sai de UM ponto central, mais abaixo) - isto e' o metal
            //         quente do bocal, e e' o que liga visualmente a mochila a chama. ---
            if (active) {
                rlSetTexture(0);
                rlSetBlendMode(RL_BLEND_ADDITIVE);
                rlDisableDepthMask();
                float hp2 = 0.7f + 0.3f * std::sin(g_player.jetpack_flame_anim * 7.0f);
                for (float sgn : {-1.0f, 1.0f}) {
                    Vec3 n = P(0.105f * sgn, -0.315f, 0.09f);
                    render_glow_disc_3d(n, 0.085f, 1.0f, 0.58f, 0.20f, 0.75f * hp2, 8);
                }
                rlEnableDepthMask();
                rlSetBlendMode(RL_BLEND_ALPHA);
            }
        }

        // Mangueiras de oxigenio: da lateral BAIXA da mochila, subindo por fora e entrando no peito.
        // Eram 2 cubos soltos de 0.07 a meia altura, sem ligar nada a nada. Agora sao 3 segmentos por
        // lado com render_box_oriented_3d, girando com o corpo - le como mangueira ligada de verdade.
        float perp_x = cos_rot;
        float perp_z = -sin_rot;
        {
            const float hyaw = std::atan2(sin_rot, cos_rot);
            auto HP = [&](float side, float up, float depth) {
                return Vec3{px + perp_x * side + sin_rot * depth, py + up + bob,
                            pz + perp_z * side + cos_rot * depth};
            };
            for (float sgn : {-1.0f, 1.0f}) {
                // 1) sai da mochila pra fora
                render_box_oriented_3d(HP(0.175f * sgn, 0.26f, -0.20f), 0.16f, 0.055f, 0.055f, hyaw,
                                       0.30f, 0.36f, 0.44f, 1.0f);
                // 2) sobe rente ao flanco
                render_box_oriented_3d(HP(0.235f * sgn, 0.33f, -0.06f), 0.055f, 0.055f, 0.30f, hyaw,
                                       0.32f, 0.38f, 0.46f, 1.0f);
                // 3) entra no peito
                render_box_oriented_3d(HP(0.16f * sgn, 0.33f, 0.115f), 0.20f, 0.05f, 0.05f, hyaw,
                                       0.30f, 0.36f, 0.44f, 1.0f);
                // Conector no peito.
                render_box_oriented_3d(HP(0.075f * sgn, 0.33f, 0.155f), 0.05f, 0.05f, 0.05f, hyaw,
                                       0.62f, 0.56f, 0.26f, 1.0f);
            }
        }

        // Painel do peito.
        float chest_x = px + sin_rot * 0.16f;
        float chest_z = pz + cos_rot * 0.16f;
        render_cube_3d(chest_x, py + 0.30f + bob, chest_z, 0.12f, 0.10f, 0.14f, 0.18f, 0.98f, false);
        render_cube_3d(chest_x + perp_x * 0.030f, py + 0.31f + bob, chest_z + perp_z * 0.030f, 0.03f, 0.16f, 0.85f, 0.30f, 1.0f, false);
        render_cube_3d(chest_x - perp_x * 0.030f, py + 0.29f + bob, chest_z - perp_z * 0.030f, 0.03f, 0.90f, 0.24f, 0.18f, 1.0f, false);

        // Luz frontal do capacete.
        float lamp_x = px + sin_rot * 0.22f;
        float lamp_z = pz + cos_rot * 0.22f;
        float lamp_i = std::clamp(g_player_visual_cfg.headlamp_intensity, 0.0f, 2.0f);
        render_cube_3d(lamp_x, py + 0.80f + bob, lamp_z, 0.06f, 0.95f * lamp_i, 0.90f * lamp_i, 0.58f * lamp_i, 0.95f, false);
        rlDisableDepthTest();
        render_plane_3d(lamp_x + sin_rot * 0.18f, py + 0.70f + bob, lamp_z + cos_rot * 0.18f, 0.42f, 1.0f, 0.92f, 0.68f, 0.20f * lamp_i);
        rlEnableDepthTest();
        
        // === PERNAS (2 blocos pequenos animados) ===
        float leg_sep = 0.12f;
        
        // Perna esquerda
        float ll_x = px - perp_x * leg_sep + sin_rot * leg_swing + idle_sway * 0.4f;
        float ll_z = pz - perp_z * leg_sep + cos_rot * leg_swing;
        render_cube_3d(ll_x, py - 0.10f - walk_weight * 0.25f, ll_z, 0.18f, 0.25f, 0.27f, 0.30f, 1.0f, true);
        
        // Perna direita
        float rl_x = px + perp_x * leg_sep - sin_rot * leg_swing - idle_sway * 0.4f;
        float rl_z = pz + perp_z * leg_sep - cos_rot * leg_swing;
        render_cube_3d(rl_x, py - 0.10f - walk_weight * 0.25f, rl_z, 0.18f, 0.25f, 0.27f, 0.30f, 1.0f, true);
        
        // === BRACOS (2 blocos pequenos - animados se minerando) ===
        float arm_bob = g_player.is_mining ? std::sin(g_player.mine_anim * 20.0f + g_player.anim_frame * 4.0f) * 0.14f : breath * 0.35f;
        float arm_sep = 0.28f;
        
        // Braco esquerdo
        float la_x = px - perp_x * arm_sep;
        float la_z = pz - perp_z * arm_sep;
        render_cube_3d(la_x, py + 0.25f + bob - arm_bob, la_z, 0.15f, suit_r * 0.96f, suit_g * 0.96f, suit_b, 1.0f, true);
        
        // Braco direito
        float ra_x = px + perp_x * arm_sep;
        float ra_z = pz + perp_z * arm_sep;
        render_cube_3d(ra_x, py + 0.25f + bob + arm_bob + mine_impact * 0.05f, ra_z, 0.15f, suit_r * 0.96f, suit_g * 0.96f, suit_b, 1.0f, true);

        // === PISTOLA DE LASER (segurada na mao direita, so' quando equipada) ===
        // render_cube_3d so' desenha cubos uniformes (sem caixa alongada) - o "cano" e'
        // aproximado por 2 cubos pequenos empilhados na direcao que o jogador olha (sin_rot/
        // cos_rot, mesma tecnica ja usada pro deslocamento da lanterna/antena acima), nao uma
        // unica caixa esticada. Mesmo estilo "Minicraft" do resto do corpo.
        if (g_selected == Block::LaserPistol) {
            // Refeita com render_box_oriented_3d: eram 3 CUBOS em fila (cabo, cano, ponta), e cubo
            // alinhado ao eixo do mundo fica torto quando o personagem olha na diagonal - o mesmo
            // problema que a mochila tinha. Com caixa orientada da' pra ter cano fino e comprido,
            // corpo achatado, mira em cima e uma celula de energia atras, tudo girando com o braco.
            // NIVEL DA ARMA (1..3, ver weapon_level em creatures.h): as pecas abaixo mudam com ele.
            // A DISTANCIA DO BOCAL NAO muda em nenhum nivel - o 0.385f do halo esta duplicado em
            // get_weapon_muzzle_pos (player_physics.cpp), que e' a origem do traco do tiro; move-lo
            // exigiria editar os dois lugares e o tiro passaria a sair de um ponto errado se um
            // deles fosse esquecido. Evolucao visual vem de PECAS NOVAS, cor e brilho.
            const int wlv = weapon_level();
            const bool mk2 = wlv >= 2, mk3 = wlv >= 3;
            // Acabamento dourado quando o trilho de legado esta completo (recompensa cosmetica da
            // missao 13 - puramente visual, nenhum efeito mecanico).
            const bool legacy_trim = objectives_legacy_complete();
            const float gyaw = std::atan2(sin_rot, cos_rot);
            const float gun_y = py + 0.25f + bob + arm_bob + mine_impact * 0.05f;
            const float gperp_x = cos_rot, gperp_z = -sin_rot;
            // Ponto na arma: (frente, altura, lado) a partir da mao.
            auto G = [&](float fwd, float up, float side) {
                return Vec3{ra_x + sin_rot * fwd + gperp_x * side, gun_y + up,
                            ra_z + cos_rot * fwd + gperp_z * side};
            };

            // Cabo inclinado pra tras (2 blocos escalonados dao a leitura de empunhadura).
            render_box_oriented_3d(G(0.015f, -0.055f, 0.0f), 0.065f, 0.115f, 0.075f, gyaw,
                                   0.16f, 0.17f, 0.20f, 1.0f);
            render_box_oriented_3d(G(-0.03f, -0.105f, 0.0f), 0.060f, 0.075f, 0.065f, gyaw,
                                   0.13f, 0.14f, 0.16f, 1.0f);
            // Guarda-mato sob o cabo.
            render_box_oriented_3d(G(0.075f, -0.075f, 0.0f), 0.045f, 0.035f, 0.10f, gyaw,
                                   0.20f, 0.21f, 0.24f, 1.0f);

            // Corpo/receptor: achatado e mais largo que alto.
            render_box_oriented_3d(G(0.085f, 0.015f, 0.0f), 0.085f, 0.085f, 0.19f, gyaw,
                                   0.26f, 0.28f, 0.32f, 1.0f);
            // Placa lateral clara de cada lado (contraste, senao a arma inteira le como um borrao).
            {
                float pr = 0.52f, pg = 0.55f, pb = 0.60f;
                if (mk2) { pr = 0.40f; pg = 0.72f; pb = 0.80f; }   // liga clara azulada
                if (mk3) { pr = 0.62f; pg = 0.88f; pb = 0.96f; }
                if (legacy_trim) { pr = 0.86f; pg = 0.72f; pb = 0.30f; }
                for (float sgn : {-1.0f, 1.0f}) {
                    render_box_oriented_3d(G(0.085f, 0.015f, 0.046f * sgn), 0.012f, 0.055f, 0.13f, gyaw,
                                           pr, pg, pb, 1.0f);
                }
            }

            // Celula de energia atras do receptor: barra acesa que ENCURTA conforme o cooldown.
            {
                float charge = 1.0f - clamp01(laser_cooldown_fraction());
                render_box_oriented_3d(G(0.03f, 0.055f, 0.0f), 0.055f, 0.030f, 0.085f, gyaw,
                                       0.10f, 0.11f, 0.13f, 1.0f);
                float lit = std::max(0.008f, 0.085f * charge);
                render_box_oriented_3d(G(0.03f - (0.085f - lit) * 0.5f, 0.058f, 0.0f),
                                       0.042f, 0.022f, lit, gyaw,
                                       0.30f, 0.92f, 1.0f, 1.0f);
            }

            // Mira em cima (base + 2 postes).
            render_box_oriented_3d(G(0.10f, 0.062f, 0.0f), 0.035f, 0.020f, 0.11f, gyaw,
                                   0.20f, 0.21f, 0.24f, 1.0f);
            render_box_oriented_3d(G(0.055f, 0.088f, 0.0f), 0.030f, 0.038f, 0.020f, gyaw,
                                   0.34f, 0.36f, 0.40f, 1.0f);
            render_box_oriented_3d(G(0.155f, 0.082f, 0.0f), 0.026f, 0.028f, 0.020f, gyaw,
                                   0.34f, 0.36f, 0.40f, 1.0f);

            // Cano: fino e COMPRIDO (era um cubo de 0.09), com 2 aneis de refrigeracao.
            render_box_oriented_3d(G(0.245f, 0.020f, 0.0f), 0.050f, 0.050f, 0.17f, gyaw,
                                   0.22f, 0.24f, 0.27f, 1.0f);
            for (float fw : {0.195f, 0.275f}) {
                render_box_oriented_3d(G(fw, 0.020f, 0.0f), 0.072f, 0.072f, 0.022f, gyaw,
                                       0.44f, 0.47f, 0.52f, 1.0f);
            }
            // ---- PECAS QUE SO' EXISTEM NOS NIVEIS ALTOS ----
            if (mk2) {
                // Trilho superior sobre o receptor: silhueta mais tecnica sem alongar a arma.
                render_box_oriented_3d(G(0.10f, 0.082f, 0.0f), 0.052f, 0.014f, 0.15f, gyaw,
                                       0.30f, 0.33f, 0.38f, 1.0f);
            }
            if (mk3) {
                // Segundo par de aneis de arrefecimento (a arma dissipa mais energia agora).
                for (float fw : {0.225f, 0.305f}) {
                    render_box_oriented_3d(G(fw, 0.020f, 0.0f), 0.066f, 0.066f, 0.016f, gyaw,
                                           0.58f, 0.62f, 0.68f, 1.0f);
                }
                // Capacitor sob o cano, com faixa acesa.
                render_box_oriented_3d(G(0.215f, -0.032f, 0.0f), 0.048f, 0.036f, 0.13f, gyaw,
                                       0.12f, 0.13f, 0.15f, 1.0f);
                render_box_oriented_3d(G(0.215f, -0.030f, 0.0f), 0.030f, 0.020f, 0.10f, gyaw,
                                       0.45f, 0.92f, 1.0f, 1.0f);
                // Pulso de energia percorrendo o cano em direcao ao bocal - uma caixinha clara cujo
                // `fwd` anda de 0.17 a 0.33 e reinicia. Usa anim_frame (o mesmo relogio do tip_glow),
                // entao nao precisa de estado novo nem de timer proprio.
                float pulse_t = std::fmod(g_player.anim_frame * 1.6f, 1.0f);
                render_box_oriented_3d(G(0.17f + pulse_t * 0.16f, 0.020f, 0.0f),
                                       0.056f, 0.056f, 0.020f, gyaw,
                                       0.55f, 0.95f, 1.0f, 0.85f);
            }

            // Emissor: bocal escuro + nucleo pulsante, na PONTA (e' de onde o traco do tiro sai -
            // ver get_weapon_muzzle_pos em player_physics.cpp).
            render_box_oriented_3d(G(0.345f, 0.020f, 0.0f), 0.062f, 0.062f, 0.040f, gyaw,
                                   0.14f, 0.15f, 0.17f, 1.0f);
            float tip_glow = 0.65f + 0.35f * std::sin(g_player.anim_frame * 6.0f);
            // Nucleo maior e mais quente a cada nivel (o Mk III fica quase branco).
            float core_sz = mk3 ? 0.052f : (mk2 ? 0.046f : 0.040f);
            float core_r = mk3 ? 0.85f : (mk2 ? 0.55f : 0.35f);
            render_box_oriented_3d(G(0.372f, 0.020f, 0.0f), core_sz, core_sz, 0.022f, gyaw,
                                   core_r * tip_glow, 0.92f * tip_glow, 1.0f * tip_glow, 1.0f);
            // Halo do emissor - o que faz a arma ler como energia e nao como ferro.
            rlSetBlendMode(RL_BLEND_ADDITIVE);
            rlDisableDepthMask();
            float halo_rad = mk3 ? 0.125f : (mk2 ? 0.098f : 0.075f);
            float halo_a   = mk3 ? 0.62f  : (mk2 ? 0.53f  : 0.45f);
            float hr = legacy_trim ? 0.95f : (mk3 ? 0.70f : 0.40f);
            float hg = legacy_trim ? 0.82f : 0.92f;
            float hb = legacy_trim ? 0.40f : 1.0f;
            render_glow_disc_3d(G(0.385f, 0.020f, 0.0f), halo_rad, hr, hg, hb, halo_a * tip_glow, 10);
            rlEnableDepthMask();
            rlSetBlendMode(RL_BLEND_ALPHA);
        }
    }

    if (g_debug && DEBUG_DRAW_COLLISIONS) {
        render_physics_debug_3d();
    }

    // === SELECAO DO ALVO / PLACE (Estilo Minicraft) ===
    // Desenha um contorno no bloco/tile sob a mira para deixar claro o que sera minerado/coletado/colocado.
    auto draw_tile_outline = [&](int tx, int tz, float y, float size, float r, float g, float b, float a, float lw) {
        float half = size * 0.5f;
        rlSetLineWidth(lw);
        rlBegin(RL_LINES);
        rlColor4f(r, g, b, a);
        rlVertex3f((float)tx - half, y, (float)tz - half); rlVertex3f((float)tx + half, y, (float)tz - half);
        rlVertex3f((float)tx + half, y, (float)tz - half); rlVertex3f((float)tx + half, y, (float)tz + half);
        rlVertex3f((float)tx + half, y, (float)tz + half); rlVertex3f((float)tx - half, y, (float)tz + half);
        rlVertex3f((float)tx - half, y, (float)tz + half); rlVertex3f((float)tx - half, y, (float)tz - half);
        rlEnd();
    };

    int ptx = world_to_tile(rpos.x);
    int ptz = world_to_tile(rpos.y);
    bool draw_selection_boxes = (g_debug && DEBUG_DRAW_COLLISIONS);
    auto is_player_tile = [&](int tx, int tz) -> bool {
        return tx == ptx && tz == ptz;
    };

    if (draw_selection_boxes && g_has_target && g_world->in_bounds(g_target_x, g_target_y)) {
        Block tb = g_world->get(g_target_x, g_target_y);
        float base_y = (float)g_world->height_at(g_target_x, g_target_y) * kHeightScale;

        rlSetTexture(rlGetTextureIdDefault());   // NAO rlSetTexture(0): pra id 0 o rlgl nao troca nada
        rlSetBlendMode(RL_BLEND_ALPHA);

        // Evita caixa em volta do proprio jogador.
        if (!is_player_tile(g_target_x, g_target_y)) {
            // Contorno preto + contorno branco por cima (boa leitura em qualquer tile)
            if (tb == Block::Air || tb == Block::Leaves || tb == Block::Water || is_ground_like(tb)) {
                float y = base_y + 0.018f;
                if (tb == Block::Leaves) y = base_y + 0.60f + 0.004f;
                else if (tb == Block::Water) y = base_y - 0.18f + 0.004f;
                draw_tile_outline(g_target_x, g_target_y, y, 1.03f, 0.0f, 0.0f, 0.0f, 0.85f, 2.5f);
                draw_tile_outline(g_target_x, g_target_y, y, 1.03f, 1.0f, 1.0f, 1.0f, 0.80f, 1.5f);
            } else {
                float cy = base_y + 0.5f;
                render_cube_outline_3d((float)g_target_x, cy, (float)g_target_y, 1.04f, 2.5f);

                // Outline branco leve
                rlSetLineWidth(1.5f);
                float half = 1.04f * 0.5f;
                rlBegin(RL_LINES);
                rlColor4f(1.0f, 1.0f, 1.0f, 0.55f);
                rlVertex3f((float)g_target_x - half, cy + half, (float)g_target_y - half); rlVertex3f((float)g_target_x + half, cy + half, (float)g_target_y - half);
                rlVertex3f((float)g_target_x + half, cy + half, (float)g_target_y - half); rlVertex3f((float)g_target_x + half, cy + half, (float)g_target_y + half);
                rlVertex3f((float)g_target_x + half, cy + half, (float)g_target_y + half); rlVertex3f((float)g_target_x - half, cy + half, (float)g_target_y + half);
                rlVertex3f((float)g_target_x - half, cy + half, (float)g_target_y + half); rlVertex3f((float)g_target_x - half, cy + half, (float)g_target_y - half);
                rlEnd();
            }
        }
    }

    if (draw_selection_boxes && g_has_place_target && g_world->in_bounds(g_place_x, g_place_y) && !is_player_tile(g_place_x, g_place_y)) {
        // Mostra um contorno azul para o tile onde o RMB vai colocar
        Block pb = g_world->get(g_place_x, g_place_y);
        float base_y = (float)g_world->height_at(g_place_x, g_place_y) * kHeightScale;
        float y = base_y + 0.020f;
        if (pb == Block::Leaves) y = base_y + 0.60f + 0.004f;
        else if (pb == Block::Water) y = base_y - 0.18f + 0.004f;
        draw_tile_outline(g_place_x, g_place_y, y, 1.05f, 0.05f, 0.65f, 1.0f, 0.65f, 2.0f);
    }

    // === EFEITO DE MINERACAO (cracks) - SEM WIREFRAME ===
    if (g_has_target) {
        float target_x = (float)g_target_x;
        float target_z = (float)g_target_y;
        Block tb = g_world->get(g_target_x, g_target_y);
        float base_y = (float)g_world->height_at(g_target_x, g_target_y) * kHeightScale;

        // Overlay de "cracks" durante mineracao (progresso)
        if (g_tex_atlas != 0 && g_mine_progress > 0.001f &&
            g_mine_block_x == g_target_x && g_mine_block_y == g_target_y) {
            int lvl = std::clamp((int)std::floor(g_mine_progress * 8.0f), 0, 7);
            Tile crack = (Tile)((int)Tile::Crack1 + lvl);

            float crack_y = base_y + 0.01f + 0.002f;
            if (tb == Block::Leaves) crack_y = base_y + 0.60f + 0.002f;
            else if (tb == Block::Water) crack_y = base_y - 0.18f + 0.002f;
            else if (tb != Block::Air && !is_ground_like(tb)) crack_y = base_y + get_block_height(tb) + 0.002f;

            rlDisableDepthMask();
            rlSetTexture(g_tex_atlas);
            render_plane_3d_tex(target_x, crack_y, target_z, 1.04f, crack, 1.0f, 1.0f, 1.0f, 1.0f);
            rlSetTexture(0);
            rlEnableDepthMask();

            // Barra de quebra opcional (feedback claro).
            if (!is_player_tile(g_target_x, g_target_y)) {
                float bar_w = 0.84f;
                float bar_h = 0.06f;
                float y = crack_y + 0.012f;
                float x0 = (float)g_target_x - bar_w * 0.5f;
                float z0 = (float)g_target_y - 0.56f;
                rlSetTexture(0);
                rlBegin(RL_QUADS);
                rlColor4f(0.02f, 0.02f, 0.03f, 0.78f);
                rlVertex3f(x0, y, z0);
                rlVertex3f(x0 + bar_w, y, z0);
                rlVertex3f(x0 + bar_w, y, z0 + bar_h);
                rlVertex3f(x0, y, z0 + bar_h);
                rlEnd();

                float fill = std::clamp(g_mine_progress, 0.0f, 1.0f) * (bar_w - 0.02f);
                rlBegin(RL_QUADS);
                rlColor4f(0.95f, 0.82f, 0.26f, 0.92f);
                rlVertex3f(x0 + 0.01f, y + 0.001f, z0 + 0.01f);
                rlVertex3f(x0 + 0.01f + fill, y + 0.001f, z0 + 0.01f);
                rlVertex3f(x0 + 0.01f + fill, y + 0.001f, z0 + bar_h - 0.01f);
                rlVertex3f(x0 + 0.01f, y + 0.001f, z0 + bar_h - 0.01f);
                rlEnd();
            }
        }
    }
    
    // === BEACON VISUAL DA BASE (farol visivel a distancia) ===
    {
        float bx = (float)g_base_x + 0.5f;
        float bz = (float)g_base_y + 0.5f;
        float base_h = (float)g_world->height_at(g_base_x, g_base_y) * kHeightScale;
        float beacon_h = base_h + g_base_cfg.beacon_height;
        
        // Verificar se esta longe da base para mostrar o beacon
        float dx_beacon = g_player.pos.x - (float)g_base_x;
        float dy_beacon = g_player.pos.y - (float)g_base_y;
        float dist_beacon = std::sqrt(dx_beacon * dx_beacon + dy_beacon * dy_beacon);
        
        // Mostrar beacon quando longe da base (mais intenso quanto mais longe)
        float beacon_intensity = std::clamp((dist_beacon - g_base_cfg.safe_radius) / 50.0f, 0.0f, 1.0f);
        
        if (beacon_intensity > 0.01f) {
            // Pulso animado
            float pulse = 0.5f + 0.5f * std::sin(g_day_time * g_base_cfg.beacon_pulse_speed);
            float final_alpha = beacon_intensity * pulse * g_base_cfg.beacon_alpha;
            
            rlSetBlendMode(RL_BLEND_ALPHA);
            rlSetTexture(0);
            rlDisableDepthMask();

            // Pilar de luz principal (gradiente vertical). GL_QUAD_STRIP -> RL_QUADS: buffer
            // do par de vertices anterior, emite (prev_left,prev_right,cur_right,cur_left) a
            // partir da 2a iteracao.
            {
                rlBegin(RL_QUADS);
                float prev_lx = 0, prev_ly = 0, prev_lz = 0, prev_rx = 0, prev_ry = 0, prev_rz = 0, prev_a = 0;
                bool have_prev = false;
                for (int i = 0; i <= 20; ++i) {
                    float t = (float)i / 20.0f;
                    float y = base_h + t * (beacon_h - base_h);
                    float alpha = (1.0f - t * 0.8f) * final_alpha;
                    float width = 0.2f * (1.0f - t * 0.5f);  // Afina no topo

                    float lx = bx - width, ly = y, lz = bz;
                    float rx = bx + width, ry = y, rz = bz;
                    if (have_prev) {
                        rlColor4f(0.3f, 0.8f, 1.0f, prev_a); rlVertex3f(prev_lx, prev_ly, prev_lz);
                        rlColor4f(0.3f, 0.8f, 1.0f, prev_a); rlVertex3f(prev_rx, prev_ry, prev_rz);
                        rlColor4f(0.3f, 0.8f, 1.0f, alpha); rlVertex3f(rx, ry, rz);
                        rlColor4f(0.3f, 0.8f, 1.0f, alpha); rlVertex3f(lx, ly, lz);
                    }
                    prev_lx = lx; prev_ly = ly; prev_lz = lz;
                    prev_rx = rx; prev_ry = ry; prev_rz = rz;
                    prev_a = alpha;
                    have_prev = true;
                }
                rlEnd();
            }

            // Pilar secundario (perpendicular para visibilidade 3D)
            {
                rlBegin(RL_QUADS);
                float prev_lx = 0, prev_ly = 0, prev_lz = 0, prev_rx = 0, prev_ry = 0, prev_rz = 0, prev_a = 0;
                bool have_prev = false;
                for (int i = 0; i <= 20; ++i) {
                    float t = (float)i / 20.0f;
                    float y = base_h + t * (beacon_h - base_h);
                    float alpha = (1.0f - t * 0.8f) * final_alpha * 0.7f;
                    float width = 0.15f * (1.0f - t * 0.5f);

                    float lx = bx, ly = y, lz = bz - width;
                    float rx = bx, ry = y, rz = bz + width;
                    if (have_prev) {
                        rlColor4f(0.3f, 0.8f, 1.0f, prev_a); rlVertex3f(prev_lx, prev_ly, prev_lz);
                        rlColor4f(0.3f, 0.8f, 1.0f, prev_a); rlVertex3f(prev_rx, prev_ry, prev_rz);
                        rlColor4f(0.3f, 0.8f, 1.0f, alpha); rlVertex3f(rx, ry, rz);
                        rlColor4f(0.3f, 0.8f, 1.0f, alpha); rlVertex3f(lx, ly, lz);
                    }
                    prev_lx = lx; prev_ly = ly; prev_lz = lz;
                    prev_rx = rx; prev_ry = ry; prev_rz = rz;
                    prev_a = alpha;
                    have_prev = true;
                }
                rlEnd();
            }

            // Halo na base do beacon (GL_TRIANGLE_FAN -> RL_TRIANGLES: buffer o anel, re-emite
            // como triplas (centro, v[i], v[i+1])).
            {
                float halo_pulse = 0.6f + 0.4f * pulse;
                float center_alpha = final_alpha * 0.5f * halo_pulse;
                std::vector<Vector3> rim(17);
                for (int i = 0; i <= 16; ++i) {
                    float angle = (float)i * (2.0f * kPi / 16.0f);
                    float halo_r = 1.5f * halo_pulse;
                    rim[i] = {bx + std::cos(angle) * halo_r, base_h + 0.05f, bz + std::sin(angle) * halo_r};
                }
                rlBegin(RL_TRIANGLES);
                for (int i = 0; i < 16; ++i) {
                    rlColor4f(0.3f, 0.8f, 1.0f, center_alpha);
                    rlVertex3f(bx, base_h + 0.1f, bz);
                    rlColor4f(0.3f, 0.8f, 1.0f, 0.0f);
                    rlVertex3f(rim[i].x, rim[i].y, rim[i].z);
                    rlColor4f(0.3f, 0.8f, 1.0f, 0.0f);
                    rlVertex3f(rim[i + 1].x, rim[i + 1].y, rim[i + 1].z);
                }
                rlEnd();
            }

            rlEnableDepthMask();
        }
    }
    
    render_hud(win_w, win_h);

    // HUD de combate (painel de ameacas + numeros de dano). Tem que vir DEPOIS de render_hud: e'
    // render_hud que troca a projecao pra ortho 2D. Chamado antes, o painel era desenhado com a
    // projecao PERSPECTIVA ainda ativa - por isso a barra de vida "nao aparecia corretamente".
    render_creature_hud(win_w, win_h);

    // DIAGNOSTICO TEMPORARIO (remover depois) - jogador reportou voo infinito + piscar do
    // chao de novo mesmo apos o ajuste de smooth_passes/detail_weight. Mostra FPS real +
    // raio de visao/paredes + quantas paredes foram desenhadas neste frame, pra medir de
    // verdade em vez de ajustar terreno as cegas outra vez.
    DrawText(TextFormat("[DEBUG] FPS:%d  view_r:%d  wall_r:%d  near_walls:%d  quality:%.2f  far_chunks:%d",
                         GetFPS(), g_debug_view_radius, g_debug_wall_radius, g_debug_wall_draws, g_render_quality, g_debug_far_chunks_drawn),
             10, win_h - 26, 18, YELLOW);

    // Overlays - Menus estilo Minecraft (Paused/Menu/Dead/Settings). Extracted verbatim to
    // ui_menu.cpp's render_menus() - see ui_menu.h for details; the build menu (g_show_build_menu)
    // and the victory/alerts/world-map overlays right after it stay inline here.
    render_menus(win_w, win_h);

    // One-time victory celebration (fires when objectives.cpp completes the final
    // milestone) instead of the old permanent "if (g_victory)" overlay that never went
    // away once triggered - the objectives HUD panel (ui_hud.cpp) now shows a permanent
    // "Marte Terraformado!" line once all milestones are done, so this overlay only needs
    // to cover the initial celebratory moment.
    float victory_celebration = objectives_victory_celebration_remaining();
    if (victory_celebration > 0.0f) {
        float alpha = std::min(1.0f, victory_celebration / 2.0f);  // fade out over the last 2s
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.0f, 0.0f, 0.0f, 0.30f * alpha);
        std::string t1 = "Marte Terraformado!";
        std::string t2 = "Parabens, colono - voce completou todos os objetivos.";
        draw_text(win_w * 0.5f - estimate_text_w_px(t1) * 0.5f, win_h * 0.20f, t1, 0.85f, 0.95f, 0.85f, 0.98f * alpha);
        draw_text(win_w * 0.5f - estimate_text_w_px(t2) * 0.5f, win_h * 0.20f + 26.0f, t2, 0.80f, 0.90f, 0.80f, 0.90f * alpha);
    }

    // Overlay de "Legado Completo!" (mesmo padrao do overlay de vitoria acima, so pro
    // trilho de legado pos-vitoria - ver objectives.h/.cpp).
    float legacy_celebration = objectives_legacy_celebration_remaining();
    if (legacy_celebration > 0.0f) {
        float alpha = std::min(1.0f, legacy_celebration / 2.0f);
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.0f, 0.0f, 0.0f, 0.30f * alpha);
        std::string t1 = "Legado Completo!";
        std::string t2 = "A colonia prospera muito alem da terraformacao.";
        draw_text(win_w * 0.5f - estimate_text_w_px(t1) * 0.5f, win_h * 0.20f, t1, 0.95f, 0.85f, 0.35f, 0.98f * alpha);
        draw_text(win_w * 0.5f - estimate_text_w_px(t2) * 0.5f, win_h * 0.20f + 26.0f, t2, 0.90f, 0.80f, 0.35f, 0.90f * alpha);
    }

    // ============= BUILD MENU =============
    // Extracted verbatim to building_interaction.cpp's render_build_menu() - see
    // building_interaction.h for details. The guard that used to wrap this block
    // ("if (g_show_build_menu && g_state == GameState::Playing)") now lives inside that
    // function instead (same pattern as render_menus() above checking g_state internally).
    render_build_menu(win_w, win_h);

    // ============= ALERTS DISPLAY =============
    if (!g_alerts.empty() && g_state == GameState::Playing && !g_show_build_menu) {
        float alert_y = 150.0f;
        for (const auto& alert : g_alerts) {
            float alpha = std::min(1.0f, alert.time_remaining);
            float alert_w = estimate_text_w_px(alert.message) + 30.0f;
            float alert_x = win_w - alert_w - 20.0f;
            
            render_quad(alert_x, alert_y, alert_w, 28.0f, alert.r * 0.3f, alert.g * 0.3f, alert.b * 0.3f, 0.85f * alpha);
            render_quad(alert_x, alert_y, 4.0f, 28.0f, alert.r, alert.g, alert.b, alpha);
            draw_text(alert_x + 15.0f, alert_y + 19.0f, alert.message, alert.r, alert.g, alert.b, alpha);
            
            alert_y += 35.0f;
        }
    }

    // === MAPA GRANDE (sobreposicao) ===
    if (g_minimap.world_map_open) {
        render_world_map(win_w, win_h);
    }

    // Resetar clique do mouse no final do frame
    g_mouse_left_clicked = false;

    // SwapBuffers(hdc) removed: win32_platform.cpp's main loop wraps this call in
    // BeginDrawing()/EndDrawing(), which handles the buffer swap now.
}

// ============= Input State =============
// key_down()/key_pressed() moved to input.h/.cpp (verbatim) - the input extraction stage.
// input.h (included at the top of this file) supplies both declarations; update_game()
// below still calls key_pressed() exactly as before, for every g_prev_<key> debounce
// global, all of which stay right here (they are this function's own hotkey-polling state,
// not part of the input module - see input.h for the full reasoning).

// ============= Update =============
// update_game() loses "static" here: win32_platform.cpp's WinMain() (the win32_platform
// extraction stage, the last of this whole refactor) calls it from another translation
// unit now, via its own forward declaration (no header owns render_world()/update_game()
// themselves, since they are the two intentional final orchestrators left in this file,
// not a reusable module) - same pattern as every other "lost static" function in this
// codebase's extraction stages.
// Meteoro raro com recurso coletavel - ver struct FallingMeteor acima. So 1 ativo por vez;
// dispara num intervalo aleatorio bem longo (5-15 minutos), caindo perto (nao em cima) do
// jogador. Ao pousar, larga um Cristal coletavel (ver spawn_item_drop) e um estouro de
// particulas (spawn_block_particles, ja usado pra minerar) marcando o local.
static void update_meteors(float dt) {
    static float meteor_timer = 0.0f;
    static float meteor_next = 60.0f + rng_next_f01() * 120.0f;

    if (g_meteors.empty() && g_world) {
        meteor_timer += dt;
        if (meteor_timer >= meteor_next) {
            meteor_timer = 0.0f;
            meteor_next = 60.0f + rng_next_f01() * 120.0f;

            // Cai dentro do campo de visao atual da camera (pedido do jogador: "deve cair
            // quando o jogador estiver olhando") em vez de um angulo 360 totalmente aleatorio
            // - sem isso, o meteoro caia e sumia com boa chance de estar fora da tela inteira.
            // rad_yaw segue a mesma convencao de update_camera_position() (camera.cpp):
            // forward = -sin(yaw), -cos(yaw) no plano XZ.
            float rad_yaw = g_camera.yaw * (kPi / 180.0f);
            float forward_ang = std::atan2(-std::cos(rad_yaw), -std::sin(rad_yaw));
            constexpr float kMeteorViewConeRad = 50.0f * (kPi / 180.0f); // bem dentro do FOV (74 graus)

            // ZONA PROIBIDA em volta da base (pedido do jogador: "nao quero o meteorito caindo na
            // base ou muito proximo dela"). 90 tiles cobre com folga a instalacao (casca ~r22), o
            // disco achatado (r46) e a rampa de transicao (r78) - a cratera nunca encosta em nada
            // construido. Tenta varios angulos dentro do cone de visao antes de desistir: assim o
            // meteoro continua caindo quando ha area livre em vista, e simplesmente NAO cai quando o
            // jogador esta na base (o intervalo apenas reinicia e ele tenta de novo depois).
            constexpr float kMeteorBaseKeepOut2 = 90.0f * 90.0f;
            int tx = -1, tz = -1;
            for (int attempt = 0; attempt < 10; ++attempt) {
                float ang = forward_ang + (rng_next_f01() - 0.5f) * kMeteorViewConeRad;
                float dist = 15.0f + rng_next_f01() * 15.0f;
                int cx2 = world_to_tile(g_player.pos.x + std::cos(ang) * dist);
                int cz2 = world_to_tile(g_player.pos.y + std::sin(ang) * dist);
                if (!g_world->in_bounds(cx2, cz2)) continue;
                float bdx = (float)(cx2 - g_base_x), bdz = (float)(cz2 - g_base_y);
                if (bdx * bdx + bdz * bdz < kMeteorBaseKeepOut2) continue;
                tx = cx2; tz = cz2;
                break;
            }
            if (tx >= 0) {
                FallingMeteor m;
                m.x = tile_center(tx);
                m.z = tile_center(tz);
                m.target_y = surface_height_at(*g_world, tx, tz);
                m.start_y = m.target_y + 90.0f;
                g_meteors.push_back(m);
            }
        }
    }

    for (auto it = g_meteors.begin(); it != g_meteors.end();) {
        it->t += dt;
        if (it->t >= it->duration) {
            // === CRATERA DE IMPACTO ===
            // Muito maior e com BORDA ELEVADA (ejecta) em vez de so' uma tigela lisa - uma
            // cratera de verdade sobe acima do terreno ao redor na beirada, e e' isso que
            // faz ela ler como cratera de impacto e nao como um buraco.
            int ix = world_to_tile(it->x);
            int iz = world_to_tile(it->z);
            if (g_world->in_bounds(ix, iz)) {
                constexpr float kCraterRadius = 7.5f;      // tigela (era 3.2 - dava um pratinho)
                constexpr float kCraterRimRadius = 10.5f;  // ate onde vai a borda elevada
                constexpr float kCraterDepthWorld = 4.0f;  // fundo no centro (era 2.2)
                constexpr float kCraterRimWorld = 1.0f;    // quanto a borda sobe
                constexpr float kMoltenRadius = 2.6f;      // nucleo ainda derretido (Lava)
                int dig_max = std::max(1, (int)std::lround(kCraterDepthWorld / std::max(0.01f, kHeightScale)));
                int rim_max = std::max(1, (int)std::lround(kCraterRimWorld / std::max(0.01f, kHeightScale)));
                int scan_r = (int)kCraterRimRadius + 2;

                // Agua/gelo NAO e' escavado. Motivo: no world-gen todo tile de agua e'
                // achatado exatamente em sea_level, e nao existe simulacao de fluido pra
                // reencher nada - escavar tiles de agua deixaria um poco seco com uma parede
                // de agua de 5 unidades do lado, e um meteoro "drenaria" o lago. Caindo perto
                // da praia, a cratera simplesmente para na linha d'agua.
                for (int cz = iz - scan_r; cz <= iz + scan_r; ++cz) {
                    for (int cx = ix - scan_r; cx <= ix + scan_r; ++cx) {
                        if (!g_world->in_bounds(cx, cz)) continue;
                        Block gb = g_world->get_ground(cx, cz);
                        if (gb == Block::Water || gb == Block::Ice) continue;
                        // Nao mexe em nada da base (modulos/estruturas) - o jogador perderia
                        // construcao sem ter como evitar.
                        Block tb = g_world->get(cx, cz);
                        if (is_module(tb) || is_base_structure(tb) || is_base_structure(gb)) continue;

                        float ddx = (float)(cx - ix), ddz = (float)(cz - iz);
                        float dist = std::sqrt(ddx * ddx + ddz * ddz);
                        if (dist > kCraterRimRadius) continue;

                        int16_t ch = g_world->height_at(cx, cz);
                        if (dist <= kCraterRadius) {
                            // Tigela: fundo no centro, subindo suave ate a beirada.
                            float t = dist / kCraterRadius;
                            float falloff = 1.0f - t * t;              // quadratico, fundo chato-ish
                            int dig = (int)std::lround((float)dig_max * falloff);
                            int nh = std::max(0, (int)ch - dig);
                            g_world->set_height(cx, cz, (int16_t)nh);
                            // Nucleo derretido: Lava (nao Stone - Stone na camada de solo
                            // permitiria minerar pedra infinita ali; Lava nao e' mineravel,
                            // nao bloqueia passagem e nao causa dano nenhum hoje, so' brilha).
                            if (dist <= kMoltenRadius) {
                                g_world->set_ground(cx, cz, Block::Lava);
                                g_world->set(cx, cz, Block::Lava);
                            } else {
                                // Resto da tigela: terra revirada, sem vegetacao/rocha em cima.
                                g_world->set_ground(cx, cz, Block::Dirt);
                                // VEIOS DE FERRO METEORICO expostos na tigela. Um unico drop nao
                                // faria diferenca no gargalo de ferro; a cratera virar um sitio de
                                // mineracao de ferro faz - e da' ao meteoro um proposito de
                                // gameplay em vez de so' ser um evento cosmetico destrutivo.
                                // Hash deterministico (nao rng por frame): a mesma cratera tem
                                // sempre os mesmos veios, e recarregar o save nao os embaralha.
                                float vh = std::sin((float)cx * 12.9898f + (float)cz * 78.233f) * 43758.5453f;
                                vh -= std::floor(vh);
                                if (vh > 0.72f) {
                                    g_world->set(cx, cz, Block::Iron);
                                } else if (tb != Block::Air) {
                                    g_world->set(cx, cz, Block::Dirt);
                                }
                            }
                        } else {
                            // Borda elevada: altura ABSOLUTA (maior vizinho + rim) em vez de
                            // somar um delta ao terreno - somar 3 unidades num terreno ja
                            // acidentado desaparece no ruido; um anel numa altura definida
                            // realmente aparece como lombada de ejecta.
                            float t = (dist - kCraterRadius) / std::max(0.01f, (kCraterRimRadius - kCraterRadius));
                            float rim_profile = 1.0f - t;      // maximo junto da tigela
                            rim_profile *= rim_profile;
                            int rise = (int)std::lround((float)rim_max * rim_profile);
                            if (rise <= 0) continue;
                            int nh = std::min(255, (int)ch + rise);
                            g_world->set_height(cx, cz, (int16_t)nh);
                        }
                    }
                }

                // Ejecta: pedras arremessadas ao redor, deterministicas (nao aleatorias por
                // frame). Comeca 2 tiles ALEM da beirada de proposito: um bloco de rocha em
                // cima da lombada bloquearia a subida (try_step_climb se recusa a subir com
                // objeto na frente), transformando a borda numa parede que exige pulo.
                for (int e = 0; e < 26; ++e) {
                    float ha = std::sin((float)e * 12.9898f + (float)ix * 0.017f) * 43758.5453f;
                    ha -= std::floor(ha);
                    float hr = std::sin((float)e * 78.233f + (float)iz * 0.023f) * 43758.5453f;
                    hr -= std::floor(hr);
                    float ang = ha * 6.2831853f;
                    float rad = kCraterRimRadius + 2.0f + hr * 5.0f;
                    int ex = ix + (int)std::lround(std::cos(ang) * rad);
                    int ez = iz + (int)std::lround(std::sin(ang) * rad);
                    if (!g_world->in_bounds(ex, ez)) continue;
                    Block egb = g_world->get_ground(ex, ez);
                    if (egb == Block::Water || egb == Block::Ice || egb == Block::Lava) continue;
                    Block etb = g_world->get(ex, ez);
                    if (etb != Block::Air) continue; // nao apaga arvore/minerio/estrutura
                    g_world->set(ex, ez, Block::Stone);
                }

                g_surface_dirty = true;

                // ================= SEMEIA O FLUIDO NA CRATERA =================
                // A cratera abria um buraco ao lado de um lago ou de um rio de lava e o fluido
                // FICAVA PARADO: a simulacao de agua/lava e' orientada a eventos e so' era semeada
                // pela escavacao MANUAL (building_interaction.cpp semeia o tile cavado + os 4
                // vizinhos). O meteoro nao semeava nada, entao a agua ficava encostada num poco sem
                // nunca escorrer pra dentro - foi o bug reportado ("a agua bugou e nao preencheu o
                // buraco").
                //
                // Semeia toda a area da cratera: water_flood_from/lava_flood_from se
                // autofiltram (nao fazem nada num tile que nao e' liquido nem tem liquido vizinho),
                // entao chamar pra ~625 tiles uma vez no impacto e' barato e nao precisa de teste
                // de adjacencia duplicado aqui. Objetos nao represam fluido (water_flow_blocked so'
                // olha pilha e estrutura da base), entao os veios de ferro meteorico continuam no
                // lugar e o buraco enche por cima deles.
                for (int cz = iz - scan_r; cz <= iz + scan_r; ++cz) {
                    for (int cx = ix - scan_r; cx <= ix + scan_r; ++cx) {
                        if (!g_world->in_bounds(cx, cz)) continue;
                        float sdx = (float)(cx - ix), sdz = (float)(cz - iz);
                        float sd2 = sdx * sdx + sdz * sdz;
                        if (sd2 > kCraterRimRadius * kCraterRimRadius) continue;
                        water_flood_from(*g_world, cx, cz);
                        // LAVA DO METEORO NAO ESPALHA - ELA ESFRIA.
                        // O nucleo derretido do impacto e' uma quantidade FINITA: nao ha camara
                        // magmatica alimentando nada, ao contrario de um vulcao. Enfileira pra
                        // ESFRIAR (lava_cool_enqueue, world.h): a crosta de basalto avanca da borda
                        // pro centro em ~23s, e a lava continua coletavel enquanto esta liquida.
                        //
                        // E NAO semeia lava_flood_from em NENHUM tile da cratera. Duas tentativas
                        // anteriores erraram aqui: semear em toda a area fez o nucleo ir de 19 pra 53
                        // tiles; restringir a "fora do nucleo" nao resolveu, porque esses tiles sao
                        // VIZINHOS do nucleo - lava_flood_from num tile seco procura vizinho de lava
                        // e se enfileira, entao continuava semeando fluxo a partir do proprio derrame
                        // do meteoro (medido: 21 -> 96 tiles). Um impacto nao deve fazer lava correr.
                        lava_cool_enqueue(*g_world, cx, cz);
                    }
                }
            }

            // FERRO, nao cristal (pedido do jogador: "o meteorito deveria deixar ferro"). Faz sentido
            // tematico - meteorito metalico e' ferro-niquel, nao gema - e resolve o gargalo real:
            // ferro e' o recurso mais exigido da campanha e o mais raro do mapa.
            spawn_item_drop(Block::Iron, it->x, it->z, it->target_y + 0.3f);
            spawn_block_particles(Block::Iron, it->x, it->z, g_world->h);
            play_meteor_impact_sound();
            // Onda de choque na hora do impacto: reaproveita o efeito de poeira do pouso de
            // jetpack (ja aprovado pelo jogador), na intensidade maxima e ancorado no ponto
            // do impacto - de graca, sem sistema de particulas novo.
            g_physics.landing_dust_timer = 1.1f;
            g_physics.landing_dust_pos = {it->x, it->target_y, it->z};
            g_physics.landing_dust_intensity = 1.0f;
            set_toast("Um meteoro caiu por perto! Ferro meteorico exposto na cratera.", 3.5f);
            it = g_meteors.erase(it);
        } else {
            ++it;
        }
    }
}

void update_game(float dt) {
    if (!g_world) return;

    // Toast timer
    if (g_toast_time > 0.0f) g_toast_time -= dt;
    
    // ============= ATUALIZAR FEEDBACK VISUAL =============
    if (g_screen_flash_red > 0.0f) g_screen_flash_red -= dt * 2.5f;
    if (g_screen_flash_green > 0.0f) g_screen_flash_green -= dt * 2.5f;
    if (g_unlock_popup_timer > 0.0f) g_unlock_popup_timer -= dt;
    if (g_hotbar_bounce > 0.0f) g_hotbar_bounce -= dt * 4.0f;
    
    // Atualizar popups de coleta
    for (auto& popup : g_collect_popups) {
        popup.life -= dt;
        popup.y -= dt * 30.0f;  // Flutua para cima
    }
    g_collect_popups.erase(
        std::remove_if(g_collect_popups.begin(), g_collect_popups.end(),
            [](const CollectPopup& p) { return p.life <= 0.0f; }),
        g_collect_popups.end());
    
    // Atualizar onboarding
    update_onboarding(dt);
    
    // Atualizar fog of war do minimapa
    update_fog_of_war(dt);

    // Espalhamento de agua: consome a fila semeada por water_flood_from() (ao cavar). Progressivo de
    // proposito - alguns tiles por tick - pra a agua ENTRAR no buraco visivelmente. Ver world.h.
    if (g_world) update_water_flow(*g_world, dt);

    // Stats timer (periodically recompute terraform score)
    g_stats_timer += dt;
    if (g_stats_timer >= 2.0f || g_surface_dirty) {
        g_stats_timer = 0.0f;
        g_surface_dirty = false;
        recompute_terraform_score(*g_world);
    }

    // Hotkey states
    bool esc_pressed = key_pressed(KEY_ESCAPE, g_prev_esc);
    bool enter_pressed = key_pressed(KEY_ENTER, g_prev_enter);
    bool f5_pressed = key_pressed(KEY_F5, g_prev_f5);
    bool f9_pressed = key_pressed(KEY_F9, g_prev_f9);
    bool l_pressed = key_pressed(KEY_L, g_prev_l);
    bool q_pressed = key_pressed(KEY_Q, g_prev_q);
    bool f3_pressed = key_pressed(KEY_F3, g_prev_f3);
    bool f6_pressed = key_pressed(KEY_F6, g_prev_f6);
    bool f7_pressed = key_pressed(KEY_F7, g_prev_f7);
    bool h_pressed = key_pressed(KEY_H, g_prev_h);
    bool tab_pressed = key_pressed(KEY_TAB, g_prev_tab);
    bool b_pressed = key_pressed(KEY_B, g_prev_b);
    bool m_pressed = key_pressed(KEY_M, g_prev_m);
    bool r_pressed = key_pressed(KEY_R, g_prev_r);
    bool c_key_pressed = key_pressed(KEY_C, g_prev_c);
    bool f_pressed = key_pressed(KEY_F, g_prev_f);
    bool g_pressed = key_pressed(KEY_G, g_prev_g);
    bool t_pressed = key_pressed(KEY_T, g_prev_t);
    bool p_pressed = key_pressed(KEY_P, g_prev_p);
    bool u_pressed = key_pressed(KEY_U, g_prev_u);
    bool f4_pressed = key_pressed(KEY_F4, g_prev_f4);
    bool v_pressed = key_pressed(KEY_V, g_prev_v);   // transicao exterior<->interior (interiors.h)
    
    // === MAPA GRANDE (tecla M) ===
    if (m_pressed && g_state == GameState::Playing) {
        g_minimap.world_map_open = !g_minimap.world_map_open;
        if (g_minimap.world_map_open) {
            // Centralizar no jogador ao abrir
            g_minimap.world_pan_x = g_player.pos.x;
            g_minimap.world_pan_y = g_player.pos.y;
            g_minimap.world_zoom = 1.0f;
        }
    }
    
    // Controles do mapa grande
    if (g_minimap.world_map_open && g_state == GameState::Playing) {
        // ESC fecha o mapa
        if (esc_pressed) {
            g_minimap.world_map_open = false;
            g_prev_esc = true;  // Consumir o ESC para nao pausar
        }
        
        // WASD para mover o mapa
        float pan_speed = g_map_cfg.world_map_pan_speed * dt / g_minimap.world_zoom;
        if (key_down(KEY_W) || key_down(KEY_UP)) g_minimap.world_pan_y -= pan_speed;
        if (key_down(KEY_S) || key_down(KEY_DOWN)) g_minimap.world_pan_y += pan_speed;
        if (key_down(KEY_A) || key_down(KEY_LEFT)) g_minimap.world_pan_x -= pan_speed;
        if (key_down(KEY_D) || key_down(KEY_RIGHT)) g_minimap.world_pan_x += pan_speed;

        // Limitar pan aos limites do mundo
        g_minimap.world_pan_x = std::clamp(g_minimap.world_pan_x, 0.0f, (float)g_world->w);
        g_minimap.world_pan_y = std::clamp(g_minimap.world_pan_y, 0.0f, (float)g_world->h);

        // Clique para adicionar waypoint
        if (g_mouse_left_clicked) {
            // Converter posicao do mouse para coordenadas do mundo (raylib: sem HWND, o
            // tamanho da janela vem direto de GetScreenWidth/Height)
            int win_w = GetScreenWidth();
            int win_h = GetScreenHeight();

            float map_margin = 50.0f;
            float map_w = (float)win_w - map_margin * 2.0f;
            float map_h = (float)win_h - map_margin * 2.0f - 50.0f;
            float map_x = map_margin;
            float map_y = map_margin;
            
            // Verificar se clicou dentro do mapa
            if (g_mouse_x >= map_x && g_mouse_x <= map_x + map_w &&
                g_mouse_y >= map_y && g_mouse_y <= map_y + map_h) {
                
                float zoom = g_minimap.world_zoom;
                float tiles_visible_x = (float)g_world->w / zoom;
                float tiles_visible_y = (float)g_world->h / zoom;
                
                float map_aspect = map_w / map_h;
                float world_aspect = tiles_visible_x / tiles_visible_y;
                if (map_aspect > world_aspect) {
                    tiles_visible_x = tiles_visible_y * map_aspect;
                } else {
                    tiles_visible_y = tiles_visible_x / map_aspect;
                }
                
                float start_world_x = g_minimap.world_pan_x - tiles_visible_x * 0.5f;
                float start_world_y = g_minimap.world_pan_y - tiles_visible_y * 0.5f;
                
                float px_per_tile_x = map_w / tiles_visible_x;
                float px_per_tile_y = map_h / tiles_visible_y;
                
                int world_x = (int)(start_world_x + (g_mouse_x - map_x) / px_per_tile_x);
                int world_y = (int)(start_world_y + (g_mouse_y - map_y) / px_per_tile_y);
                
                if (g_world->in_bounds(world_x, world_y)) {
                    add_waypoint(world_x, world_y);
                }
            }
            g_mouse_left_clicked = false;
        }
        
        // R para remover waypoint mais proximo do centro da visao
        if (r_pressed) {
            remove_nearest_waypoint((int)g_minimap.world_pan_x, (int)g_minimap.world_pan_y);
        }
        
        // C para limpar todos os waypoints
        if (c_key_pressed) {
            clear_all_waypoints();
        }
        
        // Nao processar movimento do jogador enquanto mapa esta aberto
        return;
    }

    // F3 alterna entre modos de debug: normal -> lightmap -> lights -> off
    if (f3_pressed) {
        if (!g_debug && !g_debug_lightmap && !g_debug_lights) {
            g_debug = true;  // Primeiro: debug basico
        } else if (g_debug && !g_debug_lightmap) {
            g_debug = false;
            g_debug_lightmap = true;  // Segundo: lightmap
        } else if (g_debug_lightmap && !g_debug_lights) {
            g_debug_lightmap = false;
            g_debug_lights = true;  // Terceiro: luzes
        } else {
            g_debug = false;
            g_debug_lightmap = false;
            g_debug_lights = false;  // Desliga tudo
        }
    }

    // State machine - Menu/Paused/Settings/Dead input handling. Extracted verbatim to
    // ui_menu.cpp's update_menu_input() - see ui_menu.h for details; each original
    // "return;" became "return true;" (this frame's input was fully consumed by a menu
    // screen), with a final "return false;" added for the Playing state (no menu state
    // matched - fall through to this file's own Playing-state input code below).
    if (update_menu_input(dt, esc_pressed, enter_pressed, f5_pressed, f9_pressed, l_pressed, q_pressed)) return;

    // Playing state
    
    // ESC fecha menu de construcao ou pausa o jogo
    if (esc_pressed) {
        if (g_show_build_menu) {
            g_show_build_menu = false;  // ESC fecha menu de construcao
            return;
        }
        g_state = GameState::Paused;
        return;
    }
    
    // Toggle build menu with Tab or B
    if (tab_pressed || b_pressed) {
        g_show_build_menu = !g_show_build_menu;
        if (g_show_build_menu) {
            g_build_menu_selection = 0;
            // Onboarding: dica ao abrir menu de construcao pela primeira vez
            if (!g_onboarding.shown_first_build_menu) {
                show_tip("W/S para navegar, Enter para construir, ESC para fechar", g_onboarding.shown_first_build_menu);
            }
        }
        return;
    }
    
    // Build menu navigation and actions. Extracted verbatim to building_interaction.cpp's
    // update_build_menu_input() - see building_interaction.h for details; the original
    // unconditional "return;" (when g_show_build_menu was open) became "return true;", with
    // "return false;" added for when the menu isn't open (fall through to the rest of this
    // function) - same "return true consumes the frame" convention as
    // update_menu_input() above.
    if (update_build_menu_input()) return;

    // Return to base with H
    if (h_pressed) {
        spawn_player_at_base();
        set_toast("Retornou a base!");
        return;
    }

    if (f7_pressed) {
        reload_physics_config(true);
        reload_terrain_config(true);
        reload_sky_config(true);
        reload_camera_config(true);
        reload_mining_config(true);
        reload_player_visual_config(true);
        reset_player_physics_runtime(false);
        set_toast(std::string("Configs recarregadas: ") + g_physics_config_path + " | " + g_terrain_config_path + " | " + g_sky_config_path + " | " + g_camera_config_path + " | " + g_mining_config_path + " | " + g_player_visual_config_path, 3.5f);
    }

    if (f6_pressed) {
        build_physics_test_map(*g_world);
        return;
    }

    // Update modules (energy/water/oxygen production, terraforming)
    update_modules(*g_world, dt);

    // Hotbar: teclas 1-6 selecionam os 6 slots VISIVEIS da barra de elementos. A lista de
    // elementos era duplicada aqui e em ui_hud.cpp; agora vem de kElementSlots (ui_hud.h), fonte
    // unica - com rolagem, duas copias divergiriam na hora. Rolar a barra muda o que 1-6 fazem,
    // que e' o comportamento esperado de barra rolavel.
    {
        int scroll = hud_elements_scroll();
        for (int i = 0; i < kElementVisibleSlots; ++i) {
            int idx = scroll + i;
            if (idx >= kElementSlotCount) break;
            if (key_down('1' + i)) g_selected = kElementSlots[idx];
        }
    }
    
    // Modules: 7-0 (dynamically based on unlocks)
    std::vector<Block> module_slots;
    if (g_unlocks.solar_unlocked) module_slots.push_back(Block::SolarPanel);
    if (g_unlocks.water_extractor_unlocked) module_slots.push_back(Block::WaterExtractor);
    if (g_unlocks.o2_generator_unlocked) module_slots.push_back(Block::OxygenGenerator);
    if (g_unlocks.greenhouse_unlocked) module_slots.push_back(Block::Greenhouse);
    if (g_unlocks.co2_factory_unlocked) module_slots.push_back(Block::CO2Factory);
    if (g_unlocks.habitat_unlocked) module_slots.push_back(Block::Habitat);
    if (g_unlocks.terraformer_unlocked) module_slots.push_back(Block::TerraformerBeacon);
    
    for (int i = 0; i < (int)module_slots.size() && i < 4; ++i) {
        int key = (i < 3) ? ('7' + i) : '0';
        if (key_down(key)) g_selected = module_slots[i];
    }

    // ============= MOVIMENTO 3D (TIMESTEP FIXO) =============
    float cam_yaw_rad = g_camera.yaw * (kPi / 180.0f);
    float cam_forward_x = -std::sin(cam_yaw_rad);
    float cam_forward_z = -std::cos(cam_yaw_rad);
    float cam_right_x = std::cos(cam_yaw_rad);
    float cam_right_z = -std::sin(cam_yaw_rad);

    float input_forward = 0.0f;
    float input_right = 0.0f;
    if (key_down(KEY_W) || key_down(KEY_UP)) input_forward += 1.0f;
    if (key_down(KEY_S) || key_down(KEY_DOWN)) input_forward -= 1.0f;
    if (key_down(KEY_A) || key_down(KEY_LEFT)) input_right -= 1.0f;
    if (key_down(KEY_D) || key_down(KEY_RIGHT)) input_right += 1.0f;

    Vec2 move_world = {
        input_forward * cam_forward_x + input_right * cam_right_x,
        input_forward * cam_forward_z + input_right * cam_right_z
    };
    bool has_input = (move_world.x != 0.0f || move_world.y != 0.0f);
    if (has_input) move_world = vec2_normalize(move_world);

    // VK_SHIFT has no direct raylib equivalent (raylib splits left/right shift).
    bool run_key = key_down(KEY_LEFT_SHIFT) || key_down(KEY_RIGHT_SHIFT);
    bool jump_held = key_down(KEY_SPACE);
    bool jump_pressed = jump_held && !g_physics.jump_was_held;
    bool jump_released = !jump_held && g_physics.jump_was_held;
    g_physics.jump_was_held = jump_held;
    bool descend_held = key_down(KEY_LEFT_CONTROL) || key_down(KEY_RIGHT_CONTROL);

    PlayerPhysicsInput physics_input{};
    physics_input.move = move_world;
    physics_input.has_move = has_input;
    physics_input.run = run_key;
    physics_input.jump_pressed = jump_pressed;
    physics_input.jump_held = jump_held;
    physics_input.jump_released = jump_released;
    physics_input.descend_held = descend_held;
    step_player_physics(physics_input, dt);

    // Poeira da rajada de pouso (ver g_physics.landing_dust_timer, player_physics.h) -
    // contagem regressiva por frame; render_world() desenha enquanto > 0.
    g_physics.landing_dust_timer = std::max(0.0f, g_physics.landing_dust_timer - dt);

    if (key_down(KEY_KP_ADD) || key_down(KEY_EQUAL)) {
        g_camera.distance = std::max(g_camera.min_distance, g_camera.distance - 10.0f * dt);
    }
    if (key_down(KEY_KP_SUBTRACT) || key_down(KEY_MINUS)) {
        g_camera.distance = std::min(g_camera.max_distance, g_camera.distance + 10.0f * dt);
    }

    g_player.anim_frame += dt;
    g_player.is_moving = vec2_length(g_player.vel) > 0.15f;
    if (g_player.is_moving) g_player.walk_timer += dt * (run_key ? 1.5f : 1.0f);
    else g_player.walk_timer *= 0.9f;

    // walk_blend: 1 so' quando ha' input de movimento ativo, nao apenas velocidade residual -
    // deslizando no gelo (soltou o movimento, corpo ainda escorregando por inercia) precisa
    // parecer deslizar de pe' parado, nao continuar o ciclo de passada. Suavizado (nao um
    // corte seco) pra nao "travar" as pernas de repente ao soltar o movimento.
    // SO' anda quem esta NO CHAO. Antes bastava ter input + velocidade horizontal, entao voar com o
    // jetpack segurando WASD tocava o ciclo de passada e as pernas balancavam no ar (relato do
    // jogador: "meu personagem parece andar quando esta voando"). Nadar tambem nao e' caminhar.
    bool grounded_for_walk = g_player.on_ground && !g_player.jetpack_active && !g_physics.in_water;
    float walk_blend_target = (has_input && g_player.is_moving && grounded_for_walk) ? 1.0f : 0.0f;
    float walk_blend_rate = 9.0f * dt;
    if (g_player.walk_blend < walk_blend_target) g_player.walk_blend = std::min(walk_blend_target, g_player.walk_blend + walk_blend_rate);
    else g_player.walk_blend = std::max(walk_blend_target, g_player.walk_blend - walk_blend_rate);

    update_meteors(dt);
    update_creatures(dt);

    // === SURVIVAL MECHANICS ===
    // Astronaut dies from: no oxygen OR no water (after 30 seconds without)
    static float dehydration_timer = 0.0f;  // Time without water
    static float suffocation_timer = 0.0f;  // Time without oxygen
    static float damage_tick = 0.0f;

    const float kDamageDelay = 15.0f;  // 15 seconds before damage starts (was 30s)
    // NOTA: frio extremo NAO mata mais o jogador (removido a pedido do usuario - "ele tem
    // roupa de astronauta, isso nao deveria ocorrer... deveria morrer de sede ou falta de
    // oxigenio, mas nao de frio"). O traje ja e um sistema termicamente selado por premissa
    // do jogo; g_temperature continua existindo (mostrado no HUD, usado pela Fabrica de CO2
    // e pela progressao de fases), so nao causa mais dano/morte por si so. A degradacao do
    // traje (g_suit_integrity, ver comentario completo no topo do arquivo) e o unico jeito
    // de ficar mais vulneravel longe da base hoje - isso sim amplifica sede/fome/oxigenio.
    // Mesmo predicado do reabastecimento (update_modules) e do HUD - ver player_in_base_complex()
    // em modules_building.h: disco da zona segura OU dentro do corredor/estufa.
    bool near_base_shelter = player_in_base_complex();

    // Integridade do traje: dreno CONTINUO (nao timer-depois-dano) enquanto fora do abrigo -
    // ~2.5/min, ~40min pra zerar ficando fora o tempo todo (decadencia lenta de proposito).
    // So sobe reparando na hora (tecla F, mais abaixo apos update_mining_and_placement) - ver
    // comentario completo em g_suit_integrity (topo do arquivo).
    const float kSuitDecayPerMin = 2.5f;
    if (!near_base_shelter) {
        g_suit_integrity = std::max(0.0f, g_suit_integrity - kSuitDecayPerMin / 60.0f * dt);
    }

    // === QUEIMADURA DE LAVA ===
    // Pedido do jogador: "quando passo por cima deveria comecar a queimar e a dar dano".
    // Antes a lava era 100% decorativa - nao existia NENHUM caminho de dano por lava no jogo
    // (o unico dano de terreno era queda), entao dava pra ficar parado dentro dela sem
    // consequencia nenhuma. Dano CONTINUO (nao timer-depois-dano como sede/oxigenio): pisar
    // em lava e' consequencia imediata, nao uma privacao que se acumula.
    {
        static float lava_burn_accum = 0.0f;   // fracao de HP acumulada (dano e' int)
        static float lava_warn_timer = 0.0f;
        lava_warn_timer = std::max(0.0f, lava_warn_timer - dt);

        int lava_tx = world_to_tile(g_player.pos.x);
        int lava_tz = world_to_tile(g_player.pos.y);
        bool standing_in_lava = false;
        if (g_world->in_bounds(lava_tx, lava_tz) && g_world->get_ground(lava_tx, lava_tz) == Block::Lava) {
            // Precisa estar NO chao/junto dele - voar por cima de lava nao queima (mesma
            // licao do bug de voar sobre agua: "o chao embaixo e' X" nao e' "estou em X").
            float lava_surface = surface_height_at(*g_world, lava_tx, lava_tz);
            standing_in_lava = (g_player.pos_y <= lava_surface + 1.2f);
        }

        if (standing_in_lava) {
            // ~12 HP/s: da pra atravessar um riacho estreito correndo e sobreviver, mas ficar
            // parado dentro de um lago de lava mata em ~8s. Tambem consome o traje rapido.
            lava_burn_accum += 12.0f * dt;
            g_suit_integrity = std::max(0.0f, g_suit_integrity - 6.0f * dt);
            int whole = (int)lava_burn_accum;
            if (whole > 0) {
                lava_burn_accum -= (float)whole;
                g_player.hp = std::max(0, g_player.hp - whole);
            }
            if (lava_warn_timer <= 0.0f) {
                set_toast("QUEIMANDO! Saia da lava!", 1.2f);
                lava_warn_timer = 1.0f;
            }
            if (g_player.hp <= 0) {
                respawn_player_at_base("Queimado pela lava");
                lava_burn_accum = 0.0f;
            }
        } else {
            lava_burn_accum = 0.0f;
        }
    }
    // A porta da cupula NAO tem mais teleporte. Ela existia porque a colisao da base era um
    // cilindro invisivel de 360 graus sem excecao de angulo, entao a unica forma de atravessar em
    // qualquer ponto era um pulinho por proximidade. Agora as paredes sao blocos de verdade com
    // VAOS reais nos corredores (ver generate_base) - o jogador simplesmente anda pela porta, que
    // e' desenhada como escotilha em volta do proprio vao. Nada de teleporte, nada de gatilho.


    // Track time without resources
    if (g_water_res <= 0.0f) {
        dehydration_timer += dt;
    } else {
        dehydration_timer = 0.0f; // Reset when water is available
    }

    if (g_oxygen <= 0.0f) {
        suffocation_timer += dt;
    } else {
        suffocation_timer = 0.0f; // Reset when oxygen is available
    }

    // Damage tick (every second)
    damage_tick += dt;
    if (damage_tick >= 1.0f) {
        damage_tick = 0.0f;

        // Suffocation damage - only after 30 seconds without oxygen
        if (suffocation_timer > kDamageDelay) {
            g_player.hp = std::max(0, g_player.hp - 10);
            if (g_player.hp <= 0) {
                respawn_player_at_base("Sufocamento");
                return;
            }
        }

        // Dehydration damage - only after 30 seconds without water
        if (dehydration_timer > kDamageDelay) {
            g_player.hp = std::max(0, g_player.hp - 8);
            if (g_player.hp <= 0) {
                respawn_player_at_base("Desidratacao");
                return;
            }
        }
    }

    // Warnings when resources are empty (before damage starts)
    // Increased interval from 3s to 5s to reduce spam
    static float warn_timer = 0.0f;
    warn_timer += dt;
    if (warn_timer >= 5.0f) {
        warn_timer = 0.0f;

        if (g_suit_integrity <= 30.0f) {
            set_toast("Traje se degradando! [F] para reparar (Metal/Componentes)", 2.5f);
        }

        if (g_oxygen <= 0.0f && suffocation_timer < kDamageDelay) {
            int seconds_left = (int)(kDamageDelay - suffocation_timer);
            set_toast("SEM OXIGENIO! Dano em " + std::to_string(seconds_left) + "s!", 2.5f);
        } else if (g_water_res <= 0.0f && dehydration_timer < kDamageDelay) {
            int seconds_left = (int)(kDamageDelay - dehydration_timer);
            set_toast("SEM AGUA! Dano em " + std::to_string(seconds_left) + "s!", 2.5f);
        } else if (g_oxygen < 15.0f && g_oxygen > 0.0f) {
            set_toast("Aviso: Oxigenio baixo! Construa Gerador de O2.");
            // Onboarding: dica para voltar a base (uma vez), depois dica para se tornar
            // independente da base (shown_low_oxygen - antes uma flag morta, nunca
            // disparada; agora tem um gatilho proprio distinto do de shown_return_to_base).
            if (!g_onboarding.shown_return_to_base) {
                show_tip("H para voltar a base e recarregar oxigenio", g_onboarding.shown_return_to_base);
            } else if (!g_onboarding.shown_low_oxygen) {
                show_tip("Construa um Gerador de Oxigenio para nao depender so da base", g_onboarding.shown_low_oxygen);
            }
        } else if (g_water_res < 15.0f && g_water_res > 0.0f) {
            set_toast("Aviso: Agua baixa! Construa Extrator de Agua.");
            // Onboarding: dica para agua baixa
            if (!g_onboarding.shown_low_water) {
                show_tip("Quebre blocos de gelo para obter agua", g_onboarding.shown_low_water);
            }
        }
    }

    // Camera follow sincronizado com interpolacao da fisica
    float cam_speed = 6.0f;
    Vec2 render_pos = get_player_render_pos();
    g_cam_pos.x = approach(g_cam_pos.x, render_pos.x, cam_speed * dt * std::fabs(render_pos.x - g_cam_pos.x) + 0.5f * dt);
    g_cam_pos.y = approach(g_cam_pos.y, render_pos.y, cam_speed * dt * std::fabs(render_pos.y - g_cam_pos.y) + 0.5f * dt);

    // Mouse targeting + mining/placement raycast/actions + item pickup + particle
    // simulation. Extracted verbatim to building_interaction.cpp's
    // update_mining_and_placement() - see building_interaction.h for details. The five
    // local [&]-capturing lambdas that used to live in this block (placeable_tile/
    // blocks_raycast/ray_aabb_hit/ray_hits_tile/placeable_tile_for_place) are now named,
    // explicit-parameter functions there instead - the highest-value mechanical change of
    // this extraction stage, per the refactor plan.
    update_mining_and_placement(dt);

    // === TRANSICAO EXTERIOR <-> INTERIOR (tecla V) ===
    // Sistema generico: le a tabela kInteriors (interiors.h). Adicionar laboratorio/dormitorio/
    // deposito/centro de pesquisa novo e' UMA linha lá - nao existe codigo por porta aqui.
    // Depois de update_mining_and_placement de proposito: a alcova de porta e' feita de blocos
    // is_base_structure (nao mineraveis), entao mirar nela nunca disputa com a mineracao.
    interiors_update(v_pressed);

    // Upgrade de modulo (tecla R, ver try_upgrade_module()) - so quando mirando um modulo
    // ja construido em alcance, reaproveitando o mesmo raycast que update_mining_and_placement
    // acabou de atualizar (g_has_target/g_target_x/y/g_target_in_range).
    if (r_pressed && g_has_target && g_target_in_range) {
        Block target_block = g_world->get(g_target_x, g_target_y);
        if (is_module(target_block)) {
            try_upgrade_module(g_target_x, g_target_y);
        }
    }

    // Reparo do traje (tecla F, ver g_suit_integrity) - funciona em qualquer lugar, a
    // qualquer momento (nao gatilhado so na base) de proposito: a tensao "upgrade (R) vs.
    // reparo (F)" so existe se puder ser decidida na hora, minerando, sem um desvio ate a
    // base diluir a escolha.
    if (f_pressed) {
        if (g_suit_integrity >= 100.0f) {
            set_toast("Traje ja esta 100% intacto.", 1.5f);
        } else {
            CraftCost repair_cost = get_suit_repair_cost();
            if (can_afford(repair_cost)) {
                spend_cost(repair_cost);
                g_suit_integrity = std::min(100.0f, g_suit_integrity + 25.0f);
                set_toast("Traje reparado (+25%).", 1.5f);
            } else {
                set_toast("Recursos insuficientes pro reparo! (" + module_cost_string(repair_cost) + ")", 1.5f);
            }
        }
    }

    // Refino de Liga (tecla G, ver try_refine_at_workshop()) - so quando mirando uma
    // Oficina ja construida em alcance, mesmo raycast reaproveitado do R/F acima.
    if (g_pressed) {
        if (g_has_target && g_target_in_range && g_world->get(g_target_x, g_target_y) == Block::Workshop) {
            try_refine_at_workshop(g_target_x, g_target_y);
        } else {
            set_toast("Mire numa Oficina construida e no alcance para refinar.", 1.5f);
        }
    }

    // Scanner (tecla T, ver scan_for_points_of_interest() em minimap.cpp) - funciona em
    // qualquer lugar, a qualquer momento, mesmo espirito do F/G: informacao, nao custa
    // recurso, so cooldown.
    if (t_pressed) {
        scan_for_points_of_interest();
    }

    // Fabricar Pistola de Laser (tecla P, ver try_craft_laser_pistol()) - "de campo", funciona
    // em qualquer lugar (sem precisar de Oficina, ver comentario na declaracao).
    if (p_pressed) {
        try_craft_laser_pistol();
    }

    // Tecla U: aprimorar a Pistola de Laser (Mk I -> Mk II -> Mk III). Passa o alvo do raycast quando
    // ele existe, mas try_upgrade_weapon tambem funciona sem alvo - obrigatorio, porque com a pistola
    // equipada o raycast nem roda (ver o comentario da definicao em modules_building.cpp).
    if (u_pressed) {
        try_upgrade_weapon(g_target_x, g_target_y, g_has_target && g_target_in_range);
    }

    // F4: cicla o overlay de debug de distribuicao geologica no mapa completo (M). Ferramenta de
    // desenvolvimento - ver g_geo_debug_mode em minimap.h.
    if (f4_pressed) {
        g_geo_debug_mode = (g_geo_debug_mode + 1) % kGeoDebugModeCount;
        const char* names[kGeoDebugModeCount] = {"desligado", "RECURSOS", "ALTITUDE"};
        set_toast(std::string("[F4] Debug geologico: ") + names[g_geo_debug_mode], 2.0f);
    }
}

// ============= Window Procedure / WinMain =============
// g_last_mouse_x/g_last_mouse_y/g_mouse_captured, setup_opengl(), WindowProc(), and
// WinMain() all moved to win32_platform.h/.cpp (verbatim) - the win32_platform extraction
// stage, the LAST stage of this whole refactor. win32_platform.h (included at the top of
// this file) declares nothing (see there for why); WinMain calls render_world()/
// update_game() above via its own forward declarations instead. This is the end of
// main.cpp: nothing follows WinMain() in win32_platform.cpp either.
