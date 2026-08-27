#include "raylib_platform.h"
#include "minimap.h"

#include "math_core.h"           // kPi, compute_daylight, Vec2
#include "blocks.h"              // Block, is_module, is_base_structure
#include "config_types.h"        // MapConfig, MiniMapRuntime, MapWaypoint
#include "world.h"                // World, g_world
#include "player_physics.h"      // Player, g_player
#include "modules_building.h"   // Module, g_modules, base_annex_contains, kDomeWallRadius
#include "interiors.h"        // interior_at (o distrito nao revela mapa nem gera POI)
#include "game_state.h"          // set_toast
#include "font.h"                // draw_text
#include "render_primitives.h"   // render_quad, render_circle (render_primitives extraction stage)
#include "ui_hud.h"              // hud_right_panel_right_x/bottom_y (ancora o minimapa sem duplicar geometria)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// Globais de estado de jogo ainda definidas em main.cpp (dono continua sendo main.cpp -
// nao fazem parte desta etapa de extracao). g_base_x/g_base_y/g_day_time ja eram
// nao-static em main.cpp (expostas em fases anteriores); so precisamos da declaracao aqui
// tambem, mesmo padrao de g_physics_cfg em camera.cpp / g_terrain_cfg em world.cpp.
extern int g_base_x;
extern int g_base_y;
extern float g_day_time;

// g_map_cfg lost its "static" in main.cpp specifically because the minimap/waypoint
// functions below (moved to this other translation unit) need to read it - same pattern as
// g_shooting_stars losing "static" for save_load.cpp's load_game().
extern MapConfig g_map_cfg;

// g_minimap was already non-static in main.cpp (player_physics.cpp's spawn_player_new_game()
// already reads/writes it via its own extern declaration); this file needs its own extern
// too, same pattern.
extern MiniMapRuntime g_minimap;

// add_alert() fica definida em main.cpp (sistema de alertas, fora do escopo desta etapa) -
// scan_for_points_of_interest() abaixo precisa dela, mesmo padrao de forward declaration ja
// usado em modules_building.cpp.
void add_alert(const std::string& msg, float r, float g, float b, float duration = 3.0f, float cooldown = 5.0f);

// kDayLength agora vem de game_state.h (era uma copia local aqui).

// Cooldown do scanner (tecla T, ver scan_for_points_of_interest() abaixo) - declarado aqui
// (antes de update_fog_of_war) porque o decremento roda la, reaproveitando o unico ponto
// de update incondicional 1x/frame que ja existia, em vez de criar outro.
static float g_scan_cooldown = 0.0f;

// Atualizar fog of war baseado na posicao do jogador. Recebe dt agora (nao recebia antes)
// so pra decrementar o cooldown do scanner (g_scan_cooldown, abaixo) - reaproveita a unica
// chamada incondicional 1x/frame ja existente em vez de criar outro ponto de update.

// ============= DESCOBERTA DE DESTROCO (missao de exploracao) =============
// Latch persistente: true depois que o jogador chegou perto de um sitio de destroco (POI) de verdade.
//
// Por que aqui e nao em objectives.cpp: os POIs NAO existem como lista depois de World::gen() - o
// vetor poi_centers e local do bloco de geracao e morre com ele, e nao ha flag de "visitado" em
// lugar nenhum. Sobram apenas os TILES. Este arquivo ja roda 1x por frame (update_fog_of_war) e ja
// tem g_world, g_player, is_base_structure, base_annex_contains e interior_at - exatamente o
// predicado triplo que scan_for_points_of_interest() usa pra distinguir POI da propria base. Colocar
// a deteccao em objectives.cpp exigiria replicar todos esses externs.
static bool g_poi_found = false;

bool poi_ever_found() { return g_poi_found; }
void poi_discovery_load(bool found) { g_poi_found = found; }
void reset_poi_discovery() { g_poi_found = false; }

namespace {

// Varredura local barata (11x11) em volta do jogador. Roda so' enquanto o latch esta desligado, ou
// seja: para de custar qualquer coisa assim que a missao de exploracao e' cumprida.
void poi_discovery_tick() {
    if (g_poi_found || !g_world) return;

    constexpr int kNearRadius = 5;
    const float base_excl = 2.0f * kDomeWallRadius;   // mesmo raio de exclusao do scanner
    const float base_excl2 = base_excl * base_excl;

    int px = (int)g_player.pos.x;
    int py = (int)g_player.pos.y;
    for (int dy = -kNearRadius; dy <= kNearRadius; ++dy) {
        for (int dx = -kNearRadius; dx <= kNearRadius; ++dx) {
            int x = px + dx, y = py + dy;
            if (!g_world->in_bounds(x, y)) continue;
            Block b = g_world->get(x, y);
            if (!is_base_structure(b)) continue;
            // As 3 exclusoes do scanner: o disco da base, a pegada do exterior (kExterior) e o
            // distrito de interiores. Sem elas, o jogador cumpriria a missao parado na propria base -
            // o piso e a casca da instalacao tambem sao is_base_structure.
            float bx = (float)(x - g_base_x), by = (float)(y - g_base_y);
            if (bx * bx + by * by <= base_excl2) continue;
            if (base_annex_contains(bx, by)) continue;
            if (interior_at((float)x, (float)y) >= 0) continue;
            g_poi_found = true;
            add_alert("Destroco encontrado - registrado no diario da colonia.", 0.55f, 0.85f, 1.0f);
            return;
        }
    }
}

} // namespace

void update_fog_of_war(float dt) {
    if (!g_world) return;

    if (g_scan_cooldown > 0.0f) g_scan_cooldown = std::max(0.0f, g_scan_cooldown - dt);

    // Deteccao de destroco (missao de exploracao) - antes do early-return de interior abaixo por
    // clareza: ela tem a propria exclusao do distrito, nao depende dessa.
    poi_discovery_tick();

    // Dentro de uma sala do distrito de interiores o mapa NAO revela nada: o distrito e' "backstage"
    // (fica a ~1200 tiles da base, ver interiors.h), e revelar aquele canto deixaria uma mancha
    // explorada num lugar onde o jogador nunca esteve de verdade.
    if (interior_at(g_player.pos.x, g_player.pos.y) >= 0) return;

    int px = (int)g_player.pos.x;
    int py = (int)g_player.pos.y;
    int reveal_radius = 15;  // Raio de visao do jogador

    bool changed = false;
    for (int dy = -reveal_radius; dy <= reveal_radius; ++dy) {
        for (int dx = -reveal_radius; dx <= reveal_radius; ++dx) {
            int x = px + dx;
            int y = py + dy;
            if (g_world->in_bounds(x, y)) {
                float dist = std::sqrt((float)(dx * dx + dy * dy));
                if (dist <= (float)reveal_radius) {
                    size_t idx = (size_t)y * (size_t)g_world->w + (size_t)x;
                    if (g_minimap.explored[idx] < 255) {
                        g_minimap.explored[idx] = 255;
                        changed = true;
                    }
                }
            }
        }
    }

    if (changed) {
        g_minimap.dirty_full = true;
    }
}

// Scanner (tecla T, main.cpp) - segue exatamente o precedente de upgrade/reparo/refino
// (tecla + checagem, sem item de inventario novo). Varredura circular avulsa (nao por
// frame, custo desprezivel) ao redor do jogador procurando, em ordem de prioridade: (1)
// qualquer tile is_base_structure() fora de um raio de exclusao da propria base (senao o
// scanner "acha" a propria pista/anel de slots/wreck do foguete) = um POI; (2) na falta de
// POI, o Metal mais proximo (minerio mais raro do jogo - 2 limiares de ruido em AND,
// world.cpp); (3) na falta de Metal, Copper (2o mais raro). Crystal fica de fora de
// proposito - e gerado como campo continuo de bioma, ja facil de achar sozinho, nao um
// veio esparso que precise de ajuda pra localizar. O que for encontrado vira um waypoint
// (add_waypoint ja desenha no minimapa/mapa cheio de graca, zero renderizacao nova).
bool scan_for_points_of_interest() {
    if (!g_world) return false;
    if (g_scan_cooldown > 0.0f) {
        set_toast("Scanner recarregando...", 1.5f);
        return false;
    }

    static constexpr int kScanRadius = 70;
    // 2x kDomeWallRadius (~31) cobre a pista (raio 20), o anel de slots (raio 14) e o
    // wreck do foguete (~25.5 do centro, modules_building.cpp) com folga.
    static constexpr float kScanBaseExclusionRadius = 2.0f * kDomeWallRadius;
    static constexpr float kScanCooldownSeconds = 35.0f;

    g_scan_cooldown = kScanCooldownSeconds;

    int px = (int)g_player.pos.x;
    int py = (int)g_player.pos.y;
    float base_excl2 = kScanBaseExclusionRadius * kScanBaseExclusionRadius;
    float radius2 = (float)(kScanRadius * kScanRadius);

    bool found_poi = false;    int poi_x = 0, poi_y = 0;       float poi_d2 = 0.0f;
    bool found_metal = false;  int metal_x = 0, metal_y = 0;   float metal_d2 = 0.0f;
    bool found_copper = false; int copper_x = 0, copper_y = 0; float copper_d2 = 0.0f;

    for (int dy = -kScanRadius; dy <= kScanRadius; ++dy) {
        for (int dx = -kScanRadius; dx <= kScanRadius; ++dx) {
            float d2 = (float)(dx * dx + dy * dy);
            if (d2 > radius2) continue;
            int x = px + dx, y = py + dy;
            if (!g_world->in_bounds(x, y)) continue;
            Block b = g_world->get(x, y);

            if (is_base_structure(b)) {
                float bx = (float)(x - g_base_x), by = (float)(y - g_base_y);
                // O anexo (corredor/estufa) se estende bem alem do disco de exclusao da base, e
                // suas paredes/piso sao is_base_structure - sem base_annex_contains() o scanner
                // "descobriria" a propria estufa do jogador como ponto de interesse. Um predicado
                // geometrico, nao um raio maior: aumentar o disco mataria o scanner (o raio de
                // varredura inteiro nao e' muito maior que isso).
                if (bx * bx + by * by > base_excl2 && !base_annex_contains(bx, by) &&
                    interior_at((float)x, (float)y) < 0 &&
                    (!found_poi || d2 < poi_d2)) {
                    found_poi = true; poi_x = x; poi_y = y; poi_d2 = d2;
                }
            } else if (b == Block::Metal) {
                if (!found_metal || d2 < metal_d2) { found_metal = true; metal_x = x; metal_y = y; metal_d2 = d2; }
            } else if (b == Block::Copper) {
                if (!found_copper || d2 < copper_d2) { found_copper = true; copper_x = x; copper_y = y; copper_d2 = d2; }
            }
        }
    }

    // Busca de VULCAO/LAVA num raio bem maior que o resto do scanner. Motivo medido: o
    // vulcao/lava mais proximo da base fica tipicamente a 150+ tiles, enquanto TODOS os
    // canais de descoberta do jogo sao menores que isso - o fog-of-war revela raio 15, o
    // scanner varria 70, o alcance de visao no chao e' ~110. Ou seja: nao existia nenhuma
    // forma de o jogador saber que ha um vulcao, e' por isso que ele reportou 3x que nao
    // achava nenhum. Passo de 2 tiles porque um rio de lava tem 3 tiles de largura (nunca
    // escapa da amostragem) - mantem a varredura barata mesmo com raio 4x maior.
    {
        static constexpr int kLavaScanRadius = 340;
        float lava_r2 = (float)(kLavaScanRadius * kLavaScanRadius);
        bool found_lava = false; int lava_x = 0, lava_y = 0; float lava_d2 = 0.0f;
        for (int dy = -kLavaScanRadius; dy <= kLavaScanRadius; dy += 2) {
            for (int dx = -kLavaScanRadius; dx <= kLavaScanRadius; dx += 2) {
                float d2 = (float)(dx * dx + dy * dy);
                if (d2 > lava_r2) continue;
                if (found_lava && d2 >= lava_d2) continue;
                int x = px + dx, y = py + dy;
                if (!g_world->in_bounds(x, y)) continue;
                // get_ground (nao get): lava vive na camada de solo; a camada de tiles pode
                // ter sido alterada por mineracao/terraformacao.
                if (g_world->get_ground(x, y) == Block::Lava) {
                    found_lava = true; lava_x = x; lava_y = y; lava_d2 = d2;
                }
            }
        }
        if (found_lava) {
            add_waypoint(lava_x, lava_y, "Atividade vulcanica");
            add_alert("Scanner: atividade vulcanica detectada!", 1.0f, 0.5f, 0.1f);
            return true;
        }
    }

    if (found_poi) {
        add_waypoint(poi_x, poi_y, "Sinal detectado");
        add_alert("Scanner: sinal de estrutura detectado!", 0.3f, 1.0f, 0.5f);
        return true;
    }
    if (found_metal) {
        add_waypoint(metal_x, metal_y, "Veio de metal");
        add_alert("Scanner: veio de metal detectado!", 0.3f, 1.0f, 0.5f);
        return true;
    }
    if (found_copper) {
        add_waypoint(copper_x, copper_y, "Veio de cobre");
        add_alert("Scanner: veio de cobre detectado!", 0.3f, 1.0f, 0.5f);
        return true;
    }
    set_toast("Nada de interessante por perto.", 2.0f);
    return false;
}

// Adicionar waypoint na posicao especificada
void add_waypoint(int x, int y, const char* label) {
    // Verificar limite de waypoints
    if ((int)g_minimap.waypoints.size() >= g_map_cfg.max_waypoints) {
        set_toast("Limite de waypoints atingido!", 2.0f);
        return;
    }

    // Verificar se ja existe waypoint proximo
    for (const auto& wp : g_minimap.waypoints) {
        float dx = (float)(wp.x - x);
        float dy = (float)(wp.y - y);
        if (std::sqrt(dx * dx + dy * dy) < g_map_cfg.waypoint_pick_radius) {
            set_toast("Waypoint ja existe nessa area", 1.5f);
            return;
        }
    }

    MapWaypoint wp;
    wp.x = x;
    wp.y = y;
    // Cor aleatoria para cada waypoint
    wp.r = 0.5f + 0.5f * ((float)(rand() % 100) / 100.0f);
    wp.g = 0.3f + 0.4f * ((float)(rand() % 100) / 100.0f);
    wp.b = 0.3f + 0.4f * ((float)(rand() % 100) / 100.0f);
    wp.label = label ? label : "";
    wp.visible = true;

    g_minimap.waypoints.push_back(wp);

    char msg[64];
    snprintf(msg, sizeof(msg), "Waypoint adicionado em (%d, %d)", x, y);
    set_toast(msg, 2.0f);
}

// Remover waypoint mais proximo da posicao
void remove_nearest_waypoint(int x, int y) {
    if (g_minimap.waypoints.empty()) return;

    float best_dist = 999999.0f;
    int best_idx = -1;

    for (int i = 0; i < (int)g_minimap.waypoints.size(); ++i) {
        float dx = (float)(g_minimap.waypoints[i].x - x);
        float dy = (float)(g_minimap.waypoints[i].y - y);
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = i;
        }
    }

    if (best_idx >= 0 && best_dist < g_map_cfg.waypoint_pick_radius * 3.0f) {
        g_minimap.waypoints.erase(g_minimap.waypoints.begin() + best_idx);
        set_toast("Waypoint removido", 1.5f);
    }
}

// Limpar todos os waypoints
void clear_all_waypoints() {
    g_minimap.waypoints.clear();
    set_toast("Todos os waypoints removidos", 2.0f);
}

// Obter cor do minimapa para um tile especifico
static void get_minimap_color(int x, int y, float& r, float& g, float& b) {
    if (!g_world || !g_world->in_bounds(x, y)) {
        r = 0.1f; g = 0.1f; b = 0.1f;
        return;
    }

    Block ground = g_world->get_ground(x, y);
    Block tile = g_world->get(x, y);
    int h = g_world->height_at(x, y);

    // Prioridade: base > modulos > tiles > terreno
    if (x == g_base_x && y == g_base_y) {
        // Base central - cor especial azul brilhante
        r = 0.3f; g = 0.7f; b = 1.0f;
        return;
    }

    if (is_base_structure(tile)) {
        r = 0.4f; g = 0.6f; b = 0.9f;
        return;
    }

    if (is_module(tile)) {
        // Modulos - amarelo/laranja
        r = 1.0f; g = 0.8f; b = 0.2f;
        return;
    }

    // Recursos especiais
    if (tile == Block::Crystal) { r = 0.8f; g = 0.3f; b = 0.9f; return; }
    if (tile == Block::Coal) { r = 0.2f; g = 0.2f; b = 0.2f; return; }
    // Ferro e cobre eram (0.7,0.5,0.4) e (0.9,0.6,0.3) - dois laranja-marrom quase iguais, e no
    // minimapa (1 pixel por tile) ficavam identicos: "no mapa aparece metal e ferro, mas nao tem
    // cobre". Agora ferro e' cinza-marrom dessaturado e cobre e' laranja saturado.
    if (tile == Block::Iron) { r = 0.52f; g = 0.42f; b = 0.38f; return; }
    if (tile == Block::Copper) { r = 1.0f; g = 0.48f; b = 0.12f; return; }

    // Terreno
    switch (ground) {
        case Block::Lava:
            // Lava nao tinha caso nenhum aqui: caia no default e vulcoes/rios de lava
            // apareciam no mapa com a mesma cor de terra qualquer. Laranja forte pra um
            // vulcao ja explorado ficar obvio no mapa e o jogador conseguir voltar nele.
            r = 1.0f; g = 0.42f; b = 0.05f;
            break;
        case Block::Water:
            r = 0.2f; g = 0.4f; b = 0.8f;
            break;
        case Block::Ice:
            r = 0.7f; g = 0.9f; b = 1.0f;
            break;
        case Block::Snow:
            r = 0.95f; g = 0.95f; b = 1.0f;
            break;
        case Block::Sand:
            r = 0.9f; g = 0.8f; b = 0.5f;
            break;
        case Block::Stone:
            r = 0.5f; g = 0.5f; b = 0.55f;
            break;
        case Block::Grass:
            r = 0.35f; g = 0.65f; b = 0.3f;
            break;
        case Block::Dirt:
            r = 0.5f; g = 0.4f; b = 0.3f;
            break;
        default:
            // Variar cor por altura para dar profundidade
            {
                float hf = std::clamp((float)h / 30.0f, 0.0f, 1.0f);
                r = 0.35f - hf * 0.15f;
                g = 0.55f - hf * 0.15f;
                b = 0.25f + hf * 0.1f;
            }
            break;
    }

    // Ajuste de altura para dar sensacao de profundidade
    float height_mod = 1.0f + (float)h * 0.008f;
    r = std::clamp(r * height_mod, 0.0f, 1.0f);
    g = std::clamp(g * height_mod, 0.0f, 1.0f);
    b = std::clamp(b * height_mod, 0.0f, 1.0f);
}

// Renderizar minimapa no HUD
void render_minimap(int win_w, int win_h) {
    if (!g_world) return;

    float map_size = g_map_cfg.minimap_size;
    // Ancorado nas mesmas 2 funcoes que ui_hud.cpp usa pra desenhar o painel de Fase/
    // terraformacao (ver comentario completo em ui_hud.h) - antes o minimapa usava um "y"
    // fixo (200px) sem nenhuma ligacao com a altura de verdade daquele painel (calculada
    // separadamente em render_hud()), entao os dois podiam (e chegaram a) se sobrepor.
    // Tambem alinha a borda direita dos dois, que antes tinham anchors/larguras diferentes.
    float map_x = hud_right_panel_right_x(win_w) - 3.0f - map_size;
    float map_y = hud_right_panel_bottom_y() + 12.0f;
    float map_radius = map_size * 0.5f;
    float map_cx = map_x + map_radius;
    float map_cy = map_y + map_radius;

    // Fundo do minimapa (borda) - circular de verdade agora (era um quadrado, sem nenhum
    // recorte - o "formato estranho" reportado era o contorno arredondado do fog-of-war
    // (revelado em circulo) contra os cantos quadrados do minimapa, nao um bug de
    // clipping). 2 circulos concentricos (fundo escuro + anel de borda), mesmo raio geral.
    render_circle(map_cx, map_cy, map_radius + 3.0f, 0.1f, 0.1f, 0.15f, 0.95f, 32);
    render_circle(map_cx, map_cy, map_radius + 1.0f, 0.2f, 0.25f, 0.3f, 0.9f, 32);

    // Calcular viewport do mapa (tiles visiveis)
    int view_tiles = (int)(g_map_cfg.minimap_zoom * 64.0f);
    if (view_tiles < 16) view_tiles = 16;
    if (view_tiles > 128) view_tiles = 128;

    int start_x = (int)g_player.pos.x - view_tiles / 2;
    int start_y = (int)g_player.pos.y - view_tiles / 2;

    float tile_px = map_size / (float)view_tiles;

    // Renderizar tiles do minimapa
    float map_radius2 = map_radius * map_radius;
    for (int ty = 0; ty < view_tiles; ++ty) {
        for (int tx = 0; tx < view_tiles; ++tx) {
            int wx = start_x + tx;
            int wy = start_y + ty;

            float px = map_x + (float)tx * tile_px;
            float py = map_y + (float)ty * tile_px;

            // Recorte circular: pula tiles cujo centro caia fora do circulo do minimapa
            // (antes desenhava o quadrado inteiro - os 4 cantos ficavam pra fora da borda
            // circular de fundo, um dos motivos do visual "estranho" reportado).
            float cdx = (px + tile_px * 0.5f) - map_cx;
            float cdy = (py + tile_px * 0.5f) - map_cy;
            if (cdx * cdx + cdy * cdy > map_radius2) continue;

            // Fora dos limites do mundo
            if (!g_world->in_bounds(wx, wy)) {
                render_quad(px, py, tile_px + 0.5f, tile_px + 0.5f, 0.05f, 0.05f, 0.08f, 1.0f);
                continue;
            }

            // Verificar fog of war
            size_t idx = (size_t)wy * (size_t)g_world->w + (size_t)wx;
            if (idx >= g_minimap.explored.size() || g_minimap.explored[idx] == 0) {
                // Nao explorado - escuro
                render_quad(px, py, tile_px + 0.5f, tile_px + 0.5f, 0.08f, 0.08f, 0.1f, 1.0f);
                continue;
            }

            // Obter cor do tile
            float r, g, b;
            get_minimap_color(wx, wy, r, g, b);

            // Ajustar brilho para ciclo dia/noite
            float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
            float daylight = compute_daylight(day_phase);
            float brightness = 0.5f + daylight * 0.5f;
            r *= brightness;
            g *= brightness;
            b *= brightness;

            render_quad(px, py, tile_px + 0.5f, tile_px + 0.5f, r, g, b, 1.0f);
        }
    }

    // Icone do jogador (centro do minimapa)
    {
        float player_px = map_x + map_size * 0.5f;
        float player_py = map_y + map_size * 0.5f;
        float icon_size = 4.0f;

        // Triangulo indicando direcao do jogador
        float facing_rad = g_player.rotation * (kPi / 180.0f);
        float cos_f = std::cos(facing_rad);
        float sin_f = std::sin(facing_rad);

        float p0x = player_px - sin_f * icon_size * 1.5f, p0y = player_py - cos_f * icon_size * 1.5f;
        float p1x = player_px + sin_f * icon_size - cos_f * icon_size, p1y = player_py + cos_f * icon_size + sin_f * icon_size;
        float p2x = player_px + sin_f * icon_size + cos_f * icon_size, p2y = player_py + cos_f * icon_size - sin_f * icon_size;

        rlBegin(RL_TRIANGLES);
        rlColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        rlVertex2f(p0x, p0y);
        rlVertex2f(p1x, p1y);
        rlVertex2f(p2x, p2y);
        rlEnd();

        // Contorno (GL_LINE_LOOP -> segmentos RL_LINES consecutivos + segmento de fechamento)
        rlSetLineWidth(1.5f);
        rlBegin(RL_LINES);
        rlColor4f(0.0f, 0.0f, 0.0f, 0.8f);
        rlVertex2f(p0x, p0y); rlVertex2f(p1x, p1y);
        rlVertex2f(p1x, p1y); rlVertex2f(p2x, p2y);
        rlVertex2f(p2x, p2y); rlVertex2f(p0x, p0y);
        rlEnd();
    }

    // Icone da base (se visivel no viewport)
    {
        int base_rel_x = g_base_x - start_x;
        int base_rel_y = g_base_y - start_y;

        if (base_rel_x >= 0 && base_rel_x < view_tiles && base_rel_y >= 0 && base_rel_y < view_tiles) {
            float base_px = map_x + ((float)base_rel_x + 0.5f) * tile_px;
            float base_py = map_y + ((float)base_rel_y + 0.5f) * tile_px;

            // Nao desenha se cair fora do circulo (mesmo recorte da grade de tiles acima) -
            // senao o icone "vaza" pra fora da borda circular nos cantos.
            float bdx = base_px - map_cx, bdy = base_py - map_cy;
            if (bdx * bdx + bdy * bdy <= map_radius2) {

            // Icone de casa/base
            float house_size = 5.0f;

            // Telhado (triangulo)
            rlBegin(RL_TRIANGLES);
            rlColor4f(0.3f, 0.7f, 1.0f, 1.0f);
            rlVertex2f(base_px, base_py - house_size);
            rlVertex2f(base_px - house_size, base_py);
            rlVertex2f(base_px + house_size, base_py);
            rlEnd();

            // Base (quadrado)
            render_quad(base_px - house_size * 0.7f, base_py, house_size * 1.4f, house_size, 0.3f, 0.6f, 0.9f, 1.0f);
            }
        }
    }

    // Renderizar waypoints no minimapa
    for (const auto& wp : g_minimap.waypoints) {
        int wp_rel_x = (int)wp.x - start_x;
        int wp_rel_y = (int)wp.y - start_y;

        if (wp_rel_x >= 0 && wp_rel_x < view_tiles && wp_rel_y >= 0 && wp_rel_y < view_tiles) {
            float wp_px = map_x + ((float)wp_rel_x + 0.5f) * tile_px;
            float wp_py = map_y + ((float)wp_rel_y + 0.5f) * tile_px;

            // Mesmo recorte circular - pula se cair fora da borda.
            float wdx = wp_px - map_cx, wdy = wp_py - map_cy;
            if (wdx * wdx + wdy * wdy > map_radius2) continue;

            // Marcador de waypoint (losango)
            float wp_size = 3.0f;
            rlBegin(RL_QUADS);
            rlColor4f(wp.r, wp.g, wp.b, 1.0f);
            rlVertex2f(wp_px, wp_py - wp_size);
            rlVertex2f(wp_px + wp_size, wp_py);
            rlVertex2f(wp_px, wp_py + wp_size);
            rlVertex2f(wp_px - wp_size, wp_py);
            rlEnd();
        }
    }

    // Titulo do minimapa
    draw_text(map_x + 4.0f, map_y + map_size + 14.0f, "MAPA [M]", 0.7f, 0.75f, 0.8f, 0.8f);

    // Mostrar zoom atual
    char zoom_str[32];
    snprintf(zoom_str, sizeof(zoom_str), "Zoom: %.1fx", g_map_cfg.minimap_zoom);
    draw_text(map_x + map_size - 60.0f, map_y + map_size + 14.0f, zoom_str, 0.6f, 0.65f, 0.7f, 0.7f);
}

// Renderizar mapa grande (tela cheia, tecla M)
void render_world_map(int win_w, int win_h) {
    if (!g_world || !g_minimap.world_map_open) return;

    // Fundo escuro semi-transparente
    render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.0f, 0.0f, 0.0f, 0.85f);

    // Dimensoes do mapa
    float map_margin = 50.0f;
    float map_w = (float)win_w - map_margin * 2.0f;
    float map_h = (float)win_h - map_margin * 2.0f - 50.0f;  // Espaco para legenda
    float map_x = map_margin;
    float map_y = map_margin;

    // Borda do mapa
    render_quad(map_x - 3.0f, map_y - 3.0f, map_w + 6.0f, map_h + 6.0f, 0.15f, 0.18f, 0.22f, 0.95f);
    render_quad(map_x - 1.0f, map_y - 1.0f, map_w + 2.0f, map_h + 2.0f, 0.25f, 0.28f, 0.32f, 0.9f);

    // Calcular viewport baseado em zoom e pan
    float zoom = g_minimap.world_zoom;
    float center_x = g_minimap.world_pan_x;
    float center_y = g_minimap.world_pan_y;

    // Se pan esta em 0, centralizar no jogador
    if (std::fabs(center_x) < 0.1f && std::fabs(center_y) < 0.1f) {
        center_x = g_player.pos.x;
        center_y = g_player.pos.y;
    }

    // Tiles visiveis
    float tiles_visible_x = (float)g_world->w / zoom;
    float tiles_visible_y = (float)g_world->h / zoom;

    // Ajustar para aspect ratio
    float map_aspect = map_w / map_h;
    float world_aspect = tiles_visible_x / tiles_visible_y;

    if (map_aspect > world_aspect) {
        tiles_visible_x = tiles_visible_y * map_aspect;
    } else {
        tiles_visible_y = tiles_visible_x / map_aspect;
    }

    float start_world_x = center_x - tiles_visible_x * 0.5f;
    float start_world_y = center_y - tiles_visible_y * 0.5f;

    // Pixels por tile
    float px_per_tile_x = map_w / tiles_visible_x;
    float px_per_tile_y = map_h / tiles_visible_y;

    // Renderizar tiles (com resolucao adaptativa para performance)
    int step = std::max(1, (int)(1.0f / zoom));

    for (float wy = 0.0f; wy < tiles_visible_y; wy += (float)step) {
        for (float wx = 0.0f; wx < tiles_visible_x; wx += (float)step) {
            int world_x = (int)(start_world_x + wx);
            int world_y = (int)(start_world_y + wy);

            float px = map_x + wx * px_per_tile_x;
            float py = map_y + wy * px_per_tile_y;
            float tile_w = px_per_tile_x * (float)step + 1.0f;
            float tile_h = px_per_tile_y * (float)step + 1.0f;

            // Fora do mundo
            if (!g_world->in_bounds(world_x, world_y)) {
                render_quad(px, py, tile_w, tile_h, 0.05f, 0.05f, 0.08f, 1.0f);
                continue;
            }

            // Fog of war
            size_t idx = (size_t)world_y * (size_t)g_world->w + (size_t)world_x;
            if (idx >= g_minimap.explored.size() || g_minimap.explored[idx] == 0) {
                render_quad(px, py, tile_w, tile_h, 0.1f, 0.1f, 0.12f, 1.0f);
                continue;
            }

            // Cor do tile
            float r, g, b;
            get_minimap_color(world_x, world_y, r, g, b);
            render_quad(px, py, tile_w, tile_h, r, g, b, 1.0f);
        }
    }

    // Converter coordenadas do mundo para coordenadas do mapa
    auto world_to_map = [&](float wx, float wy, float& mx, float& my) {
        mx = map_x + (wx - start_world_x) * px_per_tile_x;
        my = map_y + (wy - start_world_y) * px_per_tile_y;
    };

    // Icone da base
    {
        float base_mx, base_my;
        world_to_map((float)g_base_x + 0.5f, (float)g_base_y + 0.5f, base_mx, base_my);

        if (base_mx >= map_x && base_mx <= map_x + map_w && base_my >= map_y && base_my <= map_y + map_h) {
            float house_size = 10.0f;

            // Telhado
            rlBegin(RL_TRIANGLES);
            rlColor4f(0.3f, 0.7f, 1.0f, 1.0f);
            rlVertex2f(base_mx, base_my - house_size * 1.5f);
            rlVertex2f(base_mx - house_size, base_my);
            rlVertex2f(base_mx + house_size, base_my);
            rlEnd();

            // Base
            render_quad(base_mx - house_size * 0.8f, base_my, house_size * 1.6f, house_size, 0.3f, 0.6f, 0.9f, 1.0f);

            // Label
            draw_text(base_mx - 15.0f, base_my + house_size + 15.0f, "BASE", 0.3f, 0.7f, 1.0f, 1.0f);
        }
    }

    // Icone do jogador
    {
        float player_mx, player_my;
        world_to_map(g_player.pos.x, g_player.pos.y, player_mx, player_my);

        if (player_mx >= map_x && player_mx <= map_x + map_w && player_my >= map_y && player_my <= map_y + map_h) {
            float icon_size = 8.0f;
            float facing_rad = g_player.rotation * (kPi / 180.0f);
            float cos_f = std::cos(facing_rad);
            float sin_f = std::sin(facing_rad);

            float p0x = player_mx - sin_f * icon_size * 1.5f, p0y = player_my - cos_f * icon_size * 1.5f;
            float p1x = player_mx + sin_f * icon_size - cos_f * icon_size, p1y = player_my + cos_f * icon_size + sin_f * icon_size;
            float p2x = player_mx + sin_f * icon_size + cos_f * icon_size, p2y = player_my + cos_f * icon_size - sin_f * icon_size;

            rlBegin(RL_TRIANGLES);
            rlColor4f(1.0f, 1.0f, 1.0f, 1.0f);
            rlVertex2f(p0x, p0y);
            rlVertex2f(p1x, p1y);
            rlVertex2f(p2x, p2y);
            rlEnd();

            // Contorno (GL_LINE_LOOP -> RL_LINES + segmento de fechamento)
            rlSetLineWidth(2.0f);
            rlBegin(RL_LINES);
            rlColor4f(0.0f, 0.0f, 0.0f, 0.9f);
            rlVertex2f(p0x, p0y); rlVertex2f(p1x, p1y);
            rlVertex2f(p1x, p1y); rlVertex2f(p2x, p2y);
            rlVertex2f(p2x, p2y); rlVertex2f(p0x, p0y);
            rlEnd();
        }
    }

    // Waypoints
    for (const auto& wp : g_minimap.waypoints) {
        if (!wp.visible) continue;

        float wp_mx, wp_my;
        world_to_map((float)wp.x + 0.5f, (float)wp.y + 0.5f, wp_mx, wp_my);

        if (wp_mx >= map_x && wp_mx <= map_x + map_w && wp_my >= map_y && wp_my <= map_y + map_h) {
            float wp_size = 6.0f;

            // Losango
            rlBegin(RL_QUADS);
            rlColor4f(wp.r, wp.g, wp.b, 1.0f);
            rlVertex2f(wp_mx, wp_my - wp_size);
            rlVertex2f(wp_mx + wp_size, wp_my);
            rlVertex2f(wp_mx, wp_my + wp_size);
            rlVertex2f(wp_mx - wp_size, wp_my);
            rlEnd();

            // Contorno (GL_LINE_LOOP -> RL_LINES + segmento de fechamento)
            rlSetLineWidth(1.5f);
            rlBegin(RL_LINES);
            rlColor4f(0.0f, 0.0f, 0.0f, 0.8f);
            rlVertex2f(wp_mx, wp_my - wp_size); rlVertex2f(wp_mx + wp_size, wp_my);
            rlVertex2f(wp_mx + wp_size, wp_my); rlVertex2f(wp_mx, wp_my + wp_size);
            rlVertex2f(wp_mx, wp_my + wp_size); rlVertex2f(wp_mx - wp_size, wp_my);
            rlVertex2f(wp_mx - wp_size, wp_my); rlVertex2f(wp_mx, wp_my - wp_size);
            rlEnd();

            // Label se houver
            if (!wp.label.empty()) {
                draw_text(wp_mx + wp_size + 3.0f, wp_my + 4.0f, wp.label, wp.r, wp.g, wp.b, 0.9f);
            }
        }
    }

    // Modulos no mapa
    for (const auto& mod : g_modules) {
        float mod_mx, mod_my;
        world_to_map((float)mod.x + 0.5f, (float)mod.y + 0.5f, mod_mx, mod_my);

        if (mod_mx >= map_x && mod_mx <= map_x + map_w && mod_my >= map_y && mod_my <= map_y + map_h) {
            float mod_size = 4.0f;
            render_quad(mod_mx - mod_size, mod_my - mod_size, mod_size * 2.0f, mod_size * 2.0f, 1.0f, 0.8f, 0.2f, 0.9f);
        }
    }

    // Titulo e controles
    draw_text(map_x, map_y - 25.0f, "MAPA DO MUNDO", 0.9f, 0.92f, 0.95f, 1.0f);

    // Legenda na parte inferior
    float legend_y = map_y + map_h + 15.0f;
    draw_text(map_x, legend_y, "Controles: WASD=Mover | Scroll=Zoom | Clique=Waypoint | R=Remover | C=Limpar | M/ESC=Fechar",
              0.7f, 0.75f, 0.8f, 0.85f);

    // Info de posicao
    char pos_str[64];
    snprintf(pos_str, sizeof(pos_str), "Posicao: (%.0f, %.0f) | Zoom: %.1fx | Waypoints: %d/%d",
             g_player.pos.x, g_player.pos.y, zoom, (int)g_minimap.waypoints.size(), g_map_cfg.max_waypoints);
    draw_text(map_x + map_w - 350.0f, legend_y, pos_str, 0.6f, 0.65f, 0.7f, 0.8f);

    // Legenda de cores
    float legend_x = map_x;
    float legend_item_y = legend_y + 20.0f;

    // Base
    render_quad(legend_x, legend_item_y, 12.0f, 12.0f, 0.3f, 0.7f, 1.0f, 1.0f);
    draw_text(legend_x + 16.0f, legend_item_y + 10.0f, "Base", 0.7f, 0.75f, 0.8f, 0.9f);

    // Modulos
    legend_x += 70.0f;
    render_quad(legend_x, legend_item_y, 12.0f, 12.0f, 1.0f, 0.8f, 0.2f, 1.0f);
    draw_text(legend_x + 16.0f, legend_item_y + 10.0f, "Modulos", 0.7f, 0.75f, 0.8f, 0.9f);

    // Jogador
    legend_x += 90.0f;
    render_quad(legend_x, legend_item_y, 12.0f, 12.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    draw_text(legend_x + 16.0f, legend_item_y + 10.0f, "Jogador", 0.7f, 0.75f, 0.8f, 0.9f);

    // Agua
    legend_x += 90.0f;
    render_quad(legend_x, legend_item_y, 12.0f, 12.0f, 0.2f, 0.4f, 0.8f, 1.0f);
    draw_text(legend_x + 16.0f, legend_item_y + 10.0f, "Agua", 0.7f, 0.75f, 0.8f, 0.9f);

    // Gelo
    legend_x += 70.0f;
    render_quad(legend_x, legend_item_y, 12.0f, 12.0f, 0.7f, 0.9f, 1.0f, 1.0f);
    draw_text(legend_x + 16.0f, legend_item_y + 10.0f, "Gelo", 0.7f, 0.75f, 0.8f, 0.9f);

    // Inexplorado
    legend_x += 70.0f;
    render_quad(legend_x, legend_item_y, 12.0f, 12.0f, 0.1f, 0.1f, 0.12f, 1.0f);
    draw_text(legend_x + 16.0f, legend_item_y + 10.0f, "Inexplorado", 0.7f, 0.75f, 0.8f, 0.9f);
}
