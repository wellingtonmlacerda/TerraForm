#pragma once

#include "blocks.h"

#include <string>
#include <vector>

// ============= Base Building / Modules =============
// Extracted verbatim from main.cpp (original lines ~54-65, 159-166, 332-350, 416-662,
// 1742-1754, 1785-1854, 1920-2066, 2606-3029): the construction-queue/build-slot/module
// types, generate_base() (landing-site generation), rebuild_modules_from_world() (save-
// load helper), the module stats/status/unlock-requirement system, and update_modules()
// (the per-frame base simulation). save_game()/load_game() sit in the middle of this
// original range in main.cpp but are NOT part of this extraction - they stay in main.cpp
// (a separate future save_load.h/.cpp stage), and just call rebuild_modules_from_world()
// declared below.
//
// World is only ever used here by reference/pointer, so this header only needs a forward
// declaration - same "owner keeps the definition, users just forward-declare" rule used
// for the world/camera/player_physics split.
struct World;

// ---- Construction queue ----
struct ConstructionJob {
    Block module_type = Block::Air;
    int slot_index = -1;
    float time_remaining = 0.0f;
    float total_time = 0.0f;
    bool active = false;
};

// O unico vetor de fila de construcao do jogo. Definido (nao-static) em
// modules_building.cpp; main.cpp continua usando g_construction_queue diretamente
// (render/input do menu de build, raycast de colocacao, build_physics_test_map) atraves
// desta declaracao extern - mesmo padrao de g_world/g_camera nos estagios anteriores.
// player_physics.cpp's spawn_player_new_game() also clears it directly now that
// ConstructionJob is a complete type there (via this header) - see the note removed from
// player_physics.cpp's clear_construction_queue() wrapper, which is gone now that this
// header exists.
extern std::vector<ConstructionJob> g_construction_queue;

// ---- Build slots for the base ----
struct BuildSlotInfo {
    int x = 0;
    int y = 0;
    Block assigned_module = Block::Air;  // Air means empty slot
    std::string label;
};

// Idem g_construction_queue acima: main.cpp continua lendo/escrevendo g_build_slots
// diretamente (render/input do menu de build, build_physics_test_map).
extern std::vector<BuildSlotInfo> g_build_slots;

// ---- Modules ----
// Module status enum (precisa vir antes de struct Module)
enum class ModuleStatus {
    Available,      // Can be built
    Blocked,        // Missing resources
    Building,       // Under construction
    Active,         // Running normally
    NoPower,        // Needs energy
    Damaged         // Needs repair
};

struct Module {
    int x = 0;
    int y = 0;
    Block type = Block::SolarPanel;
    float t = 0.0f;
    float health = 100.0f;     // 0-100, se <= 0 fica Damaged
    ModuleStatus status = ModuleStatus::Active;
    bool upgraded = false;     // 1 nivel de upgrade (tecla R) - ver try_upgrade_module()
};

// Idem g_construction_queue acima: main.cpp continua lendo/escrevendo g_modules
// diretamente (render do mundo/minimapa, HUD, raycast de colocacao/remocao,
// build_physics_test_map).
extern std::vector<Module> g_modules;

// Module production/consumption rates (per minute)
struct ModuleStats {
    const char* name;
    const char* description;
    float energy_production = 0.0f;   // +Energy/min
    float energy_consumption = 0.0f;  // -Energy/min
    float oxygen_production = 0.0f;   // +O2/min
    float water_production = 0.0f;    // +Water/min
    float food_production = 0.0f;     // +Food/min
    float integrity_bonus = 0.0f;     // Repair rate/min
    float co2_production = 0.0f;      // For terraforming
    float construction_time = 30.0f;  // Seconds to build
};

// Get module statistics. Lost "static": main.cpp's build-menu rendering (outside this
// stage's scope) also calls it.
ModuleStats get_module_stats(Block b);

// Start construction of a module (spends CraftCost via inventory_crafting.h, queues a
// ConstructionJob). Lost "static": main.cpp's build-menu input (outside this stage's
// scope) also calls it.
bool start_construction(Block module_type, int slot_index);

// Upgrade de 1 nivel (tecla R, main.cpp) de um modulo ja construido em (tx,ty) - gasta
// get_module_upgrade_cost(), marca Module::upgraded=true, dispara particulas. Retorna
// false sem custar nada se nao houver modulo ali, ja estiver upgradado, ou faltar recurso.
bool try_upgrade_module(int tx, int ty);

// Refino de Liga (tecla G, main.cpp) numa Oficina (Block::Workshop) construida em (tx,ty) -
// gasta get_refine_cost(), soma kRefineBatchSize de Block::RefinedAlloy ao inventario.
// Cooldown fixo (nao gastavel em sequencia instantanea). Retorna false sem custar nada se
// nao houver Workshop ali, estiver danificado, faltar recurso, ou ainda em cooldown.
bool try_refine_at_workshop(int tx, int ty);

// Fabricacao "de campo" da Pistola de Laser (tecla P, main.cpp, funciona em qualquer lugar,
// sem exigir Oficina) - gasta get_weapon_cost(), seta g_inventory[Block::LaserPistol]=1
// (posse, nao lote). Retorna false sem custar nada se faltar recurso ou o jogador ja
// possuir uma.
bool try_craft_laser_pistol();

// NOTE: get_module_status(Block)/status_string(ModuleStatus) are NOT declared here on
// purpose - grep across the whole file shows neither is ever called from outside this
// module (both were already dead code before this refactor: defined but unused).  Since
// nothing outside modules_building.cpp touches them, they stay `static` (internal)
// there instead of being exposed here.

// ---- Unlock system ----
// UnlockRequirement/get_unlock_requirement are NOT declared here on purpose either - they
// are only ever used internally by is_unlocked/check_unlocks/unlock_progress_string
// below (all in this same file), so they stay file-local (static) to
// modules_building.cpp.
bool is_unlocked(Block b);
void check_unlocks();
std::string unlock_progress_string(Block b);

// ---- Base generation / simulation ----
void generate_base(World& world);
void rebuild_modules_from_world();
// Reconstroi g_build_slots a partir dos tiles do mundo, igual rebuild_modules_from_world() acima.
// g_build_slots nunca foi salvo (save_load.cpp nao o menciona), entao ao carregar um jogo ele ficava
// vazio (processo novo) ou com o conteudo da PARTIDA ANTERIOR (mesmo processo) - nos dois casos
// desconectado dos tiles Block::BuildSlot que estao la no mundo, e o menu de construcao caia no
// fallback de criar slot solto perto da base.
void rebuild_build_slots_from_world();
void update_modules(World& world, float dt);

// Centro da cupula da base E' g_base_x/g_base_y (declarados em main.cpp) - nao existe um
// par de coordenadas separado pra isso. Um par separado (g_shelter_door_x/y) existiu aqui
// antes e foi removido: so era atribuido dentro de generate_base(), chamada apenas num
// "Novo Jogo" de verdade - continuar/carregar uma save (save_load.cpp) nunca o tocava,
// entao apos qualquer reinicio do processo + "Continuar", esses globais ficavam presos em
// 0 (tile de origem do mundo) pelo resto da sessao, desconectados de onde a base
// realmente estava - quebrando silenciosamente a barreira de colisao, o gatilho da porta
// e ate o desenho da malha decorativa ao mesmo tempo (bug real, ja diagnosticado e
// corrigido usando g_base_x/g_base_y direto em todo lugar).

// Raio da barreira de colisao invisivel da cupula (cilindro em volta de g_base_x/y,
// qualquer altura, 360 graus - ver apply_dome_barrier() em player_physics.cpp). Fica entre o
// anel de slots (14, generate_base()) e a borda visivel da malha decorativa (16, main.cpp) -
// a barreira nao usa blocos do mundo (evitava o contorno serrilhado de uma grade quadrada
// tentando aproximar um circulo, rejeitado pelo usuario) nem tem excecao de angulo pra porta:
// a porta e so um detalhe visual sempre "fechado" na malha (render_geodesic_dome), e a unica
// forma de atravessar em qualquer ponto do circulo e o teleporte curto de proximidade.
constexpr float kDomeWallRadius = 15.5f;

// ---- Geometria do EXTERIOR da base (ver o bloco do exterior em generate_base) ----
// Tudo em TILES, relativo a (g_base_x, g_base_y). Mora aqui, e nao local a generate_base(), porque
// outros sistemas precisam da MESMA geometria: o teste de abrigo (player_in_base_complex abaixo) e a
// exclusao do scanner de POI (minimap.cpp - sem ela o scanner "descobre" a propria base do jogador).
// Duplicar coordenadas da base em 2 lugares ja causou bug real neste projeto - ver a nota sobre
// g_shelter_door_x/y acima.
//
// ARQUITETURA (referencia: a base real "Mars Base 1" em Gansu, fotos do jogador): tambor central
// (domo) + 4 modulos axiais ligados por tubos + 2 modulos tecnicos diagonais + tanques/mastros/
// dutos. TODOS MACICOS - o exterior nao tem vao interno nenhum, e' isso que impede o jetpack de
// invadir. Os ambientes jogaveis sao salas independentes no distrito de interiores (interiors.h).
//
// As constantes do layout ANTERIOR (kCorridorHalfWidth/kCorridorStart/kCorridorEnd/kModuleCenter/
// kModuleHalf/kGreenhouseCenterDy/kGreenhouseHalf/kWallLayers/kHubWall*) foram REMOVIDAS junto com
// ele: descreviam um hub oco de raio 16 com corredores e salas percorriveis por dentro, que era
// exatamente a fonte do bug "atravessa tetos / entra na geometria interna". Deixa-las aqui seria
// documentacao ativa de uma geometria que nao existe mais.
constexpr int kBaseTaperRadius      = 78;  // fim da rampa que liga o achatamento ao terreno
                                           // natural. Sem ela a borda do achatamento e' um degrau
                                           // vertical (medido: -7.25 de mundo em r=46) que o jogador
                                           // nao consegue subir de volta - ilha alta cercada de abismo.
constexpr int kBaseFlattenRadius    = 46;  // achatamento e' um DISCO: com modulos em 6 direcoes nao
                                           // existe "lado" privilegiado pra achatar. 46 cobre o anel
                                           // de slots (38) com folga e preserva as margens do vulcao
                                           // e das chaminees de lava (medido: r<=50 e' seguro).

// True se o offset (dx,dy) em tiles do centro da base cai na pegada dos volumes do exterior.
bool base_annex_contains(float dx, float dy);
bool base_annex_contains(float dx, float dy);

// "O jogador esta abrigado?" - disco configuravel ORIGINAL (g_base_cfg.safe_radius, inalterado) OU
// dentro do anexo. Fonte unica pros 3 call sites que antes recalculavam a mesma distancia na mao
// (reabastecimento em update_modules, dreno do traje em main.cpp, indicador de zona segura no HUD):
// com o anexo existindo, 3 copias fatalmente discordariam.
bool player_in_base_complex();

// Onde o jogador nasce/renasce: NA FRENTE da eclusa, no exterior. Antes o spawn era o tile central
// da base, que agora e o meio de um tambor MACICO - nascer ali seria nascer dentro de blocos
// solidos. Fonte unica lida por spawn_player_at_base() (player_physics.cpp).
void base_spawn_tile(int& out_x, int& out_z);

// 0 = nenhuma Estufa produzindo (sem modulo, ou parada por falta de agua/energia), > 0 = produzindo.
// Escrito por update_modules() e lido pelo desenho da estufa (base_interior.cpp) pra dirigir o
// visual das plantas: pedido explicito do jogador - "quero que de fato seja visivel que sao plantas
// e que estao produzindo alimento e oxigenio para a base".
extern float g_greenhouse_output;
