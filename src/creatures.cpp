#include "creatures.h"

#include "raylib_platform.h"
#include "world.h"
#include "player_physics.h"
#include "ui_hud.h"          // g_hud_pointer_over_button (nao atirar clicando na HUD)
#include "interiors.h"      // interior_at (nao spawnar criatura dentro de uma sala)
#include "game_state.h"        // set_toast, rng_next_f01, kDayLength
#include "items_particles.h"   // spawn_item_drop, spawn_block_particles
#include "render_primitives.h"
#include "audio.h"              // play_laser_fire_sound, play_laser_impact_sound

#include <cmath>
#include <algorithm>
#include <string>

// add_alert() nao tem header proprio (padrao ja usado por modules_building.cpp/
// minimap.cpp) - so' declaracao local, definida em main.cpp.
void add_alert(const std::string& msg, float r, float g, float b, float duration = 3.0f, float cooldown = 5.0f);
// key_down()/g_day_time idem: definidos em main.cpp, extern local (mesmo padrao de
// building_interaction.cpp).
bool key_down(int vk);
extern float g_day_time;

std::vector<Creature> g_creatures;

namespace {

constexpr int kMaxCreatures = 3;
constexpr float kSpawnMinInterval = 30.0f;
constexpr float kSpawnMaxInterval = 70.0f;
constexpr float kSpawnMinDist = 20.0f;
constexpr float kSpawnMaxDist = 45.0f;
constexpr float kNightSpawnGate = 0.15f; // abaixo disso (quase dia pleno), nao rola spawn
constexpr float kDetectionRange = 14.0f;
constexpr float kDespawnDist = 150.0f;
constexpr float kWanderSpeed = 1.3f;
constexpr float kChaseSpeed = 3.7f;
constexpr float kContactRadius = 1.1f;
constexpr int kContactDamage = 4;
constexpr float kContactCooldown = 1.5f;
constexpr float kRespawnGraceSeconds = 5.0f;

constexpr float kLaserRange = 35.0f;
constexpr float kLaserHitRadius = 0.8f;
constexpr int kLaserDamage = 10;
constexpr float kFireCooldownSeconds = 0.33f;
constexpr float kLaserTraceDuration = 0.13f;
constexpr float kMuzzleFlashDuration = 0.08f;

float g_respawn_grace = 0.0f;
float g_fire_cooldown = 0.0f;
float g_laser_trace_timer = 0.0f;
Vec3 g_laser_trace_a{};
Vec3 g_laser_trace_b{};
float g_muzzle_flash_timer = 0.0f;

// Efeito de impacto (faiscas + explosao de verdade: anel de estilhacos + flash central,
// mesma tecnica ja aprovada da onda de choque de pouso do jetpack, main.cpp) + marca de
// queimado (decal escuro no chao, dura bem mais) - pedido do jogador: "efeito de atingir o
// alvo... marcas de queimado", depois "sem efeito de explosao no local do impacto".
constexpr float kImpactFlashDuration = 0.35f; // vida total (sparks usam isso pra fade)
constexpr float kExplosionRingLife = 0.30f;   // anel de estilhacos - sub-janela dentro do timer acima
constexpr float kExplosionFlashLife = 0.14f;  // flash central - mais rapido, "momento da detonacao"
constexpr float kScorchMarkDuration = 25.0f;
struct ImpactFlash { Vec3 pos; Vec3 ground_pos; float timer; };
struct ScorchMark { Vec3 pos; float timer; };
std::vector<ImpactFlash> g_impact_flashes;
std::vector<ScorchMark> g_scorch_marks;

// Hash pseudo-aleatorio deterministico (mesmo indice sempre da o mesmo valor) - mesma
// tecnica ja usada pela onda de choque de pouso do jetpack (main.cpp).
float hash01(int i, float salt) {
    float x = std::sin((float)i * 12.9898f + salt * 78.233f) * 43758.5453f;
    return x - std::floor(x);
}

bool tile_is_wet_or_lava(const World& world, int tx, int tz) {
    Block g = world.get_ground(tx, tz);
    return g == Block::Water || g == Block::Ice || g == Block::Lava;
}

float creature_ground_y(const World& world, float x, float z) {
    int tx = world_to_tile(x);
    int tz = world_to_tile(z);
    if (!world.in_bounds(tx, tz)) return 0.0f;
    return stack_top_height_at(world, tx, tz);
}

void spawn_creature() {
    if (!g_world) return;
    float ang = rng_next_f01() * 2.0f * kPi;
    float dist = kSpawnMinDist + rng_next_f01() * (kSpawnMaxDist - kSpawnMinDist);
    float sx = g_player.pos.x + std::cos(ang) * dist;
    float sz = g_player.pos.y + std::sin(ang) * dist;
    int tx = world_to_tile(sx);
    int tz = world_to_tile(sz);
    if (!g_world->in_bounds(tx, tz)) return;
    if (tile_is_wet_or_lava(*g_world, tx, tz)) return;

    Creature c;
    c.x = tile_center(tx);
    c.z = tile_center(tz);
    c.y = stack_top_height_at(*g_world, tx, tz);
    c.wander_target_x = c.x;
    c.wander_target_z = c.z;
    c.yaw = rng_next_f01() * 2.0f * kPi;
    g_creatures.push_back(c);
}

void pick_new_wander_target(Creature& c) {
    if (!g_world) return;
    for (int attempt = 0; attempt < 5; ++attempt) {
        float ang = rng_next_f01() * 2.0f * kPi;
        float dist = 4.0f + rng_next_f01() * 8.0f;
        float tx_f = c.x + std::cos(ang) * dist;
        float tz_f = c.z + std::sin(ang) * dist;
        int tx = world_to_tile(tx_f);
        int tz = world_to_tile(tz_f);
        if (!g_world->in_bounds(tx, tz)) continue;
        if (tile_is_wet_or_lava(*g_world, tx, tz)) continue;
        c.wander_target_x = tile_center(tx);
        c.wander_target_z = tile_center(tz);
        return;
    }
    // Sem alvo valido por perto - fica parado ate a proxima tentativa.
    c.wander_target_x = c.x;
    c.wander_target_z = c.z;
}

} // namespace

void notify_player_respawned() {
    g_respawn_grace = kRespawnGraceSeconds;
}

void update_creatures(float dt) {
    g_fire_cooldown = std::max(0.0f, g_fire_cooldown - dt);
    g_laser_trace_timer = std::max(0.0f, g_laser_trace_timer - dt);
    g_respawn_grace = std::max(0.0f, g_respawn_grace - dt);
    g_muzzle_flash_timer = std::max(0.0f, g_muzzle_flash_timer - dt);

    for (auto it = g_impact_flashes.begin(); it != g_impact_flashes.end();) {
        it->timer -= dt;
        if (it->timer <= 0.0f) it = g_impact_flashes.erase(it); else ++it;
    }
    for (auto it = g_scorch_marks.begin(); it != g_scorch_marks.end();) {
        it->timer -= dt;
        if (it->timer <= 0.0f) it = g_scorch_marks.erase(it); else ++it;
    }

    if (!g_world) return;

    // ---- Spawn ----
    static float spawn_timer = 0.0f;
    static float spawn_next = kSpawnMinInterval + 0.0f; // primeiro sorteio real logo abaixo
    static bool spawn_seeded = false;
    if (!spawn_seeded) {
        spawn_next = kSpawnMinInterval + rng_next_f01() * (kSpawnMaxInterval - kSpawnMinInterval);
        spawn_seeded = true;
    }
    spawn_timer += dt;
    if (spawn_timer >= spawn_next) {
        spawn_timer = 0.0f;
        spawn_next = kSpawnMinInterval + rng_next_f01() * (kSpawnMaxInterval - kSpawnMinInterval);
        float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
        float night_alpha = compute_night_alpha(day_phase);
        if ((int)g_creatures.size() < kMaxCreatures && night_alpha > kNightSpawnGate &&
            interior_at(g_player.pos.x, g_player.pos.y) < 0) {
            // Nunca dentro de um interior: o spawn e' a 20-45 tiles do JOGADOR, entao estando numa
            // sala do distrito a criatura nasceria dentro do proprio laboratorio/dormitorio.
            spawn_creature();
        }
    }

    // ---- IA + dano de contato ----
    for (size_t i = 0; i < g_creatures.size();) {
        Creature& c = g_creatures[(size_t)i];
        c.contact_cooldown = std::max(0.0f, c.contact_cooldown - dt);
        c.anim_timer += dt;

        int ctx = world_to_tile(c.x);
        int ctz = world_to_tile(c.z);
        if (!g_world->in_bounds(ctx, ctz) || tile_is_wet_or_lava(*g_world, ctx, ctz)) {
            g_creatures.erase(g_creatures.begin() + (long)i);
            continue;
        }

        float dx = g_player.pos.x - c.x;
        float dz = g_player.pos.y - c.z;
        float dist2 = dx * dx + dz * dz;

        if (dist2 > kDespawnDist * kDespawnDist) {
            g_creatures.erase(g_creatures.begin() + (long)i);
            continue;
        }

        bool graced = g_respawn_grace > 0.0f;
        if (!graced && dist2 <= kDetectionRange * kDetectionRange) {
            c.state = Creature::State::Chasing;
        } else if (graced || dist2 > kDetectionRange * kDetectionRange * 1.4f) {
            c.state = Creature::State::Wandering;
        }

        float speed = (c.state == Creature::State::Chasing) ? kChaseSpeed : kWanderSpeed;
        float tx_move, tz_move;
        if (c.state == Creature::State::Chasing) {
            tx_move = g_player.pos.x;
            tz_move = g_player.pos.y;
        } else {
            c.wander_timer -= dt;
            if (c.wander_timer <= 0.0f) {
                pick_new_wander_target(c);
                c.wander_timer = 3.0f + rng_next_f01() * 3.0f;
            }
            tx_move = c.wander_target_x;
            tz_move = c.wander_target_z;
        }

        float mdx = tx_move - c.x;
        float mdz = tz_move - c.z;
        float mdist = std::sqrt(mdx * mdx + mdz * mdz);
        if (mdist > 0.05f) {
            c.yaw = std::atan2(mdz, mdx);
            float step = std::min(mdist, speed * dt);
            c.x += (mdx / mdist) * step;
            c.z += (mdz / mdist) * step;
        }
        c.y = creature_ground_y(*g_world, c.x, c.z);

        // Dano de contato - nao rola durante a graca pos-respawn.
        if (!graced && dist2 <= kContactRadius * kContactRadius && c.contact_cooldown <= 0.0f) {
            c.contact_cooldown = kContactCooldown;
            g_player.hp -= kContactDamage;
            set_toast("Uma criatura o atacou!", 1.2f);
            if (g_player.hp <= 0) {
                g_player.hp = 0;
                respawn_player_at_base("Ataque de criatura");
            }
        }

        ++i;
    }
}

void render_creatures() {
    // Marcas de queimado (decais escuros no chao) - desenhadas primeiro, embaixo de tudo.
    for (const ScorchMark& s : g_scorch_marks) {
        float a = clamp01(s.timer / kScorchMarkDuration) * 0.55f;
        render_plane_3d(s.pos.x, s.pos.y + 0.015f, s.pos.z, 0.65f, 0.05f, 0.04f, 0.03f, a);
    }

    for (const Creature& c : g_creatures) {
        float glow = 0.6f + 0.4f * std::sin(c.anim_timer * 3.0f);
        float bob = std::sin(c.anim_timer * 4.0f) * 0.05f;
        float chase_mult = (c.state == Creature::State::Chasing) ? 1.6f : 1.0f;
        float walk = std::sin(c.anim_timer * 6.0f * chase_mult) * 0.10f;

        float body_y = c.y + 0.32f + bob;
        render_cube_3d(c.x, body_y, c.z, 0.42f, 0.25f * glow, 0.75f * glow, 0.35f * glow, 1.0f, true);

        float head_y = c.y + 0.62f + bob;
        render_cube_3d(c.x, head_y, c.z, 0.26f, 0.55f * glow, 0.90f * glow, 0.55f * glow, 1.0f, true);

        // "Olho"/glow - cubo pequeno bem brilhante virado pra direcao de movimento, pra ler
        // como criatura viva (nao pedra/minerio) mesmo a distancia.
        float fx = c.x + std::cos(c.yaw) * 0.13f;
        float fz = c.z + std::sin(c.yaw) * 0.13f;
        render_cube_3d(fx, head_y, fz, 0.10f, 0.85f, 1.0f, 0.85f, 1.0f, false);

        float leg_y = c.y + 0.12f;
        render_cube_3d(c.x - 0.12f, leg_y + walk, c.z, 0.14f, 0.20f * glow, 0.55f * glow, 0.28f * glow, 1.0f, false);
        render_cube_3d(c.x + 0.12f, leg_y - walk, c.z, 0.14f, 0.20f * glow, 0.55f * glow, 0.28f * glow, 1.0f, false);
    }

    // Feixe grosso e brilhante (nucleo + halo) - substitui a linha de 1px de antes, que nao
    // tinha espessura/brilho nenhum (feedback do jogador). Aditivo, com escrita de
    // profundidade desligada enquanto desenha os 2 quads sobrepostos deste mesmo efeito
    // (senao brigam por z-order entre si e aparece uma costura visivel).
    if (g_laser_trace_timer > 0.0f) {
        float alpha = clamp01(g_laser_trace_timer / kLaserTraceDuration);
        rlSetTexture(0);
        rlSetBlendMode(RL_BLEND_ADDITIVE);
        rlDisableDepthMask();
        render_beam_3d(g_laser_trace_a, g_laser_trace_b, 0.16f, 0.35f, 0.85f, 1.0f, alpha * 0.38f);
        render_beam_3d(g_laser_trace_a, g_laser_trace_b, 0.055f, 0.85f, 0.97f, 1.0f, alpha * 0.9f);
        rlEnableDepthMask();
        rlSetBlendMode(RL_BLEND_ALPHA);
    }

    // Flash do cano (todo tiro, acerto ou erro - reforca "saiu da arma") - desenhado aqui
    // porque so' esta funcao roda todo frame independente de main.cpp; a POSICAO usada e' a
    // mesma get_weapon_muzzle_pos() usada em try_fire_laser_pistol.
    if (g_muzzle_flash_timer > 0.0f) {
        float t = clamp01(g_muzzle_flash_timer / kMuzzleFlashDuration);
        Vec3 muzzle = get_weapon_muzzle_pos();
        rlSetTexture(0);
        rlSetBlendMode(RL_BLEND_ADDITIVE);
        rlDisableDepthMask();
        render_glow_disc_3d(muzzle, 0.12f, 0.85f, 0.95f, 1.0f, t * 0.85f, 14);
        rlEnableDepthMask();
        rlSetBlendMode(RL_BLEND_ALPHA);
    }

    // Explosao de verdade no impacto (so' quando o tiro acerta uma criatura, ver
    // try_fire_laser_pistol) - anel de estilhacos (mesma tecnica ja aprovada da onda de
    // choque de pouso do jetpack, main.cpp, reescalada pro tamanho de uma criatura e
    // retintada laranja/ambar) + flash central branco->ciano (mais rapido, "momento da
    // detonacao") + as faiscas pequenas que ja existiam, por cima, sem custo extra.
    for (const ImpactFlash& f : g_impact_flashes) {
        float elapsed = kImpactFlashDuration - f.timer;

        float ring_t = clamp01(elapsed / kExplosionRingLife);
        if (ring_t < 1.0f) {
            float ring_radius = 0.15f + ring_t * 0.60f;
            float ring_alpha = (1.0f - ring_t) * 0.85f;
            constexpr int kRingChunks = 11;
            for (int i = 0; i < kRingChunks; ++i) {
                float ang = ((float)i / (float)kRingChunks) * 2.0f * kPi + hash01(i, f.pos.x) * 0.35f;
                float r = ring_radius * (0.80f + hash01(i, f.pos.z + 2.0f) * 0.35f);
                float dx = std::cos(ang) * r;
                float dz = std::sin(ang) * r;
                float size = (0.10f + hash01(i, 3.0f) * 0.08f) * (1.0f - ring_t * 0.3f);
                float shade = 0.85f + hash01(i, 4.0f) * 0.15f;
                render_plane_3d(f.ground_pos.x + dx, f.ground_pos.y + 0.02f, f.ground_pos.z + dz,
                                size, shade, shade * 0.55f, shade * 0.22f, ring_alpha);
            }
        }

        float flash_t = clamp01(elapsed / kExplosionFlashLife);
        if (flash_t < 1.0f) {
            float flash_radius = 0.20f + flash_t * 0.25f;
            float flash_alpha = (1.0f - flash_t);
            rlSetTexture(0);
            rlSetBlendMode(RL_BLEND_ADDITIVE);
            rlDisableDepthMask();
            render_glow_disc_3d(f.pos, flash_radius, 0.90f, 0.95f, 1.0f, flash_alpha * 0.9f, 16);
            rlEnableDepthMask();
            rlSetBlendMode(RL_BLEND_ALPHA);
        }

        float t = clamp01(f.timer / kImpactFlashDuration);
        for (int k = 0; k < 5; ++k) {
            float ang = (float)k / 5.0f * 2.0f * kPi + f.pos.x * 3.7f;
            float spread = (1.0f - t) * 0.30f;
            float sx = f.pos.x + std::cos(ang) * spread;
            float sz = f.pos.z + std::sin(ang) * spread;
            float sy = f.pos.y + spread * 0.6f;
            render_cube_3d(sx, sy, sz, 0.05f + 0.05f * t, 1.0f, 0.80f, 0.35f, t, false);
        }
    }
}

void try_fire_laser_pistol(const Vec3& ray_o, const Vec3& ray_d, float dt) {
    (void)dt; // cooldown ja decrementado 1x por frame em update_creatures()
    // g_hud_pointer_over_button: clicar no icone da arma na HUD nao pode disparar. O tiro usa o botao
    // SEGURADO, entao consumir o flag de clique no botao (o que a HUD fazia) nao impedia nada - bug
    // reportado ("quando clico na arma ela sempre dispara").
    bool fire_input = (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !g_hud_pointer_over_button)
                      || key_down(KEY_E);
    if (!fire_input || g_fire_cooldown > 0.0f) return;

    g_fire_cooldown = kFireCooldownSeconds;
    g_muzzle_flash_timer = kMuzzleFlashDuration; // todo tiro, acerto ou erro

    float best_t = kLaserRange;
    int best_idx = -1;
    for (int i = 0; i < (int)g_creatures.size(); ++i) {
        const Creature& c = g_creatures[(size_t)i];
        Vec3 cpos = {c.x, c.y + 0.45f, c.z};
        Vec3 rel = vec3_sub(cpos, ray_o);
        float t = vec3_dot(rel, ray_d);
        if (t < 0.2f || t > best_t) continue;
        Vec3 closest = vec3_add(ray_o, vec3_scale(ray_d, t));
        Vec3 diff = vec3_sub(cpos, closest);
        float perp2 = vec3_dot(diff, diff);
        if (perp2 <= kLaserHitRadius * kLaserHitRadius) {
            best_t = t;
            best_idx = i;
        }
    }

    // Colisao com terreno/objetos - sem isso o tiro atravessava tudo que nao fosse criatura
    // e ia parar no vazio a 35 unidades de distancia, num ponto que quase nunca batia com o
    // que o jogador via na mira (feedback: "o tiro nao esta indo na direcao certa" e "nao
    // tem efeito de colisao nenhum"). Raymarch simples por altura de terreno (mesma ideia ja
    // usada pra sondar chao em outros lugares deste arquivo), nao o AABB preciso da
    // mineracao - suficiente pra um hitscan. Só considera terreno mais perto que qualquer
    // criatura ja encontrada (nao atravessa parede pra acertar uma criatura atras dela).
    bool hit_terrain = false;
    if (g_world) {
        constexpr float kStep = 0.15f;
        for (float t = 0.3f; t < best_t; t += kStep) {
            Vec3 p = vec3_add(ray_o, vec3_scale(ray_d, t));
            int tx = world_to_tile(p.x);
            int tz = world_to_tile(p.z);
            if (!g_world->in_bounds(tx, tz)) { best_t = t; hit_terrain = true; break; }
            float h = stack_top_height_at(*g_world, tx, tz);
            if (p.y <= h) { best_t = t; hit_terrain = true; best_idx = -1; break; }
        }
    }

    // Origem VISUAL do traco = ponta do cano da arma (get_weapon_muzzle_pos), nao o olho da
    // camera - senao o tiro parece sair do ponteiro do mouse/mira em vez da arma na mao
    // (feedback do jogador). O teste de acerto acima continua usando ray_o/ray_d (camera+
    // mouse) sem nenhuma mudanca - so' o ponto desenhado muda.
    g_laser_trace_a = get_weapon_muzzle_pos();
    g_laser_trace_b = vec3_add(ray_o, vec3_scale(ray_d, best_t));
    g_laser_trace_timer = kLaserTraceDuration;
    play_laser_fire_sound();

    if (best_idx < 0 && !hit_terrain) return;

    // Efeito de impacto + marca de queimado no chao - em terreno OU criatura, em qualquer
    // acerto, nao so' quando mata (pedido do jogador: "efeito de atingir o alvo... marcas
    // de queimado", depois "efeito da colisao nem sequer existe").
    play_laser_impact_sound();
    Vec3 hit_pos = g_laser_trace_b;
    Vec3 ground_pos = hit_pos;
    if (best_idx >= 0) {
        const Creature& c = g_creatures[(size_t)best_idx];
        ground_pos = {c.x, c.y, c.z};
    }
    g_impact_flashes.push_back({hit_pos, ground_pos, kImpactFlashDuration});
    g_scorch_marks.push_back({ground_pos, kScorchMarkDuration});

    if (best_idx < 0) return;

    Creature& hit = g_creatures[(size_t)best_idx];
    hit.hp -= kLaserDamage;
    if (hit.hp > 0) return;

    float dx = hit.x, dz = hit.z, dy = hit.y;
    g_creatures.erase(g_creatures.begin() + best_idx);
    if (g_world) {
        spawn_item_drop(Block::Organic, dx, dz, dy + 0.3f);
        spawn_block_particles(Block::Organic, dx, dz, g_world->h);
    }
    add_alert("Criatura alienigena abatida!", 0.3f, 1.0f, 0.5f);
}

// Ver comentario da declaracao em creatures.h.
float laser_cooldown_fraction() {
    return clamp01(g_fire_cooldown / kFireCooldownSeconds);
}
