#pragma once

#include "math_core.h"

#include <array>
#include <cstdint>
#include <string>

// ============= Player / Physics =============
// Extracted verbatim from main.cpp (original lines ~215-317, 1116-1296, 1942-1958,
// 1978-2692): the Player struct + g_player instance, the physics-runtime types
// (TerrainPhysicsType/PhysicsRayDebug/PhysicsRuntime/PlayerPhysicsInput) + g_physics
// instance, the fixed-timestep collision/movement solver (probe_ground,
// try_step_climb, apply_single_physics_step, step_player_physics, etc. — these stay
// `static` inside player_physics.cpp since nothing outside calls them directly), and
// the spawn/respawn functions (spawn_player_at_base, respawn_player_at_base,
// spawn_player_new_game) plus the small approach() easing helper.
//
// This is the last of the world -> camera -> player_physics extraction sequence:
// player_physics.cpp includes world.h (World/g_world, surface_height_at, etc.) and
// camera.h (reset_camera_near_player) as needed, but this header does NOT include
// camera.h — only a forward declaration of World is needed here (functions take it
// by reference), matching the same "owner keeps the definition, users just
// forward-declare or extern" rule used for the world/camera split.
struct World;

struct Player {
    Vec2 pos = {0.0f, 0.0f}; // tile units (X, Z no espaco 3D)
    Vec2 vel = {0.0f, 0.0f}; // tiles/sec (horizontal)
    float w = 0.60f;
    float h = 0.60f;  // Tamanho de colisao (menor para caber entre blocos)
    int hp = 100;
    
    // === SISTEMA 3D - ALTURA E PULO ===
    float pos_y = 1.0f;       // Altura atual (Y no espaco 3D)
    float vel_y = 0.0f;       // Velocidade vertical
    bool on_ground = false;   // Se esta no chao
    bool can_jump = true;     // Pode pular (evita pulo infinito)
    float ground_height = 0.0f; // Altura do chao sob o jogador
    
    // Rotacao continua (graus, 0 = Norte, 90 = Leste, 180 = Sul, 270 = Oeste)
    float rotation = 180.0f;       // Rotacao atual
    float target_rotation = 180.0f; // Rotacao alvo (para suavizacao)
    
    // Compatibilidade com sistema antigo (calculado a partir de rotation)
    int facing_dir = 2;      // 0=Norte, 1=Leste, 2=Sul, 3=Oeste
    
    float walk_timer = 0.0f; // For walk animation
    float anim_frame = 0.0f; // General animation counter
    bool is_mining = false;
    float mine_anim = 0.0f;
    bool is_moving = false;  // Se esta andando (baseado na velocidade - inclui deslizar)
    // Suavizado 0..1: 1 = andando de verdade (input ativo), 0 = parado ou so' deslizando
    // por inercia (ex.: gelo, soltou o movimento mas o corpo ainda tem velocidade residual).
    // Usado pra escalar o ciclo de passada (bob/peso/balanco de perna) - sem isso as pernas
    // continuavam fazendo o gesto de andar mesmo com o jogador parado, so' escorregando.
    float walk_blend = 0.0f;

    // === JETPACK ===
    bool jetpack_active = false;  // Jetpack esta ativo
    float jetpack_fuel = 100.0f;  // Combustivel do jetpack (0-100)
    float jetpack_flame_anim = 0.0f; // Animacao da chama
    // Rajada de pouso (ver apply_single_physics_step) - true enquanto segurando a
    // velocidade segura ate o pouso de fato. main.cpp usa isso pra desenhar a chama MAIOR
    // que o voo manual normal nesse momento (deixa visivel que esta freando de verdade).
    bool landing_assist_active = false;
    
    // Movimento suave
    float speed_mult = 1.0f;  // Multiplicador de velocidade (acelera gradualmente)
};

// O unico Player do jogo. Definido (nao-static) em player_physics.cpp; main.cpp continua
// acessando g_player diretamente (render, UI, mineracao, save/load etc.) atraves desta
// declaracao extern - mesmo padrao de g_world/g_camera nos estagios anteriores.
extern Player g_player;

enum class TerrainPhysicsType : uint8_t {
    Normal = 0,
    Ice,
    Sand,
    Stone,
    Mud,
    Water,
};

struct PhysicsRayDebug {
    Vec3 from = {0.0f, 0.0f, 0.0f};
    Vec3 to = {0.0f, 0.0f, 0.0f};
    bool hit = false;
};

struct PhysicsRuntime {
    float accumulator = 0.0f;
    float alpha = 0.0f;

    Vec2 prev_pos = {0.0f, 0.0f};
    float prev_pos_y = 0.0f;
    float prev_rotation = 180.0f;

    Vec2 render_pos = {0.0f, 0.0f};
    float render_pos_y = 0.0f;
    float render_rotation = 180.0f;

    float jump_buffer_timer = 0.0f;
    float coyote_timer = 0.0f;
    bool jump_was_held = false;

    bool stepped = false;
    bool hit_x = false;
    bool hit_z = false;
    bool sliding = false;

    // Assistencia de pouso engatada (rajada disparada, segurando a velocidade segura ate tocar o
    // chao). Vive AQUI, e nao num `static` local de apply_single_physics_step: um static sobrevive a
    // respawn, Novo Jogo e teleporte, e este projeto ja teve bug real por estado preso em global que
    // ninguem reseta (ver a nota sobre g_shelter_door_x/y em modules_building.h). Em PhysicsRuntime
    // ele e' limpo por reset_player_physics_runtime() junto com o resto.
    bool landing_assist_engaged = false;
    TerrainPhysicsType terrain = TerrainPhysicsType::Normal;
    std::string terrain_name = "Normal";
    bool submerged = false; // Cabeca abaixo da superficie da agua (dreno extra de O2 do traje)
    // Fonte UNICA da verdade de "o jogador esta dentro da agua" (nadando), calculada COM
    // altura em apply_single_physics_step - diferente de "o chao embaixo e' agua", que e'
    // verdade em qualquer altitude sobre um tile de agua (probe_ground le o topo da coluna sem
    // olhar altitude). Quem desenha/mira deve ler ISTO, nunca re-derivar via surface_block_at:
    // era exatamente essa re-derivacao que desenhava o jogador afundado 0.57 e sem sombra
    // enquanto ele voava por cima da agua. Tambem serve de trava de continuidade da subida
    // assistida pra sair da agua (ver water_exit_ceiling).
    bool in_water = false;
    Vec3 ground_normal = {0.0f, 1.0f, 0.0f};
    Vec2 collision_normal = {0.0f, 0.0f};

    std::array<PhysicsRayDebug, 8> debug_rays = {};
    int debug_ray_count = 0;

    // Evento de poeira da rajada de pouso (ver apply_single_physics_step,
    // player_physics.cpp) - >0 = poeira ativa, contagem regressiva. main.cpp desenha um
    // anel de "puffs" procedural (mesmo estilo do fogo do jetpack, ja que o sistema
    // generico de particulas de items_particles.cpp nunca e desenhado) enquanto > 0, e
    // decrementa por frame.
    float landing_dust_timer = 0.0f;
    Vec3 landing_dust_pos = {0.0f, 0.0f, 0.0f};
    // 0..1 - o quao forte era a queda no instante da rajada (velocidade de impacto
    // relativa a faixa entre "mal disparou a assistencia" e "velocidade terminal") - ver
    // apply_single_physics_step. main.cpp escala o tamanho/raio/alpha do efeito por isso,
    // pra uma queda de pouca altura dar so um respingo pequeno e uma queda de verdade dar
    // uma onda bem maior.
    float landing_dust_intensity = 1.0f;
};

struct PlayerPhysicsInput {
    Vec2 move = {0.0f, 0.0f};
    bool has_move = false;
    bool run = false;
    bool jump_pressed = false;
    bool jump_held = false;
    bool jump_released = false;
    bool descend_held = false; // Nadar pra baixo/mergulhar (so tem efeito dentro d'agua)
};

// O unico PhysicsRuntime do jogo. Definido (nao-static) em player_physics.cpp; main.cpp
// continua lendo g_physics diretamente (HUD de debug em F3, desenho dos raios de debug,
// leitura de jump_was_held no polling de input) atraves desta declaracao extern - so
// perdeu o "static" que tinha em main.cpp porque agora e lida de duas unidades de
// traducao (mesmo padrao de g_camera_mode/g_camera_debug_rays em camera.h).
extern PhysicsRuntime g_physics;

void reset_player_physics_runtime(bool clear_timers = true);
void step_player_physics(const PlayerPhysicsInput& input, float frame_dt);

// Easing helper generico (aproxima "cur" de "target" em no maximo "max_delta"). Usado
// pelo solver de fisica acima e tambem pelo smoothing de g_cam_pos em main.cpp.
float approach(float cur, float target, float max_delta);

// ============= Spawn / Respawn =============
void spawn_player_at_base();
void respawn_player_at_base(const char* death_reason);
void spawn_player_new_game(World& world);

// Teleporte generico pra coordenadas arbitrarias (usado pra cruzar a porta solida da cupula
// da base, centrada em g_base_x/g_base_y - ver modules_building.h - um pulo curto de fora
// pra dentro da parede e vice-versa) - mesmo padrao de spawn_player_at_base(). Atualiza
// sozinho o lado logico da barreira da cupula (set_dome_barrier_side, abaixo) com base na
// distancia do destino ate o centro da cupula - quem chama nao precisa saber desse detalhe.
void teleport_player_to(int x, int y);

// set_dome_barrier_side() foi REMOVIDO junto com a barreira cilindrica invisivel da cupula - ver o
// comentario longo em player_physics.cpp. As paredes da base agora sao blocos de verdade com vaos
// reais, entao nao existe mais "lado logico" nenhum pra manter em sincronia.

// Definidas em player_physics.cpp (leem g_physics.render_pos/render_pos_y); camera.cpp
// (e o resto de main.cpp) continuam chamando-as atraves desta declaracao. camera.h ja
// forward-declara as duas de forma identica (nao gera conflito - duplicated non-
// conflicting extern declarations across TUs sao permitidas em C++); mantidas aqui
// tambem, ja que este e o modulo dono da definicao agora.
Vec2 get_player_render_pos();
float get_player_render_y();

// Posicao (mundo) da ponta do cano da Pistola de Laser quando equipada - replica com
// fidelidade total o offset de mao/bob/natacao usado por main.cpp pra desenhar a arma na
// mao. Usada por creatures.cpp (origem visual do traco/flash do tiro) e por main.cpp
// (fonte unica, evita duplicar a formula no bloco de desenho da arma).
Vec3 get_weapon_muzzle_pos();
