#pragma once

// NOTE: this header is included by both raylib-migrated files (camera.cpp, sky.cpp,
// minimap.cpp, ui_hud.cpp, ui_menu.cpp, building_interaction.cpp, win32_platform.cpp,
// main.cpp - via raylib_platform.h) and any remaining legacy files - it must not include
// platform.h (windows.h + GL/gl.h) or raylib_platform.h itself, since raylib.h and windows.h
// cannot both be visible in the same translation unit (see raylib_platform.h's own comment).
// It only needs plain STL types.
#include <string>
#include <vector>
#include <cstdint>

// ============= Config struct definitions =============
// Extracted verbatim from main.cpp (original lines ~1017-1237).
// Struct/type DEFINITIONS ONLY. The load/save logic (reload_*_config,
// write_default_*_config) and the global instances (g_terrain_cfg, etc.) stay in
// main.cpp for now — that is a separate later extraction step (config_io / game_state).

struct TerrainConfig {
    // NOTA: esses defaults ficavam desatualizados em relacao ao JSON real ha varias sessoes
    // (so importam no raro caso de escrever E reler o default falharem no mesmo boot) - agora
    // sincronizados com terrain_config.json/write_default_terrain_config de proposito, ja que
    // uma dessincronia exatamente assim contribuiu pra uma regressao de terreno nesta sessao.
    float macro_scale = 0.00045f;
    float ridge_scale = 0.0032f;
    float valley_scale = 0.0013f;
    float detail_scale = 0.0095f;
    float warp_scale = 0.0022f;
    float warp_strength = 34.0f;

    float macro_weight = 0.50f;
    float ridge_weight = 0.62f;
    float valley_weight = 0.64f;
    float detail_weight = 0.06f;

    float plateau_level = 0.57f;
    float plateau_flatten = 0.35f;

    float min_height = 2.0f;
    float max_height = 130.0f;
    float sea_height = 20.0f;
    float snow_height = 98.0f;

    int thermal_erosion_passes = 4;
    int hydraulic_erosion_passes = 4;
    int smooth_passes = 2;
    float erosion_strength = 0.38f;
    float thermal_talus = 0.021f;

    float temp_scale = 0.0016f;
    float moisture_scale = 0.0019f;
    float biome_blend = 0.16f;

    float fissure_scale = 0.0080f;
    float fissure_depth = 0.060f;
    float crater_scale = 0.0022f;
    float crater_depth = 0.11f;
    float detail_object_density = 0.060f;

    // Rios/vulcoes: quantidade de sementes geradas pro mapa (ver World::gen() Passo 3.5).
    int river_seed_count = 16;
    int volcano_seed_count = 5;
};

struct SkyConfig {
    float stars_density = 1650.0f;
    float stars_parallax = 0.008f;
    float nebula_alpha = 0.22f;
    float nebula_parallax = 0.016f;
    float cloud_alpha = 0.20f;
    float cloud_parallax = 0.050f;

    float planet_radius = 300.0f;
    float planet_distance = 1150.0f;
    float planet_orbit_speed = 0.060f;
    float planet_parallax = 0.028f;

    float sun_radius = 52.0f;
    float sun_distance = 820.0f;
    float sun_halo_size = 2.25f;
    float bloom_intensity = 0.45f;

    float moon_radius = 36.0f;
    float moon_distance = 940.0f;
    float moon_orbit_speed = 0.48f;
    float moon_parallax = 0.040f;

    float moon2_radius = 22.0f;
    float moon2_distance = 1010.0f;
    float moon2_orbit_speed = 0.98f;
    float moon2_parallax = 0.050f;

    float atmosphere_horizon_boost = 0.45f;
    float atmosphere_zenith_boost = 0.24f;
    float horizon_fade = 0.28f;

    float fog_start_factor = 0.34f;
    float fog_end_factor = 0.80f;
    float fog_distance_bonus = 15.0f;

    float eclipse_frequency_days = 8.0f;
    float eclipse_strength = 0.50f;
};

struct CameraConfig {
    float spawn_distance = 4.2f;
    float spawn_pitch = 24.0f;
    float spawn_yaw = 180.0f;

    float open_pitch = 26.0f;
    float semi_pitch = 50.0f;
    float cave_pitch = 72.0f;
    float emergency_pitch = 87.0f;

    float open_distance_scale = 1.00f;
    float semi_distance_scale = 0.86f;
    float cave_distance_scale = 0.74f;
    float emergency_distance_scale = 0.62f;

    float open_target_lift = 0.06f;
    float semi_target_lift = 0.46f;
    float cave_target_lift = 1.16f;
    float emergency_target_lift = 1.72f;

    float pitch_lerp = 0.14f;
    float distance_lerp = 0.14f;
    float lift_lerp = 0.15f;
    // Suaviza especificamente a altura vertical do foco da camera (nao a posicao X/Z, nem a
    // do proprio personagem) - sem isso, cada micro-degrau de terreno subido instantaneamente
    // por try_step_climb (player_physics.cpp) aparecia como um solavanco na camera; subindo
    // uma ladeira em varios degraus seguidos (dependendo do angulo, cruza mais fronteiras de
    // tile por metro andado) isso somava e virava a "tremida" reportada. Valor mais baixo =
    // mais suave (mais atraso), mais alto = segue o jogador mais de perto.
    float step_height_lerp = 0.22f;
    // Sensibilidade VERTICAL propria (era o literal 0.5 escondido aqui, metade da horizontal).
    // Junto com o auto-recentrar do adaptativo, isso fazia o olhar vertical parecer morto: o
    // movimento ja rendia metade e ainda era desfeito em ~1.2s. Com o recentrar corrigido
    // (camera.cpp), 0.85 deixa cima/baixo quase tao responsivo quanto esquerda/direita, sem ficar
    // nervoso - o eixo vertical tem menos curso util (127 graus contra 360) e merece um pouco menos.
    float pitch_sensitivity = 0.85f;
    // FAIXA DE PITCH, no config pra dar pra ajustar sem recompilar. Ver a nota longa em camera.h:
    // esta e' uma camera em ORBITA que olha sempre pro jogador, entao pitch negativo = camera ABAIXO
    // do jogador olhando pra CIMA, que e' o que mostra o ceu. Era travado em 2.0 (nunca descia
    // abaixo do jogador) e o ceu nunca passava de uma faixa no topo da tela.
    float min_pitch = -38.0f;
    float max_pitch = 89.0f;

    float cave_depth_start = 0.45f;
    float cave_depth_end = 2.60f;
    float enclosed_start = 0.28f;
    float enclosed_end = 0.76f;
    float occlusion_full = 0.64f;
    float emergency_hidden_time = 0.50f;

    float transparency_alpha = 0.30f;
    float transparency_fade_in = 10.0f;
    float transparency_fade_out = 5.0f;

    float collision_probe_step = 0.14f;
    float collision_padding = 0.34f;
    float min_collision_distance = 0.70f;
    float debug_ray_length = 8.0f;
};

struct MiningConfig {
    float hit_interval = 0.20f;          // intervalo base entre impactos
    float hit_interval_min = 0.09f;      // limite para nao travar em FPS alto
    float early_game_speed_mult = 1.12f; // inicio mais rapido
    float late_game_speed_mult = 0.85f;  // fim um pouco mais lento (balance)

    int hits_sand = 1;
    int hits_dirt = 2;
    int hits_ice = 2;
    int hits_snow = 2;
    int hits_stone = 3;
    int hits_metal = 4;
    int hits_crystal = 5;
    int hits_ore = 4;
    int hits_wood = 2;
    int hits_modules = 4;
};

struct PlayerVisualConfig {
    float breathing_amp = 0.018f;
    float breathing_speed = 2.2f;
    float walk_bob_amp = 0.060f;
    float walk_bob_speed = 12.5f;
    float walk_weight_amp = 0.10f;
    float mine_impact_amp = 0.19f;
    float idle_sway_amp = 0.016f;
    float idle_sway_speed = 1.6f;

    float visor_reflect_alpha = 0.36f;
    float headlamp_intensity = 0.74f;
    float suit_wear_strength = 0.20f;
    float suit_dirt_strength = 0.24f;
    float suit_frost_strength = 0.32f;
    float suit_damage_strength = 0.45f;
};

// Extracted verbatim from main.cpp (original line ~971, moved here as part of the
// config_io extraction stage since config_io.cpp needs the full type to instantiate
// reload_config<PhysicsConfig> and to implement apply_physics_config_overrides).
struct PhysicsConfig {
    float fixed_timestep = 1.0f / 120.0f;
    int max_substeps = 10;

    float max_speed = 4.8f;
    float run_multiplier = 1.42f;
    float ground_acceleration = 26.0f;
    float ground_deceleration = 22.0f;
    float air_acceleration = 9.0f;
    float air_deceleration = 6.5f;
    float ground_friction = 19.0f;
    float air_friction = 1.4f;

    float gravity = 24.0f;
    float rise_multiplier = 1.0f;
    float fall_multiplier = 2.05f;
    float jump_velocity = 8.1f;
    float jump_buffer = 0.12f;
    float coyote_time = 0.10f;
    float jump_cancel_multiplier = 2.8f;
    float terminal_velocity = 38.0f;

    // Dano de queda: so na transicao aereo->chao (nao repete parado), pousar na agua nunca
    // doi. min_speed fica acima do que um pulo normal alcanca (jump_velocity=8.1), entao so
    // quedas de verdade (penhasco, vulcao, jetpack sem combustivel) causam dano.
    float fall_damage_min_speed = 14.0f;
    float fall_damage_per_unit = 3.5f;
    float fall_damage_max = 70.0f;

    // Assistencia de pouso do jato (pedido do jogador: "so ligue poucos segundos/metros
    // antes de atingir o chao e de aquele efeito de flutuar", rajada instantanea, nao
    // freio gradual durante a queda inteira - ver apply_single_physics_step,
    // player_physics.cpp). trigger_height e' a distancia do chao (em unidades de mundo,
    // "poucos metros") que dispara a rajada; safe_margin e' a folga abaixo de
    // fall_damage_min_speed que a rajada garante (maior folga = pouso mais suave).
    float landing_assist_trigger_height = 4.0f;
    float landing_assist_safe_margin = 1.0f;

    float ground_snap = 0.20f;
    float ground_tolerance = 0.06f;

    float step_height = 2.0f;
    // Quanto o jogador ACOMPANHA o terreno ao descer sem virar queda. Tem que ser > kHeightScale
    // (0.25): uma unica unidade de heightmap pra baixo e maior que ground_snap (0.20), entao antes
    // deste campo TODO degrau descendente perdia contato com o chao e virava microqueda - a causa
    // medida do "microtravamento em terreno irregular". 0.55 cobre 2 unidades com folga.
    float step_down_snap = 0.55f;
    float step_probe_distance = 0.54f;

    float slope_limit_normal_y = 0.70f;
    float slope_slide_accel = 7.5f;
    float slope_uphill_speed_mult = 0.82f;
    float slope_downhill_speed_mult = 1.08f;

    float max_move_per_substep = 0.34f;
    float collision_skin = 0.0015f;
    float collider_width = 0.62f;
    float collider_depth = 0.62f;
    float collider_height = 1.80f;
    float rotation_smoothing = 14.0f;

    float terrain_ice_speed = 1.04f;
    float terrain_ice_accel = 0.55f;
    // Separado de terrain_ice_accel de proposito: parar (soltar o movimento) precisa ser
    // MUITO mais lento que acelerar pra sentir como deslizar por inercia no gelo, nao so
    // "andar mais devagar". Antes disso o mesmo multiplicador (0.55) era reusado pra
    // acelerar E desacelerar, e como cfg.ground_deceleration base ja e' alto (22.0), o
    // jogador ainda parava quase na hora (~0.4s) - sem sensacao nenhuma de gelo escorregadio.
    float terrain_ice_decel = 0.07f;
    float terrain_ice_friction = 0.10f;
    float terrain_sand_speed = 0.74f;
    float terrain_sand_accel = 0.80f;
    float terrain_sand_friction = 1.30f;
    float terrain_stone_speed = 1.00f;
    float terrain_stone_accel = 1.00f;
    float terrain_stone_friction = 1.00f;
    float terrain_mud_speed = 0.58f;
    float terrain_mud_accel = 0.65f;
    float terrain_mud_friction = 1.95f;
    float terrain_water_speed = 0.42f;
    float terrain_water_accel = 0.45f;
    float terrain_water_friction = 1.70f;

    float jetpack_thrust = 12.0f;
    float jetpack_fuel_consume = 15.0f;
    float jetpack_fuel_regen = 25.0f;
    float jetpack_gravity_mult = 0.35f;
    float jetpack_max_up_speed = 6.0f;
    // Voo horizontal com o jetpack usava a mesma aceleracao/velocidade fraca de uma queda
    // livre comum (air_acceleration/max_speed sem boost) - dava pra subir rapido mas quase
    // nao avancava pra frente. Esses dois so entram em jogo quando p.jetpack_active==true.
    float jetpack_speed_mult = 1.8f;
    float jetpack_air_accel = 22.0f;
};

struct BaseConfig {
    float safe_radius = 15.0f;
    float recharge_oxygen_rate = 3.0f;
    float recharge_water_rate = 2.4f;
    float recharge_food_rate = 1.2f;
    float repair_player_hp_per_sec = 0.80f;
    float jetpack_refuel_per_sec = 7.0f;
    float auto_save_cooldown = 45.0f;
    float beacon_height = 9.0f;
    float beacon_alpha = 0.42f;
    float beacon_pulse_speed = 2.0f;
};

struct MapConfig {
    float minimap_size = 188.0f;
    float minimap_zoom = 1.0f;
    float minimap_zoom_min = 0.45f;
    float minimap_zoom_max = 2.8f;
    float minimap_scan_radius = 10.0f;
    float minimap_update_interval = 0.08f;
    float minimap_follow_lerp = 0.22f;

    float world_map_zoom = 1.0f;
    float world_map_zoom_min = 0.35f;
    float world_map_zoom_max = 4.0f;
    float world_map_pan_speed = 85.0f;
    float waypoint_pick_radius = 2.2f;
    int max_waypoints = 24;
};

struct MapWaypoint {
    int x = 0;
    int y = 0;
    float r = 1.0f;
    float g = 0.3f;
    float b = 0.3f;
    std::string label;
    bool visible = true;
};

struct UiRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct MiniMapRuntime {
    std::vector<uint8_t> explored;
    std::vector<uint8_t> pixels;
    std::vector<MapWaypoint> waypoints;
    unsigned int texture = 0;
    int tex_w = 0;
    int tex_h = 0;
    bool dirty_full = true;
    float update_timer = 0.0f;
    float base_auto_save_timer = 0.0f;
    float center_x = 0.0f;
    float center_y = 0.0f;
    float minimap_zoom = 1.0f;
    float world_zoom = 1.0f;
    float world_pan_x = 0.0f;
    float world_pan_y = 0.0f;
    bool world_map_open = false;
    UiRect world_map_rect = {};
};
