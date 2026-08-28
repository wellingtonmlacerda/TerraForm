#include "blocks.h"

bool is_transparent(Block b) {
    return b == Block::Air || b == Block::Water || b == Block::Lava || b == Block::Leaves ||
           b == Block::DomeGlass || b == Block::RocketWindow || b == Block::BuildSlot;
}

// Top-down: tiles que bloqueiam movimento (inverso de walkable)
bool is_solid(Block b) {
    switch (b) {
        case Block::Air:
        case Block::Grass:
        case Block::Dirt:
        case Block::Sand:
        case Block::Snow:
        case Block::Leaves:
        case Block::BuildSlot:
        case Block::LandingPad:
        case Block::RocketWindow:
        case Block::BaseFloor:
        case Block::PlanterBed:
            return false;  // Pode passar por cima
        default:
            return true;   // Bloqueia movimento (pedra, agua, gelo, modulos, etc) - DomeGlass
                            // caiu aqui de proposito: janela de verdade bloqueia passagem (era
                            // false antes, uma combinacao inconsistente com is_walkable() -
                            // nem parede nem chao - agora vira parede de vidro real).
    }
}

bool is_module(Block b) {
    return b == Block::SolarPanel || b == Block::WaterExtractor ||
           b == Block::OxygenGenerator || b == Block::TerraformerBeacon ||
           b == Block::Greenhouse || b == Block::CO2Factory || b == Block::Habitat ||
           b == Block::EnergyGenerator || b == Block::Workshop;
}

bool is_base_structure(Block b) {
    return b == Block::RocketHull || b == Block::RocketEngine ||
           b == Block::RocketWindow || b == Block::RocketNose ||
           b == Block::RocketFin || b == Block::RocketDoor ||
           b == Block::DomeGlass || b == Block::DomeFrame ||
           b == Block::LandingPad || b == Block::BuildSlot ||
           b == Block::PipeH || b == Block::PipeV || b == Block::Antenna ||
           b == Block::BaseFloor || b == Block::PlanterBed ||
           b == Block::FurnitureLow || b == Block::FurnitureMid || b == Block::FurnitureTall ||
           b == Block::FurnitureHuge || b == Block::BaseShell;
}

// Blocos que representam "solo/superficie" (nao sao objetos acima do terreno).
// Usado para separar terreno (ground) de objetos (rochas, minerios, modulos, etc).
bool is_ground_like(Block b) {
    switch (b) {
        case Block::Grass:
        case Block::Dirt:
        case Block::Sand:
        case Block::Snow:
        case Block::Ice:
        case Block::Water:
        case Block::Lava:
        case Block::LandingPad:
        case Block::BuildSlot:
        case Block::BaseFloor:
        case Block::PlanterBed:
        case Block::Basalt:
            return true;
        default:
            return false;
    }
}

// Top-down: tiles que permitem movimento do jogador
bool is_walkable(Block b) {
    switch (b) {
        case Block::Air:
        case Block::Grass:
        case Block::Dirt:
        case Block::Sand:
        case Block::Snow:
        case Block::Leaves:       // Pode andar sobre folhas
        case Block::BuildSlot:    // Slots de construcao
        case Block::LandingPad:   // Area de pouso
        case Block::BaseFloor:    // Piso interno da base
        case Block::PlanterBed:   // Canteiro (chao, da pra andar em cima/em volta)
            return true;
        default:
            return false;  // Pedra, agua, gelo, modulos bloqueiam
    }
}

const char* block_name(Block b) {
    switch (b) {
        case Block::Air: return "Ar";
        case Block::Grass: return "Grama";
        case Block::Dirt: return "Terra";
        case Block::Stone: return "Pedra";
        case Block::Sand: return "Areia";
        case Block::Water: return "Agua";
        case Block::Ice: return "Gelo";
        case Block::Snow: return "Neve";
        case Block::Wood: return "Madeira";
        case Block::Leaves: return "Folhas";
        case Block::Coal: return "Carvao";
        case Block::Iron: return "Ferro";
        case Block::Copper: return "Cobre";
        case Block::Crystal: return "Cristal";
        case Block::Metal: return "Metal";
        case Block::Organic: return "Organico";
        case Block::Components: return "Componentes";
        case Block::SolarPanel: return "Painel Solar";
        case Block::EnergyGenerator: return "Gerador de Energia";
        case Block::WaterExtractor: return "Extrator de Agua";
        case Block::OxygenGenerator: return "Gerador de O2";
        case Block::Greenhouse: return "Estufa";
        case Block::CO2Factory: return "Fabrica de CO2";
        case Block::Habitat: return "Habitat";
        case Block::Workshop: return "Oficina";
        case Block::TerraformerBeacon: return "Terraformador";
        case Block::RocketHull: return "Foguete";
        case Block::RocketEngine: return "Motor do Foguete";
        case Block::RocketWindow: return "Janela do Foguete";
        case Block::RocketNose: return "Cone do Foguete";
        case Block::RocketFin: return "Asa do Foguete";
        case Block::RocketDoor: return "Porta do Foguete";
        case Block::DomeGlass: return "Cupula";
        case Block::DomeFrame: return "Moldura da Cupula";
        case Block::LandingPad: return "Plataforma";
        case Block::BuildSlot: return "Slot de Construcao";
        case Block::PipeH: return "Tubo";
        case Block::PipeV: return "Tubo";
        case Block::Antenna: return "Antena";
        case Block::Lava: return "Lava";
        case Block::RefinedAlloy: return "Liga Refinada";
        case Block::LaserPistol: return "Pistola de Laser";
        case Block::BaseFloor: return "Piso da Base";
        case Block::PlanterBed: return "Canteiro";
        // Aparecem na linha "Alvo:" do HUD se o jogador mirar num movel - nome sensato, nao "?".
        case Block::FurnitureLow:
        case Block::FurnitureMid:
        case Block::FurnitureTall:
        case Block::FurnitureHuge: return "Mobilia";
        case Block::BaseShell: return "Estrutura da Base";
        case Block::Basalt: return "Basalto";
        default: return "?";
    }
}

const char* phase_name(TerraPhase p) {
    switch (p) {
        case TerraPhase::Frozen: return "Congelado";
        case TerraPhase::Warming: return "Aquecendo";
        case TerraPhase::Thawing: return "Degelo";
        case TerraPhase::Habitable: return "Habitavel";
        case TerraPhase::Terraformed: return "Terraformado";
        default: return "?";
    }
}

// Ver comentario da declaracao em blocks.h.
bool is_furniture_collider(Block b) {
    return b == Block::FurnitureLow || b == Block::FurnitureMid || b == Block::FurnitureTall ||
           b == Block::FurnitureHuge;
}

// Ver comentario da declaracao em blocks.h.
bool is_invisible_collider(Block b) {
    return is_furniture_collider(b) || b == Block::BaseShell;
}
