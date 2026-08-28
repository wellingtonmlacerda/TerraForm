#include "lighting.h"

#include "math_core.h"          // Vec2, kPi, clamp01
#include "noise.h"              // lerp
#include "blocks.h"             // Block, is_solid
#include "world.h"              // World, g_world, in_bounds/get, surface_height_at
#include "player_physics.h"     // Player, g_player, get_player_render_pos/get_player_render_y
#include "modules_building.h"   // Module, ModuleStatus, g_modules
#include "game_state.h"         // kDayLength
#include "base_interior.h"       // kBaseLamps/base_interior_ambient (luminarias + ambiente interno)
#include "base_exterior.h"       // kBaseFloods (holofotes externos da base)
#include "interiors.h"           // interior_at/interior_district_center (luminarias por ambiente)
#include "sky.h"                 // sky_moonlight/sky_moonlight_azimuth (a Lua ilumina o chao)

#include <algorithm>
#include <cmath>
#include <vector>

// Globais de estado de jogo ainda definidas em main.cpp (dono continua sendo main.cpp - nao
// fazem parte desta etapa de extracao). Ja eram nao-static em main.cpp (extraidas/expostas em
// fases anteriores); so precisamos da declaracao aqui tambem, mesmo padrao de g_physics_cfg
// em camera.cpp / g_terrain_cfg em world.cpp.
extern float g_day_time;
extern float g_atmosphere;
extern int g_base_x;
extern int g_base_y;

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
    // NOTA: usa a mesma curva de daylight que o resto do jogo (compute_daylight, math_core.cpp)
    // em vez de reaproveitar a curva antiga (sin(day_phase*pi), so positiva o ciclo INTEIRO) -
    // as duas discordavam e o terreno clareava em horario errado.
    float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
    float daylight = compute_daylight(day_phase);

    // Luz ambiente: interpola entre minimo (noite) e maximo (dia)
    float ambient = lerp(g_lighting.ambient_min, g_lighting.ambient_max, daylight);

    // ================= AMBIENTE NOTURNO + LUZ DA LUA =================
    // compute_daylight() = max(0, sin(...)) e' EXATAMENTE zero em metade do ciclo. Consequencia
    // medida: durante toda a noite o lerp acima devolvia ambient_min (0.06) constante, e
    // multiplicado pela cor noturna (0.35,0.4,0.65) o lightmap ficava em ~0.02-0.04 - terreno
    // preto, sem relevo, sem gradiente, e a Lua no ceu sem nenhum efeito no chao. Era a causa exata
    // do "mesmo com a Lua visivel o ambiente fica praticamente preto".
    //
    // A correcao nao e' subir ambient_min (isso clarearia o crepusculo tambem, onde a curva do sol
    // ainda manda). E' dar a noite uma CURVA PROPRIA e combinar as duas pelo maximo:
    //   - night_ambient: o piso de uma noite fechada, sem lua. A noite continua noite.
    //   - moon_light * sky_moonlight(): a contribuicao da Lua, tirada da MESMA orbita que sky.cpp
    //     usa pra desenha-la (sky_moon_state) - luz e imagem nunca divergem.
    //   - x night_alpha: some suavemente no amanhecer, quando a curva do sol assume.
    // max(), nao soma: ao meio-dia o termo noturno nao pode somar em cima do sol e estourar.
    float night = compute_night_alpha(day_phase);
    float moonlit = g_lighting.night_ambient + g_lighting.moon_light * sky_moonlight();
    ambient = std::max(ambient, moonlit * night);

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
        // NOITE: azul-roxo frio, clareado pela LUA na medida em que ela esta alta.
        // A cor fixa antiga (0.35,0.4,0.65) tem luminancia 0.44 e, multiplicada por um ambiente de
        // 0.06, dava ~0.026 no lightmap - preto. Nao bastava subir o ambiente: a COR tambem
        // precisava ser luz de lua (prata frio) em vez de penumbra roxa, senao o terreno ganha
        // brilho mas continua com aparencia de borrao azul sem leitura de material.
        // Fica FRIA nos dois extremos - o contraste com o branco-quente do meio-dia e o que
        // mantem "a noite ainda parece noite".
        float m = sky_moonlight();
        r = lerp(0.30f, 0.66f, m);
        g = lerp(0.36f, 0.72f, m);
        b = lerp(0.58f, 0.90f, m);
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

    // ================= LUZ DIRECIONAL DA LUA =================
    // O pipeline deste motor NAO tem luz direcional: a luz natural e um termo AMBIENTE global
    // (compute_ambient_light) e so as Light2D pontuais tem posicao, atenuacao e sombra
    // (compute_shadow). Uma luz direcional de verdade exigiria normal por pixel no lightmap, que
    // nao existe. Entao a Lua entra pelo caminho que o motor JA tem: uma fonte pontual grande,
    // deslocada na direcao real do azimute lunar (sky_moonlight_azimuth, a mesma orbita que o ceu
    // desenha), a ~26 tiles do jogador.
    //
    // O resultado e o efeito pedido sem sistema novo: o lado do terreno virado pra Lua fica mais
    // claro, o lado oposto fica em penumbra, e compute_shadow projeta sombra suave de relevo e
    // estrutura na direcao certa. Custo: ~radius^2*pi pixels com um traco de <= shadow_samples (8)
    // passos cada - da ordem de 30k iteracoes, contra os 9216 pixels do lightmap ja varridos pelo
    // ambiente. E paga so de noite (o gate de intensidade abaixo).
    //
    // Nao ilumina interiores atravessando parede: compute_shadow raymarcha o mundo, entao a parede
    // de bloco da base barra o traco - e, mais decisivo, base_interior_ambient() ja define o
    // ambiente de dentro como ABSOLUTO e o lightmap e clampado em 1.0, entao la nao ha o que
    // clarear.
    {
        float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
        float night = compute_night_alpha(day_phase);
        float moon = sky_moonlight();
        float strength = g_lighting.moon_directional * moon * night;
        if (strength > 0.01f) {
            float az = sky_moonlight_azimuth();
            Light2D moon_light;
            moon_light.x = rpos.x + std::cos(az) * 26.0f;
            moon_light.y = rpos.y + std::sin(az) * 26.0f;
            moon_light.height = rpy + 40.0f;   // alta: a sombra sai longa e rasante, como luar
            moon_light.radius = 46.0f;
            moon_light.intensity = strength;
            moon_light.r = 0.62f;
            moon_light.g = 0.70f;
            moon_light.b = 0.92f;              // prata frio, nao branco
            moon_light.falloff = 1.15f;        // quase linear: e' um corpo distante, nao uma lampada
            moon_light.flicker = false;        // luar nao tremula
            moon_light.is_emissive = false;
            g_lights.push_back(moon_light);
        }
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

    // Luminarias das salas do distrito de interiores (ver base_interior.h - a MESMA tabela que
    // desenha as luminarias). Elas sao o que faz o interior parecer habitado: o ambiente e' um termo
    // global sem nocao de "dentro", entao sem fonte propria a sala nao tem nenhuma luz que seja dela.
    // Filtradas pela sala em que o jogador ESTA - as outras estao a celulas de distancia, nao cairiam
    // na janela do lightmap e so' gastariam slots do teto de 32 luzes.
    {
        int inside = interior_at(rpos.x, rpos.y);
        if (inside >= 0) {
            int icx, icz;
            interior_district_center(icx, icz);
            for (int i = 0; i < kBaseLampCount; ++i) {
                const BaseLamp& lp = kBaseLamps[i];
                if (lp.interior != inside) continue;
                Light2D l = {};
                l.x = (float)icx + lp.dx;
                l.y = (float)icz + lp.dz;
                l.height = lp.y;   // guardado, nunca usado (o lightmap e' 2D)
                l.radius = lp.radius;
                l.intensity = lp.intensity;
                l.r = lp.r; l.g = lp.g; l.b = lp.b;
                l.falloff = 2.0f;
                l.flicker = false;  // luz de base e' estavel; flicker leria como defeito
                l.flicker_speed = 0.0f;
                l.is_emissive = true;
                g_lights.push_back(l);
            }
        }
    }


    // Holofotes EXTERNOS da base (ver base_exterior.h). Sem eles, com a auto-iluminacao do modelo, a
    // instalacao ficava acesa sobre um chao PRETO de noite - o que le pior que tudo escuro. Poucos e
    // de raio grande: o objetivo e' o SOLO em volta ter luz. Cortados por distancia porque alem disso
    // nem caem na janela do lightmap (centrada no jogador) e so' gastariam slots do teto de 32 luzes.
    {
        float bdx2 = rpos.x - (float)g_base_x, bdz2 = rpos.y - (float)g_base_y;
        if (bdx2 * bdx2 + bdz2 * bdz2 < 110.0f * 110.0f) {
            for (int i = 0; i < kBaseFloodCount; ++i) {
                const BaseFlood& fl = kBaseFloods[i];
                Light2D l = {};
                l.x = (float)g_base_x + fl.dx;
                l.y = (float)g_base_y + fl.dz;
                l.height = fl.y;
                l.radius = fl.radius;
                l.intensity = fl.intensity;
                l.r = fl.r; l.g = fl.g; l.b = fl.b;
                l.falloff = 2.0f;
                l.flicker = false;
                l.flicker_speed = 0.0f;
                l.is_emissive = true;
                g_lights.push_back(l);
            }
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
            // is_furniture_collider: os blocos invisiveis de colisao de mobilia nao podem projetar
            // sombra - seria uma sombra sem corpo visivel no chao ao lado dos moveis.
            if (is_solid(obj) && obj != Block::Water && !is_furniture_collider(obj)) {
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

    // Inicializar lightmap com luz ambiente. Antes era um preenchimento plano (o mesmo valor em
    // todos os pixels); agora cada pixel passa pelo multiplicador de "dentro"
    // (base_interior_ambient_mul), a UNICA nocao de ambiente fechado do pipeline - ver
    // g_lighting.indoor_ambient_mul. Com indoor_ambient_mul = 1.0 a funcao retorna 1.0 no caminho
    // rapido e o resultado e' bit-a-bit o mesmo de antes. g_lightmap_center_x/z ja foram
    // atualizados acima, entao a conversao pixel->mundo aqui e' valida.
    for (int lz = 0; lz < kLightmapSize; ++lz) {
        float wz = (float)(lz - kLightmapSize / 2 + g_lightmap_center_z);
        for (int lx = 0; lx < kLightmapSize; ++lx) {
            float wx = (float)(lx - kLightmapSize / 2 + g_lightmap_center_x);
            int i = lz * kLightmapSize + lx;
            // Dentro do complexo de interiores o ambiente e' ABSOLUTO (iluminacao artificial 24h),
            // nao o ambiente de dia/noite multiplicado - ver a nota longa em base_interior.h. Com o
            // multiplicador antigo, de noite o interior caia pra 0.027 e renderizava preto.
            float ir, ig, ib;
            if (base_interior_ambient(wx, wz, ir, ig, ib)) {
                g_lightmap_r[i] = ir;
                g_lightmap_g[i] = ig;
                g_lightmap_b[i] = ib;
                continue;
            }
            float amb = ambient;
            g_lightmap_r[i] = amb * nat_r;
            g_lightmap_g[i] = amb * nat_g;
            g_lightmap_b[i] = amb * nat_b;
        }
    }

    // Coletar luzes
    collect_lights();
    // (o clamp do lightmap fica depois de somar as luzes - ver abaixo)

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

    // CLAMP em 1.0. add_light_to_lightmap SOMA sem limite, e no interior (ambiente absoluto de 0.38
    // + 8 luminarias) o lightmap passava facil de 1.4 - qualquer superficie clara (o painel de parede
    // estava em 0.88) era multiplicada acima de 1 e saia BRANCO PURO, perdendo todo o sombreamento.
    // Era a causa do interior "lavado de branco" nos screenshots. Fora do interior isto nao muda
    // praticamente nada: ao meio-dia o ambiente ja beira 1.0 e a cor final ja era clampada depois,
    // em apply_color_grading - a diferenca e' que agora o CONTRASTE sobrevive em vez de estourar.
    for (int i = 0; i < kLightmapPixels; ++i) {
        g_lightmap_r[i] = std::min(1.0f, g_lightmap_r[i]);
        g_lightmap_g[i] = std::min(1.0f, g_lightmap_g[i]);
        g_lightmap_b[i] = std::min(1.0f, g_lightmap_b[i]);
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

