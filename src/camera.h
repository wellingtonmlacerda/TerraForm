#pragma once

#include "math_core.h"

#include <array>
#include <cstdint>
#include <string>

// ============= Camera 3D (Terceira Pessoa) =============
// Extracted verbatim from main.cpp (original lines ~204-546 and ~648-825). GameCamera /
// GameCameraMode / CameraDebugRay plus the update/collision/visibility functions that operate
// on the single g_camera instance.
//
// Player/physics (g_physics, g_player) are NOT part of this extraction — that is the next
// stage of the plan. update_camera_for_frame() still needs the player's interpolated
// render position, so it keeps calling get_player_render_pos()/get_player_render_y(),
// declared at the bottom of this header; their definitions stay in main.cpp (they lost
// "static" there since they are now called from two translation units — they already had
// other main.cpp call sites besides the camera code, so this was needed regardless of the
// camera split).
struct GameCamera {
    Vec3 position;      // Posicao calculada da camera
    Vec3 target;        // Alvo (jogador)
    Vec3 up = {0.0f, 1.0f, 0.0f}; // Vetor up

    // Camera em terceira pessoa com horizonte visivel (menos "top-down")
    float distance = 4.8f;      // Distancia do jogador
    float yaw = 180.0f;         // Rotacao horizontal (graus) - sem limite, gira 360 livre
    float pitch = 18.0f;        // Rotacao vertical (mais baixa para ver o horizonte)
    // Faixa alargada quase ate os polos (perto de 0 = ver o horizonte/ceu de perto, perto de
    // 90 = ver quase de cima, proximo dos pes) - antes 8/88 deixava a sensacao de nao
    // conseguir olhar pro ceu nem pros pes mesmo a faixa sendo tecnicamente livre no yaw.
    float min_pitch = 2.0f;
    float max_pitch = 89.0f;
    float min_distance = 2.2f;
    float max_distance = 90.0f;
    float sensitivity = 0.30f;  // Antes 0.18 - baixo demais fazia um giro de 360 exigir muito espaco fisico de mouse
    float smooth_speed = 6.0f;  // Suavizacao do seguimento

    // Distancia efetiva (apos colisao)
    float effective_distance = 4.8f;
};

enum class GameCameraMode : uint8_t {
    Open = 0,
    SemiClosed,
    Cave,
    Emergency,
};

struct CameraDebugRay {
    Vec3 from = {0.0f, 0.0f, 0.0f};
    Vec3 to = {0.0f, 0.0f, 0.0f};
    bool blocked = false;
};

// O unico GameCamera do jogo. Definido (nao-static) em camera.cpp; main.cpp continua
// acessando g_camera diretamente (save/load de config, input de mouse/scroll, HUD de
// debug, skybox etc.) atraves desta declaracao extern.
extern GameCamera g_camera;

// Estado adaptativo do modo de camera (Open/SemiClosed/Cave/Emergency) e do HUD de debug
// associado. Definidos em camera.cpp; alguns campos (g_camera_mode, g_camera_mode_reason,
// g_camera_obstruction, g_camera_enclosed, g_camera_hidden_time, g_camera_debug_rays,
// g_camera_debug_ray_count) ainda sao lidos por main.cpp fora do modulo de camera (HUD de
// debug em F3 e desenho dos raios de debug), por isso precisam de linkage externo.
extern GameCameraMode g_camera_mode;
extern std::string g_camera_mode_reason;
extern float g_camera_obstruction;
extern float g_camera_enclosed;
extern float g_camera_hidden_time;
extern std::array<CameraDebugRay, 96> g_camera_debug_rays;
extern int g_camera_debug_ray_count;

const char* camera_mode_name(GameCameraMode mode);
void reset_camera_near_player(bool reset_angles);

// Aplicar matriz de view (gluLookAt manual) / projecao perspectiva manual.
void apply_look_at();
// FOV vertical usado pela projecao. Era um literal 74.0f em main.cpp (chamada de apply_perspective)
// E uma const local em get_mouse_ray_direction - duas copias que, se divergissem, fariam a mira e a
// projecao do HUD discordarem do que e' desenhado. Fonte unica.
constexpr float kCameraFovDegrees = 74.0f;

void apply_perspective(float fov_degrees, float aspect, float near_plane, float far_plane);

// Projecao MUNDO -> TELA. Inversa de get_mouse_ray_direction, usando o MESMO FOV e a mesma base de
// camera, porque a projecao do jogo e' manual (apply_perspective) - nao ha Camera do raylib, entao
// GetWorldToScreen() nao serve. Devolve false quando o ponto esta atras da camera.
bool world_to_screen(Vec3 world, int win_w, int win_h, float& out_x, float& out_y);

// Calcular direcao do ray a partir da posicao do mouse na tela (usado para mineracao/build).
Vec3 get_mouse_ray_direction(int mouse_x, int mouse_y, int win_w, int win_h);

// Alpha de fade de blocos que ocluem a camera (usado pelo renderer para tornar
// transparentes os blocos entre a camera e o jogador).
float camera_occluder_alpha_for_tile(int tx, int tz);

void check_camera_collision();
void update_camera_for_frame();

// Definidas em main.cpp (leem g_physics, cuja extracao para player_physics.h/.cpp e uma
// fase posterior do plano); camera.cpp so precisa chama-las.
Vec2 get_player_render_pos();
float get_player_render_y();
