#include "config_io.h"
#include "game_state.h" // GameSettings, g_settings (reload_game_settings/save_game_settings)

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

// Globais de estado ainda definidas em main.cpp (extracao completa para game_state e/ou
// camera e' uma fase posterior do plano de refatoracao). Removido o "static" delas em
// main.cpp para dar linkage externo, ja que os reload_*_config abaixo precisam le-las/
// escreve-las de outra unidade de traducao. (g_camera_cfg/g_camera_config_path NAO estao
// aqui: reload_camera_config continua definido em main.cpp, entao eles continuam static.)
extern PhysicsConfig g_physics_cfg;
extern std::string g_physics_config_path;
extern TerrainConfig g_terrain_cfg;
extern std::string g_terrain_config_path;
extern SkyConfig g_sky_cfg;
extern std::string g_sky_config_path;
extern MiningConfig g_mining_cfg;
extern std::string g_mining_config_path;
extern PlayerVisualConfig g_player_visual_cfg;
extern std::string g_player_visual_config_path;
extern GameSettings g_settings;
static std::string g_game_settings_path;

// ============= Shared helpers =============
// Extracted verbatim from main.cpp (original lines ~3063-3084).

bool file_exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec;
}

bool parse_json_number(const std::string& text, const char* key, float& out_value) {
    std::string needle = "\"";
    needle += key;
    needle += "\"";
    size_t key_pos = text.find(needle);
    if (key_pos == std::string::npos) return false;
    size_t colon = text.find(':', key_pos + needle.size());
    if (colon == std::string::npos) return false;
    const char* begin = text.c_str() + colon + 1;
    while (*begin == ' ' || *begin == '\t' || *begin == '\r' || *begin == '\n') begin++;
    char* end = nullptr;
    float parsed = std::strtof(begin, &end);
    if (end == begin) return false;
    out_value = parsed;
    return true;
}

bool parse_json_bool(const std::string& text, const char* key, bool& out_value) {
    std::string needle = "\"";
    needle += key;
    needle += "\"";
    size_t key_pos = text.find(needle);
    if (key_pos == std::string::npos) return false;
    size_t colon = text.find(':', key_pos + needle.size());
    if (colon == std::string::npos) return false;
    size_t p = colon + 1;
    while (p < text.size() && (text[p] == ' ' || text[p] == '\t' || text[p] == '\r' || text[p] == '\n')) p++;
    if (text.compare(p, 4, "true") == 0) { out_value = true; return true; }
    if (text.compare(p, 5, "false") == 0) { out_value = false; return true; }
    return false;
}

// Busca de caminho + fallback de criacao: extraida da logica duplicada 6x no inicio de
// cada reload_*_config original (candidatos relativos + escrita do default se ausente).
std::string find_or_seed_config_path(const char* filename, bool create_if_missing,
                                      const std::function<void(const std::string&)>& write_default) {
    const std::string prefixes[] = {"", "..\\", "..\\..\\", "..\\..\\..\\"};

    std::string chosen_path;
    for (const std::string& prefix : prefixes) {
        std::string candidate = prefix + filename;
        if (file_exists(candidate)) {
            chosen_path = candidate;
            break;
        }
    }

    if (chosen_path.empty()) {
        chosen_path = prefixes[0] + filename;
        if (create_if_missing) write_default(chosen_path);
    }

    return chosen_path;
}

// ============= Physics config =============
// write_default_physics_config / apply_physics_config_overrides extracted verbatim from
// main.cpp (original lines ~3086-3225). reload_physics_config shrinks to a one-line
// wrapper around the generic reload_config<Cfg> template.

static void write_default_physics_config(const std::string& path) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    f <<
"{\n"
"  \"fixed_timestep\": 0.008333333,\n"
"  \"max_substeps\": 10,\n"
"  \"max_speed\": 4.8,\n"
"  \"run_multiplier\": 1.42,\n"
"  \"ground_acceleration\": 26.0,\n"
"  \"ground_deceleration\": 22.0,\n"
"  \"air_acceleration\": 9.0,\n"
"  \"air_deceleration\": 6.5,\n"
"  \"ground_friction\": 19.0,\n"
"  \"air_friction\": 1.4,\n"
"  \"gravity\": 24.0,\n"
"  \"rise_multiplier\": 1.0,\n"
"  \"fall_multiplier\": 2.05,\n"
"  \"jump_velocity\": 8.1,\n"
"  \"jump_buffer\": 0.12,\n"
"  \"coyote_time\": 0.10,\n"
"  \"jump_cancel_multiplier\": 2.8,\n"
"  \"terminal_velocity\": 38.0,\n"
"  \"fall_damage_min_speed\": 14.0,\n"
"  \"fall_damage_per_unit\": 3.5,\n"
"  \"fall_damage_max\": 70.0,\n"
"  \"landing_assist_trigger_height\": 4.0,\n"
"  \"landing_assist_safe_margin\": 1.0,\n"
"  \"ground_snap\": 0.20,\n"
"  \"ground_tolerance\": 0.06,\n"
"  \"step_height\": 2.0,\n"
"  \"step_probe_distance\": 0.54,\n"
"  \"step_down_snap\": 0.55,\n"
"  \"slope_limit_normal_y\": 0.70,\n"
"  \"slope_slide_accel\": 7.5,\n"
"  \"slope_uphill_speed_mult\": 0.82,\n"
"  \"slope_downhill_speed_mult\": 1.08,\n"
"  \"max_move_per_substep\": 0.34,\n"
"  \"collision_skin\": 0.0015,\n"
"  \"collider_width\": 0.62,\n"
"  \"collider_depth\": 0.62,\n"
"  \"collider_height\": 1.80,\n"
"  \"rotation_smoothing\": 14.0,\n"
"  \"terrain_ice_speed\": 1.04,\n"
"  \"terrain_ice_accel\": 0.55,\n"
"  \"terrain_ice_decel\": 0.07,\n"
"  \"terrain_ice_friction\": 0.10,\n"
"  \"terrain_sand_speed\": 0.74,\n"
"  \"terrain_sand_accel\": 0.80,\n"
"  \"terrain_sand_friction\": 1.30,\n"
"  \"terrain_stone_speed\": 1.00,\n"
"  \"terrain_stone_accel\": 1.00,\n"
"  \"terrain_stone_friction\": 1.00,\n"
"  \"terrain_mud_speed\": 0.58,\n"
"  \"terrain_mud_accel\": 0.65,\n"
"  \"terrain_mud_friction\": 1.95,\n"
"  \"terrain_water_speed\": 0.42,\n"
"  \"terrain_water_accel\": 0.45,\n"
"  \"terrain_water_friction\": 1.70,\n"
"  \"jetpack_thrust\": 12.0,\n"
"  \"jetpack_fuel_consume\": 15.0,\n"
"  \"jetpack_fuel_regen\": 25.0,\n"
"  \"jetpack_gravity_mult\": 0.35,\n"
"  \"jetpack_max_up_speed\": 6.0,\n"
"  \"jetpack_speed_mult\": 1.8,\n"
"  \"jetpack_air_accel\": 22.0\n"
"}\n";
}

static void apply_physics_config_overrides(const std::string& text, PhysicsConfig& cfg) {
    auto setf = [&](const char* key, float& value) {
        float parsed = 0.0f;
        if (parse_json_number(text, key, parsed)) value = parsed;
    };

    setf("fixed_timestep", cfg.fixed_timestep);
    setf("max_speed", cfg.max_speed);
    setf("run_multiplier", cfg.run_multiplier);
    setf("ground_acceleration", cfg.ground_acceleration);
    setf("ground_deceleration", cfg.ground_deceleration);
    setf("air_acceleration", cfg.air_acceleration);
    setf("air_deceleration", cfg.air_deceleration);
    setf("ground_friction", cfg.ground_friction);
    setf("air_friction", cfg.air_friction);
    setf("gravity", cfg.gravity);
    setf("rise_multiplier", cfg.rise_multiplier);
    setf("fall_multiplier", cfg.fall_multiplier);
    setf("jump_velocity", cfg.jump_velocity);
    setf("jump_buffer", cfg.jump_buffer);
    setf("coyote_time", cfg.coyote_time);
    setf("jump_cancel_multiplier", cfg.jump_cancel_multiplier);
    setf("terminal_velocity", cfg.terminal_velocity);
    setf("fall_damage_min_speed", cfg.fall_damage_min_speed);
    setf("fall_damage_per_unit", cfg.fall_damage_per_unit);
    setf("fall_damage_max", cfg.fall_damage_max);
    setf("landing_assist_trigger_height", cfg.landing_assist_trigger_height);
    setf("landing_assist_safe_margin", cfg.landing_assist_safe_margin);
    setf("ground_snap", cfg.ground_snap);
    setf("ground_tolerance", cfg.ground_tolerance);
    setf("step_height", cfg.step_height);
    setf("step_probe_distance", cfg.step_probe_distance);
    setf("step_down_snap", cfg.step_down_snap);
    setf("slope_limit_normal_y", cfg.slope_limit_normal_y);
    setf("slope_slide_accel", cfg.slope_slide_accel);
    setf("slope_uphill_speed_mult", cfg.slope_uphill_speed_mult);
    setf("slope_downhill_speed_mult", cfg.slope_downhill_speed_mult);
    setf("max_move_per_substep", cfg.max_move_per_substep);
    setf("collision_skin", cfg.collision_skin);
    setf("collider_width", cfg.collider_width);
    setf("collider_depth", cfg.collider_depth);
    setf("collider_height", cfg.collider_height);
    setf("rotation_smoothing", cfg.rotation_smoothing);

    setf("terrain_ice_speed", cfg.terrain_ice_speed);
    setf("terrain_ice_accel", cfg.terrain_ice_accel);
    setf("terrain_ice_decel", cfg.terrain_ice_decel);
    setf("terrain_ice_friction", cfg.terrain_ice_friction);
    setf("terrain_sand_speed", cfg.terrain_sand_speed);
    setf("terrain_sand_accel", cfg.terrain_sand_accel);
    setf("terrain_sand_friction", cfg.terrain_sand_friction);
    setf("terrain_stone_speed", cfg.terrain_stone_speed);
    setf("terrain_stone_accel", cfg.terrain_stone_accel);
    setf("terrain_stone_friction", cfg.terrain_stone_friction);
    setf("terrain_mud_speed", cfg.terrain_mud_speed);
    setf("terrain_mud_accel", cfg.terrain_mud_accel);
    setf("terrain_mud_friction", cfg.terrain_mud_friction);
    setf("terrain_water_speed", cfg.terrain_water_speed);
    setf("terrain_water_accel", cfg.terrain_water_accel);
    setf("terrain_water_friction", cfg.terrain_water_friction);

    setf("jetpack_thrust", cfg.jetpack_thrust);
    setf("jetpack_fuel_consume", cfg.jetpack_fuel_consume);
    setf("jetpack_fuel_regen", cfg.jetpack_fuel_regen);
    setf("jetpack_gravity_mult", cfg.jetpack_gravity_mult);
    setf("jetpack_max_up_speed", cfg.jetpack_max_up_speed);
    setf("jetpack_speed_mult", cfg.jetpack_speed_mult);
    setf("jetpack_air_accel", cfg.jetpack_air_accel);

    float substeps = (float)cfg.max_substeps;
    setf("max_substeps", substeps);
    cfg.max_substeps = std::max(1, (int)std::lround(substeps));

    cfg.fixed_timestep = std::clamp(cfg.fixed_timestep, 1.0f / 360.0f, 1.0f / 20.0f);
    cfg.max_speed = std::max(0.1f, cfg.max_speed);
    cfg.run_multiplier = std::max(1.0f, cfg.run_multiplier);
    cfg.ground_acceleration = std::max(0.0f, cfg.ground_acceleration);
    cfg.ground_deceleration = std::max(0.0f, cfg.ground_deceleration);
    cfg.air_acceleration = std::max(0.0f, cfg.air_acceleration);
    cfg.air_deceleration = std::max(0.0f, cfg.air_deceleration);
    cfg.gravity = std::max(0.0f, cfg.gravity);
    cfg.fall_multiplier = std::max(cfg.rise_multiplier + 0.01f, cfg.fall_multiplier);
    cfg.jump_velocity = std::max(0.0f, cfg.jump_velocity);
    cfg.jump_buffer = std::clamp(cfg.jump_buffer, 0.0f, 0.35f);
    cfg.coyote_time = std::clamp(cfg.coyote_time, 0.0f, 0.35f);
    cfg.jump_cancel_multiplier = std::max(1.0f, cfg.jump_cancel_multiplier);
    cfg.terminal_velocity = std::max(1.0f, cfg.terminal_velocity);
    cfg.step_height = std::clamp(cfg.step_height, 0.0f, 3.0f);
    cfg.step_down_snap = std::clamp(cfg.step_down_snap, 0.0f, 1.5f);
    cfg.collider_height = std::clamp(cfg.collider_height, 1.0f, 2.5f);
    cfg.collider_width = std::clamp(cfg.collider_width, 0.3f, 1.2f);
    cfg.collider_depth = std::clamp(cfg.collider_depth, 0.3f, 1.2f);
    cfg.max_move_per_substep = std::clamp(cfg.max_move_per_substep, 0.05f, 0.95f);
    cfg.collision_skin = std::clamp(cfg.collision_skin, 0.0002f, 0.02f);
    cfg.slope_limit_normal_y = std::clamp(cfg.slope_limit_normal_y, 0.10f, 0.98f);
}

bool reload_physics_config(bool create_if_missing) {
    return reload_config<PhysicsConfig>("physics_config.json", g_physics_cfg, create_if_missing,
                                         write_default_physics_config, apply_physics_config_overrides,
                                         &g_physics_config_path);
}

// ============= Terrain config =============
// Extracted verbatim from main.cpp (original lines ~3264-3403).

static void write_default_terrain_config(const std::string& path) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    f <<
"{\n"
"  \"macro_scale\": 0.00045,\n"
"  \"ridge_scale\": 0.0032,\n"
"  \"valley_scale\": 0.0013,\n"
"  \"detail_scale\": 0.0095,\n"
"  \"warp_scale\": 0.0022,\n"
"  \"warp_strength\": 34.0,\n"
"  \"macro_weight\": 0.50,\n"
"  \"ridge_weight\": 0.62,\n"
"  \"valley_weight\": 0.64,\n"
"  \"detail_weight\": 0.06,\n"
"  \"plateau_level\": 0.57,\n"
"  \"plateau_flatten\": 0.35,\n"
"  \"min_height\": 2.0,\n"
"  \"max_height\": 130.0,\n"
"  \"sea_height\": 20.0,\n"
"  \"snow_height\": 98.0,\n"
"  \"thermal_erosion_passes\": 4,\n"
"  \"hydraulic_erosion_passes\": 4,\n"
"  \"smooth_passes\": 2,\n"
"  \"erosion_strength\": 0.38,\n"
"  \"thermal_talus\": 0.021,\n"
"  \"temp_scale\": 0.0016,\n"
"  \"moisture_scale\": 0.0019,\n"
"  \"biome_blend\": 0.16,\n"
"  \"fissure_scale\": 0.0080,\n"
"  \"fissure_depth\": 0.060,\n"
"  \"crater_scale\": 0.0022,\n"
"  \"crater_depth\": 0.11,\n"
"  \"detail_object_density\": 0.060,\n"
"  \"river_seed_count\": 16,\n"
"  \"volcano_seed_count\": 5\n"
"}\n";
}

static void apply_terrain_config_overrides(const std::string& text, TerrainConfig& cfg) {
    auto setf = [&](const char* key, float& value) {
        float parsed = 0.0f;
        if (parse_json_number(text, key, parsed)) value = parsed;
    };
    auto seti = [&](const char* key, int& value) {
        float parsed = 0.0f;
        if (parse_json_number(text, key, parsed)) value = (int)std::lround(parsed);
    };

    setf("macro_scale", cfg.macro_scale);
    setf("ridge_scale", cfg.ridge_scale);
    setf("valley_scale", cfg.valley_scale);
    setf("detail_scale", cfg.detail_scale);
    setf("warp_scale", cfg.warp_scale);
    setf("warp_strength", cfg.warp_strength);
    setf("macro_weight", cfg.macro_weight);
    setf("ridge_weight", cfg.ridge_weight);
    setf("valley_weight", cfg.valley_weight);
    setf("detail_weight", cfg.detail_weight);
    setf("plateau_level", cfg.plateau_level);
    setf("plateau_flatten", cfg.plateau_flatten);
    setf("min_height", cfg.min_height);
    setf("max_height", cfg.max_height);
    setf("sea_height", cfg.sea_height);
    setf("snow_height", cfg.snow_height);
    seti("thermal_erosion_passes", cfg.thermal_erosion_passes);
    seti("hydraulic_erosion_passes", cfg.hydraulic_erosion_passes);
    seti("smooth_passes", cfg.smooth_passes);
    setf("erosion_strength", cfg.erosion_strength);
    setf("thermal_talus", cfg.thermal_talus);
    setf("temp_scale", cfg.temp_scale);
    setf("moisture_scale", cfg.moisture_scale);
    setf("biome_blend", cfg.biome_blend);
    setf("fissure_scale", cfg.fissure_scale);
    setf("fissure_depth", cfg.fissure_depth);
    setf("crater_scale", cfg.crater_scale);
    setf("crater_depth", cfg.crater_depth);
    setf("detail_object_density", cfg.detail_object_density);
    seti("river_seed_count", cfg.river_seed_count);
    seti("volcano_seed_count", cfg.volcano_seed_count);

    cfg.macro_scale = std::clamp(cfg.macro_scale, 0.0001f, 0.02f);
    cfg.ridge_scale = std::clamp(cfg.ridge_scale, 0.0005f, 0.04f);
    cfg.valley_scale = std::clamp(cfg.valley_scale, 0.0003f, 0.03f);
    cfg.detail_scale = std::clamp(cfg.detail_scale, 0.002f, 0.10f);
    cfg.warp_scale = std::clamp(cfg.warp_scale, 0.0003f, 0.03f);
    cfg.warp_strength = std::clamp(cfg.warp_strength, 0.0f, 80.0f);
    cfg.plateau_level = std::clamp(cfg.plateau_level, 0.25f, 0.9f);
    cfg.plateau_flatten = std::clamp(cfg.plateau_flatten, 0.0f, 0.8f);
    cfg.min_height = std::clamp(cfg.min_height, 0.0f, 160.0f);
    cfg.max_height = std::clamp(cfg.max_height, cfg.min_height + 4.0f, 255.0f);
    cfg.sea_height = std::clamp(cfg.sea_height, cfg.min_height, cfg.max_height - 1.0f);
    cfg.snow_height = std::clamp(cfg.snow_height, cfg.sea_height + 1.0f, cfg.max_height);
    cfg.thermal_erosion_passes = std::clamp(cfg.thermal_erosion_passes, 0, 12);
    cfg.hydraulic_erosion_passes = std::clamp(cfg.hydraulic_erosion_passes, 0, 12);
    cfg.smooth_passes = std::clamp(cfg.smooth_passes, 0, 8);
    cfg.erosion_strength = std::clamp(cfg.erosion_strength, 0.0f, 1.0f);
    cfg.thermal_talus = std::clamp(cfg.thermal_talus, 0.001f, 0.2f);
    cfg.temp_scale = std::clamp(cfg.temp_scale, 0.0002f, 0.02f);
    cfg.moisture_scale = std::clamp(cfg.moisture_scale, 0.0002f, 0.02f);
    cfg.biome_blend = std::clamp(cfg.biome_blend, 0.0f, 1.0f);
    cfg.fissure_scale = std::clamp(cfg.fissure_scale, 0.0005f, 0.05f);
    cfg.fissure_depth = std::clamp(cfg.fissure_depth, 0.0f, 0.4f);
    cfg.crater_scale = std::clamp(cfg.crater_scale, 0.0005f, 0.05f);
    cfg.crater_depth = std::clamp(cfg.crater_depth, 0.0f, 0.4f);
    cfg.detail_object_density = std::clamp(cfg.detail_object_density, 0.0f, 0.4f);
    cfg.river_seed_count = std::clamp(cfg.river_seed_count, 0, 200);
    cfg.volcano_seed_count = std::clamp(cfg.volcano_seed_count, 0, 60);
}

bool reload_terrain_config(bool create_if_missing) {
    return reload_config<TerrainConfig>("terrain_config.json", g_terrain_cfg, create_if_missing,
                                         write_default_terrain_config, apply_terrain_config_overrides,
                                         &g_terrain_config_path);
}

// ============= Sky config =============
// Extracted verbatim from main.cpp (original lines ~3405-3547).

static void write_default_sky_config(const std::string& path) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    f <<
"{\n"
"  \"stars_density\": 2600.0,\n"
"  \"stars_parallax\": 0.008,\n"
"  \"nebula_alpha\": 0.22,\n"
"  \"nebula_parallax\": 0.016,\n"
"  \"cloud_alpha\": 0.20,\n"
"  \"cloud_parallax\": 0.050,\n"
"  \"planet_radius\": 300.0,\n"
"  \"planet_distance\": 1150.0,\n"
"  \"planet_orbit_speed\": 0.060,\n"
"  \"planet_parallax\": 0.028,\n"
"  \"sun_radius\": 52.0,\n"
"  \"sun_distance\": 820.0,\n"
"  \"sun_halo_size\": 2.25,\n"
"  \"bloom_intensity\": 0.45,\n"
"  \"moon_radius\": 36.0,\n"
"  \"moon_distance\": 940.0,\n"
"  \"moon_orbit_speed\": 0.48,\n"
"  \"moon_parallax\": 0.040,\n"
"  \"moon2_radius\": 22.0,\n"
"  \"moon2_distance\": 1010.0,\n"
"  \"moon2_orbit_speed\": 0.98,\n"
"  \"moon2_parallax\": 0.050,\n"
"  \"atmosphere_horizon_boost\": 0.45,\n"
"  \"atmosphere_zenith_boost\": 0.24,\n"
"  \"horizon_fade\": 0.28,\n"
"  \"fog_start_factor\": 0.34,\n"
"  \"fog_end_factor\": 0.80,\n"
"  \"fog_distance_bonus\": 15.0,\n"
"  \"eclipse_frequency_days\": 8.0,\n"
"  \"eclipse_strength\": 0.50\n"
"}\n";
}

static void apply_sky_config_overrides(const std::string& text, SkyConfig& cfg) {
    auto setf = [&](const char* key, float& value) {
        float parsed = 0.0f;
        if (parse_json_number(text, key, parsed)) value = parsed;
    };

    setf("stars_density", cfg.stars_density);
    setf("stars_parallax", cfg.stars_parallax);
    setf("nebula_alpha", cfg.nebula_alpha);
    setf("nebula_parallax", cfg.nebula_parallax);
    setf("cloud_alpha", cfg.cloud_alpha);
    setf("cloud_parallax", cfg.cloud_parallax);
    setf("planet_radius", cfg.planet_radius);
    setf("planet_distance", cfg.planet_distance);
    setf("planet_orbit_speed", cfg.planet_orbit_speed);
    setf("planet_parallax", cfg.planet_parallax);
    setf("sun_radius", cfg.sun_radius);
    setf("sun_distance", cfg.sun_distance);
    setf("sun_halo_size", cfg.sun_halo_size);
    setf("bloom_intensity", cfg.bloom_intensity);
    setf("moon_radius", cfg.moon_radius);
    setf("moon_distance", cfg.moon_distance);
    setf("moon_orbit_speed", cfg.moon_orbit_speed);
    setf("moon_parallax", cfg.moon_parallax);
    setf("moon2_radius", cfg.moon2_radius);
    setf("moon2_distance", cfg.moon2_distance);
    setf("moon2_orbit_speed", cfg.moon2_orbit_speed);
    setf("moon2_parallax", cfg.moon2_parallax);
    setf("atmosphere_horizon_boost", cfg.atmosphere_horizon_boost);
    setf("atmosphere_zenith_boost", cfg.atmosphere_zenith_boost);
    setf("horizon_fade", cfg.horizon_fade);
    setf("fog_start_factor", cfg.fog_start_factor);
    setf("fog_end_factor", cfg.fog_end_factor);
    setf("fog_distance_bonus", cfg.fog_distance_bonus);
    setf("eclipse_frequency_days", cfg.eclipse_frequency_days);
    setf("eclipse_strength", cfg.eclipse_strength);

    cfg.stars_density = std::clamp(cfg.stars_density, 100.0f, 4000.0f);
    cfg.stars_parallax = std::clamp(cfg.stars_parallax, 0.0f, 0.15f);
    cfg.nebula_alpha = std::clamp(cfg.nebula_alpha, 0.0f, 1.0f);
    cfg.nebula_parallax = std::clamp(cfg.nebula_parallax, 0.0f, 0.2f);
    cfg.cloud_alpha = std::clamp(cfg.cloud_alpha, 0.0f, 1.0f);
    cfg.cloud_parallax = std::clamp(cfg.cloud_parallax, 0.0f, 0.2f);
    cfg.planet_radius = std::clamp(cfg.planet_radius, 20.0f, 500.0f);
    cfg.planet_distance = std::clamp(cfg.planet_distance, 300.0f, 3000.0f);
    cfg.planet_orbit_speed = std::clamp(cfg.planet_orbit_speed, 0.0f, 5.0f);
    cfg.planet_parallax = std::clamp(cfg.planet_parallax, 0.0f, 0.3f);
    cfg.sun_radius = std::clamp(cfg.sun_radius, 8.0f, 180.0f);
    cfg.sun_distance = std::clamp(cfg.sun_distance, 200.0f, 2500.0f);
    cfg.sun_halo_size = std::clamp(cfg.sun_halo_size, 1.0f, 4.0f);
    cfg.bloom_intensity = std::clamp(cfg.bloom_intensity, 0.0f, 1.5f);
    cfg.moon_radius = std::clamp(cfg.moon_radius, 5.0f, 150.0f);
    cfg.moon_distance = std::clamp(cfg.moon_distance, 200.0f, 3000.0f);
    cfg.moon_orbit_speed = std::clamp(cfg.moon_orbit_speed, 0.0f, 8.0f);
    cfg.moon_parallax = std::clamp(cfg.moon_parallax, 0.0f, 0.3f);
    cfg.moon2_radius = std::clamp(cfg.moon2_radius, 4.0f, 140.0f);
    cfg.moon2_distance = std::clamp(cfg.moon2_distance, 200.0f, 3000.0f);
    cfg.moon2_orbit_speed = std::clamp(cfg.moon2_orbit_speed, 0.0f, 8.0f);
    cfg.moon2_parallax = std::clamp(cfg.moon2_parallax, 0.0f, 0.3f);
    cfg.atmosphere_horizon_boost = std::clamp(cfg.atmosphere_horizon_boost, 0.0f, 1.0f);
    cfg.atmosphere_zenith_boost = std::clamp(cfg.atmosphere_zenith_boost, 0.0f, 1.0f);
    cfg.horizon_fade = std::clamp(cfg.horizon_fade, 0.0f, 1.0f);
    cfg.fog_start_factor = std::clamp(cfg.fog_start_factor, 0.1f, 0.9f);
    cfg.fog_end_factor = std::clamp(cfg.fog_end_factor, cfg.fog_start_factor + 0.05f, 1.3f);
    cfg.fog_distance_bonus = std::clamp(cfg.fog_distance_bonus, 0.0f, 160.0f);
    cfg.eclipse_frequency_days = std::clamp(cfg.eclipse_frequency_days, 0.5f, 40.0f);
    cfg.eclipse_strength = std::clamp(cfg.eclipse_strength, 0.0f, 1.0f);
}

bool reload_sky_config(bool create_if_missing) {
    return reload_config<SkyConfig>("sky_config.json", g_sky_cfg, create_if_missing,
                                     write_default_sky_config, apply_sky_config_overrides,
                                     &g_sky_config_path);
}

// ============= Camera config (write_default/apply only — see config_io.h for why
// reload_camera_config itself is defined in main.cpp instead) =============
// Extracted verbatim from main.cpp (original lines ~3549-3671). NOT static: main.cpp's
// reload_camera_config needs to call these by name.

void write_default_camera_config(const std::string& path) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    f <<
"{\n"
"  \"spawn_distance\": 4.2,\n"
"  \"spawn_pitch\": 24.0,\n"
"  \"spawn_yaw\": 180.0,\n"
"  \"open_pitch\": 26.0,\n"
"  \"semi_pitch\": 50.0,\n"
"  \"cave_pitch\": 72.0,\n"
"  \"emergency_pitch\": 87.0,\n"
"  \"open_distance_scale\": 1.00,\n"
"  \"semi_distance_scale\": 0.86,\n"
"  \"cave_distance_scale\": 0.74,\n"
"  \"emergency_distance_scale\": 0.62,\n"
"  \"open_target_lift\": 0.06,\n"
"  \"semi_target_lift\": 0.46,\n"
"  \"cave_target_lift\": 1.16,\n"
"  \"emergency_target_lift\": 1.72,\n"
"  \"pitch_lerp\": 0.14,\n"
"  \"distance_lerp\": 0.14,\n"
"  \"lift_lerp\": 0.15,\n"
"  \"step_height_lerp\": 0.22,\n"
"  \"pitch_sensitivity\": 0.85,\n"
"  \"min_pitch\": -38.0,\n"
"  \"max_pitch\": 89.0,\n"
"  \"cave_depth_start\": 0.45,\n"
"  \"cave_depth_end\": 2.60,\n"
"  \"enclosed_start\": 0.28,\n"
"  \"enclosed_end\": 0.76,\n"
"  \"occlusion_full\": 0.64,\n"
"  \"emergency_hidden_time\": 0.50,\n"
"  \"transparency_alpha\": 0.30,\n"
"  \"transparency_fade_in\": 10.0,\n"
"  \"transparency_fade_out\": 5.0,\n"
"  \"collision_probe_step\": 0.14,\n"
"  \"collision_padding\": 0.34,\n"
"  \"min_collision_distance\": 0.70,\n"
"  \"debug_ray_length\": 8.0\n"
"}\n";
}

void apply_camera_config_overrides(const std::string& text, CameraConfig& cfg) {
    auto setf = [&](const char* key, float& value) {
        float parsed = 0.0f;
        if (parse_json_number(text, key, parsed)) value = parsed;
    };

    setf("spawn_distance", cfg.spawn_distance);
    setf("spawn_pitch", cfg.spawn_pitch);
    setf("spawn_yaw", cfg.spawn_yaw);

    setf("open_pitch", cfg.open_pitch);
    setf("semi_pitch", cfg.semi_pitch);
    setf("cave_pitch", cfg.cave_pitch);
    setf("emergency_pitch", cfg.emergency_pitch);

    setf("open_distance_scale", cfg.open_distance_scale);
    setf("semi_distance_scale", cfg.semi_distance_scale);
    setf("cave_distance_scale", cfg.cave_distance_scale);
    setf("emergency_distance_scale", cfg.emergency_distance_scale);

    setf("open_target_lift", cfg.open_target_lift);
    setf("semi_target_lift", cfg.semi_target_lift);
    setf("cave_target_lift", cfg.cave_target_lift);
    setf("emergency_target_lift", cfg.emergency_target_lift);

    setf("pitch_lerp", cfg.pitch_lerp);
    setf("distance_lerp", cfg.distance_lerp);
    setf("lift_lerp", cfg.lift_lerp);
    setf("step_height_lerp", cfg.step_height_lerp);
    setf("pitch_sensitivity", cfg.pitch_sensitivity);
    setf("min_pitch", cfg.min_pitch);
    setf("max_pitch", cfg.max_pitch);

    setf("cave_depth_start", cfg.cave_depth_start);
    setf("cave_depth_end", cfg.cave_depth_end);
    setf("enclosed_start", cfg.enclosed_start);
    setf("enclosed_end", cfg.enclosed_end);
    setf("occlusion_full", cfg.occlusion_full);
    setf("emergency_hidden_time", cfg.emergency_hidden_time);

    setf("transparency_alpha", cfg.transparency_alpha);
    setf("transparency_fade_in", cfg.transparency_fade_in);
    setf("transparency_fade_out", cfg.transparency_fade_out);

    setf("collision_probe_step", cfg.collision_probe_step);
    setf("collision_padding", cfg.collision_padding);
    setf("min_collision_distance", cfg.min_collision_distance);
    setf("debug_ray_length", cfg.debug_ray_length);

    cfg.spawn_distance = std::clamp(cfg.spawn_distance, 1.6f, 25.0f);
    cfg.spawn_pitch = std::clamp(cfg.spawn_pitch, 8.0f, 88.0f);
    while (cfg.spawn_yaw < 0.0f) cfg.spawn_yaw += 360.0f;
    while (cfg.spawn_yaw >= 360.0f) cfg.spawn_yaw -= 360.0f;

    cfg.open_pitch = std::clamp(cfg.open_pitch, 8.0f, 88.0f);
    cfg.semi_pitch = std::clamp(cfg.semi_pitch, cfg.open_pitch, 88.0f);
    cfg.cave_pitch = std::clamp(cfg.cave_pitch, cfg.semi_pitch, 88.0f);
    cfg.emergency_pitch = std::clamp(cfg.emergency_pitch, cfg.cave_pitch, 89.0f);

    cfg.open_distance_scale = std::clamp(cfg.open_distance_scale, 0.35f, 1.40f);
    cfg.semi_distance_scale = std::clamp(cfg.semi_distance_scale, 0.35f, 1.40f);
    cfg.cave_distance_scale = std::clamp(cfg.cave_distance_scale, 0.35f, 1.40f);
    cfg.emergency_distance_scale = std::clamp(cfg.emergency_distance_scale, 0.35f, 1.40f);

    cfg.open_target_lift = std::clamp(cfg.open_target_lift, 0.0f, 3.0f);
    cfg.semi_target_lift = std::clamp(cfg.semi_target_lift, 0.0f, 3.0f);
    cfg.cave_target_lift = std::clamp(cfg.cave_target_lift, 0.0f, 4.0f);
    cfg.emergency_target_lift = std::clamp(cfg.emergency_target_lift, 0.0f, 4.0f);

    cfg.pitch_lerp = std::clamp(cfg.pitch_lerp, 0.02f, 1.0f);
    cfg.distance_lerp = std::clamp(cfg.distance_lerp, 0.02f, 1.0f);
    cfg.lift_lerp = std::clamp(cfg.lift_lerp, 0.02f, 1.0f);
    cfg.step_height_lerp = std::clamp(cfg.step_height_lerp, 0.02f, 1.0f);
    cfg.pitch_sensitivity = std::clamp(cfg.pitch_sensitivity, 0.10f, 2.0f);
    // -80 e o piso pratico: abaixo disso a camera fica quase sob os pes e o corpo do jogador tapa
    // a tela inteira. 89 no teto porque em 90 exato o vetor de view fica paralelo ao up e a
    // matriz de camera degenera.
    cfg.min_pitch = std::clamp(cfg.min_pitch, -80.0f, 0.0f);
    cfg.max_pitch = std::clamp(cfg.max_pitch, 10.0f, 89.0f);

    cfg.cave_depth_start = std::clamp(cfg.cave_depth_start, 0.0f, 10.0f);
    cfg.cave_depth_end = std::clamp(cfg.cave_depth_end, cfg.cave_depth_start + 0.05f, 20.0f);
    cfg.enclosed_start = std::clamp(cfg.enclosed_start, 0.0f, 1.0f);
    cfg.enclosed_end = std::clamp(cfg.enclosed_end, cfg.enclosed_start + 0.01f, 1.0f);
    cfg.occlusion_full = std::clamp(cfg.occlusion_full, 0.05f, 1.0f);
    cfg.emergency_hidden_time = std::clamp(cfg.emergency_hidden_time, 0.05f, 3.0f);

    cfg.transparency_alpha = std::clamp(cfg.transparency_alpha, 0.05f, 0.95f);
    cfg.transparency_fade_in = std::clamp(cfg.transparency_fade_in, 0.1f, 40.0f);
    cfg.transparency_fade_out = std::clamp(cfg.transparency_fade_out, 0.1f, 40.0f);
    cfg.collision_probe_step = std::clamp(cfg.collision_probe_step, 0.04f, 0.60f);
    cfg.collision_padding = std::clamp(cfg.collision_padding, 0.05f, 1.50f);
    cfg.min_collision_distance = std::clamp(cfg.min_collision_distance, 0.20f, 4.0f);
    cfg.debug_ray_length = std::clamp(cfg.debug_ray_length, 1.0f, 20.0f);
}

// NOTE: reload_camera_config is intentionally NOT defined here — see the comment on its
// declaration in config_io.h. It lives in main.cpp instead.

// ============= Mining config =============
// Extracted verbatim from main.cpp (original lines ~3712-3811).

static void write_default_mining_config(const std::string& path) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    f <<
"{\n"
"  \"hit_interval\": 0.20,\n"
"  \"hit_interval_min\": 0.09,\n"
"  \"early_game_speed_mult\": 1.12,\n"
"  \"late_game_speed_mult\": 0.85,\n"
"  \"hits_sand\": 1,\n"
"  \"hits_dirt\": 2,\n"
"  \"hits_ice\": 2,\n"
"  \"hits_snow\": 2,\n"
"  \"hits_stone\": 3,\n"
"  \"hits_metal\": 4,\n"
"  \"hits_crystal\": 5,\n"
"  \"hits_ore\": 4,\n"
"  \"hits_wood\": 2,\n"
"  \"hits_modules\": 4\n"
"}\n";
}

static void apply_mining_config_overrides(const std::string& text, MiningConfig& cfg) {
    auto setf = [&](const char* key, float& value) {
        float parsed = 0.0f;
        if (parse_json_number(text, key, parsed)) value = parsed;
    };
    auto seti = [&](const char* key, int& value) {
        float parsed = 0.0f;
        if (parse_json_number(text, key, parsed)) value = (int)std::lround(parsed);
    };

    setf("hit_interval", cfg.hit_interval);
    setf("hit_interval_min", cfg.hit_interval_min);
    setf("early_game_speed_mult", cfg.early_game_speed_mult);
    setf("late_game_speed_mult", cfg.late_game_speed_mult);
    seti("hits_sand", cfg.hits_sand);
    seti("hits_dirt", cfg.hits_dirt);
    seti("hits_ice", cfg.hits_ice);
    seti("hits_snow", cfg.hits_snow);
    seti("hits_stone", cfg.hits_stone);
    seti("hits_metal", cfg.hits_metal);
    seti("hits_crystal", cfg.hits_crystal);
    seti("hits_ore", cfg.hits_ore);
    seti("hits_wood", cfg.hits_wood);
    seti("hits_modules", cfg.hits_modules);

    cfg.hit_interval = std::clamp(cfg.hit_interval, 0.06f, 0.80f);
    cfg.hit_interval_min = std::clamp(cfg.hit_interval_min, 0.04f, cfg.hit_interval);
    cfg.early_game_speed_mult = std::clamp(cfg.early_game_speed_mult, 0.50f, 3.00f);
    cfg.late_game_speed_mult = std::clamp(cfg.late_game_speed_mult, 0.35f, 2.50f);

    cfg.hits_sand = std::clamp(cfg.hits_sand, 1, 8);
    cfg.hits_dirt = std::clamp(cfg.hits_dirt, 1, 8);
    cfg.hits_ice = std::clamp(cfg.hits_ice, 1, 8);
    cfg.hits_snow = std::clamp(cfg.hits_snow, 1, 8);
    cfg.hits_stone = std::clamp(cfg.hits_stone, 1, 12);
    cfg.hits_metal = std::clamp(cfg.hits_metal, 1, 12);
    cfg.hits_crystal = std::clamp(cfg.hits_crystal, 1, 14);
    cfg.hits_ore = std::clamp(cfg.hits_ore, 1, 12);
    cfg.hits_wood = std::clamp(cfg.hits_wood, 1, 10);
    cfg.hits_modules = std::clamp(cfg.hits_modules, 1, 14);
}

bool reload_mining_config(bool create_if_missing) {
    return reload_config<MiningConfig>("mining_config.json", g_mining_cfg, create_if_missing,
                                        write_default_mining_config, apply_mining_config_overrides,
                                        &g_mining_config_path);
}

// ============= Player visual config =============
// Extracted verbatim from main.cpp (original lines ~3813-3907).

static void write_default_player_visual_config(const std::string& path) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    f <<
"{\n"
"  \"breathing_amp\": 0.018,\n"
"  \"breathing_speed\": 2.2,\n"
"  \"walk_bob_amp\": 0.060,\n"
"  \"walk_bob_speed\": 12.5,\n"
"  \"walk_weight_amp\": 0.10,\n"
"  \"mine_impact_amp\": 0.19,\n"
"  \"idle_sway_amp\": 0.016,\n"
"  \"idle_sway_speed\": 1.6,\n"
"  \"visor_reflect_alpha\": 0.36,\n"
"  \"headlamp_intensity\": 0.74,\n"
"  \"suit_wear_strength\": 0.20,\n"
"  \"suit_dirt_strength\": 0.24,\n"
"  \"suit_frost_strength\": 0.32,\n"
"  \"suit_damage_strength\": 0.45\n"
"}\n";
}

static void apply_player_visual_config_overrides(const std::string& text, PlayerVisualConfig& cfg) {
    auto setf = [&](const char* key, float& value) {
        float parsed = 0.0f;
        if (parse_json_number(text, key, parsed)) value = parsed;
    };

    setf("breathing_amp", cfg.breathing_amp);
    setf("breathing_speed", cfg.breathing_speed);
    setf("walk_bob_amp", cfg.walk_bob_amp);
    setf("walk_bob_speed", cfg.walk_bob_speed);
    setf("walk_weight_amp", cfg.walk_weight_amp);
    setf("mine_impact_amp", cfg.mine_impact_amp);
    setf("idle_sway_amp", cfg.idle_sway_amp);
    setf("idle_sway_speed", cfg.idle_sway_speed);
    setf("visor_reflect_alpha", cfg.visor_reflect_alpha);
    setf("headlamp_intensity", cfg.headlamp_intensity);
    setf("suit_wear_strength", cfg.suit_wear_strength);
    setf("suit_dirt_strength", cfg.suit_dirt_strength);
    setf("suit_frost_strength", cfg.suit_frost_strength);
    setf("suit_damage_strength", cfg.suit_damage_strength);

    cfg.breathing_amp = std::clamp(cfg.breathing_amp, 0.0f, 0.08f);
    cfg.breathing_speed = std::clamp(cfg.breathing_speed, 0.2f, 8.0f);
    cfg.walk_bob_amp = std::clamp(cfg.walk_bob_amp, 0.0f, 0.20f);
    cfg.walk_bob_speed = std::clamp(cfg.walk_bob_speed, 2.0f, 24.0f);
    cfg.walk_weight_amp = std::clamp(cfg.walk_weight_amp, 0.0f, 0.30f);
    cfg.mine_impact_amp = std::clamp(cfg.mine_impact_amp, 0.0f, 0.50f);
    cfg.idle_sway_amp = std::clamp(cfg.idle_sway_amp, 0.0f, 0.06f);
    cfg.idle_sway_speed = std::clamp(cfg.idle_sway_speed, 0.2f, 6.0f);
    cfg.visor_reflect_alpha = std::clamp(cfg.visor_reflect_alpha, 0.0f, 1.0f);
    cfg.headlamp_intensity = std::clamp(cfg.headlamp_intensity, 0.0f, 2.0f);
    cfg.suit_wear_strength = std::clamp(cfg.suit_wear_strength, 0.0f, 1.0f);
    cfg.suit_dirt_strength = std::clamp(cfg.suit_dirt_strength, 0.0f, 1.0f);
    cfg.suit_frost_strength = std::clamp(cfg.suit_frost_strength, 0.0f, 1.0f);
    cfg.suit_damage_strength = std::clamp(cfg.suit_damage_strength, 0.0f, 1.0f);
}

bool reload_player_visual_config(bool create_if_missing) {
    return reload_config<PlayerVisualConfig>("player_visual.json", g_player_visual_cfg, create_if_missing,
                                              write_default_player_visual_config,
                                              apply_player_visual_config_overrides,
                                              &g_player_visual_config_path);
}

// ============= Game Settings (unico config que o jogador altera e precisa SALVAR) =========
// Os 5 configs acima sao arquivos de ajuste do desenvolvedor (carregados 1x, editados a mao)
// - GameSettings e' diferente: o jogador muda esses valores pelo menu de Configuracoes
// (ui_menu.cpp) e espera que persistam entre sessoes (antes desta mudanca, nada disso era
// salvo - resetava tudo pro padrao a cada vez que o jogo abria).
static void write_default_game_settings(const std::string& path) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    f <<
"{\n"
"  \"ui_scale\": 1.0,\n"
"  \"camera_sensitivity\": 0.30,\n"
"  \"invert_y\": false,\n"
"  \"brightness\": 1.0,\n"
"  \"contrast\": 1.0,\n"
"  \"music_volume\": 0.6,\n"
"  \"sfx_volume\": 0.8,\n"
"  \"music_enabled\": true,\n"
"  \"sfx_enabled\": true,\n"
"  \"lighting_enabled\": true,\n"
"  \"lighting_shadows_enabled\": true,\n"
"  \"lighting_bloom_intensity\": 0.55,\n"
"  \"lighting_vignette_intensity\": 0.30\n"
"}\n";
}

static void apply_game_settings_overrides(const std::string& text, GameSettings& cfg) {
    auto setf = [&](const char* key, float& value) {
        float parsed = 0.0f;
        if (parse_json_number(text, key, parsed)) value = parsed;
    };
    auto setb = [&](const char* key, bool& value) {
        bool parsed = false;
        if (parse_json_bool(text, key, parsed)) value = parsed;
    };

    setf("ui_scale", cfg.ui_scale);
    setf("camera_sensitivity", cfg.camera_sensitivity);
    setb("invert_y", cfg.invert_y);
    setf("brightness", cfg.brightness);
    setf("contrast", cfg.contrast);
    setf("music_volume", cfg.music_volume);
    setf("sfx_volume", cfg.sfx_volume);
    setb("music_enabled", cfg.music_enabled);
    setb("sfx_enabled", cfg.sfx_enabled);
    setb("lighting_enabled", cfg.lighting_enabled);
    setb("lighting_shadows_enabled", cfg.lighting_shadows_enabled);
    setf("lighting_bloom_intensity", cfg.lighting_bloom_intensity);
    setf("lighting_vignette_intensity", cfg.lighting_vignette_intensity);

    cfg.ui_scale = std::clamp(cfg.ui_scale, 0.75f, 1.5f);
    cfg.camera_sensitivity = std::clamp(cfg.camera_sensitivity, 0.05f, 0.5f);
    cfg.brightness = std::clamp(cfg.brightness, 0.5f, 1.5f);
    cfg.music_volume = std::clamp(cfg.music_volume, 0.0f, 1.0f);
    cfg.sfx_volume = std::clamp(cfg.sfx_volume, 0.0f, 1.0f);
    cfg.lighting_bloom_intensity = std::clamp(cfg.lighting_bloom_intensity, 0.0f, 1.0f);
    cfg.lighting_vignette_intensity = std::clamp(cfg.lighting_vignette_intensity, 0.0f, 0.6f);
}

bool reload_game_settings(bool create_if_missing) {
    return reload_config<GameSettings>("game_settings.json", g_settings, create_if_missing,
                                        write_default_game_settings,
                                        apply_game_settings_overrides,
                                        &g_game_settings_path);
}

// Escreve o g_settings ATUAL (nao os padroes) - chamado sempre que o jogador muda algo no
// menu de Configuracoes (ui_menu.cpp), pra persistir na hora sem precisar de um botao
// "Salvar" separado (mesmo espirito de aplicar audio/camera na hora ja usado ali).
void save_game_settings() {
    std::string path = g_game_settings_path.empty() ? "game_settings.json" : g_game_settings_path;
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    f <<
"{\n"
"  \"ui_scale\": " << g_settings.ui_scale << ",\n"
"  \"camera_sensitivity\": " << g_settings.camera_sensitivity << ",\n"
"  \"invert_y\": " << (g_settings.invert_y ? "true" : "false") << ",\n"
"  \"brightness\": " << g_settings.brightness << ",\n"
"  \"contrast\": " << g_settings.contrast << ",\n"
"  \"music_volume\": " << g_settings.music_volume << ",\n"
"  \"sfx_volume\": " << g_settings.sfx_volume << ",\n"
"  \"music_enabled\": " << (g_settings.music_enabled ? "true" : "false") << ",\n"
"  \"sfx_enabled\": " << (g_settings.sfx_enabled ? "true" : "false") << ",\n"
"  \"lighting_enabled\": " << (g_settings.lighting_enabled ? "true" : "false") << ",\n"
"  \"lighting_shadows_enabled\": " << (g_settings.lighting_shadows_enabled ? "true" : "false") << ",\n"
"  \"lighting_bloom_intensity\": " << g_settings.lighting_bloom_intensity << ",\n"
"  \"lighting_vignette_intensity\": " << g_settings.lighting_vignette_intensity << "\n"
"}\n";
}
