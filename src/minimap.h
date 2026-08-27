#pragma once

// ============= Minimap / World Map / Waypoints =============
// Extracted verbatim from main.cpp (original lines ~846-1399): fog-of-war tracking,
// waypoint management, and the two minimap renderers (corner HUD minimap + fullscreen
// world map). All six functions lost "static": main.cpp's update_game()/render_world()
// (out of scope for this stage) still call all of them - same pattern as g_world/g_camera
// in earlier extraction stages. get_minimap_color() is NOT declared here on purpose - it
// is only ever used internally by render_minimap()/render_world_map() (both moved here
// too), so it stays file-local (static) to minimap.cpp.
void update_fog_of_war(float dt);
void add_waypoint(int x, int y, const char* label = nullptr);
void remove_nearest_waypoint(int x, int y);
void clear_all_waypoints();
void render_minimap(int win_w, int win_h);
void render_world_map(int win_w, int win_h);

// Scanner (tecla T, main.cpp) - varre uma area ao redor do jogador por POI/minerio raro
// e cria um waypoint apontando pro achado mais proximo. Ver comentario completo em
// minimap.cpp. Retorna false se ja estava em cooldown (nao consome de novo) ou se a
// varredura nao achou nada (consome o cooldown normalmente) - o toast/alert ja avisa o
// jogador em qualquer caso.
bool scan_for_points_of_interest();

// ============= Descoberta de destroco (missao de exploracao) =============
// True depois que o jogador chegou perto de um sitio de destroco (POI) real - fora do disco da base,
// fora da pegada do exterior e fora do distrito de interiores (as mesmas 3 exclusoes que
// scan_for_points_of_interest usa). Latch: nunca volta a false, entao a missao nao desfaz.
// Detectado dentro de update_fog_of_war (unico ponto que ja roda 1x/frame com todo o estado
// necessario a mao); a varredura para de custar assim que o latch liga.
bool poi_ever_found();
void poi_discovery_load(bool found);   // save/load (bloco v11)
void reset_poi_discovery();            // novo jogo
