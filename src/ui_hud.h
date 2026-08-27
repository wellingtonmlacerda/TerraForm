#pragma once

#include "blocks.h"   // Block (kElementSlots - barra de elementos)

// ============= HUD Rendering =============
// Extracted verbatim from main.cpp's render_world() (original lines ~1707-2451): the
// switch from 3D to 2D/ortho projection, the vignette effect, the lightmap/lights debug
// overlays, the mouse crosshair, HP/O2/water/food/jetpack status bars, base resource bars,
// terraforming/phase/temperature/CO2/atmosphere stats, the base direction indicator, the
// minimap, the hotbar (resource + module slots), collect popups, target/debug info, toast
// notifications, screen-flash feedback, the unlock popup, and the onboarding tip.
//
// render_hud() lost "static" by construction: it is defined here and called from
// render_world() in main.cpp, which is a different translation unit - same pattern as
// every other render_*()/update_*() entry point declared in the other extracted headers.
//
// Only the projection switch + HUD drawing moved to this stage; the Paused/Menu/Dead/
// Settings overlay block and the alerts/world-map overlay that used to sit right after
// this in render_world() stay inline in main.cpp (a later ui_menu extraction stage
// handles those).
void render_hud(int win_w, int win_h);

// Geometria do painel direito (Fase/terraformacao) - exposta pra outros arquivos (hoje so
// minimap.cpp) poderem ancorar elementos em relacao a ele sem duplicar os mesmos numeros
// magicos numa 2a copia independente. Bug real corrigido por causa disso: o minimapa
// ancorava numa altura fixa (200px) que nao tinha ligacao nenhuma com a altura de verdade
// deste painel (calculada aqui) - qualquer mudanca num dos dois lados descolava do outro,
// e chegou a sobrepor. render_hud() usa essas mesmas funcoes internamente agora, entao os
// dois nunca mais podem divergir.
float hud_right_panel_right_x(int win_w);
float hud_right_panel_bottom_y();

// True enquanto o cursor esta sobre um botao redondo do cluster de acao (arma). Lido pelo caminho de
// TIRO (creatures.cpp) e de mineracao/construcao (building_interaction.cpp) pra nao agir no mundo
// quando o clique era na interface.
//
// Existe porque o tiro usa IsMouseButtonDown (botao SEGURADO), nao o flag de clique: consumir
// g_mouse_left_clicked no botao nao impedia nada, e clicar no icone da arma sempre disparava um tiro -
// bug reportado. Um teste de "cursor sobre a interface" resolve pros dois caminhos de uma vez.
// Atualizado 1x por frame por render_hud(); os botoes sao estaticos, entao um frame de atraso e'
// irrelevante.
extern bool g_hud_pointer_over_button;

// ============= Barra de elementos (rolagem) =============
// FONTE UNICA da lista de elementos colecionaveis mostrada na barra de baixo. Antes a lista dos 6
// recursos estava DUPLICADA em ui_hud.cpp (desenho) e main.cpp (teclas 1-6) - duas copias que
// fatalmente discordariam, o footgun que este projeto ja documentou em outros lugares.
//
// 13 entradas: os 6 originais primeiro (a vista inicial fica identica a de antes, sem quebrar a
// memoria muscular), depois os que nao tinham slot nenhum. Madeira/Organico so' aparecem depois da
// terraformacao e Liga Refinada so' sai da Oficina - continuam listados, com contagem 0, porque um
// slot que aparece e desaparece embaralharia as posicoes das teclas.
extern const Block kElementSlots[];
constexpr int kElementSlotCount = 13;
constexpr int kElementVisibleSlots = 6;   // quantos cabem na tela de uma vez

int  hud_elements_scroll();                 // indice do primeiro slot visivel
void hud_elements_scroll_by(int delta);     // clampado em [0, total - visiveis]

// True enquanto o cursor esta sobre a barra de elementos. Lido por process_input_events
// (win32_platform.cpp) pra a roda do mouse rolar a barra em vez de dar zoom na camera.
extern bool g_hud_pointer_over_elements;
