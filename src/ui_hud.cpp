#include "ui_hud.h"

#include "raylib_platform.h"
#include "math_core.h"
#include "blocks.h"
#include "textures.h"
#include "world.h"
#include "config_types.h"
#include "game_state.h"
#include "player_physics.h"
#include "inventory_crafting.h"
#include "modules_building.h"
#include "font.h"
#include "interiors.h"   // interior_prompt (dica de porta exterior<->interior)
#include "minimap.h"
#include "render_primitives.h"
#include "lighting.h"
#include "camera.h"
#include "objectives.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// Geometria compartilhada do painel direito (Fase/terraformacao) - unica fonte de verdade,
// ver comentario completo em ui_hud.h. render_hud() (abaixo) e minimap.cpp's
// render_minimap() usam as mesmas 2 funcoes em vez de recalcular os mesmos numeros de
// forma independente (era assim antes - a causa raiz do minimapa sobrepondo o painel).
static constexpr float kRightPanelBarW = 180.0f;
static constexpr float kRightPanelBarGap = 18.0f;
static constexpr float kRightPanelTopY = 18.0f;
static constexpr float kRightPanelH = kRightPanelBarGap * 6.0f + 90.0f;
// Ver comentario da declaracao em ui_hud.h.
bool g_hud_pointer_over_button = false;


float hud_right_panel_right_x(int win_w) {
    return (float)win_w - 20.0f;
}

float hud_right_panel_bottom_y() {
    return (kRightPanelTopY - 10.0f) + kRightPanelH;
}

// ============= HUD Rendering =============
// Extracted verbatim from main.cpp's render_world() (original lines ~1707-2451): the
// switch from 3D to 2D/ortho projection, the vignette effect, the lightmap/lights debug
// overlays, the mouse crosshair, HP/O2/water/food/jetpack status bars, base resource bars,
// terraforming/phase/temperature/CO2/atmosphere stats, the base direction indicator, the
// minimap, the hotbar (resource + module slots), collect popups, target/debug info, toast
// notifications, screen-flash feedback, the unlock popup, and the onboarding tip. Only the
// projection switch + HUD drawing moved here; the Paused/Menu/Dead/Settings overlay block
// and the alerts/world-map overlay that used to sit right after this in render_world()
// stay inline in main.cpp (a later ui_menu extraction stage handles those).
//
// g_atmosphere/g_base_cfg/g_base_energy/g_base_food/g_base_integrity/g_base_oxygen/
// g_base_water/g_base_x/g_base_y/g_co2_level/g_day_time/g_debug/g_phase/g_player_food/
// g_player_oxygen/g_player_water/g_temperature/g_terraform/g_unlocks/g_minimap are all
// owned by main.cpp (already non-static there for other extracted modules) - this file
// gets them via its own local extern declarations, same pattern as g_day_time/g_base_x in
// modules_building.cpp/minimap.cpp/etc.
//
// g_has_target/g_mouse_left_clicked/g_mouse_x/g_mouse_y/g_target_in_range/g_target_x/
// g_target_y lost "static" in main.cpp for this stage: they used to only be touched by
// main.cpp's own mining-raycast/mouse-input code (still there, out of scope for this
// stage), but the hotbar/target-info HUD code moved here now reads them from this new
// translation unit too - same reasoning as every other "lost static" comment in this
// codebase's extraction stages.
extern float g_atmosphere;
extern BaseConfig g_base_cfg;
extern float g_base_energy;
extern float g_base_food;
extern float g_base_integrity;
extern float g_base_oxygen;
extern float g_base_water;
extern int g_base_x;
extern int g_base_y;
extern float g_co2_level;
extern float g_day_time;
extern bool g_debug;
extern bool g_has_target;
extern bool g_mouse_left_clicked;
extern int g_mouse_x;
extern int g_mouse_y;
extern bool g_target_in_range;
extern int g_target_x;
extern int g_target_y;
extern TerraPhase g_phase;
extern float g_player_food;
extern float g_player_oxygen;
extern float g_suit_integrity;
extern float g_player_water;
extern float g_temperature;
extern float g_terraform;
extern UnlockProgress g_unlocks;
extern MiniMapRuntime g_minimap;
// g_physics_cfg: found missing during this refactor's final sanity sweep (Fase 1 closeout) -
// render_hud()'s F3 debug line reads g_physics_cfg.fixed_timestep but this file had no
// extern declaration for it (a gap from the original ui_hud extraction stage, pre-dating the
// input/win32_platform stage). Same pattern as the other extern declarations above:
// PhysicsConfig is owned (non-static) by main.cpp.
extern PhysicsConfig g_physics_cfg;

// kBaseEnergyMax/kBaseWaterMax/kBaseOxygenMax/kBaseFoodMax/kBaseIntegrityMax are
// compile-time literals (not mutable state) defined in main.cpp; kept here as this file's
// own copy rather than shared via extern - same pattern as the kDayLength/kBaseIntegrityMax
// duplication already used in modules_building.cpp/minimap.cpp/sky.cpp/lighting.cpp.
static constexpr float kBaseEnergyMax = 500.0f;
static constexpr float kBaseWaterMax = 200.0f;
static constexpr float kBaseOxygenMax = 200.0f;
static constexpr float kBaseFoodMax = 200.0f;
static constexpr float kBaseIntegrityMax = 100.0f;

// kColorX arrays: same "own copy of a compile-time-ish literal" reasoning as the kBase*Max
// constants above - these are internal-linkage (static const float[]) color tables defined
// in main.cpp ("SISTEMA DE CORES CENTRALIZADO"), so this file keeps its own identical copy
// of only the ones the moved HUD code actually uses, rather than changing main.cpp's
// existing linkage for a purely cosmetic constant table.
static const float kColorHp[]           = {0.90f, 0.14f, 0.18f, 1.0f};   // Vermelho - vida/dano
static const float kColorOxygen[]        = {0.20f, 0.85f, 0.55f, 1.0f};  // Verde - oxigenio
static const float kColorWater[]         = {0.25f, 0.65f, 0.95f, 1.0f};  // Azul - agua
static const float kColorFood[]          = {0.85f, 0.65f, 0.25f, 1.0f};  // Laranja - comida
static const float kColorDanger[]        = {0.95f, 0.35f, 0.20f, 1.0f};  // Vermelho-laranja - perigo
static const float kColorSuccess[]       = {0.30f, 0.95f, 0.45f, 1.0f};  // Verde brilhante - sucesso
static const float kColorWarning[]       = {0.95f, 0.75f, 0.20f, 1.0f};  // Amarelo-laranja - aviso
static const float kColorTextPrimary[]   = {0.95f, 0.95f, 0.95f, 1.0f};  // Texto principal
static const float kColorTextSecondary[] = {0.70f, 0.70f, 0.75f, 0.90f}; // Texto secundario
static const float kColorSelection[]     = {0.35f, 0.65f, 0.95f, 0.80f}; // Selecao azul

// Painel padrao do HUD: sombra suave + corpo com cantos arredondados (azul-acinzentado
// escuro em vez de preto chapado) + realce superior sutil (efeito "vidro"/holografico) +
// linha de destaque colorida na base, no tema do painel. Substitui os antigos fundos
// retangulares chapados (render_quad preto translucido) usados por cada painel do HUD -
// visual mais suave/polido, mantendo a mesma area/posicao de cada painel.
static void draw_hud_panel(float x, float y, float w, float h, float accent_r, float accent_g, float accent_b) {
    render_rounded_rect(x + 3.0f, y + 4.0f, w, h, 8.0f, 0.0f, 0.0f, 0.0f, 0.30f);
    render_rounded_rect(x, y, w, h, 8.0f, 0.06f, 0.08f, 0.12f, 0.68f);
    render_quad(x + 4.0f, y + 2.0f, w - 8.0f, h * 0.30f, 1.0f, 1.0f, 1.0f, 0.05f);
    render_quad(x + 4.0f, y + h - 2.0f, w - 8.0f, 2.0f, accent_r, accent_g, accent_b, 0.45f);
}

void render_hud(int win_w, int win_h) {
    // === MUDAR PARA PROJECAO 2D PARA HUD ===
    // CRITICO (migracao raylib): rlgl NAO transforma vertices no momento de rlVertex3f() -
    // ele so aplica RLGL.State.transform (rlPushMatrix/rlTranslatef, nao usado aqui) e guarda
    // a posicao "crua" no vertex buffer da render batch. A projecao/view (modelview) so e
    // aplicada quando a batch e efetivamente desenhada (rlDrawRenderBatch), usando o
    // RLGL.State.projection/modelview *daquele momento* - nao o que estava ativo quando cada
    // rlVertex3f foi chamado (ver rlgl.h: rlVertex3f so aplica RLGL.State.transform; o MVP e
    // montado dentro de rlDrawRenderBatch a partir do estado atual). No OpenGL 1.x fixo
    // original isso nunca foi um problema (cada glVertex era processado imediatamente pelo
    // pipeline fixo, na ordem exata dos comandos). Sem um flush explicito aqui, toda a
    // geometria 3D ainda pendente no buffer da render batch (terreno/paredes/player/outlines/
    // beacon - render_world() acima, antes desta chamada) so seria de fato desenhada no
    // EndDrawing() do loop principal (win32_platform.cpp) - momento em que a matriz ja teria
    // sido trocada para a ortho 2D abaixo, fazendo aquela geometria 3D (coordenadas de mundo,
    // ex.: x/z na faixa 0..512/0..256) ser interpretada como coordenadas de tela em pixels
    // pela ortho, produzindo uma superficie/quad gigante e mal posicionada cobrindo boa parte
    // da tela (e o player, com "size" de fracoes de unidade, ficando efetivamente invisivel/
    // sub-pixel). rlDrawRenderBatchActive() forca o desenho de tudo que estiver pendente
    // *agora*, enquanto a projecao/view 3D de render_world() ainda esta ativa, antes de trocar
    // para a projecao 2D do HUD.
    rlDrawRenderBatchActive();
    g_frame_fog.enabled = false; // era glDisable(GL_FOG)
    rlDisableDepthTest();
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlOrtho(0, win_w, win_h, 0, -1, 1);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();

    // === VINHETA (RTX FAKE - efeito cinematico) ===
    if (g_lighting.enabled && g_lighting.vignette_intensity > 0.0f) {
        rlSetBlendMode(RL_BLEND_ALPHA);

        // Desenhar vinheta como gradiente radial usando quads
        float cx = win_w * 0.5f;
        float cy = win_h * 0.5f;
        float max_dist = std::sqrt(cx * cx + cy * cy);
        float vignette_start = g_lighting.vignette_radius * max_dist;

        // Criar overlay de vinheta com gradiente
        int segments = 32;
        for (int ring = 0; ring < 8; ++ring) {
            float inner_r = vignette_start + ring * (max_dist - vignette_start) / 8.0f;
            float outer_r = vignette_start + (ring + 1) * (max_dist - vignette_start) / 8.0f;
            float inner_alpha = (float)ring / 8.0f * g_lighting.vignette_intensity;
            float outer_alpha = (float)(ring + 1) / 8.0f * g_lighting.vignette_intensity;

            // GL_QUAD_STRIP -> RL_QUADS: buffer o par de vertices anterior (outer,inner) e,
            // a partir da 2a iteracao, emite o quad (prev_outer, prev_inner, cur_inner,
            // cur_outer) na mesma ordem que a strip original produziria.
            rlBegin(RL_QUADS);
            float prev_ox = 0, prev_oy = 0, prev_oa = 0;
            float prev_ix = 0, prev_iy = 0, prev_ia = 0;
            bool have_prev = false;
            for (int i = 0; i <= segments; ++i) {
                float angle = (float)i / segments * 2.0f * kPi;
                float cos_a = std::cos(angle);
                float sin_a = std::sin(angle);

                float ox = cx + outer_r * cos_a, oy = cy + outer_r * sin_a;
                float ix = cx + inner_r * cos_a, iy = cy + inner_r * sin_a;

                if (have_prev) {
                    rlColor4f(0.0f, 0.0f, 0.0f, prev_oa); rlVertex2f(prev_ox, prev_oy);
                    rlColor4f(0.0f, 0.0f, 0.0f, prev_ia); rlVertex2f(prev_ix, prev_iy);
                    rlColor4f(0.0f, 0.0f, 0.0f, inner_alpha); rlVertex2f(ix, iy);
                    rlColor4f(0.0f, 0.0f, 0.0f, outer_alpha); rlVertex2f(ox, oy);
                }
                prev_ox = ox; prev_oy = oy; prev_oa = outer_alpha;
                prev_ix = ix; prev_iy = iy; prev_ia = inner_alpha;
                have_prev = true;
            }
            rlEnd();
        }
    }
    
    // === DEBUG: VISUALIZAR LIGHTMAP ===
    if (g_debug_lightmap && g_lighting.enabled) {
        float debug_size = 150.0f;
        float debug_x = win_w - debug_size - 10.0f;
        float debug_y = 10.0f;
        float cell_size = debug_size / kLightmapSize;
        
        // Fundo
        rlBegin(RL_QUADS);
        rlColor4f(0.0f, 0.0f, 0.0f, 0.8f);
        rlVertex2f(debug_x - 5, debug_y - 5);
        rlVertex2f(debug_x + debug_size + 5, debug_y - 5);
        rlVertex2f(debug_x + debug_size + 5, debug_y + debug_size + 5);
        rlVertex2f(debug_x - 5, debug_y + debug_size + 5);
        rlEnd();

        // Lightmap pixels
        for (int z = 0; z < kLightmapSize; ++z) {
            for (int x = 0; x < kLightmapSize; ++x) {
                int idx = z * kLightmapSize + x;
                float r = std::min(1.0f, g_lightmap_r[idx]);
                float g = std::min(1.0f, g_lightmap_g[idx]);
                float b = std::min(1.0f, g_lightmap_b[idx]);

                float px = debug_x + x * cell_size;
                float py = debug_y + z * cell_size;
                rlBegin(RL_QUADS);
                rlColor4f(r, g, b, 1.0f);
                rlVertex2f(px, py);
                rlVertex2f(px + cell_size, py);
                rlVertex2f(px + cell_size, py + cell_size);
                rlVertex2f(px, py + cell_size);
                rlEnd();
            }
        }
        
        // Label
        draw_text(debug_x, debug_y + debug_size + 10.0f, "LIGHTMAP DEBUG", 0.9f, 0.9f, 0.3f, 1.0f);
    }
    
    // === DEBUG: VISUALIZAR LUZES ===
    if (g_debug_lights && g_lighting.enabled) {
        float debug_y = g_debug_lightmap ? 180.0f : 10.0f;
        char buf[128];
        snprintf(buf, sizeof(buf), "Luzes ativas: %d", (int)g_lights.size());
        draw_text(win_w - 200.0f, debug_y, buf, 0.9f, 0.9f, 0.3f, 1.0f);
        
        float y_offset = debug_y + 20.0f;
        for (size_t i = 0; i < std::min(g_lights.size(), (size_t)8); ++i) {
            const auto& light = g_lights[i];
            snprintf(buf, sizeof(buf), "L%d: (%.1f,%.1f) r=%.1f i=%.2f", 
                (int)i, light.x, light.y, light.radius, light.intensity);
            draw_text(win_w - 200.0f, y_offset, buf, light.r, light.g, light.b, 1.0f);
            y_offset += 15.0f;
        }
    }
    
    // === CROSSHAIR SEGUINDO O MOUSE (Estilo Minicraft) ===
    {
        float cx = (float)g_mouse_x;
        float cy = (float)g_mouse_y;
        float cross_size = 12.0f;
        float cross_thick = 2.0f;
        
        // Contorno preto
        rlSetLineWidth(cross_thick + 2.0f);
        rlBegin(RL_LINES);
        rlColor4f(0.0f, 0.0f, 0.0f, 0.7f);
        rlVertex2f(cx - cross_size, cy);
        rlVertex2f(cx + cross_size, cy);
        rlVertex2f(cx, cy - cross_size);
        rlVertex2f(cx, cy + cross_size);
        rlEnd();

        // Crosshair branco
        rlSetLineWidth(cross_thick);
        rlBegin(RL_LINES);
        rlColor4f(1.0f, 1.0f, 1.0f, 0.9f);
        rlVertex2f(cx - cross_size, cy);
        rlVertex2f(cx + cross_size, cy);
        rlVertex2f(cx, cy - cross_size);
        rlVertex2f(cx, cy + cross_size);
        rlEnd();

        // Ponto central (GL_POINTS de 1 ponto -> DrawCircle, mais simples que reconstruir
        // um quad billboard 2D para um unico pixel de tela).
        DrawCircle((int)cx, (int)cy, 2.0f, WHITE);
    }

    // HUD
    if (g_state == GameState::Playing || g_state == GameState::Paused) {
        
        // ============= BARRA DE PROGRESSO DE TERRAFORMACAO (TOPO) =============
        {
            float progress_w = 400.0f;
            float progress_h = 22.0f;
            float progress_x = win_w * 0.5f - progress_w * 0.5f;
            float progress_y = 12.0f;
            
            // Fundo da barra
            render_quad(progress_x - 4.0f, progress_y - 4.0f, progress_w + 8.0f, progress_h + 8.0f, 
                0.0f, 0.0f, 0.0f, 0.65f);
            
            // Barra de progresso colorida por fase
            float pct = g_terraform / 100.0f;
            float pr, pg, pb;
            std::string phase_name;
            if (g_phase == TerraPhase::Frozen) { pr = 0.4f; pg = 0.6f; pb = 0.9f; phase_name = "Congelado"; }
            else if (g_phase == TerraPhase::Warming) { pr = 0.9f; pg = 0.6f; pb = 0.3f; phase_name = "Aquecendo"; }
            else if (g_phase == TerraPhase::Thawing) { pr = 0.4f; pg = 0.8f; pb = 0.9f; phase_name = "Degelo"; }
            else if (g_phase == TerraPhase::Habitable) { pr = 0.3f; pg = 0.9f; pb = 0.4f; phase_name = "Habitavel"; }
            else { pr = 0.2f; pg = 1.0f; pb = 0.5f; phase_name = "Terraformado"; }
            
            // Barra de fundo (cinza)
            render_quad(progress_x, progress_y, progress_w, progress_h, 0.15f, 0.15f, 0.18f, 0.90f);
            
            // Barra de progresso
            render_quad(progress_x, progress_y, progress_w * pct, progress_h, pr, pg, pb, 0.95f);
            
            // Bordas pixeladas
            render_quad(progress_x, progress_y, progress_w, 2.0f, 0.4f, 0.4f, 0.45f, 0.90f);
            render_quad(progress_x, progress_y + progress_h - 2.0f, progress_w, 2.0f, 0.1f, 0.1f, 0.12f, 0.90f);
            
            // Texto de progresso
            char buf[64];
            snprintf(buf, sizeof(buf), "%d%% - %s", (int)(pct * 100.0f), phase_name.c_str());
            float tw = estimate_text_w_px(buf);
            draw_text(progress_x + progress_w * 0.5f - tw * 0.5f, progress_y + 15.0f, buf,
                kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.95f);
        }

        // ============= OBJETIVO ATUAL (logo abaixo da barra de terraformacao) =============
        // 3 estados agora (era 2): objetivo principal 1-10, trilho de legado pos-vitoria
        // 11-13, ou "Legado Completo!" quando os 13 acabam - ver objectives.h/.cpp.
        {
            std::string line1, line2;
            int idx = objectives_current_index();
            bool legacy_done = objectives_legacy_complete();
            bool main_done = objectives_all_complete();
            if (legacy_done) {
                line1 = "Legado Completo!";
                line2 = "Todos os objetivos concluidos - bom trabalho, colono.";
            } else if (main_done) {
                const ObjectiveDef& def = objective_def(idx);
                char hdr[96];
                snprintf(hdr, sizeof(hdr), "Legado %d/%d: %s", idx - kMainObjectiveCount + 1,
                         kObjectiveCount - kMainObjectiveCount, def.title);
                line1 = hdr;
                line2 = def.hint;
                if (def.related_module != Block::Air && !is_unlocked(def.related_module)) {
                    line2 += "  (" + unlock_progress_string(def.related_module) + ")";
                }
                if ((ObjectiveId)idx == ObjectiveId::BankRefinedAlloy) {
                    int have = std::max(0, g_inventory[(int)Block::RefinedAlloy]);
                    line2 += "  (" + std::to_string(have) + "/20)";
                }
            } else {
                const ObjectiveDef& def = objective_def(idx);
                char hdr[96];
                snprintf(hdr, sizeof(hdr), "Objetivo %d/%d: %s", idx + 1, kMainObjectiveCount, def.title);
                line1 = hdr;
                line2 = def.hint;
                if (def.related_module != Block::Air && !is_unlocked(def.related_module)) {
                    line2 += "  (" + unlock_progress_string(def.related_module) + ")";
                }
            }
            bool all_done = legacy_done; // reaproveitado abaixo pra cor da borda
            float tw1 = estimate_text_w_px(line1);
            float tw2 = estimate_text_w_px(line2);
            float box_w = std::max(tw1, tw2) + 30.0f;
            float box_h = 46.0f;  // um pouco mais alta: a metrica real da Consolas via raylib
                                  // e um pouco mais alta que o bitmap GDI antigo
            float box_x = win_w * 0.5f - box_w * 0.5f;
            float box_y = 46.0f;

            float border_r = all_done ? 0.30f : 0.35f;
            float border_g = all_done ? 0.90f : 0.75f;
            float border_b = all_done ? 0.50f : 0.55f;
            draw_hud_panel(box_x, box_y, box_w, box_h, border_r, border_g, border_b);

            draw_text(win_w * 0.5f - tw1 * 0.5f, box_y + 17.0f, line1,
                kColorSuccess[0], kColorSuccess[1], kColorSuccess[2], 0.95f);
            draw_text(win_w * 0.5f - tw2 * 0.5f, box_y + 35.0f, line2,
                kColorTextSecondary[0], kColorTextSecondary[1], kColorTextSecondary[2], 0.90f);
        }

        float x0 = 20.0f;
        float y0 = 50.0f;  // Ajustado para dar espaco para a barra de terraformacao
        float bar_w = 180.0f;
        float bar_h = 14.0f;
        float bar_gap = 18.0f;
        
        // Check if player is at base (usando distancia 2D)
        // Mesmo predicado do reabastecimento e do dreno do traje - ver player_in_base_complex()
        // (modules_building.h): disco da zona segura OU dentro do corredor/estufa. As 3 copias
        // manuais do calculo de distancia que existiam aqui, em update_modules e em main.cpp foram
        // substituidas por esta chamada - com o anexo existindo, elas fatalmente discordariam.
        bool at_base = player_in_base_complex();
        
        // === FUNDO TRANSPARENTE DO HUD ESQUERDO ===
        float left_panel_h = bar_gap * 11 + 100.0f;  // Altura aproximada do painel esquerdo (incluindo jetpack + traje)
        draw_hud_panel(x0 - 10.0f, y0 - 10.0f, bar_w + 20.0f, left_panel_h, 0.35f, 0.65f, 0.90f);
        
        // === LEFT PANEL: SUIT STATUS (Player) ===
        draw_text(x0, y0 - 2.0f, "TRAJE", 0.70f, 0.75f, 0.85f, 0.85f);
        y0 += 12.0f;
        
        // HP Bar (vermelho - usando cores centralizadas)
        float hp_pct = g_player.hp / 100.0f;
        bool hp_crit = hp_pct < 0.25f;
        float hp_flash = hp_crit ? (0.7f + 0.3f * std::sin(g_player.anim_frame * 6.0f)) : 1.0f;
        render_bar(x0, y0, bar_w, 16.0f, hp_pct, kColorHp[0] * hp_flash, kColorHp[1], kColorHp[2]);
        draw_text(x0 + 6.0f, y0 + 12.0f, "HP " + std::to_string(g_player.hp), 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.95f);
        
        // Suit Oxygen (verde - usando cores centralizadas)
        float o2_pct = g_player_oxygen / 100.0f;
        bool o2_crit = o2_pct < 0.25f;
        float o2_r = o2_crit ? kColorDanger[0] : kColorOxygen[0];
        float o2_g = o2_crit ? kColorDanger[1] : kColorOxygen[1];
        float o2_b = o2_crit ? kColorDanger[2] : kColorOxygen[2];
        float o2_flash = o2_crit ? (0.7f + 0.3f * std::sin(g_player.anim_frame * 6.0f)) : 1.0f;
        render_bar(x0, y0 + bar_gap, bar_w, bar_h, o2_pct, o2_r * o2_flash, o2_g * o2_flash, o2_b);
        draw_text(x0 + 6.0f, y0 + bar_gap + 11.0f, "O2 " + std::to_string((int)g_player_oxygen) + "%", 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.90f);
        
        // Suit Water (azul - usando cores centralizadas)
        float water_pct = g_player_water / 100.0f;
        bool water_crit = water_pct < 0.25f;
        float water_flash = water_crit ? (0.7f + 0.3f * std::sin(g_player.anim_frame * 6.0f)) : 1.0f;
        render_bar(x0, y0 + bar_gap * 2, bar_w, bar_h, water_pct, 
            (water_crit ? kColorDanger[0] : kColorWater[0]) * water_flash,
            (water_crit ? kColorDanger[1] : kColorWater[1]) * water_flash,
            water_crit ? kColorDanger[2] : kColorWater[2]);
        draw_text(x0 + 6.0f, y0 + bar_gap * 2 + 11.0f, "H2O " + std::to_string((int)g_player_water) + "%", 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.90f);
        
        // Suit Food (laranja - usando cores centralizadas)
        float food_pct = g_player_food / 100.0f;
        bool food_crit = food_pct < 0.25f;
        render_bar(x0, y0 + bar_gap * 3, bar_w, bar_h, food_pct, 
            food_crit ? kColorWarning[0] : kColorFood[0],
            food_crit ? kColorWarning[1] : kColorFood[1],
            food_crit ? kColorWarning[2] : kColorFood[2]);
        draw_text(x0 + 6.0f, y0 + bar_gap * 3 + 11.0f, "Comida " + std::to_string((int)g_player_food) + "%", 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.90f);
        
        // Jetpack Fuel (amarelo-laranja)
        float jet_pct = g_player.jetpack_fuel / 100.0f;
        bool jet_active = g_player.jetpack_active;
        float jet_r = jet_active ? 1.0f : 0.85f;
        float jet_g = jet_active ? 0.65f : 0.55f;
        float jet_b = 0.15f;
        float jet_pulse = jet_active ? (0.8f + 0.2f * std::sin(g_player.jetpack_flame_anim)) : 1.0f;
        render_bar(x0, y0 + bar_gap * 4, bar_w, bar_h, jet_pct, 
            jet_r * jet_pulse, jet_g * jet_pulse, jet_b);
        std::string jet_label = jet_active ? "JETPACK ATIVO" : "Jetpack " + std::to_string((int)g_player.jetpack_fuel) + "%";
        draw_text(x0 + 6.0f, y0 + bar_gap * 4 + 11.0f, jet_label,
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.90f);

        // Integridade do traje (0..100, decai continuamente longe da base - ver
        // g_suit_integrity em main.cpp) - cinza-azulado normal, pisca vermelho quando
        // critico (mesmo padrao de flash das outras barras).
        float suit_pct = g_suit_integrity / 100.0f;
        bool suit_crit = suit_pct < 0.30f;
        float suit_flash = suit_crit ? (0.7f + 0.3f * std::sin(g_player.anim_frame * 6.0f)) : 1.0f;
        float suit_r = suit_crit ? kColorDanger[0] * suit_flash : 0.55f;
        float suit_g = suit_crit ? kColorDanger[1] * suit_flash : 0.60f;
        float suit_b = suit_crit ? kColorDanger[2] * suit_flash : 0.68f;
        render_bar(x0, y0 + bar_gap * 5, bar_w, bar_h, suit_pct, suit_r, suit_g, suit_b);
        draw_text(x0 + 6.0f, y0 + bar_gap * 5 + 11.0f, "Traje " + std::to_string((int)g_suit_integrity) + "%",
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.90f);

        // === LEFT PANEL: BASE STATUS ===
        y0 += bar_gap * 6 + 15.0f;
        
        // At base indicator
        if (at_base) {
            render_quad(x0 - 5.0f, y0 - 5.0f, bar_w + 10.0f, 20.0f, 0.15f, 0.35f, 0.20f, 0.80f);
            draw_text(x0, y0 + 10.0f, "NA BASE - RECARREGANDO", 0.40f, 0.95f, 0.50f, 0.95f);
            y0 += 22.0f;
        } else {
            draw_text(x0, y0 + 10.0f, "ARMAZENAMENTO DA BASE", 0.70f, 0.75f, 0.85f, 0.85f);
            y0 += 15.0f;
        }
        
        render_bar(x0, y0, bar_w, bar_h, g_base_energy / kBaseEnergyMax, 0.95f, 0.84f, 0.25f);
        draw_text(x0 + 6.0f, y0 + 11.0f, "Energia " + std::to_string((int)g_base_energy) + "/" + std::to_string((int)kBaseEnergyMax), 0.90f, 0.90f, 0.90f, 0.90f);

        render_bar(x0, y0 + bar_gap, bar_w, bar_h, g_base_water / kBaseWaterMax, 0.25f, 0.65f, 0.95f);
        draw_text(x0 + 6.0f, y0 + bar_gap + 11.0f, "Agua " + std::to_string((int)g_base_water) + "/" + std::to_string((int)kBaseWaterMax), 0.90f, 0.90f, 0.90f, 0.90f);

        render_bar(x0, y0 + bar_gap * 2, bar_w, bar_h, g_base_oxygen / kBaseOxygenMax, 0.20f, 0.95f, 0.55f);
        draw_text(x0 + 6.0f, y0 + bar_gap * 2 + 11.0f, "Oxigenio " + std::to_string((int)g_base_oxygen) + "/" + std::to_string((int)kBaseOxygenMax), 0.90f, 0.90f, 0.90f, 0.90f);
        
        render_bar(x0, y0 + bar_gap * 3, bar_w, bar_h, g_base_food / kBaseFoodMax, 0.85f, 0.65f, 0.25f);
        draw_text(x0 + 6.0f, y0 + bar_gap * 3 + 11.0f, "Comida " + std::to_string((int)g_base_food) + "/" + std::to_string((int)kBaseFoodMax), 0.90f, 0.90f, 0.90f, 0.90f);
        
        // Integrity bar with color based on level
        float int_r = g_base_integrity > 50.0f ? 0.35f : (g_base_integrity > 25.0f ? 0.90f : 0.95f);
        float int_g = g_base_integrity > 50.0f ? 0.85f : (g_base_integrity > 25.0f ? 0.65f : 0.25f);
        float int_b = g_base_integrity > 50.0f ? 0.45f : 0.20f;
        render_bar(x0, y0 + bar_gap * 4, bar_w, bar_h, g_base_integrity / kBaseIntegrityMax, int_r, int_g, int_b);
        draw_text(x0 + 6.0f, y0 + bar_gap * 4 + 11.0f, "Integ " + std::to_string((int)g_base_integrity) + "/" + std::to_string((int)kBaseIntegrityMax), 0.90f, 0.90f, 0.90f, 0.90f);
        
        // === RIGHT PANEL: Terraforming Stats ===
        // rx0/ry0/right_panel_h vem das funcoes compartilhadas (kRightPanelBarW/BarGap/
        // TopY/H acima) - minimap.cpp ancora o minimapa nas mesmas 2 funcoes, entao os
        // dois nunca mais podem ficar dessincronizados.
        float rx0 = hud_right_panel_right_x(win_w) - kRightPanelBarW;
        float ry0 = kRightPanelTopY;

        // === FUNDO TRANSPARENTE DO HUD DIREITO ===
        float right_panel_h = kRightPanelH;
        draw_hud_panel(rx0 - 10.0f, ry0 - 10.0f, bar_w + 20.0f, right_panel_h, 0.90f, 0.65f, 0.35f);
        
        // Phase indicator
        float phase_colors[5][3] = {
            {0.4f, 0.6f, 0.9f},  // Frozen - blue
            {0.9f, 0.6f, 0.3f},  // Warming - orange
            {0.4f, 0.8f, 0.9f},  // Thawing - cyan
            {0.3f, 0.9f, 0.4f},  // Habitable - green
            {0.2f, 1.0f, 0.5f},  // Terraformed - bright green
        };
        int pi = (int)g_phase;
        render_quad(rx0, ry0, bar_w, 20.0f, phase_colors[pi][0] * 0.3f, phase_colors[pi][1] * 0.3f, phase_colors[pi][2] * 0.3f, 0.7f);
        draw_text(rx0 + 6.0f, ry0 + 15.0f, std::string("Fase: ") + phase_name(g_phase), phase_colors[pi][0], phase_colors[pi][1], phase_colors[pi][2], 0.98f);
        
        // Temperature
        ry0 += 28.0f;
        float temp_pct = clamp01((g_temperature + 60.0f) / 100.0f); // -60 to +40
        float temp_r = temp_pct;
        float temp_b = 1.0f - temp_pct;
        render_bar(rx0, ry0, bar_w, bar_h, temp_pct, temp_r, 0.3f, temp_b);
        char temp_str[32];
        snprintf(temp_str, sizeof(temp_str), "Temp %.0fC", g_temperature);
        draw_text(rx0 + 6.0f, ry0 + 11.0f, temp_str, 0.95f, 0.95f, 0.95f, 0.90f);
        
        // CO2 Level
        ry0 += bar_gap;
        render_bar(rx0, ry0, bar_w, bar_h, g_co2_level / 100.0f, 0.70f, 0.50f, 0.30f);
        draw_text(rx0 + 6.0f, ry0 + 11.0f, "CO2 " + std::to_string((int)g_co2_level) + "%", 0.90f, 0.90f, 0.90f, 0.90f);
        
        // Atmosphere
        ry0 += bar_gap;
        render_bar(rx0, ry0, bar_w, bar_h, g_atmosphere / 100.0f, 0.50f, 0.70f, 0.90f);
        draw_text(rx0 + 6.0f, ry0 + 11.0f, "Atmos " + std::to_string((int)g_atmosphere) + "%", 0.90f, 0.90f, 0.90f, 0.90f);
        
        // Terraform Progress
        ry0 += bar_gap;
        render_bar(rx0, ry0, bar_w, bar_h, g_terraform / 100.0f, 0.25f, 0.90f, 0.40f);
        draw_text(rx0 + 6.0f, ry0 + 11.0f, "Terraform " + std::to_string((int)g_terraform) + "%", 0.90f, 0.90f, 0.90f, 0.90f);
        
        // === BASE INDICATOR (com direcao 2D e seta) ===
        ry0 += bar_gap + 10.0f;
        {
            float dir_x = (float)g_base_x - g_player.pos.x;
            float dir_y = (float)g_base_y - g_player.pos.y;
            float dist_blocks = std::sqrt(dir_x * dir_x + dir_y * dir_y);
            
            // Fundo do indicador
            render_quad(rx0, ry0, bar_w, 28.0f, 0.15f, 0.18f, 0.25f, 0.75f);
            
            // Cores baseadas na distancia
            float dist_alpha = (dist_blocks > 30.0f) ? 0.95f : 0.70f;
            float dist_r = (dist_blocks > 80.0f) ? 0.95f : (at_base ? 0.35f : 0.65f);
            float dist_g = (dist_blocks > 80.0f) ? 0.55f : (at_base ? 0.85f : 0.85f);
            float dist_b = at_base ? 0.45f : 0.60f;
            
            // Texto de distancia
            char dist_str[64];
            if (at_base) {
                snprintf(dist_str, sizeof(dist_str), "BASE (Zona Segura)");
            } else {
                snprintf(dist_str, sizeof(dist_str), "Base: %.0fm", dist_blocks);
            }
            draw_text(rx0 + 6.0f, ry0 + 12.0f, dist_str, dist_r, dist_g, dist_b, dist_alpha);
            
            // Desenhar seta de direcao quando longe da base
            if (!at_base && dist_blocks > 5.0f) {
                float arrow_cx = rx0 + bar_w - 35.0f;
                float arrow_cy = ry0 + 14.0f;
                float angle = std::atan2(dir_y, dir_x);
                float arrow_size = 10.0f;
                
                // Normalizar direcao
                float nx = dir_x / dist_blocks;
                float ny = dir_y / dist_blocks;
                
                // Ponta da seta
                float tip_x = arrow_cx + nx * arrow_size;
                float tip_y = arrow_cy + ny * arrow_size;
                
                // Base da seta (perpendicular)
                float perp_x = -ny * arrow_size * 0.5f;
                float perp_y = nx * arrow_size * 0.5f;
                
                // Cor pulsante para seta
                float pulse = 0.7f + 0.3f * std::sin(g_day_time * 3.0f);
                
                rlBegin(RL_TRIANGLES);
                rlColor4f(0.3f * pulse, 0.8f * pulse, 1.0f * pulse, 0.9f);
                rlVertex2f(tip_x, tip_y);
                rlVertex2f(arrow_cx - nx * arrow_size * 0.3f + perp_x, arrow_cy - ny * arrow_size * 0.3f + perp_y);
                rlVertex2f(arrow_cx - nx * arrow_size * 0.3f - perp_x, arrow_cy - ny * arrow_size * 0.3f - perp_y);
                rlEnd();
            }
            
            // Tecla de atalho
            draw_text(rx0 + bar_w - 22.0f, ry0 + 24.0f, "[H]", 0.55f, 0.75f, 0.95f, 0.70f);
        }
        
        // === MINIMAPA ===
        if (!g_minimap.world_map_open) {
            render_minimap(win_w, win_h);
        }

        // === HOTBAR ESTILO MINICRAFT ===
        // Funcao local para desenhar slot pixelado
        auto draw_minicraft_slot = [&](float x, float y, float size, bool selected, Block block, int key_num, int count) {
            // Fundo escuro
            render_quad(x, y, size, size, 0.15f, 0.15f, 0.18f, 0.92f);
            
            // Borda pixelada (3 pixels)
            float border = 3.0f;
            // Borda clara superior/esquerda
            render_quad(x, y, size, border, 0.45f, 0.45f, 0.50f, 0.95f);
            render_quad(x, y, border, size, 0.45f, 0.45f, 0.50f, 0.95f);
            // Borda escura inferior/direita
            render_quad(x, y + size - border, size, border, 0.08f, 0.08f, 0.10f, 0.95f);
            render_quad(x + size - border, y, border, size, 0.08f, 0.08f, 0.10f, 0.95f);
            
            // Highlight se selecionado
            if (selected) {
                render_quad(x - 3.0f, y - 3.0f, size + 6.0f, size + 6.0f, 0.95f, 0.95f, 0.35f, 0.35f);
                render_quad(x + 2.0f, y + 2.0f, size - 4.0f, size - 4.0f, 0.25f, 0.25f, 0.30f, 0.90f);
            }
            
            // Icone do bloco (cubo 3D simples)
            float icon_size = size * 0.55f;
            float ix = x + (size - icon_size) * 0.5f + 2.0f;
            float iy = y + (size - icon_size) * 0.4f;
            if (g_tex_atlas != 0) {
                BlockTex bt = block_tex(block);
                int wf = ((int)std::floor(g_day_time * 4.0f)) & 3;
                if (bt.is_water) {
                    bt.top = (Tile)((int)Tile::Water0 + wf);
                    bt.side = bt.top;
                    bt.bottom = bt.top;
                }
                float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f, alpha = 1.0f;
                if (bt.uses_tint || bt.transparent) {
                    float cr, cg, cb, ca;
                    block_color(block, 128, 256, cr, cg, cb, ca);
                    if (bt.uses_tint) { tint_r = cr; tint_g = cg; tint_b = cb; }
                    if (bt.transparent) alpha = ca;
                }

                // render_quad_tex (DrawTexturePro) gerencia seu proprio bind de textura -
                // nao precisa mais de glEnable/glBindTexture/glDisable ao redor.
                render_quad_tex(ix, iy, icon_size, icon_size * 0.5f, bt.top, tint_r, tint_g, tint_b, 0.98f * alpha);
                render_quad_tex(ix, iy + icon_size * 0.5f, icon_size, icon_size * 0.5f, bt.side,
                                tint_r * 0.75f, tint_g * 0.75f, tint_b * 0.75f, 0.98f * alpha);

                // Linha de divisao
                rlSetLineWidth(1.0f);
                rlBegin(RL_LINES);
                rlColor4f(0.0f, 0.0f, 0.0f, 0.5f);
                rlVertex2f(ix, iy + icon_size * 0.5f);
                rlVertex2f(ix + icon_size, iy + icon_size * 0.5f);
                rlEnd();
            } else {
                float r, g, bl, a;
                block_color(block, 128, 256, r, g, bl, a);
                // Face superior
                render_quad(ix, iy, icon_size, icon_size * 0.5f, r, g, bl, 0.98f);
                // Face frontal (mais escura)
                render_quad(ix, iy + icon_size * 0.5f, icon_size, icon_size * 0.5f, r * 0.7f, g * 0.7f, bl * 0.7f, 0.98f);
                // Linha de divisao
                rlSetLineWidth(1.0f);
                rlBegin(RL_LINES);
                rlColor4f(0.0f, 0.0f, 0.0f, 0.5f);
                rlVertex2f(ix, iy + icon_size * 0.5f);
                rlVertex2f(ix + icon_size, iy + icon_size * 0.5f);
                rlEnd();
            }
             
            // Numero da tecla (canto superior esquerdo)
            if (key_num >= 0) {
                draw_text(x + 4.0f, y + 12.0f, std::to_string(key_num), 0.95f, 0.95f, 0.95f, 0.90f);
            }
            
            // Quantidade (canto inferior direito)
            if (count >= 0) {
                std::string cnt = std::to_string(count);
                float tw = estimate_text_w_px(cnt);
                draw_text(x + size - tw - 5.0f, y + size - 5.0f, cnt, 0.95f, 0.95f, 0.95f, 0.95f);
            }
        };
        
        // Slots de recursos (1-6)
        const Block resource_slots[] = {Block::Dirt, Block::Stone, Block::Iron, Block::Copper, Block::Coal, Block::Wood};
        const int res_count = 6;
        
        // Slots de modulos (7+) - apenas desbloqueados
        std::vector<Block> module_slots;
        if (g_unlocks.solar_unlocked) module_slots.push_back(Block::SolarPanel);
        if (g_unlocks.water_extractor_unlocked) module_slots.push_back(Block::WaterExtractor);
        if (g_unlocks.o2_generator_unlocked) module_slots.push_back(Block::OxygenGenerator);
        if (g_unlocks.greenhouse_unlocked) module_slots.push_back(Block::Greenhouse);
        if (g_unlocks.co2_factory_unlocked) module_slots.push_back(Block::CO2Factory);
        if (g_unlocks.habitat_unlocked) module_slots.push_back(Block::Habitat);
        if (g_unlocks.terraformer_unlocked) module_slots.push_back(Block::TerraformerBeacon);
        
        float slot_size = 48.0f;
        float slot_gap = 4.0f;

        // === HOTBAR UNIFICADA (centrada na base da tela) ===
        // Pistola de Laser NAO entra aqui - fica num cluster de botoes proprio, separado dos
        // materiais/modulos (ver render_weapon_cluster() logo abaixo, pedido do jogador: "a
        // pistola ficou junto dos materiais quero um menu assim", referenciando o cluster de
        // botoes redondos de arma/punho no canto da tela em jogos tipo Last Day on Earth).
        int total_slots = res_count + (int)module_slots.size();
        float total_w = total_slots * slot_size + (total_slots - 1) * slot_gap;
        float hx = win_w * 0.5f - total_w * 0.5f;
        float hy = win_h - slot_size - 12.0f;
        
        // Fundo da hotbar (painel escuro)
        draw_hud_panel(hx - 8.0f, hy - 8.0f, total_w + 16.0f, slot_size + 16.0f, 0.55f, 0.60f, 0.75f);
        
        // Funcao auxiliar para verificar se mouse esta sobre um slot
        auto mouse_over_slot = [&](float sx, float sy, float ss) -> bool {
            return g_mouse_x >= sx && g_mouse_x <= sx + ss && 
                   g_mouse_y >= sy && g_mouse_y <= sy + ss;
        };
        
        // Desenhar slots de recursos
        for (int i = 0; i < res_count; ++i) {
            float bx = hx + i * (slot_size + slot_gap);
            
            // Detectar clique do mouse no slot
            if (g_mouse_left_clicked && mouse_over_slot(bx, hy, slot_size) && g_state == GameState::Playing) {
                g_selected = resource_slots[i];
                bounce_hotbar_slot(i);
                g_mouse_left_clicked = false;
            }
            
            bool sel = (g_selected == resource_slots[i]);
            // Highlight se mouse esta sobre o slot
            bool hovered = mouse_over_slot(bx, hy, slot_size);
            int count = std::max(0, g_inventory[(int)resource_slots[i]]);
            
            // Desenhar com efeito de hover
            if (hovered && !sel) {
                render_quad(bx - 2.0f, hy - 2.0f, slot_size + 4.0f, slot_size + 4.0f, 0.55f, 0.65f, 0.85f, 0.35f);
            }
            draw_minicraft_slot(bx, hy, slot_size, sel, resource_slots[i], i + 1, count);
        }
        
        // Separador visual entre recursos e modulos
        if (!module_slots.empty()) {
            float sep_x = hx + res_count * (slot_size + slot_gap) - slot_gap * 0.5f;
            render_quad(sep_x - 1.0f, hy + 4.0f, 2.0f, slot_size - 8.0f, 0.40f, 0.40f, 0.45f, 0.80f);
        }
        
        // Desenhar slots de modulos
        for (int i = 0; i < (int)module_slots.size(); ++i) {
            float bx = hx + (res_count + i) * (slot_size + slot_gap);
            
            // Detectar clique do mouse no slot de modulo
            if (g_mouse_left_clicked && mouse_over_slot(bx, hy, slot_size) && g_state == GameState::Playing) {
                g_selected = module_slots[i];
                bounce_hotbar_slot(res_count + i);
                g_mouse_left_clicked = false;
            }
            
            bool sel = (g_selected == module_slots[i]);
            bool hovered = mouse_over_slot(bx, hy, slot_size);
            CraftCost c = get_module_cost(module_slots[i]);
            bool can_build = can_afford(c);
            int key_num = -1;
            if (i < 4) key_num = (i < 3) ? (7 + i) : 0;
            
            // Desenhar com efeito de hover
            if (hovered && !sel) {
                render_quad(bx - 2.0f, hy - 2.0f, slot_size + 4.0f, slot_size + 4.0f, 0.55f, 0.65f, 0.85f, 0.35f);
            }
            draw_minicraft_slot(bx, hy, slot_size, sel, module_slots[i], key_num, can_build ? 1 : 0);
        }

        // === CLUSTER DE ACAO (Ferramenta / Arma) - separado da hotbar de materiais/modulos =====
        // Pedido do jogador com screenshot de referencia (Last Day on Earth): botoes redondos
        // dedicados de "punho"/arma num canto da tela, nao misturados com os icones de
        // materiais. So' existe o icone da Pistola (nenhuma outra arma no jogo ainda) - o
        // botao "Ferramenta" mostra o ultimo item normal selecionado (o que volta a ficar
        // ativo ao desequipar) em vez de um icone fixo de punho (sem asset pra isso).
        {
            static Block s_last_non_weapon = Block::Dirt;
            if (g_selected != Block::LaserPistol) s_last_non_weapon = g_selected;
            bool has_pistol = g_inventory[(int)Block::LaserPistol] > 0;

            auto mouse_in_circle = [&](float cx, float cy, float r) -> bool {
                float dx = g_mouse_x - cx, dy = g_mouse_y - cy;
                return (dx * dx + dy * dy) <= r * r;
            };

            auto draw_action_button = [&](float cx, float cy, float radius, Block icon_block, bool selected) {
                if (selected) render_circle(cx, cy, radius + 5.0f, 0.95f, 0.85f, 0.30f, 0.95f);
                render_circle(cx, cy, radius + 2.0f, 0.05f, 0.05f, 0.08f, 0.95f);
                render_circle(cx, cy, radius, 0.20f, 0.22f, 0.28f, 0.95f);

                float icon_size = radius * 1.15f;
                float ix = cx - icon_size * 0.5f;
                float iy = cy - icon_size * 0.5f;
                if (icon_block == Block::LaserPistol) {
                    // Silhueta pixel-art de pistola (2 retangulos: cano + cabo, mais a ponta
                    // brilhante) em vez de reaproveitar um icone de bloco generico que nao lia
                    // como arma nenhuma - unico jeito de ter uma forma reconhecivel sem asset
                    // novo de textura.
                    float br_w = icon_size * 0.80f, br_h = icon_size * 0.24f;
                    float br_x = cx - br_w * 0.5f - icon_size * 0.06f, br_y = cy - icon_size * 0.16f;
                    render_quad(br_x, br_y, br_w, br_h, 0.16f, 0.18f, 0.22f, 0.95f);

                    float gr_w = icon_size * 0.24f, gr_h = icon_size * 0.44f;
                    float gr_x = br_x + icon_size * 0.10f, gr_y = br_y + br_h * 0.55f;
                    render_quad(gr_x, gr_y, gr_w, gr_h, 0.14f, 0.15f, 0.18f, 0.95f);

                    float tip_w = icon_size * 0.16f, tip_h = br_h * 0.7f;
                    render_quad(br_x + br_w - tip_w * 0.4f, br_y + br_h * 0.15f, tip_w, tip_h,
                                0.35f, 0.90f, 1.0f, 0.95f);
                } else if (g_tex_atlas != 0) {
                    BlockTex bt = block_tex(icon_block);
                    float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f, alpha = 1.0f;
                    if (bt.uses_tint || bt.transparent) {
                        float cr, cg, cb, ca;
                        block_color(icon_block, 128, 256, cr, cg, cb, ca);
                        if (bt.uses_tint) { tint_r = cr; tint_g = cg; tint_b = cb; }
                        if (bt.transparent) alpha = ca;
                    }
                    render_quad_tex(ix, iy, icon_size, icon_size * 0.5f, bt.top, tint_r, tint_g, tint_b, alpha);
                    render_quad_tex(ix, iy + icon_size * 0.5f, icon_size, icon_size * 0.5f, bt.side,
                                    tint_r * 0.75f, tint_g * 0.75f, tint_b * 0.75f, alpha);
                } else {
                    float r, g, bl, a;
                    block_color(icon_block, 128, 256, r, g, bl, a);
                    render_quad(ix, iy, icon_size, icon_size * 0.5f, r, g, bl, 0.95f);
                    render_quad(ix, iy + icon_size * 0.5f, icon_size, icon_size * 0.5f, r * 0.7f, g * 0.7f, bl * 0.7f, 0.95f);
                }
            };

            float btn_radius = 30.0f;
            float cluster_cx = hud_right_panel_right_x(win_w) - btn_radius - 6.0f;
            float gun_cy = hud_right_panel_bottom_y() + 240.0f + btn_radius;

            // UM botao so' (a arma), sem rotulos de texto. Pedido do jogador: "nao precisa ter textos
            // ali, e nao precisa ter o menu ferramentas, basta desmarcar a arma para usar ferramentas".
            // O botao "Ferramenta" era redundante - desmarcar a arma ja devolve o ultimo item normal.
            g_hud_pointer_over_button = false;
            if (has_pistol) {
                bool gun_selected = (g_selected == Block::LaserPistol);
                bool over = mouse_in_circle(cluster_cx, gun_cy, btn_radius);
                // Marca ANTES de tratar o clique e independente dele: o tiro usa o botao SEGURADO
                // (IsMouseButtonDown), entao o que precisa ser bloqueado e' o cursor estar sobre o
                // botao, nao o evento de clique.
                if (over && g_state == GameState::Playing) g_hud_pointer_over_button = true;
                if (g_mouse_left_clicked && over && g_state == GameState::Playing) {
                    g_selected = gun_selected ? s_last_non_weapon : Block::LaserPistol;
                    g_mouse_left_clicked = false;
                }
                draw_action_button(cluster_cx, gun_cy, btn_radius, Block::LaserPistol, gun_selected);
            }
        }

        // Info do item selecionado (acima da hotbar)
        {
            std::string s = std::string(block_name(g_selected));
            if (is_module(g_selected)) {
                if (!is_unlocked(g_selected)) {
                    s += " [" + unlock_progress_string(g_selected) + "]";
                } else {
                    s += " - " + cost_string(get_module_cost(g_selected));
                }
            } else {
                s += " x" + std::to_string(std::max(0, g_inventory[(int)g_selected]));
            }
            float tw = estimate_text_w_px(s);
            // Fundo do texto
            render_quad(win_w * 0.5f - tw * 0.5f - 8.0f, hy - 26.0f, tw + 16.0f, 18.0f, 0.0f, 0.0f, 0.0f, 0.65f);
            draw_text(win_w * 0.5f - tw * 0.5f, hy - 12.0f, s, 0.95f, 0.95f, 0.95f, 0.95f);
        }

        // Popups de coleta (feedback acima da hotbar)
        if (!g_collect_popups.empty()) {
            float base_x = win_w * 0.5f;
            float base_y = hy - 42.0f;
            float line_h = 18.0f;

            int n = (int)g_collect_popups.size();
            int max_show = 6;
            int start = std::max(0, n - max_show);

            for (int idx = n - 1; idx >= start; --idx) {
                int stack = (n - 1) - idx;
                const CollectPopup& p = g_collect_popups[idx];

                float alpha = std::min(1.0f, p.life / 0.45f);
                float tw = estimate_text_w_px(p.text);

                bool draw_icon = (g_tex_atlas != 0 && p.item != Block::Air);
                float icon_sz = 16.0f;
                float pad_x = 10.0f;
                float gap = 6.0f;
                float box_w = tw + pad_x * 2.0f + (draw_icon ? (icon_sz + gap) : 0.0f);

                float px = base_x + p.x - box_w * 0.5f;
                float py = base_y + p.y - (float)stack * line_h;

                // Fundo + faixa colorida
                render_quad(px, py - 14.0f, box_w, 18.0f, 0.0f, 0.0f, 0.0f, 0.58f * alpha);
                render_quad(px, py - 14.0f, 3.0f, 18.0f, p.r, p.g, p.b, 0.85f * alpha);

                float tx = px + pad_x;
                if (draw_icon) {
                    BlockTex bt = block_tex(p.item);
                    int wf = ((int)std::floor(g_day_time * 4.0f)) & 3;
                    if (bt.is_water) {
                        bt.top = (Tile)((int)Tile::Water0 + wf);
                        bt.side = bt.top;
                        bt.bottom = bt.top;
                    }

                    float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f, icon_a = 1.0f;
                    if (bt.uses_tint || bt.transparent) {
                        float cr, cg, cb, ca;
                        block_color(p.item, 128, 256, cr, cg, cb, ca);
                        if (bt.uses_tint) { tint_r = cr; tint_g = cg; tint_b = cb; }
                        if (bt.transparent) icon_a = ca;
                    }

                    // render_quad_tex (DrawTexturePro) gerencia seu proprio bind de textura.
                    render_quad_tex(tx, py - 12.0f, icon_sz, icon_sz, bt.top, tint_r, tint_g, tint_b, 0.98f * alpha * icon_a);

                    tx += icon_sz + gap;
                }

                draw_text(tx, py, p.text, p.r, p.g, p.b, 0.95f * alpha);
            }
        }

        // Target info
        if (g_has_target) {
            Block b = g_world->get(g_target_x, g_target_y);
            if (b != Block::Air) {
                float rr = g_target_in_range ? 0.85f : 0.95f;
                float gg = g_target_in_range ? 0.95f : 0.35f;
                draw_text(20.0f, win_h - 100.0f, std::string("Alvo: ") + block_name(b), rr, gg, 0.25f, 0.95f);

                // Upgrade de modulo (tecla R) - so mostra a dica quando mirando um modulo
                // ja construido em alcance (mesmo padrao de "Alvo:" acima).
                if (g_target_in_range && is_module(b)) {
                    for (const Module& m : g_modules) {
                        if (m.x != g_target_x || m.y != g_target_y) continue;
                        if (m.upgraded) {
                            draw_text(20.0f, win_h - 82.0f, "Aprimorado", 0.55f, 0.90f, 0.55f, 0.90f);
                        } else {
                            CraftCost uc = get_module_upgrade_cost(b);
                            bool affordable = can_afford(uc);
                            draw_text(20.0f, win_h - 82.0f, "[R] Aprimorar (" + module_cost_string(uc) + ")",
                                affordable ? 0.85f : 0.95f, affordable ? 0.95f : 0.35f, 0.25f, 0.90f);
                        }
                        break;
                    }
                }
            }
        }

        // === PROMPT DE PORTA (exterior <-> interior) ===
        // Centralizado e acima do hotbar - a transicao e' por tecla (nao por proximidade), entao sem
        // um prompt visivel a porta seria indescobrivel. interior_prompt() devolve nullptr quando
        // nao ha porta por perto, entao isto nao polui a tela.
        if (const char* prompt = interior_prompt()) {
            std::string txt(prompt);
            float tw = estimate_text_w_px(txt);
            float px = ((float)win_w - tw) * 0.5f;
            float py = win_h - 150.0f;
            render_quad(px - 12.0f, py - 8.0f, tw + 24.0f, 30.0f, 0.05f, 0.06f, 0.08f, 0.72f);
            draw_text(px, py, txt, 0.95f, 0.88f, 0.45f, 1.0f);
        }

        // Debug info (3D)
        if (g_debug) {
            char buf[256];
            snprintf(buf, sizeof(buf), "XZ: %.1f,%.1f  Y: %.2f  Chao: %.1f  %s  Mat: %s  VelXY: %.2f",
                g_player.pos.x, g_player.pos.y, g_player.pos_y, g_player.ground_height,
                g_player.on_ground ? "NO CHAO" : "NO AR",
                g_physics.terrain_name.c_str(),
                vec2_length(g_player.vel));
            draw_text(20.0f, win_h - 136.0f, buf, 0.85f, 0.85f, 0.90f, 0.95f);
             
            snprintf(buf, sizeof(buf), "VelY: %.2f  Normal:(%.2f, %.2f, %.2f)  Coy:%.2f Buf:%.2f  %s%s%s",
                g_player.vel_y,
                g_physics.ground_normal.x, g_physics.ground_normal.y, g_physics.ground_normal.z,
                g_physics.coyote_timer, g_physics.jump_buffer_timer,
                g_physics.sliding ? "SLIDE " : "",
                g_physics.stepped ? "STEP " : "",
                (g_physics.hit_x || g_physics.hit_z) ? "HIT" : "");
            draw_text(20.0f, win_h - 118.0f, buf, 0.85f, 0.85f, 0.90f, 0.95f);

            snprintf(buf, sizeof(buf), "Cam: yaw=%.0f pitch=%.0f dist=%.1f mode=%s(%s) occ=%.2f encl=%.2f",
                g_camera.yaw, g_camera.pitch, g_camera.distance,
                camera_mode_name(g_camera_mode), g_camera_mode_reason.c_str(),
                g_camera_obstruction, g_camera_enclosed);
            draw_text(20.0f, win_h - 100.0f, buf, 0.85f, 0.85f, 0.90f, 0.95f);

            snprintf(buf, sizeof(buf), "Phys: dt=%.4f alpha=%.2f cam_hide=%.2fs cam_rays=%d",
                g_physics_cfg.fixed_timestep, g_physics.alpha, g_camera_hidden_time, g_camera_debug_ray_count);
            draw_text(20.0f, win_h - 82.0f, buf, 0.85f, 0.85f, 0.90f, 0.95f);
        }
    }

    // Toast notifications
    if (g_toast_time > 0.0f && !g_toast.empty()) {
        float toast_alpha = std::min(1.0f, g_toast_time);
        float tw = estimate_text_w_px(g_toast);
        render_quad(win_w * 0.5f - tw * 0.5f - 10.0f, 50.0f, tw + 20.0f, 28.0f, 0.0f, 0.0f, 0.0f, 0.6f * toast_alpha);
        draw_text(win_w * 0.5f - tw * 0.5f, 70.0f, g_toast, 0.95f, 0.95f, 0.50f, toast_alpha);
    }
    
    // ============= FEEDBACK VISUAL APRIMORADO =============
    
    // Flash vermelho (erro/dano)
    if (g_screen_flash_red > 0.0f) {
        float alpha = g_screen_flash_red * 0.4f;
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 
            kColorDanger[0], kColorDanger[1], kColorDanger[2], alpha);
    }
    
    // Flash verde (sucesso)
    if (g_screen_flash_green > 0.0f) {
        float alpha = g_screen_flash_green * 0.35f;
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 
            kColorSuccess[0], kColorSuccess[1], kColorSuccess[2], alpha);
    }
    
    // Popup grande de desbloqueio (conquista)
    if (g_unlock_popup_timer > 0.0f) {
        float alpha = std::min(1.0f, g_unlock_popup_timer);
        float popup_w = 380.0f;
        float popup_h = 100.0f;
        float px = win_w * 0.5f - popup_w * 0.5f;
        float py = win_h * 0.25f;
        
        // Fundo com borda verde
        render_quad(px - 4.0f, py - 4.0f, popup_w + 8.0f, popup_h + 8.0f, 
            kColorSuccess[0], kColorSuccess[1], kColorSuccess[2], 0.9f * alpha);
        render_quad(px, py, popup_w, popup_h, 0.05f, 0.08f, 0.05f, 0.95f * alpha);
        
        // Titulo
        float tw = estimate_text_w_px(g_unlock_popup_text);
        draw_text(win_w * 0.5f - tw * 0.5f, py + 35.0f, g_unlock_popup_text, 
            kColorSuccess[0], kColorSuccess[1], kColorSuccess[2], alpha);
        
        // Subtitulo
        float sw = estimate_text_w_px(g_unlock_popup_subtitle);
        draw_text(win_w * 0.5f - sw * 0.5f, py + 65.0f, g_unlock_popup_subtitle, 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], alpha * 0.9f);
    }
    
    // Dica de onboarding
    if (g_onboarding.tip_timer > 0.0f && !g_onboarding.current_tip.empty()) {
        float alpha = std::min(1.0f, g_onboarding.tip_timer);
        float tw = estimate_text_w_px(g_onboarding.current_tip);
        float tip_y = win_h * 0.15f;
        
        // Fundo azul suave
        render_quad(win_w * 0.5f - tw * 0.5f - 15.0f, tip_y - 10.0f, tw + 30.0f, 35.0f, 
            kColorSelection[0] * 0.3f, kColorSelection[1] * 0.3f, kColorSelection[2] * 0.3f, 0.85f * alpha);
        render_quad(win_w * 0.5f - tw * 0.5f - 15.0f, tip_y - 10.0f, 4.0f, 35.0f, 
            kColorSelection[0], kColorSelection[1], kColorSelection[2], 0.95f * alpha);
        
        draw_text(win_w * 0.5f - tw * 0.5f, tip_y + 10.0f, g_onboarding.current_tip, 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], alpha);
    }

}
