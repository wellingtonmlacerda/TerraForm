#pragma once

#include <string>
#include <functional>
#include <fstream>

#include "config_types.h"

// ============= Config load/save subsystem =============
// Extracted from main.cpp (original lines ~3063-3907): six near-identical
// reload_*_config functions (physics, terrain, sky, camera, mining, player_visual),
// each shaped as write_default_X_config -> apply_X_config_overrides -> reload_X_config,
// deduplicated per the refactor plan's Fase 1b / item 1 ("Config loaders").
//
// The path-search-and-fallback plumbing that used to be copy-pasted 6x is now the
// generic find_or_seed_config_path() + reload_config<Cfg>() template below. The
// write_default_*_config / apply_*_config_overrides functions themselves are
// genuinely different per struct and are NOT deduplicated (moved verbatim into
// config_io.cpp instead), per the plan.

// ---- Shared JSON-ish parsing helpers (generic, no per-config specifics) ----
bool file_exists(const std::string& path);
bool parse_json_number(const std::string& text, const char* key, float& out_value);
bool parse_json_bool(const std::string& text, const char* key, bool& out_value);

// Busca os 4 caminhos candidatos relativos ("<filename>", "..\\<filename>",
// "..\\..\\<filename>", "..\\..\\..\\<filename>"), retornando o primeiro que existir.
// Se nenhum existir: escolhe o 1o candidato e, se create_if_missing, escreve o default
// nele via write_default. Idêntico ao bloco duplicado 6x no main.cpp original.
std::string find_or_seed_config_path(const char* filename, bool create_if_missing,
                                      const std::function<void(const std::string&)>& write_default);

// Mecanismo genérico de reload de config. Reproduz exatamente o comportamento das 6
// versões originais: busca/seed do caminho; se o arquivo abre, aplica overrides e marca
// loaded=true; se não abre e create_if_missing, escreve o default (loaded permanece
// false nesse caso — igual ao original, que não tentava reabrir depois de escrever).
// Definido aqui (não em config_io.cpp) porque precisa ser visível no ponto de
// instanciação de cada tipo Cfg.
template <typename Cfg>
bool reload_config(const char* filename, Cfg& out_cfg, bool create_if_missing,
                    const std::function<void(const std::string&)>& write_default,
                    const std::function<void(const std::string& text, Cfg&)>& apply_overrides,
                    std::string* out_chosen_path) {
    std::string chosen_path = find_or_seed_config_path(filename, create_if_missing, write_default);

    Cfg cfg = Cfg{};
    bool loaded = false;
    std::ifstream f(chosen_path);
    if (f) {
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        apply_overrides(text, cfg);
        loaded = true;
    } else if (create_if_missing) {
        write_default(chosen_path);
    }

    out_cfg = cfg;
    if (out_chosen_path) *out_chosen_path = chosen_path;
    return loaded;
}

bool reload_physics_config(bool create_if_missing);
bool reload_terrain_config(bool create_if_missing);
bool reload_sky_config(bool create_if_missing);
bool reload_mining_config(bool create_if_missing);
bool reload_player_visual_config(bool create_if_missing);

// GameSettings (sensibilidade/brilho/audio/etc, ver game_state.h) - unico config desta
// familia que tambem precisa SALVAR (os outros 5 sao arquivos ajustaveis a mao pelo
// desenvolvedor, carregados 1x; este e' alterado pelo proprio jogador no menu de
// Configuracoes e precisa persistir entre sessoes - pedido do jogador: "o jogo nao guarda
// minhas configuracoes"). reload_game_settings() segue o mesmo template reload_config<Cfg>
// dos outros; save_game_settings() escreve o g_settings atual de volta no mesmo arquivo -
// chamar sempre que o jogador mudar algo no menu (ui_menu.cpp).
bool reload_game_settings(bool create_if_missing);
void save_game_settings();

// reload_camera_config is declared here like the other 5, but unlike them its
// DEFINITION stays in main.cpp (not config_io.cpp). The original function did two
// things the other 5 don't: after applying overrides to CameraConfig, it also
// re-clamps the live g_camera object's distance/pitch against g_camera's own
// min/max fields:
//   g_camera.distance = std::clamp(g_camera.distance, g_camera.min_distance, g_camera.max_distance);
//   g_camera.pitch = std::clamp(g_camera.pitch, g_camera.min_pitch, g_camera.max_pitch);
// Camera3D (g_camera's type) has since moved to camera.h/.cpp, so config_io.cpp could
// now include camera.h and reference g_camera's members if it wanted to — the original
// blocker (no complete Camera3D definition available here) is gone. reload_camera_config
// just hasn't been relocated to config_io.cpp along with it, since that isn't needed for
// the camera extraction itself; it keeps its definition in main.cpp, reusing
// reload_config<Cfg> for the generic part and adding the g_camera clamp itself. Because of
// that, write_default_camera_config / apply_camera_config_overrides (needed by main.cpp's
// reload_camera_config) are declared below with external linkage instead of being
// file-local (static) to config_io.cpp like their 5 counterparts.
bool reload_camera_config(bool create_if_missing);
void write_default_camera_config(const std::string& path);
void apply_camera_config_overrides(const std::string& text, CameraConfig& cfg);

// NOTE: g_physics_cfg/g_terrain_cfg/g_sky_cfg/g_mining_cfg/g_player_visual_cfg and
// their *_config_path siblings are still defined (non-static) in main.cpp — extracting
// them properly is a later "game_state" phase of the plan. config_io.cpp declares its
// own `extern` for them at its top, same pattern as g_oxygen/g_water_res/etc. in
// textures.cpp (declared in the .cpp that needs them, not re-declared here in the
// shared header, since no other translation unit needs to see them).
