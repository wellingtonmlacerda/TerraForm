#include "camera.h"

#include "raylib_platform.h"
#include "config_types.h" // CameraConfig, PhysicsConfig (types of the extern globals below)
#include "noise.h"        // lerp
#include "world.h"        // World, g_world, world_to_tile, kHeightScale, object_block_at, get_block_height

// Globais de estado de jogo ainda definidas em main.cpp (extracao completa para
// config_io/game_state/player_physics e uma fase posterior ou paralela do plano de
// refatoracao). g_camera_cfg perdeu o "static" que tinha em main.cpp (era static la
// porque so reload_camera_config, definido no proprio main.cpp, a tocava; agora este
// arquivo tambem precisa le-la) para dar linkage externo, mesmo padrao de g_terrain_cfg
// em world.cpp / g_oxygen em textures.cpp. g_physics_cfg ja era nao-static (config_io.cpp
// ja a le via extern); so precisamos da declaracao aqui tambem.
extern CameraConfig g_camera_cfg;
extern PhysicsConfig g_physics_cfg;

// g_debug e um toggle de debug geral (nao especifico de camera) que continua definido em
// main.cpp; perdeu o "static" la porque update_camera_for_frame (abaixo) tambem precisa
// le-lo.
extern bool g_debug;

// ============= CAMERA 3D (Terceira Pessoa) =============
// Extracted verbatim from main.cpp (original lines ~204-546 and ~648-825).
GameCamera g_camera;

static float g_camera_adapt_pitch = 0.0f;
static float g_camera_adapt_distance_scale = 1.0f;
static float g_camera_adapt_target_lift = 0.0f;
GameCameraMode g_camera_mode = GameCameraMode::Open;
std::string g_camera_mode_reason = "Open";
float g_camera_obstruction = 0.0f;
float g_camera_enclosed = 0.0f;
float g_camera_hidden_time = 0.0f;
static std::unordered_map<int, float> g_camera_occluder_alpha;
std::array<CameraDebugRay, 96> g_camera_debug_rays = {};
int g_camera_debug_ray_count = 0;

const char* camera_mode_name(GameCameraMode mode) {
    switch (mode) {
        case GameCameraMode::Open: return "ABERTO";
        case GameCameraMode::SemiClosed: return "SEMI";
        case GameCameraMode::Cave: return "CAVERNA";
        case GameCameraMode::Emergency: return "EMERGENCIA";
        default: return "?";
    }
}

void reset_camera_near_player(bool reset_angles) {
    g_camera.distance = std::clamp(g_camera_cfg.spawn_distance, g_camera.min_distance, g_camera.max_distance);
    g_camera.effective_distance = g_camera.distance;
    if (reset_angles) {
        g_camera.pitch = std::clamp(g_camera_cfg.spawn_pitch, g_camera.min_pitch, g_camera.max_pitch);
        g_camera.yaw = g_camera_cfg.spawn_yaw;
    }
    g_camera_adapt_pitch = 0.0f;
    g_camera_adapt_distance_scale = 1.0f;
    g_camera_adapt_target_lift = 0.0f;
    g_camera_mode = GameCameraMode::Open;
    g_camera_mode_reason = "Spawn";
    g_camera_hidden_time = 0.0f;
    g_camera_obstruction = 0.0f;
    g_camera_enclosed = 0.0f;
    g_camera_occluder_alpha.clear();
}

// Calcular posicao da camera baseada em coordenadas esfericas
static void update_camera_position() {
    float rad_yaw = g_camera.yaw * (kPi / 180.0f);
    float adaptive_pitch = std::clamp(g_camera.pitch + g_camera_adapt_pitch, g_camera.min_pitch, g_camera.max_pitch);
    float rad_pitch = adaptive_pitch * (kPi / 180.0f);
    float dist = std::clamp(g_camera.effective_distance * g_camera_adapt_distance_scale, g_camera.min_distance, g_camera.max_distance);

    // Posicao da camera em coordenadas esfericas relativas ao target
    float x = dist * std::cos(rad_pitch) * std::sin(rad_yaw);
    float y = dist * std::sin(rad_pitch);
    float z = dist * std::cos(rad_pitch) * std::cos(rad_yaw);

    g_camera.position.x = g_camera.target.x + x;
    g_camera.position.y = g_camera.target.y + y;
    g_camera.position.z = g_camera.target.z + z;
}

// Aplicar matriz de view (gluLookAt manual)
void apply_look_at() {
    Vec3 f = vec3_normalize(vec3_sub(g_camera.target, g_camera.position)); // Forward
    Vec3 s = vec3_normalize(vec3_cross(f, g_camera.up));                   // Side (right)
    Vec3 u = vec3_cross(s, f);                                              // Up ajustado

    // Matriz de view (column-major para OpenGL)
    float m[16] = {
         s.x,  u.x, -f.x, 0.0f,
         s.y,  u.y, -f.y, 0.0f,
         s.z,  u.z, -f.z, 0.0f,
        -vec3_dot(s, g_camera.position),
        -vec3_dot(u, g_camera.position),
         vec3_dot(f, g_camera.position),
        1.0f
    };

    rlMultMatrixf(m);
}

// Projecao perspectiva manual
void apply_perspective(float fov_degrees, float aspect, float near_plane, float far_plane) {
    float fov_rad = fov_degrees * (kPi / 180.0f);
    float f = 1.0f / std::tan(fov_rad / 2.0f);

    float m[16] = {
        f / aspect, 0.0f,  0.0f,                                           0.0f,
        0.0f,       f,     0.0f,                                           0.0f,
        0.0f,       0.0f, (far_plane + near_plane) / (near_plane - far_plane), -1.0f,
        0.0f,       0.0f, (2.0f * far_plane * near_plane) / (near_plane - far_plane), 0.0f
    };

    rlMultMatrixf(m);
}

// Calcular direcao do ray a partir da posicao do mouse na tela
Vec3 get_mouse_ray_direction(int mouse_x, int mouse_y, int win_w, int win_h) {
    // FOV e aspect ratio usados na projecao
    const float kFov = kCameraFovDegrees;
    float aspect = (float)win_w / (float)win_h;
    float fov_rad = kFov * (kPi / 180.0f);
    float tan_half_fov = std::tan(fov_rad / 2.0f);

    // Normalizar coordenadas do mouse para [-1, 1]
    float ndc_x = (2.0f * (float)mouse_x / (float)win_w) - 1.0f;
    float ndc_y = 1.0f - (2.0f * (float)mouse_y / (float)win_h);  // Invertido porque Y da tela cresce para baixo

    // Direcao no espaco da camera (view space)
    float view_x = ndc_x * aspect * tan_half_fov;
    float view_y = ndc_y * tan_half_fov;
    float view_z = -1.0f;  // Aponta para frente da camera

    // Obter vetores da camera para transformar de view space para world space
    float yaw_rad = g_camera.yaw * (kPi / 180.0f);
    float pitch_rad = g_camera.pitch * (kPi / 180.0f);

    // Vetor forward da camera (para onde ela aponta)
    Vec3 cam_forward = vec3_normalize(vec3_sub(g_camera.target, g_camera.position));

    // Vetor right da camera
    Vec3 world_up = {0.0f, 1.0f, 0.0f};
    Vec3 cam_right = vec3_normalize(vec3_cross(cam_forward, world_up));

    // Vetor up da camera
    Vec3 cam_up = vec3_cross(cam_right, cam_forward);

    // Transformar direcao do view space para world space
    Vec3 ray_dir;
    ray_dir.x = cam_right.x * view_x + cam_up.x * view_y - cam_forward.x * view_z;
    ray_dir.y = cam_right.y * view_x + cam_up.y * view_y - cam_forward.y * view_z;
    ray_dir.z = cam_right.z * view_x + cam_up.z * view_y - cam_forward.z * view_z;

    return vec3_normalize(ray_dir);
}

// Verificar colisao da camera com o mundo usando raycast
// Nota: g_world e is_solid sao definidos apos esta funcao
struct CameraTraceResult {
    bool blocked = false;
    float first_hit_t = 1.0f;   // 0..1 ao longo do segmento
    float blocked_ratio = 0.0f; // 0..1
};

struct CameraVisibilityMetrics {
    float primary_ratio = 0.0f;
    float edge_ratio = 0.0f;
    float up_ratio = 0.0f;
    float total_ratio = 0.0f;
    float blocked_ray_ratio = 0.0f;
    bool primary_blocked = false;
};

static int camera_tile_key(int tx, int tz) {
    return ((tz & 0xFFFF) << 16) ^ (tx & 0xFFFF);
}

float camera_occluder_alpha_for_tile(int tx, int tz) {
    auto it = g_camera_occluder_alpha.find(camera_tile_key(tx, tz));
    if (it == g_camera_occluder_alpha.end()) return 1.0f;
    return std::clamp(it->second, 0.08f, 1.0f);
}

static bool camera_sample_hits_world(float sx, float sy, float sz, int* out_tx = nullptr, int* out_tz = nullptr) {
    if (!g_world) return false;

    int tx = world_to_tile(sx);
    int tz = world_to_tile(sz);
    if (out_tx) *out_tx = tx;
    if (out_tz) *out_tz = tz;

    if (!g_world->in_bounds(tx, tz)) return true;

    float terrain_y = (float)g_world->height_at(tx, tz) * kHeightScale;
    if (sy <= terrain_y + 0.06f) return true;

    Block obj = object_block_at(*g_world, tx, tz);
    float top = terrain_y;
    bool has_top = false;
    // is_furniture_collider: os blocos invisiveis de colisao de mobilia NAO devem ocluir a camera -
    // senao aproximar-se de uma cama/mesa puxa a camera pra dentro e desvanece os tiles visiveis em
    // volta (camera_occluder_alpha_for_tile), como se houvesse uma parede ali.
    if (obj != Block::Air && !is_furniture_collider(obj)) { top += get_block_height(obj); has_top = true; }
    int stack_h = g_world->stack_height_at(tx, tz);
    if (stack_h > 0) { top += (float)stack_h * 1.0f; has_top = true; }
    if (has_top && sy >= terrain_y - 0.03f && sy <= top + 0.02f) return true;
    return false;
}

static CameraTraceResult camera_trace_segment(const Vec3& from, const Vec3& to,
                                              bool collect_occluders,
                                              std::unordered_map<int, float>* out_occluders,
                                              bool store_debug_ray) {
    CameraTraceResult out = {};
    Vec3 seg = vec3_sub(to, from);
    float len = vec3_length(seg);
    if (len < 1e-4f) return out;
    Vec3 dir = vec3_scale(seg, 1.0f / len);

    float step = std::max(0.04f, g_camera_cfg.collision_probe_step);
    int steps = std::max(1, (int)std::ceil(len / step));
    int blocked_count = 0;
    Vec3 first_hit_pos = to;

    for (int i = 1; i <= steps; ++i) {
        float t = (float)i / (float)steps;
        Vec3 p = vec3_add(from, vec3_scale(dir, len * t));
        int tx = 0, tz = 0;
        bool hit = camera_sample_hits_world(p.x, p.y, p.z, &tx, &tz);
        if (!hit) continue;

        blocked_count++;
        if (!out.blocked) {
            out.blocked = true;
            out.first_hit_t = t;
            first_hit_pos = p;
        }

        if (collect_occluders && out_occluders && g_world && g_world->in_bounds(tx, tz)) {
            int key = camera_tile_key(tx, tz);
            auto it = out_occluders->find(key);
            float desired = std::clamp(g_camera_cfg.transparency_alpha, 0.08f, 0.95f);
            if (it == out_occluders->end()) (*out_occluders)[key] = desired;
            else it->second = std::min(it->second, desired);
        }
    }

    out.blocked_ratio = (float)blocked_count / (float)steps;

    if (store_debug_ray && g_camera_debug_ray_count < (int)g_camera_debug_rays.size()) {
        CameraDebugRay& dbg = g_camera_debug_rays[(size_t)g_camera_debug_ray_count++];
        dbg.from = from;
        dbg.to = out.blocked ? first_hit_pos : to;
        dbg.blocked = out.blocked;
    }
    return out;
}

static void update_camera_occluder_fade(const std::unordered_map<int, float>& desired, float dt) {
    auto move_towards = [](float cur, float target, float max_delta) -> float {
        float d = target - cur;
        if (d > max_delta) return cur + max_delta;
        if (d < -max_delta) return cur - max_delta;
        return target;
    };

    float fade_in = std::max(0.1f, g_camera_cfg.transparency_fade_in);
    float fade_out = std::max(0.1f, g_camera_cfg.transparency_fade_out);

    for (auto it = g_camera_occluder_alpha.begin(); it != g_camera_occluder_alpha.end();) {
        auto dit = desired.find(it->first);
        float target = (dit != desired.end()) ? dit->second : 1.0f;
        float speed = (target < it->second) ? fade_in : fade_out;
        it->second = move_towards(it->second, target, speed * dt);
        if (dit == desired.end() && it->second >= 0.995f) it = g_camera_occluder_alpha.erase(it);
        else ++it;
    }

    for (const auto& kv : desired) {
        if (g_camera_occluder_alpha.find(kv.first) != g_camera_occluder_alpha.end()) continue;
        g_camera_occluder_alpha[kv.first] = move_towards(1.0f, kv.second, fade_in * dt);
    }
}

void check_camera_collision() {
    if (!g_world) return;

    // Direcao do target para a posicao desejada da camera
    Vec3 dir = vec3_sub(g_camera.position, g_camera.target);
    float max_dist = vec3_length(dir);
    if (max_dist < 0.1f) {
        g_camera.effective_distance = g_camera.distance;
        return;
    }

    dir = vec3_normalize(dir);
    g_camera.effective_distance = g_camera.distance;

    Vec3 world_up = {0.0f, 1.0f, 0.0f};
    Vec3 right = vec3_cross(dir, world_up);
    if (vec3_length(right) < 0.0001f) right = {1.0f, 0.0f, 0.0f};
    right = vec3_normalize(right);
    Vec3 up = vec3_normalize(vec3_cross(right, dir));

    const Vec3 offsets[] = {
        {0.0f, 0.0f, 0.0f},
        vec3_scale(right, 0.18f),
        vec3_scale(right, -0.18f),
        vec3_scale(up, 0.18f),
        vec3_scale(up, -0.18f),
    };

    bool blocked_any = false;
    float min_hit_t = 1.0f;
    for (const Vec3& off : offsets) {
        CameraTraceResult tr = camera_trace_segment(
            vec3_add(g_camera.target, off),
            vec3_add(g_camera.position, off),
            false, nullptr, false);
        if (!tr.blocked) continue;
        blocked_any = true;
        min_hit_t = std::min(min_hit_t, tr.first_hit_t);
    }

    if (blocked_any) {
        float safe_dist = max_dist * min_hit_t - g_camera_cfg.collision_padding;
        g_camera.effective_distance = std::clamp(
            safe_dist,
            std::max(0.35f, g_camera_cfg.min_collision_distance),
            g_camera.distance);
    }
}

// Atualiza alvo/posicao da camera para o frame atual (inclui offset para estilo Minicraft: player levemente fora do centro).
void update_camera_for_frame() {
    Vec2 rpos = get_player_render_pos();
    float ry = get_player_render_y();
    float frame_dt = std::max(1.0f / 240.0f, g_physics_cfg.fixed_timestep);

    // Suaviza so a altura vertical do foco - o X/Z e o proprio personagem continuam seguindo
    // a fisica direto. Sem isso, cada micro-degrau de terreno subido instantaneamente por
    // try_step_climb (player_physics.cpp) virava um solavanco na camera; uma ladeira em
    // varios degraus seguidos (mais fronteiras de tile cruzadas por metro andado em certos
    // angulos) somava esses solavancos numa tremida perceptivel.
    static float s_cam_smooth_y = 0.0f;
    static bool s_cam_smooth_y_init = false;
    // Teletransportes (respawn, porta da cupula) movem o jogador dezenas/centenas de
    // unidades num unico frame - sem esse snap, o lerp abaixo faria a camera "alcancar"
    // visivelmente o jogador ao longo de varios frames em vez de acompanhar na hora.
    if (!s_cam_smooth_y_init || std::fabs(ry - s_cam_smooth_y) > 5.0f) {
        s_cam_smooth_y = ry;
        s_cam_smooth_y_init = true;
    }
    s_cam_smooth_y = lerp(s_cam_smooth_y, ry, clamp01(g_camera_cfg.step_height_lerp));

    // Base do foco (player sempre centralizado).
    Vec3 focus_base = {rpos.x, s_cam_smooth_y + 1.10f, rpos.y};

    // Analise macro de profundidade/enclausuramento.
    float pit_factor = 0.0f;
    float enclosed_factor = 0.0f;
    if (g_world) {
        int px = world_to_tile(rpos.x);
        int pz = world_to_tile(rpos.y);
        if (g_world->in_bounds(px, pz)) {
            float here_h = (float)g_world->height_at(px, pz) * kHeightScale;
            float neigh_max = here_h;
            int high_walls = 0;
            int samples = 0;
            for (int oz = -3; oz <= 3; ++oz) {
                for (int ox = -3; ox <= 3; ++ox) {
                    if (ox == 0 && oz == 0) continue;
                    int tx = px + ox;
                    int tz = pz + oz;
                    if (!g_world->in_bounds(tx, tz)) continue;
                    float h = (float)g_world->height_at(tx, tz) * kHeightScale;
                    float d = std::sqrt((float)(ox * ox + oz * oz));
                    neigh_max = std::max(neigh_max, h - d * 0.16f);
                    float rise = h - here_h;
                    if (rise > 0.52f + d * 0.12f) high_walls++;
                    samples++;
                }
            }
            float pit_depth = std::max(0.0f, neigh_max - here_h);
            pit_factor = smoothstep01(g_camera_cfg.cave_depth_start, g_camera_cfg.cave_depth_end, pit_depth);
            if (samples > 0) {
                float enclosed = (float)high_walls / (float)samples;
                enclosed_factor = smoothstep01(g_camera_cfg.enclosed_start, g_camera_cfg.enclosed_end, enclosed);
            }
        }
    }

    auto evaluate_visibility = [&](const Vec3& target, const Vec3& cam_pos,
                                   bool collect_occluders, bool store_debug,
                                   std::unordered_map<int, float>* out_occ) -> CameraVisibilityMetrics {
        CameraVisibilityMetrics m = {};
        Vec3 view_dir = vec3_sub(target, cam_pos);
        float cam_len = vec3_length(view_dir);
        if (cam_len < 1e-4f) return m;
        view_dir = vec3_scale(view_dir, 1.0f / cam_len);

        Vec3 world_up = {0.0f, 1.0f, 0.0f};
        Vec3 right = vec3_cross(view_dir, world_up);
        if (vec3_length(right) < 0.0001f) right = {1.0f, 0.0f, 0.0f};
        right = vec3_normalize(right);
        Vec3 up = vec3_normalize(vec3_cross(right, view_dir));

        float edge_off = std::clamp(cam_len * 0.18f, 0.45f, 1.55f);
        Vec3 edge_pts[4] = {
            vec3_add(cam_pos, vec3_scale(right, edge_off)),
            vec3_add(cam_pos, vec3_scale(right, -edge_off)),
            vec3_add(cam_pos, vec3_scale(up, edge_off * 0.95f)),
            vec3_add(cam_pos, vec3_scale(up, -edge_off * 0.70f)),
        };

        CameraTraceResult main_tr = camera_trace_segment(target, cam_pos, collect_occluders, out_occ, store_debug);
        m.primary_ratio = main_tr.blocked_ratio;
        m.primary_blocked = main_tr.blocked;

        float edge_ratio_acc = 0.0f;
        int edge_hits = 0;
        for (const Vec3& p : edge_pts) {
            CameraTraceResult tr = camera_trace_segment(target, p, collect_occluders, out_occ, store_debug);
            edge_ratio_acc += tr.blocked_ratio;
            if (tr.blocked) edge_hits++;
        }
        m.edge_ratio = edge_ratio_acc * 0.25f;

        Vec3 up_end = vec3_add(target, Vec3{0.0f, 2.9f, 0.0f});
        CameraTraceResult up_tr = camera_trace_segment(target, up_end, collect_occluders, out_occ, store_debug);
        m.up_ratio = up_tr.blocked_ratio;

        // Peso maior no ray principal (player -> camera) e nas bordas da tela.
        m.total_ratio = std::clamp((m.primary_ratio * 0.55f) + (m.edge_ratio * 0.35f) + (m.up_ratio * 0.10f), 0.0f, 1.0f);
        float blocked_weight = (main_tr.blocked ? 3.0f : 0.0f) + (float)edge_hits + (up_tr.blocked ? 1.0f : 0.0f);
        m.blocked_ray_ratio = blocked_weight / 8.0f; // 3 + 4 + 1
        return m;
    };

    // 1) Sonda com o estado atual para decidir o modo.
    g_camera.target = vec3_add(focus_base, Vec3{0.0f, g_camera_adapt_target_lift, 0.0f});
    g_camera.effective_distance = g_camera.distance;
    update_camera_position();
    check_camera_collision();
    update_camera_position();
    CameraVisibilityMetrics vis_probe = evaluate_visibility(g_camera.target, g_camera.position, false, false, nullptr);

    float occlusion_factor = std::clamp(
        vis_probe.total_ratio * 0.80f + vis_probe.blocked_ray_ratio * 0.35f,
        0.0f, 1.0f);
    float cave_score = std::max(std::max(pit_factor, enclosed_factor), occlusion_factor);

    if (vis_probe.primary_blocked || vis_probe.blocked_ray_ratio > 0.62f) {
        g_camera_hidden_time += frame_dt;
    } else {
        g_camera_hidden_time = std::max(0.0f, g_camera_hidden_time - frame_dt * 1.8f);
    }

    GameCameraMode desired_mode = GameCameraMode::Open;
    if (g_camera_hidden_time >= g_camera_cfg.emergency_hidden_time) {
        desired_mode = GameCameraMode::Emergency;
    } else if (cave_score >= 0.62f || (pit_factor > 0.52f && occlusion_factor > 0.45f)) {
        desired_mode = GameCameraMode::Cave;
    } else if (cave_score >= 0.30f) {
        desired_mode = GameCameraMode::SemiClosed;
    }

    float desired_pitch_abs = g_camera_cfg.open_pitch;
    float desired_scale = g_camera_cfg.open_distance_scale;
    float desired_lift = g_camera_cfg.open_target_lift;

    if (desired_mode == GameCameraMode::SemiClosed) {
        float t = smoothstep01(0.30f, 0.72f, cave_score);
        desired_pitch_abs = lerp(g_camera_cfg.open_pitch, g_camera_cfg.semi_pitch, t);
        desired_scale = lerp(g_camera_cfg.open_distance_scale, g_camera_cfg.semi_distance_scale, t);
        desired_lift = lerp(g_camera_cfg.open_target_lift, g_camera_cfg.semi_target_lift, t);
    } else if (desired_mode == GameCameraMode::Cave) {
        float t = smoothstep01(0.44f, 0.95f, std::max(cave_score, occlusion_factor));
        desired_pitch_abs = lerp(g_camera_cfg.semi_pitch, g_camera_cfg.cave_pitch, t);
        desired_scale = lerp(g_camera_cfg.semi_distance_scale, g_camera_cfg.cave_distance_scale, t);
        desired_lift = lerp(g_camera_cfg.semi_target_lift, g_camera_cfg.cave_target_lift, t);
    } else if (desired_mode == GameCameraMode::Emergency) {
        desired_pitch_abs = g_camera_cfg.emergency_pitch;
        desired_scale = g_camera_cfg.emergency_distance_scale;
        desired_lift = g_camera_cfg.emergency_target_lift;
    }

    float desired_pitch_offset = desired_pitch_abs - g_camera.pitch;
    float min_off = g_camera.min_pitch - g_camera.pitch;
    float max_off = g_camera.max_pitch - g_camera.pitch;
    desired_pitch_offset = std::clamp(desired_pitch_offset, min_off, max_off);

    float pitch_lerp_t = clamp01(g_camera_cfg.pitch_lerp);
    float dist_lerp_t = clamp01(g_camera_cfg.distance_lerp);
    float lift_lerp_t = clamp01(g_camera_cfg.lift_lerp);
    g_camera_adapt_pitch = lerp(g_camera_adapt_pitch, desired_pitch_offset, pitch_lerp_t);
    g_camera_adapt_distance_scale = lerp(g_camera_adapt_distance_scale, desired_scale, dist_lerp_t);
    g_camera_adapt_target_lift = lerp(g_camera_adapt_target_lift, desired_lift, lift_lerp_t);

    g_camera_mode = desired_mode;
    g_camera_enclosed = lerp(g_camera_enclosed, enclosed_factor, 0.20f);

    float reason_pit = pit_factor;
    float reason_occ = occlusion_factor;
    float reason_enc = enclosed_factor;
    if (desired_mode == GameCameraMode::Emergency) g_camera_mode_reason = "Oculto";
    else if (reason_pit >= reason_occ && reason_pit >= reason_enc) g_camera_mode_reason = "Buraco";
    else if (reason_enc >= reason_occ) g_camera_mode_reason = "Fechado";
    else g_camera_mode_reason = "Obstrucao";

    // 2) Aplica modo final + colisao de camera (sweep multi-ray).
    g_camera.target = vec3_add(focus_base, Vec3{0.0f, g_camera_adapt_target_lift, 0.0f});
    g_camera.effective_distance = g_camera.distance;
    update_camera_position();
    check_camera_collision();
    update_camera_position();

    // 3) Visibilidade final (debug + transparencia de blocos que obstruem).
    std::unordered_map<int, float> occ_tiles;
    g_camera_debug_ray_count = 0;
    CameraVisibilityMetrics vis_final = evaluate_visibility(g_camera.target, g_camera.position, true, g_debug, &occ_tiles);
    float final_occlusion = std::clamp(
        vis_final.total_ratio * 0.80f + vis_final.blocked_ray_ratio * 0.35f,
        0.0f, 1.0f);
    g_camera_obstruction = lerp(g_camera_obstruction, final_occlusion, 0.24f);
    update_camera_occluder_fade(occ_tiles, frame_dt);
}

// Ver comentario da declaracao em camera.h.
bool world_to_screen(Vec3 world, int win_w, int win_h, float& out_x, float& out_y) {
    Vec3 cam_forward = vec3_normalize(vec3_sub(g_camera.target, g_camera.position));
    Vec3 world_up = {0.0f, 1.0f, 0.0f};
    Vec3 cam_right = vec3_normalize(vec3_cross(cam_forward, world_up));
    Vec3 cam_up = vec3_cross(cam_right, cam_forward);

    Vec3 rel = vec3_sub(world, g_camera.position);
    // Coordenadas no espaco da camera. z_view > 0 = na frente (mesma convencao do -cam_forward
    // usado em get_mouse_ray_direction).
    float x_view = vec3_dot(rel, cam_right);
    float y_view = vec3_dot(rel, cam_up);
    float z_view = vec3_dot(rel, cam_forward);
    if (z_view <= 0.0001f) return false;

    float aspect = (float)win_w / (float)std::max(1, win_h);
    float tan_half = std::tan(kCameraFovDegrees * (kPi / 180.0f) * 0.5f);
    float ndc_x = (x_view / z_view) / (tan_half * aspect);
    float ndc_y = (y_view / z_view) / tan_half;

    out_x = ((ndc_x + 1.0f) * 0.5f) * (float)win_w;
    out_y = ((1.0f - ndc_y) * 0.5f) * (float)win_h;
    return true;
}
