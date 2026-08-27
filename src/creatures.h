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

// ============= ARQUETIPOS DE INIMIGO =============
// Antes existia UMA criatura com 4 "especies" que mudavam so' a paleta: mesmo HP (40), mesmo dano
// de contato (4), mesma velocidade, mesma deteccao. Cor nao e' identidade - o jogador nao tinha como
// saber, olhando, se aquele bicho era perigoso.
//
// Agora sao 4 arquetipos com diferenca REAL em silhueta, porte, HP, defesa, dano, velocidade,
// alcance, cadencia de ataque, telegrafe, reacao ao dano, som e comportamento. A cor continua, mas
// como elemento complementar - a silhueta e o tamanho identificam de longe.
enum class EnemyType : uint8_t {
    Crawler = 0,   // pequeno, rapido, fraco - ameaca de enxame
    Stalker,       // medio, mantem distancia e reposiciona
    Brute,         // pesado, blindado, lento, dano alto
    Alpha,         // elite raro, agressivo, nucleo energetico
};
constexpr int kEnemyTypeCount = 4;

// Classe de peso do som de impacto. So' 3 (nao um som por tipo): Alpha reusa o blindado com pitch
// proprio - o pedido foi explicito em nao criar dezenas de sons.
enum class HitSoundClass : uint8_t { Light = 0, Heavy, Armored };

// ============= TABELA UNICA DE ATRIBUTOS =============
// TUDO que varia por tipo mora aqui, numa linha por arquetipo. A IA, o dano, o render e o audio sao
// codigo COMPARTILHADO que le esta tabela - nao ha "if (type == Brute)" espalhado, e adicionar um
// quinto tipo e' acrescentar uma linha.
struct EnemyArchetype {
    const char* name;          // mostrado no HUD ao mirar

    // ---- combate ----
    int   max_hp;
    int   defense;             // subtraido do dano por tiro (min 1) - resistencia != vida
    int   contact_damage;      // dano por golpe no jogador
    float attack_range;        // distancia de golpe
    float attack_cooldown;     // segundos entre golpes
    float attack_windup;       // telegrafe: som + pausa ANTES do golpe, pra o jogador reagir
    float flinch_scale;        // 0 = imovel ao ser atingido, 1 = recuo cheio (massa)

    // ---- movimento / percepcao ----
    float wander_speed;
    float chase_speed;         // referencia: max_speed do jogador = 4.8
    float detect_range;
    float keep_distance;       // > 0: recua se ficar mais perto que isto (Stalker reposiciona)

    // ---- aparencia ----
    float scale;
    float body_r, body_g, body_b;
    float head_r, head_g, head_b;
    float limb_r, limb_g, limb_b;
    float plate_r, plate_g, plate_b;
    float eye_r, eye_g, eye_b;
    int   legs;                // 6 no Crawler, 2 no Stalker, 4 nos pesados
    int   plates;              // placas dorsais
    float antenna;
    bool  elite;               // marca discreta de raridade no HUD + aura

    // ---- audio ----
    HitSoundClass hit_sound;
    float sound_pitch;         // grave nos pesados, agudo nos leves
};

const EnemyArchetype& enemy_archetype(EnemyType t);

struct Creature {
    float x = 0.0f, z = 0.0f, y = 0.0f;
    EnemyType type = EnemyType::Crawler;
    int hp = 1;
    int max_hp = 1;
    enum class State { Wandering, Chasing } state = State::Wandering;
    float wander_target_x = 0.0f, wander_target_z = 0.0f;
    float wander_timer = 0.0f;
    float yaw = 0.0f;
    float anim_timer = 0.0f;
    // Throttle do golpe (nao e' um "tick" por segundo cheio, e' por encontro).
    float contact_cooldown = 0.0f;
    // Telegrafe: > 0 significa "golpe carregando". O som toca no inicio da carga e o dano so' sai
    // no fim - e' o que da ao jogador a chance de sair de perto, sobretudo dos pesados.
    float windup = 0.0f;
    // Reacao ao dano: cronometro do recuo/piscada. Escala por flinch_scale, entao um Brute quase
    // nao reage e um Crawler e' jogado pra tras.
    float flinch = 0.0f;
    float flinch_dx = 0.0f, flinch_dz = 0.0f;
    // Barra de vida: aparece ao receber dano e some depois. Nao ha barra permanente sobre todo
    // bicho do mapa - poluiria a tela (pedido explicito).
    float hp_bar_timer = 0.0f;
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

// ============= PROGRESSAO DA ARMA (Mk I -> Mk II -> Mk III) =============
// O NIVEL MORA EM g_inventory[(int)Block::LaserPistol] (1..3), nao num global novo. Isso nao e'
// gambiarra oportunista: as unicas leituras desse slot em todo o projeto sao "> 0"
// (modules_building.cpp/try_craft_laser_pistol e ui_hud.cpp/botao da arma) e as unicas escritas sao
// "= 1"; LaserPistol nao esta em kCraftCostFields, entao spend_cost/refund_cost nunca o tocam, e ele
// nunca e' colocado como tile nem dropado. Consequencia pratica: o inventario ja e' salvo com prefixo
// de contagem (save_load.cpp), logo o NIVEL PERSISTE SEM NENHUMA MUDANCA DE FORMATO DE SAVE.
int weapon_level();

// Sobe 1 nivel (chamado por try_upgrade_weapon, modules_building.cpp - que cobra o custo e exige
// Oficina). Devolve false se ja esta no maximo.
bool weapon_level_up();
constexpr int kWeaponMaxLevel = 3;

// Nome do nivel pra HUD ("Mk I"/"Mk II"/"Mk III").
const char* weapon_level_name();

// ============= ESTATISTICA DE COMBATE PERSISTENTE =============
// Contador MONOTONICO de criaturas abatidas pelo laser. Existe porque as missoes de combate
// (ObjectiveId::FirstHunt e ProtectColony) precisam de um evento real de abate, e o projeto nao tinha
// NENHUMA estatistica de combate. Incrementado no unico site de morte por tiro (try_fire_laser_pistol);
// os outros dois erase() de g_creatures sao despawn (fora de limites / distancia) e de proposito NAO
// contam - despawn nao e' abate. Salvo no bloco v11 de save_load.cpp.
int creature_kills();

// Novo jogo: zera abates E o vetor de criaturas. Chamar de spawn_player_new_game() ANTES de
// generate_base(), pela mesma razao documentada para reset_objectives().
void reset_creature_state();

// Save/load - mesmo par estreito de acessores que objectives.h usa pra g_current/g_ever_built.
void creature_stats_load(int kills);

// ============= HUD de combate =============
// Barra de vida + nome do tipo + numeros de dano flutuantes. Desenhado em 2D (projeta a posicao do
// mundo com GetWorldToScreen), entao precisa ser chamado DEPOIS do passe 3D e antes do resto do HUD.
// So' desenha o que esta em combate: barra aparece ao mirar ou ao levar dano e some sozinha - nao ha
// objeto de UI persistente por inimigo.
void render_creature_hud(int win_w, int win_h);

// Aponta pra criatura atualmente sob a mira (a mesma logica de acerto do tiro), ou -1. Usado pelo
// HUD pra mostrar a barra de quem o jogador esta mirando, mesmo sem ter atirado ainda.
int creature_under_aim();

// ============= Feedback de dano NO JOGADOR =============
// Antes, ser atingido produzia so' um toast e o HP caindo - o jogador nao sabia de onde vinha nem
// tinha resposta imediata. Estes campos alimentam o indicador direcional e o pulso no HUD
// (ui_hud.cpp). Direcao aponta DA criatura pro jogador (de onde veio o golpe).
extern float g_player_hit_timer;    // > 0 = mostrar feedback
extern float g_player_hit_dir_x, g_player_hit_dir_z;
extern int   g_player_hit_amount;

// Abates por tipo (arquitetura pronta pra objetivos como "derrote 3 Brutes"). Indice = EnemyType.
int creature_kills_of(EnemyType t);

// TESTE TEMPORARIO (REMOVER)
int weapon_damage_for_test();
float weapon_cooldown_for_test();
EnemyType pick_spawn_type_for_test();
