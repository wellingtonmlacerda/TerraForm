#pragma once

#include "blocks.h"
#include "math_core.h"

#include <vector>

// ============= Criaturas alienigenas =============
// Ameaca leve/opcional (pedido do jogador, escopo confirmado via pergunta direta): bichos
// que perambulam pelo mapa e perseguem o jogador quando perto, com dano de contato leve, e
// uma Pistola de Laser (Block::LaserPistol) pra abate-los. Nao existia nenhum sistema de
// inimigo/projetil antes disto - ver o plano salvo em
// C:\Users\9173\.claude\plans\quero-refatorar-todo-o-serene-kazoo.md pro contexto completo
// da investigacao. Segue o padrao de FallingMeteor/g_meteors (main.cpp): vetor efemero, NAO
// salvo em save_load.cpp (comportamento "ambiente", nao progresso a preservar).

struct Creature {
    float x = 0.0f, z = 0.0f, y = 0.0f;
    int hp = 20;
    int max_hp = 20;
    enum class State { Wandering, Chasing } state = State::Wandering;
    float wander_target_x = 0.0f, wander_target_z = 0.0f;
    float wander_timer = 0.0f;
    float yaw = 0.0f;
    float anim_timer = 0.0f;
    // Throttle do dano de contato (nao e' um "tick" por segundo cheio, e' por encontro).
    float contact_cooldown = 0.0f;
};

extern std::vector<Creature> g_creatures;

// Spawn (raro, mais comum a noite)/IA (perambular -> perseguir)/dano de contato leve/
// despawn - chamar 1x por frame, incondicional (main.cpp, update_game()).
void update_creatures(float dt);

// Desenho procedural (render_cube_3d/render_line_3d, sem textura, mesmo estilo do corpo do
// jogador so bem mais simples) - chamar de dentro de render_world() (main.cpp), perto do
// loop dos meteoros.
void render_creatures();

// Disparo (hitscan) da Pistola de Laser - chamado de update_mining_and_placement()
// (building_interaction.cpp) quando g_selected == Block::LaserPistol, ANTES de qualquer
// logica normal de mineracao/colocacao (ray_o/ray_d ja calculados pelo chamador, mesmo raio
// da mira normal). Cooldown de taxa de tiro e traco visual sao internos (nao expostos).
void try_fire_laser_pistol(const Vec3& ray_o, const Vec3& ray_d, float dt);

// Chamar logo apos respawn_player_at_base() (player_physics.cpp) - concede alguns segundos
// de graca onde criaturas ignoram o jogador. Respawn restaura so 50 HP (nao 100) - um bicho
// ja perseguindo bem na hora do respawn seria injusto.
void notify_player_respawned();

// Fracao 0..1 do cooldown do tiro que ainda falta (1 = acabou de atirar, 0 = pronto). Usada pela
// celula de energia desenhada na arma (main.cpp): a barra encurta e recarrega junto com o cooldown,
// dando leitura de estado na propria arma em vez de so' no HUD.
float laser_cooldown_fraction();
