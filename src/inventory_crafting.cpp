#include "inventory_crafting.h"

#include <string>

// ============= Inventory / Crafting (state) =============
// O unico inventario/selecao do jogo. main.cpp perdeu o "static" que tinha nestas duas
// globais (elas ja eram nao-static la, pois player_physics.cpp's spawn_player_new_game()
// grants o starter kit / reseta o bloco selecionado); agora sao definidas aqui, dono
// natural do tipo CraftCost/da logica de inventario.
std::array<int, kBlockTypeCount> g_inventory = {};
Block g_selected = Block::Dirt;

// Module build costs (updated with new resources)
CraftCost get_module_cost(Block b) {
    CraftCost c{};
    switch (b) {
        case Block::SolarPanel:
            c.iron = 30; c.copper = 10;
            break;
        case Block::EnergyGenerator:
            c.iron = 40; c.crystal = 20; c.copper = 25;
            break;
        case Block::OxygenGenerator:
            c.ice = 50; c.iron = 50; c.copper = 20;
            break;
        case Block::WaterExtractor:
            c.ice = 30; c.metal = 20; c.copper = 15;
            break;
        case Block::Greenhouse:
            // Organico 40 -> gelo/ferro/cobre. Obrigatorio: com a flora organica removida do planeta
            // (ver world.cpp, "NENHUMA MATERIA ORGANICA"), nao existe fonte de organico ANTES da
            // fase Habitavel, e organico 40 travaria a Estufa - e com ela a missao 6 e a campanha
            // inteira. Tematicamente casa melhor: uma estufa e' estrutura vedada + hidroponia
            // (agua e tubulacao), e as sementes vieram da Terra com o colono, nao do solo marciano.
            c.ice = 50; c.iron = 30; c.copper = 15;
            break;
        case Block::Workshop:
            c.iron = 60; c.components = 30; c.copper = 40;
            break;
        case Block::CO2Factory:
            c.iron = 60; c.coal = 50; c.copper = 30;
            break;
        case Block::Habitat:
            c.stone = 80; c.iron = 60; c.copper = 40; c.metal = 30;
            break;
        case Block::TerraformerBeacon:
            c.iron = 100; c.crystal = 50; c.components = 40; c.copper = 60;
            break;
        default: break;
    }
    return c;
}

// Upgrade de 1 nivel (nao uma arvore) - ~1/3 do custo de construcao, pesado em
// Metal/Components/Crystal (mesma carteira de get_suit_repair_cost() abaixo).
CraftCost get_module_upgrade_cost(Block b) {
    CraftCost c{};
    switch (b) {
        case Block::SolarPanel:       c.metal = 15; c.components = 5;  break;
        case Block::EnergyGenerator:  c.metal = 20; c.crystal = 10; c.components = 8;  break;
        case Block::OxygenGenerator:  c.metal = 20; c.components = 10; break;
        case Block::WaterExtractor:   c.metal = 18; c.components = 8;  break;
        case Block::Greenhouse:       c.metal = 15; c.components = 8;  break;
        case Block::Workshop:         c.metal = 25; c.components = 15; break;
        case Block::CO2Factory:       c.metal = 25; c.components = 12; break;
        case Block::Habitat:          c.metal = 30; c.components = 15; break;
        case Block::TerraformerBeacon: c.metal = 40; c.crystal = 20; c.components = 25; break;
        default: break;
    }
    return c;
}

// "Topup" discreto (nao gasto por segundo) - restaura +25 de integridade do traje.
CraftCost get_suit_repair_cost() {
    CraftCost c{};
    c.metal = 5;
    c.components = 3;
    return c;
}

// Refino no Workshop (tecla G) - consome ferro/cobre/metal, produz Liga Refinada em lote
// (ver try_refine_at_workshop(), modules_building.cpp).
CraftCost get_refine_cost() {
    CraftCost c{};
    c.iron = 20;
    c.copper = 15;
    c.metal = 10;
    return c;
}

// Pistola de laser (tecla P, numa Oficina) - item de tecnologia avancada, pesado em
// Metal/Componentes/Cristal (mesma carteira do upgrade/refino acima).
CraftCost get_weapon_cost() {
    CraftCost c{};
    c.metal = 35;
    c.components = 20;
    c.crystal = 15;
    return c;
}

// Custo do upgrade da arma, por nivel DESTINO (2 = Mk II, 3 = Mk III). Mesmo bolso
// Metal/Componentes/Cristal de get_module_upgrade_cost - tecnologia avancada usa recurso
// refinado, nao minerio bruto. RefinedAlloy NAO entra: nao e' campo de CraftCost
// (ver kCraftCostFields), entao can_afford/spend_cost nao saberiam cobra-lo.
CraftCost get_weapon_upgrade_cost(int to_level) {
    CraftCost c{};
    if (to_level <= 2) {
        c.metal = 25; c.components = 15; c.crystal = 10;
    } else {
        c.metal = 45; c.components = 30; c.crystal = 25;
    }
    return c;
}

std::string module_cost_string(const CraftCost& c) {
    std::string s;
    auto add = [&](const char* name, int need, int have) {
        if (need <= 0) return;
        if (!s.empty()) s += " ";
        s += name;
        s += ":" + std::to_string(need);
        if (have < need) s += "(!)";
    };
    add("Pedra", c.stone, g_inventory[(int)Block::Stone]);
    add("Ferro", c.iron, g_inventory[(int)Block::Iron]);
    add("Carvao", c.coal, g_inventory[(int)Block::Coal]);
    add("Madeira", c.wood, g_inventory[(int)Block::Wood]);
    add("Cobre", c.copper, g_inventory[(int)Block::Copper]);
    add("Gelo", c.ice, g_inventory[(int)Block::Ice]);
    add("Cristal", c.crystal, g_inventory[(int)Block::Crystal]);
    add("Metal", c.metal, g_inventory[(int)Block::Metal]);
    add("Organico", c.organic, g_inventory[(int)Block::Organic]);
    add("Comp", c.components, g_inventory[(int)Block::Components]);
    return s.empty() ? "Gratis" : s;
}

// ============= can_afford/spend_cost/refund_cost dedup (Fase 1b of the plan) =============
// Originally these three functions each hand-listed the same 10 CraftCost fields against
// g_inventory[(int)Block::X] identically three times (30 near-identical lines total).
// Verified before deduplicating: CraftCost has exactly these 10 fields (stone, iron,
// coal, wood, copper, ice, crystal, metal, organic, components - see inventory_crafting.h)
// and all three original functions operated on all 10 in the same order with no field
// skipped or special-cased in one but not the others, so a single pointer-to-member table
// iterated by all three is behavior-preserving. cost_string() (below) is NOT part of this
// dedup - it only ever displayed 5 of the 10 fields by name (stone/iron/coal/copper/wood),
// which is pre-existing, unrelated behavior kept exactly as it was.
struct CraftCostField { Block block; int CraftCost::*field; };
static constexpr CraftCostField kCraftCostFields[] = {
    {Block::Stone, &CraftCost::stone}, {Block::Iron, &CraftCost::iron},
    {Block::Coal, &CraftCost::coal},   {Block::Wood, &CraftCost::wood},
    {Block::Copper, &CraftCost::copper}, {Block::Ice, &CraftCost::ice},
    {Block::Crystal, &CraftCost::crystal}, {Block::Metal, &CraftCost::metal},
    {Block::Organic, &CraftCost::organic}, {Block::Components, &CraftCost::components},
};

bool can_afford(const CraftCost& c) {
    for (auto& f : kCraftCostFields) {
        if (g_inventory[(int)f.block] < c.*f.field) return false;
    }
    return true;
}

void spend_cost(const CraftCost& c) {
    for (auto& f : kCraftCostFields) {
        g_inventory[(int)f.block] -= c.*f.field;
    }
}

void refund_cost(const CraftCost& c) {
    for (auto& f : kCraftCostFields) {
        g_inventory[(int)f.block] += c.*f.field;
    }
}

std::string cost_string(const CraftCost& c) {
    std::string s;
    auto add = [&](const char* name, int v) {
        if (v <= 0) return;
        if (!s.empty()) s += " ";
        s += name;
        s += std::to_string(v);
    };
    add("St", c.stone);
    add("Fe", c.iron);
    add("C", c.coal);
    add("Cu", c.copper);
    add("W", c.wood);
    return s.empty() ? "-" : s;
}

// Ver comentario da declaracao em inventory_crafting.h. Percorre a MESMA kCraftCostFields que
// can_afford/spend_cost usam, entao nao ha uma segunda lista de campos pra divergir.
int module_cost_breakdown(Block module_type, ResourceReq* out, int max_out) {
    CraftCost c = get_module_cost(module_type);
    int n = 0;
    for (const auto& f : kCraftCostFields) {
        if (n >= max_out) break;
        int need = c.*f.field;
        if (need <= 0) continue;
        out[n].block = f.block;
        out[n].name = block_name(f.block);
        out[n].have = g_inventory[(int)f.block];
        out[n].need = need;
        ++n;
    }
    return n;
}
