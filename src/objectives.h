#pragma once

#include "blocks.h"

#include <string>

// ============= Player Objectives (milestone track) =============
// A single ordered sequence of milestones the player completes one at a time,
// reusing existing progression signals (module construction, TerraPhase) rather
// than introducing a parallel quest system. Once a milestone is completed it
// never un-completes, even if the underlying condition later stops being true
// (e.g. the module is destroyed, or the terraform score dips) - this matches
// how an "achievement" should behave, as opposed to a live status check.
// A trilha deixou de ser "construa 10 predios + espere". Cada marco introduz ou usa uma mecanica
// diferente: exploracao (FindWreck), combate (FirstHunt, ProtectColony), uso real de producao
// (GreenhouseRunning), evolucao da arma (WeaponMkII, WeaponMkIII) e infraestrutura de verdade
// (TerraformComplete exige tipos distintos de modulo, nao so' a fase).
enum class ObjectiveId {
    BuildFirstPower = 0,
    BuildWaterExtractor,
    BuildOxygenGenerator,
    // EXPLORACAO: chegar num destroco espalhado pelo mapa (8 sitios, World::gen "Passo 7"). Usa o
    // latch poi_ever_found() de minimap.cpp - ver por que a deteccao vive la, nao aqui.
    FindWreck,
    // COMBATE: o "ObjectiveId::FirstHunt" que estava previsto no plano antigo e nunca foi implementado.
    // Baseado num abate REAL (creature_kills()), nao em posse de arma.
    FirstHunt,
    // Estufa CONSTRUIDA E PRODUZINDO (g_greenhouse_output > 0 ja e' um AND de "existe" + "nao
    // danificada" + "tem agua e energia") - "construa e use", nao so' construa.
    GreenhouseRunning,
    // ARMA + OFICINA: exige a Oficina construida E a pistola em Mk II. E' aqui que a Oficina ganha
    // papel real; a pistola em si vem de fabrica (spawn_player_new_game), nao precisa de Oficina.
    WeaponMkII,
    // COMBATE + CONSTRUCAO: defender o que foi construido.
    ProtectColony,
    // Fabrica de CO2 + Beacon + fase Degelo num unico marco (antes eram 3 marcos, 2 deles espera pura).
    AwakenPlanet,
    // FINAL: fase Terraformado E infraestrutura minima (tipos distintos de modulo ja construidos) -
    // a fase deixou de ser a UNICA condicao.
    TerraformComplete,
    // ---- Trilho de legado pos-vitoria (indices 10-12) ----
    // A Oficina saiu do legado (virou requisito do WeaponMkII, marco 7) e o lugar dela e' o topo da
    // progressao da arma. kMainObjectiveCount preserva o ponto de vitoria original pros 3 call sites
    // externos que dependem dele (ui_hud.cpp, save_load.cpp, world.cpp).
    WeaponMkIII,
    UpgradeThreeModules,
    BankRefinedAlloy,
};
constexpr int kMainObjectiveCount = 10; // ponto de vitoria original (Terraformar Marte) - inalterado
constexpr int kObjectiveCount = 13;      // inclui os 3 objetivos de legado pos-vitoria

struct ObjectiveDef {
    const char* title;
    const char* hint;
    Block related_module;  // Block::Air if this milestone isn't gated by a specific module unlock
};

const ObjectiveDef& objective_def(int index);
int objectives_current_index();      // 0..kObjectiveCount; == kObjectiveCount means all complete
bool objectives_all_complete();

// Call whenever the player actually places a module (both the queued-construction
// path and the instant-placement path in building_interaction.cpp call this).

// Texto de progresso da missao de indice `index`, ou "" quando ela nao tem contagem. O HUD so'
// concatena isto na linha de dica (padrao que o painel JA usava com unlock_progress_string e com o
// "(N/20)" da Liga Refinada, que agora sai daqui). O conhecimento de quantos/de que fica em
// objectives.cpp; ui_hud.cpp continua burro.
std::string objective_progress_string(int index);

void notify_module_built(Block type);

// Call once per tick (from update_modules, alongside update_phase()).
void update_objectives(float dt);

// New game / respawn-to-fresh-state.
void reset_objectives();

// While > 0, a one-time victory celebration overlay should be shown; counts down
// with dt inside update_objectives(). Replaces the old permanent g_victory overlay.
float objectives_victory_celebration_remaining();

// Trilho de legado (objetivos 10-12, pos-TerraformComplete) - mesmo padrao do par acima.
bool objectives_legacy_complete();               // g_current >= kObjectiveCount (13)
float objectives_legacy_celebration_remaining();

// ---- Save/load support ----
const bool* objectives_ever_built_snapshot();  // size kBlockTypeCount, read-only view for save_game
void objectives_load_state(int current_index, const bool* ever_built, int ever_built_count);
