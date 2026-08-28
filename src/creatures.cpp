#include "creatures.h"

#include "raylib_platform.h"
#include "world.h"
#include "player_physics.h"
#include "ui_hud.h"          // g_hud_pointer_over_button (nao atirar clicando na HUD)
#include "camera.h"           // g_camera / get_mouse_ray_direction (mira e projecao do HUD)
#include "font.h"             // draw_text / estimate_text_w_px (barra de vida)
#include "interiors.h"      // interior_at (nao spawnar criatura dentro de uma sala)
#include "game_state.h"        // set_toast, rng_next_f01, kDayLength
#include "items_particles.h"   // spawn_block_particles (o drop de loot foi removido a pedido)
#include "inventory_crafting.h" // g_inventory - carrega o NIVEL da arma (ver weapon_level em creatures.h)
#include "render_primitives.h"
#include "audio.h"              // play_laser_fire_sound, play_laser_impact_sound

#include <cmath>
#include <algorithm>
#include <string>

// add_alert() nao tem header proprio (padrao ja usado por modules_building.cpp/
// minimap.cpp) - so' declaracao local, definida em main.cpp.
void add_alert(const std::string& msg, float r, float g, float b, float duration = 3.0f, float cooldown = 5.0f);
// key_down()/g_day_time idem: definidos em main.cpp, extern local (mesmo padrao de
// building_interaction.cpp).
bool key_down(int vk);
extern float g_day_time;
// Fase da terraformacao: define QUAIS criaturas podem aparecer (pick_spawn_type). Definida em
// main.cpp, extern local - mesmo padrao de g_day_time acima.
extern TerraPhase g_phase;

std::vector<Creature> g_creatures;
std::vector<CreatureProjectile> g_creature_projectiles;

float g_player_hit_timer = 0.0f;
float g_player_hit_dir_x = 0.0f, g_player_hit_dir_z = 0.0f;
int   g_player_hit_amount = 0;

namespace {

// 3 -> 7 e intervalo 30-70s -> 14-38s (pedido do jogador: "quero mais inimigos"). O teto e o
// intervalo sao o que realmente controlam a populacao; o corte por noite (kNightSpawnGate) fica
// como esta, entao continuam raras de dia.
constexpr int kMaxCreatures = 7;
constexpr float kSpawnMinInterval = 14.0f;
constexpr float kSpawnMaxInterval = 38.0f;
constexpr float kSpawnMinDist = 20.0f;
constexpr float kSpawnMaxDist = 45.0f;
constexpr float kNightSpawnGate = 0.15f; // abaixo disso (quase dia pleno), nao rola spawn
// Deteccao, velocidades, alcance de golpe, dano e cadencia agora vivem em kArchetypes (uma linha
// por tipo de inimigo, mais abaixo) - eram constantes UNICAS compartilhadas por todas as criaturas,
// o que era metade do motivo de "todos os inimigos sao praticamente iguais".
constexpr float kDespawnDist = 150.0f;
// Teto de projeteis em voo. Com 7 criaturas e recargas de 2.6-4.5s o normal fica muito abaixo
// disso; e rede de seguranca, nao ritmo.
constexpr size_t kMaxCreatureProjectiles = 48;
constexpr float kRespawnGraceSeconds = 5.0f;

// Alcance/raio de acerto/dano/cadencia/duracao do traco agora vivem em kWeaponTiers (mais abaixo),
// uma linha por nivel da arma - os valores do Mk I sao exatamente os que eram estas constantes.
constexpr float kMuzzleFlashDuration = 0.08f;

float g_respawn_grace = 0.0f;
float g_fire_cooldown = 0.0f;
float g_laser_trace_timer = 0.0f;
Vec3 g_laser_trace_a{};
Vec3 g_laser_trace_b{};
float g_muzzle_flash_timer = 0.0f;

// Efeito de impacto (faiscas + explosao de verdade: anel de estilhacos + flash central,
// mesma tecnica ja aprovada da onda de choque de pouso do jetpack, main.cpp) + marca de
// queimado (decal escuro no chao, dura bem mais) - pedido do jogador: "efeito de atingir o
// alvo... marcas de queimado", depois "sem efeito de explosao no local do impacto".
constexpr float kImpactFlashDuration = 0.35f; // vida total (sparks usam isso pra fade)
constexpr float kExplosionRingLife = 0.30f;   // anel de estilhacos - sub-janela dentro do timer acima
constexpr float kExplosionFlashLife = 0.14f;  // flash central - mais rapido, "momento da detonacao"
constexpr float kScorchMarkDuration = 25.0f;
constexpr float kFlinchDuration = 0.22f;   // duracao da piscada/recuo ao ser atingido
constexpr float kHpBarVisibleSeconds = 3.0f; // barra fica visivel apos o ultimo dano
constexpr float kPlayerHitFlashSeconds = 0.55f;

// ============= TABELA DE NIVEIS DA ARMA =============
// Table-driven, mesmo estilo de kInteriors[]/kExterior[]: adicionar um nivel e' uma linha.
//
// Por que uma tabela e nao "so' trocar kLaserDamage": todo numero VISUAL do tiro estava como literal
// no call site (larguras 0.16/0.055 do feixe, RGBs, raio 0.12 do muzzle, formulas do anel de
// explosao). Sem icar esses literais, "upgrade" seria so' um numero de dano diferente - exatamente o
// que o jogador recusou ("nao quero apenas alterar uma variavel de dano"). Agora dano, cadencia,
// alcance E aparencia saem todos da mesma linha.
//
// BALANCEAMENTO (criatura tem 40 HP, ver Creature::hp):
//   Mk I   dano 10, cd 0.33 -> 4 tiros, 0.99s ate matar
//   Mk II  dano 16, cd 0.24 -> 3 tiros, 0.48s
//   Mk III dano 24, cd 0.16 -> 2 tiros, 0.16s  (6x mais rapido que Mk I)
// Mk III fica em 24, ABAIXO dos 40 HP de proposito: nunca mata com um tiro so'. Matar com um tiro foi
// justamente a reclamacao anterior do jogador, e um Mk III que one-shota tornaria as 7 criaturas
// simultaneas irrelevantes.
struct WeaponTier {
    const char* name;
    int   damage;
    float fire_cooldown;
    float range;
    float hit_radius;
    float trace_duration;
    float beam_core_w;
    float beam_halo_w;
    float core_r, core_g, core_b;   // nucleo do feixe
    float halo_r, halo_g, halo_b;   // brilho em volta
    float muzzle_radius;
    int   ring_chunks;              // estilhacos do anel de impacto
    float ring_radius;              // raio final do anel
};

const WeaponTier kWeaponTiers[kWeaponMaxLevel] = {
    // nome     dano   cd    alc  hit   traco  nucleo halo   nucleo RGB           halo RGB             muzzle chunks anel
    { "Mk I",    10, 0.33f, 35.0f, 0.80f, 0.13f, 0.055f, 0.16f, 0.85f,0.97f,1.00f, 0.35f,0.85f,1.00f, 0.120f, 11, 0.60f },
    { "Mk II",   16, 0.24f, 40.0f, 0.90f, 0.15f, 0.075f, 0.22f, 0.92f,0.99f,1.00f, 0.45f,0.90f,1.00f, 0.170f, 14, 0.75f },
    { "Mk III",  24, 0.16f, 46.0f, 1.00f, 0.17f, 0.100f, 0.30f, 1.00f,1.00f,1.00f, 0.60f,0.88f,1.00f, 0.230f, 18, 0.95f },
};

// Abates acumulados. `static` aqui (nao global exposto) pelo mesmo motivo de g_current em
// objectives.cpp: so' este arquivo escreve, todo mundo le pelo acessor.
int g_kills = 0;
// Abates POR TIPO. Nao ha missao usando isto ainda, mas o pedido foi deixar a arquitetura
// preparada pra objetivos como "derrote 3 Brutes" / "derrote 1 Alpha" - sem isso, adicionar essa
// missao depois exigiria mexer no site de morte de novo.
int g_kills_by_type[kEnemyTypeCount] = {0, 0, 0, 0};

// ============= TABELA DE ARQUETIPOS - ver comentario em creatures.h =============
// BALANCEAMENTO calibrado contra kWeaponTiers (mais abaixo) e contra max_speed do jogador = 4.8
// (physics_config.json). O dano por tiro efetivo e' max(1, dano_arma - defense), entao DEFESA pune
// arma fraca de forma nao-linear - e' exatamente onde o upgrade da pistola aparece:
//
//              def  HP  | Mk I (10, 0.33s)   | Mk II (16, 0.24s)  | Mk III (24, 0.16s)
//   Crawler      0   20 |  2 tiros  0.33s    |  2 tiros  0.24s    |  1 tiro
//   Stalker      2   55 |  7 tiros  1.98s    |  4 tiros  0.72s    |  3 tiros  0.32s
//   Brute        5  150 | 30 tiros  9.57s    | 14 tiros  3.12s    |  8 tiros  1.12s
//   Alpha        7  240 | 80 tiros 26.07s    | 27 tiros  6.24s    | 15 tiros  2.24s
//
// Alpha com Mk I e' proibitivo de proposito, e isso NAO e' esponja de HP: Alpha so' aparece na fase
// Terraformado (ver pick_spawn_type), que e' o ponto de vitoria - o jogador chega la com Mk II/III.
// Brute so' aparece em Habitavel. A fauna acompanha a terraformacao, entao "o planeta ficou mais
// perigoso" e "minha arma ficou melhor" andam juntos em vez de um anular o outro.
//
// VELOCIDADE contra 4.8 do jogador: Crawler 5.4 ALCANCA (nao da pra simplesmente fugir), Alpha 4.3
// quase alcanca, Stalker 3.9 da pra fugir, Brute 2.1 e' facil de deixar atras. Isso e' o que faz
// cada encontro pedir uma decisao diferente.
const EnemyArchetype kArchetypes[kEnemyTypeCount] = {
    // ---------------- CRAWLER: baixo, 6 pernas, rapido, morre facil ----------------
    { "CRAWLER",
      /*hp*/ 20, /*def*/ 0, /*dmg*/ 3, /*range*/ 1.0f, /*atk_cd*/ 0.9f, /*windup*/ 0.15f,
      /*flinch*/ 1.0f,
      // Sem ataque a distancia: e o bicho que CHEGA em cima, rapido e barato.
      /*ranged*/ 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
      /*wander*/ 2.0f, /*chase*/ 5.4f, /*detect*/ 16.0f, /*keep*/ 0.0f,
      /*accel*/ 16.0f, /*turn*/ 7.0f,
      /*scale*/ 0.72f,
      0.30f,0.62f,0.34f,  0.44f,0.80f,0.46f,  0.22f,0.44f,0.26f,
      0.58f,0.88f,0.56f,  0.90f,1.00f,0.70f,
      /*legs*/ 6, /*plates*/ 2, /*antenna*/ 0.06f, /*elite*/ false,
      HitSoundClass::Light, 1.35f },

    // ---------------- STALKER: alto, esguio, 2 pernas longas, reposiciona ----------------
    { "STALKER",
      /*hp*/ 55, /*def*/ 2, /*dmg*/ 7, /*range*/ 1.4f, /*atk_cd*/ 1.5f, /*windup*/ 0.35f,
      /*flinch*/ 0.7f,
      // DARDO: reto (gravidade 0), rapido, alcance longo. Dano 5 (MENOR que o golpe de 7 - o
      // preco de atacar de longe), recarga 2.6s e telegrafe de 0.45s. Ele mantem 4.5 de distancia
      // e agora tem com que atingir dali: virou fustigador em vez de enfeite.
      /*ranged*/ 5, 15.0f, 2.5f, 2.6f, 0.45f, 16.0f, 0.0f, 0.16f,
      /*wander*/ 1.4f, /*chase*/ 3.9f, /*detect*/ 20.0f, /*keep*/ 4.5f,
      /*accel*/ 9.0f, /*turn*/ 4.2f,
      /*scale*/ 1.05f,
      0.46f,0.24f,0.66f,  0.66f,0.40f,0.86f,  0.30f,0.16f,0.46f,
      0.78f,0.56f,0.94f,  0.95f,0.72f,1.00f,
      /*legs*/ 2, /*plates*/ 1, /*antenna*/ 0.22f, /*elite*/ false,
      HitSoundClass::Light, 0.95f },

    // ---------------- BRUTE: massa, carapaca, 4 pernas curtas, quase nao reage ----------------
    { "BRUTE",
      /*hp*/ 150, /*def*/ 5, /*dmg*/ 16, /*range*/ 1.9f, /*atk_cd*/ 2.6f, /*windup*/ 0.80f,
      /*flinch*/ 0.12f,
      // LOB: arco balistico (gravidade 9), lento, telegrafe LONGO de 1.1s. Dano 12, recarga 4.5s.
      // Responde ao problema oposto ao do Stalker: o Brute e lento (2.1 contra 4.8 do jogador),
      // entao bastava andar pra longe e ele ficava inofensivo. Agora manter distancia custa - mas o
      // arco e lento e anunciado, da pra desviar andando de lado.
      /*ranged*/ 12, 19.0f, 4.5f, 4.5f, 1.10f, 11.0f, 9.0f, 0.34f,
      /*wander*/ 0.8f, /*chase*/ 2.1f, /*detect*/ 13.0f, /*keep*/ 0.0f,
      /*accel*/ 2.6f, /*turn*/ 1.4f,
      /*scale*/ 1.55f,
      0.60f,0.30f,0.18f,  0.72f,0.42f,0.24f,  0.40f,0.20f,0.12f,
      0.86f,0.62f,0.34f,  1.00f,0.62f,0.20f,
      /*legs*/ 4, /*plates*/ 5, /*antenna*/ 0.0f, /*elite*/ false,
      HitSoundClass::Armored, 0.62f },

    // ---------------- ALPHA: elite, nucleo energetico, chifres, agressivo ----------------
    { "ALPHA",
      /*hp*/ 240, /*def*/ 7, /*dmg*/ 22, /*range*/ 2.2f, /*atk_cd*/ 1.8f, /*windup*/ 0.55f,
      /*flinch*/ 0.06f,
      // Sem ataque a distancia: o elite e corpo-a-corpo puro, rapido e devastador de perto.
      /*ranged*/ 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
      /*wander*/ 1.6f, /*chase*/ 4.3f, /*detect*/ 24.0f, /*keep*/ 0.0f,
      /*accel*/ 5.5f, /*turn*/ 2.6f,
      /*scale*/ 1.85f,
      0.14f,0.16f,0.22f,  0.20f,0.22f,0.30f,  0.10f,0.11f,0.16f,
      0.95f,0.80f,0.30f,  1.00f,0.85f,0.35f,
      /*legs*/ 4, /*plates*/ 6, /*antenna*/ 0.10f, /*elite*/ true,
      HitSoundClass::Armored, 0.48f },
};

// Numeros de dano flutuantes. Efemeros, sem alocacao por frame - vetor reaproveitado.
struct DamageNumber {
    float x, y, z;
    int amount;
    float timer;
    bool blocked;   // dano cortado pela defesa: cor/texto diferentes
};
std::vector<DamageNumber> g_damage_numbers;
constexpr float kDamageNumberLife = 0.85f;
constexpr size_t kMaxDamageNumbers = 32;

const EnemyArchetype& archetype_of(const Creature& c) {
    int i = (int)c.type;
    if (i < 0 || i >= kEnemyTypeCount) i = 0;
    return kArchetypes[i];
}

// Nivel corrente lido direto do inventario (ver o comentario de weapon_level em creatures.h).
// clamp defensivo: um save antigo tem 1, e nada no jogo escreve fora de 1..3.
const WeaponTier& cur_tier() {
    int lv = g_inventory[(int)Block::LaserPistol];
    lv = std::clamp(lv, 1, kWeaponMaxLevel);
    return kWeaponTiers[lv - 1];
}
struct ImpactFlash { Vec3 pos; Vec3 ground_pos; float timer; };
struct ScorchMark { Vec3 pos; float timer; };
std::vector<ImpactFlash> g_impact_flashes;
std::vector<ScorchMark> g_scorch_marks;

// Hash pseudo-aleatorio deterministico (mesmo indice sempre da o mesmo valor) - mesma
// tecnica ja usada pela onda de choque de pouso do jetpack (main.cpp).
float hash01(int i, float salt) {
    float x = std::sin((float)i * 12.9898f + salt * 78.233f) * 43758.5453f;
    return x - std::floor(x);
}

bool tile_is_wet_or_lava(const World& world, int tx, int tz) {
    Block g = world.get_ground(tx, tz);
    return g == Block::Water || g == Block::Ice || g == Block::Lava;
}

float creature_ground_y(const World& world, float x, float z) {
    int tx = world_to_tile(x);
    int tz = world_to_tile(z);
    if (!world.in_bounds(tx, tz)) return 0.0f;
    return stack_top_height_at(world, tx, tz);
}

// A FAUNA ACOMPANHA A TERRAFORMACAO. Pedido do jogador: "Marte congelado -> criaturas mais simples;
// Degelo -> novas criaturas; Habitavel -> mais fortes; Terraformado -> raras/especiais". Sai de
// graca porque g_phase ja existe e ja e' extern-visivel.
//
// Isso tambem e' o que faz o upgrade da arma NAO anular o desafio: o Brute (def 5) so' aparece
// quando o jogador plausivelmente tem Mk II, e o Alpha (def 7) na fase que E' o ponto de vitoria,
// quando ele tem Mk III. Progressao dos dois lados em vez de um lado esmagando o outro.
EnemyType pick_spawn_type() {
    // Pesos por fase (Crawler, Stalker, Brute, Alpha). Soma qualquer - normalizada no sorteio.
    //
    // A tabela anterior era { 100, 0, 0, 0 } em Congelado: SO' Crawler, o tipo mais fraco. A ideia
    // era "a fauna acompanha a terraformacao", mas na pratica Congelado e' onde o jogo INTEIRO
    // comeca e onde o jogador passa as primeiras horas - e ali ele nunca via nada alem do bicho
    // mais fraco. Foi o que o jogador relatou. Portao total por fase estava errado; o certo e'
    // MISTURA com proporcao deslizante.
    //
    // Agora toda fase tem variedade desde o inicio, e o que muda e' a PROPORCAO: Crawler cai de 60%
    // pra 18%, Brute sobe de 8% pra 36%, e o Alpha so' entra a partir de Degelo (ele e' elite, com
    // 240 HP e defesa 7 - contra a Mk I seriam 80 tiros, nao e' encontro de inicio de jogo).
    //
    // O Brute em Congelado (8%) e' de proposito um encontro de FUGIR, nao de matar: 150 HP e defesa
    // 5 contra a Mk I dao 5 de dano por tiro, ou seja 30 tiros / 10 segundos de fogo continuo. Mas
    // a velocidade dele e' 2.1 contra 4.8 do jogador, entao da pra deixar atras andando. E' uma
    // aparicao que ensina "nem tudo se resolve atirando" sem bloquear o progresso.
    static const int kWeights[5][kEnemyTypeCount] = {
        /* Frozen      */ { 60, 32,  8,  0 },
        /* Warming     */ { 46, 34, 18,  2 },
        /* Thawing     */ { 36, 33, 25,  6 },
        /* Habitable   */ { 26, 30, 32, 12 },
        /* Terraformed */ { 18, 26, 36, 20 },
    };
    int ph = std::clamp((int)g_phase, 0, 4);
    int total = 0;
    for (int i = 0; i < kEnemyTypeCount; ++i) total += kWeights[ph][i];
    if (total <= 0) return EnemyType::Crawler;
    int roll = (int)(rng_next_u32() % (uint32_t)total);
    for (int i = 0; i < kEnemyTypeCount; ++i) {
        roll -= kWeights[ph][i];
        if (roll < 0) return (EnemyType)i;
    }
    return EnemyType::Crawler;
}

void spawn_creature() {
    if (!g_world) return;
    float ang = rng_next_f01() * 2.0f * kPi;
    float dist = kSpawnMinDist + rng_next_f01() * (kSpawnMaxDist - kSpawnMinDist);
    float sx = g_player.pos.x + std::cos(ang) * dist;
    float sz = g_player.pos.y + std::sin(ang) * dist;
    int tx = world_to_tile(sx);
    int tz = world_to_tile(sz);
    if (!g_world->in_bounds(tx, tz)) return;
    if (tile_is_wet_or_lava(*g_world, tx, tz)) return;

    Creature c;
    c.x = tile_center(tx);
    c.z = tile_center(tz);
    c.y = stack_top_height_at(*g_world, tx, tz);
    c.wander_target_x = c.x;
    c.wander_target_z = c.z;
    c.yaw = rng_next_f01() * 2.0f * kPi;
    c.type = pick_spawn_type();
    // HP vem do arquetipo, nao de um literal na struct - adicionar um tipo novo nao exige tocar aqui.
    const EnemyArchetype& a = kArchetypes[(int)c.type];
    c.hp = a.max_hp;
    c.max_hp = a.max_hp;
    g_creatures.push_back(c);
}

void pick_new_wander_target(Creature& c) {
    if (!g_world) return;
    for (int attempt = 0; attempt < 5; ++attempt) {
        float ang = rng_next_f01() * 2.0f * kPi;
        float dist = 4.0f + rng_next_f01() * 8.0f;
        float tx_f = c.x + std::cos(ang) * dist;
        float tz_f = c.z + std::sin(ang) * dist;
        int tx = world_to_tile(tx_f);
        int tz = world_to_tile(tz_f);
        if (!g_world->in_bounds(tx, tz)) continue;
        if (tile_is_wet_or_lava(*g_world, tx, tz)) continue;
        c.wander_target_x = tile_center(tx);
        c.wander_target_z = tile_center(tz);
        return;
    }
    // Sem alvo valido por perto - fica parado ate a proxima tentativa.
    c.wander_target_x = c.x;
    c.wander_target_z = c.z;
}

} // namespace

void notify_player_respawned() {
    g_respawn_grace = kRespawnGraceSeconds;
}

void update_creatures(float dt) {
    g_fire_cooldown = std::max(0.0f, g_fire_cooldown - dt);
    g_laser_trace_timer = std::max(0.0f, g_laser_trace_timer - dt);
    g_respawn_grace = std::max(0.0f, g_respawn_grace - dt);
    g_muzzle_flash_timer = std::max(0.0f, g_muzzle_flash_timer - dt);
    // Feedback de dano no jogador: a cunha direcional + o "-N" somem sozinhos. Sem este
    // decremento o indicador ficava CONGELADO na tela para sempre (bug reportado: "esses valores
    // -10 e -3 estao travados no ar e nunca somem").
    g_player_hit_timer = std::max(0.0f, g_player_hit_timer - dt);

    // Numeros de dano flutuantes: sobem e expiram. Sem este laco eles nunca saiam da tela E o
    // vetor enchia ate kMaxDamageNumbers, ponto em que PARAVA de mostrar numero novo - o mesmo
    // defeito produzia os dois sintomas.
    for (size_t di = 0; di < g_damage_numbers.size();) {
        g_damage_numbers[di].timer -= dt;
        if (g_damage_numbers[di].timer <= 0.0f) {
            g_damage_numbers.erase(g_damage_numbers.begin() + (long)di);
        } else {
            ++di;
        }
    }

    for (auto it = g_impact_flashes.begin(); it != g_impact_flashes.end();) {
        it->timer -= dt;
        if (it->timer <= 0.0f) it = g_impact_flashes.erase(it); else ++it;
    }
    for (auto it = g_scorch_marks.begin(); it != g_scorch_marks.end();) {
        it->timer -= dt;
        if (it->timer <= 0.0f) it = g_scorch_marks.erase(it); else ++it;
    }

    if (!g_world) return;

    // ---- Spawn ----
    static float spawn_timer = 0.0f;
    static float spawn_next = kSpawnMinInterval + 0.0f; // primeiro sorteio real logo abaixo
    static bool spawn_seeded = false;
    if (!spawn_seeded) {
        spawn_next = kSpawnMinInterval + rng_next_f01() * (kSpawnMaxInterval - kSpawnMinInterval);
        spawn_seeded = true;
    }
    spawn_timer += dt;
    if (spawn_timer >= spawn_next) {
        spawn_timer = 0.0f;
        spawn_next = kSpawnMinInterval + rng_next_f01() * (kSpawnMaxInterval - kSpawnMinInterval);
        float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
        float night_alpha = compute_night_alpha(day_phase);
        if ((int)g_creatures.size() < kMaxCreatures && night_alpha > kNightSpawnGate &&
            interior_at(g_player.pos.x, g_player.pos.y) < 0) {
            // Nunca dentro de um interior: o spawn e' a 20-45 tiles do JOGADOR, entao estando numa
            // sala do distrito a criatura nasceria dentro do proprio laboratorio/dormitorio.
            spawn_creature();
        }
    }

    // ---- IA + dano de contato ----
    for (size_t i = 0; i < g_creatures.size();) {
        Creature& c = g_creatures[(size_t)i];
        c.contact_cooldown = std::max(0.0f, c.contact_cooldown - dt);
        c.anim_timer += dt;

        int ctx = world_to_tile(c.x);
        int ctz = world_to_tile(c.z);
        if (!g_world->in_bounds(ctx, ctz) || tile_is_wet_or_lava(*g_world, ctx, ctz)) {
            g_creatures.erase(g_creatures.begin() + (long)i);
            continue;
        }

        const EnemyArchetype& a = archetype_of(c);
        float dx = g_player.pos.x - c.x;
        float dz = g_player.pos.y - c.z;
        float dist2 = dx * dx + dz * dz;

        if (dist2 > kDespawnDist * kDespawnDist) {
            g_creatures.erase(g_creatures.begin() + (long)i);
            continue;
        }

        // Troca de lado do strafe a cada ~2.5s (Stalker). Fica aqui, num cronometro proprio, e nao
        // derivado de anim_timer no calculo do alvo.
        c.strafe_timer -= dt;
        if (c.strafe_timer <= 0.0f) {
            c.strafe_side = -c.strafe_side;
            c.strafe_timer = 2.0f + rng_next_f01() * 1.5f;
        }

        // Cronometros de reacao/HUD.
        if (c.flinch > 0.0f) c.flinch = std::max(0.0f, c.flinch - dt);
        if (c.hp_bar_timer > 0.0f) c.hp_bar_timer = std::max(0.0f, c.hp_bar_timer - dt);

        // ---- DETECCAO: alcance por arquetipo (o Alpha ve de 24, o Brute so' de 13) ----
        bool graced = g_respawn_grace > 0.0f;
        float det2 = a.detect_range * a.detect_range;
        if (!graced && dist2 <= det2) {
            c.state = Creature::State::Chasing;
        } else if (graced || dist2 > det2 * 1.4f) {
            c.state = Creature::State::Wandering;
        }

        // ---- ALVO DE MOVIMENTO: uma IA so', parametrizada ----
        // keep_distance > 0 (Stalker) faz ele RECUAR quando fica perto demais e circular em vez de
        // colar no jogador. Os outros vem direto. Nao ha IA por arquetipo - so' parametros.
        float speed = (c.state == Creature::State::Chasing) ? a.chase_speed : a.wander_speed;
        float tx_move, tz_move;
        if (c.state == Creature::State::Chasing) {
            float dist = std::sqrt(std::max(0.0001f, dist2));
            if (a.keep_distance > 0.0f && dist < a.keep_distance && c.windup <= 0.0f) {
                // Reposiciona: recua na diagonal (afasta + tangencia), o que le como flanqueio.
                float nx = -dx / dist, nz = -dz / dist;
                float tanx = -nz, tanz = nx;
                // Lado guardado na criatura e trocado por cronometro - derivar de anim_timer a cada
                // frame fazia o alvo saltar de lado no meio do passo e o Stalker tremia no lugar.
                float side = c.strafe_side;
                tx_move = c.x + (nx * 0.7f + tanx * side * 0.7f) * 4.0f;
                tz_move = c.z + (nz * 0.7f + tanz * side * 0.7f) * 4.0f;
            } else {
                tx_move = g_player.pos.x;
                tz_move = g_player.pos.y;
            }
        } else {
            c.wander_timer -= dt;
            if (c.wander_timer <= 0.0f) {
                pick_new_wander_target(c);
                c.wander_timer = 3.0f + rng_next_f01() * 3.0f;
            }
            tx_move = c.wander_target_x;
            tz_move = c.wander_target_z;
        }

        // Durante a carga do golpe a criatura NAO se move: e' o que torna o telegrafe util - da
        // pra sair de perto de um Brute nos 0.8s em que ele esta se preparando.
        float move_mul = (c.windup > 0.0f) ? 0.0f : 1.0f;
        // Recuo do impacto tambem freia (proporcional a flinch_scale: quase nada num Brute).
        if (c.flinch > 0.0f) move_mul *= (1.0f - a.flinch_scale * 0.75f);

        // ================= MOVIMENTO COM INERCIA E GIRO SUAVE =================
        // Antes: posicao integrada direto do vetor pro alvo, com yaw atribuido por atan2 no mesmo
        // frame. Resultado: velocidade instantanea (0 -> maxima -> 0 sem transicao), giro em L
        // (virava 180 graus num frame) e altura colada no terreno (pulinho a cada degrau de tile).
        // Era isso que lia como "movimento estranho".
        //
        // Agora ha 3 suavizacoes, todas parametrizadas por arquetipo (peso vem de numeros, nao de
        // codigo separado): aceleracao pra velocidade, taxa de giro pro yaw, e lerp pra altura.
        float des_vx = 0.0f, des_vz = 0.0f;
        float mdx = tx_move - c.x;
        float mdz = tz_move - c.z;
        float mdist = std::sqrt(mdx * mdx + mdz * mdz);
        if (mdist > 0.05f) {
            // Chegando perto do alvo de perambulacao, desacelera em vez de parar seco.
            float approach = (c.state == Creature::State::Wandering)
                           ? clamp01(mdist / 1.5f) : 1.0f;
            des_vx = (mdx / mdist) * speed * move_mul * approach;
            des_vz = (mdz / mdist) * speed * move_mul * approach;
        }
        // Aproxima a velocidade atual da desejada. O Brute (accel 2.6) leva ~0.8s pra atingir a
        // velocidade de perseguicao; o Crawler (16.0) chega quase na hora.
        float ak = std::min(1.0f, a.accel * dt);
        c.vel_x += (des_vx - c.vel_x) * ak;
        c.vel_z += (des_vz - c.vel_z) * ak;
        c.x += c.vel_x * dt;
        c.z += c.vel_z * dt;

        // ---- GIRO LIMITADO POR TAXA ----
        // Encara o jogador quando perseguindo, a direcao do movimento quando perambulando. O giro
        // caminha ate o alvo a turn_rate rad/s, entao o Brute (1.4) leva mais de 2s pra dar meia
        // volta - e' o que faz ele parecer pesado em vez de um cursor girando.
        float want_yaw;
        if (c.state == Creature::State::Chasing) want_yaw = std::atan2(dz, dx);
        else if (std::fabs(c.vel_x) + std::fabs(c.vel_z) > 0.05f) want_yaw = std::atan2(c.vel_z, c.vel_x);
        else want_yaw = c.yaw;
        float dyaw = want_yaw - c.yaw;
        while (dyaw > kPi)  dyaw -= 2.0f * kPi;   // caminho mais curto
        while (dyaw < -kPi) dyaw += 2.0f * kPi;
        float max_turn = a.turn_rate * dt;
        float applied = std::clamp(dyaw, -max_turn, max_turn);
        c.yaw += applied;
        c.last_yaw_rate = applied / std::max(0.0001f, dt);

        // ---- MARCHA E INCLINACAO ----
        // Fase da marcha por DISTANCIA percorrida (nao por tempo): stride = passada em unidades de
        // mundo por ciclo completo, escalada pelo porte do bicho. speed/stride da ciclos por
        // segundo, entao o pe acompanha o chao e nao patina. Parado, a fase para.
        float speed_now = std::sqrt(c.vel_x * c.vel_x + c.vel_z * c.vel_z);
        // PASSADA: derivada da capacidade de velocidade do bicho, nao so' do porte. Com stride
        // proporcional apenas a escala, o Crawler (chase 5.4, escala pequena) dava 12.1 ciclos por
        // segundo correndo - 5 frames por ciclo a 60fps, o que le como VIBRACAO, nao corrida. Foi a
        // medicao que pegou isso antes de virar bug visual.
        //
        // Dividindo a velocidade de perseguicao por uma cadencia-alvo, todo tipo corre com a mesma
        // legibilidade (~3 ciclos/s) e vaga em ~1.1, e a passada continua CONSTANTE por tipo - que
        // e' o que impede o pe de patinar. O piso por escala protege um tipo futuro que seja lento.
        constexpr float kTargetCadence = 3.0f;   // ciclos por segundo na velocidade de perseguicao
        float stride = std::max(0.55f * a.scale, a.chase_speed / kTargetCadence);
        c.gait_phase += (speed_now / std::max(0.15f, stride)) * dt;
        if (c.gait_phase > 1000.0f) c.gait_phase -= 1000.0f;   // nao deixa crescer sem limite

        // ACELERACAO -> inclinacao pra frente. Projeta a variacao de velocidade no eixo FRENTE do
        // bicho: acelerando ele mergulha o corpo pra frente, freando joga pra tras. E' o sinal de
        // peso que faltava - antes o corpo ficava sempre perpendicular ao chao.
        float acc_x = (c.vel_x - c.prev_vel_x) / std::max(0.0001f, dt);
        float acc_z = (c.vel_z - c.prev_vel_z) / std::max(0.0001f, dt);
        c.prev_vel_x = c.vel_x;
        c.prev_vel_z = c.vel_z;
        float fx = std::cos(c.yaw), fz = std::sin(c.yaw);
        float acc_fwd = acc_x * fx + acc_z * fz;
        // Alvo de inclinacao: pela aceleracao mais um mergulho constante proporcional a velocidade
        // (corpo de bicho correndo fica baixo e pra frente). Limitado pra nao virar cambalhota.
        float want_pitch = std::clamp(acc_fwd * 0.016f + (speed_now / std::max(0.5f, a.chase_speed)) * 0.10f,
                                      -0.30f, 0.34f);
        // Os pesados inclinam menos (massa) - reusa flinch_scale, que ja e' a "leveza" do arquetipo.
        want_pitch *= (0.45f + 0.55f * a.flinch_scale);
        c.lean_pitch += (want_pitch - c.lean_pitch) * std::min(1.0f, 6.0f * dt);

        // GIRO -> inclinacao lateral. Calculada no bloco de giro logo abaixo (yaw_rate), aplicada
        // aqui no frame seguinte - um frame de atraso e' invisivel e evita reordenar o bloco.
        float want_roll = std::clamp(-c.last_yaw_rate * 0.085f, -0.26f, 0.26f);
        want_roll *= (0.45f + 0.55f * a.flinch_scale);
        c.lean_roll += (want_roll - c.lean_roll) * std::min(1.0f, 7.0f * dt);

        // ---- ALTURA SUAVIZADA ----
        float ground = creature_ground_y(*g_world, c.x, c.z);
        if (!c.y_init) { c.smooth_y = ground; c.y_init = true; }
        // Sobe rapido (nao afunda em degrau) e desce um pouco mais devagar (nao "cai" de repente).
        float yk = (ground > c.smooth_y) ? std::min(1.0f, 14.0f * dt) : std::min(1.0f, 8.0f * dt);
        c.smooth_y += (ground - c.smooth_y) * yk;
        c.y = c.smooth_y;


        // ================= ATAQUE A DISTANCIA =================
        // Uma rotina so' pros dois tipos que atiram. O que muda entre o dardo do Stalker e o lob do
        // Brute sao NUMEROS da tabela (velocidade, gravidade, dano, recarga, telegrafe) - nao ha
        // "atirar do Stalker" e "atirar do Brute" em codigo separado.
        if (a.ranged_damage > 0) {
            if (c.ranged_cd > 0.0f) c.ranged_cd = std::max(0.0f, c.ranged_cd - dt);

            if (c.ranged_windup_t > 0.0f) {
                c.ranged_windup_t = std::max(0.0f, c.ranged_windup_t - dt);
                if (c.ranged_windup_t <= 0.0f) {
                    // DISPARA na posicao ATUAL do jogador, sem prever movimento: e' isso que torna
                    // o tiro desviavel - quem andar durante o voo do projetil escapa.
                    float px = g_player.pos.x, pz = g_player.pos.y;
                    float mz_y = c.y + 0.75f * a.scale;                  // altura do bocal
                    float tgt_y = g_player.pos_y + 0.90f;                // torso do jogador
                    float ddx = px - c.x, ddz = pz - c.z;
                    float hd = std::sqrt(std::max(0.0001f, ddx * ddx + ddz * ddz));
                    float sp = std::max(1.0f, a.projectile_speed);
                    float tof = hd / sp;                                  // tempo de voo horizontal
                    // Solucao balistica EXATA pra esta velocidade horizontal. Com gravidade 0 isso
                    // degenera em vy = dy/t, ou seja tiro reto - uma formula so' pros dois casos.
                    float vy = ((tgt_y - mz_y) + 0.5f * a.projectile_gravity * tof * tof) / tof;
                    if (g_creature_projectiles.size() < kMaxCreatureProjectiles) {
                        CreatureProjectile p{};
                        p.x = c.x + (ddx / hd) * 0.45f * a.scale;
                        p.y = mz_y;
                        p.z = c.z + (ddz / hd) * 0.45f * a.scale;
                        p.vx = (ddx / hd) * sp;
                        p.vz = (ddz / hd) * sp;
                        p.vy = vy;
                        p.gravity = a.projectile_gravity;
                        p.radius = a.projectile_radius;
                        p.damage = a.ranged_damage;
                        p.life = tof * 2.4f + 1.0f;   // folga pra errar sem ficar eterno
                        p.r = a.eye_r; p.g = a.eye_g; p.b = a.eye_b;
                        p.spin = rng_next_f01() * 6.2831853f;
                        g_creature_projectiles.push_back(p);
                    }
                    play_creature_attack_hit_sound(a.sound_pitch * 1.15f);
                    c.ranged_cd = a.ranged_cooldown;
                }
            } else if (!graced && c.state == Creature::State::Chasing && c.ranged_cd <= 0.0f &&
                       c.windup <= 0.0f) {
                float d = std::sqrt(std::max(0.0001f, dist2));
                if (d >= a.ranged_min_range && d <= a.ranged_range) {
                    // Telegrafe: som + pausa antes do tiro, mesma ideia do golpe corpo-a-corpo. E' o
                    // que da chance de reagir - sem isso o dano vem do nada, de longe.
                    c.ranged_windup_t = a.ranged_windup;
                    play_creature_windup_sound(a.sound_pitch * 0.9f);
                }
            }
        }

        // ================= ATAQUE COM TELEGRAFE =================
        // Antes o dano de contato saia INSTANTANEO ao entrar no raio: o jogador levava dano sem ter
        // como reagir e sem saber de onde veio. Agora: entra no alcance -> som de carga + pausa ->
        // golpe. A duracao da carga vem do arquetipo (0.15s no Crawler, 0.80s no Brute).
        if (c.windup > 0.0f) {
            c.windup = std::max(0.0f, c.windup - dt);
            if (c.windup <= 0.0f) {
                // Fim da carga: acerta se o jogador AINDA estiver no alcance (com folga pequena) -
                // sair de perto durante a carga faz o golpe passar em branco.
                float now2 = (g_player.pos.x - c.x) * (g_player.pos.x - c.x) +
                             (g_player.pos.y - c.z) * (g_player.pos.y - c.z);
                float reach = a.attack_range * 1.25f;
                if (!graced && now2 <= reach * reach) {
                    g_player.hp -= a.contact_damage;
                    // Feedback de dano no jogador: som + flash direcional no HUD (ver
                    // g_player_hit_* abaixo), nao apenas o numero de HP caindo.
                    play_creature_attack_hit_sound(a.sound_pitch);
                    float ddx = c.x - g_player.pos.x, ddz = c.z - g_player.pos.y;
                    float dl = std::sqrt(std::max(0.0001f, ddx * ddx + ddz * ddz));
                    g_player_hit_dir_x = ddx / dl;
                    g_player_hit_dir_z = ddz / dl;
                    g_player_hit_timer = kPlayerHitFlashSeconds;
                    g_player_hit_amount = a.contact_damage;
                    g_screen_flash_red = std::max(g_screen_flash_red, 0.16f);
                    if (g_player.hp <= 0) {
                        g_player.hp = 0;
                        respawn_player_at_base(a.name);   // o nome do arquetipo ja diz o que matou
                    }
                }
            }
        } else if (!graced && c.contact_cooldown <= 0.0f &&
                   dist2 <= a.attack_range * a.attack_range) {
            c.windup = a.attack_windup;
            c.contact_cooldown = a.attack_cooldown;
            play_creature_windup_sound(a.sound_pitch);
        }

        ++i;
    }

    // ================= PROJETEIS EM VOO =================
    // Integra, aplica gravidade, testa acerto no jogador e no terreno. O terreno bloquear e' o que
    // faz cobertura funcionar: atirar atraves de um morro simplesmente desperdica o tiro, sem
    // precisar de teste de linha de visao na hora de disparar.
    for (size_t i = 0; i < g_creature_projectiles.size();) {
        CreatureProjectile& p = g_creature_projectiles[i];
        p.vy -= p.gravity * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.z += p.vz * dt;
        p.life -= dt;
        p.spin += dt * 9.0f;

        bool gone = (p.life <= 0.0f);

        // Acerto no jogador: cilindro em volta do torso, generoso o bastante pra o tiro nao
        // atravessar de raspao, apertado o bastante pra andar de lado escapar.
        if (!gone) {
            float ddx = p.x - g_player.pos.x, ddz = p.z - g_player.pos.y;
            float ddy = p.y - (g_player.pos_y + 0.90f);
            float hit_r = p.radius + 0.55f;
            if (ddx * ddx + ddz * ddz <= hit_r * hit_r && std::fabs(ddy) <= 1.05f) {
                g_player.hp -= p.damage;
                play_creature_attack_hit_sound(1.0f);
                // Mesmo feedback do golpe corpo-a-corpo: flash direcional no HUD, nao so' o numero
                // de HP caindo. Reusa g_player_hit_*.
                float dl = std::sqrt(std::max(0.0001f, ddx * ddx + ddz * ddz));
                g_player_hit_dir_x = ddx / dl;
                g_player_hit_dir_z = ddz / dl;
                g_player_hit_timer = kPlayerHitFlashSeconds;
                g_player_hit_amount = p.damage;
                g_screen_flash_red = std::max(g_screen_flash_red, 0.20f);
                if (g_player.hp <= 0) {
                    g_player.hp = 0;
                    respawn_player_at_base("Tiro de criatura");
                }
                gone = true;
            }
        }
        // Acerto no terreno.
        if (!gone && g_world) {
            int tx = world_to_tile(p.x), tz = world_to_tile(p.z);
            if (!g_world->in_bounds(tx, tz)) gone = true;
            else if (p.y <= creature_ground_y(*g_world, p.x, p.z)) gone = true;
        }

        if (gone) {
            // Estouro no ponto de impacto - reusa o flash de impacto que o laser ja usa.
            g_impact_flashes.push_back({{p.x, p.y, p.z}, {p.x, p.y, p.z}, kImpactFlashDuration * 0.6f});
            g_creature_projectiles[i] = g_creature_projectiles.back();
            g_creature_projectiles.pop_back();
        } else {
            ++i;
        }
    }
}

void render_creatures() {
    // Marcas de queimado (decais escuros no chao) - desenhadas primeiro, embaixo de tudo.
    for (const ScorchMark& s : g_scorch_marks) {
        float a = clamp01(s.timer / kScorchMarkDuration) * 0.55f;
        render_plane_3d(s.pos.x, s.pos.y + 0.015f, s.pos.z, 0.65f, 0.05f, 0.04f, 0.03f, a);
    }

    for (const Creature& c : g_creatures) {
        const EnemyArchetype& a = archetype_of(c);
        // ---- MARCHA ----
        // `step` vem de gait_phase (regida por DISTANCIA percorrida, ver update_creatures), nao mais
        // de anim_timer*6*gait. Consequencias diretas no que o jogador ve:
        //   - parado, step congela: o bicho para de marchar no lugar;
        //   - correndo, a cadencia acompanha a velocidade: o pe nao patina no chao.
        bool chasing = (c.state == Creature::State::Chasing);
        float step = std::sin(c.gait_phase * 2.0f * kPi);
        // Fracao da velocidade maxima: escala amplitude de passo, bob e balanco do corpo. Parado da
        // 0 e tudo desliga menos a respiracao.
        float spd_frac = clamp01(std::sqrt(c.vel_x * c.vel_x + c.vel_z * c.vel_z) /
                                 std::max(0.5f, a.chase_speed));
        // RESPIRACAO no lugar: pequena, lenta, sempre presente. Sem ela um bicho parado fica uma
        // estatua absoluta - o oposto do problema da marcha no lugar, mas igualmente morto.
        float breath = std::sin(c.anim_timer * 1.6f + (float)(int)c.type) * 0.012f * (1.0f - spd_frac);
        float pulse = 0.74f + 0.26f * std::sin(c.anim_timer * 3.0f + (float)(int)c.type);
        float S = a.scale;

        // Reacao ao dano: piscada branca + recuo. flinch_scale da tabela controla o quanto - um
        // Brute quase nao se move (massa), um Crawler e' jogado pra tras. Sem isso o combate nao
        // tinha peso: o inimigo absorvia tiro sem dar sinal nenhum.
        float fl = clamp01(c.flinch / kFlinchDuration);
        float hit_flash = fl * 0.8f;
        float knock = fl * a.flinch_scale * 0.22f;

        // Bob: amplitude proporcional a VELOCIDADE (parado nao balanca) e regida pela marcha, nao
        // por um seno solto no tempo. Os pesados sobem e descem menos (inercia). A respiracao entra
        // por cima pra o bicho parado nao virar estatua.
        float bob_amp = ((a.scale > 1.4f) ? 0.030f : 0.065f) * spd_frac;
        float bob = std::fabs(std::sin(c.gait_phase * 2.0f * kPi)) * bob_amp + breath;

        float fwd_x = std::cos(c.yaw), fwd_z = std::sin(c.yaw);
        float box_yaw = std::atan2(fwd_x, fwd_z);
        float sd_x = fwd_z, sd_z = -fwd_x;
        // Inclinacao combinada. render_box_tilted_3d aceita um pitch; combino mergulho (aceleracao)
        // e rolagem (curva) num angulo unico - com pecas em caixa a diferenca entre os dois eixos
        // nao le, e um valor so evita duplicar a geometria.
        float lean = c.lean_pitch + c.lean_roll * 0.6f;
        float base_y = c.y + bob;
        float cx = c.x + c.flinch_dx * knock;
        float cz = c.z + c.flinch_dz * knock;
        auto P = [&](float f, float u, float s) -> Vec3 {
            return Vec3{cx + fwd_x * f + sd_x * s, base_y + u, cz + fwd_z * f + sd_z * s};
        };
        // Peca de corpo: cor base * pulso + piscada de impacto.
        auto B = [&](float f, float u, float s, float sx, float sy, float sz,
                     float r, float g, float b) {
            float m = pulse;
            // box TILTED (nao oriented): a inclinacao de aceleracao/curva (lean_pitch/lean_roll) e o
            // que da peso ao movimento - sem ela o corpo fica sempre perpendicular ao chao, que e
            // metade da leitura de "boneco duro".
            render_box_tilted_3d(P(f * S, u * S, s * S), sx * S, sy * S, sz * S, box_yaw, lean,
                                 r * m + hit_flash, g * m + hit_flash, b * m + hit_flash, 1.0f);
        };
        // Emissivo (olhos, nucleo): sem pulse, com a piscada.
        auto E = [&](float f, float u, float s, float sx, float sy, float sz,
                     float r, float g, float b) {
            render_box_tilted_3d(P(f * S, u * S, s * S), sx * S, sy * S, sz * S, box_yaw, lean,
                                 r + hit_flash, g + hit_flash, b + hit_flash, 1.0f);
        };

        // Marca no chao: mais forte no elite.
        rlSetBlendMode(RL_BLEND_ADDITIVE);
        rlDisableDepthMask();
        render_glow_disc_3d({cx, c.y + 0.02f, cz}, 0.34f * S,
                            a.body_r, a.body_g, a.body_b, (a.elite ? 0.26f : 0.15f) * pulse, 12);
        rlEnableDepthMask();
        rlSetBlendMode(RL_BLEND_ALPHA);

        // ================= SILHUETAS: cada tipo monta um corpo diferente =================
        // Nao e' o mesmo corpo em escalas diferentes - a proporcao, a postura, a contagem de
        // membros e as pecas mudam, entao a silhueta e' reconhecivel de longe.
        switch (c.type) {
        case EnemyType::Crawler:
            // BAIXO E LARGO, rente ao chao, sem cabeca destacada - um "inseto rasteiro".
            B(-0.06f, 0.16f, 0.0f, 0.40f, 0.16f, 0.44f, a.body_r, a.body_g, a.body_b);
            B(0.16f, 0.17f, 0.0f, 0.28f, 0.14f, 0.22f, a.head_r, a.head_g, a.head_b);
            for (int p = 0; p < a.plates; ++p) {
                B(-0.14f + (float)p * 0.14f, 0.25f, 0.0f, 0.26f, 0.05f, 0.09f,
                  a.plate_r, a.plate_g, a.plate_b);
            }
            for (float sgn : {-1.0f, 1.0f}) {
                E(0.28f, 0.19f, 0.05f * sgn, 0.05f, 0.05f, 0.04f, a.eye_r, a.eye_g, a.eye_b);
                // Palpos curtos.
                B(0.30f, 0.12f, 0.06f * sgn, 0.05f, 0.04f, 0.10f, a.plate_r, a.plate_g, a.plate_b);
            }
            break;

        case EnemyType::Stalker:
            // ALTO E ESGUIO: pernas longas, tronco vertical estreito, cabeca inclinada a frente.
            B(0.0f, 0.74f, 0.0f, 0.20f, 0.44f, 0.20f, a.body_r, a.body_g, a.body_b);
            B(-0.02f, 0.44f, 0.0f, 0.16f, 0.24f, 0.16f, a.body_r * 0.9f, a.body_g * 0.9f, a.body_b * 0.9f);
            B(0.10f, 1.02f, 0.0f, 0.17f, 0.15f, 0.24f, a.head_r, a.head_g, a.head_b);
            for (int p = 0; p < a.plates; ++p) {
                B(-0.08f, 0.88f - (float)p * 0.14f, 0.0f, 0.22f, 0.06f, 0.07f,
                  a.plate_r, a.plate_g, a.plate_b);
            }
            for (float sgn : {-1.0f, 1.0f}) {
                E(0.19f, 1.06f, 0.05f * sgn, 0.05f, 0.04f, 0.04f, a.eye_r, a.eye_g, a.eye_b);
                // Bracos longos pendendo - a leitura de "alcance".
                B(0.06f, 0.66f, 0.15f * sgn, 0.06f, 0.34f, 0.06f, a.limb_r, a.limb_g, a.limb_b);
                B(0.12f, 0.44f, 0.17f * sgn, 0.05f, 0.22f, 0.05f, a.limb_r, a.limb_g, a.limb_b);
            }
            break;

        case EnemyType::Brute:
            // MASSA: tronco enorme e largo, ombros salientes, cabeca afundada entre eles, carapaca
            // de placas. Ocupa muito mais espaco lateral que altura.
            B(-0.10f, 0.42f, 0.0f, 0.62f, 0.44f, 0.56f, a.body_r, a.body_g, a.body_b);
            B(0.08f, 0.56f, 0.0f, 0.54f, 0.30f, 0.40f, a.body_r * 1.1f, a.body_g * 1.1f, a.body_b * 1.1f);
            // Ombros.
            for (float sgn : {-1.0f, 1.0f}) {
                B(0.02f, 0.62f, 0.30f * sgn, 0.22f, 0.24f, 0.22f, a.plate_r, a.plate_g, a.plate_b);
            }
            // Cabeca baixa, entre os ombros.
            B(0.28f, 0.50f, 0.0f, 0.26f, 0.22f, 0.24f, a.head_r, a.head_g, a.head_b);
            // Carapaca dorsal escalonada.
            for (int p = 0; p < a.plates; ++p) {
                float t = (float)p / (float)std::max(1, a.plates - 1);
                B(-0.28f + t * 0.34f, 0.68f - t * 0.03f, 0.0f, 0.44f - t * 0.10f, 0.10f, 0.10f,
                  a.plate_r, a.plate_g, a.plate_b);
            }
            for (float sgn : {-1.0f, 1.0f}) {
                E(0.40f, 0.54f, 0.07f * sgn, 0.06f, 0.05f, 0.04f, a.eye_r, a.eye_g, a.eye_b);
                // Presas grossas pra frente.
                B(0.42f, 0.40f, 0.09f * sgn, 0.08f, 0.08f, 0.16f, a.plate_r, a.plate_g, a.plate_b);
            }
            break;

        case EnemyType::Alpha:
            // ELITE: corpo escuro quase preto com NUCLEO ENERGETICO exposto no peito, coroa de
            // chifres e placas dorsais altas. Domina em altura E largura.
            B(-0.10f, 0.52f, 0.0f, 0.58f, 0.52f, 0.52f, a.body_r, a.body_g, a.body_b);
            B(0.12f, 0.70f, 0.0f, 0.46f, 0.34f, 0.36f, a.head_r, a.head_g, a.head_b);
            // Nucleo: pulsa forte, e' o que marca "esta e' diferente".
            {
                float core = 0.6f + 0.4f * std::sin(c.anim_timer * 4.5f);
                E(0.30f, 0.62f, 0.0f, 0.16f, 0.16f, 0.10f,
                  a.eye_r * core, a.eye_g * core * 0.8f, a.eye_b * core * 0.4f);
                rlSetBlendMode(RL_BLEND_ADDITIVE);
                rlDisableDepthMask();
                render_glow_disc_3d(P(0.36f * S, 0.62f * S, 0.0f), 0.30f * S,
                                    1.0f, 0.78f, 0.30f, 0.40f * core, 14);
                rlEnableDepthMask();
                rlSetBlendMode(RL_BLEND_ALPHA);
            }
            // Coroa de chifres.
            for (int h = 0; h < 3; ++h) {
                float sgn = (h == 1) ? 0.0f : ((h == 0) ? -1.0f : 1.0f);
                B(0.16f, 0.96f + (h == 1 ? 0.08f : 0.0f), 0.13f * sgn,
                  0.07f, 0.26f + (h == 1 ? 0.10f : 0.0f), 0.07f,
                  a.plate_r, a.plate_g, a.plate_b);
            }
            // Placas dorsais altas.
            for (int p = 0; p < a.plates; ++p) {
                float t = (float)p / (float)std::max(1, a.plates - 1);
                B(-0.32f + t * 0.30f, 0.84f + t * 0.06f, 0.0f, 0.36f - t * 0.08f, 0.18f, 0.07f,
                  a.plate_r, a.plate_g, a.plate_b);
            }
            for (float sgn : {-1.0f, 1.0f}) {
                E(0.34f, 0.80f, 0.09f * sgn, 0.06f, 0.06f, 0.04f, a.eye_r, a.eye_g, a.eye_b);
            }
            break;
        }

        // ---- PERNAS: a contagem vem da tabela (6 / 2 / 4 / 4) ----
        // Uma so' rotina pros 4 tipos: as pernas se distribuem ao longo do corpo, com fase
        // alternada. Nao ha implementacao de perna por arquetipo.
        {
            int n = a.legs;
            float body_len = (c.type == EnemyType::Crawler) ? 0.34f : 0.26f;
            float hip_y  = (c.type == EnemyType::Stalker) ? 0.42f : ((c.type == EnemyType::Crawler) ? 0.14f : 0.34f);
            float leg_len = (c.type == EnemyType::Stalker) ? 0.42f : ((c.type == EnemyType::Crawler) ? 0.12f : 0.30f);
            float spread = (c.type == EnemyType::Crawler) ? 0.24f : ((c.type == EnemyType::Stalker) ? 0.10f : 0.30f);
            int per_side = std::max(1, n / 2);
            for (int i = 0; i < n; ++i) {
                int side = (i % 2 == 0) ? -1 : 1;
                int idx = i / 2;
                float along = (per_side == 1) ? 0.0f
                            : (-body_len + (float)idx * (2.0f * body_len / (float)(per_side - 1)));
                float phase = ((i % 2 == 0) == (idx % 2 == 0)) ? step : -step;
                // Amplitudes ANTES: avanco 0.04 e levantada 0.05 - 4 e 5 cm num bicho de escala 1,
                // praticamente invisivel, o que fazia as pernas parecerem pinos rigidos. Agora sao
                // 0.17 e 0.13, e escalam com a velocidade (spd_frac): parado a perna nao mexe.
                float swing = phase * 0.17f * spd_frac;
                float lift = std::max(0.0f, phase) * 0.13f * spd_frac;
                B(along + swing * 0.45f, hip_y + lift * 0.5f, spread * (float)side,
                  0.07f, leg_len, 0.07f, a.limb_r, a.limb_g, a.limb_b);
                B(along + swing, hip_y - leg_len * 0.55f + lift, spread * 1.12f * (float)side,
                  0.06f, leg_len * 0.55f, 0.06f, a.limb_r * 0.82f, a.limb_g * 0.82f, a.limb_b * 0.82f);
            }
        }

        // Antenas (so' quem tem na tabela).
        if (a.antenna > 0.0f) {
            for (float sgn : {-1.0f, 1.0f}) {
                float base_up = (c.type == EnemyType::Stalker) ? 1.12f
                              : ((c.type == EnemyType::Crawler) ? 0.26f : 0.92f);
                B(0.12f, base_up + a.antenna * 0.5f, 0.06f * sgn, 0.03f, a.antenna, 0.03f,
                  a.plate_r, a.plate_g, a.plate_b);
            }
        }

        // TELEGRAFE DO GOLPE: enquanto carrega, um anel cresce no chao e o corpo se abaixa. E' o
        // aviso que da ao jogador a chance de sair - sobretudo do Brute, cuja carga dura 0.8s.
        if (c.windup > 0.0f) {
            float w = 1.0f - clamp01(c.windup / std::max(0.01f, a.attack_windup));
            rlSetBlendMode(RL_BLEND_ADDITIVE);
            rlDisableDepthMask();
            render_glow_disc_3d({cx, c.y + 0.05f, cz}, (0.4f + w * 0.9f) * S,
                                1.0f, 0.45f, 0.22f, (1.0f - w) * 0.45f, 16);
            rlEnableDepthMask();
            rlSetBlendMode(RL_BLEND_ALPHA);
        }

        // Perseguindo: anel de agressao (elite mais forte).
        if (chasing) {
            rlSetBlendMode(RL_BLEND_ADDITIVE);
            rlDisableDepthMask();
            render_glow_disc_3d({cx, c.y + 0.04f, cz}, 0.46f * S,
                                a.eye_r, a.eye_g, a.eye_b,
                                (a.elite ? 0.16f : 0.09f) + 0.07f * std::sin(c.anim_timer * 9.0f), 14);
            rlEnableDepthMask();
            rlSetBlendMode(RL_BLEND_ALPHA);
        }
    }

    // Feixe grosso e brilhante (nucleo + halo) - substitui a linha de 1px de antes, que nao
    // tinha espessura/brilho nenhum (feedback do jogador). Aditivo, com escrita de
    // profundidade desligada enquanto desenha os 2 quads sobrepostos deste mesmo efeito
    // (senao brigam por z-order entre si e aparece uma costura visivel).
    if (g_laser_trace_timer > 0.0f) {
        const WeaponTier& wt = cur_tier();
        float alpha = clamp01(g_laser_trace_timer / wt.trace_duration);
        rlSetTexture(0);
        rlSetBlendMode(RL_BLEND_ADDITIVE);
        rlDisableDepthMask();
        render_beam_3d(g_laser_trace_a, g_laser_trace_b, wt.beam_halo_w, wt.halo_r, wt.halo_g, wt.halo_b, alpha * 0.38f);
        render_beam_3d(g_laser_trace_a, g_laser_trace_b, wt.beam_core_w, wt.core_r, wt.core_g, wt.core_b, alpha * 0.9f);
        rlEnableDepthMask();
        rlSetBlendMode(RL_BLEND_ALPHA);
    }

    // Flash do cano (todo tiro, acerto ou erro - reforca "saiu da arma") - desenhado aqui
    // porque so' esta funcao roda todo frame independente de main.cpp; a POSICAO usada e' a
    // mesma get_weapon_muzzle_pos() usada em try_fire_laser_pistol.
    if (g_muzzle_flash_timer > 0.0f) {
        float t = clamp01(g_muzzle_flash_timer / kMuzzleFlashDuration);
        Vec3 muzzle = get_weapon_muzzle_pos();
        rlSetTexture(0);
        rlSetBlendMode(RL_BLEND_ADDITIVE);
        rlDisableDepthMask();
        const WeaponTier& mt = cur_tier();
        render_glow_disc_3d(muzzle, mt.muzzle_radius, mt.core_r, mt.core_g, mt.core_b, t * 0.85f, 14);
        rlEnableDepthMask();
        rlSetBlendMode(RL_BLEND_ALPHA);
    }

    // Explosao de verdade no impacto (so' quando o tiro acerta uma criatura, ver
    // try_fire_laser_pistol) - anel de estilhacos (mesma tecnica ja aprovada da onda de
    // choque de pouso do jetpack, main.cpp, reescalada pro tamanho de uma criatura e
    // retintada laranja/ambar) + flash central branco->ciano (mais rapido, "momento da
    // detonacao") + as faiscas pequenas que ja existiam, por cima, sem custo extra.
    for (const ImpactFlash& f : g_impact_flashes) {
        float elapsed = kImpactFlashDuration - f.timer;

        float ring_t = clamp01(elapsed / kExplosionRingLife);
        if (ring_t < 1.0f) {
            // Raio e numero de estilhacos escalam com o nivel da arma: o impacto do Mk III le como
            // um golpe bem mais forte, nao so' um numero de dano maior.
            const WeaponTier& it = cur_tier();
            float ring_radius = 0.15f + ring_t * it.ring_radius;
            float ring_alpha = (1.0f - ring_t) * 0.85f;
            const int kRingChunks = it.ring_chunks;
            for (int i = 0; i < kRingChunks; ++i) {
                float ang = ((float)i / (float)kRingChunks) * 2.0f * kPi + hash01(i, f.pos.x) * 0.35f;
                float r = ring_radius * (0.80f + hash01(i, f.pos.z + 2.0f) * 0.35f);
                float dx = std::cos(ang) * r;
                float dz = std::sin(ang) * r;
                float size = (0.10f + hash01(i, 3.0f) * 0.08f) * (1.0f - ring_t * 0.3f);
                float shade = 0.85f + hash01(i, 4.0f) * 0.15f;
                render_plane_3d(f.ground_pos.x + dx, f.ground_pos.y + 0.02f, f.ground_pos.z + dz,
                                size, shade, shade * 0.55f, shade * 0.22f, ring_alpha);
            }
        }

        float flash_t = clamp01(elapsed / kExplosionFlashLife);
        if (flash_t < 1.0f) {
            float flash_radius = 0.20f + flash_t * 0.25f;
            float flash_alpha = (1.0f - flash_t);
            rlSetTexture(0);
            rlSetBlendMode(RL_BLEND_ADDITIVE);
            rlDisableDepthMask();
            render_glow_disc_3d(f.pos, flash_radius, 0.90f, 0.95f, 1.0f, flash_alpha * 0.9f, 16);
            rlEnableDepthMask();
            rlSetBlendMode(RL_BLEND_ALPHA);
        }

        float t = clamp01(f.timer / kImpactFlashDuration);
        for (int k = 0; k < 5; ++k) {
            float ang = (float)k / 5.0f * 2.0f * kPi + f.pos.x * 3.7f;
            float spread = (1.0f - t) * 0.30f;
            float sx = f.pos.x + std::cos(ang) * spread;
            float sz = f.pos.z + std::sin(ang) * spread;
            float sy = f.pos.y + spread * 0.6f;
            render_cube_3d(sx, sy, sz, 0.05f + 0.05f * t, 1.0f, 0.80f, 0.35f, t, false);
        }
    }

    // ================= PROJETEIS =================
    // Corpo pequeno solido + brilho aditivo + rastro. O rastro e' o que torna o tiro LEGIVEL: um
    // ponto voando some no cenario, um risco curto na direcao do voo mostra de onde vem e pra onde
    // vai, que e' a informacao necessaria pra desviar.
    if (!g_creature_projectiles.empty()) {
        for (const CreatureProjectile& p : g_creature_projectiles) {
            float sp = std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz);
            // Rastro: alguns pontos ao longo da trajetoria RECENTE (integracao pra tras), cada um
            // menor e mais apagado. Reproduz a gravidade, entao o lob do Brute deixa risco curvo.
            const int kTrail = 6;
            rlSetBlendMode(RL_BLEND_ADDITIVE);
            rlDisableDepthMask();
            for (int k = 1; k <= kTrail; ++k) {
                float t = (float)k * 0.030f;
                float tx = p.x - p.vx * t;
                float tz = p.z - p.vz * t;
                float ty = p.y - p.vy * t - 0.5f * p.gravity * t * t * -1.0f;
                float f = 1.0f - (float)k / (float)(kTrail + 1);
                render_glow_disc_3d({tx, ty, tz}, p.radius * (0.9f * f + 0.25f),
                                    p.r, p.g, p.b, 0.30f * f, 8);
            }
            render_glow_disc_3d({p.x, p.y, p.z}, p.radius * 2.2f, p.r, p.g, p.b, 0.55f, 12);
            rlEnableDepthMask();
            rlSetBlendMode(RL_BLEND_ALPHA);
            // Nucleo solido: gira, pra ler como objeto e nao como luz.
            render_box_oriented_3d({p.x, p.y, p.z}, p.radius * 1.5f, p.radius * 1.5f, p.radius * 2.6f,
                                   std::atan2(p.vx, p.vz) + p.spin * 0.15f,
                                   std::min(1.0f, p.r + 0.35f), std::min(1.0f, p.g + 0.35f),
                                   std::min(1.0f, p.b + 0.35f), 1.0f);
            (void)sp;
        }
    }
}

void try_fire_laser_pistol(const Vec3& ray_o, const Vec3& ray_d, float dt) {
    (void)dt; // cooldown ja decrementado 1x por frame em update_creatures()
    // g_hud_pointer_over_button: clicar no icone da arma na HUD nao pode disparar. O tiro usa o botao
    // SEGURADO, entao consumir o flag de clique no botao (o que a HUD fazia) nao impedia nada - bug
    // reportado ("quando clico na arma ela sempre dispara").
    bool fire_input = (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !g_hud_pointer_over_button)
                      || key_down(KEY_E);
    if (!fire_input || g_fire_cooldown > 0.0f) return;

    const WeaponTier& wt = cur_tier();
    g_fire_cooldown = wt.fire_cooldown;
    g_muzzle_flash_timer = kMuzzleFlashDuration; // todo tiro, acerto ou erro

    float best_t = wt.range;
    int best_idx = -1;
    for (int i = 0; i < (int)g_creatures.size(); ++i) {
        const Creature& c = g_creatures[(size_t)i];
        Vec3 cpos = {c.x, c.y + 0.45f, c.z};
        Vec3 rel = vec3_sub(cpos, ray_o);
        float t = vec3_dot(rel, ray_d);
        if (t < 0.2f || t > best_t) continue;
        Vec3 closest = vec3_add(ray_o, vec3_scale(ray_d, t));
        Vec3 diff = vec3_sub(cpos, closest);
        float perp2 = vec3_dot(diff, diff);
        if (perp2 <= wt.hit_radius * wt.hit_radius) {
            best_t = t;
            best_idx = i;
        }
    }

    // Colisao com terreno/objetos - sem isso o tiro atravessava tudo que nao fosse criatura
    // e ia parar no vazio a 35 unidades de distancia, num ponto que quase nunca batia com o
    // que o jogador via na mira (feedback: "o tiro nao esta indo na direcao certa" e "nao
    // tem efeito de colisao nenhum"). Raymarch simples por altura de terreno (mesma ideia ja
    // usada pra sondar chao em outros lugares deste arquivo), nao o AABB preciso da
    // mineracao - suficiente pra um hitscan. Só considera terreno mais perto que qualquer
    // criatura ja encontrada (nao atravessa parede pra acertar uma criatura atras dela).
    bool hit_terrain = false;
    if (g_world) {
        constexpr float kStep = 0.15f;
        for (float t = 0.3f; t < best_t; t += kStep) {
            Vec3 p = vec3_add(ray_o, vec3_scale(ray_d, t));
            int tx = world_to_tile(p.x);
            int tz = world_to_tile(p.z);
            if (!g_world->in_bounds(tx, tz)) { best_t = t; hit_terrain = true; break; }
            float h = stack_top_height_at(*g_world, tx, tz);
            if (p.y <= h) { best_t = t; hit_terrain = true; best_idx = -1; break; }
        }
    }

    // Origem VISUAL do traco = ponta do cano da arma (get_weapon_muzzle_pos), nao o olho da
    // camera - senao o tiro parece sair do ponteiro do mouse/mira em vez da arma na mao
    // (feedback do jogador). O teste de acerto acima continua usando ray_o/ray_d (camera+
    // mouse) sem nenhuma mudanca - so' o ponto desenhado muda.
    g_laser_trace_a = get_weapon_muzzle_pos();
    g_laser_trace_b = vec3_add(ray_o, vec3_scale(ray_d, best_t));
    g_laser_trace_timer = wt.trace_duration;
    play_laser_fire_sound();

    if (best_idx < 0 && !hit_terrain) return;

    // Efeito de impacto + marca de queimado no chao - em terreno OU criatura, em qualquer
    // acerto, nao so' quando mata (pedido do jogador: "efeito de atingir o alvo... marcas
    // de queimado", depois "efeito da colisao nem sequer existe").
    play_laser_impact_sound();
    Vec3 hit_pos = g_laser_trace_b;
    Vec3 ground_pos = hit_pos;
    if (best_idx >= 0) {
        const Creature& c = g_creatures[(size_t)best_idx];
        ground_pos = {c.x, c.y, c.z};
    }
    g_impact_flashes.push_back({hit_pos, ground_pos, kImpactFlashDuration});
    g_scorch_marks.push_back({ground_pos, kScorchMarkDuration});

    if (best_idx < 0) return;

    Creature& hit = g_creatures[(size_t)best_idx];
    const EnemyArchetype& ha = archetype_of(hit);

    // ================= DANO COM RESISTENCIA =================
    // Era "hit.hp -= wt.damage" direto. Resistencia agora e' DIFERENTE de vida: a defesa e'
    // subtraida de CADA tiro, com piso 1. Consequencia desejada: a mesma arma perde muito mais
    // eficiencia contra um alvo blindado do que contra um mole, e o upgrade da pistola aparece
    // justamente ai. Contra o Brute (def 5), Mk I entrega 5 por tiro e Mk III entrega 19 - quase
    // 4x, nao 2.4x como a razao de dano bruto sugeriria.
    int raw = wt.damage;
    int dealt = std::max(1, raw - ha.defense);
    bool blocked = (ha.defense > 0 && dealt < raw);
    hit.hp -= dealt;

    // Reacao ao dano: piscada + recuo na direcao do tiro, escalados por flinch_scale (1.0 no
    // Crawler, 0.06 no Alpha). O Brute NAO pode ser travado por tiros rapidos - com 0.12 ele
    // apenas pisca e continua vindo.
    hit.flinch = kFlinchDuration;
    float kdx = hit.x - ray_o.x, kdz = hit.z - ray_o.z;
    float kdl = std::sqrt(std::max(0.0001f, kdx * kdx + kdz * kdz));
    hit.flinch_dx = kdx / kdl;
    hit.flinch_dz = kdz / kdl;
    hit.hp_bar_timer = kHpBarVisibleSeconds;

    // Numero de dano flutuante no ponto do impacto. "def" quando a defesa cortou parte do dano -
    // e' a leitura de "esse bicho e' resistente" sem abrir menu nenhum.
    if (g_damage_numbers.size() < kMaxDamageNumbers) {
        g_damage_numbers.push_back({hit.x, hit.y + 0.9f * ha.scale, hit.z, dealt,
                                    kDamageNumberLife, blocked});
    }

    // Som de impacto POR CLASSE DE PESO, com pitch do arquetipo: corpo mole da um estalo seco,
    // carapaca da um clank metalico. Antes havia um unico som de impacto de laser, igual pra tudo.
    if (ha.hit_sound == HitSoundClass::Armored) play_creature_hit_armored_sound(ha.sound_pitch);
    else                                       play_creature_hit_light_sound(ha.sound_pitch);

    if (hit.hp > 0) return;

    float dx = hit.x, dz = hit.z, dy = hit.y;
    EnemyType dead_type = hit.type;
    float dead_scale = ha.scale;
    float dead_pitch = ha.sound_pitch;
    const char* dead_name = ha.name;
    g_creatures.erase(g_creatures.begin() + best_idx);

    // ---- MORTE: som proprio + explosao proporcional ao porte ----
    // "HP <= 0 -> desaparece" nao dava a sensacao de ter matado. Agora ha som de morte com pitch do
    // tipo e um estouro de particulas/anel cuja escala acompanha o tamanho do bicho.
    play_creature_death_sound(dead_pitch);
    if (g_world) spawn_block_particles(Block::Organic, dx, dz, g_world->h);
    g_impact_flashes.push_back({{dx, dy + 0.5f * dead_scale, dz}, {dx, dy, dz}, kImpactFlashDuration});
    g_scorch_marks.push_back({{dx, dy, dz}, kScorchMarkDuration});

    // UNICO site de abate do jogo. Os outros dois erase() de g_creatures em update_creatures sao
    // despawn (tile molhado/fora de limites e distancia > kDespawnDist) e de proposito nao contam:
    // despawn nao e' abate, e contar despawn deixaria a missao de combate concluir sozinha.
    ++g_kills;
    ++g_kills_by_type[(int)dead_type];
    // Elite abatido merece destaque - o resto usa a cor padrao de sucesso.
    if (dead_type == EnemyType::Alpha) {
        add_alert(std::string(dead_name) + " ABATIDO!", 1.0f, 0.82f, 0.30f, 5.0f);
    } else {
        add_alert(std::string(dead_name) + " abatido", 0.3f, 1.0f, 0.5f);
    }
}

// Ver comentario da declaracao em creatures.h.
float laser_cooldown_fraction() {
    // Divide pelo cooldown do NIVEL ATUAL, nao pela constante: assim a celula de energia desenhada na
    // arma (main.cpp) recarrega visivelmente mais rapido no Mk II/III sem nenhuma mudanca la.
    return clamp01(g_fire_cooldown / cur_tier().fire_cooldown);
}

// ============= PROGRESSAO DA ARMA / ESTATISTICA - ver creatures.h =============
int weapon_level() {
    return std::clamp(g_inventory[(int)Block::LaserPistol], 1, kWeaponMaxLevel);
}

bool weapon_level_up() {
    int lv = weapon_level();
    if (lv >= kWeaponMaxLevel) return false;
    // Escreve o nivel no PROPRIO slot de inventario da pistola. As duas leituras existentes desse
    // slot no projeto testam "> 0", entao continuam corretas com 2 ou 3.
    g_inventory[(int)Block::LaserPistol] = lv + 1;
    return true;
}

const char* weapon_level_name() { return cur_tier().name; }

int creature_kills() { return g_kills; }

void reset_creature_state() {
    g_kills = 0;
    for (int i = 0; i < kEnemyTypeCount; ++i) g_kills_by_type[i] = 0;
    g_damage_numbers.clear();
    g_creatures.clear();
    // Projeteis em voo tambem: sem isto um tiro disparado antes do Novo Jogo chegaria no jogador
    // novo, no mapa novo - o mesmo footgun de estado que nao e resetado.
    g_creature_projectiles.clear();
}

void creature_stats_load(int kills) {
    g_kills = std::max(0, kills);
    g_creatures.clear();   // criaturas nao sao salvas (efemeras, padrao de g_meteors)
}

int creature_kills_of(EnemyType t) {
    int i = (int)t;
    if (i < 0 || i >= kEnemyTypeCount) return 0;
    return g_kills_by_type[i];
}

// ============= HUD DE COMBATE - ver comentario em creatures.h =============
// Nao existia barra de vida nenhuma: o jogador atirava e nao tinha como saber se estava progredindo.
// Aqui a barra aparece SO' em combate (mirando ou por kHpBarVisibleSeconds apos o ultimo dano) e
// nao ha objeto de UI persistente por inimigo - o desenho e' imediato, como todo o resto do HUD.
int creature_under_aim() {
    if (g_inventory[(int)Block::LaserPistol] <= 0) return -1;
    if (g_selected != Block::LaserPistol) return -1;
    // Mesmo teste de acerto do tiro (raio da camera pelo cursor), pra a barra mostrar exatamente
    // quem levaria o proximo disparo - nao um alvo "proximo o suficiente" diferente.
    Vec3 ray_o = g_camera.position;
    Vector2 mp = GetMousePosition();
    Vec3 ray_d = get_mouse_ray_direction((int)mp.x, (int)mp.y, GetScreenWidth(), GetScreenHeight());
    const WeaponTier& wt = cur_tier();
    float best_t = wt.range;
    int best = -1;
    for (int i = 0; i < (int)g_creatures.size(); ++i) {
        const Creature& c = g_creatures[(size_t)i];
        Vec3 cp = {c.x, c.y + 0.45f * archetype_of(c).scale, c.z};
        Vec3 rel = vec3_sub(cp, ray_o);
        float t = vec3_dot(rel, ray_d);
        if (t < 0.2f || t > best_t) continue;
        Vec3 closest = vec3_add(ray_o, vec3_scale(ray_d, t));
        Vec3 diff = vec3_sub(cp, closest);
        if (vec3_dot(diff, diff) <= wt.hit_radius * wt.hit_radius) { best_t = t; best = i; }
    }
    return best;
}

// ============= PAINEL DE AMEACAS (canto da tela) =============
// A versao anterior desenhava uma barra flutuante SOBRE A CABECA de cada criatura, projetando a
// posicao do mundo pra tela. Dois problemas: (1) era chamada antes de render_hud, que e' quem troca
// a projecao pra ortho 2D - entao as barras saiam desenhadas com a projecao PERSPECTIVA ainda ativa
// e "nao apareciam corretamente"; (2) mesmo corrigida a ordem, barra sobre a cabeca se perde atras
// de terreno, sai da tela quando a criatura esta fora do enquadramento e fica ilegivel de longe.
//
// Agora e' uma LISTA FIXA no canto: uma linha por criatura que esta efetivamente em combate
// (perseguindo ou levou dano recentemente). Sempre no mesmo lugar, sempre legivel, e responde a
// pergunta que importa - "quem esta me atacando e quanto falta pra cada um morrer".
void render_creature_hud(int win_w, int win_h) {
    // ---- coleta as ameacas ativas ----
    // Ordena por fracao de vida (mais ferido primeiro): quem esta perto de morrer sobe na lista,
    // que e' a informacao acionavel - termina esse antes de trocar de alvo.
    struct Threat { int idx; float frac; float dist2; };
    Threat list[16];
    int n = 0;
    for (int i = 0; i < (int)g_creatures.size() && n < 16; ++i) {
        const Creature& c = g_creatures[(size_t)i];
        bool in_combat = (c.state == Creature::State::Chasing) || (c.hp_bar_timer > 0.0f);
        if (!in_combat) continue;
        float ddx = c.x - g_player.pos.x, ddz = c.z - g_player.pos.y;
        list[n].idx = i;
        list[n].frac = (float)c.hp / (float)std::max(1, c.max_hp);
        list[n].dist2 = ddx * ddx + ddz * ddz;
        ++n;
    }
    // Insertion sort (n <= 16): elite primeiro, depois mais ferido, depois mais perto.
    for (int i = 1; i < n; ++i) {
        Threat k = list[i];
        bool k_elite = enemy_archetype(g_creatures[(size_t)k.idx].type).elite;
        int j = i - 1;
        while (j >= 0) {
            bool j_elite = enemy_archetype(g_creatures[(size_t)list[j].idx].type).elite;
            bool swap = (k_elite && !j_elite) ||
                        (k_elite == j_elite && k.frac < list[j].frac) ||
                        (k_elite == j_elite && k.frac == list[j].frac && k.dist2 < list[j].dist2);
            if (!swap) break;
            list[j + 1] = list[j];
            --j;
        }
        list[j + 1] = k;
    }

    int aimed = creature_under_aim();

    // ---- painel ----
    // Canto DIREITO, logo abaixo do painel de fase. O minimapa saiu daqui pro canto inferior
    // esquerdo, entao esta faixa ficou livre - e o lado direito e' onde o jogador ja olha por
    // status, nao por acao.
    if (n > 0) {
        const float row_h = 26.0f;
        const float pw = 196.0f;
        float px = (float)win_w - pw - 20.0f;
        float py = hud_right_panel_bottom_y() + 14.0f;
        float ph = 22.0f + (float)n * row_h;

        // Fundo + borda de alerta (vermelha quando ha elite na lista).
        bool any_elite = false;
        for (int i = 0; i < n; ++i)
            if (enemy_archetype(g_creatures[(size_t)list[i].idx].type).elite) any_elite = true;
        render_quad(px - 6.0f, py - 6.0f, pw + 12.0f, ph + 12.0f, 0.05f, 0.05f, 0.07f, 0.80f);
        render_quad(px - 6.0f, py - 6.0f, pw + 12.0f, 1.0f,
                    any_elite ? 1.0f : 0.92f, any_elite ? 0.82f : 0.38f, any_elite ? 0.30f : 0.30f, 0.85f);

        char hdr[48];
        snprintf(hdr, sizeof(hdr), "AMEACAS  %d", n);
        draw_text(px, py + 12.0f, hdr, 0.92f, 0.45f, 0.40f, 0.95f);

        float y = py + 24.0f;
        for (int i = 0; i < n; ++i) {
            const Creature& c = g_creatures[(size_t)list[i].idx];
            const EnemyArchetype& a = enemy_archetype(c.type);
            bool is_aimed = (list[i].idx == aimed);
            float frac = clamp01(list[i].frac);
            float dist = std::sqrt(list[i].dist2);

            // Realce da linha do alvo atualmente sob a mira - liga a lista ao que o tiro vai acertar.
            if (is_aimed) {
                render_quad(px - 4.0f, y - 11.0f, pw + 8.0f, row_h - 2.0f, 0.16f, 0.24f, 0.34f, 0.85f);
                render_quad(px - 4.0f, y - 11.0f, 2.0f, row_h - 2.0f, 0.40f, 0.82f, 1.0f, 0.95f);
            }

            // Marca de tipo: quadradinho na cor do corpo (a cor volta a ser complemento, nao a
            // identidade - quem identifica e' o nome + a silhueta no mundo).
            render_quad(px, y - 8.0f, 7.0f, 7.0f, a.body_r, a.body_g, a.body_b, 0.95f);
            if (a.elite) {
                render_quad(px - 1.0f, y - 9.0f, 9.0f, 1.0f, 1.0f, 0.82f, 0.30f, 0.95f);
                render_quad(px - 1.0f, y - 1.0f, 9.0f, 1.0f, 1.0f, 0.82f, 0.30f, 0.95f);
            }

            // Nome + distancia.
            draw_text(px + 12.0f, y, a.name,
                      a.elite ? 1.0f : 0.90f, a.elite ? 0.85f : 0.92f, a.elite ? 0.40f : 0.95f, 0.95f);
            char dtxt[24];
            snprintf(dtxt, sizeof(dtxt), "%.0fm", dist);
            float dw = estimate_text_w_px(dtxt);
            draw_text(px + pw - dw, y, dtxt, 0.55f, 0.58f, 0.66f, 0.90f);

            // Barra proporcional. Largura maior no elite (hierarquia pedida: comum pequena,
            // pesado normal, elite destacada).
            float bw = a.elite ? pw : (a.scale > 1.4f ? pw * 0.88f : pw * 0.76f);
            float bh = a.elite ? 6.0f : (a.scale > 1.4f ? 5.0f : 4.0f);
            float by = y + 5.0f;
            render_quad(px, by, bw, bh, 0.13f, 0.13f, 0.17f, 0.92f);
            float hr = (frac > 0.5f) ? (1.0f - (frac - 0.5f) * 1.4f) : 1.0f;
            float hg = (frac > 0.35f) ? 0.85f : 0.28f;
            render_quad(px, by, bw * frac, bh, hr, hg, 0.26f, 0.95f);
            // Marcas de 25% - da leitura de proporcao sem ler numero.
            for (int q = 1; q < 4; ++q)
                render_quad(px + bw * 0.25f * (float)q, by, 1.0f, bh, 0.05f, 0.05f, 0.07f, 0.9f);

            // Numeros + aviso de blindagem, so' na linha mirada (o resto fica limpo).
            if (is_aimed) {
                char hp[56];
                snprintf(hp, sizeof(hp), "%d / %d%s", c.hp, c.max_hp,
                         a.defense >= 5 ? "  BLINDADO" : "");
                draw_text(px + 12.0f, by + bh + 11.0f, hp, 0.72f, 0.78f, 0.86f, 0.92f);
                y += 12.0f;
            }
            y += row_h;
        }
    }

    // ---- NUMEROS DE DANO ----
    // Continuam no mundo (sobem do ponto do impacto), agora desenhados no passe 2D correto. Sao
    // pequenos e vivem 0.85s: o pedido foi nao poluir a tela.
    for (const DamageNumber& d : g_damage_numbers) {
        float t = 1.0f - clamp01(d.timer / kDamageNumberLife);
        Vec3 p = {d.x, d.y + t * 0.85f, d.z};
        float spx, spy;
        if (!world_to_screen(p, win_w, win_h, spx, spy)) continue;
        if (spx < 0.0f || spy < 0.0f || spx > (float)win_w || spy > (float)win_h) continue;
        char buf[24];
        snprintf(buf, sizeof(buf), "-%d%s", d.amount, d.blocked ? " def" : "");
        float w = estimate_text_w_px(buf);
        float alpha = (1.0f - t) * 0.95f;
        // Amarelo quando a defesa cortou (leitura de "resistiu"), branco-quente quando entrou cheio.
        if (d.blocked) draw_text(spx - w * 0.5f, spy, buf, 0.98f, 0.80f, 0.32f, alpha);
        else           draw_text(spx - w * 0.5f, spy, buf, 1.0f, 0.95f, 0.90f, alpha);
    }
}

// Ver comentario da declaracao em creatures.h. Fora do namespace anonimo: e' API publica (o HUD e
// as missoes leem atributos por tipo).
const EnemyArchetype& enemy_archetype(EnemyType t) {
    int i = (int)t;
    if (i < 0 || i >= kEnemyTypeCount) i = 0;
    return kArchetypes[i];
}
