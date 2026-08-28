#include "objectives.h"

#include "game_state.h"
#include "modules_building.h"    // Module, g_modules (UpgradeThreeModules)
#include "inventory_crafting.h"  // g_inventory (BankRefinedAlloy)
#include "creatures.h"           // creature_kills / weapon_level / weapon_level_name (combate + arma)
#include "minimap.h"             // poi_ever_found (missao de exploracao)

#include <algorithm>
#include <array>
#include <string>

// g_terraform/g_victory/g_phase are still owned by main.cpp (already non-static there for
// other extracted modules, e.g. world.cpp) - same "own local extern" pattern used
// throughout this codebase's extraction stages.
extern float g_terraform;
extern bool g_victory;
extern TerraPhase g_phase;

static const ObjectiveDef kObjectives[kObjectiveCount] = {
    {"Restabelecer energia", "Construa um Painel Solar ou Gerador de Energia.", Block::SolarPanel},
    {"Encontrar agua", "Construa um Extrator de Agua.", Block::WaterExtractor},
    {"Respirar", "Construa um Gerador de Oxigenio.", Block::OxygenGenerator},
    {"Sinais na poeira", "Use o scanner (T) e va ate um destroco no mapa.", Block::Air},
    {"Primeiro contato", "Derrote uma criatura alienigena com a pistola de laser.", Block::Air},
    {"Semear a colonia", "Construa uma Estufa e mantenha-a produzindo (precisa de agua e energia).", Block::Greenhouse},
    {"Nucleo Laser Mk II", "Construa uma Oficina e aprimore a pistola (tecla U perto dela).", Block::Workshop},
    {"Proteger a colonia", "Construa um Habitat e elimine 5 criaturas.", Block::Habitat},
    {"Despertar o planeta", "Construa a Fabrica de CO2 e o Terraformer Beacon, e alcance a fase Degelo.", Block::CO2Factory},
    {"Terraformar Marte", "Alcance a fase Terraformado com 6 tipos de modulo construidos.", Block::Air},
    // Trilho de legado (pos-vitoria) - ver comentario em objectives.h.
    {"Arsenal completo", "Aprimore a pistola ate o Mk III (tecla U perto da Oficina).", Block::Air},
    {"Aprimorar a colonia", "Aprimore (tecla R) pelo menos 3 modulos.", Block::Air},
    {"Refinar um legado", "Produza 20 unidades de Liga Refinada (tecla G, mirando uma Oficina).", Block::Air},
};

static int g_current = 0;
static std::array<bool, kBlockTypeCount> g_ever_built = {};
static float g_victory_celebration = 0.0f;
static float g_legacy_celebration = 0.0f;

const ObjectiveDef& objective_def(int index) {
    index = std::clamp(index, 0, kObjectiveCount - 1);
    return kObjectives[index];
}

int objectives_current_index() { return g_current; }
// Preserva a semantica original ("os 10 objetivos principais estao completos") pros 3
// call sites externos que dependem dela (ui_hud.cpp mostra o overlay de vitoria,
// save_load.cpp restaura g_victory, world.cpp dispara o toast de mudanca de fase) -
// nenhum deles deve esperar ate os 13 (legado) pra disparar.
bool objectives_all_complete() { return g_current >= kMainObjectiveCount; }
bool objectives_legacy_complete() { return g_current >= kObjectiveCount; }

void notify_module_built(Block type) {
    if (type == Block::Air) return;
    g_ever_built[(size_t)type] = true;
}

void reset_objectives() {
    g_current = 0;
    g_ever_built.fill(false);
    g_victory_celebration = 0.0f;
    g_legacy_celebration = 0.0f;
}

float objectives_victory_celebration_remaining() { return g_victory_celebration; }
float objectives_legacy_celebration_remaining() { return g_legacy_celebration; }

const bool* objectives_ever_built_snapshot() { return g_ever_built.data(); }

void objectives_load_state(int current_index, const bool* ever_built, int ever_built_count) {
    g_current = std::clamp(current_index, 0, kObjectiveCount);
    g_ever_built.fill(false);
    if (ever_built) {
        int n = std::min(ever_built_count, (int)kBlockTypeCount);
        for (int i = 0; i < n; ++i) g_ever_built[(size_t)i] = ever_built[i];
    }
    g_victory_celebration = 0.0f;
    g_legacy_celebration = 0.0f;
}

// Quantos TIPOS DISTINTOS de modulo o jogador ja construiu ao menos uma vez. Deriva de g_ever_built,
// que e' monotonico e ja salvo (v7) - de proposito NAO usa g_modules.size(): aquele vetor e'
// reconstruido a partir dos tiles do mundo a cada load e DIMINUI se um modulo for destruido, o que
// faria a missao final poder "desconcluir".
static int distinct_modules_built() {
    static const Block kCountable[] = {
        Block::SolarPanel, Block::EnergyGenerator, Block::WaterExtractor, Block::OxygenGenerator,
        Block::Greenhouse, Block::CO2Factory, Block::Habitat, Block::Workshop,
        Block::TerraformerBeacon,
    };
    int n = 0;
    for (Block b : kCountable) {
        if (g_ever_built[(size_t)b]) ++n;
    }
    return n;
}

static constexpr int kProtectColonyKills = 5;
static constexpr int kTerraformModuleTypes = 6;
static constexpr int kLegacyAlloyTarget = 20;

static bool check_objective(int index) {
    switch ((ObjectiveId)index) {
        case ObjectiveId::BuildFirstPower:
            return g_ever_built[(size_t)Block::SolarPanel] || g_ever_built[(size_t)Block::EnergyGenerator];
        case ObjectiveId::BuildWaterExtractor:   return g_ever_built[(size_t)Block::WaterExtractor];
        case ObjectiveId::BuildOxygenGenerator:  return g_ever_built[(size_t)Block::OxygenGenerator];
        // Exploracao: latch de minimap.cpp (nunca volta a false).
        case ObjectiveId::FindWreck:             return poi_ever_found();
        // Combate: abate de verdade, nao posse de arma. creature_kills() e' monotonico.
        case ObjectiveId::FirstHunt:             return creature_kills() >= 1;
        // Construir E usar: g_greenhouse_output > 0 exige estufa nao-danificada COM agua e energia.
        // Como a missao trava ao concluir, uma estufa que depois pare por falta de agua nao desfaz.
        case ObjectiveId::GreenhouseRunning:
            return g_ever_built[(size_t)Block::Greenhouse] && g_greenhouse_output > 0.0f;
        case ObjectiveId::WeaponMkII:
            return g_ever_built[(size_t)Block::Workshop] && weapon_level() >= 2;
        case ObjectiveId::ProtectColony:
            return g_ever_built[(size_t)Block::Habitat] && creature_kills() >= kProtectColonyKills;
        case ObjectiveId::AwakenPlanet:
            return g_ever_built[(size_t)Block::CO2Factory] &&
                   g_ever_built[(size_t)Block::TerraformerBeacon] &&
                   (int)g_phase >= (int)TerraPhase::Thawing;
        case ObjectiveId::TerraformComplete:
            return g_phase == TerraPhase::Terraformed &&
                   distinct_modules_built() >= kTerraformModuleTypes;
        case ObjectiveId::WeaponMkIII:           return weapon_level() >= kWeaponMaxLevel;
        case ObjectiveId::UpgradeThreeModules: {
            int upgraded = 0;
            for (const Module& m : g_modules) {
                if (m.upgraded) ++upgraded;
            }
            return upgraded >= 3;
        }
        case ObjectiveId::BankRefinedAlloy:
            return g_inventory[(size_t)Block::RefinedAlloy] >= kLegacyAlloyTarget;
    }
    return false;
}

// Ver o comentario da declaracao em objectives.h. String vazia = missao sem contagem.
std::string objective_progress_string(int index) {
    auto frac = [](int have, int need) {
        return std::to_string(std::min(have, need)) + "/" + std::to_string(need);
    };
    switch ((ObjectiveId)index) {
        case ObjectiveId::FirstHunt:
            return frac(creature_kills(), 1);
        case ObjectiveId::ProtectColony:
            return frac(creature_kills(), kProtectColonyKills);
        case ObjectiveId::WeaponMkII:
        case ObjectiveId::WeaponMkIII:
            return std::string(weapon_level_name());
        case ObjectiveId::TerraformComplete:
            return frac(distinct_modules_built(), kTerraformModuleTypes) + " tipos";
        case ObjectiveId::UpgradeThreeModules: {
            int upgraded = 0;
            for (const Module& m : g_modules) {
                if (m.upgraded) ++upgraded;
            }
            return frac(upgraded, 3);
        }
        case ObjectiveId::BankRefinedAlloy:
            return frac(std::max(0, g_inventory[(int)Block::RefinedAlloy]), kLegacyAlloyTarget);
        default:
            return std::string();
    }
}

void update_objectives(float dt) {
    if (g_victory_celebration > 0.0f) {
        g_victory_celebration = std::max(0.0f, g_victory_celebration - dt);
    }
    if (g_legacy_celebration > 0.0f) {
        g_legacy_celebration = std::max(0.0f, g_legacy_celebration - dt);
    }

    if (g_current >= kObjectiveCount) return;
    if (!check_objective(g_current)) return;

    show_unlock_popup("Objetivo concluido!", kObjectives[g_current].title);
    ++g_current;

    if (g_current == kMainObjectiveCount) {
        g_victory = true;
        g_victory_celebration = 8.0f;
        set_toast("Marte terraformado! Voce venceu!", 6.0f);
    }

    if (g_current == kObjectiveCount) {
        g_legacy_celebration = 6.0f;
        set_toast("Legado completo! A colonia prospera muito alem da terraformacao.", 6.0f);
    }
}
