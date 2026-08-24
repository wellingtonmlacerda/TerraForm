#include "lighting.h"

#include "math_core.h"          // Vec2, kPi, clamp01
#include "noise.h"              // lerp
#include "blocks.h"             // Block, is_solid
#include "world.h"              // World, g_world, in_bounds/get, surface_height_at
#include "player_physics.h"     // Player, g_player, get_player_render_pos/get_player_render_y
#include "modules_building.h"   // Module, ModuleStatus, g_modules
#include "game_state.h"         // kDayLength

#include <algorithm>
#include <cmath>
#include <vector>

// Globais de estado de jogo ainda definidas em main.cpp (dono continua sendo main.cpp - nao
// fazem parte desta etapa de extracao). Ja eram nao-static em main.cpp (extraidas/expostas em
// fases anteriores); so precisamos da declaracao aqui tambem, mesmo padrao de g_physics_cfg
// em camera.cpp / g_terrain_cfg em world.cpp.
extern float g_day_time;
extern float g_atmosphere;

// kDayLength agora vem de game_state.h (era uma copia local aqui).

// Vetor de luzes ativas no frame. Extern-declared in lighting.h (render_world's F3 debug
// overlay reads it directly); defined (non-static) here.
std::vector<Light2D> g_lights;

// Lightmap - grade 2D para iluminacao acumulada. kLightmapSize comes from lighting.h (shared
// with render_world's debug overlay); kLightmapPixels is only ever used in this file, so it
// stays a file-local derived constant.
static constexpr int kLightmapPixels = kLightmapSize * kLightmapSize;
std::vector<float> g_lightmap_r(kLightmapPixels, 1.0f);
std::vector<float> g_lightmap_g(kLightmapPixels, 1.0f);
std::vector<float> g_lightmap_b(kLightmapPixels, 1.0f);

// Bloom buffer - para efeito de glow. Only touched inside this file (extract_bloom/
// blur_bloom/compute_lightmap), so stays file-local static.
static std::vector<float> g_bloom_r(kLightmapPixels, 0.0f);
static std::vector<float> g_bloom_g(kLightmapPixels, 0.0f);
static std::vector<float> g_bloom_b(kLightmapPixels, 0.0f);

// Buffer temporario para blur. Same as above - file-local static.
static std::vector<float> g_temp_r(kLightmapPixels, 0.0f);
static std::vector<float> g_temp_g(kLightmapPixels, 0.0f);
static std::vector<float> g_temp_b(kLightmapPixels, 0.0f);

// Centro do lightmap no mundo (para mapeamento de coordenadas). Only touched inside this
// file (world_to_lightmap_index/compute_lightmap), so stays file-local static.
static int g_lightmap_center_x = 0;
static int g_lightmap_center_z = 0;

// Configuracoes de iluminacao. Extern-declared in lighting.h (render_world/update_game read
// and write its fields directly); defined (non-static) here.
LightingSettings g_lighting;

// Debug. g_debug_lightmap/g_debug_lights are extern-declared in lighting.h (render_world's
// overlays + update_game's F3 toggle/settings menu read and write them); defined
// (non-static) here. g_debug_bloom is grep-confirmed dead code (never read anywhere in the
// codebase, before or after this refactor) - kept as a file-local static, preserved as-is.
bool g_debug_lightmap = false;
static bool g_debug_bloom = false;
bool g_debug_lights = false;

// ============= SISTEMA DE ILUMINACAO 2D - FUNCOES =============

// Smoothstep para transicoes suaves
static float smoothstep(float edge0, float edge1, float x) {
    float t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

// Atenuacao de luz baseada na distancia
static float light_attenuation(float dist, float radius, float falloff) {
    if (dist >= radius) return 0.0f;
    float t = dist / radius;
    if (falloff <= 1.0f) return 1.0f - t;                    // Linear
    if (falloff <= 2.0f) return 1.0f - t * t;                // Quadratica
    return std::pow(1.0f - t, falloff);                      // Custom
}

// Obter luz para um tipo de modulo
static Light2D get_module_light(const Module& mod) {
    Light2D light = {};
    light.x = (float)mod.x + 0.5f;
    light.y = (float)mod.y + 0.5f;
    light.height = 1.5f;
    light.falloff = 2.0f;
    light.flicker = false;
    light.flicker_speed = 0.0f;
    light.is_emissive = true;

    switch (mod.type) {
        case Block::EnergyGenerator:
            light.r = 1.0f; light.g = 0.75f; light.b = 0.15f;
            light.radius = 12.0f;
            light.intensity = 0.95f;
            light.flicker = true;
            light.flicker_speed = 6.0f;
            break;
        case Block::SolarPanel:
            light.r = 0.3f; light.g = 0.5f; light.b = 0.9f;
            light.radius = 5.0f;
            light.intensity = 0.35f;
            break;
        case Block::OxygenGenerator:
            light.r = 0.2f; light.g = 0.95f; light.b = 0.4f;
            light.radius = 7.0f;
            light.intensity = 0.55f;
            light.flicker = true;
            light.flicker_speed = 3.0f;
            break;
        case Block::TerraformerBeacon:
            light.r = 0.85f; light.g = 0.25f; light.b = 0.95f;
            light.radius = 15.0f;
            light.intensity = 0.9f;
            light.flicker = true;
            light.flicker_speed = 2.0f;
            break;
        case Block::Greenhouse:
            light.r = 0.45f; light.g = 0.95f; light.b = 0.35f;
            light.radius = 6.0f;
            light.intensity = 0.45f;
            break;
        case Block::CO2Factory:
            light.r = 0.9f; light.g = 0.5f; light.b = 0.2f;
            light.radius = 8.0f;
            light.intensity = 0.6f;
            light.flicker = true;
            light.flicker_speed = 4.0f;
            break;
        case Block::Habitat:
            light.r = 1.0f; light.g = 0.92f; light.b = 0.7f;
            light.radius = 10.0f;
            light.intensity = 0.75f;
            break;
        case Block::Workshop:
            light.r = 0.9f; light.g = 0.85f; light.b = 0.6f;
            light.radius = 8.0f;
            light.intensity = 0.65f;
            light.flicker = true;
            light.flicker_speed = 8.0f;
            break;
        case Block::WaterExtractor:
            light.r = 0.3f; light.g = 0.7f; light.b = 1.0f;
            light.radius = 5.0f;
            light.intensity = 0.4f;
            break;
        default:
            light.intensity = 0.0f;
            break;
    }

    return light;
}

// Calcular luz ambiente baseada no ciclo dia/noite
static float compute_ambient_light() {
    // Usa compute_daylight() (math_core.cpp) - a MESMA formula que posiciona o sol de
    // verdade em render_alien_sky() (sky.cpp). Antes esse arquivo reimplementava sua
    // propria curva (sin(day_phase*pi), so positiva o ciclo INTEIRO) em vez de reaproveitar
    // a de verdade (sin(day_phase*2*pi - pi/2), zero por metade do ciclo) - as duas
    // discordavam sobre quando e "dia" (bug real, mesmo nao sendo a causa principal das
    // manchas escuras reportadas - essas vem do AO por profundidade/inclinacao logo
    // abaixo, independente de hora do dia de proposito).
    float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
    float daylight = compute_daylight(day_phase);

    // Interpolar entre luz minima (noite) e maxima (dia)
    float ambient = lerp(g_lighting.ambient_min, g_lighting.ambient_max, daylight);

    // Terraformacao aumenta luz ambiente levemente
    ambient += clamp01(g_atmosphere / 100.0f) * 0.08f;

    return clamp01(ambient);
}

// Obter cor da luz natural baseada no ciclo dia/noite
static void get_natural_light_color(float& r, float& g, float& b) {
    // Mesma curva de compute_ambient_light() acima (compute_daylight(), math_core.cpp) -
    // consistente com o sol de verdade do ceu (sky.cpp).
    float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
    float daylight = compute_daylight(day_phase);

    if (daylight > 0.7f) {
        // Meio-dia: branco/amarelo quente
        r = 1.0f; g = 0.97f; b = 0.88f;
    } else if (daylight > 0.4f) {
        // Transicao: laranja dourado
        float t = (daylight - 0.4f) / 0.3f;
        r = lerp(1.0f, 1.0f, t);
        g = lerp(0.65f, 0.97f, t);
        b = lerp(0.35f, 0.88f, t);
    } else if (daylight > 0.15f) {
        // Amanhecer/entardecer: laranja/rosa
        float t = (daylight - 0.15f) / 0.25f;
        r = lerp(0.85f, 1.0f, t);
        g = lerp(0.45f, 0.65f, t);
        b = lerp(0.55f, 0.35f, t);
    } else {
        // Noite: azul/roxo frio
        r = 0.35f; g = 0.4f; b = 0.65f;
    }
}

// Coletar todas as fontes de luz no mundo
static void collect_lights() {
    g_lights.clear();
    Vec2 rpos = get_player_render_pos();
    float rpy = get_player_render_y();

    // Luz do jogador (lanterna no capacete)
    {
        Light2D player_light;
        player_light.x = rpos.x;
        player_light.y = rpos.y;
        player_light.height = rpy + 1.6f;
        player_light.radius = 10.0f;
        player_light.intensity = 0.7f;
        player_light.r = 1.0f;
        player_light.g = 0.95f;
        player_light.b = 0.85f;
        player_light.falloff = 2.0f;
        player_light.flicker = true;
        player_light.flicker_speed = 12.0f;
        player_light.is_emissive = false;
        g_lights.push_back(player_light);
    }

    // Luz do jetpack se ativo
    if (g_player.jetpack_active && g_player.jetpack_fuel > 0.0f) {
        Light2D jet_light;
        jet_light.x = rpos.x;
        jet_light.y = rpos.y;
        jet_light.height = rpy + 0.3f;
        jet_light.radius = 6.0f;
        jet_light.intensity = 0.85f;
        jet_light.r = 1.0f;
        jet_light.g = 0.6f;
        jet_light.b = 0.15f;
        jet_light.falloff = 1.5f;
        jet_light.flicker = true;
        jet_light.flicker_speed = 20.0f;
        jet_light.is_emissive = true;
        g_lights.push_back(jet_light);
    }

    // Luzes dos modulos ativos
    for (const auto& mod : g_modules) {
        if (mod.status != ModuleStatus::Active) continue;
        Light2D light = get_module_light(mod);
        if (light.intensity > 0.0f) {
            g_lights.push_back(light);
        }
    }

    // Luzes de recursos emissivos (cristais)
    if (g_world) {
        int px = (int)g_player.pos.x;
        int pz = (int)g_player.pos.y;
        int check_radius = 20;

        for (int dz = -check_radius; dz <= check_radius; dz += 2) {
            for (int dx = -check_radius; dx <= check_radius; dx += 2) {
                int tx = px + dx;
                int tz = pz + dz;
                if (!g_world->in_bounds(tx, tz)) continue;

                Block obj = g_world->get(tx, tz);
                if (obj == Block::Crystal) {
                    Light2D crystal_light;
                    crystal_light.x = (float)tx + 0.5f;
                    crystal_light.y = (float)tz + 0.5f;
                    crystal_light.height = surface_height_at(*g_world, tx, tz) + 0.5f;
                    crystal_light.radius = 4.0f;
                    crystal_light.intensity = 0.5f;
                    crystal_light.r = 0.7f;
                    crystal_light.g = 0.9f;
                    crystal_light.b = 1.0f;
                    crystal_light.falloff = 2.0f;
                    crystal_light.flicker = true;
                    crystal_light.flicker_speed = 5.0f;
                    crystal_light.is_emissive = true;
                    g_lights.push_back(crystal_light);
                    continue;
                }
                // LAVA como fonte de luz de verdade: nao bastava a lava se desenhar brilhante,
                // ela tem que ILUMINAR o terreno em volta (pedido do jogador: "no escuro ela
                // deveria ser luminosa, pois e' lava" - a noite um campo de lava ficava um
                // borrao escuro cercado de terreno preto). Passo de 4 tiles (nao 2 como o
                // cristal): campos de lava sao grandes e contiguos, sem isso um unico lago
                // estouraria sozinho o teto de 32 luzes e apagaria todas as outras.
                if ((dx % 4) == 0 && (dz % 4) == 0 && g_world->get_ground(tx, tz) == Block::Lava) {
                    Light2D lava_light;
                    lava_light.x = (float)tx + 0.5f;
                    lava_light.y = (float)tz + 0.5f;
                    lava_light.height = surface_height_at(*g_world, tx, tz) + 0.35f;
                    lava_light.radius = 9.0f;
                    lava_light.intensity = 0.95f;
                    lava_light.r = 1.0f;
                    lava_light.g = 0.45f;
                    lava_light.b = 0.12f;
                    lava_light.falloff = 1.6f;
                    lava_light.flicker = true;       // luz de fogo nunca e' estavel
                    lava_light.flicker_speed = 2.4f; // mais lento que cristal - massa pesada
                    lava_light.is_emissive = true;
                    g_lights.push_back(lava_light);
                }
            }
        }
    }
}

// Calcular sombra por raymarching 2D
static float compute_shadow(float lx, float ly, float px, float py) {
    if (!g_world || !g_lighting.shadows_enabled) return 1.0f;

    float dx = px - lx;
    float dy = py - ly;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.5f) return 1.0f;  // Muito perto, sem sombra

    int steps = std::min(g_lighting.shadow_samples, (int)(dist * 2.0f));
    if (steps < 2) return 1.0f;

    float shadow = 1.0f;
    float inv_steps = 1.0f / (float)steps;

    for (int i = 1; i < steps; ++i) {
        float t = (float)i * inv_steps;
        int tx = (int)(lx + dx * t);
        int ty = (int)(ly + dy * t);

        if (g_world->in_bounds(tx, ty)) {
            Block obj = g_world->get(tx, ty);
            if (is_solid(obj) && obj != Block::Water) {
                // Sombra parcial - blocos nao bloqueiam totalmente
                shadow *= g_lighting.shadow_softness;
                if (shadow < 0.1f) break;
            }
        }
    }

    return shadow;
}

// Converter coordenadas do mundo para indice do lightmap
static int world_to_lightmap_index(float world_x, float world_z) {
    int lx = (int)(world_x - g_lightmap_center_x + kLightmapSize / 2);
    int lz = (int)(world_z - g_lightmap_center_z + kLightmapSize / 2);

    if (lx < 0 || lx >= kLightmapSize || lz < 0 || lz >= kLightmapSize) {
        return -1;
    }

    return lz * kLightmapSize + lx;
}

// Adicionar contribuicao de uma luz ao lightmap
static void add_light_to_lightmap(const Light2D& light) {
    float light_world_x = light.x;
    float light_world_z = light.y;

    // Aplicar flicker
    float flicker_mult = 1.0f;
    if (light.flicker) {
        float flicker = std::sin(g_day_time * light.flicker_speed) * 0.5f + 0.5f;
        flicker_mult = 0.85f + flicker * 0.15f;
    }

    float intensity = light.intensity * flicker_mult;
    int radius_int = (int)std::ceil(light.radius);

    // Iterar sobre a area de influencia da luz
    for (int dz = -radius_int; dz <= radius_int; ++dz) {
        for (int dx = -radius_int; dx <= radius_int; ++dx) {
            float px = light_world_x + (float)dx;
            float pz = light_world_z + (float)dz;

            // Distancia ao centro da luz
            float dist = std::sqrt((float)(dx * dx + dz * dz));
            if (dist > light.radius) continue;

            // Atenuacao
            float atten = light_attenuation(dist, light.radius, light.falloff);
            if (atten < 0.01f) continue;

            // Sombra
            float shadow = compute_shadow(light_world_x, light_world_z, px, pz);

            // Contribuicao final
            float contrib = intensity * atten * shadow;

            // Adicionar ao lightmap
            int idx = world_to_lightmap_index(px, pz);
            if (idx >= 0 && idx < kLightmapPixels) {
                g_lightmap_r[idx] += light.r * contrib;
                g_lightmap_g[idx] += light.g * contrib;
                g_lightmap_b[idx] += light.b * contrib;
            }
        }
    }
}

// Aplicar blur gaussiano 3x3 ao lightmap (para suavizar sombras)
static void blur_lightmap_pass(std::vector<float>& src, std::vector<float>& dst) {
    const float k0 = 0.0625f;  // 1/16
    const float k1 = 0.125f;   // 2/16
    const float k2 = 0.25f;    // 4/16

    for (int z = 1; z < kLightmapSize - 1; ++z) {
        for (int x = 1; x < kLightmapSize - 1; ++x) {
            int idx = z * kLightmapSize + x;

            float sum = 0.0f;
            sum += src[idx - kLightmapSize - 1] * k0;
            sum += src[idx - kLightmapSize] * k1;
            sum += src[idx - kLightmapSize + 1] * k0;
            sum += src[idx - 1] * k1;
            sum += src[idx] * k2;
            sum += src[idx + 1] * k1;
            sum += src[idx + kLightmapSize - 1] * k0;
            sum += src[idx + kLightmapSize] * k1;
            sum += src[idx + kLightmapSize + 1] * k0;

            dst[idx] = sum;
        }
    }
}

static void blur_lightmap() {
    // Blur horizontal + vertical (separavel)
    blur_lightmap_pass(g_lightmap_r, g_temp_r);
    blur_lightmap_pass(g_lightmap_g, g_temp_g);
    blur_lightmap_pass(g_lightmap_b, g_temp_b);

    // Copiar de volta
    std::copy(g_temp_r.begin(), g_temp_r.end(), g_lightmap_r.begin());
    std::copy(g_temp_g.begin(), g_temp_g.end(), g_lightmap_g.begin());
    std::copy(g_temp_b.begin(), g_temp_b.end(), g_lightmap_b.begin());
}

// Extrair brilho para bloom
static void extract_bloom() {
    float threshold = g_lighting.bloom_threshold;

    for (int i = 0; i < kLightmapPixels; ++i) {
        float brightness = (g_lightmap_r[i] + g_lightmap_g[i] + g_lightmap_b[i]) / 3.0f;

        if (brightness > threshold) {
            float excess = (brightness - threshold) / (1.0f - threshold + 0.001f);
            excess = std::min(excess, 2.0f);

            g_bloom_r[i] = g_lightmap_r[i] * excess;
            g_bloom_g[i] = g_lightmap_g[i] * excess;
            g_bloom_b[i] = g_lightmap_b[i] * excess;
        } else {
            g_bloom_r[i] = 0.0f;
            g_bloom_g[i] = 0.0f;
            g_bloom_b[i] = 0.0f;
        }
    }
}

// Blur maior para bloom (5x5 aproximado com 2 passadas de 3x3)
static void blur_bloom() {
    // Primeira passada
    blur_lightmap_pass(g_bloom_r, g_temp_r);
    blur_lightmap_pass(g_bloom_g, g_temp_g);
    blur_lightmap_pass(g_bloom_b, g_temp_b);

    std::copy(g_temp_r.begin(), g_temp_r.end(), g_bloom_r.begin());
    std::copy(g_temp_g.begin(), g_temp_g.end(), g_bloom_g.begin());
    std::copy(g_temp_b.begin(), g_temp_b.end(), g_bloom_b.begin());

    // Segunda passada
    blur_lightmap_pass(g_bloom_r, g_temp_r);
    blur_lightmap_pass(g_bloom_g, g_temp_g);
    blur_lightmap_pass(g_bloom_b, g_temp_b);

    std::copy(g_temp_r.begin(), g_temp_r.end(), g_bloom_r.begin());
    std::copy(g_temp_g.begin(), g_temp_g.end(), g_bloom_g.begin());
    std::copy(g_temp_b.begin(), g_temp_b.end(), g_bloom_b.begin());
}

// Computar lightmap completo
void compute_lightmap() {
    if (!g_lighting.enabled) return;

    // Atualizar centro do lightmap
    Vec2 rpos = get_player_render_pos();
    g_lightmap_center_x = (int)rpos.x;
    g_lightmap_center_z = (int)rpos.y;

    // Obter cor da luz natural
    float nat_r, nat_g, nat_b;
    get_natural_light_color(nat_r, nat_g, nat_b);

    // Luz ambiente baseada no ciclo dia/noite
    float ambient = compute_ambient_light();

    // Inicializar lightmap com luz ambiente
    for (int i = 0; i < kLightmapPixels; ++i) {
        g_lightmap_r[i] = ambient * nat_r;
        g_lightmap_g[i] = ambient * nat_g;
        g_lightmap_b[i] = ambient * nat_b;
    }

    // Coletar luzes
    collect_lights();

    // Limitar numero de luzes para performance (prioriza mais proximas ao jogador)
    const int kMaxLights = 32;
    if (g_lights.size() > kMaxLights) {
        // Ordenar por distancia ao jogador
        std::sort(g_lights.begin(), g_lights.end(), [rpos](const Light2D& a, const Light2D& b) {
            float da = (a.x - rpos.x) * (a.x - rpos.x) +
                       (a.y - rpos.y) * (a.y - rpos.y);
            float db = (b.x - rpos.x) * (b.x - rpos.x) +
                       (b.y - rpos.y) * (b.y - rpos.y);
            return da < db;
        });
        g_lights.resize(kMaxLights);
    }

    // Adicionar contribuicao de cada luz
    for (const auto& light : g_lights) {
        add_light_to_lightmap(light);
    }

    // Blur para suavizar sombras
    if (g_lighting.shadows_enabled) {
        blur_lightmap();
    }

    // Extrair e processar bloom
    if (g_lighting.bloom_enabled) {
        extract_bloom();
        blur_bloom();

        // Adicionar bloom ao lightmap
        float bloom_int = g_lighting.bloom_intensity;
        for (int i = 0; i < kLightmapPixels; ++i) {
            g_lightmap_r[i] += g_bloom_r[i] * bloom_int;
            g_lightmap_g[i] += g_bloom_g[i] * bloom_int;
            g_lightmap_b[i] += g_bloom_b[i] * bloom_int;
        }
    }
}

// Amostrar iluminacao do lightmap para uma posicao do mundo
void sample_lightmap(float world_x, float world_z, float& r, float& g, float& b) {
    if (!g_lighting.enabled) {
        r = g = b = 1.0f;
        return;
    }

    int idx = world_to_lightmap_index(world_x, world_z);

    if (idx >= 0 && idx < kLightmapPixels) {
        r = g_lightmap_r[idx];
        g = g_lightmap_g[idx];
        b = g_lightmap_b[idx];
    } else {
        // Fora do lightmap - usar luz ambiente
        float ambient = compute_ambient_light();
        float nat_r, nat_g, nat_b;
        get_natural_light_color(nat_r, nat_g, nat_b);
        r = ambient * nat_r;
        g = ambient * nat_g;
        b = ambient * nat_b;
    }

    // Clamp para evitar valores negativos ou muito altos
    r = std::clamp(r, 0.0f, 2.5f);
    g = std::clamp(g, 0.0f, 2.5f);
    b = std::clamp(b, 0.0f, 2.5f);
}

// Aplicar escurecimento por profundidade (para cavernas/areas baixas)
float compute_depth_factor(float tile_height, float player_height) {
    if (!g_lighting.enabled) return 1.0f;

    float depth_diff = player_height - tile_height;
    if (depth_diff <= 0.0f) return 1.0f;

    // A forca desse escurecimento (AO por profundidade) era fixa - disparava igual em
    // pleno meio-dia ou de noite, criando manchas escuras acompanhando o relevo mesmo com
    // o sol a pino (bug reportado: "a noite esta chegando mesmo com o sol no ceu" - nao
    // era noite de verdade, era esse AO sem nenhuma relacao com hora do dia). Em dia cheio
    // a forca cai bastante (35% do normal); de noite continua no maximo, ja que tudo esta
    // escuro mesmo e nao ha "manchas" pra destacar.
    float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
    float daylight = compute_daylight(day_phase);
    float darkening_strength = g_lighting.depth_darkening * lerp(1.0f, 0.35f, daylight);

    // Escurecer areas mais baixas que o jogador
    float factor = 1.0f - clamp01(depth_diff / 8.0f) * darkening_strength;
    return std::max(0.2f, factor);
}

// Color grading e pos-processamento
void apply_color_grading(float& r, float& g, float& b) {
    if (!g_lighting.color_grading) return;

    // Contraste
    r = (r - 0.5f) * g_lighting.contrast + 0.5f;
    g = (g - 0.5f) * g_lighting.contrast + 0.5f;
    b = (b - 0.5f) * g_lighting.contrast + 0.5f;

    // Exposure
    r *= g_lighting.exposure;
    g *= g_lighting.exposure;
    b *= g_lighting.exposure;

    // Saturacao
    float gray = r * 0.299f + g * 0.587f + b * 0.114f;
    r = lerp(gray, r, g_lighting.saturation);
    g = lerp(gray, g, g_lighting.saturation);
    b = lerp(gray, b, g_lighting.saturation);

    // Clamp final
    r = clamp01(r);
    g = clamp01(g);
    b = clamp01(b);
}

// Calcular vinheta para uma posicao da tela
static float compute_vignette(float screen_x, float screen_y, float screen_w, float screen_h) {
    if (g_lighting.vignette_intensity <= 0.0f) return 1.0f;

    float cx = screen_w * 0.5f;
    float cy = screen_h * 0.5f;
    float max_dist = std::sqrt(cx * cx + cy * cy);

    float dx = screen_x - cx;
    float dy = screen_y - cy;
    float dist = std::sqrt(dx * dx + dy * dy) / max_dist;

    float vignette = 1.0f - smoothstep(g_lighting.vignette_radius - 0.2f, 1.0f, dist) * g_lighting.vignette_intensity;
    return vignette;
}
