#include "ui_menu.h"

#include "raylib_platform.h"
#include "math_core.h"
#include "blocks.h"
#include "game_state.h"
#include "world.h"
#include "player_physics.h"
#include "modules_building.h"
#include "items_particles.h"
#include "save_load.h"
#include "font.h"
#include "render_primitives.h"
#include "camera.h"
#include "lighting.h"
#include "audio.h"
#include "config_io.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// ============= Menu System (Paused / Main Menu / Dead / Settings) =============
// Extracted verbatim from main.cpp's render_world()/update_game() - see ui_menu.h for the
// full extraction-stage description.
//
// g_alerts/g_day_time/g_shooting_stars are owned by main.cpp (already non-static there for
// other extracted modules) - this file gets them via its own local extern declarations, same
// pattern as g_alerts in modules_building.cpp/player_physics.cpp and g_day_time in
// minimap.cpp/sky.cpp/lighting.cpp/ui_hud.cpp/modules_building.cpp/save_load.cpp.
//
// g_cam_pos/g_mouse_x/g_mouse_y/g_mouse_left_clicked were already non-static in main.cpp
// before this stage (save_load.cpp and ui_hud.cpp needed them respectively) - this file just
// adds its own extern declarations for the same globals, same pattern as those files.
//
// g_settings/g_settings_selection/g_pause_selection/g_menu_selection/g_quit lost "static" in
// main.cpp for this stage: they used to only be touched by main.cpp's own menu render/input
// code (now moved here), so this is the first cross-translation-unit use of each - same
// reasoning as every other "lost static" comment in this codebase's extraction stages.
extern std::vector<Alert> g_alerts;
extern float g_day_time;
extern std::vector<ShootingStar> g_shooting_stars;
extern Vec2 g_cam_pos;
extern int g_mouse_x;
extern int g_mouse_y;
extern bool g_mouse_left_clicked;
extern GameSettings g_settings;
extern int g_settings_selection;
extern int g_pause_selection;
extern int g_menu_selection;
extern bool g_quit;

// kWorldWidth/kWorldHeight agora vem de world.h, kDayLength de game_state.h (eram copias
// locais aqui - ver world.h/game_state.h pra detalhes da consolidacao).

// kColorPanelBorder: same "own copy of a compile-time-ish literal" reasoning as above - this
// is an internal-linkage (static const float[]) color-table entry defined in main.cpp
// ("SISTEMA DE CORES CENTRALIZADO"), so this file keeps its own identical copy of just the one
// entry the moved Settings-menu code actually uses, rather than changing main.cpp's existing
// linkage for a purely cosmetic constant table - same pattern as ui_hud.cpp's kColorHp etc.
static const float kColorPanelBorder[] = {0.30f, 0.55f, 0.85f, 0.90f}; // Borda azul

// ============= Rendering =============
// Extracted verbatim from main.cpp's render_world() (original lines ~1715-1974): the
// darkened-background overlay, the inline draw_mc_button()/mouse_in_rect() lambdas, the
// Paused-menu button list + hardcoded controls text, the Main-Menu button list, the
// death-screen text, and the Settings panel (graphics/audio/gameplay rows). Only the
// projection/HUD-adjacent overlay drawing moved here; the build menu and the victory/alerts/
// world-map overlay that render_world() draws right after this block stay inline in main.cpp.
void render_menus(int win_w, int win_h) {
    if (g_state == GameState::Paused || g_state == GameState::Menu) {
        // Fundo escurecido
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.0f, 0.0f, 0.0f, g_state == GameState::Paused ? 0.55f : 0.70f);

        // Funcao para desenhar botao estilo Minecraft
        auto draw_mc_button = [&](float x, float y, float w, float h, const std::string& text, bool hovered, bool enabled = true) {
            // Cores base do botao Minecraft
            float bg_r = enabled ? 0.45f : 0.30f;
            float bg_g = enabled ? 0.45f : 0.30f;
            float bg_b = enabled ? 0.50f : 0.35f;

            if (hovered && enabled) {
                bg_r = 0.55f; bg_g = 0.65f; bg_b = 0.85f;  // Highlight azul
            }

            // Sombra (borda inferior/direita escura)
            render_quad(x + 3.0f, y + 3.0f, w, h, 0.05f, 0.05f, 0.08f, 0.95f);

            // Corpo do botao
            render_quad(x, y, w, h, bg_r * 0.7f, bg_g * 0.7f, bg_b * 0.7f, 0.98f);

            // Borda superior clara (3D effect)
            render_quad(x, y, w, 3.0f, bg_r * 1.3f, bg_g * 1.3f, bg_b * 1.3f, 0.95f);
            render_quad(x, y, 3.0f, h, bg_r * 1.3f, bg_g * 1.3f, bg_b * 1.3f, 0.95f);

            // Borda inferior escura
            render_quad(x, y + h - 3.0f, w, 3.0f, bg_r * 0.4f, bg_g * 0.4f, bg_b * 0.4f, 0.95f);
            render_quad(x + w - 3.0f, y, 3.0f, h, bg_r * 0.4f, bg_g * 0.4f, bg_b * 0.4f, 0.95f);

            // Interior do botao (gradiente sutil)
            render_quad(x + 3.0f, y + 3.0f, w - 6.0f, h - 6.0f, bg_r, bg_g, bg_b, 0.98f);

            // Texto centralizado
            float text_w = estimate_text_w_px(text);
            float text_r = enabled ? 1.0f : 0.55f;
            float text_g = enabled ? 1.0f : 0.55f;
            float text_b = enabled ? 1.0f : 0.55f;
            if (hovered && enabled) {
                text_r = 1.0f; text_g = 1.0f; text_b = 0.65f;  // Texto amarelado quando hover
            }
            draw_text(x + (w - text_w) * 0.5f, y + h * 0.5f + 5.0f, text, text_r, text_g, text_b, 1.0f);
        };

        // Verifica se mouse esta sobre um retangulo
        auto mouse_in_rect = [&](float x, float y, float w, float h) -> bool {
            return g_mouse_x >= x && g_mouse_x <= x + w && g_mouse_y >= y && g_mouse_y <= y + h;
        };

        if (g_state == GameState::Paused) {
            // === MENU DE PAUSA ESTILO MINECRAFT ===
            float btn_w = 280.0f;
            float btn_h = 40.0f;
            float btn_gap = 8.0f;
            float start_y = win_h * 0.22f;
            float center_x = win_w * 0.5f - btn_w * 0.5f;

            // Titulo "Jogo Pausado"
            std::string title = "Jogo Pausado";
            float title_w = estimate_text_w_px(title);
            draw_text(win_w * 0.5f - title_w * 0.5f, start_y, title, 1.0f, 1.0f, 1.0f, 1.0f);

            start_y += 50.0f;

            // Botoes do menu de pausa
            struct PauseButton { std::string text; int id; };
            PauseButton buttons[] = {
                {"Continuar", 0},
                {"Salvar Jogo", 1},
                {"Carregar Jogo", 2},
                {"Configuracoes", 3},
                {"Menu Principal", 4}
            };

            g_pause_selection = -1;  // Reset selection

            for (int i = 0; i < 5; ++i) {
                float by = start_y + i * (btn_h + btn_gap);
                bool hovered = mouse_in_rect(center_x, by, btn_w, btn_h);
                if (hovered) g_pause_selection = buttons[i].id;
                draw_mc_button(center_x, by, btn_w, btn_h, buttons[i].text, hovered);
            }

            // Separador
            start_y += 5 * (btn_h + btn_gap) + 15.0f;
            render_quad(center_x, start_y, btn_w, 2.0f, 0.5f, 0.5f, 0.55f, 0.6f);

            // Controles (texto menor)
            start_y += 15.0f;
            draw_text(center_x, start_y, "CONTROLES:", 0.75f, 0.80f, 0.90f, 0.90f);
            start_y += 22.0f;
            draw_text(center_x, start_y, "WASD - Mover", 0.65f, 0.65f, 0.70f, 0.85f);
            start_y += 18.0f;
            draw_text(center_x, start_y, "Espaco - Pular  |  Shift - Correr", 0.65f, 0.65f, 0.70f, 0.85f);
            start_y += 18.0f;
            draw_text(center_x, start_y, "Segurar Scroll - Rotacionar Camera", 0.65f, 0.65f, 0.70f, 0.85f);
            start_y += 18.0f;
            draw_text(center_x, start_y, "Rolar Scroll - Zoom  |  1-9 - Selecionar Item", 0.65f, 0.65f, 0.70f, 0.85f);
            start_y += 18.0f;
            draw_text(center_x, start_y, "Esquerdo - Minerar  |  Direito - Construir", 0.65f, 0.65f, 0.70f, 0.85f);
            start_y += 18.0f;
            draw_text(center_x, start_y, "P - Fabricar Pistola de Laser", 0.65f, 0.65f, 0.70f, 0.85f);
            start_y += 18.0f;
            draw_text(center_x, start_y, "Clique no icone dela na hotbar - equipar/desequipar", 0.65f, 0.65f, 0.70f, 0.85f);

        } else if (g_state == GameState::Menu) {
            // === MENU PRINCIPAL ESTILO MINECRAFT ===
            float btn_w = 320.0f;
            float btn_h = 45.0f;
            float btn_gap = 10.0f;

            // Logo/Titulo grande
            std::string title = "TERRAFORMER";
            float title_w = estimate_text_w_px(title);
            // Sombra do titulo
            draw_text(win_w * 0.5f - title_w * 0.5f + 3.0f, win_h * 0.18f + 3.0f, title, 0.15f, 0.15f, 0.15f, 0.9f);
            // Titulo principal
            draw_text(win_w * 0.5f - title_w * 0.5f, win_h * 0.18f, title, 0.95f, 0.85f, 0.25f, 1.0f);

            // Subtitulo
            std::string subtitle = "Colonize. Construa. Terraforma.";
            float sub_w = estimate_text_w_px(subtitle);
            draw_text(win_w * 0.5f - sub_w * 0.5f, win_h * 0.18f + 35.0f, subtitle, 0.70f, 0.75f, 0.80f, 0.90f);

            float start_y = win_h * 0.38f;
            float center_x = win_w * 0.5f - btn_w * 0.5f;

            // Botoes do menu principal
            struct MenuButton { std::string text; int id; };
            MenuButton buttons[] = {
                {"Novo Jogo", 0},
                {"Carregar Jogo", 1},
                {"Sair", 2}
            };

            g_menu_selection = -1;  // Reset selection

            for (int i = 0; i < 3; ++i) {
                float by = start_y + i * (btn_h + btn_gap);
                bool hovered = mouse_in_rect(center_x, by, btn_w, btn_h);
                if (hovered) g_menu_selection = buttons[i].id;
                draw_mc_button(center_x, by, btn_w, btn_h, buttons[i].text, hovered);
            }

            // Versao no canto
            draw_text(10.0f, win_h - 20.0f, "TerraFormer v1.0", 0.5f, 0.5f, 0.55f, 0.7f);
        }
    }

    // Death screen
    if (g_state == GameState::Dead) {
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.15f, 0.0f, 0.0f, 0.75f);
        std::string title = "VOCE MORREU";
        draw_text(win_w * 0.5f - estimate_text_w_px(title) * 0.5f, win_h * 0.35f, title, 0.95f, 0.25f, 0.25f, 0.98f);
        draw_text(win_w * 0.5f - estimate_text_w_px(g_toast) * 0.5f, win_h * 0.35f + 40.0f, g_toast, 0.90f, 0.90f, 0.90f, 0.95f);
        draw_text(win_w * 0.5f - 100.0f, win_h * 0.35f + 90.0f, "Enter - Novo Jogo", 0.90f, 0.90f, 0.90f, 0.95f);
        draw_text(win_w * 0.5f - 100.0f, win_h * 0.35f + 115.0f, "Esc - Menu Principal", 0.90f, 0.90f, 0.90f, 0.95f);
    }

    // Settings menu
    if (g_state == GameState::Settings) {
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.0f, 0.0f, 0.0f, 0.85f);

        float menu_w = 480.0f;
        // Altura ideal (cabe as 13 opcoes + 2 cabecalhos de secao confortavelmente) - mas
        // NUNCA passa da janela real (win_h - 20 de folga): sem esse teto, numa janela
        // menor (o jogo permite ate 360px de altura, ver SetWindowMinSize) o painel
        // simplesmente vazava pra fora da tela, cortando "Voltar" e as instrucoes no rodape.
        float menu_h = std::min(560.0f, (float)win_h - 20.0f);
        float menu_x = win_w * 0.5f - menu_w * 0.5f;
        float menu_y = std::max(10.0f, win_h * 0.5f - menu_h * 0.5f);

        // Background panel
        render_quad(menu_x, menu_y, menu_w, menu_h, 0.08f, 0.10f, 0.14f, 0.98f);
        render_quad(menu_x, menu_y, menu_w, 4.0f, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        render_quad(menu_x, menu_y + menu_h - 4.0f, menu_w, 4.0f, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        render_quad(menu_x, menu_y, 4.0f, menu_h, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        render_quad(menu_x + menu_w - 4.0f, menu_y, 4.0f, menu_h, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);

        std::string title = "CONFIGURACOES";
        draw_text(win_w * 0.5f - estimate_text_w_px(title) * 0.5f, menu_y + 25.0f, title, 0.95f, 0.95f, 0.95f, 1.0f);

        // row_h reduzido de 40 pra 30 (13 opcoes + 2 cabecalhos nao cabiam confortavelmente
        // no valor antigo sem o painel ficar quase do tamanho da janela inteira) - todo o
        // resto do bloco usa "row_y += row_h"/"row_h * 0.x" pra empilhar as linhas, entao
        // um unico ajuste aqui reflete em tudo.
        float row_y = menu_y + 55.0f;
        float row_h = 30.0f;
        float label_x = menu_x + 30.0f;
        float value_x = menu_x + 280.0f;

        // Opcao: Sensibilidade da Camera
        bool sel0 = (g_settings_selection == 0);
        if (sel0) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Sensibilidade Camera", sel0 ? 1.0f : 0.8f, sel0 ? 1.0f : 0.8f, sel0 ? 1.0f : 0.8f, 1.0f);
        char sens_buf[32];
        snprintf(sens_buf, sizeof(sens_buf), "< %.2f >", g_settings.camera_sensitivity);
        draw_text(value_x, row_y + 5.0f, sens_buf, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;

        // Opcao: Inverter Y
        bool sel1 = (g_settings_selection == 1);
        if (sel1) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Inverter Eixo Y", sel1 ? 1.0f : 0.8f, sel1 ? 1.0f : 0.8f, sel1 ? 1.0f : 0.8f, 1.0f);
        const char* invert_str = g_settings.invert_y ? "Sim" : "Nao";
        draw_text(value_x, row_y + 5.0f, invert_str, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;

        // Opcao: Brilho
        bool sel2 = (g_settings_selection == 2);
        if (sel2) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Brilho", sel2 ? 1.0f : 0.8f, sel2 ? 1.0f : 0.8f, sel2 ? 1.0f : 0.8f, 1.0f);
        char bright_buf[32];
        snprintf(bright_buf, sizeof(bright_buf), "< %.0f%% >", g_settings.brightness * 100.0f);
        draw_text(value_x, row_y + 5.0f, bright_buf, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;

        // Opcao: Escala UI
        bool sel3 = (g_settings_selection == 3);
        if (sel3) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Escala UI", sel3 ? 1.0f : 0.8f, sel3 ? 1.0f : 0.8f, sel3 ? 1.0f : 0.8f, 1.0f);
        char scale_buf[32];
        snprintf(scale_buf, sizeof(scale_buf), "< %.0f%% >", g_settings.ui_scale * 100.0f);
        draw_text(value_x, row_y + 5.0f, scale_buf, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;

        // === OPCOES DE ILUMINACAO (RTX FAKE) ===
        draw_text(label_x, row_y + 5.0f, "--- Iluminacao RTX ---", 0.9f, 0.75f, 0.3f, 0.9f);
        row_y += row_h * 0.7f;

        // Opcao: Iluminacao Ativada
        bool sel4 = (g_settings_selection == 4);
        if (sel4) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Iluminacao 2D", sel4 ? 1.0f : 0.8f, sel4 ? 1.0f : 0.8f, sel4 ? 1.0f : 0.8f, 1.0f);
        const char* light_str = g_lighting.enabled ? "Ativada" : "Desativada";
        draw_text(value_x, row_y + 5.0f, light_str, g_lighting.enabled ? 0.3f : 0.8f, g_lighting.enabled ? 0.9f : 0.4f, 0.3f, 1.0f);
        row_y += row_h;

        // Opcao: Sombras
        bool sel5 = (g_settings_selection == 5);
        if (sel5) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Sombras 2D", sel5 ? 1.0f : 0.8f, sel5 ? 1.0f : 0.8f, sel5 ? 1.0f : 0.8f, 1.0f);
        const char* shadow_str = g_lighting.shadows_enabled ? "Ativadas" : "Desativadas";
        draw_text(value_x, row_y + 5.0f, shadow_str, g_lighting.shadows_enabled ? 0.3f : 0.8f, g_lighting.shadows_enabled ? 0.9f : 0.4f, 0.3f, 1.0f);
        row_y += row_h;

        // Opcao: Bloom
        bool sel6 = (g_settings_selection == 6);
        if (sel6) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Bloom/Glow", sel6 ? 1.0f : 0.8f, sel6 ? 1.0f : 0.8f, sel6 ? 1.0f : 0.8f, 1.0f);
        char bloom_buf[32];
        snprintf(bloom_buf, sizeof(bloom_buf), "< %.0f%% >", g_lighting.bloom_intensity * 100.0f);
        draw_text(value_x, row_y + 5.0f, bloom_buf, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;

        // Opcao: Vinheta
        bool sel7 = (g_settings_selection == 7);
        if (sel7) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Vinheta", sel7 ? 1.0f : 0.8f, sel7 ? 1.0f : 0.8f, sel7 ? 1.0f : 0.8f, 1.0f);
        char vignette_buf[32];
        snprintf(vignette_buf, sizeof(vignette_buf), "< %.0f%% >", g_lighting.vignette_intensity * 100.0f);
        draw_text(value_x, row_y + 5.0f, vignette_buf, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;

        // === OPCOES DE AUDIO ===
        draw_text(label_x, row_y + 5.0f, "--- Audio ---", 0.9f, 0.75f, 0.3f, 0.9f);
        row_y += row_h * 0.7f;

        // Opcao: Musica Ativada
        bool sel8 = (g_settings_selection == 8);
        if (sel8) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Musica", sel8 ? 1.0f : 0.8f, sel8 ? 1.0f : 0.8f, sel8 ? 1.0f : 0.8f, 1.0f);
        const char* music_on_str = g_settings.music_enabled ? "Ligada" : "Desligada";
        draw_text(value_x, row_y + 5.0f, music_on_str, g_settings.music_enabled ? 0.3f : 0.8f, g_settings.music_enabled ? 0.9f : 0.4f, 0.3f, 1.0f);
        row_y += row_h;

        // Opcao: Volume da Musica
        bool sel9 = (g_settings_selection == 9);
        if (sel9) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Volume Musica", sel9 ? 1.0f : 0.8f, sel9 ? 1.0f : 0.8f, sel9 ? 1.0f : 0.8f, 1.0f);
        char music_vol_buf[32];
        snprintf(music_vol_buf, sizeof(music_vol_buf), "< %.0f%% >", g_settings.music_volume * 100.0f);
        draw_text(value_x, row_y + 5.0f, music_vol_buf, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;

        // Opcao: Efeitos Sonoros Ativados
        bool sel10 = (g_settings_selection == 10);
        if (sel10) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Efeitos Sonoros", sel10 ? 1.0f : 0.8f, sel10 ? 1.0f : 0.8f, sel10 ? 1.0f : 0.8f, 1.0f);
        const char* sfx_on_str = g_settings.sfx_enabled ? "Ligados" : "Desligados";
        draw_text(value_x, row_y + 5.0f, sfx_on_str, g_settings.sfx_enabled ? 0.3f : 0.8f, g_settings.sfx_enabled ? 0.9f : 0.4f, 0.3f, 1.0f);
        row_y += row_h;

        // Opcao: Volume dos Efeitos
        bool sel11 = (g_settings_selection == 11);
        if (sel11) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Volume Efeitos", sel11 ? 1.0f : 0.8f, sel11 ? 1.0f : 0.8f, sel11 ? 1.0f : 0.8f, 1.0f);
        char sfx_vol_buf[32];
        snprintf(sfx_vol_buf, sizeof(sfx_vol_buf), "< %.0f%% >", g_settings.sfx_volume * 100.0f);
        draw_text(value_x, row_y + 5.0f, sfx_vol_buf, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;

        // Opcao: Voltar
        bool sel12 = (g_settings_selection == 12);
        if (sel12) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Voltar", sel12 ? 1.0f : 0.8f, sel12 ? 1.0f : 0.8f, sel12 ? 1.0f : 0.8f, 1.0f);

        // Instrucoes
        draw_text(menu_x + 30.0f, menu_y + menu_h - 40.0f, "W/S: Navegar | A/D: Ajustar | Esc/Enter: Voltar | F3: Debug Lightmap", 0.6f, 0.65f, 0.70f, 0.9f);
    }
}

// ============= Input =============
// Extracted verbatim from main.cpp's update_game() (original lines ~2382-2630): the four
// "if (g_state == GameState::X) { ...; return; }" state-machine blocks for Menu/Paused/
// Settings/Dead. Every original bare "return;" inside these four blocks became "return true;"
// (this frame's input was fully handled by a menu screen - update_game() should return
// immediately, exactly as before); a final "return false;" was added after the last block
// (g_state is Playing - update_game() should fall through to its own Playing-state code).
//
// kSavePath stays owned by main.cpp (not needed by anything else there per save_load.h's
// comment) - this file needs it too now, so it gets its own local copy, same "own copy of a
// compile-time literal" pattern as kDayLength/kColorPanelBorder above (kSavePath is a
// `static const char*`, not mutable state, so a duplicate literal is safe/equivalent to an
// extern here).
static const char* kSavePath = "save_slot0.tf2d";

// Geracao de mundo e sincrona (sem threads/fatiamento) e o mapa agora e bem maior
// (kWorldWidth/kWorldHeight, ver world.h) - o "new World(...)" abaixo pode levar alguns
// segundos. Sem isso pareceria que o jogo travou ao clicar em Novo Jogo. So desenha
// um frame com a mensagem antes da chamada bloqueante (nao e uma barra de progresso real,
// a geracao em si nao e fatiavel sem reescrever World::gen() em etapas).
static void draw_loading_screen(const char* message) {
    int win_w = GetScreenWidth();
    int win_h = GetScreenHeight();
    BeginDrawing();
    ClearBackground(Color{8, 10, 14, 255});
    float text_w = estimate_text_w_px(message);
    draw_text((float)win_w * 0.5f - text_w * 0.5f, (float)win_h * 0.5f - 8.0f, message, 0.80f, 0.86f, 0.98f, 1.0f);
    EndDrawing();
}

bool update_menu_input(float dt, bool esc_pressed, bool enter_pressed,
                        bool f5_pressed, bool f9_pressed, bool l_pressed, bool q_pressed) {
    (void)dt;

    if (g_state == GameState::Menu) {
        // Clique do mouse nos botoes do menu principal
        if (g_mouse_left_clicked && g_menu_selection >= 0) {
            g_mouse_left_clicked = false;
            switch (g_menu_selection) {
                case 0:  // Novo Jogo
                    delete g_world;
                    draw_loading_screen("Gerando mundo...");
                    g_world = new World(kWorldWidth, kWorldHeight, (unsigned)(GetTime() * 1000000.0));
                    spawn_player_new_game(*g_world);
                    g_cam_pos = g_player.pos;
                    g_day_time = kDayLength * 0.25f;
                    g_modules.clear();
                    g_particles.clear();
                    g_shooting_stars.clear();
                    g_construction_queue.clear();
                    g_alerts.clear();
                    // NAO limpar g_build_slots aqui: spawn_player_new_game() -> generate_base() ja
                    // fez o clear() como PRIMEIRA instrucao (modules_building.cpp:91) e acabou de
                    // encher o vetor com os 10 slots do anel. Limpar DEPOIS apagava todos eles (os
                    // tiles Block::BuildSlot ficavam no mundo, orfaos) e o menu de construcao caia
                    // no fallback de "criar slot automatico" varrendo y = g_base_y-1
                    // (building_interaction.cpp) - por isso o 1o modulo nascia fora da cupula.
                    g_collect_popups.clear();
                    g_drops.clear();
                    g_onboarding = OnboardingState();
                    g_state = GameState::Playing;
                    show_tip("WASD para mover, Espaco para pular, ESC para pausar/configuracoes", g_onboarding.shown_first_move);
                    return true;
                case 1:  // Carregar Jogo
                    if (load_game(kSavePath)) {
                        set_toast("Jogo carregado!");
                        g_state = GameState::Playing;
                    } else {
                        set_toast("Nenhum save encontrado.");
                    }
                    return true;
                case 2:  // Sair
                    g_quit = true;
                    return true;
            }
        }

        if (esc_pressed) {
            g_quit = true;
            return true;
        }
        if (enter_pressed) {
            delete g_world;
            draw_loading_screen("Gerando mundo...");
            g_world = new World(kWorldWidth, kWorldHeight, (unsigned)(GetTime() * 1000000.0));
            spawn_player_new_game(*g_world);  // This sets O2, water, etc. to 100%
            g_cam_pos = g_player.pos;
            g_day_time = kDayLength * 0.25f;  // Start at morning
            g_modules.clear();
            g_particles.clear();
            g_shooting_stars.clear();
            g_construction_queue.clear();
            g_alerts.clear();
            // NAO limpar g_build_slots aqui - ver comentario no outro caminho de Novo Jogo acima
            // (generate_base() ja limpou e repopulou o vetor; limpar depois orfanava os 10 slots).
            g_collect_popups.clear();
            g_drops.clear();

            // Reset onboarding para novo jogo
            g_onboarding = OnboardingState();

            g_state = GameState::Playing;

            // Dica inicial de onboarding
            show_tip("WASD para mover, Espaco para pular, ESC para pausar/configuracoes", g_onboarding.shown_first_move);
            return true;
        }
        if (l_pressed || f9_pressed) {
            if (load_game(kSavePath)) {
                set_toast("Jogo carregado!");
                g_state = GameState::Playing;
            } else {
                set_toast("Nenhum save encontrado.");
            }
            return true;
        }
        return true;
    }

    if (g_state == GameState::Paused) {
        // Clique do mouse nos botoes do menu de pausa
        if (g_mouse_left_clicked && g_pause_selection >= 0) {
            g_mouse_left_clicked = false;
            switch (g_pause_selection) {
                case 0:  // Continuar
                    g_state = GameState::Playing;
                    return true;
                case 1:  // Salvar Jogo
                    if (save_game(kSavePath)) set_toast("Jogo salvo!");
                    else set_toast("Falha ao salvar!");
                    return true;
                case 2:  // Carregar Jogo
                    if (load_game(kSavePath)) {
                        set_toast("Jogo carregado!");
                        g_state = GameState::Playing;
                    } else {
                        set_toast("Falha ao carregar!");
                    }
                    return true;
                case 3:  // Configuracoes
                    g_state = GameState::Settings;
                    g_settings_selection = 0;
                    return true;
                case 4:  // Menu Principal
                    g_state = GameState::Menu;
                    return true;
            }
        }

        if (esc_pressed) {
            g_state = GameState::Playing;
            return true;
        }
        if (q_pressed) {
            g_state = GameState::Menu;
            return true;
        }
        // Tecla 'O' abre configuracoes
        if (IsKeyDown(KEY_O)) {
            static bool o_was_pressed = false;
            if (!o_was_pressed) {
                g_state = GameState::Settings;
                g_settings_selection = 0;
                o_was_pressed = true;
            }
        } else {
            static bool o_was_pressed = false;
            o_was_pressed = false;
        }
        if (f5_pressed) {
            if (save_game(kSavePath)) set_toast("Jogo salvo!");
            else set_toast("Falha ao salvar!");
            return true;
        }
        if (f9_pressed) {
            if (load_game(kSavePath)) {
                set_toast("Jogo carregado!");
                g_state = GameState::Playing;
            } else {
                set_toast("Falha ao carregar!");
            }
            return true;
        }
        return true;
    }

    // Menu de configuracoes
    if (g_state == GameState::Settings) {
        static bool key_w_held = false;
        static bool key_s_held = false;
        static bool key_a_held = false;
        static bool key_d_held = false;

        bool w_now = IsKeyDown(KEY_W);
        bool s_now = IsKeyDown(KEY_S);
        bool a_now = IsKeyDown(KEY_A);
        bool d_now = IsKeyDown(KEY_D);

        // Navegar para cima
        if (w_now && !key_w_held) {
            g_settings_selection = (g_settings_selection - 1 + 13) % 13;
        }
        key_w_held = w_now;

        // Navegar para baixo
        if (s_now && !key_s_held) {
            g_settings_selection = (g_settings_selection + 1) % 13;
        }
        key_s_held = s_now;

        // F3 para debug lightmap
        static bool f3_held = false;
        bool f3_now = IsKeyDown(KEY_F3);
        if (f3_now && !f3_held) {
            g_debug_lightmap = !g_debug_lightmap;
        }
        f3_held = f3_now;

        // Ajustar valores
        float delta = 0.0f;
        if (a_now && !key_a_held) delta = -1.0f;
        if (d_now && !key_d_held) delta = 1.0f;
        key_a_held = a_now;
        key_d_held = d_now;

        if (delta != 0.0f) {
            switch (g_settings_selection) {
                case 0: // Sensibilidade
                    g_settings.camera_sensitivity = std::clamp(g_settings.camera_sensitivity + delta * 0.02f, 0.05f, 0.5f);
                    g_camera.sensitivity = g_settings.camera_sensitivity;
                    break;
                case 1: // Inverter Y
                    g_settings.invert_y = !g_settings.invert_y;
                    break;
                case 2: // Brilho
                    g_settings.brightness = std::clamp(g_settings.brightness + delta * 0.1f, 0.5f, 1.5f);
                    break;
                case 3: // Escala UI
                    g_settings.ui_scale = std::clamp(g_settings.ui_scale + delta * 0.1f, 0.75f, 1.5f);
                    break;
                case 4: // Iluminacao 2D
                    g_lighting.enabled = !g_lighting.enabled;
                    g_settings.lighting_enabled = g_lighting.enabled;
                    break;
                case 5: // Sombras
                    g_lighting.shadows_enabled = !g_lighting.shadows_enabled;
                    g_settings.lighting_shadows_enabled = g_lighting.shadows_enabled;
                    break;
                case 6: // Bloom
                    g_lighting.bloom_intensity = std::clamp(g_lighting.bloom_intensity + delta * 0.1f, 0.0f, 1.0f);
                    g_lighting.bloom_enabled = (g_lighting.bloom_intensity > 0.0f);
                    g_settings.lighting_bloom_intensity = g_lighting.bloom_intensity;
                    break;
                case 7: // Vinheta
                    g_lighting.vignette_intensity = std::clamp(g_lighting.vignette_intensity + delta * 0.1f, 0.0f, 0.6f);
                    g_settings.lighting_vignette_intensity = g_lighting.vignette_intensity;
                    break;
                case 8: // Musica Ativada
                    g_settings.music_enabled = !g_settings.music_enabled;
                    break;
                case 9: // Volume Musica - passo de 2% (pedido do jogador, os 10% de antes
                    // eram grosso demais pra ajuste fino).
                    g_settings.music_volume = std::clamp(g_settings.music_volume + delta * 0.02f, 0.0f, 1.0f);
                    break;
                case 10: // Efeitos Sonoros Ativados
                    g_settings.sfx_enabled = !g_settings.sfx_enabled;
                    break;
                case 11: // Volume Efeitos - mesmo passo de 2% do Volume Musica acima.
                    g_settings.sfx_volume = std::clamp(g_settings.sfx_volume + delta * 0.02f, 0.0f, 1.0f);
                    break;
                case 12: // Voltar
                    break;
            }
            if (g_settings_selection >= 8 && g_settings_selection <= 11) {
                apply_audio_settings(g_settings.music_volume, g_settings.music_enabled,
                                      g_settings.sfx_volume, g_settings.sfx_enabled);
            }
            // Persiste QUALQUER mudanca nesta tela (sensibilidade/brilho/iluminacao/audio) -
            // pedido do jogador: "o jogo nao guarda minhas configuracoes". Antes disso nada
            // em GameSettings sobrevivia a um reinicio do processo.
            if (g_settings_selection != 12) {
                save_game_settings();
            }
        }

        // ESC ou Enter no "Voltar" fecha o menu
        if (esc_pressed || (enter_pressed && g_settings_selection == 12)) {
            g_state = GameState::Paused;
            return true;
        }
        return true;
    }

    if (g_state == GameState::Dead) {
        // Death screen - wait for Enter to start new game
        if (enter_pressed) {
            delete g_world;
            draw_loading_screen("Gerando mundo...");
            g_world = new World(kWorldWidth, kWorldHeight, (unsigned)(GetTime() * 1000000.0));
            spawn_player_new_game(*g_world);
            g_cam_pos = g_player.pos;
            g_day_time = kDayLength * 0.25f;
            g_modules.clear();
            g_particles.clear();
            g_drops.clear();
            g_construction_queue.clear();
            g_alerts.clear();
            // NAO limpar g_build_slots aqui - ver comentario no 1o caminho de Novo Jogo acima.
            g_state = GameState::Playing;
            set_toast("Novo jogo!");
            return true;
        }
        if (esc_pressed) {
            g_state = GameState::Menu;
            return true;
        }
        return true;
    }

    return false;
}
