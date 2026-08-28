#pragma once

// ============= Menu de Construcao da Colonia =============
// Extraido de building_interaction.cpp (render_build_menu + update_build_menu_input) e reescrito.
// Sai daquele arquivo pelo mesmo motivo de todas as outras extracoes deste projeto: building_
// interaction.cpp e' sobre mineracao/colocacao no mundo, e o menu cresceu pra ser um subsistema de
// UI proprio com layout de 3 colunas, categorias, cards e navegacao.
//
// O menu anterior era UMA LISTA VERTICAL DE TEXTO: 9 linhas com nome, descricao, producao, consumo,
// custo, tempo e status todos empilhados como texto colorido - lia como tela de debug. Este e' um
// terminal de gerenciamento: categorias -> grade de cards -> painel de detalhes + status da
// colonia, com barra de recursos no rodape.
//
// REGRA DE OURO deste arquivo: ele NAO declara nenhum dado de construcao. Bloco, categoria,
// producao, custo, tempo e requisito de desbloqueio vem todos de kBuildables/get_module_stats/
// module_cost_breakdown/module_unlock_breakdown (modules_building.h, inventory_crafting.h). A lista
// de modulos estava duplicada nas duas funcoes antigas - esse era o defeito a nao repetir.

// Desenha o menu. No-op quando o menu esta fechado E a animacao de fechamento ja terminou.
// Chamar de render_world() (main.cpp), depois do HUD e antes dos alertas.
void render_build_menu(int win_w, int win_h);

// Navegacao/acao por teclado. Devolve true quando o menu esta aberto (consome o frame de input,
// mesma convencao de update_menu_input em ui_menu.cpp).
//   W/S ou setas cima/baixo -> construcao anterior/proxima DENTRO da categoria
//   A/D ou setas esq/dir    -> categoria anterior/proxima
//   Enter                   -> construir
//   Tab/B                   -> fechar (tratado em main.cpp, como antes)
bool update_build_menu_input();
