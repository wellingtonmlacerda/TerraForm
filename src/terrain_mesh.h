#pragma once

// ============= Terreno distante: cache de malha (GPU) por chunk =============
// Motivacao (pedido do jogador: "o jogo esta ficando complexo e pesado, lide com isso"):
// medido ao vivo com um contador de debug, o loop de terreno de render_world() (main.cpp)
// desenha TODO tile visivel em modo imediato (rlBegin/rlVertex3f/rlEnd por quad) TODO frame,
// mesmo quando nada mudou - a 20 FPS, so as paredes (diferenca de altura entre tiles
// vizinhos) somavam 34091 quads num unico frame com view_radius apenas 207 (bem abaixo do
// teto de 380). Terreno nunca muda sozinho (so' mineracao/construcao/vulcao/degelo o
// alteram, tudo via World::set/set_ground/set_height) - redesenhar tudo em modo imediato
// todo frame e' puro desperdicio de CPU pro terreno que nao mudou.
//
// Este modulo cobre os tiles LONGE do jogador (alem de near_radius) com Mesh de verdade
// (raylib Mesh/UploadMesh/DrawMesh) por chunk de kTerrainChunkTiles x kTerrainChunkTiles
// tiles, reconstruida so' quando: (a) algum tile do chunk mudou (terrain_mesh_mark_dirty,
// chamado de World::set/set_ground/set_height - ver world.h) ou (b) o chunk ficou obsoleto
// (nevoa/iluminacao dependem de posicao da camera/hora do dia, que mudam todo frame - um
// chunk distante e reconstruido periodicamente pra nao "congelar" a neblina/luz de quando
// foi construido, ver terrain_mesh.cpp). Os tiles PERTO do jogador (dentro de near_radius)
// continuam desenhados exatamente como antes, em modo imediato, sem NENHUMA mudanca -
// preserva fidelidade total (agua animada, brilho de sol, blend com o cursor de mineracao)
// exatamente onde o jogador esta olhando de perto.
constexpr int kTerrainChunkTiles = 16;

// Verdade se o tile pertence a um chunk "longe" (coberto pela malha cacheada) em vez de
// "perto" (modo imediato, desenhado por render_world() em main.cpp). Esta e' a UNICA fonte
// da verdade pra essa fronteira - main.cpp (loop de terreno) e terrain_mesh_render_far()
// (abaixo) chamam esta mesma funcao, garantindo que cada tile caia em exatamente um dos 2
// caminhos. Usar criterios diferentes nos 2 lados (ex.: distancia por tile de um lado,
// distancia por centro de chunk do outro) fazia alguns tiles perto da fronteira serem
// desenhados nos DOIS caminhos ao mesmo tempo - geometria sobreposta na mesma posicao/altura
// gera z-fighting (bug real reportado: "o chao voltou a piscar").
bool terrain_mesh_tile_is_far(int tile_x, int tile_z, int player_tile_x, int player_tile_z, int near_radius);

// Chamar sempre que um tile mudar de verdade (World::set/set_ground/set_height chamam isso
// internamente - ver world.h). Barato (so' marca 1 flag) - seguro de chamar com frequencia.
void terrain_mesh_mark_dirty(int tile_x, int tile_y);

// Desenha os chunks visiveis fora de near_radius e dentro de far_radius (reconstruindo os
// sujos/obsoletos, limitado a poucos por frame pra nao gerar soluco). Requer que o atlas de
// textura ja esteja ligado (rlSetTexture(g_tex_atlas), mesmo estado que render_world() ja
// deixa antes do loop de terreno) - so funciona com use_textures=true (sem textura nao vale
// a pena cachear, o fallback sem atlas so' e usado quando a textura falha ao carregar).
// Retorna quantos chunks foram desenhados neste frame (usado pelo overlay de diagnostico).
int terrain_mesh_render_far(int player_tile_x, int player_tile_z, int near_radius, int far_radius);

// Descarta todas as malhas em cache (chamar antes de fechar o jogo, ou se precisar forcar
// reconstrucao total). terrain_mesh_render_far ja detecta sozinho quando g_world foi trocado
// (Novo Jogo/Carregar) e reconstroi tudo automaticamente - isso aqui e so' pra shutdown.
void terrain_mesh_shutdown();
