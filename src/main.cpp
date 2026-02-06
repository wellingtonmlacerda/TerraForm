#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

// ===========================
// TerraFormer 2D (prototype)
// Win32 + OpenGL (immediate mode)
// ===========================

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Operacoes Vec3
static Vec3 vec3_add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static Vec3 vec3_sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 vec3_scale(const Vec3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }
static float vec3_dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float vec3_length(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
static Vec3 vec3_normalize(const Vec3& v) {
    float len = vec3_length(v);
    if (len < 0.0001f) return {0, 0, 0};
    return {v.x / len, v.y / len, v.z / len};
}
static Vec3 vec3_cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static constexpr float kPi = 3.1415926535f;
static float clamp01(float v) { return std::fmax(0.0f, std::fmin(1.0f, v)); }
static float smoothstep01(float edge0, float edge1, float x) {
    if (edge0 == edge1) return (x < edge0) ? 0.0f : 1.0f;
    float t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

// Escala vertical do heightmap (suaviza o relevo e evita "degraus" grandes por tile).
// 1 unidade no heightmap = kHeightScale unidades no mundo 3D.
static constexpr float kHeightScale = 0.25f;

// ============= SISTEMA DE CORES CENTRALIZADO (UX) =============
// Cores funcionais - cada cor tem significado consistente
static const float kColorHp[]       = {0.90f, 0.14f, 0.18f, 1.0f};   // Vermelho - vida/dano
static const float kColorOxygen[]   = {0.20f, 0.85f, 0.55f, 1.0f};   // Verde - oxigenio
static const float kColorWater[]    = {0.25f, 0.65f, 0.95f, 1.0f};   // Azul - agua
static const float kColorEnergy[]   = {0.95f, 0.84f, 0.25f, 1.0f};   // Amarelo - energia
static const float kColorFood[]     = {0.85f, 0.65f, 0.25f, 1.0f};   // Laranja - comida
static const float kColorDanger[]   = {0.95f, 0.35f, 0.20f, 1.0f};   // Vermelho-laranja - perigo
static const float kColorSuccess[]  = {0.30f, 0.95f, 0.45f, 1.0f};   // Verde brilhante - sucesso
static const float kColorLocked[]   = {0.50f, 0.50f, 0.55f, 1.0f};   // Cinza - bloqueado
static const float kColorWarning[]  = {0.95f, 0.75f, 0.20f, 1.0f};   // Amarelo-laranja - aviso

// Cores de UI - paineis e textos
static const float kColorPanelBg[]      = {0.08f, 0.08f, 0.10f, 0.85f};  // Fundo de painel
static const float kColorPanelBorder[]  = {0.30f, 0.55f, 0.85f, 0.90f};  // Borda azul
static const float kColorTextPrimary[]  = {0.95f, 0.95f, 0.95f, 1.0f};   // Texto principal
static const float kColorTextSecondary[]= {0.70f, 0.70f, 0.75f, 0.90f};  // Texto secundario
static const float kColorHighlight[]    = {0.95f, 0.95f, 0.35f, 0.90f};  // Destaque amarelo
static const float kColorSelection[]    = {0.35f, 0.65f, 0.95f, 0.80f};  // Selecao azul

// ============= Perlin Noise =============
static int perm[512];

static void init_permutation(unsigned seed = 1337) {
    std::vector<int> p(256);
    for (int i = 0; i < 256; ++i) p[i] = i;
    unsigned s = seed;
    for (int i = 255; i > 0; --i) {
        s = 1664525u * s + 1013904223u;
        int j = (int)(s % (unsigned)(i + 1));
        std::swap(p[i], p[j]);
    }
    for (int i = 0; i < 512; ++i) perm[i] = p[i & 255];
}

static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
static float lerp(float a, float b, float t) { return a + t * (b - a); }
static float grad(int hash, float x, float y) {
    int h = hash & 7;
    float u = (h < 4) ? x : y;
    float v = (h < 4) ? y : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
}

static float perlin(float x, float y) {
    int xi = (int)std::floor(x) & 255;
    int yi = (int)std::floor(y) & 255;
    float xf = x - std::floor(x);
    float yf = y - std::floor(y);
    float u = fade(xf);
    float v = fade(yf);
    int aa = perm[perm[xi] + yi];
    int ab = perm[perm[xi] + yi + 1];
    int ba = perm[perm[xi + 1] + yi];
    int bb = perm[perm[xi + 1] + yi + 1];
    float x1 = lerp(grad(aa, xf, yf), grad(ba, xf - 1, yf), u);
    float x2 = lerp(grad(ab, xf, yf - 1), grad(bb, xf - 1, yf - 1), u);
    return lerp(x1, x2, v) * 0.5f + 0.5f;
}

static float fbm(float x, float y, int octaves = 5) {
    float value = 0.0f;
    float amp = 0.55f;
    float freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        value += perlin(x * freq, y * freq) * amp;
        freq *= 2.0f;
        amp *= 0.5f;
    }
    return clamp01(value);
}

// Ridged fBm (gera cristas/cordilheiras mais definidas, bom para montanhas e desfiladeiros).
// Saida: 0..1
static float ridged_fbm(float x, float y, int octaves = 4) {
    float value = 0.0f;
    float amp = 0.55f;
    float freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        float n = perlin(x * freq, y * freq);            // 0..1
        n = 1.0f - std::fabs(n * 2.0f - 1.0f);           // 0..1 (cristas)
        value += n * amp;
        freq *= 2.0f;
        amp *= 0.5f;
    }
    return clamp01(value);
}

// ============= Blocks =============
enum class Block : uint8_t {
    Air = 0,
    Grass,
    Dirt,
    Stone,
    Sand,
    Water,
    Ice,           // Frozen water (before warming)
    Snow,          // Snow (top-down biomes)
    Wood,
    Leaves,
    Coal,
    Iron,
    Copper,        // New resource for advanced modules
    Crystal,       // Rare crystal for energy systems
    Metal,         // Refined metal
    Organic,       // Organic material for food/plants
    Components,    // Electronic components
    // Modules
    SolarPanel,
    EnergyGenerator,  // Main power source
    WaterExtractor,
    OxygenGenerator,
    Greenhouse,    // Food production
    CO2Factory,    // Releases CO2 for warming
    Habitat,       // Living quarters
    Workshop,      // Repairs and crafting
    TerraformerBeacon,
    // Base structures (not buildable, generated)
    RocketHull,    // Landed rocket
    RocketEngine,  // Rocket engine
    RocketWindow,  // Rocket window
    RocketNose,    // Rocket nose cone
    RocketFin,     // Rocket fins
    RocketDoor,    // Rocket door/hatch
    DomeGlass,     // Habitat dome glass
    DomeFrame,     // Dome metal frame
    LandingPad,    // Landing pad floor
    BuildSlot,     // Empty slot for building modules
    PipeH,         // Horizontal pipe
    PipeV,         // Vertical pipe
    Antenna,       // Communication antenna
};

static constexpr int kBlockTypeCount = (int)Block::Antenna + 1;

static bool is_transparent(Block b) {
    return b == Block::Air || b == Block::Water || b == Block::Leaves || 
           b == Block::DomeGlass || b == Block::RocketWindow || b == Block::BuildSlot;
}

// Top-down: tiles que bloqueiam movimento (inverso de walkable)
static bool is_solid(Block b) {
    switch (b) {
        case Block::Air:
        case Block::Grass:
        case Block::Dirt:
        case Block::Sand:
        case Block::Snow:
        case Block::Leaves:
        case Block::BuildSlot:
        case Block::LandingPad:
        case Block::DomeGlass:
        case Block::RocketWindow:
            return false;  // Pode passar por cima
        default:
            return true;   // Bloqueia movimento (pedra, agua, gelo, modulos, etc)
    }
}

static bool is_module(Block b) {
    return b == Block::SolarPanel || b == Block::WaterExtractor || 
           b == Block::OxygenGenerator || b == Block::TerraformerBeacon ||
           b == Block::Greenhouse || b == Block::CO2Factory || b == Block::Habitat ||
           b == Block::EnergyGenerator || b == Block::Workshop;
}

static bool is_base_structure(Block b) {
    return b == Block::RocketHull || b == Block::RocketEngine ||
           b == Block::RocketWindow || b == Block::RocketNose ||
           b == Block::RocketFin || b == Block::RocketDoor ||
           b == Block::DomeGlass || b == Block::DomeFrame ||
           b == Block::LandingPad || b == Block::BuildSlot ||
           b == Block::PipeH || b == Block::PipeV || b == Block::Antenna;
}

// Blocos que representam "solo/superficie" (nao sao objetos acima do terreno).
// Usado para separar terreno (ground) de objetos (rochas, minerios, modulos, etc).
static bool is_ground_like(Block b) {
    switch (b) {
        case Block::Grass:
        case Block::Dirt:
        case Block::Sand:
        case Block::Snow:
        case Block::Ice:
        case Block::Water:
        case Block::LandingPad:
        case Block::BuildSlot:
            return true;
        default:
            return false;
    }
}

// Top-down: tiles que permitem movimento do jogador
static bool is_walkable(Block b) {
    switch (b) {
        case Block::Air:
        case Block::Grass:
        case Block::Dirt:
        case Block::Sand:
        case Block::Snow:
        case Block::Leaves:       // Pode andar sobre folhas
        case Block::BuildSlot:    // Slots de construcao
        case Block::LandingPad:   // Area de pouso
            return true;
        default:
            return false;  // Pedra, agua, gelo, modulos bloqueiam
    }
}

static const char* block_name(Block b) {
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
        default: return "?";
    }
}

// ============= Terraforming Phases =============
enum class TerraPhase {
    Frozen = 0,      // Starting phase: -60°C, no liquid water, need suits
    Warming,         // CO2 being released, temperature rising
    Thawing,         // 0°C+, ice melting, liquid water possible
    Habitable,       // 15°C+, can plant outside, atmosphere forming
    Terraformed,     // Earth-like conditions achieved!
};

static const char* phase_name(TerraPhase p) {
    switch (p) {
        case TerraPhase::Frozen: return "Congelado";
        case TerraPhase::Warming: return "Aquecendo";
        case TerraPhase::Thawing: return "Degelo";
        case TerraPhase::Habitable: return "Habitavel";
        case TerraPhase::Terraformed: return "Terraformado";
        default: return "?";
    }
}

// ============= Resources & Global State =============
// BASE resources (stored in the base, modules fill these)
static float g_base_energy = 50.0f;   // 0..500 (energy stored in base)
static float g_base_water = 50.0f;    // 0..200 (water stored in base)
static float g_base_oxygen = 50.0f;   // 0..200 (oxygen stored in base)
static float g_base_food = 50.0f;     // 0..200 (food stored in base)
static float g_base_integrity = 100.0f;  // 0..100 (base structural integrity)

static constexpr float kBaseEnergyMax = 500.0f;
static constexpr float kBaseWaterMax = 200.0f;
static constexpr float kBaseOxygenMax = 200.0f;
static constexpr float kBaseFoodMax = 200.0f;
static constexpr float kBaseIntegrityMax_Global = 100.0f;  // For reference before full declaration

// Construction and alert systems (defined here for early use)
struct ConstructionJob {
    Block module_type = Block::Air;
    int slot_index = -1;
    float time_remaining = 0.0f;
    float total_time = 0.0f;
    bool active = false;
};
static std::vector<ConstructionJob> g_construction_queue;

struct Alert {
    std::string message;
    float r, g, b;
    float time_remaining;
};
static std::vector<Alert> g_alerts;

// PLAYER resources (suit tanks, refilled at base)
static float g_player_oxygen = 100.0f;  // 0..100 (suit O2 tank)
static float g_player_water = 100.0f;   // 0..100 (suit water tank)
static float g_player_food = 100.0f;    // 0..100 (carried food)

// Legacy compatibility (these map to player resources now)
static float g_energy = 0.0f;        // Deprecated, use g_base_energy
static float g_water_res = 0.0f;     // Deprecated, maps to g_player_water
static float g_oxygen = 0.0f;        // Deprecated, maps to g_player_oxygen
static float g_food = 100.0f;        // Deprecated, maps to g_player_food

static float g_terraform = 0.0f;     // 0..100 (computed)
static bool g_victory = false;

// Atmosphere & Temperature (Realistic Terraforming)
static float g_temperature = -60.0f;  // Starting temp in Celsius (Mars-like)
static float g_co2_level = 0.0f;      // 0..100 (atmospheric CO2)
static float g_atmosphere = 0.0f;     // 0..100 (atmosphere density)
static TerraPhase g_phase = TerraPhase::Frozen;

static constexpr float kEnergyMax = 500.0f;
static constexpr float kTempFrozen = -20.0f;    // Below this: frozen
static constexpr float kTempThawing = 0.0f;     // Water can be liquid
static constexpr float kTempHabitable = 15.0f;  // Can plant outside
static constexpr float kTempTarget = 22.0f;     // Ideal Earth-like

// Unlock System - tracks total resources ever collected
struct UnlockProgress {
    int total_stone = 0;
    int total_iron = 0;
    int total_coal = 0;
    int total_copper = 0;
    int total_wood = 0;
    
    bool solar_unlocked = false;
    bool water_extractor_unlocked = false;
    bool o2_generator_unlocked = false;
    bool greenhouse_unlocked = false;
    bool co2_factory_unlocked = false;
    bool habitat_unlocked = false;
    bool terraformer_unlocked = false;
};

static UnlockProgress g_unlocks;

// ============= SISTEMA DE ONBOARDING =============
struct OnboardingState {
    bool shown_first_move = false;
    bool shown_first_mine = false;
    bool shown_first_collect = false;
    bool shown_first_build_menu = false;
    bool shown_first_unlock = false;
    bool shown_return_to_base = false;
    bool shown_low_oxygen = false;
    bool shown_low_water = false;
    float tip_timer = 0.0f;        // Timer para mostrar dicas
    std::string current_tip = "";  // Dica atual
};
static OnboardingState g_onboarding;

// ============= CONFIGURACOES DE ACESSIBILIDADE =============
struct GameSettings {
    float ui_scale = 1.0f;           // 0.75 - 1.5
    float camera_sensitivity = 0.20f;
    bool invert_y = false;
    float brightness = 1.0f;
    float contrast = 1.0f;
};
static GameSettings g_settings;

// ============= FEEDBACK VISUAL =============
static float g_screen_flash_red = 0.0f;   // Flash vermelho (erro/dano)
static float g_screen_flash_green = 0.0f; // Flash verde (sucesso)
static float g_hotbar_bounce = 0.0f;      // Animacao de bounce na hotbar
static int g_hotbar_bounce_slot = -1;     // Slot que esta animando

// Popup de coleta flutuante
struct CollectPopup {
    float x, y;
    Block item = Block::Air;
    int amount = 1;
    std::string text;
    float life;
    float r, g, b;
};
static std::vector<CollectPopup> g_collect_popups;

// Popup de conquista/desbloqueio
static float g_unlock_popup_timer = 0.0f;
static std::string g_unlock_popup_text = "";
static std::string g_unlock_popup_subtitle = "";

// Base location (landing site)
static int g_base_x = 0;
static int g_base_y = 0;
static bool g_show_build_menu = false;
static int g_build_menu_selection = 0;
static int g_settings_selection = 0;  // 0=sensibilidade, 1=inverter Y, 2=brilho, 3=escala UI, 4=voltar

// Build slots for the base
struct BuildSlotInfo {
    int x = 0;
    int y = 0;
    Block assigned_module = Block::Air;  // Air means empty slot
    std::string label;
};
static std::vector<BuildSlotInfo> g_build_slots;

// ============= Block Colors =============
static void block_color(Block b, int y, int world_h, float& r, float& g, float& bl, float& a) {
    a = 1.0f;
    float life = clamp01((g_oxygen * 0.75f + g_water_res * 0.25f) / 100.0f);
    float temp_factor = clamp01((g_temperature + 60.0f) / 80.0f); // -60 to +20 mapped to 0-1

    switch (b) {
        case Block::Grass: {
            // CORES MAIS ESCURAS E CONTRASTANTES
            float br = 0.45f, bg = 0.35f, bb = 0.18f;  // Dead/brown
            float gr = 0.20f, gg = 0.55f, gb = 0.15f;  // Alive/green (mais escuro)
            r = lerp(br, gr, life);
            g = lerp(bg, gg, life);
            bl = lerp(bb, gb, life);
            break;
        }
        case Block::Dirt:  r = 0.55f; g = 0.35f; bl = 0.18f; break;  // Mais claro
        case Block::Stone: r = 0.35f; g = 0.38f; bl = 0.42f; break;  // Mais escuro
        case Block::Sand:  r = 0.95f; g = 0.80f; bl = 0.45f; break;  // Mais amarelo
        case Block::Water: {
            float w0r = 0.15f, w0g = 0.20f, w0b = 0.35f;  // Murky escuro
            float w1r = 0.08f, w1g = 0.30f, w1b = 0.70f;  // Clear blue saturado
            float clarity = clamp01(g_atmosphere / 70.0f);
            r = lerp(w0r, w1r, clarity);
            g = lerp(w0g, w1g, clarity);
            bl = lerp(w0b, w1b, clarity);
            a = 0.80f;
            break;
        }
        case Block::Ice: {
            // Ice color - mais azulado e brilhante
            r = 0.65f; g = 0.88f; bl = 1.0f;
            a = 0.90f - temp_factor * 0.2f;
            break;
        }
        case Block::Snow: r = 1.0f; g = 0.98f; bl = 1.0f; break;  // Branco puro
        case Block::Wood:  r = 0.50f; g = 0.32f; bl = 0.18f; break;  // Mais escuro
        case Block::Leaves: {
            float lr = 0.22f, lg = 0.30f, lb = 0.15f;  // Dead
            float gr = 0.12f, gg = 0.60f, gb = 0.15f;  // Alive (mais saturado)
            r = lerp(lr, gr, life);
            g = lerp(lg, gg, life);
            bl = lerp(lb, gb, life);
            a = 0.75f;
            break;
        }
        case Block::Coal:   r = 0.12f; g = 0.12f; bl = 0.14f; break;  // Mais escuro
        case Block::Iron:   r = 0.70f; g = 0.55f; bl = 0.40f; break;  // Mais contrastante
        case Block::Copper: r = 0.90f; g = 0.50f; bl = 0.20f; break;  // Laranja vivo
        case Block::Crystal: r = 0.70f; g = 0.25f; bl = 1.0f; break;  // Roxo brilhante
        case Block::Metal: r = 0.75f; g = 0.78f; bl = 0.82f; break;  // Metal mais claro
        case Block::Organic: r = 0.30f; g = 0.75f; bl = 0.18f; break;  // Verde mais vivo
        case Block::Components: r = 0.15f; g = 0.60f; bl = 0.15f; break;  // Circuit green vivo
        
        // Modules - CORES MAIS VIBRANTES
        case Block::SolarPanel:      r = 0.10f; g = 0.20f; bl = 0.50f; break;  // Azul escuro
        case Block::EnergyGenerator: r = 1.0f; g = 0.80f; bl = 0.15f; break;   // Amarelo vivo
        case Block::WaterExtractor:  r = 0.15f; g = 0.55f; bl = 0.85f; break;  // Azul vivo
        case Block::OxygenGenerator: r = 0.18f; g = 0.90f; bl = 0.30f; break;  // Verde vivo
        case Block::Greenhouse:      r = 0.25f; g = 0.85f; bl = 0.25f; break;  // Verde claro
        case Block::CO2Factory:      r = 0.80f; g = 0.40f; bl = 0.15f; break;  // Laranja industrial
        case Block::Habitat:         r = 0.92f; g = 0.92f; bl = 0.95f; break;  // Branco brilhante
        case Block::Workshop:        r = 0.60f; g = 0.40f; bl = 0.25f; break;  // Ferrugem
        case Block::TerraformerBeacon: r = 0.85f; g = 0.25f; bl = 0.95f; break; // Magenta
        // Base structures
        case Block::RocketHull:      r = 0.95f; g = 0.95f; bl = 0.98f; break;  // Branco puro
        case Block::RocketEngine:    r = 0.30f; g = 0.32f; bl = 0.35f; break;  // Metal escuro
        case Block::RocketWindow:    r = 0.15f; g = 0.35f; bl = 0.75f; a = 0.85f; break;  // Azul
        case Block::RocketNose:      r = 1.0f; g = 0.20f; bl = 0.10f; break;   // Vermelho vivo
        case Block::RocketFin:       r = 0.80f; g = 0.82f; bl = 0.85f; break;  // Prata
        case Block::RocketDoor:      r = 0.45f; g = 0.47f; bl = 0.50f; break;  // Cinza
        case Block::DomeGlass:       r = 0.65f; g = 0.85f; bl = 1.0f; a = 0.45f; break;  // Transparente azul
        case Block::DomeFrame:       r = 0.55f; g = 0.58f; bl = 0.62f; break;  // Metal
        case Block::LandingPad:      r = 0.35f; g = 0.37f; bl = 0.40f; break;  // Concreto escuro
        case Block::BuildSlot:       r = 0.20f; g = 0.40f; bl = 0.55f; a = 0.65f; break;  // Slot azul
        case Block::PipeH:           r = 0.50f; g = 0.55f; bl = 0.60f; break;  // Metal
        case Block::PipeV:           r = 0.50f; g = 0.55f; bl = 0.60f; break;  // Metal
        case Block::Antenna:         r = 0.75f; g = 0.77f; bl = 0.80f; break;  // Metal claro
        default: r = 1.0f; g = 0.0f; bl = 1.0f; break;
    }

    // REMOVIDO: sombreamento por profundidade Y que escurecia tudo
    // O mundo agora tem cores consistentes sem escurecimento artificial
}

// ============= TEXTURAS (ESTILO MINICRAFT / PIXEL ART) =============
// Sem assets externos: atlas gerado proceduralmente em tempo de execucao.
static GLuint g_tex_atlas = 0;
static constexpr int kAtlasTileSize = 16;
static constexpr int kAtlasTilesPerRow = 16;
static constexpr int kAtlasSizePx = kAtlasTileSize * kAtlasTilesPerRow; // 256x256

struct Color8 {
    uint8_t r, g, b, a;
};

static Color8 c8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return {r, g, b, a}; }

static uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static uint32_t noise2_u32(int x, int y, uint32_t seed) {
    uint32_t h = seed;
    h ^= (uint32_t)x * 374761393u;
    h ^= (uint32_t)y * 668265263u;
    return hash_u32(h);
}

static uint8_t clamp_u8(int v) { return (uint8_t)std::clamp(v, 0, 255); }

enum class Tile : int {
    Missing = 0,
    // Naturais
    GrassTop,
    GrassSide,
    Dirt,
    Stone,
    Sand,
    Water0,
    Water1,
    Water2,
    Water3,
    Ice,
    Snow,
    WoodTop,
    WoodSide,
    Leaves,
    // Recursos
    CoalOre,
    IronOre,
    CopperOre,
    CrystalOre,
    Metal,
    Organic,
    Components,
    // Modulos
    SolarPanel,
    EnergyGenerator,
    WaterExtractor,
    OxygenGenerator,
    Greenhouse,
    CO2Factory,
    Habitat,
    Workshop,
    Terraformer,
    // Estruturas base
    RocketHull,
    RocketEngine,
    RocketWindow,
    RocketNose,
    RocketFin,
    RocketDoor,
    DomeGlass,
    DomeFrame,
    LandingPad,
    BuildSlot,
    Pipe,
    Antenna,
    // Cracks (mining)
    Crack1,
    Crack2,
    Crack3,
    Crack4,
    Crack5,
    Crack6,
    Crack7,
    Crack8,
};

struct UvRect {
    float u0, v0, u1, v1;
};

static UvRect atlas_uv(Tile t) {
    int id = (int)t;
    int tx = id % kAtlasTilesPerRow;
    int ty = id / kAtlasTilesPerRow;

    // Half-texel inset para evitar bleeding entre tiles.
    float inset = 0.5f;
    float u0 = (tx * kAtlasTileSize + inset) / (float)kAtlasSizePx;
    float v0 = (ty * kAtlasTileSize + inset) / (float)kAtlasSizePx;
    float u1 = (tx * kAtlasTileSize + (kAtlasTileSize - inset)) / (float)kAtlasSizePx;
    float v1 = (ty * kAtlasTileSize + (kAtlasTileSize - inset)) / (float)kAtlasSizePx;
    return {u0, v0, u1, v1};
}

static void atlas_set_px(std::vector<uint8_t>& atlas, int x, int y, Color8 c) {
    if (x < 0 || y < 0 || x >= kAtlasSizePx || y >= kAtlasSizePx) return;
    size_t idx = (size_t)(y * kAtlasSizePx + x) * 4u;
    atlas[idx + 0] = c.r;
    atlas[idx + 1] = c.g;
    atlas[idx + 2] = c.b;
    atlas[idx + 3] = c.a;
}

static void tile_set_px(std::vector<uint8_t>& atlas, Tile t, int x, int y_top, Color8 c) {
    int id = (int)t;
    int tx = id % kAtlasTilesPerRow;
    int ty = id / kAtlasTilesPerRow;

    // Converter y_top (0 = topo) -> y_bottom (0 = base) e escrever no atlas (OpenGL: origem embaixo).
    int y_bottom = (kAtlasTileSize - 1) - y_top;
    int gx = tx * kAtlasTileSize + x;
    int gy = ty * kAtlasTileSize + y_bottom;
    atlas_set_px(atlas, gx, gy, c);
}

static void tile_fill(std::vector<uint8_t>& atlas, Tile t, Color8 c) {
    for (int y = 0; y < kAtlasTileSize; ++y)
        for (int x = 0; x < kAtlasTileSize; ++x)
            tile_set_px(atlas, t, x, y, c);
}

static void tile_noise(std::vector<uint8_t>& atlas, Tile t, Color8 base, int amp, uint32_t seed) {
    for (int y = 0; y < kAtlasTileSize; ++y) {
        for (int x = 0; x < kAtlasTileSize; ++x) {
            uint32_t n = noise2_u32(x, y, seed);
            int d = (int)(n & 255u) % (amp * 2 + 1) - amp;
            tile_set_px(atlas, t, x, y, c8(
                clamp_u8((int)base.r + d),
                clamp_u8((int)base.g + d),
                clamp_u8((int)base.b + d),
                base.a));
        }
    }
}

static void tile_add_specks(std::vector<uint8_t>& atlas, Tile t, Color8 speck, int count, uint32_t seed) {
    for (int i = 0; i < count; ++i) {
        uint32_t h = noise2_u32(i, i * 7, seed);
        int x = (int)(h % (uint32_t)kAtlasTileSize);
        int y = (int)((h >> 8) % (uint32_t)kAtlasTileSize);
        tile_set_px(atlas, t, x, y, speck);
    }
}

static void tile_draw_rect(std::vector<uint8_t>& atlas, Tile t, int x0, int y0, int w, int h, Color8 c) {
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x)
            if (x >= 0 && y >= 0 && x < kAtlasTileSize && y < kAtlasTileSize)
                tile_set_px(atlas, t, x, y, c);
}

static void tile_generate_all(std::vector<uint8_t>& atlas) {
    atlas.assign((size_t)kAtlasSizePx * (size_t)kAtlasSizePx * 4u, 0);

    // Missing: checker magenta/black
    for (int y = 0; y < kAtlasTileSize; ++y) {
        for (int x = 0; x < kAtlasTileSize; ++x) {
            bool on = ((x / 4) ^ (y / 4)) & 1;
            tile_set_px(atlas, Tile::Missing, x, y, on ? c8(255, 0, 255) : c8(0, 0, 0));
        }
    }

    // Grama/folhas/agua: textura "valor" (quase cinza) + tint dinamico via block_color().
    tile_noise(atlas, Tile::GrassTop, c8(225, 225, 225), 18, 0x11u);
    tile_noise(atlas, Tile::GrassSide, c8(220, 220, 220), 18, 0x12u);
    // Faixa superior de "grama" no side (mais clara)
    tile_draw_rect(atlas, Tile::GrassSide, 0, 0, kAtlasTileSize, 5, c8(245, 245, 245));

    tile_noise(atlas, Tile::Leaves, c8(220, 220, 220, 210), 22, 0x13u);
    // Alguns pixels transparentes para folhas
    for (int y = 0; y < kAtlasTileSize; ++y) {
        for (int x = 0; x < kAtlasTileSize; ++x) {
            uint32_t n = noise2_u32(x, y, 0xBEEF1234u);
            if ((n % 23u) == 0u) tile_set_px(atlas, Tile::Leaves, x, y, c8(0, 0, 0, 0));
        }
    }

    // Terra, pedra, areia
    tile_noise(atlas, Tile::Dirt, c8(132, 88, 48), 28, 0x20u);
    tile_noise(atlas, Tile::Stone, c8(110, 114, 120), 22, 0x21u);
    tile_noise(atlas, Tile::Sand, c8(222, 194, 104), 18, 0x22u);

    // Agua (4 frames)
    for (int f = 0; f < 4; ++f) {
        Tile tf = (Tile)((int)Tile::Water0 + f);
        tile_noise(atlas, tf, c8(235, 235, 235, 210), 12, 0x30u + (uint32_t)f);
        for (int y = 0; y < kAtlasTileSize; ++y) {
            for (int x = 0; x < kAtlasTileSize; ++x) {
                // Ondas simples (linhas diagonais)
                int v = (x + y + f * 2) & 7;
                if (v == 0) tile_set_px(atlas, tf, x, y, c8(255, 255, 255, 235));
                if (v == 1) tile_set_px(atlas, tf, x, y, c8(205, 205, 205, 210));
            }
        }
    }

    // Gelo / neve (tendem a ficar neutros, com leve detalhe)
    tile_noise(atlas, Tile::Ice, c8(210, 238, 255, 235), 10, 0x40u);
    for (int y = 0; y < kAtlasTileSize; ++y) {
        for (int x = 0; x < kAtlasTileSize; ++x) {
            uint32_t n = noise2_u32(x, y, 0x40u);
            if ((n % 19u) == 0u) tile_set_px(atlas, Tile::Ice, x, y, c8(255, 255, 255, 240));
        }
    }
    tile_noise(atlas, Tile::Snow, c8(245, 248, 255), 8, 0x41u);

    // Madeira (top com "aneis", side com listras)
    tile_fill(atlas, Tile::WoodSide, c8(128, 84, 48));
    for (int x = 0; x < kAtlasTileSize; ++x) {
        int stripe = (x + (x / 3)) & 3;
        uint8_t add = (stripe == 0) ? 20 : (stripe == 1 ? 8 : 0);
        for (int y = 0; y < kAtlasTileSize; ++y) {
            Color8 c = c8(clamp_u8(128 + add), clamp_u8(84 + add), clamp_u8(48 + add));
            tile_set_px(atlas, Tile::WoodSide, x, y, c);
        }
    }
    tile_fill(atlas, Tile::WoodTop, c8(140, 92, 52));
    for (int y = 0; y < kAtlasTileSize; ++y) {
        for (int x = 0; x < kAtlasTileSize; ++x) {
            float dx = (x + 0.5f) - kAtlasTileSize * 0.5f;
            float dy = (y + 0.5f) - kAtlasTileSize * 0.5f;
            float d = std::sqrt(dx * dx + dy * dy);
            int ring = ((int)std::floor(d)) & 3;
            uint8_t add = (ring == 0) ? 18 : (ring == 1 ? 10 : 0);
            tile_set_px(atlas, Tile::WoodTop, x, y, c8(clamp_u8(140 + add), clamp_u8(92 + add), clamp_u8(52 + add)));
        }
    }

    // Minerios: pedra + specks
    tile_noise(atlas, Tile::CoalOre, c8(110, 114, 120), 20, 0x50u);
    tile_add_specks(atlas, Tile::CoalOre, c8(18, 18, 20), 38, 0x501u);

    tile_noise(atlas, Tile::IronOre, c8(110, 114, 120), 20, 0x51u);
    tile_add_specks(atlas, Tile::IronOre, c8(202, 128, 70), 32, 0x511u);

    tile_noise(atlas, Tile::CopperOre, c8(110, 114, 120), 20, 0x52u);
    tile_add_specks(atlas, Tile::CopperOre, c8(235, 135, 55), 32, 0x521u);

    tile_noise(atlas, Tile::CrystalOre, c8(110, 114, 120), 20, 0x53u);
    tile_add_specks(atlas, Tile::CrystalOre, c8(200, 80, 255), 26, 0x531u);

    tile_noise(atlas, Tile::Metal, c8(200, 205, 212), 10, 0x60u);
    tile_noise(atlas, Tile::Organic, c8(90, 200, 80), 26, 0x61u);
    tile_noise(atlas, Tile::Components, c8(40, 130, 55), 20, 0x62u);
    // Trilhas de circuito
    for (int y = 2; y < kAtlasTileSize; y += 4) {
        tile_draw_rect(atlas, Tile::Components, 1, y, kAtlasTileSize - 2, 1, c8(15, 75, 20));
    }
    for (int x = 2; x < kAtlasTileSize; x += 5) {
        tile_draw_rect(atlas, Tile::Components, x, 1, 1, kAtlasTileSize - 2, c8(15, 75, 20));
    }

    // Modulos: padroes simples (icones pixel)
    tile_noise(atlas, Tile::SolarPanel, c8(25, 45, 110), 12, 0x70u);
    tile_draw_rect(atlas, Tile::SolarPanel, 2, 3, 12, 2, c8(180, 190, 215));
    tile_draw_rect(atlas, Tile::SolarPanel, 2, 7, 12, 2, c8(180, 190, 215));
    tile_draw_rect(atlas, Tile::SolarPanel, 2, 11, 12, 2, c8(180, 190, 215));

    tile_noise(atlas, Tile::EnergyGenerator, c8(240, 205, 60), 18, 0x71u);
    tile_draw_rect(atlas, Tile::EnergyGenerator, 6, 3, 4, 10, c8(40, 40, 40));

    tile_noise(atlas, Tile::WaterExtractor, c8(40, 150, 220), 18, 0x72u);
    tile_draw_rect(atlas, Tile::WaterExtractor, 3, 4, 10, 8, c8(15, 50, 120));

    tile_noise(atlas, Tile::OxygenGenerator, c8(60, 220, 100), 18, 0x73u);
    tile_draw_rect(atlas, Tile::OxygenGenerator, 4, 4, 8, 8, c8(15, 80, 35));

    tile_noise(atlas, Tile::Greenhouse, c8(70, 220, 70), 18, 0x74u);
    tile_draw_rect(atlas, Tile::Greenhouse, 2, 4, 12, 8, c8(200, 240, 255, 220));

    tile_noise(atlas, Tile::CO2Factory, c8(200, 110, 45), 18, 0x75u);
    tile_draw_rect(atlas, Tile::CO2Factory, 5, 2, 6, 12, c8(55, 55, 60));

    tile_noise(atlas, Tile::Habitat, c8(235, 235, 242), 10, 0x76u);
    tile_draw_rect(atlas, Tile::Habitat, 3, 5, 10, 6, c8(35, 80, 180, 220));

    tile_noise(atlas, Tile::Workshop, c8(160, 110, 70), 18, 0x77u);
    tile_draw_rect(atlas, Tile::Workshop, 3, 3, 10, 10, c8(60, 45, 30));

    tile_noise(atlas, Tile::Terraformer, c8(200, 80, 230), 18, 0x78u);
    tile_draw_rect(atlas, Tile::Terraformer, 7, 2, 2, 12, c8(255, 255, 255, 230));

    // Estruturas base (bem simples)
    tile_noise(atlas, Tile::RocketHull, c8(235, 235, 242), 8, 0x80u);
    tile_noise(atlas, Tile::RocketEngine, c8(70, 75, 85), 12, 0x81u);
    tile_noise(atlas, Tile::RocketWindow, c8(120, 170, 255, 210), 8, 0x82u);
    tile_noise(atlas, Tile::RocketNose, c8(255, 70, 55), 10, 0x83u);
    tile_noise(atlas, Tile::RocketFin, c8(210, 215, 222), 10, 0x84u);
    tile_noise(atlas, Tile::RocketDoor, c8(120, 124, 130), 10, 0x85u);
    tile_noise(atlas, Tile::DomeGlass, c8(160, 210, 255, 150), 8, 0x86u);
    tile_noise(atlas, Tile::DomeFrame, c8(150, 155, 165), 10, 0x87u);
    tile_noise(atlas, Tile::LandingPad, c8(85, 88, 95), 10, 0x88u);
    tile_noise(atlas, Tile::BuildSlot, c8(60, 130, 170, 200), 10, 0x89u);
    tile_noise(atlas, Tile::Pipe, c8(155, 165, 175), 8, 0x8Au);
    tile_noise(atlas, Tile::Antenna, c8(205, 210, 220), 8, 0x8Bu);

    // Cracks: linhas pretas sobre alpha
    for (int i = 0; i < 8; ++i) {
        Tile t = (Tile)((int)Tile::Crack1 + i);
        tile_fill(atlas, t, c8(0, 0, 0, 0));
        uint8_t a = (uint8_t)(40 + i * 22);
        // desenho simples: alguns riscos diagonais
        for (int y = 1; y < kAtlasTileSize - 1; ++y) {
            int x = (y + i * 2) % (kAtlasTileSize - 2) + 1;
            tile_set_px(atlas, t, x, y, c8(0, 0, 0, a));
            if ((y & 3) == 0) tile_set_px(atlas, t, std::max(1, x - 1), y, c8(0, 0, 0, a));
        }
        for (int x = 2; x < kAtlasTileSize - 2; x += 5) {
            tile_set_px(atlas, t, x, (x + i) % (kAtlasTileSize - 2) + 1, c8(0, 0, 0, a));
        }
    }
}

static void init_texture_atlas() {
    if (g_tex_atlas != 0) return;

    std::vector<uint8_t> pixels;
    tile_generate_all(pixels);

    glGenTextures(1, &g_tex_atlas);
    glBindTexture(GL_TEXTURE_2D, g_tex_atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kAtlasSizePx, kAtlasSizePx, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
}

struct BlockTex {
    Tile top = Tile::Missing;
    Tile side = Tile::Missing;
    Tile bottom = Tile::Missing;
    bool uses_tint = false;      // Se true, multiplicar textura por block_color() (vida/atmosfera)
    bool transparent = false;    // Se true, respeitar alpha do block_color()
    bool is_water = false;       // Para pequenas regras de render (altura/anim)
};

static BlockTex block_tex(Block b) {
    BlockTex t{};
    switch (b) {
        case Block::Grass: t = {Tile::GrassTop, Tile::GrassSide, Tile::Dirt, true, false, false}; break;
        case Block::Dirt:  t = {Tile::Dirt, Tile::Dirt, Tile::Dirt, false, false, false}; break;
        case Block::Stone: t = {Tile::Stone, Tile::Stone, Tile::Stone, false, false, false}; break;
        case Block::Sand:  t = {Tile::Sand, Tile::Sand, Tile::Sand, false, false, false}; break;
        case Block::Water: t = {Tile::Water0, Tile::Water0, Tile::Water0, true, true, true}; break;
        case Block::Ice:   t = {Tile::Ice, Tile::Ice, Tile::Ice, false, true, false}; break;
        case Block::Snow:  t = {Tile::Snow, Tile::Snow, Tile::Snow, false, false, false}; break;
        case Block::Wood:  t = {Tile::WoodTop, Tile::WoodSide, Tile::WoodTop, false, false, false}; break;
        case Block::Leaves:t = {Tile::Leaves, Tile::Leaves, Tile::Leaves, true, true, false}; break;
        case Block::Coal:  t = {Tile::CoalOre, Tile::CoalOre, Tile::CoalOre, false, false, false}; break;
        case Block::Iron:  t = {Tile::IronOre, Tile::IronOre, Tile::IronOre, false, false, false}; break;
        case Block::Copper:t = {Tile::CopperOre, Tile::CopperOre, Tile::CopperOre, false, false, false}; break;
        case Block::Crystal:t = {Tile::CrystalOre, Tile::CrystalOre, Tile::CrystalOre, false, false, false}; break;
        case Block::Metal: t = {Tile::Metal, Tile::Metal, Tile::Metal, false, false, false}; break;
        case Block::Organic:t = {Tile::Organic, Tile::Organic, Tile::Organic, false, false, false}; break;
        case Block::Components:t = {Tile::Components, Tile::Components, Tile::Components, false, false, false}; break;

        // Modules
        case Block::SolarPanel: t = {Tile::SolarPanel, Tile::SolarPanel, Tile::SolarPanel, false, false, false}; break;
        case Block::EnergyGenerator: t = {Tile::EnergyGenerator, Tile::EnergyGenerator, Tile::EnergyGenerator, false, false, false}; break;
        case Block::WaterExtractor: t = {Tile::WaterExtractor, Tile::WaterExtractor, Tile::WaterExtractor, false, false, false}; break;
        case Block::OxygenGenerator: t = {Tile::OxygenGenerator, Tile::OxygenGenerator, Tile::OxygenGenerator, false, false, false}; break;
        case Block::Greenhouse: t = {Tile::Greenhouse, Tile::Greenhouse, Tile::Greenhouse, false, true, false}; break;
        case Block::CO2Factory: t = {Tile::CO2Factory, Tile::CO2Factory, Tile::CO2Factory, false, false, false}; break;
        case Block::Habitat: t = {Tile::Habitat, Tile::Habitat, Tile::Habitat, false, true, false}; break;
        case Block::Workshop: t = {Tile::Workshop, Tile::Workshop, Tile::Workshop, false, false, false}; break;
        case Block::TerraformerBeacon: t = {Tile::Terraformer, Tile::Terraformer, Tile::Terraformer, false, false, false}; break;

        // Base structures
        case Block::RocketHull: t = {Tile::RocketHull, Tile::RocketHull, Tile::RocketHull, false, false, false}; break;
        case Block::RocketEngine: t = {Tile::RocketEngine, Tile::RocketEngine, Tile::RocketEngine, false, false, false}; break;
        case Block::RocketWindow: t = {Tile::RocketWindow, Tile::RocketWindow, Tile::RocketWindow, false, true, false}; break;
        case Block::RocketNose: t = {Tile::RocketNose, Tile::RocketNose, Tile::RocketNose, false, false, false}; break;
        case Block::RocketFin: t = {Tile::RocketFin, Tile::RocketFin, Tile::RocketFin, false, false, false}; break;
        case Block::RocketDoor: t = {Tile::RocketDoor, Tile::RocketDoor, Tile::RocketDoor, false, false, false}; break;
        case Block::DomeGlass: t = {Tile::DomeGlass, Tile::DomeGlass, Tile::DomeGlass, false, true, false}; break;
        case Block::DomeFrame: t = {Tile::DomeFrame, Tile::DomeFrame, Tile::DomeFrame, false, false, false}; break;
        case Block::LandingPad: t = {Tile::LandingPad, Tile::LandingPad, Tile::LandingPad, false, false, false}; break;
        case Block::BuildSlot: t = {Tile::BuildSlot, Tile::BuildSlot, Tile::BuildSlot, false, true, false}; break;
        case Block::PipeH:
        case Block::PipeV: t = {Tile::Pipe, Tile::Pipe, Tile::Pipe, false, false, false}; break;
        case Block::Antenna: t = {Tile::Antenna, Tile::Antenna, Tile::Antenna, false, false, false}; break;

        default: t = {Tile::Missing, Tile::Missing, Tile::Missing, false, false, false}; break;
    }
    return t;
}

// ============= World =============
struct World {
    int w = 0;
    int h = 0;
    unsigned seed = 1337;
    int sea_level = 0;
    std::vector<Block> tiles;
    std::vector<Block> ground;
    std::vector<int16_t> heightmap; // altura do terreno por tile (0 = nivel base)
    std::vector<int> surface_y;

    World(int W, int H, unsigned s)
        : w(W)
        , h(H)
        , seed(s)
        , tiles((size_t)W * (size_t)H, Block::Air)
        , ground((size_t)W * (size_t)H, Block::Dirt)
        , heightmap((size_t)W * (size_t)H, 0)
        , surface_y((size_t)W, H / 2) {
        gen();
    }

    bool in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }

    Block get(int x, int y) const {
        if (!in_bounds(x, y)) return Block::Stone;
        return tiles[(size_t)y * (size_t)w + (size_t)x];
    }

    void set(int x, int y, Block b) {
        if (!in_bounds(x, y)) return;
        tiles[(size_t)y * (size_t)w + (size_t)x] = b;
    }

    Block get_ground(int x, int y) const {
        if (!in_bounds(x, y)) return Block::Dirt;
        return ground[(size_t)y * (size_t)w + (size_t)x];
    }

    void set_ground(int x, int y, Block b) {
        if (!in_bounds(x, y)) return;
        ground[(size_t)y * (size_t)w + (size_t)x] = b;
    }

    int16_t height_at(int x, int y) const {
        if (!in_bounds(x, y)) return 0;
        return heightmap[(size_t)y * (size_t)w + (size_t)x];
    }

    void set_height(int x, int y, int16_t v) {
        if (!in_bounds(x, y)) return;
        heightmap[(size_t)y * (size_t)w + (size_t)x] = v;
    }

    void rebuild_surface_cache() {
        surface_y.assign((size_t)w, h - 1);
        for (int x = 0; x < w; ++x) {
            int sy = h - 1;
            for (int y = 0; y < h; ++y) {
                Block b = get(x, y);
                if (b != Block::Air && b != Block::Water && b != Block::Leaves) {
                    sy = y;
                    break;
                }
            }
            surface_y[(size_t)x] = sy;
        }
    }

    // ============= WORLD GENERATION (Relevo 3D + Biomas Inospitos) =============
    // Objetivo:
    // - Terreno com altura real (montanhas, vales e desfiladeiros).
    // - Montanhas com neve (picos frios).
    // - Planeta inicialmente inospito: sem grama/plantas (isso so aparece apos terraformacao).
    // - Recursos (rochas/minerios) aparecem como "blocos" sobre o solo.
    void gen() {
        init_permutation(seed);

        std::fill(tiles.begin(), tiles.end(), Block::Air);
        std::fill(ground.begin(), ground.end(), Block::Dirt);
        std::fill(heightmap.begin(), heightmap.end(), 0);

        sea_level = 0; // Mantido para compatibilidade (sistema antigo)

        // Preencher surface_y para compatibilidade (top-down)
        for (int x = 0; x < w; ++x) {
            surface_y[(size_t)x] = 0;
        }

        // Tunings (0..1 para todos os noises)
        // Escalas menores => features maiores (continentes); escalas maiores => detalhes.
        static constexpr float kContScale = 0.0018f;     // macro elevacao (montanhas/vales)
        static constexpr float kRangeScale = 0.0048f;    // distribuicao de cordilheiras
        static constexpr float kPeakScale = 0.0095f;     // cristas/serras
        static constexpr float kDetailScale = 0.0350f;   // micro detalhe (quebra repeticao)
        static constexpr float kCanyonScale = 0.0320f;   // ruído para desfiladeiros
        static constexpr float kTempScale = 0.0019f;     // bandas climaticas largas
        static constexpr float kMoistScale = 0.0021f;

        static constexpr int16_t kMinH = 0;
        static constexpr int16_t kMaxH = 64;
        static constexpr int16_t kSeaH = 6;        // Abaixo disso: lagos congelados
        static constexpr int16_t kSnowLine = 44;   // Acima disso: neve/gelo (picos)

        static constexpr float kCanyonWidth = 0.030f;
        static constexpr int16_t kCanyonDepth = 28;

        // === PASSO 1: heightmap + solo ===
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float fx = (float)x;
                float fy = (float)y;

                float cont = fbm(fx * kContScale, fy * kContScale, 5);

                // Cordilheiras: mascara em bandas (0..1), com transicao suave.
                float range_n = fbm(fx * kRangeScale + 1200.0f, fy * kRangeScale + 1200.0f, 4);
                float range_mask = smoothstep01(0.54f, 0.80f, range_n);

                float peaks = ridged_fbm(fx * kPeakScale + 100.0f, fy * kPeakScale + 100.0f, 5);
                float peak_shaped = std::pow(peaks, 2.2f); // picos mais raros e definidos

                float detail = fbm(fx * kDetailScale + 250.0f, fy * kDetailScale + 250.0f, 3);

                // Combinacao: continentes + picos, com boost em cordilheiras.
                float hn = cont * 0.68f +
                    range_mask * 0.12f +
                    peak_shaped * (0.55f + 0.65f * range_mask) +
                    (detail - 0.5f) * 0.10f - 0.14f;
                hn = clamp01(hn);
                float shaped = std::pow(hn, 1.08f); // mais montanhas sem virar "dente de serra"

                int16_t th = (int16_t)std::lround(shaped * (float)kMaxH);

                // Desfiladeiros: linhas finas que "cortam" o relevo
                float canyon_n = perlin(fx * kCanyonScale + 2000.0f, fy * kCanyonScale + 2000.0f);
                float canyon = std::fabs(canyon_n - 0.5f); // 0..0.5
                if (canyon < kCanyonWidth && (range_mask > 0.25f || peaks > 0.55f)) {
                    float t = clamp01((kCanyonWidth - canyon) / kCanyonWidth);
                    int16_t extra = (int16_t)std::lround(range_mask * 10.0f);
                    int16_t cut = (int16_t)std::lround(t * (float)(kCanyonDepth + extra));
                    th = (int16_t)(th - cut);
                }

                th = (int16_t)std::clamp((int)th, (int)kMinH, (int)kMaxH);
                set_height(x, y, th);

                float lat = 0.0f;
                if (h > 1) {
                    float ny = (fy / (float)(h - 1)) * 2.0f - 1.0f;
                    lat = std::fabs(ny); // 0 equador .. 1 polos
                }

                float temp_n = fbm(fx * kTempScale + 900.0f, fy * kTempScale + 900.0f, 3);
                float temp = clamp01(temp_n * 0.70f + (1.0f - lat) * 0.30f);
                // planeta inicialmente frio
                temp = clamp01(temp * 0.82f - 0.06f); // 0 frio .. 1 quente
                float moisture = fbm(fx * kMoistScale + 1300.0f, fy * kMoistScale + 1300.0f, 3);  // 0 seco .. 1 umido
                float dryness = 1.0f - moisture;

                Block g = Block::Dirt;
                if (th <= kSeaH) {
                    g = Block::Ice; // planeta frio: agua principalmente congelada
                } else if (th >= kSnowLine || temp < (0.20f + lat * 0.18f)) {
                    float snow_var = fbm(fx * 0.06f + 7777.0f, fy * 0.06f + 7777.0f, 2);
                    g = (snow_var > 0.56f) ? Block::Ice : Block::Snow;
                } else if (dryness > 0.72f && temp > 0.48f) {
                    g = Block::Sand; // dunas / regiao seca
                } else {
                    g = Block::Dirt; // regolith
                }

                set_ground(x, y, g);
                set(x, y, g); // Top layer comeca igual ao solo
            }
        }

        // === PASSO 2: rochas/minerios como blocos sobre o solo ===
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                Block g = get_ground(x, y);
                int16_t th = height_at(x, y);

                // Evitar lagos congelados rasos
                if (g == Block::Ice && th <= kSeaH) continue;

                float fx = (float)x;
                float fy = (float)y;

                float range_n = fbm(fx * kRangeScale + 1200.0f, fy * kRangeScale + 1200.0f, 4);
                float range_mask = smoothstep01(0.54f, 0.80f, range_n);
                float peaks = ridged_fbm(fx * kPeakScale + 100.0f, fy * kPeakScale + 100.0f, 5);
                float rock = fbm(fx * 0.065f + 2100.0f, fy * 0.065f + 2100.0f, 3);

                // Rochas mais provaveis em cordilheiras/cristas e altitudes medias/altas
                float rock_bias = rock + peaks * 0.55f + range_mask * 0.65f + (th / (float)kMaxH) * 0.45f;
                if (rock_bias > 1.85f) {
                    set(x, y, Block::Stone);
                    continue;
                }

                // Minerios (outcrops) - thresholds mais agressivos para garantir jogabilidade
                float ore1 = fbm(fx * 0.12f + 200.0f, fy * 0.12f + 200.0f, 3);
                float ore2 = fbm(fx * 0.10f + 300.0f, fy * 0.10f + 300.0f, 3);
                float ore3 = fbm(fx * 0.15f + 400.0f, fy * 0.15f + 400.0f, 2);

                if (ore1 > 0.86f && th > kSeaH + 1) {
                    set(x, y, Block::Iron);
                } else if (ore1 > 0.83f && th > kSeaH + 1) {
                    set(x, y, Block::Coal);
                } else if (ore2 > 0.88f && th > kSeaH + 2) {
                    set(x, y, Block::Copper);
                } else if (ore3 > 0.90f && (g == Block::Snow || th > kSnowLine - 2)) {
                    set(x, y, Block::Crystal);
                } else if (ore2 > 0.92f && ore3 > 0.92f) {
                    set(x, y, Block::Metal);
                }

                // Materia organica (rarissima) como deposito - nao e vegetacao.
                if (get(x, y) == get_ground(x, y) && th > kSeaH + 1 && th < kSnowLine - 2) {
                    float moisture = fbm(fx * kMoistScale + 1300.0f, fy * kMoistScale + 1300.0f, 3);
                    float org = fbm(fx * 0.11f + 500.0f, fy * 0.11f + 500.0f, 2);
                    if (moisture > 0.70f && org > 0.92f) {
                        set(x, y, Block::Organic);
                    }
                }

                // Componentes (sucata/tech) em regioes secas
                if (get(x, y) == get_ground(x, y)) {
                    float dryness = 1.0f - fbm(fx * kMoistScale + 1300.0f, fy * kMoistScale + 1300.0f, 3);
                    float tech = fbm(fx * 0.085f + 4200.0f, fy * 0.085f + 4200.0f, 2);
                    if (dryness > 0.60f && tech > 0.93f) {
                        set(x, y, Block::Components);
                    }
                }
            }
        }

        rebuild_surface_cache(); // Legacy (nao critica para top-down atual)
    }
};

// Forward declarations (gameplay/render below)
static void rebuild_modules_from_world();
static bool save_game(const char* path);
static bool load_game(const char* path);
static void generate_base(World& world);
static Block surface_block_at(const World& world, int tx, int tz);
static Block object_block_at(const World& world, int tx, int tz);
static float surface_height_at(const World& world, int tx, int tz);

// ============= Gameplay State =============
static bool g_quit = false;
static const int WORLD_WIDTH = 512;
static const int WORLD_HEIGHT = 256;
static constexpr float TILE_PX = 16.0f;

// Sistema de zoom para melhor visibilidade
static float g_zoom = 2.0f;  // Zoom padrao 2x (tiles aparecem 32px)
static constexpr float kMinZoom = 1.5f;
static constexpr float kMaxZoom = 4.0f;

static World* g_world = nullptr;
static Vec2 g_cam_pos = {0.0f, 0.0f};  // Mantido para compatibilidade temporaria

// ============= CAMERA 3D (Terceira Pessoa) =============
struct Camera3D {
    Vec3 position;      // Posicao calculada da camera
    Vec3 target;        // Alvo (jogador)
    Vec3 up = {0.0f, 1.0f, 0.0f}; // Vetor up
    
    // Camera em terceira pessoa com horizonte visivel (menos "top-down")
    float distance = 24.0f;     // Distancia do jogador
    float yaw = 180.0f;         // Rotacao horizontal (graus)
    float pitch = 20.0f;        // Rotacao vertical (mais baixa para ver o horizonte)
    float min_pitch = 8.0f;
    float max_pitch = 65.0f;
    float min_distance = 6.0f;
    float max_distance = 90.0f;
    float sensitivity = 0.18f;  // Sensibilidade mais suave
    float smooth_speed = 6.0f;  // Suavizacao do seguimento
    
    // Distancia efetiva (apos colisao)
    float effective_distance = 24.0f;
};
static Camera3D g_camera;

// Calcular posicao da camera baseada em coordenadas esfericas
static void update_camera_position() {
    float rad_yaw = g_camera.yaw * (kPi / 180.0f);
    float rad_pitch = g_camera.pitch * (kPi / 180.0f);
    
    // Posicao da camera em coordenadas esfericas relativas ao target
    float x = g_camera.effective_distance * std::cos(rad_pitch) * std::sin(rad_yaw);
    float y = g_camera.effective_distance * std::sin(rad_pitch);
    float z = g_camera.effective_distance * std::cos(rad_pitch) * std::cos(rad_yaw);
    
    g_camera.position.x = g_camera.target.x + x;
    g_camera.position.y = g_camera.target.y + y;
    g_camera.position.z = g_camera.target.z + z;
}

// Aplicar matriz de view (gluLookAt manual)
static void apply_look_at() {
    Vec3 f = vec3_normalize(vec3_sub(g_camera.target, g_camera.position)); // Forward
    Vec3 s = vec3_normalize(vec3_cross(f, g_camera.up));                   // Side (right)
    Vec3 u = vec3_cross(s, f);                                              // Up ajustado
    
    // Matriz de view (column-major para OpenGL)
    float m[16] = {
         s.x,  u.x, -f.x, 0.0f,
         s.y,  u.y, -f.y, 0.0f,
         s.z,  u.z, -f.z, 0.0f,
        -vec3_dot(s, g_camera.position),
        -vec3_dot(u, g_camera.position),
         vec3_dot(f, g_camera.position),
        1.0f
    };
    
    glMultMatrixf(m);
}

// Projecao perspectiva manual
static void apply_perspective(float fov_degrees, float aspect, float near_plane, float far_plane) {
    float fov_rad = fov_degrees * (kPi / 180.0f);
    float f = 1.0f / std::tan(fov_rad / 2.0f);
    
    float m[16] = {
        f / aspect, 0.0f,  0.0f,                                           0.0f,
        0.0f,       f,     0.0f,                                           0.0f,
        0.0f,       0.0f, (far_plane + near_plane) / (near_plane - far_plane), -1.0f,
        0.0f,       0.0f, (2.0f * far_plane * near_plane) / (near_plane - far_plane), 0.0f
    };
    
    glMultMatrixf(m);
}

// Verificar colisao da camera com o mundo usando raycast
// Nota: g_world e is_solid sao definidos apos esta funcao
static void check_camera_collision() {
    if (!g_world) return;
    
    // Direcao do target para a posicao desejada da camera
    Vec3 dir = vec3_sub(g_camera.position, g_camera.target);
    float max_dist = vec3_length(dir);
    if (max_dist < 0.1f) {
        g_camera.effective_distance = g_camera.distance;
        return;
    }
    
    dir = vec3_normalize(dir);
    g_camera.effective_distance = g_camera.distance;
    
    // Raycast do target em direcao a camera
    for (float t = 0.5f; t < max_dist; t += 0.3f) {
        float test_x = g_camera.target.x + dir.x * t;
        float test_y = g_camera.target.y + dir.y * t;
        float test_z = g_camera.target.z + dir.z * t;
        
        int bx = (int)std::floor(test_x);
        int bz = (int)std::floor(test_z);

        bool hit = false;
        if (!g_world->in_bounds(bx, bz)) {
            hit = true;
        } else {
            float ground_y = (float)g_world->height_at(bx, bz) * kHeightScale;
            float top_y = surface_height_at(*g_world, bx, bz);
            Block obj = object_block_at(*g_world, bx, bz);

            // Colide com o terreno (nao deixar a camera atravessar o chao)
            if (test_y < ground_y + 0.15f) {
                hit = true;
            }
            // Colide com objetos (cubos) sobre o terreno
            if (!hit && obj != Block::Air) {
                if (test_y >= ground_y && test_y <= top_y) hit = true;
            }
        }

        if (hit) {
            g_camera.effective_distance = std::max(g_camera.min_distance, t - 0.5f);
            break;
        }
    }
}

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
    bool is_moving = false;  // Se esta andando
    
    // Movimento suave
    float speed_mult = 1.0f;  // Multiplicador de velocidade (acelera gradualmente)
};

static Player g_player;

static std::array<int, kBlockTypeCount> g_inventory = {};
static Block g_selected = Block::Dirt;

static bool g_prev_lmb = false;
static bool g_prev_rmb = false;
static bool g_prev_esc = false;
static bool g_prev_enter = false;
static bool g_prev_e = false;  // Tecla de interacao (top-down)
static bool g_prev_f5 = false;
static bool g_prev_f9 = false;
static bool g_prev_l = false;
static bool g_prev_q = false;
static bool g_prev_f3 = false;
static bool g_prev_h = false;
static bool g_prev_tab = false;
static bool g_prev_b = false;

static bool g_debug = false;

static float g_place_cd = 0.0f;
static float g_drown_accum = 0.0f;

// Mining progress (estilo Minicraft/Minecraft: segurar para quebrar)
static int g_mine_block_x = -1;
static int g_mine_block_y = -1;
static float g_mine_progress = 0.0f; // 0..1

static bool g_has_target = false;
static int g_target_x = 0;
static int g_target_y = 0;
static bool g_target_in_range = false;

// Target de colocacao (tile onde o RMB vai tentar colocar)
static bool g_has_place_target = false;
static int g_place_x = 0;
static int g_place_y = 0;
static bool g_place_in_range = false;

struct Particle {
    Vec2 pos;
    Vec2 vel;
    float life;
    float r, g, b, a;
};
static std::vector<Particle> g_particles;

// Drops coletaveis (estilo Minicraft/Minecraft)
struct ItemDrop {
    Block item = Block::Stone;
    float x = 0.0f;
    float z = 0.0f;
    float y = 0.25f;
    float vy = 0.0f;
    float t = 0.0f;
    float pickup_delay = 0.12f;
};
static std::vector<ItemDrop> g_drops;

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
};
static std::vector<Module> g_modules;

// ============= Generate Base (Landing Site) =============
static void generate_base(World& world) {
    g_build_slots.clear();
    
    // Top-down: escolher um "bom ponto" perto do centro (evita agua/gelo e terreno muito inclinado)
    int center_x = world.w / 2;
    int center_y = world.h / 2;
    int best_x = center_x;
    int best_y = center_y;
    int best_score = std::numeric_limits<int>::min();

    // Margens: base/rocket/domo usam offsets negativos em Y (para "cima" no mapa)
    int margin_x = 40;
    int margin_y = 30;

    for (int y = center_y - 45; y <= center_y + 45; y += 2) {
        for (int x = center_x - 70; x <= center_x + 70; x += 2) {
            if (x < margin_x || x >= world.w - margin_x) continue;
            if (y < margin_y || y >= world.h - margin_y) continue;

            int score = 0;
            int16_t min_h = std::numeric_limits<int16_t>::max();
            int16_t max_h = std::numeric_limits<int16_t>::min();
            // Amostra uma "área de pouso" menor, suficiente para decidir.
            for (int dy = -10; dy <= 10; ++dy) {
                for (int dx = -18; dx <= 18; ++dx) {
                    int sx = x + dx;
                    int sy = y + dy;
                    if (!world.in_bounds(sx, sy)) { score -= 10; continue; }

                    int16_t hh = world.height_at(sx, sy);
                    min_h = std::min(min_h, hh);
                    max_h = std::max(max_h, hh);

                    // Penalizar objetos (rochas/minerios/modulos) na area de pouso
                    if (object_block_at(world, sx, sy) != Block::Air) score -= 6;

                    // Preferir solo seco/estavel
                    Block surface = surface_block_at(world, sx, sy);
                    if (surface == Block::Water || surface == Block::Ice) score -= 10;
                    else if (surface == Block::Snow) score -= 2;
                    else if (surface == Block::Sand) score += 1;
                    else if (surface == Block::Dirt) score += 2;
                    else if (surface == Block::Grass) score += 3;
                }
            }

            // Penalizar area inclinada (base precisa ser plana)
            int range = (int)max_h - (int)min_h;
            score -= range * 6;
            if (min_h <= 8) score -= 30; // muito perto de baixadas geladas

            if (score > best_score) {
                best_score = score;
                best_x = x;
                best_y = y;
            }
        }
    }

    g_base_x = best_x;
    int surface = best_y;
    g_base_y = surface;

    // === FLATTEN HEIGHTMAP (base precisa ser plana no terreno 3D) ===
    int16_t base_h = world.height_at(best_x, surface);
    for (int dy = -30; dy <= 25; ++dy) {
        for (int dx = -40; dx <= 40; ++dx) {
            int tx = best_x + dx;
            int ty = surface + dy;
            if (!world.in_bounds(tx, ty)) continue;
            world.set_height(tx, ty, base_h);
            // Limpar objetos existentes (rochas/minerios) para nao poluir a base
            if (object_block_at(world, tx, ty) != Block::Air) {
                world.set(tx, ty, Block::Air);
            }
        }
    }
     
    // === PLATAFORMA DA BASE (3D) ===
    // A base anterior era desenhada como "sprite" no grid (bom para top-down),
    // mas em camera 3D isso parecia um desenho no chao. Aqui criamos uma plataforma real.
    static constexpr int kPadHalfW = 22;
    static constexpr int kPadHalfH = 12;

    // Levantar 1 unidade de heightmap (=> 0.25 no mundo) para dar volume nas bordas via paredes.
    int16_t pad_h = (int16_t)std::clamp((int)base_h + 1, 0, 256);

    for (int dy = -kPadHalfH; dy <= kPadHalfH; ++dy) {
        for (int dx = -kPadHalfW; dx <= kPadHalfW; ++dx) {
            int tx = best_x + dx;
            int ty = surface + dy;
            if (!world.in_bounds(tx, ty)) continue;

            world.set_height(tx, ty, pad_h);

            // Limpar objetos existentes (rochas/minerios) para nao poluir a base
            if (object_block_at(world, tx, ty) != Block::Air) {
                world.set(tx, ty, Block::Air);
            }

            world.set_ground(tx, ty, Block::LandingPad);
            world.set(tx, ty, Block::LandingPad);
        }
    }

    auto place_slot = [&](int sx, int sy, const std::string& label) {
        if (!world.in_bounds(sx, sy)) return;
        world.set_ground(sx, sy, Block::BuildSlot);
        world.set(sx, sy, Block::BuildSlot);
        g_build_slots.push_back({sx, sy, Block::Air, label});
    };

    // === SLOTS DE CONSTRUCAO (organizados em grid) ===
    int cx = best_x;
    int cy = surface;
    int front_y = cy - 6;
    int back_y = cy + 5;
    int mid_y = cy + 1;

    // Solar (3 slots)
    for (int i = 0; i < 3; ++i) {
        int sx = cx - 12 + i * 2;
        place_slot(sx, front_y, "Solar " + std::to_string(i + 1));
    }

    // Agua / Oxigenio
    place_slot(cx + 6, front_y, "Water Extractor");
    place_slot(cx + 8, front_y, "O2 Generator");

    // Estufas (2 slots)
    place_slot(cx - 14, back_y, "Greenhouse 1");
    place_slot(cx - 12, back_y, "Greenhouse 2");

    // Terraformacao (CO2 + Terraformer)
    place_slot(cx + 12, back_y, "CO2 Factory");
    place_slot(cx + 14, back_y, "Terraformer");

    // Habitat (centro)
    place_slot(cx - 1, mid_y, "Habitat");

    // === DECORACAO 3D (simples) ===
    // Pequeno "wreck" de foguete (nao deitado como sprite)
    {
        int rx = cx + 14;
        int ry = cy - 1;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (world.in_bounds(rx + dx, ry + dy)) world.set(rx + dx, ry + dy, Block::RocketHull);
            }
        }
        if (world.in_bounds(rx, ry)) world.set(rx, ry, Block::RocketEngine);
        if (world.in_bounds(rx, ry - 2)) world.set(rx, ry - 2, Block::RocketNose);
    }

    // Pequeno "hub" em domo (anel)
    {
        int dx0 = cx - 12;
        int dy0 = cy - 1;
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                int tx = dx0 + dx;
                int ty = dy0 + dy;
                if (!world.in_bounds(tx, ty)) continue;

                if (std::abs(dx) == 2 || std::abs(dy) == 2) {
                    world.set(tx, ty, Block::DomeFrame);
                } else if (dx == 0 && dy == 0) {
                    world.set(tx, ty, Block::DomeGlass);
                }
            }
        }
        if (world.in_bounds(dx0, dy0 - 3)) world.set(dx0, dy0 - 3, Block::Antenna);
    }

    // === MODULO INICIAL (painel solar) ===
    if (!g_build_slots.empty()) {
        int sx = g_build_slots[0].x;
        int sy = g_build_slots[0].y;
        world.set(sx, sy, Block::SolarPanel);
        g_modules.push_back(Module{sx, sy, Block::SolarPanel, 0.0f});
        g_build_slots[0].assigned_module = Block::SolarPanel;
    }
    
    world.rebuild_surface_cache();
}

static uint32_t g_rng = 0xA341316Cu;

static uint32_t rng_next_u32() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static float rng_next_f01() {
    return (rng_next_u32() & 0x00FFFFFFu) / (float)0x01000000u;
}

enum class GameState {
    Playing = 0,
    Paused,
    Menu,
    Dead,      // Death screen
    Settings,  // Settings menu
};

static GameState g_state = GameState::Playing;

static float g_day_time = 0.0f;
static constexpr float kDayLength = 150.0f; // seconds

static float g_stats_timer = 0.0f;
static bool g_surface_dirty = true;

static float g_toast_time = 0.0f;
static std::string g_toast;

static void set_toast(const std::string& msg, float seconds = 2.0f) {
    g_toast = msg;
    g_toast_time = seconds;
}

// ============= FUNCOES DE FEEDBACK VISUAL =============

// Mostrar erro com flash vermelho
static void show_error(const std::string& msg) {
    set_toast(msg, 2.0f);
    g_screen_flash_red = 0.25f;
}

// Mostrar sucesso com flash verde
static void show_success(const std::string& msg) {
    set_toast(msg, 2.0f);
    g_screen_flash_green = 0.20f;
}

// Adicionar popup de coleta flutuante
static void add_collect_popup(float x, float y, const std::string& text, float r, float g, float b,
                              Block item = Block::Air, int amount = 1) {
    CollectPopup popup;
    popup.x = x;
    popup.y = y;
    popup.item = item;
    popup.amount = amount;
    popup.text = text;
    popup.life = 1.5f;
    popup.r = r;
    popup.g = g;
    popup.b = b;
    g_collect_popups.push_back(popup);

    // Evita acumular infinito em runs longas
    if (g_collect_popups.size() > 12u) {
        g_collect_popups.erase(g_collect_popups.begin(), g_collect_popups.begin() + (g_collect_popups.size() - 12u));
    }
}

// Mostrar popup de desbloqueio grande
static void show_unlock_popup(const std::string& title, const std::string& subtitle) {
    g_unlock_popup_text = title;
    g_unlock_popup_subtitle = subtitle;
    g_unlock_popup_timer = 3.5f;
    g_screen_flash_green = 0.3f;
    
    // Onboarding: dica ao desbloquear algo pela primeira vez
    if (!g_onboarding.shown_first_unlock) {
        g_onboarding.shown_first_unlock = true;
    }
}

// Animar bounce no slot da hotbar
static void bounce_hotbar_slot(int slot) {
    g_hotbar_bounce = 0.3f;
    g_hotbar_bounce_slot = slot;
}

// ============= FUNCOES DE ONBOARDING =============

// Mostrar dica contextual (apenas uma vez)
static void show_tip(const std::string& tip, bool& shown_flag) {
    if (shown_flag) return;
    shown_flag = true;
    g_onboarding.current_tip = tip;
    g_onboarding.tip_timer = 4.0f;
}

// Atualizar sistema de onboarding
static void update_onboarding(float dt) {
    if (g_onboarding.tip_timer > 0.0f) {
        g_onboarding.tip_timer -= dt;
        if (g_onboarding.tip_timer <= 0.0f) {
            g_onboarding.current_tip = "";
        }
    }
}

// ============= Font =============
static GLuint g_font_base = 0;

static void init_font(HDC hdc) {
    if (g_font_base != 0) return;
    HFONT font = CreateFontA(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        FF_DONTCARE | DEFAULT_PITCH, "Consolas");
    if (!font) return;

    HGDIOBJ old = SelectObject(hdc, font);
    g_font_base = glGenLists(96);
    wglUseFontBitmapsA(hdc, 32, 96, g_font_base);
    SelectObject(hdc, old);
    DeleteObject(font);
}

static void draw_text(float x, float y, const std::string& s, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) {
    if (g_font_base == 0 || s.empty()) return;
    glColor4f(r, g, b, a);
    glRasterPos2f(x, y);
    glPushAttrib(GL_LIST_BIT);
    glListBase(g_font_base - 32);
    glCallLists((GLsizei)s.size(), GL_UNSIGNED_BYTE, s.c_str());
    glPopAttrib();
}

static float estimate_text_w_px(const std::string& s) {
    return (float)s.size() * 8.0f;
}

// ============= Save/Load =============
static const char* kSavePath = "save_slot0.tf2d";

static void rebuild_modules_from_world() {
    g_modules.clear();
    if (!g_world) return;
    for (int y = 0; y < g_world->h; ++y) {
        for (int x = 0; x < g_world->w; ++x) {
            Block b = g_world->get(x, y);
            if (is_module(b)) g_modules.push_back(Module{x, y, b, 0.0f});
        }
    }
}

static bool save_game(const char* path) {
    if (!g_world) return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    const char magic[4] = {'T', 'F', '3', 'D'};  // Atualizado para 3D
    uint32_t version = 5;  // Version 5 - adds heightmap + ground layer (relevo 3D)
    uint32_t w = (uint32_t)g_world->w;
    uint32_t h = (uint32_t)g_world->h;
    uint32_t seed = (uint32_t)g_world->seed;

    f.write(magic, 4);
    f.write((const char*)&version, sizeof(version));
    f.write((const char*)&w, sizeof(w));
    f.write((const char*)&h, sizeof(h));
    f.write((const char*)&seed, sizeof(seed));

    f.write((const char*)&g_player.pos, sizeof(g_player.pos));
    f.write((const char*)&g_player.vel, sizeof(g_player.vel));
    int32_t hp = (int32_t)g_player.hp;
    f.write((const char*)&hp, sizeof(hp));

    uint8_t sel = (uint8_t)g_selected;
    f.write((const char*)&sel, sizeof(sel));

    uint32_t inv_count = (uint32_t)kBlockTypeCount;
    f.write((const char*)&inv_count, sizeof(inv_count));
    for (uint32_t i = 0; i < inv_count; ++i) {
        int32_t c = (int32_t)g_inventory[(size_t)i];
        f.write((const char*)&c, sizeof(c));
    }

    f.write((const char*)&g_energy, sizeof(g_energy));
    f.write((const char*)&g_water_res, sizeof(g_water_res));
    f.write((const char*)&g_oxygen, sizeof(g_oxygen));
    f.write((const char*)&g_day_time, sizeof(g_day_time));
    
    // Version 3 additions
    f.write((const char*)&g_base_x, sizeof(g_base_x));
    f.write((const char*)&g_base_y, sizeof(g_base_y));
    f.write((const char*)&g_food, sizeof(g_food));
    f.write((const char*)&g_temperature, sizeof(g_temperature));
    f.write((const char*)&g_co2_level, sizeof(g_co2_level));
    f.write((const char*)&g_atmosphere, sizeof(g_atmosphere));
    f.write((const char*)&g_terraform, sizeof(g_terraform));
    uint8_t phase = (uint8_t)g_phase;
    f.write((const char*)&phase, sizeof(phase));
    
    // Unlocks
    f.write((const char*)&g_unlocks.total_stone, sizeof(g_unlocks.total_stone));
    f.write((const char*)&g_unlocks.total_iron, sizeof(g_unlocks.total_iron));
    f.write((const char*)&g_unlocks.total_coal, sizeof(g_unlocks.total_coal));
    f.write((const char*)&g_unlocks.total_copper, sizeof(g_unlocks.total_copper));
    f.write((const char*)&g_unlocks.total_wood, sizeof(g_unlocks.total_wood));
    uint8_t unlocks_flags = 
        (g_unlocks.solar_unlocked ? 1 : 0) |
        (g_unlocks.water_extractor_unlocked ? 2 : 0) |
        (g_unlocks.o2_generator_unlocked ? 4 : 0) |
        (g_unlocks.greenhouse_unlocked ? 8 : 0) |
        (g_unlocks.co2_factory_unlocked ? 16 : 0) |
        (g_unlocks.habitat_unlocked ? 32 : 0) |
        (g_unlocks.terraformer_unlocked ? 64 : 0);
    f.write((const char*)&unlocks_flags, sizeof(unlocks_flags));
    
    // Version 4: Camera 3D settings
    f.write((const char*)&g_camera.distance, sizeof(g_camera.distance));
    f.write((const char*)&g_camera.yaw, sizeof(g_camera.yaw));
    f.write((const char*)&g_camera.pitch, sizeof(g_camera.pitch));
    f.write((const char*)&g_camera.sensitivity, sizeof(g_camera.sensitivity));
    f.write((const char*)&g_player.rotation, sizeof(g_player.rotation));

    static_assert(sizeof(Block) == 1, "Block must be 1 byte for save format.");
    static_assert(sizeof(int16_t) == 2, "int16_t must be 2 bytes for save format.");

    // Ground layer (solo), heightmap e depois top layer (objetos/overrides)
    f.write((const char*)g_world->ground.data(), (std::streamsize)g_world->ground.size());
    f.write((const char*)g_world->heightmap.data(), (std::streamsize)(g_world->heightmap.size() * sizeof(int16_t)));
    f.write((const char*)g_world->tiles.data(), (std::streamsize)g_world->tiles.size());

    return (bool)f;
}

static bool load_game(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    char magic[4] = {};
    f.read(magic, 4);
    // Aceitar tanto TF2D (versoes antigas) quanto TF3D (versao 4+)
    bool valid_magic = (magic[0] == 'T' && magic[1] == 'F' && 
                        (magic[2] == '2' || magic[2] == '3') && magic[3] == 'D');
    if (!f || !valid_magic) return false;

    uint32_t version = 0;
    uint32_t w = 0, h = 0, seed = 0;
    f.read((char*)&version, sizeof(version));
    f.read((char*)&w, sizeof(w));
    f.read((char*)&h, sizeof(h));
    f.read((char*)&seed, sizeof(seed));
    if (!f || w == 0 || h == 0 || w > 4096 || h > 4096) return false;

    Vec2 pos{}, vel{};
    int32_t hp = 100;
    uint8_t sel = (uint8_t)Block::Dirt;
    f.read((char*)&pos, sizeof(pos));
    f.read((char*)&vel, sizeof(vel));
    f.read((char*)&hp, sizeof(hp));
    f.read((char*)&sel, sizeof(sel));
    if (!f) return false;

    std::array<int, kBlockTypeCount> inv = {};
    inv.fill(0);

    float energy = 0.0f, water_res = 100.0f, oxygen = 100.0f, day_time = 0.0f;
    float food = 100.0f, temperature = -60.0f, co2_level = 0.0f, atmosphere = 5.0f, terraform = 0.0f;
    int base_x = 0, base_y = 0;
    uint8_t phase = 0;
    UnlockProgress unlocks{};
    float placeholder_fuel = 100.0f; // Placeholder para compatibilidade (era jetpack_fuel)
    
    // Camera 3D settings (Version 4+)
    float cam_distance = 10.0f, cam_yaw = 180.0f, cam_pitch = 35.0f, cam_sensitivity = 0.25f;
    float player_rotation = 180.0f;

    if (version == 1) {
        constexpr uint32_t kV1InvCount = (uint32_t)Block::Iron + 1;
        for (uint32_t i = 0; i < kV1InvCount; ++i) {
            int32_t c = 0;
            f.read((char*)&c, sizeof(c));
            if (i < (uint32_t)kBlockTypeCount) inv[(size_t)i] = (int)c;
        }
    } else if (version >= 2 && version <= 5) {
        uint32_t inv_count = 0;
        f.read((char*)&inv_count, sizeof(inv_count));
        if (!f || inv_count > 4096) return false;
        for (uint32_t i = 0; i < inv_count; ++i) {
            int32_t c = 0;
            f.read((char*)&c, sizeof(c));
            if (i < (uint32_t)kBlockTypeCount) inv[(size_t)i] = (int)c;
        }
        f.read((char*)&energy, sizeof(energy));
        f.read((char*)&water_res, sizeof(water_res));
        f.read((char*)&oxygen, sizeof(oxygen));
        f.read((char*)&day_time, sizeof(day_time));
        
        if (version >= 3) {
            // Version 3 additions
            f.read((char*)&base_x, sizeof(base_x));
            f.read((char*)&base_y, sizeof(base_y));
            f.read((char*)&food, sizeof(food));
            f.read((char*)&temperature, sizeof(temperature));
            f.read((char*)&co2_level, sizeof(co2_level));
            f.read((char*)&atmosphere, sizeof(atmosphere));
            f.read((char*)&terraform, sizeof(terraform));
            f.read((char*)&phase, sizeof(phase));
            
            // Unlocks
            f.read((char*)&unlocks.total_stone, sizeof(unlocks.total_stone));
            f.read((char*)&unlocks.total_iron, sizeof(unlocks.total_iron));
            f.read((char*)&unlocks.total_coal, sizeof(unlocks.total_coal));
            f.read((char*)&unlocks.total_copper, sizeof(unlocks.total_copper));
            f.read((char*)&unlocks.total_wood, sizeof(unlocks.total_wood));
            uint8_t unlocks_flags = 0;
            f.read((char*)&unlocks_flags, sizeof(unlocks_flags));
            unlocks.solar_unlocked = (unlocks_flags & 1) != 0;
            unlocks.water_extractor_unlocked = (unlocks_flags & 2) != 0;
            unlocks.o2_generator_unlocked = (unlocks_flags & 4) != 0;
            unlocks.greenhouse_unlocked = (unlocks_flags & 8) != 0;
            unlocks.co2_factory_unlocked = (unlocks_flags & 16) != 0;
            unlocks.habitat_unlocked = (unlocks_flags & 32) != 0;
            unlocks.terraformer_unlocked = (unlocks_flags & 64) != 0;
            
            if (version == 3) {
                f.read((char*)&placeholder_fuel, sizeof(placeholder_fuel)); // Compatibilidade v3
            }
        }
        
        // Version 4: Camera 3D settings
        if (version >= 4) {
            f.read((char*)&cam_distance, sizeof(cam_distance));
            f.read((char*)&cam_yaw, sizeof(cam_yaw));
            f.read((char*)&cam_pitch, sizeof(cam_pitch));
            f.read((char*)&cam_sensitivity, sizeof(cam_sensitivity));
            f.read((char*)&player_rotation, sizeof(player_rotation));
        }
    } else {
        return false;
    }
    if (!f) return false;

    World* nw = new World((int)w, (int)h, (unsigned)seed);
    size_t tile_count = (size_t)w * (size_t)h;
    nw->tiles.assign(tile_count, Block::Air);
    nw->ground.assign(tile_count, Block::Dirt);
    nw->heightmap.assign(tile_count, 0);

    if (version >= 5) {
        // v5+: ground layer + heightmap + top layer
        std::vector<uint8_t> raw_ground(tile_count, 0);
        std::vector<int16_t> raw_h(tile_count, 0);
        std::vector<uint8_t> raw_tiles(tile_count, 0);

        f.read((char*)raw_ground.data(), (std::streamsize)raw_ground.size());
        f.read((char*)raw_h.data(), (std::streamsize)(raw_h.size() * sizeof(int16_t)));
        f.read((char*)raw_tiles.data(), (std::streamsize)raw_tiles.size());
        if (!f) {
            delete nw;
            return false;
        }

        for (size_t i = 0; i < tile_count; ++i) {
            uint8_t gv = raw_ground[i];
            uint8_t tv = raw_tiles[i];

            nw->ground[i] = (gv < (uint8_t)kBlockTypeCount) ? (Block)gv : Block::Dirt;
            nw->tiles[i] = (tv < (uint8_t)kBlockTypeCount) ? (Block)tv : Block::Air;

            int16_t hh = raw_h[i];
            nw->heightmap[i] = (int16_t)std::clamp((int)hh, 0, 256);
        }
    } else {
        // v1..v4: apenas tiles (mundo era "plano").
        std::vector<uint8_t> raw(tile_count, 0);
        f.read((char*)raw.data(), (std::streamsize)raw.size());
        if (!f) {
            delete nw;
            return false;
        }
        for (size_t i = 0; i < tile_count; ++i) {
            uint8_t v = raw[i];
            nw->tiles[i] = (v < (uint8_t)kBlockTypeCount) ? (Block)v : Block::Air;
            // Melhor esforco: manter solo coerente com tiles walkable; objetos ficam no top layer.
            nw->ground[i] = (nw->tiles[i] != Block::Air && is_ground_like(nw->tiles[i])) ? nw->tiles[i] : Block::Dirt;
            nw->heightmap[i] = 0;
        }
    }
    nw->rebuild_surface_cache();

    delete g_world;
    g_world = nw;
    g_player.pos = pos;
    g_player.vel = vel;
    g_player.vel_y = 0.0f;
    {
        int tx = (int)std::floor(g_player.pos.x);
        int tz = (int)std::floor(g_player.pos.y);
        if (g_world->in_bounds(tx, tz)) {
            g_player.pos_y = surface_height_at(*g_world, tx, tz);
        } else {
            g_player.pos_y = 0.0f;
        }
        g_player.ground_height = g_player.pos_y;
        g_player.on_ground = true;
        g_player.can_jump = true;
    }
    g_player.hp = std::clamp((int)hp, 0, 100);
    g_selected = ((int)sel >= 0 && (int)sel < kBlockTypeCount) ? (Block)sel : Block::Dirt;
    g_inventory = inv;
    g_particles.clear();
    g_drops.clear();

    g_energy = std::clamp(energy, 0.0f, kEnergyMax);
    g_water_res = std::clamp(water_res, 0.0f, 100.0f);
    g_oxygen = std::clamp(oxygen, 0.0f, 100.0f);
    g_day_time = std::fmax(0.0f, day_time);
    
    // Version 3 data
    g_base_x = base_x;
    g_base_y = base_y;
    g_food = std::clamp(food, 0.0f, 100.0f);
    g_temperature = std::clamp(temperature, -100.0f, 100.0f);
    g_co2_level = std::clamp(co2_level, 0.0f, 100.0f);
    g_atmosphere = std::clamp(atmosphere, 0.0f, 100.0f);
    g_terraform = std::clamp(terraform, 0.0f, 100.0f);
    g_phase = (phase < 5) ? (TerraPhase)phase : TerraPhase::Frozen;
    g_unlocks = unlocks;
    
    // Camera 3D settings (Version 4)
    g_camera.distance = std::clamp(cam_distance, g_camera.min_distance, g_camera.max_distance);
    g_camera.effective_distance = g_camera.distance;
    g_camera.yaw = cam_yaw;
    g_camera.pitch = std::clamp(cam_pitch, g_camera.min_pitch, g_camera.max_pitch);
    g_camera.sensitivity = std::clamp(cam_sensitivity, 0.05f, 1.0f);
    g_player.rotation = player_rotation;
    g_player.target_rotation = player_rotation;

    g_cam_pos = g_player.pos;
    g_surface_dirty = true;
    g_victory = false;
    g_show_build_menu = false;
    rebuild_modules_from_world();
    return true;
}

// ============= Movement / Collision =============
static float approach(float cur, float target, float max_delta) {
    float d = target - cur;
    if (d > max_delta) return cur + max_delta;
    if (d < -max_delta) return cur - max_delta;
    return target;
}

// Top-down: coloca jogador em posicao segura (walkable)
static void place_player_near(World& world, int x) {
    x = std::clamp(x, 0, world.w - 1);
    // Em top-down, usar centro do mapa em Y
    int y = world.h / 2;
    
    // Encontrar tile walkable proximo
    for (int radius = 0; radius < 20; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                int tx = x + dx, ty = y + dy;
                if (tx < 1 || tx >= world.w - 1 || ty < 1 || ty >= world.h - 1) continue;
                if (!is_solid(world.get(tx, ty))) {
                    g_player.pos = {(float)tx + 0.5f, (float)ty + 0.5f};
                    g_player.vel = {0.0f, 0.0f};
                    return;
                }
            }
        }
    }
    // Fallback
    g_player.pos = {(float)x + 0.5f, (float)y + 0.5f};
    g_player.vel = {0.0f, 0.0f};
}

static int find_spawn_x(const World& world) {
    int mid = world.w / 2;
    for (int off = 0; off < world.w / 2; ++off) {
        for (int s = 0; s < 2; ++s) {
            int x = mid + (s == 0 ? off : -off);
            if (x < 0 || x >= world.w) continue;
            int sy = world.surface_y[(size_t)x];
            if (sy < world.sea_level - 2) return x;
        }
    }
    return mid;
}

static void spawn_player_at_base() {
    // Spawn player at the base
    g_player.pos.x = (float)g_base_x;
    g_player.pos.y = (float)g_base_y;
    g_player.vel = {0.0f, 0.0f};
    g_player.vel_y = 0.0f;
    g_player.pos_y = 0.0f;
    if (g_world && g_world->in_bounds(g_base_x, g_base_y)) {
        g_player.pos_y = surface_height_at(*g_world, g_base_x, g_base_y);
    }
    g_player.on_ground = true;
    g_player.can_jump = true;
    g_player.ground_height = g_player.pos_y;
    g_player.facing_dir = 2; // Olhando para sul
}

static void spawn_player_new_game(World& world) {
    // Generate base first (sets g_base_x and g_base_y)
    generate_base(world);
    
    // Spawn at base
    spawn_player_at_base();
    
    g_player.hp = 100;
    g_player.facing_dir = 2; // Olhando para sul

    // Starter kit - some resources to start building
    g_inventory.fill(0);
    g_inventory[(int)Block::Dirt] = 20;
    g_inventory[(int)Block::Stone] = 10;
    g_inventory[(int)Block::Iron] = 5;
    g_selected = Block::Dirt;
    
    // Player suit tanks - start full
    g_player_oxygen = 100.0f;
    g_player_water = 100.0f;
    g_player_food = 100.0f;
    
    // Base storage - start with some resources from the rocket
    g_base_energy = 100.0f;   // Solar panel starts generating
    g_base_water = 30.0f;     // Small water reserve
    g_base_oxygen = 50.0f;    // Some O2 from the rocket
    g_base_food = 40.0f;      // Emergency rations
    g_base_integrity = 100.0f;  // Base starts in perfect condition
    
    // Clear construction queue
    g_construction_queue.clear();
    g_alerts.clear();
    
    // Sync legacy variables
    g_energy = g_base_energy;
    g_water_res = g_player_water;
    g_oxygen = g_player_oxygen;
    g_food = g_player_food;
    
    // Reset terraforming state - frozen planet
    g_temperature = -60.0f;
    g_co2_level = 0.0f;
    g_atmosphere = 5.0f;  // Thin atmosphere
    g_terraform = 0.0f;
    g_phase = TerraPhase::Frozen;
    g_victory = false;
    
    // Progressive unlock system - only basic modules start unlocked
    g_unlocks = UnlockProgress{};
    g_unlocks.solar_unlocked = true;        // Começa com painel solar
    g_unlocks.water_extractor_unlocked = false;  // Desbloqueia ao coletar gelo
    g_unlocks.o2_generator_unlocked = false;     // Desbloqueia ao coletar ferro
    g_unlocks.greenhouse_unlocked = false;       // Desbloqueia ao ter agua
    g_unlocks.co2_factory_unlocked = false;      // Desbloqueia ao ter O2
    g_unlocks.habitat_unlocked = false;          // Desbloqueia ao ter estufa
    g_unlocks.terraformer_unlocked = false;      // Desbloqueia no final
    
    g_show_build_menu = false;
    g_build_menu_selection = 0;
}

// Maximo de "degrau" que o player sobe sem pular.
// Mantido < 1.0 para evitar subir automaticamente em cubos (rochas/minerios/modulos).
static constexpr float kPlayerStepHeight = 0.60f;

// Altura adicional de um bloco acima do terreno (para colisao/ground height).
static float get_block_height(Block b) {
    if (b == Block::Air) return 0.0f;
    if (is_ground_like(b)) return 0.0f; // Solo (inclui agua/gelo), sem volume acima
    if (b == Block::Leaves) return 0.0f; // Folhagem e tratada como plano

    // Objetos (rochas/minerios/modulos/estruturas): cubo 1x1x1 sobre o solo.
    // Se quiser modulos mais altos no futuro, troque por um box/prisma (nao cubo uniforme).
    if (is_module(b)) return 1.0f;
    if (is_base_structure(b)) return 1.0f;
    if (is_solid(b)) return 1.0f;
    return 0.0f;
}

static Block surface_block_at(const World& world, int tx, int tz) {
    Block top = world.get(tx, tz);
    if (top != Block::Air && is_ground_like(top)) return top;
    return world.get_ground(tx, tz);
}

static Block object_block_at(const World& world, int tx, int tz) {
    Block top = world.get(tx, tz);
    if (top != Block::Air && !is_ground_like(top)) return top;
    return Block::Air;
}

static float surface_height_at(const World& world, int tx, int tz) {
    float h = (float)world.height_at(tx, tz) * kHeightScale;
    Block obj = object_block_at(world, tx, tz);
    if (obj != Block::Air) h += get_block_height(obj);
    return h;
}

static bool is_mineable(Block b) {
    if (b == Block::Air || b == Block::Water) return false;
    if (is_base_structure(b)) return false;
    return true;
}

static float block_hardness(Block b) {
    // Valores aproximados (segundos para quebrar, com base_speed = 1.0).
    switch (b) {
        case Block::Grass:
        case Block::Dirt:
        case Block::Sand:
        case Block::Snow:
        case Block::Leaves:
        case Block::Organic:
            return 0.55f;
        case Block::Ice:
            return 0.75f;
        case Block::Wood:
            return 0.95f;
        case Block::Stone:
            return 1.55f;
        case Block::Coal:
        case Block::Iron:
        case Block::Copper:
            return 1.75f;
        case Block::Crystal:
            return 2.10f;
        case Block::Metal:
        case Block::Components:
            return 1.90f;
        default:
            break;
    }
    if (is_module(b)) return 2.25f;
    return 1.35f;
}

// Colisao 3D - considera altura do jogador para permitir pular sobre blocos
static void resolve_player_collisions_3d(Player& p, const World& world, float dx, float dy) {
    const float eps = 1e-4f;
    
    // Funcao para verificar se bloco bloqueia na altura atual do jogador
    // Retorna true se o bloco impede a passagem do jogador
    auto blocks_at_height = [&](int tx, int ty) -> bool {
        if (!world.in_bounds(tx, ty)) return true;  // Fora do mapa = bloqueado

        // Topo solido do tile = altura do terreno + altura do objeto (se existir)
        float tile_top = surface_height_at(world, tx, ty);
        float player_bottom = p.pos_y; // Base dos pes

        // No chao, permitir entrar em tiles um pouco mais altos (step climbing)
        float clearance = p.on_ground ? kPlayerStepHeight : 0.10f;
        return tile_top > player_bottom + clearance;
    };

    // Colisao horizontal (X no mundo)
    if (dx != 0.0f) {
        float left = p.pos.x - p.w * 0.5f;
        float right = p.pos.x + p.w * 0.5f;
        float top = p.pos.y - p.h * 0.5f;
        float bottom = p.pos.y + p.h * 0.5f;

        int x0 = (int)std::floor(left);
        int x1 = (int)std::floor(right - eps);
        int y0 = (int)std::floor(top);
        int y1 = (int)std::floor(bottom - eps);

        if (dx > 0.0f) {
            float new_right = right;
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    if (!blocks_at_height(x, y)) continue;
                    new_right = std::min(new_right, (float)x);
                }
            }
            if (new_right < right) {
                p.pos.x = new_right - p.w * 0.5f - eps;
                p.vel.x = 0.0f;
            }
        } else {
            float new_left = left;
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    if (!blocks_at_height(x, y)) continue;
                    new_left = std::max(new_left, (float)(x + 1));
                }
            }
            if (new_left > left) {
                p.pos.x = new_left + p.w * 0.5f + eps;
                p.vel.x = 0.0f;
            }
        }
    }

    // Colisao vertical (Z no mundo 3D, Y no sistema 2D)
    if (dy != 0.0f) {
        float left = p.pos.x - p.w * 0.5f;
        float right = p.pos.x + p.w * 0.5f;
        float top = p.pos.y - p.h * 0.5f;
        float bottom = p.pos.y + p.h * 0.5f;

        int x0 = (int)std::floor(left);
        int x1 = (int)std::floor(right - eps);
        int y0 = (int)std::floor(top);
        int y1 = (int)std::floor(bottom - eps);

        if (dy > 0.0f) {
            float new_bottom = bottom;
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    if (!blocks_at_height(x, y)) continue;
                    new_bottom = std::min(new_bottom, (float)y);
                }
            }
            if (new_bottom < bottom) {
                p.pos.y = new_bottom - p.h * 0.5f - eps;
                p.vel.y = 0.0f;
            }
        } else {
            float new_top = top;
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    if (!blocks_at_height(x, y)) continue;
                    new_top = std::max(new_top, (float)(y + 1));
                }
            }
            if (new_top > top) {
                p.pos.y = new_top + p.h * 0.5f + eps;
                p.vel.y = 0.0f;
            }
        }
    }

    // Limitar posicao ao mundo
    p.pos.x = std::clamp(p.pos.x, 1.0f, (float)world.w - 2.0f);
    p.pos.y = std::clamp(p.pos.y, 1.0f, (float)world.h - 2.0f);
}

// ============= Crafting =============
// ============================================================================
// MODULE & RESOURCE SYSTEM - Complete Gameplay Loop
// ============================================================================

struct CraftCost {
    int stone = 0;
    int iron = 0;
    int coal = 0;
    int wood = 0;
    int copper = 0;
    int ice = 0;
    int crystal = 0;
    int metal = 0;
    int organic = 0;
    int components = 0;
};

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

// Construction in progress
// Note: ConstructionJob, Alert, g_construction_queue, g_alerts, g_base_integrity
// are declared earlier in the file with forward declarations

static constexpr float kBaseIntegrityMax = 100.0f;
static constexpr float kBaseIntegrityDecayRate = 0.5f;  // Per minute without workshop

// Cooldown para evitar spam de alertas
static std::unordered_map<std::string, float> g_alert_cooldowns;

static void add_alert(const std::string& msg, float r, float g, float b, float duration = 3.0f, float cooldown = 5.0f) {
    // Check cooldown
    auto it = g_alert_cooldowns.find(msg);
    if (it != g_alert_cooldowns.end() && it->second > 0.0f) {
        return;  // Still on cooldown
    }
    
    // Don't duplicate alerts
    for (auto& a : g_alerts) {
        if (a.message == msg) {
            a.time_remaining = duration;
            return;
        }
    }
    g_alerts.push_back({msg, r, g, b, duration});
    g_alert_cooldowns[msg] = cooldown;
}

// Get module statistics
static ModuleStats get_module_stats(Block b) {
    ModuleStats s{};
    switch (b) {
        case Block::SolarPanel:
            s.name = "Painel Solar";
            s.description = "Gera energia basica";
            s.energy_production = 3.0f;
            s.construction_time = 15.0f;
            break;
        case Block::EnergyGenerator:
            s.name = "Gerador de Energia";
            s.description = "Fonte principal de energia";
            s.energy_production = 8.0f;
            s.energy_consumption = 0.0f;
            s.construction_time = 45.0f;
            break;
        case Block::OxygenGenerator:
            s.name = "Gerador de Oxigenio";
            s.description = "Produz O2 para a base";
            s.oxygen_production = 2.0f;
            s.energy_consumption = 1.0f;
            s.construction_time = 30.0f;
            break;
        case Block::WaterExtractor:
            s.name = "Purificador de Agua";
            s.description = "Extrai e purifica agua";
            s.water_production = 1.5f;
            s.energy_consumption = 0.8f;
            s.construction_time = 25.0f;
            break;
        case Block::Greenhouse:
            s.name = "Estufa";
            s.description = "Produz comida";
            s.food_production = 1.0f;
            s.energy_consumption = 0.5f;
            s.construction_time = 40.0f;
            break;
        case Block::Workshop:
            s.name = "Oficina";
            s.description = "Repara a base";
            s.integrity_bonus = 2.0f;
            s.energy_consumption = 1.5f;
            s.construction_time = 60.0f;
            break;
        case Block::CO2Factory:
            s.name = "Fabrica de CO2";
            s.description = "Aquece o planeta";
            s.co2_production = 0.5f;
            s.energy_consumption = 2.0f;
            s.construction_time = 50.0f;
            break;
        case Block::Habitat:
            s.name = "Habitat";
            s.description = "Moradia extra";
            s.energy_consumption = 0.3f;
            s.construction_time = 90.0f;
            break;
        case Block::TerraformerBeacon:
            s.name = "Terraformador";
            s.description = "Terraformacao avancada";
            s.energy_consumption = 5.0f;
            s.construction_time = 120.0f;
            break;
        default:
            s.name = "Unknown";
            s.description = "";
            break;
    }
    return s;
}

// Module build costs (updated with new resources)
static CraftCost get_module_cost(Block b) {
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
            c.organic = 40; c.iron = 25; c.ice = 25; 
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

// Forward declaration - defined later in file
static bool can_afford(const CraftCost& c);

// Forward declaration - defined later in file
static void spend_cost(const CraftCost& c);

static std::string module_cost_string(const CraftCost& c) {
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

// Get module status for display
static ModuleStatus get_module_status(Block b) {
    // Check if under construction
    for (const auto& job : g_construction_queue) {
        if (job.active && job.module_type == b) {
            return ModuleStatus::Building;
        }
    }
    
    // Check if we have resources
    CraftCost cost = get_module_cost(b);
    if (!can_afford(cost)) {
        return ModuleStatus::Blocked;
    }
    
    return ModuleStatus::Available;
}

static const char* status_string(ModuleStatus s) {
    switch (s) {
        case ModuleStatus::Available: return "DISPONIVEL";
        case ModuleStatus::Blocked: return "BLOQUEADO";
        case ModuleStatus::Building: return "CONSTRUINDO";
        case ModuleStatus::Active: return "ATIVO";
        case ModuleStatus::NoPower: return "SEM ENERGIA";
        case ModuleStatus::Damaged: return "DANIFICADO";
        default: return "???";
    }
}

// Start construction of a module
static bool start_construction(Block module_type, int slot_index) {
    CraftCost cost = get_module_cost(module_type);
    if (!can_afford(cost)) {
        add_alert("Recursos insuficientes!", 1.0f, 0.3f, 0.3f);
        return false;
    }
    
    spend_cost(cost);
    
    ModuleStats stats = get_module_stats(module_type);
    ConstructionJob job;
    job.module_type = module_type;
    job.slot_index = slot_index;
    job.time_remaining = stats.construction_time;
    job.total_time = stats.construction_time;
    job.active = true;
    g_construction_queue.push_back(job);
    
    add_alert("Construcao iniciada: " + std::string(stats.name), 0.3f, 1.0f, 0.5f);
    return true;
}

// Legacy unlock requirements (for backward compatibility)
struct UnlockRequirement {
    int stone = 0;
    int iron = 0;
    int coal = 0;
    int copper = 0;
    int wood = 0;
};

static UnlockRequirement get_unlock_requirement(Block b) {
    UnlockRequirement r{};
    // Progressive unlock requirements - collect resources to unlock modules
    switch (b) {
        case Block::SolarPanel:       r.iron = 0; break;  // Already unlocked
        case Block::WaterExtractor:   r.stone = 5; break; // Colete pedra
        case Block::OxygenGenerator:  r.iron = 5; break;  // Colete ferro
        case Block::Greenhouse:       r.stone = 10; r.iron = 5; break;  // Recursos variados
        case Block::CO2Factory:       r.iron = 10; r.coal = 5; break;   // Precisa carvao
        case Block::Habitat:          r.iron = 15; r.stone = 15; break; // Mais avancado
        case Block::TerraformerBeacon: r.iron = 25; r.copper = 10; break; // Final
        default: break;
    }
    return r;
}

static bool is_unlocked(Block b) {
    switch (b) {
        case Block::SolarPanel:       return g_unlocks.solar_unlocked;
        case Block::WaterExtractor:   return g_unlocks.water_extractor_unlocked;
        case Block::OxygenGenerator:  return g_unlocks.o2_generator_unlocked;
        case Block::Greenhouse:       return g_unlocks.greenhouse_unlocked;
        case Block::CO2Factory:       return g_unlocks.co2_factory_unlocked;
        case Block::Habitat:          return g_unlocks.habitat_unlocked;
        case Block::TerraformerBeacon: return g_unlocks.terraformer_unlocked;
        default: return true; // Non-modules are always available
    }
}

static void check_unlocks() {
    // Check and unlock modules based on total collected resources
    auto check = [](bool& flag, const UnlockRequirement& r) {
        if (flag) return;
        if (g_unlocks.total_stone >= r.stone &&
            g_unlocks.total_iron >= r.iron &&
            g_unlocks.total_coal >= r.coal &&
            g_unlocks.total_copper >= r.copper &&
            g_unlocks.total_wood >= r.wood) {
            flag = true;
        }
    };
    
    check(g_unlocks.solar_unlocked, get_unlock_requirement(Block::SolarPanel));
    check(g_unlocks.water_extractor_unlocked, get_unlock_requirement(Block::WaterExtractor));
    check(g_unlocks.o2_generator_unlocked, get_unlock_requirement(Block::OxygenGenerator));
    check(g_unlocks.greenhouse_unlocked, get_unlock_requirement(Block::Greenhouse));
    check(g_unlocks.co2_factory_unlocked, get_unlock_requirement(Block::CO2Factory));
    check(g_unlocks.habitat_unlocked, get_unlock_requirement(Block::Habitat));
    
    // Terraformer only unlocks after all survival modules are built
    if (!g_unlocks.terraformer_unlocked) {
        bool has_survival = false;
        for (const auto& m : g_modules) {
            if (m.type == Block::Habitat) has_survival = true;
        }
        // Need habitat + basic modules unlocked
        if (has_survival && g_unlocks.habitat_unlocked && 
            g_unlocks.o2_generator_unlocked && g_unlocks.greenhouse_unlocked) {
            UnlockRequirement r = get_unlock_requirement(Block::TerraformerBeacon);
            if (g_unlocks.total_stone >= r.stone &&
                g_unlocks.total_iron >= r.iron &&
                g_unlocks.total_coal >= r.coal &&
                g_unlocks.total_copper >= r.copper) {
                g_unlocks.terraformer_unlocked = true;
            }
        }
    }
}

static std::string unlock_progress_string(Block b) {
    UnlockRequirement r = get_unlock_requirement(b);
    std::string s;
    auto add = [&](const char* name, int have, int need) {
        if (need <= 0) return;
        if (!s.empty()) s += " ";
        s += name;
        s += std::to_string(have) + "/" + std::to_string(need);
    };
    add("St", g_unlocks.total_stone, r.stone);
    add("Fe", g_unlocks.total_iron, r.iron);
    add("C", g_unlocks.total_coal, r.coal);
    add("Cu", g_unlocks.total_copper, r.copper);
    add("W", g_unlocks.total_wood, r.wood);
    return s;
}

static CraftCost module_cost(Block b) {
    CraftCost c{};
    switch (b) {
        case Block::SolarPanel:       c.iron = 3; c.stone = 2; break;
        case Block::WaterExtractor:   c.iron = 4; c.stone = 4; c.copper = 2; break;
        case Block::OxygenGenerator:  c.iron = 5; c.coal = 3; c.copper = 2; break;
        case Block::Greenhouse:       c.iron = 6; c.wood = 4; c.copper = 3; c.stone = 4; break;
        case Block::CO2Factory:       c.iron = 8; c.coal = 6; c.copper = 4; c.stone = 6; break;
        case Block::Habitat:          c.iron = 10; c.stone = 12; c.copper = 6; c.wood = 4; break;
        case Block::TerraformerBeacon: c.iron = 15; c.coal = 10; c.copper = 10; c.stone = 10; break;
        default: break;
    }
    return c;
}

static bool can_afford(const CraftCost& c) {
    return g_inventory[(int)Block::Stone] >= c.stone &&
           g_inventory[(int)Block::Iron] >= c.iron &&
           g_inventory[(int)Block::Coal] >= c.coal &&
           g_inventory[(int)Block::Wood] >= c.wood &&
           g_inventory[(int)Block::Copper] >= c.copper &&
           g_inventory[(int)Block::Ice] >= c.ice &&
           g_inventory[(int)Block::Crystal] >= c.crystal &&
           g_inventory[(int)Block::Metal] >= c.metal &&
           g_inventory[(int)Block::Organic] >= c.organic &&
           g_inventory[(int)Block::Components] >= c.components;
}

static void spend_cost(const CraftCost& c) {
    g_inventory[(int)Block::Stone] -= c.stone;
    g_inventory[(int)Block::Iron] -= c.iron;
    g_inventory[(int)Block::Coal] -= c.coal;
    g_inventory[(int)Block::Wood] -= c.wood;
    g_inventory[(int)Block::Copper] -= c.copper;
    g_inventory[(int)Block::Ice] -= c.ice;
    g_inventory[(int)Block::Crystal] -= c.crystal;
    g_inventory[(int)Block::Metal] -= c.metal;
    g_inventory[(int)Block::Organic] -= c.organic;
    g_inventory[(int)Block::Components] -= c.components;
}

static void refund_cost(const CraftCost& c) {
    g_inventory[(int)Block::Stone] += c.stone;
    g_inventory[(int)Block::Iron] += c.iron;
    g_inventory[(int)Block::Coal] += c.coal;
    g_inventory[(int)Block::Wood] += c.wood;
    g_inventory[(int)Block::Copper] += c.copper;
    g_inventory[(int)Block::Ice] += c.ice;
    g_inventory[(int)Block::Crystal] += c.crystal;
    g_inventory[(int)Block::Metal] += c.metal;
    g_inventory[(int)Block::Organic] += c.organic;
    g_inventory[(int)Block::Components] += c.components;
}

static std::string cost_string(const CraftCost& c) {
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

// ============= Effects =============
static void spawn_block_particles(Block b, float cx, float cy, int world_h) {
    float r, g, bl, a;
    block_color(b, (int)cy, world_h, r, g, bl, a);
    for (int i = 0; i < 12; ++i) {
        float ang = rng_next_f01() * 6.2831853f;
        float spd = 2.0f + rng_next_f01() * 4.5f;
        Particle p;
        p.pos = {cx + (rng_next_f01() - 0.5f) * 0.15f, cy + (rng_next_f01() - 0.5f) * 0.15f};
        p.vel = {std::cos(ang) * spd, std::sin(ang) * spd - 2.0f};
        p.life = 0.55f + rng_next_f01() * 0.35f;
        p.r = r;
        p.g = g;
        p.b = bl;
        p.a = 1.0f;
        g_particles.push_back(p);
    }
}

static Block drop_item_for_block(Block broken) {
    // Simplifica drops para itens realmente uteis no prototipo.
    switch (broken) {
        case Block::Grass:  return Block::Dirt;
        case Block::Leaves: return Block::Organic;
        case Block::Sand:   return Block::Dirt;
        case Block::Snow:   return Block::Ice;
        default:            return broken;
    }
}

static float drop_spawn_y_for_block(Block broken) {
    if (broken == Block::Leaves) return 0.70f;
    if (is_module(broken)) return 1.15f;
    if (is_solid(broken)) return 0.95f;
    return 0.35f;
}

static void spawn_item_drop(Block item, float x, float z, float spawn_y) {
    ItemDrop d;
    d.item = item;
    d.x = x;
    d.z = z;
    d.y = spawn_y;
    d.vy = 2.8f + rng_next_f01() * 1.2f;
    d.t = rng_next_f01() * 10.0f;
    d.pickup_delay = 0.12f;
    g_drops.push_back(d);

    // Limit simples para evitar crescimento infinito em casos extremos
    if (g_drops.size() > 500u) {
        g_drops.erase(g_drops.begin(), g_drops.begin() + 100);
    }
}

static void on_pickup_item(Block item, float x, float z) {
    g_inventory[(int)item]++;

    // Bonus de sobrevivencia (reforça onboarding: gelo -> agua)
    if (item == Block::Ice) {
        g_player_water = std::min(100.0f, g_player_water + 25.0f);
    } else if (item == Block::Organic) {
        g_player_food = std::min(100.0f, g_player_food + 8.0f);
    }

    // Unlock tracking: total "coletado" (agora no pickup, nao no break)
    switch (item) {
        case Block::Stone: g_unlocks.total_stone++; break;
        case Block::Iron:  g_unlocks.total_iron++; break;
        case Block::Coal:  g_unlocks.total_coal++; break;
        case Block::Copper: g_unlocks.total_copper++; break;
        case Block::Wood:  g_unlocks.total_wood++; break;
        default: break;
    }

    // Popup leve de coleta (feedback no HUD, estilo Minicraft)
    {
        float cr, cg, cb, ca;
        block_color(item, (int)std::floor(g_player.pos.y), g_world->h, cr, cg, cb, ca);
        float jitter_x = (rng_next_f01() - 0.5f) * 90.0f;

        std::string txt = "+1 ";
        txt += block_name(item);
        if (item == Block::Ice) txt += " (+25 Agua)";
        else if (item == Block::Organic) txt += " (+8 Comida)";

        add_collect_popup(jitter_x, 0.0f, txt, cr, cg, cb, item, 1);
    }
    (void)x; (void)z;

    if (!g_onboarding.shown_first_collect) {
        show_tip("Tab para abrir menu de construcao", g_onboarding.shown_first_collect);
    }

    bool had_solar = g_unlocks.solar_unlocked;
    bool had_water = g_unlocks.water_extractor_unlocked;
    bool had_o2 = g_unlocks.o2_generator_unlocked;
    bool had_greenhouse = g_unlocks.greenhouse_unlocked;
    bool had_co2 = g_unlocks.co2_factory_unlocked;
    bool had_habitat = g_unlocks.habitat_unlocked;
    bool had_terraform = g_unlocks.terraformer_unlocked;

    check_unlocks();

    if (!had_solar && g_unlocks.solar_unlocked)
        show_unlock_popup("DESBLOQUEADO!", "Painel Solar - Tab para construir");
    if (!had_water && g_unlocks.water_extractor_unlocked)
        show_unlock_popup("DESBLOQUEADO!", "Extrator de Agua - Tab para construir");
    if (!had_o2 && g_unlocks.o2_generator_unlocked)
        show_unlock_popup("DESBLOQUEADO!", "Gerador de O2 - Tab para construir");
    if (!had_greenhouse && g_unlocks.greenhouse_unlocked)
        show_unlock_popup("DESBLOQUEADO!", "Estufa - Tab para construir");
    if (!had_co2 && g_unlocks.co2_factory_unlocked)
        show_unlock_popup("DESBLOQUEADO!", "Fabrica de CO2 - Comece a aquecer!");
    if (!had_habitat && g_unlocks.habitat_unlocked)
        show_unlock_popup("DESBLOQUEADO!", "Habitat - Lar doce lar");
    if (!had_terraform && g_unlocks.terraformer_unlocked)
        show_unlock_popup("DESBLOQUEADO!", "Terraformador - Transforme o planeta!");
}

static void update_item_drops(float dt) {
    static constexpr float kRestY = 0.22f;
    static constexpr float kGravity = 9.5f;

    for (auto& d : g_drops) {
        d.t += dt;
        d.pickup_delay -= dt;

        // Fisica simples (queda/bounce)
        d.vy -= kGravity * dt;
        d.y += d.vy * dt;
        if (d.y < kRestY) {
            d.y = kRestY;
            if (std::fabs(d.vy) < 0.8f) d.vy = 0.0f;
            else d.vy = -d.vy * 0.28f;
        }
    }

    // Coleta por proximidade
    for (size_t i = 0; i < g_drops.size();) {
        ItemDrop& d = g_drops[i];
        if (d.pickup_delay <= 0.0f) {
            float dx = d.x - g_player.pos.x;
            float dz = d.z - g_player.pos.y;
            float dist2 = dx * dx + dz * dz;
            if (dist2 <= (0.75f * 0.75f)) {
                on_pickup_item(d.item, d.x, d.z);
                g_drops[i] = g_drops.back();
                g_drops.pop_back();
                continue;
            }
        }
        ++i;
    }
}

// ============= OpenGL Setup =============
static HGLRC setup_opengl(HDC hdc) {
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int format = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, format, &pfd);

    HGLRC hrc = wglCreateContext(hdc);
    wglMakeCurrent(hdc, hrc);

    glDisable(GL_DEPTH_TEST);
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Atlas pixel-art (Minicraft/Minecraft-like)
    init_texture_atlas();
    glBindTexture(GL_TEXTURE_2D, 0);
    return hrc;
}

// ============= Rendering Helpers =============
static void render_quad(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

// Quad 2D texturizado (tile do atlas). Requer GL_TEXTURE_2D habilitado e g_tex_atlas bindado.
static void render_quad_tex(float x, float y, float w, float h, Tile tile, float tint_r, float tint_g, float tint_b, float a = 1.0f) {
    UvRect uv = atlas_uv(tile);
    glColor4f(tint_r, tint_g, tint_b, a);
    glBegin(GL_QUADS);
    glTexCoord2f(uv.u0, uv.v1); glVertex2f(x, y);
    glTexCoord2f(uv.u1, uv.v1); glVertex2f(x + w, y);
    glTexCoord2f(uv.u1, uv.v0); glVertex2f(x + w, y + h);
    glTexCoord2f(uv.u0, uv.v0); glVertex2f(x, y + h);
    glEnd();
}

static void render_bar(float x, float y, float w, float h, float pct, float r, float g, float b) {
    render_quad(x, y, w, h, 0.0f, 0.0f, 0.0f, 0.55f);
    render_quad(x + 2.0f, y + 2.0f, (w - 4.0f) * clamp01(pct), h - 4.0f, r, g, b, 0.92f);
}

// ============= Astronaut Rendering =============
static void render_circle(float cx, float cy, float radius, float r, float g, float b, float a, int segments = 16) {
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= segments; ++i) {
        float angle = (float)i / (float)segments * 2.0f * kPi;
        glVertex2f(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius);
    }
    glEnd();
}

static void render_ellipse(float cx, float cy, float rx, float ry, float r, float g, float b, float a, int segments = 16) {
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= segments; ++i) {
        float angle = (float)i / (float)segments * 2.0f * kPi;
        glVertex2f(cx + std::cos(angle) * rx, cy + std::sin(angle) * ry);
    }
    glEnd();
}

static void render_rounded_rect(float x, float y, float w, float h, float radius, float r, float g, float b, float a) {
    // Simple rounded rectangle approximation
    render_quad(x + radius, y, w - 2*radius, h, r, g, b, a);
    render_quad(x, y + radius, w, h - 2*radius, r, g, b, a);
    render_circle(x + radius, y + radius, radius, r, g, b, a, 8);
    render_circle(x + w - radius, y + radius, radius, r, g, b, a, 8);
    render_circle(x + radius, y + h - radius, radius, r, g, b, a, 8);
    render_circle(x + w - radius, y + h - radius, radius, r, g, b, a, 8);
}

// ============= TOP-DOWN PLAYER RENDERING =============
// Astronauta visto de cima com 4 direcoes
static void render_player_topdown(float px, float py, float scale, const Player& player) {
    // Cores do traje - MAIS CONTRASTANTES
    const float suit_r = 1.0f, suit_g = 0.95f, suit_b = 0.90f;    // Traje branco brilhante
    const float visor_r = 0.10f, visor_g = 0.50f, visor_b = 0.90f; // Visor azul vivo
    const float pack_r = 0.35f, pack_g = 0.38f, pack_b = 0.42f;    // Mochila cinza escuro
    const float gold_r = 1.0f, gold_g = 0.70f, gold_b = 0.15f;     // Detalhes dourados vivos
    const float boot_r = 0.20f, boot_g = 0.22f, boot_b = 0.25f;    // Botas escuras
    const float outline_r = 0.0f, outline_g = 0.0f, outline_b = 0.0f; // Outline preto
    
    // TAMANHO AUMENTADO: era 14, agora 24
    float size = 24.0f * scale;
    float outline_w = 2.0f * scale; // Largura do outline
    
    // Animacao de caminhada
    float walk_offset = 0.0f;
    float leg_anim = 0.0f;
    if (player.is_moving) {
        walk_offset = std::sin(player.walk_timer * 10.0f) * 1.5f * scale;
        leg_anim = std::sin(player.walk_timer * 12.0f) * 4.0f * scale;
    }
    
    // Offset de direcao para elementos (mochila, visor)
    float dir_x = 0.0f, dir_y = 0.0f;
    switch (player.facing_dir) {
        case 0: dir_y = -1.0f; break;  // Norte
        case 1: dir_x = 1.0f;  break;  // Leste
        case 2: dir_y = 1.0f;  break;  // Sul
        case 3: dir_x = -1.0f; break;  // Oeste
    }
    
    float center_x = px;
    float center_y = py;
    
    // === SOMBRA GRANDE E VISIVEL ===
    render_ellipse(center_x + 3.0f * scale, center_y + 6.0f * scale, 
                   size * 0.55f, size * 0.30f, 0.0f, 0.0f, 0.0f, 0.5f);
    
    // === MOCHILA (atras do jogador) ===
    float pack_offset = 7.0f * scale;
    float pack_x = center_x - dir_x * pack_offset;
    float pack_y = center_y - dir_y * pack_offset;
    
    // Outline da mochila
    render_circle(pack_x, pack_y, size * 0.32f + outline_w, outline_r, outline_g, outline_b, 1.0f);
    // Mochila
    render_circle(pack_x, pack_y, size * 0.32f, pack_r, pack_g, pack_b, 1.0f);
    // Tanques de oxigenio na mochila
    render_ellipse(pack_x - 3.0f * scale, pack_y, 2.5f * scale, 4.0f * scale, 0.50f, 0.55f, 0.60f, 1.0f);
    render_ellipse(pack_x + 3.0f * scale, pack_y, 2.5f * scale, 4.0f * scale, 0.50f, 0.55f, 0.60f, 1.0f);
    
    // === PERNAS (animadas) ===
    float leg_offset = 5.0f * scale;
    float leg_size = 4.0f * scale;
    
    // Perna esquerda
    float left_leg_x = center_x;
    float left_leg_y = center_y;
    if (player.facing_dir == 0 || player.facing_dir == 2) {
        left_leg_x -= leg_offset;
        left_leg_y += (player.facing_dir == 0 ? 1 : -1) * leg_anim * 0.3f;
    } else {
        left_leg_y -= leg_offset;
        left_leg_x += (player.facing_dir == 1 ? -1 : 1) * leg_anim * 0.3f;
    }
    // Outline perna esquerda
    render_circle(left_leg_x, left_leg_y, leg_size + outline_w, outline_r, outline_g, outline_b, 1.0f);
    render_circle(left_leg_x, left_leg_y, leg_size, boot_r, boot_g, boot_b, 1.0f);
    
    // Perna direita
    float right_leg_x = center_x;
    float right_leg_y = center_y;
    if (player.facing_dir == 0 || player.facing_dir == 2) {
        right_leg_x += leg_offset;
        right_leg_y -= (player.facing_dir == 0 ? 1 : -1) * leg_anim * 0.3f;
    } else {
        right_leg_y += leg_offset;
        right_leg_x -= (player.facing_dir == 1 ? -1 : 1) * leg_anim * 0.3f;
    }
    // Outline perna direita
    render_circle(right_leg_x, right_leg_y, leg_size + outline_w, outline_r, outline_g, outline_b, 1.0f);
    render_circle(right_leg_x, right_leg_y, leg_size, boot_r, boot_g, boot_b, 1.0f);
    
    // === CORPO (circulo principal) ===
    // Outline do corpo
    render_circle(center_x, center_y + walk_offset, size * 0.5f + outline_w, outline_r, outline_g, outline_b, 1.0f);
    render_circle(center_x, center_y + walk_offset, size * 0.5f, suit_r, suit_g, suit_b, 1.0f);
    
    // Detalhe do traje (faixa dourada)
    if (player.facing_dir == 0 || player.facing_dir == 2) {
        render_quad(center_x - size * 0.4f, center_y + walk_offset - 1.5f * scale, 
                   size * 0.8f, 3.0f * scale, gold_r, gold_g * 0.8f, 0.2f, 0.9f);
    } else {
        render_quad(center_x - 1.5f * scale, center_y + walk_offset - size * 0.4f, 
                   3.0f * scale, size * 0.8f, gold_r, gold_g * 0.8f, 0.2f, 0.9f);
    }
    
    // === CAPACETE (cabeca) ===
    float head_offset = size * 0.18f;
    float head_x = center_x + dir_x * head_offset;
    float head_y = center_y + walk_offset + dir_y * head_offset;
    
    // Outline do capacete
    render_circle(head_x, head_y, size * 0.40f + outline_w, outline_r, outline_g, outline_b, 1.0f);
    // Capacete branco
    render_circle(head_x, head_y, size * 0.40f, suit_r, suit_g, suit_b, 1.0f);
    
    // Borda dourada do capacete
    render_circle(head_x, head_y, size * 0.42f, gold_r, gold_g, gold_b, 0.4f);
    
    // === VISOR (indica direcao) ===
    float visor_dist = size * 0.25f;
    float visor_x = head_x + dir_x * visor_dist;
    float visor_y = head_y + dir_y * visor_dist;
    
    // Outline do visor
    render_circle(visor_x, visor_y, size * 0.20f + outline_w * 0.5f, outline_r, outline_g, outline_b, 1.0f);
    // Visor azul reflexivo
    render_circle(visor_x, visor_y, size * 0.20f, visor_r, visor_g, visor_b, 1.0f);
    
    // Reflexo no visor
    float ref_intensity = 0.5f + 0.2f * std::sin(player.anim_frame * 0.8f);
    render_circle(visor_x - 2.0f * scale * (1.0f - std::fabs(dir_x)), 
                 visor_y - 2.0f * scale * (1.0f - std::fabs(dir_y)), 
                 size * 0.08f, 1.0f, 1.0f, 1.0f, ref_intensity);
    
    // === ANTENA ===
    float antenna_x = head_x - dir_x * size * 0.28f + (dir_y != 0 ? 4.0f * scale : 0);
    float antenna_y = head_y - dir_y * size * 0.28f + (dir_x != 0 ? -4.0f * scale : 0);
    render_circle(antenna_x, antenna_y, 2.5f * scale, 0.3f, 0.32f, 0.35f, 1.0f);
    // Luz da antena (pisca)
    float blink = (std::sin(player.anim_frame * 4.0f) > 0.0f) ? 1.0f : 0.3f;
    render_circle(antenna_x, antenna_y, 1.5f * scale, 1.0f * blink, 0.2f * blink, 0.2f * blink, 1.0f);
    
    // === FERRAMENTA (quando minerando) ===
    if (player.is_mining) {
        float tool_dist = size * 0.7f;
        float tool_x = center_x + dir_x * tool_dist;
        float tool_y = center_y + dir_y * tool_dist;
        float mine_swing = std::sin(player.mine_anim * 15.0f) * 4.0f * scale;
        
        // Picareta - outline
        render_quad(tool_x - 2.0f * scale, tool_y - 2.0f * scale + mine_swing, 
                   4.0f * scale, 12.0f * scale, 0.0f, 0.0f, 0.0f, 1.0f);
        render_quad(tool_x - 1.5f * scale, tool_y - 1.5f * scale + mine_swing, 
                   3.0f * scale, 10.0f * scale, 0.55f, 0.35f, 0.2f, 1.0f);
        render_quad(tool_x - 5.0f * scale, tool_y - 3.0f * scale + mine_swing, 
                   10.0f * scale, 3.0f * scale, 0.5f, 0.5f, 0.55f, 1.0f);
    }
    
    // === LUZ DE STATUS ===
    float status_x = center_x + (dir_x == 0 ? 5.0f * scale : 0);
    float status_y = center_y + walk_offset + (dir_y == 0 ? -5.0f * scale : 0);
    float status_blink = (std::sin(player.anim_frame * 3.0f) > 0.0f) ? 1.0f : 0.5f;
    render_circle(status_x, status_y, 2.0f * scale, 0.2f * status_blink, 1.0f * status_blink, 0.3f * status_blink, 1.0f);
}

// Wrapper para compatibilidade (ignora in_water em top-down)
static void render_astronaut(float px, float py, float scale, const Player& player, bool /*in_water*/) {
    render_player_topdown(px, py, scale, player);
}

// ============= Terraforming Simulation =============
static void try_spawn_tree(World& world, int x, int y) {
    // Planeta inospito no comeco: so gera vegetacao em fase habitavel/terraformed.
    if (g_phase < TerraPhase::Habitable) return;
    if (x < 2 || x >= world.w - 2 || y < 2 || y >= world.h - 2) return;

    // Apenas em grama e sem objetos/estruturas/modulos.
    if (world.get_ground(x, y) != Block::Grass) return;
    if (is_base_structure(world.get_ground(x, y))) return;
    if (object_block_at(world, x, y) != Block::Air) return;

    // Evitar encostar em modulos/base (mantem legibilidade e evita conflito com construcoes).
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            Block t = world.get(x + dx, y + dy);
            if (is_module(t) || is_base_structure(t)) return;
        }
    }

    // Arvore simples estilo Minicraft: tronco (cubo) + copa (folhas em volta).
    world.set(x, y, Block::Wood);
    for (int oy = -2; oy <= 2; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            if (std::abs(ox) + std::abs(oy) > 3) continue;
            int tx = x + ox;
            int ty = y + oy;
            if (!world.in_bounds(tx, ty)) continue;
            if (tx == x && ty == y) continue;

            Block cur = world.get(tx, ty);
            if (is_module(cur) || is_base_structure(cur)) continue;
            if (object_block_at(world, tx, ty) != Block::Air) continue;

            world.set(tx, ty, Block::Leaves);
        }
    }
}

static void terraform_step(World& world, int cx, int cy) {
    int radius = 10;
    for (int i = 0; i < 3; ++i) {
        float ang = rng_next_f01() * 6.2831853f;
        float rr = rng_next_f01() * radius;
        int x = cx + (int)std::round(std::cos(ang) * rr);
        int y = cy + (int)std::round(std::sin(ang) * rr);
        if (!world.in_bounds(x, y)) continue;

        Block top = world.get(x, y);
        if (is_module(top) || is_base_structure(top)) continue;
        if (is_base_structure(world.get_ground(x, y))) continue;

        auto set_ground_surface = [&](int tx, int ty, Block nb) {
            world.set_ground(tx, ty, nb);
            Block t = world.get(tx, ty);
            if (t != Block::Air && is_ground_like(t) && !is_base_structure(t) && !is_module(t)) {
                world.set(tx, ty, nb);
            }
        };

        Block g = world.get_ground(x, y);

        if (g == Block::Sand && g_oxygen >= 12.0f && g_water_res >= 12.0f) {
            set_ground_surface(x, y, Block::Dirt);
            g_surface_dirty = true;
        } else if (g == Block::Dirt && g_phase >= TerraPhase::Habitable &&
            g_oxygen >= 28.0f && g_water_res >= 18.0f) {
            set_ground_surface(x, y, Block::Grass);
            g_surface_dirty = true;
        } else if (g == Block::Grass && g_phase >= TerraPhase::Habitable &&
            g_oxygen >= 45.0f && g_water_res >= 35.0f) {
            if ((rng_next_u32() % 100u) < 2u) {
                try_spawn_tree(world, x, y);
                g_surface_dirty = true;
            }
        }
    }
}

static void recompute_terraform_score(World& world) {
    int grass_tiles = 0;
    int tree_tiles = 0;
    int water_tiles = 0;

    for (int y = 0; y < world.h; ++y) {
        for (int x = 0; x < world.w; ++x) {
            Block g = world.get_ground(x, y);
            if (g == Block::Grass) grass_tiles++;
            if (g == Block::Water) water_tiles++;

            Block obj = object_block_at(world, x, y);
            if (obj == Block::Wood) tree_tiles++;
        }
    }

    float total = (float)std::max(1, world.w * world.h);
    float grass = (float)grass_tiles / total;
    float trees = (float)tree_tiles / total;
    float water = (float)water_tiles / total;

    float base = grass * 60.0f + trees * 20.0f + water * 20.0f;
    float env = 0.4f + 0.6f * (0.5f * clamp01(g_oxygen / 100.0f) + 0.5f * clamp01(g_water_res / 100.0f));
    g_terraform = std::clamp(base * env, 0.0f, 100.0f);

    if (!g_victory && g_terraform >= 80.0f) {
        g_victory = true;
        set_toast("Vitoria! Terraformacao >= 80%");
    }
}

static void update_phase() {
    // Update terraforming phase based on temperature
    TerraPhase old_phase = g_phase;
    
    if (g_temperature >= kTempHabitable && g_atmosphere >= 60.0f) {
        g_phase = TerraPhase::Habitable;
    } else if (g_temperature >= kTempThawing) {
        g_phase = TerraPhase::Thawing;
    } else if (g_co2_level > 10.0f) {
        g_phase = TerraPhase::Warming;
    } else {
        g_phase = TerraPhase::Frozen;
    }
    
    // Check for victory
    if (!g_victory && g_temperature >= kTempTarget && g_atmosphere >= 80.0f && g_terraform >= 70.0f) {
        g_phase = TerraPhase::Terraformed;
        g_victory = true;
        set_toast("VITORIA! Planeta terraformado com sucesso!");
    }
    
    // Notify phase changes
    if (old_phase != g_phase && !g_victory) {
        set_toast(std::string("Fase: ") + phase_name(g_phase), 4.0f);
    }
}

static void melt_ice_around(World& world, int cx, int cy, int radius) {
    if (g_temperature < kTempThawing) return; // Too cold to melt
    
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx*dx + dy*dy > radius*radius) continue;
            int x = cx + dx;
            int y = cy + dy;
            if (!world.in_bounds(x, y)) continue;
            
            // Derreter gelo do SOLO (ground). O topo (tiles) pode estar Air/objeto.
            if (world.get_ground(x, y) == Block::Ice && !is_base_structure(world.get_ground(x, y))) {
                world.set_ground(x, y, Block::Water);
                Block t = world.get(x, y);
                if (t != Block::Air && is_ground_like(t) && !is_base_structure(t) && !is_module(t)) {
                    world.set(x, y, Block::Water);
                }
                g_surface_dirty = true;
            }
        }
    }
}

static void update_modules(World& world, float dt) {
    g_day_time += dt;

    float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
    float daylight = std::fmax(0.0f, std::sin(day_phase * kPi));
    
    // Update alerts timer
    for (auto it = g_alerts.begin(); it != g_alerts.end();) {
        it->time_remaining -= dt;
        if (it->time_remaining <= 0.0f) {
            it = g_alerts.erase(it);
        } else {
            ++it;
        }
    }
    
    // Update alert cooldowns
    for (auto& pair : g_alert_cooldowns) {
        if (pair.second > 0.0f) {
            pair.second -= dt;
        }
    }

    // ========== PROCESS CONSTRUCTION QUEUE ==========
    for (auto& job : g_construction_queue) {
        if (!job.active) continue;
        
        // Construction requires energy
        float energy_cost = 2.0f * dt;
        if (g_base_energy >= energy_cost) {
            g_base_energy -= energy_cost;
            job.time_remaining -= dt;
            
            if (job.time_remaining <= 0.0f) {
                // Construction complete!
                job.active = false;
                
                // Place the module
                if (job.slot_index >= 0 && job.slot_index < (int)g_build_slots.size()) {
                    BuildSlotInfo& slot = g_build_slots[job.slot_index];
                    slot.assigned_module = job.module_type;
                    world.set(slot.x, slot.y, job.module_type);
                    
                    Module mod;
                    mod.type = job.module_type;
                    mod.x = slot.x;
                    mod.y = slot.y;
                    mod.t = 0.0f;
                    g_modules.push_back(mod);
                }
                
                ModuleStats stats = get_module_stats(job.module_type);
                add_alert("Construido: " + std::string(stats.name), 0.3f, 1.0f, 0.5f, 4.0f);
            }
        } else {
            add_alert("Construcao parada - Sem energia!", 1.0f, 0.5f, 0.2f);
        }
    }
    
    // Clean up completed jobs
    g_construction_queue.erase(
        std::remove_if(g_construction_queue.begin(), g_construction_queue.end(),
            [](const ConstructionJob& j) { return !j.active; }),
        g_construction_queue.end());

    // ========== UPDATE MODULE STATUS ==========
    // Check energy and health for each module
    for (Module& m : g_modules) {
        // Degrade health slowly over time (0.5% per minute)
        float health_decay = 0.5f / 60.0f * dt;
        m.health = std::max(0.0f, m.health - health_decay);
        
        // Determine status
        if (m.health <= 0.0f) {
            m.status = ModuleStatus::Damaged;
        } else if (g_base_energy <= 0.0f && m.type != Block::SolarPanel && m.type != Block::EnergyGenerator) {
            m.status = ModuleStatus::NoPower;
        } else {
            m.status = ModuleStatus::Active;
        }
    }
    
    // Count ACTIVE modules (damaged modules don't produce)
    int solar_count = 0;
    int energy_gen_count = 0;
    int water_count = 0;
    int o2_count = 0;
    int greenhouse_count = 0;
    int workshop_count = 0;
    int co2_factory_count = 0;
    int habitat_count = 0;
    int beacon_count = 0;
    
    for (const Module& m : g_modules) {
        // Skip damaged modules
        if (m.status == ModuleStatus::Damaged) continue;
        
        switch (m.type) {
            case Block::SolarPanel: solar_count++; break;
            case Block::EnergyGenerator: energy_gen_count++; break;
            case Block::WaterExtractor: water_count++; break;
            case Block::OxygenGenerator: o2_count++; break;
            case Block::Greenhouse: greenhouse_count++; break;
            case Block::Workshop: workshop_count++; break;
            case Block::CO2Factory: co2_factory_count++; break;
            case Block::Habitat: habitat_count++; break;
            case Block::TerraformerBeacon: beacon_count++; break;
            default: break;
        }
    }

    // ========== BASE CONSTANT CONSUMPTION ==========
    // The base always consumes resources (per minute converted to per second)
    float base_o2_consumption = 1.0f / 60.0f * dt;    // -1 O2/min
    float base_energy_consumption = 2.0f / 60.0f * dt; // -2 Energy/min
    float base_water_consumption = 1.0f / 60.0f * dt;  // -1 Water/min
    
    g_base_oxygen = std::max(0.0f, g_base_oxygen - base_o2_consumption);
    g_base_energy = std::max(0.0f, g_base_energy - base_energy_consumption);
    g_base_water = std::max(0.0f, g_base_water - base_water_consumption);
    
    // ========== BASE INTEGRITY DECAY ==========
    // Without workshop, integrity slowly decays
    float integrity_decay = (kBaseIntegrityDecayRate / 60.0f) * dt;
    if (workshop_count == 0) {
        g_base_integrity = std::max(0.0f, g_base_integrity - integrity_decay);
    }

    // ========== SOLAR PANELS ==========
    // Generate energy for the BASE (rate per minute: +3/panel)
    float solar_efficiency = 0.7f + 0.3f * clamp01(g_atmosphere / 50.0f);
    float solar_rate = 3.0f / 60.0f;  // Per second
    float energy_produced = (float)solar_count * solar_rate * daylight * solar_efficiency * dt;
    g_base_energy = std::clamp(g_base_energy + energy_produced, 0.0f, kBaseEnergyMax);

    // ========== ENERGY GENERATORS ==========
    // Main power source (+8 energy/min)
    if (energy_gen_count > 0) {
        float gen_rate = 8.0f / 60.0f;  // Per second
        float gen_produced = (float)energy_gen_count * gen_rate * dt;
        g_base_energy = std::clamp(g_base_energy + gen_produced, 0.0f, kBaseEnergyMax);
    }

    // ========== WATER EXTRACTORS ==========
    // Extract water (+1.5/min, costs -0.8 energy/min)
    if (water_count > 0) {
        float e_cost = (0.8f / 60.0f) * (float)water_count * dt;
        float water_rate = 1.5f / 60.0f;  // Per second
        
        if (g_base_energy >= e_cost) {
            g_base_energy -= e_cost;
            float temp_bonus = clamp01((g_temperature + 60.0f) / 80.0f);
            float water_produced = (float)water_count * water_rate * (0.5f + 0.5f * temp_bonus) * dt;
            g_base_water = std::clamp(g_base_water + water_produced, 0.0f, kBaseWaterMax);
        } else {
            add_alert("Purificador parado - Sem energia!", 1.0f, 0.5f, 0.2f);
        }
    }

    // ========== OXYGEN GENERATORS ==========
    // Produce O2 (+2/min, costs -1 energy/min)
    if (o2_count > 0) {
        float e_cost = (1.0f / 60.0f) * (float)o2_count * dt;
        float o2_rate = 2.0f / 60.0f;  // Per second
        
        if (g_base_energy >= e_cost) {
            g_base_energy -= e_cost;
            float o2_produced = (float)o2_count * o2_rate * dt;
            g_base_oxygen = std::clamp(g_base_oxygen + o2_produced, 0.0f, kBaseOxygenMax);
            g_atmosphere = std::clamp(g_atmosphere + o2_produced * 0.1f, 0.0f, 100.0f);
        } else {
            add_alert("Gerador O2 parado - Sem energia!", 1.0f, 0.5f, 0.2f);
        }
    }

    // ========== GREENHOUSES ==========
    // Produce food (+1/min, costs -0.5 energy/min, needs water)
    if (greenhouse_count > 0) {
        float e_cost = (0.5f / 60.0f) * (float)greenhouse_count * dt;
        float w_cost = (0.3f / 60.0f) * (float)greenhouse_count * dt;
        float food_rate = 1.0f / 60.0f;  // Per second
        
        if (g_base_water <= 0.0f) {
            add_alert("Estufa parada - Sem agua!", 0.2f, 0.6f, 1.0f);
        } else if (g_base_energy >= e_cost && g_base_water >= w_cost) {
            g_base_energy -= e_cost;
            g_base_water -= w_cost;
            float food_produced = (float)greenhouse_count * food_rate * dt;
            g_base_food = std::clamp(g_base_food + food_produced, 0.0f, kBaseFoodMax);
            g_base_oxygen = std::clamp(g_base_oxygen + food_produced * 0.2f, 0.0f, kBaseOxygenMax);
        } else {
            add_alert("Estufa parada - Sem energia!", 1.0f, 0.5f, 0.2f);
        }
    }

    // ========== WORKSHOP ==========
    // Repairs base integrity (+2/min, costs -1.5 energy/min)
    if (workshop_count > 0) {
        float e_cost = (1.5f / 60.0f) * (float)workshop_count * dt;
        float repair_rate = 2.0f / 60.0f;  // Per second
        float module_repair_rate = 5.0f / 60.0f;  // 5% health per minute per workshop
        
        if (g_base_energy >= e_cost) {
            g_base_energy -= e_cost;
            
            // Repair base integrity
            float repair = (float)workshop_count * repair_rate * dt;
            g_base_integrity = std::clamp(g_base_integrity + repair, 0.0f, kBaseIntegrityMax);
            
            // Repair damaged modules
            for (Module& m : g_modules) {
                if (m.health < 100.0f) {
                    m.health = std::min(100.0f, m.health + module_repair_rate * (float)workshop_count * dt);
                }
            }
        } else {
            add_alert("Oficina parada - Sem energia!", 1.0f, 0.5f, 0.2f);
        }
    }

    // ========== CO2 FACTORIES ==========
    // Release CO2 to warm the planet (costs -2 energy/min)
    if (co2_factory_count > 0) {
        float e_cost = (2.0f / 60.0f) * (float)co2_factory_count * dt;
        
        if (g_base_energy >= e_cost) {
            g_base_energy -= e_cost;
            
            float co2_rate = 0.5f / 60.0f;  // Per second
            float co2_produce = (float)co2_factory_count * co2_rate * dt;
            g_co2_level = std::clamp(g_co2_level + co2_produce, 0.0f, 100.0f);
            
            float warming_rate = 0.2f * (float)co2_factory_count * (1.0f - g_temperature / 50.0f);
            g_temperature = std::clamp(g_temperature + warming_rate * dt / 60.0f, -60.0f, 40.0f);
            
            g_atmosphere = std::clamp(g_atmosphere + co2_produce * 0.5f, 0.0f, 100.0f);
        } else {
            add_alert("Fabrica CO2 parada - Sem energia!", 1.0f, 0.5f, 0.2f);
        }
    }

    // ========== HABITATS ==========
    // Provide shelter (minimal consumption -0.3 energy/min)
    if (habitat_count > 0) {
        float e_cost = (0.3f / 60.0f) * (float)habitat_count * dt;
        if (g_base_energy >= e_cost) {
            g_base_energy -= e_cost;
            // Small passive O2 recycling
            g_base_oxygen = std::clamp(g_base_oxygen + 0.3f * (float)habitat_count * dt / 60.0f, 0.0f, kBaseOxygenMax);
        }
    }

    // ========== TERRAFORMER BEACONS ==========
    // Advanced terraforming (costs -5 energy/min)
    if (g_phase >= TerraPhase::Thawing) {
        for (Module& m : g_modules) {
            if (m.type != Block::TerraformerBeacon) continue;
            
            float e_cost = (5.0f / 60.0f) * dt;
            if (g_base_energy >= e_cost && g_base_water >= 1.0f) {
                g_base_energy -= e_cost;
                
                m.t += dt;
                while (m.t >= 0.15f && g_base_water > 0.5f) {
                    m.t -= 0.15f;
                    g_base_water = std::max(0.0f, g_base_water - 0.5f);
                    terraform_step(world, m.x, m.y);
                    melt_ice_around(world, m.x, m.y, 8);
                }
            } else {
                add_alert("Terraformador parado - Recursos!", 0.8f, 0.3f, 0.8f);
            }
        }
    }

    // ========== PLAYER IS AT BASE - RECHARGE SUIT ==========
    float dist_to_base = std::fabs(g_player.pos.x - (float)g_base_x);
    bool at_base = (dist_to_base < 15.0f);  // Within 15 blocks of base
    
    if (at_base) {
        // Recharge rate - slower for more challenge (3% per second)
        float recharge_rate = 3.0f * dt;  // Recharge speed (reduced from 8.0f)
        
        // Recharge O2 from base storage (consumes base O2!)
        if (g_player_oxygen < 100.0f && g_base_oxygen > 0.0f) {
            float need = std::min(recharge_rate, 100.0f - g_player_oxygen);
            float o2_cost = need * 0.20f;  // Costs 20% extra O2 from base (increased from 15%)
            float available = std::min(need, g_base_oxygen - o2_cost);
            if (available > 0.0f) {
                g_player_oxygen += available;
                g_base_oxygen -= (available + o2_cost);  // Extra cost!
            }
        }
        
        // Recharge water from base storage
        if (g_player_water < 100.0f && g_base_water > 0.0f) {
            float need = std::min(recharge_rate * 0.8f, 100.0f - g_player_water);
            float available = std::min(need, g_base_water);
            g_player_water += available;
            g_base_water -= available;
        }
        
        // Recharge food from base storage (slowest)
        if (g_player_food < 100.0f && g_base_food > 0.0f) {
            float need = std::min(recharge_rate * 0.4f, 100.0f - g_player_food);
            float available = std::min(need, g_base_food);
            g_player_food += available;
            g_base_food -= available;
        }
        
        // Can't recharge if base O2 too low!
        if (g_base_oxygen < 10.0f && g_player_oxygen < 50.0f) {
            add_alert("Oxigenio da base muito baixo!", 1.0f, 0.3f, 0.3f);
        }
    }

    // ========== FAILURE CONSEQUENCES ==========
    
    // Oxygen = 0 -> Can't recharge player
    if (g_base_oxygen <= 0.0f) {
        add_alert("O2 ZERADO - Nao pode recarregar!", 1.0f, 0.2f, 0.2f);
    } else if (g_base_oxygen < 20.0f) {
        add_alert("O2 BAIXO", 1.0f, 0.6f, 0.2f);
    }
    
    // Energy = 0 -> Modules shut down
    if (g_base_energy <= 0.0f) {
        add_alert("ENERGIA CRITICA - Modulos desligados!", 1.0f, 0.8f, 0.2f);
    } else if (g_base_energy < 20.0f) {
        add_alert("Energia baixa", 1.0f, 0.8f, 0.4f);
    }
    
    // Check for damaged modules
    int damaged_count = 0;
    for (const Module& m : g_modules) {
        if (m.status == ModuleStatus::Damaged) damaged_count++;
    }
    if (damaged_count > 0) {
        add_alert("Modulos danificados: " + std::to_string(damaged_count) + " - Construa Oficina!", 1.0f, 0.5f, 0.2f);
    }
    
    // Integrity = 0 -> Base collapse (severe damage)
    if (g_base_integrity <= 0.0f) {
        add_alert("BASE EM COLAPSO!", 1.0f, 0.0f, 0.0f);
        // Leak resources rapidly
        g_base_oxygen = std::max(0.0f, g_base_oxygen - 5.0f * dt);
        g_base_water = std::max(0.0f, g_base_water - 3.0f * dt);
        // Damage player if at base
        if (at_base) {
            g_player.hp = std::max(0, g_player.hp - 1);
        }
    } else if (g_base_integrity < 30.0f) {
        add_alert("Integridade critica - Construa Oficina!", 1.0f, 0.5f, 0.3f);
    }

    // ========== NATURAL PROCESSES ==========
    
    // Natural temperature equilibrium
    float base_temp = -60.0f + g_co2_level * 0.8f;
    g_temperature = lerp(g_temperature, base_temp, 0.001f * dt);
    
    // Player suit consumption (outside base uses suit tanks faster)
    float suit_use_mult = at_base ? 0.3f : 1.0f;  // Use less when at base
    float suit_o2_use = 0.12f * suit_use_mult * dt;
    float suit_water_use = 0.06f * suit_use_mult * dt;
    float suit_food_use = 0.03f * suit_use_mult * dt;
    
    g_player_oxygen = std::max(0.0f, g_player_oxygen - suit_o2_use);
    g_player_water = std::max(0.0f, g_player_water - suit_water_use);
    g_player_food = std::max(0.0f, g_player_food - suit_food_use);
    
    // Sync legacy variables for compatibility
    g_oxygen = g_player_oxygen;
    g_water_res = g_player_water;
    g_food = g_player_food;
    g_energy = g_base_energy;
    
    // HP regeneration when well fed (faster regeneration)
    if (g_player_food > 40.0f && g_player.hp < 100) {
        static float regen_timer = 0.0f;
        regen_timer += dt;
        // Regenerate 2 HP every 1.2 seconds (was 1 HP every 2s)
        if (regen_timer >= 1.2f) {
            regen_timer = 0.0f;
            int regen_amount = (g_player_food > 75.0f) ? 3 : 2;  // More food = faster regen
            g_player.hp = std::min(100, g_player.hp + regen_amount);
        }
    }
    
    // Update phase based on current conditions
    update_phase();
    
    // Melt ice globally when temperature rises above freezing
    static float melt_timer = 0.0f;
    melt_timer += dt;
    if (melt_timer >= 2.0f && g_temperature >= kTempThawing) {
        melt_timer = 0.0f;
        // Randomly melt some ice blocks
        for (int i = 0; i < 10; ++i) {
            int x = rng_next_u32() % world.w;
            int y = rng_next_u32() % world.h;
            if (world.get(x, y) == Block::Ice) {
                world.set(x, y, Block::Water);
                g_surface_dirty = true;
            }
        }
    }
}

// ============= Renderizacao 3D (Estilo Minicraft) =============

// Renderizar outline de um cubo (bordas pretas estilo pixel art)
static void render_cube_outline_3d(float x, float y, float z, float size, float line_width = 1.5f) {
    float half = size * 0.5f;
    
    glLineWidth(line_width);
    glColor4f(0.0f, 0.0f, 0.0f, 0.8f);
    
    // Arestas superiores
    glBegin(GL_LINE_LOOP);
    glVertex3f(x - half, y + half, z - half);
    glVertex3f(x + half, y + half, z - half);
    glVertex3f(x + half, y + half, z + half);
    glVertex3f(x - half, y + half, z + half);
    glEnd();
    
    // Arestas inferiores
    glBegin(GL_LINE_LOOP);
    glVertex3f(x - half, y - half, z - half);
    glVertex3f(x + half, y - half, z - half);
    glVertex3f(x + half, y - half, z + half);
    glVertex3f(x - half, y - half, z + half);
    glEnd();
    
    // Arestas verticais
    glBegin(GL_LINES);
    glVertex3f(x - half, y - half, z - half);
    glVertex3f(x - half, y + half, z - half);
    glVertex3f(x + half, y - half, z - half);
    glVertex3f(x + half, y + half, z - half);
    glVertex3f(x + half, y - half, z + half);
    glVertex3f(x + half, y + half, z + half);
    glVertex3f(x - half, y - half, z + half);
    glVertex3f(x - half, y + half, z + half);
    glEnd();
}

// Renderizar um cubo no espaco 3D com iluminacao simples (Minicraft style)
static void render_cube_3d(float x, float y, float z, float size, float r, float g, float b, float a = 1.0f, bool outline = false) {
    float half = size * 0.5f;
    
    // Cores com sombreamento por face (iluminacao fake - Minicraft tem 3 niveis)
    float top_shade = 1.0f;      // Face superior - clara
    float side_shade = 0.70f;    // Faces laterais - media
    float dark_shade = 0.50f;    // Faces escuras
    
    glBegin(GL_QUADS);
    
    // Face superior (Y+) - mais clara
    glColor4f(r * top_shade, g * top_shade, b * top_shade, a);
    glVertex3f(x - half, y + half, z - half);
    glVertex3f(x + half, y + half, z - half);
    glVertex3f(x + half, y + half, z + half);
    glVertex3f(x - half, y + half, z + half);
    
    // Face inferior (Y-) - escura (normalmente nao visivel)
    glColor4f(r * dark_shade, g * dark_shade, b * dark_shade, a);
    glVertex3f(x - half, y - half, z + half);
    glVertex3f(x + half, y - half, z + half);
    glVertex3f(x + half, y - half, z - half);
    glVertex3f(x - half, y - half, z - half);
    
    // Face frontal (Z+) - media
    glColor4f(r * side_shade, g * side_shade, b * side_shade, a);
    glVertex3f(x - half, y - half, z + half);
    glVertex3f(x + half, y - half, z + half);
    glVertex3f(x + half, y + half, z + half);
    glVertex3f(x - half, y + half, z + half);
    
    // Face traseira (Z-) - escura
    glColor4f(r * dark_shade, g * dark_shade, b * dark_shade, a);
    glVertex3f(x + half, y - half, z - half);
    glVertex3f(x - half, y - half, z - half);
    glVertex3f(x - half, y + half, z - half);
    glVertex3f(x + half, y + half, z - half);
    
    // Face direita (X+) - media
    glColor4f(r * side_shade, g * side_shade, b * side_shade, a);
    glVertex3f(x + half, y - half, z + half);
    glVertex3f(x + half, y - half, z - half);
    glVertex3f(x + half, y + half, z - half);
    glVertex3f(x + half, y + half, z + half);
    
    // Face esquerda (X-) - escura
    glColor4f(r * dark_shade, g * dark_shade, b * dark_shade, a);
    glVertex3f(x - half, y - half, z - half);
    glVertex3f(x - half, y - half, z + half);
    glVertex3f(x - half, y + half, z + half);
    glVertex3f(x - half, y + half, z - half);
    
    glEnd();
    
    // Desenhar outline se solicitado (estilo pixel art)
    if (outline) {
        render_cube_outline_3d(x, y, z, size, 1.0f);
    }
}

// Renderizar cubo 3D texturizado (tile do atlas) com iluminacao fake por face.
// Requer GL_TEXTURE_2D habilitado e g_tex_atlas bindado.
static void render_cube_3d_tex(float x, float y, float z, float size, Tile top, Tile side, Tile bottom,
                               float tint_r, float tint_g, float tint_b, float a = 1.0f, bool outline = false) {
    float half = size * 0.5f;

    // Iluminacao fake (3 niveis)
    float top_shade = 1.00f;
    float side_shade = 0.72f;
    float dark_shade = 0.52f;

    UvRect uv_top = atlas_uv(top);
    UvRect uv_side = atlas_uv(side);
    UvRect uv_bottom = atlas_uv(bottom);

    glBegin(GL_QUADS);

    // Top (Y+)
    glColor4f(tint_r * top_shade, tint_g * top_shade, tint_b * top_shade, a);
    glTexCoord2f(uv_top.u0, uv_top.v1); glVertex3f(x - half, y + half, z - half);
    glTexCoord2f(uv_top.u1, uv_top.v1); glVertex3f(x + half, y + half, z - half);
    glTexCoord2f(uv_top.u1, uv_top.v0); glVertex3f(x + half, y + half, z + half);
    glTexCoord2f(uv_top.u0, uv_top.v0); glVertex3f(x - half, y + half, z + half);

    // Bottom (Y-)
    glColor4f(tint_r * dark_shade, tint_g * dark_shade, tint_b * dark_shade, a);
    glTexCoord2f(uv_bottom.u0, uv_bottom.v0); glVertex3f(x - half, y - half, z + half);
    glTexCoord2f(uv_bottom.u1, uv_bottom.v0); glVertex3f(x + half, y - half, z + half);
    glTexCoord2f(uv_bottom.u1, uv_bottom.v1); glVertex3f(x + half, y - half, z - half);
    glTexCoord2f(uv_bottom.u0, uv_bottom.v1); glVertex3f(x - half, y - half, z - half);

    // Front (Z+)
    glColor4f(tint_r * side_shade, tint_g * side_shade, tint_b * side_shade, a);
    glTexCoord2f(uv_side.u0, uv_side.v0); glVertex3f(x - half, y - half, z + half);
    glTexCoord2f(uv_side.u1, uv_side.v0); glVertex3f(x + half, y - half, z + half);
    glTexCoord2f(uv_side.u1, uv_side.v1); glVertex3f(x + half, y + half, z + half);
    glTexCoord2f(uv_side.u0, uv_side.v1); glVertex3f(x - half, y + half, z + half);

    // Back (Z-)
    glColor4f(tint_r * dark_shade, tint_g * dark_shade, tint_b * dark_shade, a);
    glTexCoord2f(uv_side.u0, uv_side.v0); glVertex3f(x + half, y - half, z - half);
    glTexCoord2f(uv_side.u1, uv_side.v0); glVertex3f(x - half, y - half, z - half);
    glTexCoord2f(uv_side.u1, uv_side.v1); glVertex3f(x - half, y + half, z - half);
    glTexCoord2f(uv_side.u0, uv_side.v1); glVertex3f(x + half, y + half, z - half);

    // Left (X-)
    glColor4f(tint_r * dark_shade, tint_g * dark_shade, tint_b * dark_shade, a);
    glTexCoord2f(uv_side.u0, uv_side.v0); glVertex3f(x - half, y - half, z - half);
    glTexCoord2f(uv_side.u1, uv_side.v0); glVertex3f(x - half, y - half, z + half);
    glTexCoord2f(uv_side.u1, uv_side.v1); glVertex3f(x - half, y + half, z + half);
    glTexCoord2f(uv_side.u0, uv_side.v1); glVertex3f(x - half, y + half, z - half);

    // Right (X+)
    glColor4f(tint_r * side_shade, tint_g * side_shade, tint_b * side_shade, a);
    glTexCoord2f(uv_side.u0, uv_side.v0); glVertex3f(x + half, y - half, z + half);
    glTexCoord2f(uv_side.u1, uv_side.v0); glVertex3f(x + half, y - half, z - half);
    glTexCoord2f(uv_side.u1, uv_side.v1); glVertex3f(x + half, y + half, z - half);
    glTexCoord2f(uv_side.u0, uv_side.v1); glVertex3f(x + half, y + half, z + half);

    glEnd();

    if (outline) {
        render_cube_outline_3d(x, y, z, size, 1.0f);
    }
}

// Renderizar plano horizontal 3D (para chao/agua)
static void render_plane_3d(float x, float y, float z, float size, float r, float g, float b, float a = 1.0f) {
    float half = size * 0.5f;
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex3f(x - half, y, z - half);
    glVertex3f(x + half, y, z - half);
    glVertex3f(x + half, y, z + half);
    glVertex3f(x - half, y, z + half);
    glEnd();
}

// Renderizar plano texturizado (tile do atlas). Requer GL_TEXTURE_2D habilitado.
static void render_plane_3d_tex(float x, float y, float z, float size, Tile tile,
                                float tint_r, float tint_g, float tint_b, float a = 1.0f) {
    float half = size * 0.5f;
    UvRect uv = atlas_uv(tile);
    glColor4f(tint_r, tint_g, tint_b, a);
    glBegin(GL_QUADS);
    glTexCoord2f(uv.u0, uv.v0); glVertex3f(x - half, y, z - half);
    glTexCoord2f(uv.u1, uv.v0); glVertex3f(x + half, y, z - half);
    glTexCoord2f(uv.u1, uv.v1); glVertex3f(x + half, y, z + half);
    glTexCoord2f(uv.u0, uv.v1); glVertex3f(x - half, y, z + half);
    glEnd();
}

// Renderizar parede vertical texturizada (para laterais do terreno em altura).
// Requer GL_TEXTURE_2D habilitado.
static void render_wall_3d_tex_xpos(float x, float z, float y0, float y1, Tile tile,
                                   float tint_r, float tint_g, float tint_b, float a, float shade) {
    if (y1 <= y0) return;
    constexpr float half = 0.5f;
    UvRect uv = atlas_uv(tile);
    float xf = x + half;
    float z0 = z - half;
    float z1 = z + half;
    glColor4f(tint_r * shade, tint_g * shade, tint_b * shade, a);
    glBegin(GL_QUADS);
    glTexCoord2f(uv.u0, uv.v0); glVertex3f(xf, y0, z0);
    glTexCoord2f(uv.u1, uv.v0); glVertex3f(xf, y0, z1);
    glTexCoord2f(uv.u1, uv.v1); glVertex3f(xf, y1, z1);
    glTexCoord2f(uv.u0, uv.v1); glVertex3f(xf, y1, z0);
    glEnd();
}

static void render_wall_3d_tex_xneg(float x, float z, float y0, float y1, Tile tile,
                                   float tint_r, float tint_g, float tint_b, float a, float shade) {
    if (y1 <= y0) return;
    constexpr float half = 0.5f;
    UvRect uv = atlas_uv(tile);
    float xf = x - half;
    float z0 = z - half;
    float z1 = z + half;
    glColor4f(tint_r * shade, tint_g * shade, tint_b * shade, a);
    glBegin(GL_QUADS);
    glTexCoord2f(uv.u0, uv.v0); glVertex3f(xf, y0, z1);
    glTexCoord2f(uv.u1, uv.v0); glVertex3f(xf, y0, z0);
    glTexCoord2f(uv.u1, uv.v1); glVertex3f(xf, y1, z0);
    glTexCoord2f(uv.u0, uv.v1); glVertex3f(xf, y1, z1);
    glEnd();
}

static void render_wall_3d_tex_zpos(float x, float z, float y0, float y1, Tile tile,
                                   float tint_r, float tint_g, float tint_b, float a, float shade) {
    if (y1 <= y0) return;
    constexpr float half = 0.5f;
    UvRect uv = atlas_uv(tile);
    float zf = z + half;
    float x0 = x - half;
    float x1 = x + half;
    glColor4f(tint_r * shade, tint_g * shade, tint_b * shade, a);
    glBegin(GL_QUADS);
    glTexCoord2f(uv.u0, uv.v0); glVertex3f(x0, y0, zf);
    glTexCoord2f(uv.u1, uv.v0); glVertex3f(x1, y0, zf);
    glTexCoord2f(uv.u1, uv.v1); glVertex3f(x1, y1, zf);
    glTexCoord2f(uv.u0, uv.v1); glVertex3f(x0, y1, zf);
    glEnd();
}

static void render_wall_3d_tex_zneg(float x, float z, float y0, float y1, Tile tile,
                                   float tint_r, float tint_g, float tint_b, float a, float shade) {
    if (y1 <= y0) return;
    constexpr float half = 0.5f;
    UvRect uv = atlas_uv(tile);
    float zf = z - half;
    float x0 = x - half;
    float x1 = x + half;
    glColor4f(tint_r * shade, tint_g * shade, tint_b * shade, a);
    glBegin(GL_QUADS);
    glTexCoord2f(uv.u0, uv.v0); glVertex3f(x1, y0, zf);
    glTexCoord2f(uv.u1, uv.v0); glVertex3f(x0, y0, zf);
    glTexCoord2f(uv.u1, uv.v1); glVertex3f(x0, y1, zf);
    glTexCoord2f(uv.u0, uv.v1); glVertex3f(x1, y1, zf);
    glEnd();
}

// Renderizar esfera 3D simples (para player)
static void render_sphere_3d(float cx, float cy, float cz, float radius, float r, float g, float b, float a = 1.0f, int segments = 12) {
    // Aproximacao com faixas horizontais
    for (int i = 0; i < segments; ++i) {
        float lat0 = kPi * (-0.5f + (float)i / segments);
        float lat1 = kPi * (-0.5f + (float)(i + 1) / segments);
        float y0 = std::sin(lat0);
        float y1 = std::sin(lat1);
        float r0 = std::cos(lat0);
        float r1 = std::cos(lat1);
        
        // Sombreamento baseado na altura
        float shade = 0.6f + 0.4f * ((float)i / segments);
        glColor4f(r * shade, g * shade, b * shade, a);
        
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= segments; ++j) {
            float lng = 2.0f * kPi * (float)j / segments;
            float x = std::cos(lng);
            float z = std::sin(lng);
            
            glVertex3f(cx + radius * x * r1, cy + radius * y1, cz + radius * z * r1);
            glVertex3f(cx + radius * x * r0, cy + radius * y0, cz + radius * z * r0);
        }
        glEnd();
    }
}

// Renderizar cilindro 3D (para corpo do player)
static void render_cylinder_3d(float cx, float cy, float cz, float radius, float height, float r, float g, float b, float a = 1.0f, int segments = 12) {
    float half_h = height * 0.5f;
    
    // Corpo do cilindro
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * kPi * (float)i / segments;
        float x = std::cos(angle);
        float z = std::sin(angle);
        float shade = 0.7f + 0.3f * std::fabs(x);  // Sombreamento lateral
        glColor4f(r * shade, g * shade, b * shade, a);
        glVertex3f(cx + radius * x, cy + half_h, cz + radius * z);
        glVertex3f(cx + radius * x, cy - half_h, cz + radius * z);
    }
    glEnd();
    
    // Topo
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(cx, cy + half_h, cz);
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * kPi * (float)i / segments;
        glVertex3f(cx + radius * std::cos(angle), cy + half_h, cz + radius * std::sin(angle));
    }
    glEnd();
}

// ============= Rendering =============
static void render_world(HDC hdc, int win_w, int win_h) {
    if (!g_world) return;

    // === SETUP 3D ===
    glViewport(0, 0, win_w, win_h);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Projecao perspectiva
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = (float)win_w / (float)win_h;
    apply_perspective(74.0f, aspect, 0.1f, 2200.0f);
    
    // Atualizar target da camera para seguir o jogador
    g_camera.target.x = g_player.pos.x;
    g_camera.target.y = g_player.pos_y + 1.10f;  // Seguir altura do jogador + offset (mais "horizonte")
    g_camera.target.z = g_player.pos.y;  // Usando Y do mundo 2D como Z no 3D
    
    // Calcular posicao desejada e ajustar por colisao (raycast)
    g_camera.effective_distance = g_camera.distance;
    update_camera_position();
    check_camera_collision();
    update_camera_position();
    
    // Aplicar view matrix
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    apply_look_at();

    float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
    float daylight = std::fmax(0.0f, std::sin(day_phase * kPi));
    float o2f = clamp01(g_oxygen / 100.0f);

    // Background: mars-like -> earth-like, plus day/night factor.
    float atmos_factor = clamp01(g_atmosphere / 100.0f);
    float t_atmos = atmos_factor;
    
    // Sky color based on atmosphere and terraforming
    float sky_r = lerp(0.15f, 0.10f, t_atmos);
    float sky_g = lerp(0.08f, 0.20f, t_atmos);
    float sky_b = lerp(0.10f, 0.50f, t_atmos);
    float night_factor = 0.20f + 0.80f * daylight;
    
    // Clear com cor do ceu
    glClearColor(sky_r * night_factor, sky_g * night_factor, sky_b * night_factor, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Calcular area visivel baseada na posicao do jogador (culling)
    int player_tile_x = (int)std::floor(g_player.pos.x);
    int player_tile_z = (int)std::floor(g_player.pos.y);  // Y do 2D = Z no 3D
    int view_radius = (int)std::clamp(g_camera.distance * 3.8f + 55.0f, 110.0f, 200.0f);
    int wall_radius = std::clamp(view_radius - 45, 80, view_radius);
    int obj_radius = std::clamp(view_radius - 30, 90, view_radius);
    int view_radius2 = view_radius * view_radius;
    int wall_radius2 = wall_radius * wall_radius;
    int obj_radius2 = obj_radius * obj_radius;

    // Fog simples para dar sensacao de horizonte e esconder o limite de draw distance
    {
        float fog_col[4] = {sky_r * night_factor, sky_g * night_factor, sky_b * night_factor, 1.0f};
        glEnable(GL_FOG);
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogfv(GL_FOG_COLOR, fog_col);
        glHint(GL_FOG_HINT, GL_NICEST);

        float fog_start = std::max(90.0f, (float)view_radius * 0.42f);
        float fog_end = std::max(fog_start + 160.0f, (float)view_radius * 0.92f + 25.0f);
        glFogf(GL_FOG_START, fog_start);
        glFogf(GL_FOG_END, fog_end);
    }

    // === RENDERIZACAO 3D DO MUNDO ===
    
    int start_x = std::max(0, player_tile_x - view_radius);
    int end_x = std::min(g_world->w - 1, player_tile_x + view_radius);
    int start_z = std::max(0, player_tile_z - view_radius);
    int end_z = std::min(g_world->h - 1, player_tile_z + view_radius);
    
    // Texturas
    bool use_textures = (g_tex_atlas != 0);
    if (use_textures) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_tex_atlas);
    } else {
        glDisable(GL_TEXTURE_2D);
    }
    int water_frame = ((int)std::floor(g_day_time * 4.0f)) & 3;

    // Renderizar terreno com altura (montanhas/vales/desfiladeiros) + objetos sobre o solo
    {
        constexpr float side_shade = 0.72f;
        constexpr float dark_shade = 0.52f;
        constexpr float kTopEps = 0.01f;

        for (int tz = start_z; tz <= end_z; ++tz) {
            for (int tx = start_x; tx <= end_x; ++tx) {
                int ddx = tx - player_tile_x;
                int ddz = tz - player_tile_z;
                int dist2 = ddx * ddx + ddz * ddz;
                if (dist2 > view_radius2) continue; // culling circular

                float base_y = (float)g_world->height_at(tx, tz) * kHeightScale;

                Block surface = surface_block_at(*g_world, tx, tz);
                Block obj = object_block_at(*g_world, tx, tz);

                float world_x = (float)tx;
                float world_z = (float)tz;

                // === SOLO (top) ===
                {
                    BlockTex gtex = block_tex(surface);
                    if (gtex.is_water) {
                        gtex.top = (Tile)((int)Tile::Water0 + water_frame);
                        gtex.side = gtex.top;
                        gtex.bottom = gtex.top;
                    }

                    float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f, a = 1.0f;
                    if (gtex.uses_tint || gtex.transparent) {
                        float cr, cg, cb, ca;
                        block_color(surface, tz, g_world->h, cr, cg, cb, ca);
                        if (gtex.uses_tint) { tint_r = cr; tint_g = cg; tint_b = cb; }
                        if (gtex.transparent) a = ca;
                    }

                    float h_here = base_y;
                    float h_e = (tx < g_world->w - 1) ? (float)g_world->height_at(tx + 1, tz) * kHeightScale : h_here;
                    float h_w = (tx > 0) ? (float)g_world->height_at(tx - 1, tz) * kHeightScale : h_here;
                    float h_s = (tz < g_world->h - 1) ? (float)g_world->height_at(tx, tz + 1) * kHeightScale : h_here;
                    float h_n = (tz > 0) ? (float)g_world->height_at(tx, tz - 1) * kHeightScale : h_here;

                    // Shading leve por inclinacao/altura para destacar montanhas/vales.
                    float dhx = h_e - h_w;
                    float dhz = h_s - h_n;
                    float slope = std::sqrt(dhx * dhx + dhz * dhz);
                    float slope_shade = 1.0f - std::clamp(slope * 0.22f, 0.0f, 0.28f);
                    float alt_shade = 0.90f + 0.10f * clamp01(base_y / 18.0f);
                    float shade = slope_shade * alt_shade;
                    tint_r *= shade;
                    tint_g *= shade;
                    tint_b *= shade;

                    if (surface == Block::Water) {
                        float water_y = base_y - 0.18f + 0.05f * std::sin(g_day_time * 2.0f + world_x * 0.5f + world_z * 0.3f);
                        if (use_textures) render_plane_3d_tex(world_x, water_y, world_z, 1.0f, gtex.top, tint_r, tint_g, tint_b, a);
                        else render_plane_3d(world_x, water_y, world_z, 1.0f, tint_r, tint_g, tint_b, 0.75f);
                    } else {
                        float top_y = base_y + kTopEps;
                        if (use_textures) render_plane_3d_tex(world_x, top_y, world_z, 1.0f, gtex.top, tint_r, tint_g, tint_b, a);
                        else render_plane_3d(world_x, top_y, world_z, 1.0f, tint_r, tint_g, tint_b, a);
                    }

                    // === LATERAIS (paredes) para diferenca de altura ===
                    bool do_walls = (dist2 <= wall_radius2);
                    if (!do_walls) {
                        float max_drop = std::max(std::max(h_here - h_e, h_here - h_w), std::max(h_here - h_s, h_here - h_n));
                        if (max_drop > 1.40f) do_walls = true; // manter grandes penhascos visiveis ao longe
                    }

                    if (do_walls) {
                        if (use_textures) {
                            if (h_e < h_here) render_wall_3d_tex_xpos(world_x, world_z, h_e, h_here, gtex.side, tint_r, tint_g, tint_b, a, side_shade);
                            if (h_w < h_here) render_wall_3d_tex_xneg(world_x, world_z, h_w, h_here, gtex.side, tint_r, tint_g, tint_b, a, dark_shade);
                            if (h_s < h_here) render_wall_3d_tex_zpos(world_x, world_z, h_s, h_here, gtex.side, tint_r, tint_g, tint_b, a, side_shade);
                            if (h_n < h_here) render_wall_3d_tex_zneg(world_x, world_z, h_n, h_here, gtex.side, tint_r, tint_g, tint_b, a, dark_shade);
                        } else {
                            // Fallback sem texturas: quads coloridos
                            auto wall_col = [&](float s) {
                                glColor4f(tint_r * s, tint_g * s, tint_b * s, a);
                            };
                            constexpr float half = 0.5f;
                            if (h_e < h_here) {
                                wall_col(side_shade);
                                glBegin(GL_QUADS);
                                glVertex3f(world_x + half, h_e, world_z - half);
                                glVertex3f(world_x + half, h_e, world_z + half);
                                glVertex3f(world_x + half, h_here, world_z + half);
                                glVertex3f(world_x + half, h_here, world_z - half);
                                glEnd();
                            }
                            if (h_w < h_here) {
                                wall_col(dark_shade);
                                glBegin(GL_QUADS);
                                glVertex3f(world_x - half, h_w, world_z + half);
                                glVertex3f(world_x - half, h_w, world_z - half);
                                glVertex3f(world_x - half, h_here, world_z - half);
                                glVertex3f(world_x - half, h_here, world_z + half);
                                glEnd();
                            }
                            if (h_s < h_here) {
                                wall_col(side_shade);
                                glBegin(GL_QUADS);
                                glVertex3f(world_x - half, h_s, world_z + half);
                                glVertex3f(world_x + half, h_s, world_z + half);
                                glVertex3f(world_x + half, h_here, world_z + half);
                                glVertex3f(world_x - half, h_here, world_z + half);
                                glEnd();
                            }
                            if (h_n < h_here) {
                                wall_col(dark_shade);
                                glBegin(GL_QUADS);
                                glVertex3f(world_x + half, h_n, world_z - half);
                                glVertex3f(world_x - half, h_n, world_z - half);
                                glVertex3f(world_x - half, h_here, world_z - half);
                                glVertex3f(world_x + half, h_here, world_z - half);
                                glEnd();
                            }
                        }
                    }
                }

                // === OBJETOS sobre o solo (rochas/minerios/modulos/estruturas) ===
                if (obj != Block::Air && dist2 <= obj_radius2) {
                    BlockTex tex = block_tex(obj);
                    if (tex.is_water) {
                        tex.top = (Tile)((int)Tile::Water0 + water_frame);
                        tex.side = tex.top;
                        tex.bottom = tex.top;
                    }

                    float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f, a = 1.0f;
                    if (tex.uses_tint || tex.transparent) {
                        float cr, cg, cb, ca;
                        block_color(obj, tz, g_world->h, cr, cg, cb, ca);
                        if (tex.uses_tint) { tint_r = cr; tint_g = cg; tint_b = cb; }
                        if (tex.transparent) a = ca;
                    }

                    if (obj == Block::Leaves) {
                        // Folhas como plano elevado acima do terreno
                        float leaf_y = base_y + 0.60f;
                        if (use_textures) render_plane_3d_tex(world_x, leaf_y, world_z, 1.0f, tex.top, tint_r, tint_g, tint_b, a);
                        else render_plane_3d(world_x, leaf_y, world_z, 1.0f, tint_r, tint_g, tint_b, 0.85f);
                    } else if (obj == Block::Water) {
                        // Agua como plano levemente abaixo, com animacao
                        float water_y = base_y - 0.18f + 0.05f * std::sin(g_day_time * 2.0f + world_x * 0.5f + world_z * 0.3f);
                        if (use_textures) render_plane_3d_tex(world_x, water_y, world_z, 1.0f, tex.top, tint_r, tint_g, tint_b, a);
                        else render_plane_3d(world_x, water_y, world_z, 1.0f, tint_r, tint_g, tint_b, 0.75f);
                    } else {
                        bool use_outline = is_module(obj) || (obj == Block::Crystal || obj == Block::Coal || obj == Block::Iron || obj == Block::Copper);
                        float center_y = base_y + 0.5f;
                        if (use_textures) render_cube_3d_tex(world_x, center_y, world_z, 1.0f, tex.top, tex.side, tex.bottom, tint_r, tint_g, tint_b, a, use_outline);
                        else render_cube_3d(world_x, center_y, world_z, 1.0f, tint_r, tint_g, tint_b, a, use_outline);
                    }
                }
            }
        }
    }

    // Drops coletaveis
    if (use_textures && !g_drops.empty()) {
        for (const auto& d : g_drops) {
            // Culling simples no grid visivel
            if (d.x < (float)start_x - 2.0f || d.x >(float)end_x + 2.0f ||
                d.z < (float)start_z - 2.0f || d.z >(float)end_z + 2.0f) continue;

            BlockTex tex = block_tex(d.item);
            if (tex.is_water) {
                tex.top = (Tile)((int)Tile::Water0 + water_frame);
                tex.side = tex.top;
                tex.bottom = tex.top;
            }

            float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f, a = 1.0f;
            if (tex.uses_tint || tex.transparent) {
                float cr, cg, cb, ca;
                block_color(d.item, (int)d.z, g_world->h, cr, cg, cb, ca);
                if (tex.uses_tint) { tint_r = cr; tint_g = cg; tint_b = cb; }
                if (tex.transparent) a = ca;
            }

            float bob = 0.03f * std::sin(d.t * 4.0f);
            render_cube_3d_tex(d.x, d.y + bob, d.z, 0.34f, tex.top, tex.side, tex.bottom, tint_r, tint_g, tint_b, a, true);
        }
    }

    if (use_textures) {
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }
    
    // === RENDERIZAR PLAYER 3D (Estilo Minicraft - Blocky) ===
    {
        float px = g_player.pos.x;
        float py = g_player.pos_y;  // Usar altura real do jogador
        float pz = g_player.pos.y;  // Y do 2D = Z no 3D
        
        // Indicador de perigo (player pisca vermelho quando HP ou O2 baixo)
        bool in_danger = (g_player.hp < 30 || g_player_oxygen < 20.0f);
        float danger_pulse = in_danger ? (0.5f + 0.5f * std::sin(g_player.anim_frame * 8.0f)) : 0.0f;
        
        // Sombra no chao (maior e mais visivel)
        glDisable(GL_DEPTH_TEST);
        render_plane_3d(px, g_player.ground_height + 0.02f, pz, 0.9f, 0.0f, 0.0f, 0.0f, 0.55f);
        
        // Circulo de indicador de perigo
        if (in_danger) {
            render_plane_3d(px, g_player.ground_height + 0.03f, pz, 1.2f, 
                kColorDanger[0], kColorDanger[1], kColorDanger[2], danger_pulse * 0.3f);
        }
        glEnable(GL_DEPTH_TEST);
        
        // Usar rotacao continua para orientar o personagem
        float rot_rad = g_player.rotation * (kPi / 180.0f);
        float sin_rot = std::sin(rot_rad);
        float cos_rot = std::cos(rot_rad);
        
        // Animacao de movimento (bob up/down)
        float bob = g_player.is_moving ? std::sin(g_player.walk_timer * 14.0f) * 0.04f : 0.0f;
        float leg_swing = g_player.is_moving ? std::sin(g_player.walk_timer * 10.0f) * 0.12f : 0.0f;
        
        // === CORPO (Bloco principal - torso branco do astronauta) ===
        render_cube_3d(px, py + 0.30f + bob, pz, 0.45f, 0.95f, 0.95f, 0.98f, 1.0f, true);
        
        // === CABECA (Capacete - bloco branco com visor) ===
        render_cube_3d(px, py + 0.68f + bob, pz, 0.38f, 0.92f, 0.92f, 0.95f, 1.0f, true);
        
        // Visor (bloco azul na frente da cabeca)
        float visor_dist = 0.12f;
        float vx = px + sin_rot * visor_dist;
        float vz = pz + cos_rot * visor_dist;
        render_cube_3d(vx, py + 0.68f + bob, vz, 0.22f, 0.1f, 0.35f, 0.75f, 0.95f, false);
        
        // === MOCHILA (Bloco cinza atras) ===
        float pack_dist = 0.25f;
        float pack_x = px - sin_rot * pack_dist;
        float pack_z = pz - cos_rot * pack_dist;
        render_cube_3d(pack_x, py + 0.35f + bob, pack_z, 0.30f, 0.45f, 0.47f, 0.50f, 1.0f, true);
        
        // === PERNAS (2 blocos pequenos animados) ===
        float leg_sep = 0.12f;
        // Offset perpendicular a direcao
        float perp_x = cos_rot;
        float perp_z = -sin_rot;
        
        // Perna esquerda
        float ll_x = px - perp_x * leg_sep + sin_rot * leg_swing;
        float ll_z = pz - perp_z * leg_sep + cos_rot * leg_swing;
        render_cube_3d(ll_x, py - 0.10f, ll_z, 0.18f, 0.25f, 0.27f, 0.30f, 1.0f, true);
        
        // Perna direita
        float rl_x = px + perp_x * leg_sep - sin_rot * leg_swing;
        float rl_z = pz + perp_z * leg_sep - cos_rot * leg_swing;
        render_cube_3d(rl_x, py - 0.10f, rl_z, 0.18f, 0.25f, 0.27f, 0.30f, 1.0f, true);
        
        // === BRACOS (2 blocos pequenos - animados se minerando) ===
        float arm_bob = g_player.is_mining ? std::sin(g_player.mine_anim * 15.0f) * 0.15f : 0.0f;
        float arm_sep = 0.28f;
        
        // Braco esquerdo
        float la_x = px - perp_x * arm_sep;
        float la_z = pz - perp_z * arm_sep;
        render_cube_3d(la_x, py + 0.25f + bob - arm_bob, la_z, 0.15f, 0.90f, 0.90f, 0.92f, 1.0f, true);
        
        // Braco direito
        float ra_x = px + perp_x * arm_sep;
        float ra_z = pz + perp_z * arm_sep;
        render_cube_3d(ra_x, py + 0.25f + bob + arm_bob, ra_z, 0.15f, 0.90f, 0.90f, 0.92f, 1.0f, true);
    }

    // === HIGHLIGHT DO ALVO 3D ===
    if (g_has_target) {
        float target_x = (float)g_target_x;
        float target_z = (float)g_target_y;
        Block tb = g_world->get(g_target_x, g_target_y);
        float base_y = (float)g_world->height_at(g_target_x, g_target_y) * kHeightScale;
        float target_y = base_y + 0.01f;
        if (tb == Block::Leaves) target_y = base_y + 0.60f;
        else if (tb == Block::Water) target_y = base_y - 0.18f;
        else if (tb != Block::Air && !is_ground_like(tb)) target_y = base_y + 0.50f;
        
        // Desenhar wireframe do cubo alvo
        float rr = g_target_in_range ? 0.25f : 0.90f;
        float gg = g_target_in_range ? 0.95f : 0.25f;
        glColor4f(rr, gg, 0.20f, 0.95f);
        glLineWidth(2.0f);
        
        float half = 0.52f;
        glBegin(GL_LINE_LOOP);
        glVertex3f(target_x - half, target_y + half, target_z - half);
        glVertex3f(target_x + half, target_y + half, target_z - half);
        glVertex3f(target_x + half, target_y + half, target_z + half);
        glVertex3f(target_x - half, target_y + half, target_z + half);
        glEnd();
        glBegin(GL_LINE_LOOP);
        glVertex3f(target_x - half, target_y - half, target_z - half);
        glVertex3f(target_x + half, target_y - half, target_z - half);
        glVertex3f(target_x + half, target_y - half, target_z + half);
        glVertex3f(target_x - half, target_y - half, target_z + half);
        glEnd();
        glBegin(GL_LINES);
        glVertex3f(target_x - half, target_y - half, target_z - half);
        glVertex3f(target_x - half, target_y + half, target_z - half);
        glVertex3f(target_x + half, target_y - half, target_z - half);
        glVertex3f(target_x + half, target_y + half, target_z - half);
        glVertex3f(target_x + half, target_y - half, target_z + half);
        glVertex3f(target_x + half, target_y + half, target_z + half);
        glVertex3f(target_x - half, target_y - half, target_z + half);
        glVertex3f(target_x - half, target_y + half, target_z + half);
        glEnd();

        // Overlay de "cracks" durante mineracao (progresso)
        if (g_tex_atlas != 0 && g_mine_progress > 0.001f &&
            g_mine_block_x == g_target_x && g_mine_block_y == g_target_y) {
            int lvl = std::clamp((int)std::floor(g_mine_progress * 8.0f), 0, 7);
            Tile crack = (Tile)((int)Tile::Crack1 + lvl);

            float crack_y = base_y + 0.01f + 0.002f;
            if (tb == Block::Leaves) crack_y = base_y + 0.60f + 0.002f;
            else if (tb == Block::Water) crack_y = base_y - 0.18f + 0.002f;
            else if (tb != Block::Air && !is_ground_like(tb)) crack_y = base_y + get_block_height(tb) + 0.002f;

            glDepthMask(GL_FALSE);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, g_tex_atlas);
            render_plane_3d_tex(target_x, crack_y, target_z, 1.04f, crack, 1.0f, 1.0f, 1.0f, 1.0f);
            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_TEXTURE_2D);
            glDepthMask(GL_TRUE);
        }

        // Preview de colocacao (RMB) - onde o bloco/modulo vai ser colocado
        if (g_has_place_target && g_place_in_range &&
            (g_place_x != g_target_x || g_place_y != g_target_y)) {
            float px = (float)g_place_x;
            float pz = (float)g_place_y;
            float place_base_y = (float)g_world->height_at(g_place_x, g_place_y) * kHeightScale;
            float py = place_base_y + 0.50f;
            if (g_selected == Block::Leaves) py = place_base_y + 0.60f;
            else if (g_selected == Block::Water) py = place_base_y - 0.18f;

            Block cur = g_world->get(g_place_x, g_place_y);
            bool placeable = (!is_base_structure(cur) && !is_module(cur) &&
                              (cur == Block::Air || cur == Block::Water || !is_solid(cur)));

            float pl = g_player.pos.x - g_player.w * 0.5f;
            float pr = g_player.pos.x + g_player.w * 0.5f;
            float pt = g_player.pos.y - g_player.h * 0.5f;
            float pb = g_player.pos.y + g_player.h * 0.5f;
            bool overlaps_player = !(g_place_x + 1 <= pl || g_place_x >= pr || g_place_y + 1 <= pt || g_place_y >= pb);

            bool has_item = false;
            if (is_module(g_selected)) {
                has_item = is_unlocked(g_selected) && can_afford(module_cost(g_selected));
            } else {
                has_item = (g_inventory[(int)g_selected] > 0);
            }

            bool ok = placeable && !overlaps_player && has_item;
            glColor4f(ok ? 0.25f : 0.95f, ok ? 0.70f : 0.25f, ok ? 0.95f : 0.25f, 0.80f);
            glLineWidth(2.0f);

            float half = 0.52f;
            glBegin(GL_LINE_LOOP);
            glVertex3f(px - half, py + half, pz - half);
            glVertex3f(px + half, py + half, pz - half);
            glVertex3f(px + half, py + half, pz + half);
            glVertex3f(px - half, py + half, pz + half);
            glEnd();
            glBegin(GL_LINE_LOOP);
            glVertex3f(px - half, py - half, pz - half);
            glVertex3f(px + half, py - half, pz - half);
            glVertex3f(px + half, py - half, pz + half);
            glVertex3f(px - half, py - half, pz + half);
            glEnd();
            glBegin(GL_LINES);
            glVertex3f(px - half, py - half, pz - half);
            glVertex3f(px - half, py + half, pz - half);
            glVertex3f(px + half, py - half, pz - half);
            glVertex3f(px + half, py + half, pz - half);
            glVertex3f(px + half, py - half, pz + half);
            glVertex3f(px + half, py + half, pz + half);
            glVertex3f(px - half, py - half, pz + half);
            glVertex3f(px - half, py + half, pz + half);
            glEnd();
        }
    }
    
    // === MUDAR PARA PROJECAO 2D PARA HUD ===
    glDisable(GL_FOG);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, win_w, win_h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    // === CROSSHAIR CENTRAL (Estilo Minicraft) ===
    {
        float cx = win_w * 0.5f;
        float cy = win_h * 0.5f;
        float cross_size = 12.0f;
        float cross_thick = 2.0f;
        
        // Contorno preto
        glColor4f(0.0f, 0.0f, 0.0f, 0.7f);
        glLineWidth(cross_thick + 2.0f);
        glBegin(GL_LINES);
        glVertex2f(cx - cross_size, cy);
        glVertex2f(cx + cross_size, cy);
        glVertex2f(cx, cy - cross_size);
        glVertex2f(cx, cy + cross_size);
        glEnd();
        
        // Crosshair branco
        glColor4f(1.0f, 1.0f, 1.0f, 0.9f);
        glLineWidth(cross_thick);
        glBegin(GL_LINES);
        glVertex2f(cx - cross_size, cy);
        glVertex2f(cx + cross_size, cy);
        glVertex2f(cx, cy - cross_size);
        glVertex2f(cx, cy + cross_size);
        glEnd();
        
        // Ponto central
        glPointSize(4.0f);
        glBegin(GL_POINTS);
        glVertex2f(cx, cy);
        glEnd();
    }

    // HUD
    if (g_state == GameState::Playing || g_state == GameState::Paused) {
        
        // ============= BARRA DE PROGRESSO DE TERRAFORMACAO (TOPO) =============
        {
            float progress_w = 400.0f;
            float progress_h = 22.0f;
            float progress_x = win_w * 0.5f - progress_w * 0.5f;
            float progress_y = 12.0f;
            
            // Fundo da barra
            render_quad(progress_x - 4.0f, progress_y - 4.0f, progress_w + 8.0f, progress_h + 8.0f, 
                0.0f, 0.0f, 0.0f, 0.65f);
            
            // Barra de progresso colorida por fase
            float pct = g_terraform / 100.0f;
            float pr, pg, pb;
            std::string phase_name;
            if (g_phase == TerraPhase::Frozen) { pr = 0.4f; pg = 0.6f; pb = 0.9f; phase_name = "Congelado"; }
            else if (g_phase == TerraPhase::Warming) { pr = 0.9f; pg = 0.6f; pb = 0.3f; phase_name = "Aquecendo"; }
            else if (g_phase == TerraPhase::Thawing) { pr = 0.4f; pg = 0.8f; pb = 0.9f; phase_name = "Degelo"; }
            else if (g_phase == TerraPhase::Habitable) { pr = 0.3f; pg = 0.9f; pb = 0.4f; phase_name = "Habitavel"; }
            else { pr = 0.2f; pg = 1.0f; pb = 0.5f; phase_name = "Terraformado"; }
            
            // Barra de fundo (cinza)
            render_quad(progress_x, progress_y, progress_w, progress_h, 0.15f, 0.15f, 0.18f, 0.90f);
            
            // Barra de progresso
            render_quad(progress_x, progress_y, progress_w * pct, progress_h, pr, pg, pb, 0.95f);
            
            // Bordas pixeladas
            render_quad(progress_x, progress_y, progress_w, 2.0f, 0.4f, 0.4f, 0.45f, 0.90f);
            render_quad(progress_x, progress_y + progress_h - 2.0f, progress_w, 2.0f, 0.1f, 0.1f, 0.12f, 0.90f);
            
            // Texto de progresso
            char buf[64];
            snprintf(buf, sizeof(buf), "%d%% - %s", (int)(pct * 100.0f), phase_name.c_str());
            float tw = estimate_text_w_px(buf);
            draw_text(progress_x + progress_w * 0.5f - tw * 0.5f, progress_y + 15.0f, buf, 
                kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.95f);
        }
        
        float x0 = 20.0f;
        float y0 = 50.0f;  // Ajustado para dar espaco para a barra de terraformacao
        float bar_w = 180.0f;
        float bar_h = 14.0f;
        float bar_gap = 18.0f;
        
        // Check if player is at base
        float dist_to_base = std::fabs(g_player.pos.x - (float)g_base_x);
        bool at_base = (dist_to_base < 15.0f);
        
        // === FUNDO TRANSPARENTE DO HUD ESQUERDO ===
        float left_panel_h = bar_gap * 9 + 80.0f;  // Altura aproximada do painel esquerdo
        render_quad(x0 - 10.0f, y0 - 10.0f, bar_w + 20.0f, left_panel_h, 0.0f, 0.0f, 0.0f, 0.30f);
        
        // === LEFT PANEL: SUIT STATUS (Player) ===
        draw_text(x0, y0 - 2.0f, "TRAJE", 0.70f, 0.75f, 0.85f, 0.85f);
        y0 += 12.0f;
        
        // HP Bar (vermelho - usando cores centralizadas)
        float hp_pct = g_player.hp / 100.0f;
        bool hp_crit = hp_pct < 0.25f;
        float hp_flash = hp_crit ? (0.7f + 0.3f * std::sin(g_player.anim_frame * 6.0f)) : 1.0f;
        render_bar(x0, y0, bar_w, 16.0f, hp_pct, kColorHp[0] * hp_flash, kColorHp[1], kColorHp[2]);
        draw_text(x0 + 6.0f, y0 + 12.0f, "HP " + std::to_string(g_player.hp), 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.95f);
        
        // Suit Oxygen (verde - usando cores centralizadas)
        float o2_pct = g_player_oxygen / 100.0f;
        bool o2_crit = o2_pct < 0.25f;
        float o2_r = o2_crit ? kColorDanger[0] : kColorOxygen[0];
        float o2_g = o2_crit ? kColorDanger[1] : kColorOxygen[1];
        float o2_b = o2_crit ? kColorDanger[2] : kColorOxygen[2];
        float o2_flash = o2_crit ? (0.7f + 0.3f * std::sin(g_player.anim_frame * 6.0f)) : 1.0f;
        render_bar(x0, y0 + bar_gap, bar_w, bar_h, o2_pct, o2_r * o2_flash, o2_g * o2_flash, o2_b);
        draw_text(x0 + 6.0f, y0 + bar_gap + 11.0f, "O2 " + std::to_string((int)g_player_oxygen) + "%", 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.90f);
        
        // Suit Water (azul - usando cores centralizadas)
        float water_pct = g_player_water / 100.0f;
        bool water_crit = water_pct < 0.25f;
        float water_flash = water_crit ? (0.7f + 0.3f * std::sin(g_player.anim_frame * 6.0f)) : 1.0f;
        render_bar(x0, y0 + bar_gap * 2, bar_w, bar_h, water_pct, 
            (water_crit ? kColorDanger[0] : kColorWater[0]) * water_flash,
            (water_crit ? kColorDanger[1] : kColorWater[1]) * water_flash,
            water_crit ? kColorDanger[2] : kColorWater[2]);
        draw_text(x0 + 6.0f, y0 + bar_gap * 2 + 11.0f, "H2O " + std::to_string((int)g_player_water) + "%", 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.90f);
        
        // Suit Food (laranja - usando cores centralizadas)
        float food_pct = g_player_food / 100.0f;
        bool food_crit = food_pct < 0.25f;
        render_bar(x0, y0 + bar_gap * 3, bar_w, bar_h, food_pct, 
            food_crit ? kColorWarning[0] : kColorFood[0],
            food_crit ? kColorWarning[1] : kColorFood[1],
            food_crit ? kColorWarning[2] : kColorFood[2]);
        draw_text(x0 + 6.0f, y0 + bar_gap * 3 + 11.0f, "Comida " + std::to_string((int)g_player_food) + "%", 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.90f);
        
        // === LEFT PANEL: BASE STATUS ===
        y0 += bar_gap * 4 + 15.0f;
        
        // At base indicator
        if (at_base) {
            render_quad(x0 - 5.0f, y0 - 5.0f, bar_w + 10.0f, 20.0f, 0.15f, 0.35f, 0.20f, 0.80f);
            draw_text(x0, y0 + 10.0f, "NA BASE - RECARREGANDO", 0.40f, 0.95f, 0.50f, 0.95f);
            y0 += 22.0f;
        } else {
            draw_text(x0, y0 + 10.0f, "ARMAZENAMENTO DA BASE", 0.70f, 0.75f, 0.85f, 0.85f);
            y0 += 15.0f;
        }
        
        render_bar(x0, y0, bar_w, bar_h, g_base_energy / kBaseEnergyMax, 0.95f, 0.84f, 0.25f);
        draw_text(x0 + 6.0f, y0 + 11.0f, "Energia " + std::to_string((int)g_base_energy) + "/" + std::to_string((int)kBaseEnergyMax), 0.90f, 0.90f, 0.90f, 0.90f);

        render_bar(x0, y0 + bar_gap, bar_w, bar_h, g_base_water / kBaseWaterMax, 0.25f, 0.65f, 0.95f);
        draw_text(x0 + 6.0f, y0 + bar_gap + 11.0f, "Agua " + std::to_string((int)g_base_water) + "/" + std::to_string((int)kBaseWaterMax), 0.90f, 0.90f, 0.90f, 0.90f);

        render_bar(x0, y0 + bar_gap * 2, bar_w, bar_h, g_base_oxygen / kBaseOxygenMax, 0.20f, 0.95f, 0.55f);
        draw_text(x0 + 6.0f, y0 + bar_gap * 2 + 11.0f, "Oxigenio " + std::to_string((int)g_base_oxygen) + "/" + std::to_string((int)kBaseOxygenMax), 0.90f, 0.90f, 0.90f, 0.90f);
        
        render_bar(x0, y0 + bar_gap * 3, bar_w, bar_h, g_base_food / kBaseFoodMax, 0.85f, 0.65f, 0.25f);
        draw_text(x0 + 6.0f, y0 + bar_gap * 3 + 11.0f, "Comida " + std::to_string((int)g_base_food) + "/" + std::to_string((int)kBaseFoodMax), 0.90f, 0.90f, 0.90f, 0.90f);
        
        // Integrity bar with color based on level
        float int_r = g_base_integrity > 50.0f ? 0.35f : (g_base_integrity > 25.0f ? 0.90f : 0.95f);
        float int_g = g_base_integrity > 50.0f ? 0.85f : (g_base_integrity > 25.0f ? 0.65f : 0.25f);
        float int_b = g_base_integrity > 50.0f ? 0.45f : 0.20f;
        render_bar(x0, y0 + bar_gap * 4, bar_w, bar_h, g_base_integrity / kBaseIntegrityMax, int_r, int_g, int_b);
        draw_text(x0 + 6.0f, y0 + bar_gap * 4 + 11.0f, "Integ " + std::to_string((int)g_base_integrity) + "/" + std::to_string((int)kBaseIntegrityMax), 0.90f, 0.90f, 0.90f, 0.90f);
        
        // === RIGHT PANEL: Terraforming Stats ===
        float rx0 = win_w - bar_w - 30.0f;
        float ry0 = 18.0f;
        
        // === FUNDO TRANSPARENTE DO HUD DIREITO ===
        float right_panel_h = bar_gap * 6 + 90.0f;  // Altura aproximada do painel direito
        render_quad(rx0 - 10.0f, ry0 - 10.0f, bar_w + 20.0f, right_panel_h, 0.0f, 0.0f, 0.0f, 0.30f);
        
        // Phase indicator
        float phase_colors[5][3] = {
            {0.4f, 0.6f, 0.9f},  // Frozen - blue
            {0.9f, 0.6f, 0.3f},  // Warming - orange
            {0.4f, 0.8f, 0.9f},  // Thawing - cyan
            {0.3f, 0.9f, 0.4f},  // Habitable - green
            {0.2f, 1.0f, 0.5f},  // Terraformed - bright green
        };
        int pi = (int)g_phase;
        render_quad(rx0, ry0, bar_w, 20.0f, phase_colors[pi][0] * 0.3f, phase_colors[pi][1] * 0.3f, phase_colors[pi][2] * 0.3f, 0.7f);
        draw_text(rx0 + 6.0f, ry0 + 15.0f, std::string("Fase: ") + phase_name(g_phase), phase_colors[pi][0], phase_colors[pi][1], phase_colors[pi][2], 0.98f);
        
        // Temperature
        ry0 += 28.0f;
        float temp_pct = clamp01((g_temperature + 60.0f) / 100.0f); // -60 to +40
        float temp_r = temp_pct;
        float temp_b = 1.0f - temp_pct;
        render_bar(rx0, ry0, bar_w, bar_h, temp_pct, temp_r, 0.3f, temp_b);
        char temp_str[32];
        snprintf(temp_str, sizeof(temp_str), "Temp %.0fC", g_temperature);
        draw_text(rx0 + 6.0f, ry0 + 11.0f, temp_str, 0.95f, 0.95f, 0.95f, 0.90f);
        
        // CO2 Level
        ry0 += bar_gap;
        render_bar(rx0, ry0, bar_w, bar_h, g_co2_level / 100.0f, 0.70f, 0.50f, 0.30f);
        draw_text(rx0 + 6.0f, ry0 + 11.0f, "CO2 " + std::to_string((int)g_co2_level) + "%", 0.90f, 0.90f, 0.90f, 0.90f);
        
        // Atmosphere
        ry0 += bar_gap;
        render_bar(rx0, ry0, bar_w, bar_h, g_atmosphere / 100.0f, 0.50f, 0.70f, 0.90f);
        draw_text(rx0 + 6.0f, ry0 + 11.0f, "Atmos " + std::to_string((int)g_atmosphere) + "%", 0.90f, 0.90f, 0.90f, 0.90f);
        
        // Terraform Progress
        ry0 += bar_gap;
        render_bar(rx0, ry0, bar_w, bar_h, g_terraform / 100.0f, 0.25f, 0.90f, 0.40f);
        draw_text(rx0 + 6.0f, ry0 + 11.0f, "Terraform " + std::to_string((int)g_terraform) + "%", 0.90f, 0.90f, 0.90f, 0.90f);
        
        // === BASE INDICATOR ===
        ry0 += bar_gap + 10.0f;
        float dist_x = (float)g_base_x - g_player.pos.x;
        float dist_blocks = std::fabs(dist_x);
        std::string dir = dist_x > 2.0f ? "<<<" : (dist_x < -2.0f ? ">>>" : "AQUI");
        std::string dist_str = "Base: " + dir + " " + std::to_string((int)dist_blocks) + "m";
        float dist_alpha = (dist_blocks > 30.0f) ? 0.95f : 0.70f;
        float dist_r = (dist_blocks > 80.0f) ? 0.95f : 0.65f;
        float dist_g = (dist_blocks > 80.0f) ? 0.55f : 0.85f;
        render_quad(rx0, ry0, bar_w, 20.0f, 0.15f, 0.18f, 0.25f, 0.75f);
        draw_text(rx0 + 6.0f, ry0 + 15.0f, dist_str, dist_r, dist_g, 0.60f, dist_alpha);
        draw_text(rx0 + bar_w - 50.0f, ry0 + 15.0f, "[H]", 0.55f, 0.75f, 0.95f, 0.80f);

        // === HOTBAR ESTILO MINICRAFT ===
        // Funcao local para desenhar slot pixelado
        auto draw_minicraft_slot = [&](float x, float y, float size, bool selected, Block block, int key_num, int count) {
            // Fundo escuro
            render_quad(x, y, size, size, 0.15f, 0.15f, 0.18f, 0.92f);
            
            // Borda pixelada (3 pixels)
            float border = 3.0f;
            // Borda clara superior/esquerda
            render_quad(x, y, size, border, 0.45f, 0.45f, 0.50f, 0.95f);
            render_quad(x, y, border, size, 0.45f, 0.45f, 0.50f, 0.95f);
            // Borda escura inferior/direita
            render_quad(x, y + size - border, size, border, 0.08f, 0.08f, 0.10f, 0.95f);
            render_quad(x + size - border, y, border, size, 0.08f, 0.08f, 0.10f, 0.95f);
            
            // Highlight se selecionado
            if (selected) {
                render_quad(x - 3.0f, y - 3.0f, size + 6.0f, size + 6.0f, 0.95f, 0.95f, 0.35f, 0.35f);
                render_quad(x + 2.0f, y + 2.0f, size - 4.0f, size - 4.0f, 0.25f, 0.25f, 0.30f, 0.90f);
            }
            
            // Icone do bloco (cubo 3D simples)
            float icon_size = size * 0.55f;
            float ix = x + (size - icon_size) * 0.5f + 2.0f;
            float iy = y + (size - icon_size) * 0.4f;
            if (g_tex_atlas != 0) {
                BlockTex bt = block_tex(block);
                int wf = ((int)std::floor(g_day_time * 4.0f)) & 3;
                if (bt.is_water) {
                    bt.top = (Tile)((int)Tile::Water0 + wf);
                    bt.side = bt.top;
                    bt.bottom = bt.top;
                }
                float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f, alpha = 1.0f;
                if (bt.uses_tint || bt.transparent) {
                    float cr, cg, cb, ca;
                    block_color(block, 128, 256, cr, cg, cb, ca);
                    if (bt.uses_tint) { tint_r = cr; tint_g = cg; tint_b = cb; }
                    if (bt.transparent) alpha = ca;
                }

                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, g_tex_atlas);
                render_quad_tex(ix, iy, icon_size, icon_size * 0.5f, bt.top, tint_r, tint_g, tint_b, 0.98f * alpha);
                render_quad_tex(ix, iy + icon_size * 0.5f, icon_size, icon_size * 0.5f, bt.side,
                                tint_r * 0.75f, tint_g * 0.75f, tint_b * 0.75f, 0.98f * alpha);
                glBindTexture(GL_TEXTURE_2D, 0);
                glDisable(GL_TEXTURE_2D);

                // Linha de divisao
                glLineWidth(1.0f);
                glColor4f(0.0f, 0.0f, 0.0f, 0.5f);
                glBegin(GL_LINES);
                glVertex2f(ix, iy + icon_size * 0.5f);
                glVertex2f(ix + icon_size, iy + icon_size * 0.5f);
                glEnd();
            } else {
                float r, g, bl, a;
                block_color(block, 128, 256, r, g, bl, a);
                // Face superior
                render_quad(ix, iy, icon_size, icon_size * 0.5f, r, g, bl, 0.98f);
                // Face frontal (mais escura)
                render_quad(ix, iy + icon_size * 0.5f, icon_size, icon_size * 0.5f, r * 0.7f, g * 0.7f, bl * 0.7f, 0.98f);
                // Linha de divisao
                glLineWidth(1.0f);
                glColor4f(0.0f, 0.0f, 0.0f, 0.5f);
                glBegin(GL_LINES);
                glVertex2f(ix, iy + icon_size * 0.5f);
                glVertex2f(ix + icon_size, iy + icon_size * 0.5f);
                glEnd();
            }
             
            // Numero da tecla (canto superior esquerdo)
            if (key_num >= 0) {
                draw_text(x + 4.0f, y + 12.0f, std::to_string(key_num), 0.95f, 0.95f, 0.95f, 0.90f);
            }
            
            // Quantidade (canto inferior direito)
            if (count >= 0) {
                std::string cnt = std::to_string(count);
                float tw = estimate_text_w_px(cnt);
                draw_text(x + size - tw - 5.0f, y + size - 5.0f, cnt, 0.95f, 0.95f, 0.95f, 0.95f);
            }
        };
        
        // Slots de recursos (1-6)
        const Block resource_slots[] = {Block::Dirt, Block::Stone, Block::Iron, Block::Copper, Block::Coal, Block::Wood};
        const int res_count = 6;
        
        // Slots de modulos (7+) - apenas desbloqueados
        std::vector<Block> module_slots;
        if (g_unlocks.solar_unlocked) module_slots.push_back(Block::SolarPanel);
        if (g_unlocks.water_extractor_unlocked) module_slots.push_back(Block::WaterExtractor);
        if (g_unlocks.o2_generator_unlocked) module_slots.push_back(Block::OxygenGenerator);
        if (g_unlocks.greenhouse_unlocked) module_slots.push_back(Block::Greenhouse);
        if (g_unlocks.co2_factory_unlocked) module_slots.push_back(Block::CO2Factory);
        if (g_unlocks.habitat_unlocked) module_slots.push_back(Block::Habitat);
        if (g_unlocks.terraformer_unlocked) module_slots.push_back(Block::TerraformerBeacon);
        
        float slot_size = 48.0f;
        float slot_gap = 4.0f;
        
        // === HOTBAR UNIFICADA (centrada na base da tela) ===
        int total_slots = res_count + (int)module_slots.size();
        float total_w = total_slots * slot_size + (total_slots - 1) * slot_gap;
        float hx = win_w * 0.5f - total_w * 0.5f;
        float hy = win_h - slot_size - 12.0f;
        
        // Fundo da hotbar (painel escuro)
        render_quad(hx - 8.0f, hy - 8.0f, total_w + 16.0f, slot_size + 16.0f, 0.08f, 0.08f, 0.10f, 0.75f);
        
        // Desenhar slots de recursos
        for (int i = 0; i < res_count; ++i) {
            bool sel = (g_selected == resource_slots[i]);
            float bx = hx + i * (slot_size + slot_gap);
            int count = std::max(0, g_inventory[(int)resource_slots[i]]);
            draw_minicraft_slot(bx, hy, slot_size, sel, resource_slots[i], i + 1, count);
        }
        
        // Separador visual entre recursos e modulos
        if (!module_slots.empty()) {
            float sep_x = hx + res_count * (slot_size + slot_gap) - slot_gap * 0.5f;
            render_quad(sep_x - 1.0f, hy + 4.0f, 2.0f, slot_size - 8.0f, 0.40f, 0.40f, 0.45f, 0.80f);
        }
        
        // Desenhar slots de modulos
        for (int i = 0; i < (int)module_slots.size(); ++i) {
            bool sel = (g_selected == module_slots[i]);
            float bx = hx + (res_count + i) * (slot_size + slot_gap);
            CraftCost c = module_cost(module_slots[i]);
            bool can_build = can_afford(c);
            int key_num = -1;
            if (i < 4) key_num = (i < 3) ? (7 + i) : 0;
            draw_minicraft_slot(bx, hy, slot_size, sel, module_slots[i], key_num, can_build ? 1 : 0);
        }
        
        // Info do item selecionado (acima da hotbar)
        {
            std::string s = std::string(block_name(g_selected));
            if (is_module(g_selected)) {
                if (!is_unlocked(g_selected)) {
                    s += " [" + unlock_progress_string(g_selected) + "]";
                } else {
                    s += " - " + cost_string(module_cost(g_selected));
                }
            } else {
                s += " x" + std::to_string(std::max(0, g_inventory[(int)g_selected]));
            }
            float tw = estimate_text_w_px(s);
            // Fundo do texto
            render_quad(win_w * 0.5f - tw * 0.5f - 8.0f, hy - 26.0f, tw + 16.0f, 18.0f, 0.0f, 0.0f, 0.0f, 0.65f);
            draw_text(win_w * 0.5f - tw * 0.5f, hy - 12.0f, s, 0.95f, 0.95f, 0.95f, 0.95f);
        }

        // Popups de coleta (feedback acima da hotbar)
        if (!g_collect_popups.empty()) {
            float base_x = win_w * 0.5f;
            float base_y = hy - 42.0f;
            float line_h = 18.0f;

            int n = (int)g_collect_popups.size();
            int max_show = 6;
            int start = std::max(0, n - max_show);

            for (int idx = n - 1; idx >= start; --idx) {
                int stack = (n - 1) - idx;
                const CollectPopup& p = g_collect_popups[idx];

                float alpha = std::min(1.0f, p.life / 0.45f);
                float tw = estimate_text_w_px(p.text);

                bool draw_icon = (g_tex_atlas != 0 && p.item != Block::Air);
                float icon_sz = 16.0f;
                float pad_x = 10.0f;
                float gap = 6.0f;
                float box_w = tw + pad_x * 2.0f + (draw_icon ? (icon_sz + gap) : 0.0f);

                float px = base_x + p.x - box_w * 0.5f;
                float py = base_y + p.y - (float)stack * line_h;

                // Fundo + faixa colorida
                render_quad(px, py - 14.0f, box_w, 18.0f, 0.0f, 0.0f, 0.0f, 0.58f * alpha);
                render_quad(px, py - 14.0f, 3.0f, 18.0f, p.r, p.g, p.b, 0.85f * alpha);

                float tx = px + pad_x;
                if (draw_icon) {
                    BlockTex bt = block_tex(p.item);
                    int wf = ((int)std::floor(g_day_time * 4.0f)) & 3;
                    if (bt.is_water) {
                        bt.top = (Tile)((int)Tile::Water0 + wf);
                        bt.side = bt.top;
                        bt.bottom = bt.top;
                    }

                    float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f, icon_a = 1.0f;
                    if (bt.uses_tint || bt.transparent) {
                        float cr, cg, cb, ca;
                        block_color(p.item, 128, 256, cr, cg, cb, ca);
                        if (bt.uses_tint) { tint_r = cr; tint_g = cg; tint_b = cb; }
                        if (bt.transparent) icon_a = ca;
                    }

                    glEnable(GL_TEXTURE_2D);
                    glBindTexture(GL_TEXTURE_2D, g_tex_atlas);
                    render_quad_tex(tx, py - 12.0f, icon_sz, icon_sz, bt.top, tint_r, tint_g, tint_b, 0.98f * alpha * icon_a);
                    glBindTexture(GL_TEXTURE_2D, 0);
                    glDisable(GL_TEXTURE_2D);

                    tx += icon_sz + gap;
                }

                draw_text(tx, py, p.text, p.r, p.g, p.b, 0.95f * alpha);
            }
        }

        // Target info
        if (g_has_target) {
            Block b = g_world->get(g_target_x, g_target_y);
            if (b != Block::Air) {
                float rr = g_target_in_range ? 0.85f : 0.95f;
                float gg = g_target_in_range ? 0.95f : 0.35f;
                draw_text(20.0f, win_h - 100.0f, std::string("Alvo: ") + block_name(b), rr, gg, 0.25f, 0.95f);
            }
        }

        // Debug info (3D)
        if (g_debug) {
            const char* dir_names[] = {"Norte", "Leste", "Sul", "Oeste"};
            char buf[256];
            snprintf(buf, sizeof(buf), "XZ: %.1f,%.1f  Y: %.2f  Chao: %.1f  %s  Vel: %.1f",
                g_player.pos.x, g_player.pos.y, g_player.pos_y, g_player.ground_height,
                g_player.on_ground ? "NO CHAO" : "NO AR",
                std::sqrt(g_player.vel.x*g_player.vel.x + g_player.vel.y*g_player.vel.y));
            draw_text(20.0f, win_h - 120.0f, buf, 0.85f, 0.85f, 0.90f, 0.95f);
            
            snprintf(buf, sizeof(buf), "VelY: %.2f  Cam: yaw=%.0f pitch=%.0f dist=%.1f",
                g_player.vel_y, g_camera.yaw, g_camera.pitch, g_camera.distance);
            draw_text(20.0f, win_h - 100.0f, buf, 0.85f, 0.85f, 0.90f, 0.95f);
        }
    }

    // Toast notifications
    if (g_toast_time > 0.0f && !g_toast.empty()) {
        float toast_alpha = std::min(1.0f, g_toast_time);
        float tw = estimate_text_w_px(g_toast);
        render_quad(win_w * 0.5f - tw * 0.5f - 10.0f, 50.0f, tw + 20.0f, 28.0f, 0.0f, 0.0f, 0.0f, 0.6f * toast_alpha);
        draw_text(win_w * 0.5f - tw * 0.5f, 70.0f, g_toast, 0.95f, 0.95f, 0.50f, toast_alpha);
    }
    
    // ============= FEEDBACK VISUAL APRIMORADO =============
    
    // Flash vermelho (erro/dano)
    if (g_screen_flash_red > 0.0f) {
        float alpha = g_screen_flash_red * 0.4f;
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 
            kColorDanger[0], kColorDanger[1], kColorDanger[2], alpha);
    }
    
    // Flash verde (sucesso)
    if (g_screen_flash_green > 0.0f) {
        float alpha = g_screen_flash_green * 0.35f;
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 
            kColorSuccess[0], kColorSuccess[1], kColorSuccess[2], alpha);
    }
    
    // Popup grande de desbloqueio (conquista)
    if (g_unlock_popup_timer > 0.0f) {
        float alpha = std::min(1.0f, g_unlock_popup_timer);
        float popup_w = 380.0f;
        float popup_h = 100.0f;
        float px = win_w * 0.5f - popup_w * 0.5f;
        float py = win_h * 0.25f;
        
        // Fundo com borda verde
        render_quad(px - 4.0f, py - 4.0f, popup_w + 8.0f, popup_h + 8.0f, 
            kColorSuccess[0], kColorSuccess[1], kColorSuccess[2], 0.9f * alpha);
        render_quad(px, py, popup_w, popup_h, 0.05f, 0.08f, 0.05f, 0.95f * alpha);
        
        // Titulo
        float tw = estimate_text_w_px(g_unlock_popup_text);
        draw_text(win_w * 0.5f - tw * 0.5f, py + 35.0f, g_unlock_popup_text, 
            kColorSuccess[0], kColorSuccess[1], kColorSuccess[2], alpha);
        
        // Subtitulo
        float sw = estimate_text_w_px(g_unlock_popup_subtitle);
        draw_text(win_w * 0.5f - sw * 0.5f, py + 65.0f, g_unlock_popup_subtitle, 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], alpha * 0.9f);
    }
    
    // Dica de onboarding
    if (g_onboarding.tip_timer > 0.0f && !g_onboarding.current_tip.empty()) {
        float alpha = std::min(1.0f, g_onboarding.tip_timer);
        float tw = estimate_text_w_px(g_onboarding.current_tip);
        float tip_y = win_h * 0.15f;
        
        // Fundo azul suave
        render_quad(win_w * 0.5f - tw * 0.5f - 15.0f, tip_y - 10.0f, tw + 30.0f, 35.0f, 
            kColorSelection[0] * 0.3f, kColorSelection[1] * 0.3f, kColorSelection[2] * 0.3f, 0.85f * alpha);
        render_quad(win_w * 0.5f - tw * 0.5f - 15.0f, tip_y - 10.0f, 4.0f, 35.0f, 
            kColorSelection[0], kColorSelection[1], kColorSelection[2], 0.95f * alpha);
        
        draw_text(win_w * 0.5f - tw * 0.5f, tip_y + 10.0f, g_onboarding.current_tip, 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], alpha);
    }

    // Overlays
    if (g_state == GameState::Paused || g_state == GameState::Menu) {
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.0f, 0.0f, 0.0f, g_state == GameState::Paused ? 0.45f : 0.70f);
        if (g_state == GameState::Paused) {
            std::string title = "PAUSADO";
            draw_text(win_w * 0.5f - estimate_text_w_px(title) * 0.5f, win_h * 0.30f, title, 0.95f, 0.95f, 0.95f, 0.98f);
            draw_text(win_w * 0.5f - 100.0f, win_h * 0.30f + 45.0f, "Esc - Continuar", 0.90f, 0.90f, 0.90f, 0.95f);
            draw_text(win_w * 0.5f - 100.0f, win_h * 0.30f + 70.0f, "F5 - Salvar Jogo", 0.90f, 0.90f, 0.90f, 0.95f);
            draw_text(win_w * 0.5f - 100.0f, win_h * 0.30f + 95.0f, "F9 - Carregar Jogo", 0.90f, 0.90f, 0.90f, 0.95f);
            draw_text(win_w * 0.5f - 100.0f, win_h * 0.30f + 120.0f, "O - Configuracoes", 0.90f, 0.90f, 0.90f, 0.95f);
            draw_text(win_w * 0.5f - 100.0f, win_h * 0.30f + 145.0f, "Q - Novo Jogo", 0.90f, 0.90f, 0.90f, 0.95f);
            draw_text(win_w * 0.5f - 100.0f, win_h * 0.30f + 185.0f, "CONTROLES:", 0.75f, 0.80f, 0.90f, 0.90f);
            draw_text(win_w * 0.5f - 100.0f, win_h * 0.30f + 205.0f, "WASD - Mover (relativo a camera)", 0.70f, 0.70f, 0.75f, 0.85f);
            draw_text(win_w * 0.5f - 100.0f, win_h * 0.30f + 223.0f, "Espaco - Pular", 0.70f, 0.70f, 0.75f, 0.85f);
            draw_text(win_w * 0.5f - 100.0f, win_h * 0.30f + 241.0f, "Shift - Correr", 0.70f, 0.70f, 0.75f, 0.85f);
            draw_text(win_w * 0.5f - 100.0f, win_h * 0.30f + 259.0f, "Botao Direito - Rotacionar Camera", 0.70f, 0.70f, 0.75f, 0.85f);
            draw_text(win_w * 0.5f - 100.0f, win_h * 0.30f + 277.0f, "Scroll - Zoom", 0.70f, 0.70f, 0.75f, 0.85f);
            draw_text(win_w * 0.5f - 100.0f, win_h * 0.30f + 295.0f, "Botao Esquerdo - Minerar/Construir", 0.70f, 0.70f, 0.75f, 0.85f);
            draw_text(win_w * 0.5f - 100.0f, win_h * 0.30f + 313.0f, "1-9 - Selecionar Item", 0.70f, 0.70f, 0.75f, 0.85f);
        } else if (g_state == GameState::Menu) {
            std::string title = "TerraFormer 3D";
            draw_text(win_w * 0.5f - estimate_text_w_px(title) * 0.5f, win_h * 0.30f, title, 0.95f, 0.95f, 0.95f, 0.98f);
            draw_text(win_w * 0.5f - 120.0f, win_h * 0.30f + 60.0f, "Enter - Novo Jogo", 0.90f, 0.90f, 0.90f, 0.95f);
            draw_text(win_w * 0.5f - 120.0f, win_h * 0.30f + 85.0f, "L / F9 - Carregar Jogo", 0.90f, 0.90f, 0.90f, 0.95f);
            draw_text(win_w * 0.5f - 120.0f, win_h * 0.30f + 110.0f, "Esc - Sair", 0.90f, 0.90f, 0.90f, 0.95f);
            draw_text(win_w * 0.5f - 180.0f, win_h * 0.30f + 160.0f, "Colete recursos, construa modulos,", 0.70f, 0.75f, 0.80f, 0.90f);
            draw_text(win_w * 0.5f - 180.0f, win_h * 0.30f + 180.0f, "e terraforma o planeta!", 0.70f, 0.75f, 0.80f, 0.90f);
        }
    }
    
    // Death screen
    if (g_state == GameState::Dead) {
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.15f, 0.0f, 0.0f, 0.75f);
        std::string title = "VOCE MORREU";
        draw_text(win_w * 0.5f - estimate_text_w_px(title) * 0.5f, win_h * 0.35f, title, 0.95f, 0.25f, 0.25f, 0.98f);
        draw_text(win_w * 0.5f - estimate_text_w_px(g_toast) * 0.5f, win_h * 0.35f + 40.0f, g_toast, 0.90f, 0.90f, 0.90f, 0.95f);
        draw_text(win_w * 0.5f - 100.0f, win_h * 0.35f + 90.0f, "Enter - Novo Jogo", 0.90f, 0.90f, 0.90f, 0.95f);
        draw_text(win_w * 0.5f - 100.0f, win_h * 0.35f + 115.0f, "Esc - Menu Principal", 0.90f, 0.90f, 0.90f, 0.95f);
    }
    
    // Settings menu
    if (g_state == GameState::Settings) {
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.0f, 0.0f, 0.0f, 0.85f);
        
        float menu_w = 450.0f;
        float menu_h = 350.0f;
        float menu_x = win_w * 0.5f - menu_w * 0.5f;
        float menu_y = win_h * 0.5f - menu_h * 0.5f;
        
        // Background panel
        render_quad(menu_x, menu_y, menu_w, menu_h, 0.08f, 0.10f, 0.14f, 0.98f);
        render_quad(menu_x, menu_y, menu_w, 4.0f, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        render_quad(menu_x, menu_y + menu_h - 4.0f, menu_w, 4.0f, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        render_quad(menu_x, menu_y, 4.0f, menu_h, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        render_quad(menu_x + menu_w - 4.0f, menu_y, 4.0f, menu_h, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        
        std::string title = "CONFIGURACOES";
        draw_text(win_w * 0.5f - estimate_text_w_px(title) * 0.5f, menu_y + 25.0f, title, 0.95f, 0.95f, 0.95f, 1.0f);
        
        float row_y = menu_y + 70.0f;
        float row_h = 40.0f;
        float label_x = menu_x + 30.0f;
        float value_x = menu_x + 280.0f;
        
        // Opcao: Sensibilidade da Camera
        bool sel0 = (g_settings_selection == 0);
        if (sel0) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Sensibilidade Camera", sel0 ? 1.0f : 0.8f, sel0 ? 1.0f : 0.8f, sel0 ? 1.0f : 0.8f, 1.0f);
        char sens_buf[32];
        snprintf(sens_buf, sizeof(sens_buf), "< %.2f >", g_settings.camera_sensitivity);
        draw_text(value_x, row_y + 5.0f, sens_buf, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;
        
        // Opcao: Inverter Y
        bool sel1 = (g_settings_selection == 1);
        if (sel1) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Inverter Eixo Y", sel1 ? 1.0f : 0.8f, sel1 ? 1.0f : 0.8f, sel1 ? 1.0f : 0.8f, 1.0f);
        const char* invert_str = g_settings.invert_y ? "Sim" : "Nao";
        draw_text(value_x, row_y + 5.0f, invert_str, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;
        
        // Opcao: Brilho
        bool sel2 = (g_settings_selection == 2);
        if (sel2) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Brilho", sel2 ? 1.0f : 0.8f, sel2 ? 1.0f : 0.8f, sel2 ? 1.0f : 0.8f, 1.0f);
        char bright_buf[32];
        snprintf(bright_buf, sizeof(bright_buf), "< %.0f%% >", g_settings.brightness * 100.0f);
        draw_text(value_x, row_y + 5.0f, bright_buf, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;
        
        // Opcao: Escala UI
        bool sel3 = (g_settings_selection == 3);
        if (sel3) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Escala UI", sel3 ? 1.0f : 0.8f, sel3 ? 1.0f : 0.8f, sel3 ? 1.0f : 0.8f, 1.0f);
        char scale_buf[32];
        snprintf(scale_buf, sizeof(scale_buf), "< %.0f%% >", g_settings.ui_scale * 100.0f);
        draw_text(value_x, row_y + 5.0f, scale_buf, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;
        
        // Opcao: Voltar
        bool sel4 = (g_settings_selection == 4);
        if (sel4) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Voltar", sel4 ? 1.0f : 0.8f, sel4 ? 1.0f : 0.8f, sel4 ? 1.0f : 0.8f, 1.0f);
        
        // Instrucoes
        draw_text(menu_x + 30.0f, menu_y + menu_h - 40.0f, "W/S: Navegar | A/D: Ajustar | Esc/Enter: Voltar", 0.6f, 0.65f, 0.70f, 0.9f);
    }

    if (g_victory) {
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.0f, 0.0f, 0.0f, 0.18f);
        std::string t2 = "Terraformacao Completa!";
        draw_text(win_w * 0.5f - estimate_text_w_px(t2) * 0.5f, win_h * 0.20f, t2, 0.85f, 0.95f, 0.85f, 0.98f);
    }
    
    // ============= BUILD MENU =============
    if (g_show_build_menu && g_state == GameState::Playing) {
        float menu_w = 850.0f;
        float menu_h = 650.0f;
        float menu_x = win_w * 0.5f - menu_w * 0.5f;
        float menu_y = win_h * 0.5f - menu_h * 0.5f;
        
        // Background
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.0f, 0.0f, 0.0f, 0.70f);
        render_quad(menu_x, menu_y, menu_w, menu_h, 0.05f, 0.07f, 0.10f, 0.98f);
        
        // Border
        render_quad(menu_x, menu_y, menu_w, 3.0f, 0.30f, 0.55f, 0.85f, 1.0f);
        render_quad(menu_x, menu_y + menu_h - 3.0f, menu_w, 3.0f, 0.30f, 0.55f, 0.85f, 1.0f);
        render_quad(menu_x, menu_y, 3.0f, menu_h, 0.30f, 0.55f, 0.85f, 1.0f);
        render_quad(menu_x + menu_w - 3.0f, menu_y, 3.0f, menu_h, 0.30f, 0.55f, 0.85f, 1.0f);
        
        // Title
        std::string title = "MENU DE CONSTRUCAO";
        draw_text(menu_x + menu_w * 0.5f - estimate_text_w_px(title) * 0.5f, menu_y + 25.0f, title, 0.95f, 0.95f, 0.95f, 1.0f);
        draw_text(menu_x + menu_w * 0.5f - 150.0f, menu_y + 45.0f, "Tab/B: Fechar  |  W/S: Selecionar  |  Enter: Construir", 0.55f, 0.60f, 0.70f, 0.85f);
        
        // Module types available
        Block module_types[] = {
            Block::SolarPanel,
            Block::EnergyGenerator,
            Block::OxygenGenerator,
            Block::WaterExtractor,
            Block::Greenhouse,
            Block::Workshop,
            Block::CO2Factory,
            Block::Habitat,
            Block::TerraformerBeacon,
        };
        const int module_count = 9;
        
        // Clamp selection
        if (g_build_menu_selection < 0) g_build_menu_selection = 0;
        if (g_build_menu_selection >= module_count) g_build_menu_selection = module_count - 1;
        
        float list_x = menu_x + 15.0f;
        float list_y = menu_y + 65.0f;
        float row_h = 58.0f;
        float list_w = menu_w - 250.0f;
        
        for (int i = 0; i < module_count; ++i) {
            Block mtype = module_types[i];
            ModuleStats stats = get_module_stats(mtype);
            CraftCost cost = get_module_cost(mtype);
            bool affordable = can_afford(cost);
            bool selected = (i == g_build_menu_selection);
            
            // Check if under construction
            bool building = false;
            float build_progress = 0.0f;
            for (const auto& job : g_construction_queue) {
                if (job.active && job.module_type == mtype) {
                    building = true;
                    build_progress = 1.0f - (job.time_remaining / job.total_time);
                    break;
                }
            }
            
            // Count how many of this module we have
            int count = 0;
            for (const auto& mod : g_modules) {
                if (mod.type == mtype) count++;
            }
            
            // Determine status
            const char* status_str;
            float stat_r, stat_g, stat_b;
            if (building) {
                status_str = "CONSTRUINDO";
                stat_r = 0.95f; stat_g = 0.75f; stat_b = 0.20f;
            } else if (affordable) {
                status_str = "DISPONIVEL";
                stat_r = 0.30f; stat_g = 0.90f; stat_b = 0.40f;
            } else {
                status_str = "BLOQUEADO";
                stat_r = 0.80f; stat_g = 0.40f; stat_b = 0.35f;
            }
            
            // Row background
            float bg_alpha = selected ? 0.40f : 0.15f;
            float bg_r = selected ? 0.12f : 0.08f;
            float bg_g = selected ? 0.22f : 0.10f;
            float bg_b = selected ? 0.38f : 0.15f;
            render_quad(list_x, list_y, list_w, row_h - 3.0f, bg_r, bg_g, bg_b, bg_alpha);
            
            // Selection indicator
            if (selected) {
                render_quad(list_x, list_y, 4.0f, row_h - 3.0f, 0.35f, 0.75f, 0.95f, 1.0f);
            }
            
            // Build progress bar if building
            if (building) {
                render_quad(list_x + 4.0f, list_y + row_h - 8.0f, (list_w - 8.0f) * build_progress, 4.0f, 0.30f, 0.80f, 0.50f, 0.90f);
            }
            
            // Module name and count
            float name_r = affordable ? 0.95f : 0.60f;
            float name_g = affordable ? 0.95f : 0.60f;
            float name_b = affordable ? 0.95f : 0.65f;
            std::string name_str = std::string(stats.name);
            if (count > 0) name_str += " [" + std::to_string(count) + " ativo]";
            draw_text(list_x + 12.0f, list_y + 16.0f, name_str, name_r, name_g, name_b, 1.0f);
            
            // Description
            draw_text(list_x + 12.0f, list_y + 32.0f, stats.description, 0.55f, 0.60f, 0.70f, 0.80f);
            
            // Production/Consumption info
            std::string prod_str;
            if (stats.energy_production > 0.0f) prod_str += "+" + std::to_string((int)stats.energy_production) + " Energia/min ";
            if (stats.oxygen_production > 0.0f) prod_str += "+" + std::to_string((int)(stats.oxygen_production*10)/10.0f).substr(0,3) + " O2/min ";
            if (stats.water_production > 0.0f) prod_str += "+" + std::to_string((int)(stats.water_production*10)/10.0f).substr(0,3) + " Agua/min ";
            if (stats.food_production > 0.0f) prod_str += "+" + std::to_string((int)(stats.food_production*10)/10.0f).substr(0,3) + " Comida/min ";
            if (stats.integrity_bonus > 0.0f) prod_str += "+" + std::to_string((int)stats.integrity_bonus) + " Reparo/min ";
            if (prod_str.empty()) prod_str = "Terraformacao";
            
            std::string cons_str;
            if (stats.energy_consumption > 0.0f) cons_str = "-" + std::to_string((int)(stats.energy_consumption*10)/10.0f).substr(0,3) + " Energia/min";
            
            draw_text(list_x + 220.0f, list_y + 16.0f, prod_str, 0.35f, 0.80f, 0.45f, 0.85f);
            if (!cons_str.empty()) {
                draw_text(list_x + 220.0f, list_y + 32.0f, cons_str, 0.85f, 0.55f, 0.35f, 0.80f);
            }
            
            // Status
            draw_text(list_x + list_w - 95.0f, list_y + 16.0f, status_str, stat_r, stat_g, stat_b, 0.95f);
            
            // Cost
            std::string cost_str = module_cost_string(cost);
            float cost_r = affordable ? 0.50f : 0.75f;
            float cost_g = affordable ? 0.80f : 0.50f;
            float cost_b = affordable ? 0.55f : 0.45f;
            draw_text(list_x + 12.0f, list_y + 46.0f, cost_str, cost_r, cost_g, cost_b, 0.75f);
            
            // Construction time
            std::string time_str = "Tempo: " + std::to_string((int)stats.construction_time) + "s";
            draw_text(list_x + list_w - 95.0f, list_y + 32.0f, time_str, 0.60f, 0.65f, 0.70f, 0.75f);
            
            list_y += row_h;
        }
        
        // === RIGHT SIDE: BASE STATUS ===
        float status_x = menu_x + menu_w - 225.0f;
        float status_y = menu_y + 65.0f;
        
        render_quad(status_x - 5.0f, status_y - 5.0f, 220.0f, 250.0f, 0.08f, 0.10f, 0.14f, 0.90f);
        draw_text(status_x + 55.0f, status_y + 12.0f, "STATUS DA BASE", 0.85f, 0.90f, 0.95f, 0.95f);
        status_y += 30.0f;
        
        // Base resources with detailed bars
        auto draw_status_bar = [&](const char* label, float value, float max_val, float r, float g, float b) {
            float pct = std::clamp(value / max_val, 0.0f, 1.0f);
            render_quad(status_x, status_y, 200.0f, 18.0f, 0.12f, 0.12f, 0.18f, 0.85f);
            render_quad(status_x + 1.0f, status_y + 1.0f, 198.0f * pct, 16.0f, r, g, b, 0.90f);
            std::string txt = std::string(label) + ": " + std::to_string((int)value) + "/" + std::to_string((int)max_val);
            draw_text(status_x + 5.0f, status_y + 13.0f, txt, 0.95f, 0.95f, 0.95f, 0.98f);
            status_y += 24.0f;
        };
        
        draw_status_bar("Energia", g_base_energy, kBaseEnergyMax, 0.95f, 0.80f, 0.20f);
        draw_status_bar("Agua", g_base_water, kBaseWaterMax, 0.25f, 0.60f, 0.95f);
        draw_status_bar("Oxigenio", g_base_oxygen, kBaseOxygenMax, 0.25f, 0.90f, 0.50f);
        draw_status_bar("Comida", g_base_food, kBaseFoodMax, 0.85f, 0.60f, 0.25f);
        
        // Integrity bar
        float int_r = g_base_integrity > 50.0f ? 0.30f : (g_base_integrity > 25.0f ? 0.90f : 0.95f);
        float int_g = g_base_integrity > 50.0f ? 0.85f : (g_base_integrity > 25.0f ? 0.70f : 0.30f);
        float int_b = g_base_integrity > 50.0f ? 0.40f : 0.20f;
        draw_status_bar("Integridade", g_base_integrity, kBaseIntegrityMax, int_r, int_g, int_b);
        
        // Consumption info
        status_y += 10.0f;
        draw_text(status_x, status_y, "CONSUMO CONSTANTE:", 0.70f, 0.75f, 0.85f, 0.80f);
        status_y += 18.0f;
        draw_text(status_x, status_y, "-1 O2/min  -2 Energia/min  -1 Agua/min", 0.85f, 0.55f, 0.45f, 0.75f);
        
        // === BOTTOM: INVENTORY ===
        float bottom_y = menu_y + menu_h - 90.0f;
        render_quad(menu_x + 10.0f, bottom_y, menu_w - 20.0f, 80.0f, 0.08f, 0.10f, 0.14f, 0.90f);
        draw_text(menu_x + 20.0f, bottom_y + 15.0f, "SEU INVENTARIO:", 0.80f, 0.85f, 0.95f, 0.92f);
        
        std::string res_line1 = 
            "Pedra: " + std::to_string(g_inventory[(int)Block::Stone]) + 
            "   Ferro: " + std::to_string(g_inventory[(int)Block::Iron]) + 
            "   Cobre: " + std::to_string(g_inventory[(int)Block::Copper]) +
            "   Gelo: " + std::to_string(g_inventory[(int)Block::Ice]);
        std::string res_line2 = 
            "Carvao: " + std::to_string(g_inventory[(int)Block::Coal]) + 
            "   Cristal: " + std::to_string(g_inventory[(int)Block::Crystal]) +
            "   Metal: " + std::to_string(g_inventory[(int)Block::Metal]) +
            "   Organico: " + std::to_string(g_inventory[(int)Block::Organic]) +
            "   Comp: " + std::to_string(g_inventory[(int)Block::Components]);
        draw_text(menu_x + 20.0f, bottom_y + 38.0f, res_line1, 0.90f, 0.92f, 0.95f, 0.95f);
        draw_text(menu_x + 20.0f, bottom_y + 58.0f, res_line2, 0.90f, 0.92f, 0.95f, 0.95f);
    }
    
    // ============= ALERTS DISPLAY =============
    if (!g_alerts.empty() && g_state == GameState::Playing && !g_show_build_menu) {
        float alert_y = 150.0f;
        for (const auto& alert : g_alerts) {
            float alpha = std::min(1.0f, alert.time_remaining);
            float alert_w = estimate_text_w_px(alert.message) + 30.0f;
            float alert_x = win_w - alert_w - 20.0f;
            
            render_quad(alert_x, alert_y, alert_w, 28.0f, alert.r * 0.3f, alert.g * 0.3f, alert.b * 0.3f, 0.85f * alpha);
            render_quad(alert_x, alert_y, 4.0f, 28.0f, alert.r, alert.g, alert.b, alpha);
            draw_text(alert_x + 15.0f, alert_y + 19.0f, alert.message, alert.r, alert.g, alert.b, alpha);
            
            alert_y += 35.0f;
        }
    }

    SwapBuffers(hdc);
}

// ============= Input State =============
static bool key_down(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static bool key_pressed(int vk, bool& prev) {
    bool cur = key_down(vk);
    bool pressed = cur && !prev;
    prev = cur;
    return pressed;
}

// ============= Update =============
static void update_game(float dt, HWND hwnd) {
    if (!g_world) return;

    // Toast timer
    if (g_toast_time > 0.0f) g_toast_time -= dt;
    
    // ============= ATUALIZAR FEEDBACK VISUAL =============
    if (g_screen_flash_red > 0.0f) g_screen_flash_red -= dt * 2.5f;
    if (g_screen_flash_green > 0.0f) g_screen_flash_green -= dt * 2.5f;
    if (g_unlock_popup_timer > 0.0f) g_unlock_popup_timer -= dt;
    if (g_hotbar_bounce > 0.0f) g_hotbar_bounce -= dt * 4.0f;
    
    // Atualizar popups de coleta
    for (auto& popup : g_collect_popups) {
        popup.life -= dt;
        popup.y -= dt * 30.0f;  // Flutua para cima
    }
    g_collect_popups.erase(
        std::remove_if(g_collect_popups.begin(), g_collect_popups.end(),
            [](const CollectPopup& p) { return p.life <= 0.0f; }),
        g_collect_popups.end());
    
    // Atualizar onboarding
    update_onboarding(dt);

    // Stats timer (periodically recompute terraform score)
    g_stats_timer += dt;
    if (g_stats_timer >= 2.0f || g_surface_dirty) {
        g_stats_timer = 0.0f;
        g_surface_dirty = false;
        recompute_terraform_score(*g_world);
    }

    // Hotkey states
    bool esc_pressed = key_pressed(VK_ESCAPE, g_prev_esc);
    bool enter_pressed = key_pressed(VK_RETURN, g_prev_enter);
    bool f5_pressed = key_pressed(VK_F5, g_prev_f5);
    bool f9_pressed = key_pressed(VK_F9, g_prev_f9);
    bool l_pressed = key_pressed('L', g_prev_l);
    bool q_pressed = key_pressed('Q', g_prev_q);
    bool f3_pressed = key_pressed(VK_F3, g_prev_f3);
    bool h_pressed = key_pressed('H', g_prev_h);
    bool tab_pressed = key_pressed(VK_TAB, g_prev_tab);
    bool b_pressed = key_pressed('B', g_prev_b);

    if (f3_pressed) g_debug = !g_debug;

    // State machine
    if (g_state == GameState::Menu) {
        if (esc_pressed) {
            g_quit = true;
            return;
        }
        if (enter_pressed) {
            delete g_world;
            g_world = new World(WORLD_WIDTH, WORLD_HEIGHT, (unsigned)GetTickCount());
            spawn_player_new_game(*g_world);  // This sets O2, water, etc. to 100%
            g_cam_pos = g_player.pos;
            g_day_time = kDayLength * 0.25f;  // Start at morning
            g_modules.clear();
            g_particles.clear();
            g_construction_queue.clear();
            g_alerts.clear();
            g_build_slots.clear();
            g_collect_popups.clear();
            g_drops.clear();
            
            // Reset onboarding para novo jogo
            g_onboarding = OnboardingState();
            
            g_state = GameState::Playing;
            
            // Dica inicial de onboarding
            show_tip("WASD para mover, Espaco para pular, Botao direito para girar camera", g_onboarding.shown_first_move);
            return;
        }
        if (l_pressed || f9_pressed) {
            if (load_game(kSavePath)) {
                set_toast("Jogo carregado!");
                g_state = GameState::Playing;
            } else {
                set_toast("Nenhum save encontrado.");
            }
            return;
        }
        return;
    }

    if (g_state == GameState::Paused) {
        if (esc_pressed) {
            g_state = GameState::Playing;
            return;
        }
        if (q_pressed) {
            g_state = GameState::Menu;
            return;
        }
        // Tecla 'O' abre configuracoes
        if (GetAsyncKeyState('O') & 0x8000) {
            static bool o_was_pressed = false;
            if (!o_was_pressed) {
                g_state = GameState::Settings;
                g_settings_selection = 0;
                o_was_pressed = true;
            }
        } else {
            static bool o_was_pressed = false;
            o_was_pressed = false;
        }
        if (f5_pressed) {
            if (save_game(kSavePath)) set_toast("Jogo salvo!");
            else set_toast("Falha ao salvar!");
            return;
        }
        if (f9_pressed) {
            if (load_game(kSavePath)) {
                set_toast("Jogo carregado!");
                g_state = GameState::Playing;
            } else {
                set_toast("Falha ao carregar!");
            }
            return;
        }
        return;
    }
    
    // Menu de configuracoes
    if (g_state == GameState::Settings) {
        static bool key_w_held = false;
        static bool key_s_held = false;
        static bool key_a_held = false;
        static bool key_d_held = false;
        
        bool w_now = (GetAsyncKeyState('W') & 0x8000) != 0;
        bool s_now = (GetAsyncKeyState('S') & 0x8000) != 0;
        bool a_now = (GetAsyncKeyState('A') & 0x8000) != 0;
        bool d_now = (GetAsyncKeyState('D') & 0x8000) != 0;
        
        // Navegar para cima
        if (w_now && !key_w_held) {
            g_settings_selection = (g_settings_selection - 1 + 5) % 5;
        }
        key_w_held = w_now;
        
        // Navegar para baixo
        if (s_now && !key_s_held) {
            g_settings_selection = (g_settings_selection + 1) % 5;
        }
        key_s_held = s_now;
        
        // Ajustar valores
        float delta = 0.0f;
        if (a_now && !key_a_held) delta = -1.0f;
        if (d_now && !key_d_held) delta = 1.0f;
        key_a_held = a_now;
        key_d_held = d_now;
        
        if (delta != 0.0f) {
            switch (g_settings_selection) {
                case 0: // Sensibilidade
                    g_settings.camera_sensitivity = std::clamp(g_settings.camera_sensitivity + delta * 0.02f, 0.05f, 0.5f);
                    g_camera.sensitivity = g_settings.camera_sensitivity;
                    break;
                case 1: // Inverter Y
                    g_settings.invert_y = !g_settings.invert_y;
                    break;
                case 2: // Brilho
                    g_settings.brightness = std::clamp(g_settings.brightness + delta * 0.1f, 0.5f, 1.5f);
                    break;
                case 3: // Escala UI
                    g_settings.ui_scale = std::clamp(g_settings.ui_scale + delta * 0.1f, 0.75f, 1.5f);
                    break;
                case 4: // Voltar
                    if (delta != 0.0f) {
                        g_state = GameState::Paused;
                    }
                    break;
            }
        }
        
        // ESC ou Enter no "Voltar" fecha o menu
        if (esc_pressed || (enter_pressed && g_settings_selection == 4)) {
            g_state = GameState::Paused;
            return;
        }
        return;
    }
    
    if (g_state == GameState::Dead) {
        // Death screen - wait for Enter to start new game
        if (enter_pressed) {
            delete g_world;
            g_world = new World(WORLD_WIDTH, WORLD_HEIGHT, (unsigned)GetTickCount());
            spawn_player_new_game(*g_world);
            g_cam_pos = g_player.pos;
            g_day_time = kDayLength * 0.25f;
            g_modules.clear();
            g_particles.clear();
            g_drops.clear();
            g_construction_queue.clear();
            g_alerts.clear();
            g_build_slots.clear();
            g_state = GameState::Playing;
            set_toast("Novo jogo!");
            return;
        }
        if (esc_pressed) {
            g_state = GameState::Menu;
            return;
        }
        return;
    }

    // Playing state
    
    // ESC fecha menu de construcao ou pausa o jogo
    if (esc_pressed) {
        if (g_show_build_menu) {
            g_show_build_menu = false;  // ESC fecha menu de construcao
            return;
        }
        g_state = GameState::Paused;
        return;
    }
    
    // Toggle build menu with Tab or B
    if (tab_pressed || b_pressed) {
        g_show_build_menu = !g_show_build_menu;
        if (g_show_build_menu) {
            g_build_menu_selection = 0;
            // Onboarding: dica ao abrir menu de construcao pela primeira vez
            if (!g_onboarding.shown_first_build_menu) {
                show_tip("W/S para navegar, Enter para construir, ESC para fechar", g_onboarding.shown_first_build_menu);
            }
        }
        return;
    }
    
    // Build menu navigation and actions
    if (g_show_build_menu) {
        static bool prev_w = false, prev_s = false, prev_enter = false;
        bool w_now = key_down('W') || key_down(VK_UP);
        bool s_now = key_down('S') || key_down(VK_DOWN);
        bool enter_now = key_down(VK_RETURN);
        
        // Module types list (matches render order)
        const Block module_types[] = {
            Block::SolarPanel, Block::EnergyGenerator, Block::OxygenGenerator,
            Block::WaterExtractor, Block::Greenhouse, Block::Workshop,
            Block::CO2Factory, Block::Habitat, Block::TerraformerBeacon
        };
        const int module_count = 9;
        
        // Navigate up (W ou seta para cima)
        if (w_now && !prev_w) {
            g_build_menu_selection--;
            if (g_build_menu_selection < 0) 
                g_build_menu_selection = module_count - 1;
            bounce_hotbar_slot(g_build_menu_selection);  // Feedback visual
        }
        // Navigate down (S ou seta para baixo)
        if (s_now && !prev_s) {
            g_build_menu_selection++;
            if (g_build_menu_selection >= module_count) 
                g_build_menu_selection = 0;
            bounce_hotbar_slot(g_build_menu_selection);  // Feedback visual
        }
        // Build action with construction time
        if (enter_now && !prev_enter && g_build_menu_selection >= 0 && 
            g_build_menu_selection < module_count) {
            
            Block module_type = module_types[g_build_menu_selection];
            CraftCost cost = get_module_cost(module_type);
            
            // Check if already under construction
            bool already_building = false;
            for (const auto& job : g_construction_queue) {
                if (job.active && job.module_type == module_type) {
                    already_building = true;
                    break;
                }
            }
            
            if (already_building) {
                show_error("Ja em construcao!");
            } else if (can_afford(cost)) {
                // Find a free slot for this module (or create at base)
                int slot_index = -1;
                
                // Try to find an empty slot
                for (int si = 0; si < (int)g_build_slots.size(); ++si) {
                    if (g_build_slots[si].assigned_module == Block::Air) {
                        slot_index = si;
                        break;
                    }
                }
                
                // If no slot, create a new one near base
                if (slot_index < 0) {
                    // Find empty spot near base
                    for (int dx = -30; dx <= 30; ++dx) {
                        int tx = g_base_x + dx;
                        if (tx < 0 || tx >= g_world->w) continue;
                        int ty = g_base_y - 1;
                        Block current = g_world->get(tx, ty);
                        if (current == Block::Air || current == Block::BuildSlot) {
                            BuildSlotInfo new_slot;
                            new_slot.x = tx;
                            new_slot.y = ty;
                            new_slot.assigned_module = Block::Air;
                            new_slot.label = "Auto";
                            g_build_slots.push_back(new_slot);
                            slot_index = (int)g_build_slots.size() - 1;
                            break;
                        }
                    }
                }
                
                if (slot_index >= 0) {
                    // Start construction (will take time!)
                    start_construction(module_type, slot_index);
                    g_build_slots[slot_index].assigned_module = module_type;
                } else {
                    show_error("Sem espaco para construir!");
                }
            } else {
                show_error("Recursos insuficientes!");
            }
        }
        
        prev_w = w_now;
        prev_s = s_now;
        prev_enter = enter_now;
        return;  // Don't process other inputs while in menu
    }
    
    // Return to base with H
    if (h_pressed) {
        spawn_player_at_base();
        set_toast("Retornou a base!");
        return;
    }

    // Update modules (energy/water/oxygen production, terraforming)
    update_modules(*g_world, dt);

    // Hotbar selection
    // Resources: 1-6
    const Block resource_slots[] = {Block::Dirt, Block::Stone, Block::Iron, Block::Copper, Block::Coal, Block::Wood};
    for (int i = 0; i < 6; ++i) {
        if (key_down('1' + i)) g_selected = resource_slots[i];
    }
    
    // Modules: 7-0 (dynamically based on unlocks)
    std::vector<Block> module_slots;
    if (g_unlocks.solar_unlocked) module_slots.push_back(Block::SolarPanel);
    if (g_unlocks.water_extractor_unlocked) module_slots.push_back(Block::WaterExtractor);
    if (g_unlocks.o2_generator_unlocked) module_slots.push_back(Block::OxygenGenerator);
    if (g_unlocks.greenhouse_unlocked) module_slots.push_back(Block::Greenhouse);
    if (g_unlocks.co2_factory_unlocked) module_slots.push_back(Block::CO2Factory);
    if (g_unlocks.habitat_unlocked) module_slots.push_back(Block::Habitat);
    if (g_unlocks.terraformer_unlocked) module_slots.push_back(Block::TerraformerBeacon);
    
    for (int i = 0; i < (int)module_slots.size() && i < 4; ++i) {
        int key = (i < 3) ? ('7' + i) : '0';
        if (key_down(key)) g_selected = module_slots[i];
    }

    // ============= MOVIMENTO 3D COM PULO E GRAVIDADE =============
    
    // Constantes de fisica
    const float kGravity = 20.0f;       // Gravidade (tiles/s^2)
    const float kJumpForce = 7.0f;      // Forca do pulo
    const float kMoveSpeed = 4.5f;      // Velocidade base
    const float kRunMult = 1.5f;        // Multiplicador de corrida
    const float kAirControl = 0.6f;     // Controle no ar
    const float kAcceleration = 15.0f;  // Aceleracao no chao
    const float kDeceleration = 12.0f;  // Desaceleracao
    // Altura maxima de step climbing (pouco mais que 1 bloco) = kPlayerStepHeight
    
    // === CALCULAR DIRECOES DA CAMERA ===
    // A camera aponta PARA o jogador. Usamos o vetor camera->jogador (projetado no chao)
    // como direcao de "frente" para o movimento (W).
    float cam_yaw_rad = g_camera.yaw * (kPi / 180.0f);
    
    // Vetor da camera para o jogador (projetado no chao) = "frente" para o jogador.
    // (Sem isso, W/S ficam invertidos dependendo da yaw.)
    float cam_forward_x = -std::sin(cam_yaw_rad);
    float cam_forward_z = -std::cos(cam_yaw_rad);
    // Vetor perpendicular (direita do jogador)
    float cam_right_x = std::cos(cam_yaw_rad);
    float cam_right_z = -std::sin(cam_yaw_rad);
    
    // === INPUT DE MOVIMENTO ===
    float input_forward = 0.0f;
    float input_right = 0.0f;
    // W = frente, S = tras
    if (key_down('W') || key_down(VK_UP))    input_forward += 1.0f;
    if (key_down('S') || key_down(VK_DOWN))  input_forward -= 1.0f;
    if (key_down('A') || key_down(VK_LEFT))  input_right -= 1.0f;     // Esquerda
    if (key_down('D') || key_down(VK_RIGHT)) input_right += 1.0f;     // Direita
    
    // Calcular direcao de movimento no espaco do mundo
    float desired_x = input_forward * cam_forward_x + input_right * cam_right_x;
    float desired_z = input_forward * cam_forward_z + input_right * cam_right_z;
    
    // Normalizar se houver input
    bool has_input = (desired_x != 0.0f || desired_z != 0.0f);
    if (has_input) {
        float len = std::sqrt(desired_x * desired_x + desired_z * desired_z);
        desired_x /= len;
        desired_z /= len;
        
        // Calcular rotacao alvo (personagem vira para onde esta andando)
        g_player.target_rotation = std::atan2(desired_x, desired_z) * (180.0f / kPi);
        if (g_player.target_rotation < 0.0f) g_player.target_rotation += 360.0f;
    }
    
    // === FUNCAO PARA OBTER ALTURA DO CHAO ===
    auto get_ground_height_at = [&](float px, float pz) -> float {
        // Checar multiplos pontos para AABB do jogador
        float hw = g_player.w * 0.4f;
        float max_h = 0.0f;
        for (float ox = -hw; ox <= hw; ox += hw) {
            for (float oz = -hw; oz <= hw; oz += hw) {
                int tx = (int)std::floor(px + ox);
                int tz = (int)std::floor(pz + oz);
                if (g_world->in_bounds(tx, tz)) {
                    max_h = std::max(max_h, surface_height_at(*g_world, tx, tz));
                }
            }
        }
        return max_h;
    };
    
    // === DETECTAR CHAO SOB O JOGADOR ===
    float current_ground = get_ground_height_at(g_player.pos.x, g_player.pos.y);
    g_player.ground_height = current_ground;
    
    // Verificar se esta no chao (com margem)
    bool was_on_ground = g_player.on_ground;
    g_player.on_ground = (g_player.pos_y <= current_ground + 0.05f);
    
    // === PULO (Espaco) ===
    bool jump_key = key_down(VK_SPACE);
    if (jump_key && g_player.on_ground && g_player.can_jump) {
        g_player.vel_y = kJumpForce;
        g_player.on_ground = false;
        g_player.can_jump = false;
    }
    if (!jump_key) {
        g_player.can_jump = true;
    }
    
    // === GRAVIDADE ===
    g_player.vel_y -= kGravity * dt;
    
    // === MOVIMENTO HORIZONTAL ===
    bool run_key = key_down(VK_SHIFT);
    float target_speed = kMoveSpeed * (run_key ? kRunMult : 1.0f);
    float control = g_player.on_ground ? 1.0f : kAirControl;
    
    float target_vel_x = desired_x * target_speed;
    float target_vel_z = desired_z * target_speed;
    
    float accel = has_input ? kAcceleration : kDeceleration;
    g_player.vel.x += (target_vel_x - g_player.vel.x) * std::min(1.0f, accel * control * dt);
    g_player.vel.y += (target_vel_z - g_player.vel.y) * std::min(1.0f, accel * control * dt);
    
    // === APLICAR MOVIMENTO VERTICAL ===
    g_player.pos_y += g_player.vel_y * dt;
    
    // === STEP CLIMBING (subir blocos automaticamente) ===
    // Apenas quando no chao e se movendo
    if (g_player.on_ground && has_input && g_player.vel_y <= 0.0f) {
        // Checar altura na direcao do movimento
        float check_dist = 0.6f;  // Distancia a frente para checar
        float next_x = g_player.pos.x + desired_x * check_dist;
        float next_z = g_player.pos.y + desired_z * check_dist;
        float ground_ahead = get_ground_height_at(next_x, next_z);
        
        // Se o bloco a frente e mais alto mas ainda alcancavel, subir suavemente
        float height_diff = ground_ahead - g_player.pos_y;
        if (height_diff > 0.05f && height_diff <= kPlayerStepHeight) {
            // Subir gradualmente (nao instantaneo)
            float step_speed = 6.0f;  // Velocidade de subida
            float step_amount = std::min(height_diff, step_speed * dt);
            g_player.pos_y += step_amount;
            
            // Se subiu completamente, manter no chao
            if (g_player.pos_y >= ground_ahead - 0.01f) {
                g_player.pos_y = ground_ahead;
            }
        }
    }
    
    // === COLISAO COM CHAO ===
    current_ground = get_ground_height_at(g_player.pos.x, g_player.pos.y);
    if (g_player.pos_y < current_ground) {
        g_player.pos_y = current_ground;
        g_player.vel_y = 0.0f;
        g_player.on_ground = true;
    }
    
    // Impedir cair abaixo de 0
    if (g_player.pos_y < 0.0f) {
        g_player.pos_y = 0.0f;
        g_player.vel_y = 0.0f;
        g_player.on_ground = true;
    }
    
    // Atualizar ground_height apos movimento
    g_player.ground_height = get_ground_height_at(g_player.pos.x, g_player.pos.y);
    
    // === SUAVIZAR ROTACAO ===
    float rot_diff = g_player.target_rotation - g_player.rotation;
    while (rot_diff > 180.0f) rot_diff -= 360.0f;
    while (rot_diff < -180.0f) rot_diff += 360.0f;
    g_player.rotation += rot_diff * std::min(1.0f, 12.0f * dt);
    while (g_player.rotation >= 360.0f) g_player.rotation -= 360.0f;
    while (g_player.rotation < 0.0f) g_player.rotation += 360.0f;
    
    // Atualizar facing_dir para compatibilidade
    float normalized_rot = g_player.rotation;
    if (normalized_rot >= 315.0f || normalized_rot < 45.0f) {
        g_player.facing_dir = 0;
    } else if (normalized_rot >= 45.0f && normalized_rot < 135.0f) {
        g_player.facing_dir = 1;
    } else if (normalized_rot >= 135.0f && normalized_rot < 225.0f) {
        g_player.facing_dir = 2;
    } else {
        g_player.facing_dir = 3;
    }
    
    // Zoom da camera
    if (key_down(VK_ADD) || key_down(VK_OEM_PLUS)) {
        g_camera.distance = std::max(g_camera.min_distance, g_camera.distance - 10.0f * dt);
    }
    if (key_down(VK_SUBTRACT) || key_down(VK_OEM_MINUS)) {
        g_camera.distance = std::min(g_camera.max_distance, g_camera.distance + 10.0f * dt);
    }
    
    // Update animation
    g_player.anim_frame += dt;
    g_player.is_moving = has_input;
    if (g_player.is_moving) {
        g_player.walk_timer += dt * (run_key ? 1.5f : 1.0f);
    } else {
        g_player.walk_timer *= 0.9f;
    }
    
    // === SURVIVAL MECHANICS ===
    // Astronaut dies from: no oxygen OR no water (after 30 seconds without)
    static float dehydration_timer = 0.0f;  // Time without water
    static float suffocation_timer = 0.0f;  // Time without oxygen
    static float damage_tick = 0.0f;
    
    const float kDamageDelay = 15.0f;  // 15 seconds before damage starts (was 30s)
    
    // Track time without resources
    if (g_water_res <= 0.0f) {
        dehydration_timer += dt;
    } else {
        dehydration_timer = 0.0f; // Reset when water is available
    }
    
    if (g_oxygen <= 0.0f) {
        suffocation_timer += dt;
    } else {
        suffocation_timer = 0.0f; // Reset when oxygen is available
    }
    
    // Damage tick (every second)
    damage_tick += dt;
    if (damage_tick >= 1.0f) {
        damage_tick = 0.0f;
        
        // Suffocation damage - only after 30 seconds without oxygen
        if (suffocation_timer > kDamageDelay) {
            g_player.hp = std::max(0, g_player.hp - 10);
            if (g_player.hp <= 0) {
                g_toast = "You suffocated!";
                g_state = GameState::Dead;
                return;
            }
        }
        
        // Dehydration damage - only after 30 seconds without water
        if (dehydration_timer > kDamageDelay) {
            g_player.hp = std::max(0, g_player.hp - 8);
            if (g_player.hp <= 0) {
                g_toast = "You died of dehydration!";
                g_state = GameState::Dead;
                return;
            }
        }
    }
    
    // Warnings when resources are empty (before damage starts)
    // Increased interval from 3s to 5s to reduce spam
    static float warn_timer = 0.0f;
    warn_timer += dt;
    if (warn_timer >= 5.0f) {
        warn_timer = 0.0f;
        
        if (g_oxygen <= 0.0f && suffocation_timer < kDamageDelay) {
            int seconds_left = (int)(kDamageDelay - suffocation_timer);
            set_toast("SEM OXIGENIO! Dano em " + std::to_string(seconds_left) + "s!", 2.5f);
        } else if (g_water_res <= 0.0f && dehydration_timer < kDamageDelay) {
            int seconds_left = (int)(kDamageDelay - dehydration_timer);
            set_toast("SEM AGUA! Dano em " + std::to_string(seconds_left) + "s!", 2.5f);
        } else if (g_oxygen < 15.0f && g_oxygen > 0.0f) {
            set_toast("Aviso: Oxigenio baixo! Construa Gerador de O2.");
            // Onboarding: dica para voltar a base
            if (!g_onboarding.shown_return_to_base) {
                show_tip("H para voltar a base e recarregar oxigenio", g_onboarding.shown_return_to_base);
            }
        } else if (g_water_res < 15.0f && g_water_res > 0.0f) {
            set_toast("Aviso: Agua baixa! Construa Extrator de Agua.");
            // Onboarding: dica para agua baixa
            if (!g_onboarding.shown_low_water) {
                show_tip("Quebre blocos de gelo para obter agua", g_onboarding.shown_low_water);
            }
        }
    }

    // Clamp velocity
    g_player.vel.x = std::clamp(g_player.vel.x, -20.0f, 20.0f);
    g_player.vel.y = std::clamp(g_player.vel.y, -25.0f, 35.0f);

    // Move and collide (usando colisao 3D que considera altura)
    float dx = g_player.vel.x * dt;
    float dy = g_player.vel.y * dt;
    g_player.pos.x += dx;
    resolve_player_collisions_3d(g_player, *g_world, dx, 0.0f);
    g_player.pos.y += dy;
    resolve_player_collisions_3d(g_player, *g_world, 0.0f, dy);

    // Camera follow
    float cam_speed = 6.0f;
    g_cam_pos.x = approach(g_cam_pos.x, g_player.pos.x, cam_speed * dt * std::fabs(g_player.pos.x - g_cam_pos.x) + 0.5f * dt);
    g_cam_pos.y = approach(g_cam_pos.y, g_player.pos.y, cam_speed * dt * std::fabs(g_player.pos.y - g_cam_pos.y) + 0.5f * dt);

    // Mouse targeting
    POINT cursor;
    GetCursorPos(&cursor);
    ScreenToClient(hwnd, &cursor);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int win_w = rc.right - rc.left;
    int win_h = rc.bottom - rc.top;

    // Usar zoom para calculos
    float tile_size = TILE_PX * g_zoom;
    float view_half_x = (win_w / tile_size) * 0.5f;
    float view_half_y = (win_h / tile_size) * 0.5f;
    float cam_x = std::clamp(g_cam_pos.x, view_half_x, (float)g_world->w - view_half_x);
    float cam_y = std::clamp(g_cam_pos.y, view_half_y, (float)g_world->h - view_half_y);

    // ============= TARGETING 3D (Estilo Minicraft) =============
    // O alvo e o bloco na direcao que o jogador esta olhando
    // Usando a rotacao do jogador para determinar o bloco alvo
    const float kReach = 4.5f;
    
    // Calcular direcao do olhar baseado na rotacao do jogador
    float look_rad = g_player.rotation * (kPi / 180.0f);
    float look_x = std::sin(look_rad);
    float look_z = std::cos(look_rad);
    
    // Raycast para alvo (minerar) + alvo de colocacao (RMB) estilo Minecraft/Minicraft.
    // O chao (tiles walkable) nao bloqueia o raycast, para facilitar mirar em rochas/arvores.
    g_has_target = false;
    g_target_in_range = false;
    g_has_place_target = false;
    g_place_in_range = false;

    int last_place_x = -1;
    int last_place_y = -1;

    auto placeable_tile = [&](Block b) -> bool {
        if (is_base_structure(b)) return false;
        if (is_module(b)) return false;
        if (b == Block::Air || b == Block::Water) return true;
        // Pode substituir tiles walkable (grama/terra/areia/neve/folhas etc)
        return !is_solid(b);
    };

    auto blocks_raycast = [&](Block b) -> bool {
        if (b == Block::Air) return false;
        if (b == Block::Water) return true;
        if (b == Block::Leaves) return true;
        if (is_base_structure(b)) return true;
        if (is_module(b)) return true;
        return is_solid(b);
    };

    for (float t = 0.5f; t <= kReach; t += 0.25f) {
        int test_x = (int)std::floor(g_player.pos.x + look_x * t);
        int test_y = (int)std::floor(g_player.pos.y + look_z * t);
        if (!g_world->in_bounds(test_x, test_y)) break;

        Block b = g_world->get(test_x, test_y);

        if (placeable_tile(b)) {
            last_place_x = test_x;
            last_place_y = test_y;
        }

        if (blocks_raycast(b)) {
            g_target_x = test_x;
            g_target_y = test_y;
            g_has_target = true;
            g_target_in_range = true;

            // Se o alvo for substituivel (agua/folhas), coloca nele; senao, coloca no tile anterior
            if (placeable_tile(b)) {
                g_place_x = test_x;
                g_place_y = test_y;
                g_has_place_target = true;
                g_place_in_range = true;
            } else if (last_place_x != -1) {
                g_place_x = last_place_x;
                g_place_y = last_place_y;
                g_has_place_target = true;
                g_place_in_range = true;
            }

            // Onboarding: dica de mineracao ao mirar algo mineravel pela primeira vez
            if (!g_onboarding.shown_first_mine && is_mineable(b)) {
                show_tip("Segure clique esquerdo (ou E) para minerar blocos", g_onboarding.shown_first_mine);
            }
            break;
        }
    }

    // Se nao encontrou nada bloqueando, mirar um tile a frente (principalmente para colocar)
    if (!g_has_target) {
        if (last_place_x != -1) {
            g_target_x = last_place_x;
            g_target_y = last_place_y;
        } else {
            float t = 2.0f;
            g_target_x = (int)std::floor(g_player.pos.x + look_x * t);
            g_target_y = (int)std::floor(g_player.pos.y + look_z * t);
        }
        g_has_target = g_world->in_bounds(g_target_x, g_target_y);
        g_target_in_range = g_has_target;

        if (g_has_target) {
            Block b = g_world->get(g_target_x, g_target_y);
            if (placeable_tile(b)) {
                g_place_x = g_target_x;
                g_place_y = g_target_y;
                g_has_place_target = true;
                g_place_in_range = true;
            }
        }
    }

    // Cooldowns (apenas colocacao)
    if (g_place_cd > 0.0f) g_place_cd -= dt;

    bool lmb = key_down(VK_LBUTTON);
    bool rmb = key_down(VK_RBUTTON);
    
    // ============= TOP-DOWN INTERACTION (Tecla E) =============
    // Calcula tile a frente do jogador baseado na direcao
    int front_x = (int)std::floor(g_player.pos.x);
    int front_y = (int)std::floor(g_player.pos.y);
    switch (g_player.facing_dir) {
        case 0: front_y -= 1; break; // Norte
        case 1: front_x += 1; break; // Leste
        case 2: front_y += 1; break; // Sul
        case 3: front_x -= 1; break; // Oeste
    }
    
    bool e_key = key_down('E');
    g_prev_e = e_key;
    
    // Interacao com E (segurar) - mira o tile a frente para mineracao sem mouse
    if (e_key && g_world->in_bounds(front_x, front_y)) {
        Block front_block = g_world->get(front_x, front_y);
        if (is_mineable(front_block)) {
            g_target_x = front_x;
            g_target_y = front_y;
            g_has_target = true;
            g_target_in_range = true;
        }
    }

    // Mining com progresso (segurar LMB ou E)
    bool mine_input = (lmb || e_key);
    bool has_mine_target = g_has_target && g_target_in_range && g_world->in_bounds(g_target_x, g_target_y);
    Block mine_block = has_mine_target ? g_world->get(g_target_x, g_target_y) : Block::Air;

    // Feedback ao tentar minerar estrutura da base
    if (mine_input && has_mine_target && is_base_structure(mine_block)) {
        static float base_warn_cd = 0.0f;
        base_warn_cd -= dt;
        if (base_warn_cd <= 0.0f) {
            show_error("Nao pode destruir estruturas da base!");
            base_warn_cd = 1.0f;
        }
    }

    bool mine_ok = mine_input && has_mine_target && is_mineable(mine_block);
    static float mining_particle_timer = 0.0f;
    if (mine_ok) {
        g_player.is_mining = true;
        g_player.mine_anim += dt;

        // Virar na direcao do alvo
        float dx = (g_target_x + 0.5f) - g_player.pos.x;
        float dz = (g_target_y + 0.5f) - g_player.pos.y;
        g_player.target_rotation = std::atan2(-dx, -dz) * (180.0f / kPi);
        if (g_player.target_rotation < 0.0f) g_player.target_rotation += 360.0f;

        // Se mudou de bloco alvo, resetar progresso
        if (g_target_x != g_mine_block_x || g_target_y != g_mine_block_y) {
            g_mine_block_x = g_target_x;
            g_mine_block_y = g_target_y;
            g_mine_progress = 0.0f;
        }

        float hard = std::max(0.05f, block_hardness(mine_block));
        g_mine_progress = std::min(1.0f, g_mine_progress + (dt / hard));

        // Particulas de mineracao (feedback visual constante)
        mining_particle_timer += dt;
        if (mining_particle_timer >= 0.08f) {
            mining_particle_timer = 0.0f;
            if (mine_block != Block::Air) {
                for (int i = 0; i < 2; ++i) {
                    Particle p;
                    p.pos.x = g_target_x + 0.5f + (rand() % 100 - 50) / 100.0f * 0.4f;
                    p.pos.y = g_target_y + 0.5f + (rand() % 100 - 50) / 100.0f * 0.4f;
                    p.vel.x = (rand() % 100 - 50) / 50.0f;
                    p.vel.y = (rand() % 100 - 50) / 50.0f - 1.0f;
                    p.life = 0.3f + (rand() % 20) / 100.0f;
                    float br, bg, bb, ba;
                    block_color(mine_block, g_target_y, g_world->h, br, bg, bb, ba);
                    p.r = br * 0.9f + 0.1f;
                    p.g = bg * 0.9f + 0.1f;
                    p.b = bb * 0.9f + 0.1f;
                    p.a = 0.9f;
                    g_particles.push_back(p);
                }
            }
        }

        // Quebrar bloco ao completar progresso
        if (g_mine_progress >= 0.999f) {
            Block b = mine_block;
            spawn_block_particles(b, g_target_x + 0.5f, g_target_y + 0.5f, g_world->h);
            g_world->set(g_target_x, g_target_y, Block::Air);
            g_surface_dirty = true;

            if (is_module(b)) {
                refund_cost(module_cost(b));
                g_modules.erase(std::remove_if(g_modules.begin(), g_modules.end(),
                    [](const Module& m) { return m.x == g_target_x && m.y == g_target_y; }), g_modules.end());
            } else {
                Block drop = drop_item_for_block(b);
                float sy = (float)g_world->height_at(g_target_x, g_target_y) * kHeightScale + drop_spawn_y_for_block(b);
                spawn_item_drop(drop, (float)g_target_x, (float)g_target_y, sy);
            }

            // Reset apos quebrar
            g_mine_progress = 0.0f;
            g_mine_block_x = -1;
            g_mine_block_y = -1;
            mining_particle_timer = 0.0f;
        }
    } else {
        g_player.is_mining = false;
        g_player.mine_anim = 0.0f;
        mining_particle_timer = 0.0f;
        g_mine_progress = 0.0f;
        g_mine_block_x = -1;
        g_mine_block_y = -1;
    }

    g_prev_lmb = lmb;

    // Placing (RMB)
    auto placeable_tile_for_place = [&](Block b) -> bool {
        if (is_base_structure(b)) return false;
        if (is_module(b)) return false;
        if (b == Block::Air || b == Block::Water) return true;
        return !is_solid(b); // chao/walkable pode ser substituido
    };

    if (rmb && !g_prev_rmb && g_has_place_target && g_place_in_range && g_place_cd <= 0.0f) {
        Block cur = g_world->get(g_place_x, g_place_y);
        if (placeable_tile_for_place(cur)) {
            // Check player collision
            float pl = g_player.pos.x - g_player.w * 0.5f;
            float pr = g_player.pos.x + g_player.w * 0.5f;
            float pt = g_player.pos.y - g_player.h * 0.5f;
            float pb = g_player.pos.y + g_player.h * 0.5f;
            bool overlaps_player = !(g_place_x + 1 <= pl || g_place_x >= pr || g_place_y + 1 <= pt || g_place_y >= pb);

            if (!overlaps_player) {
                if (is_module(g_selected)) {
                    // Check if module is unlocked first
                    if (!is_unlocked(g_selected)) {
                        set_toast("Modulo nao desbloqueado! Colete mais recursos.");
                    } else {
                        CraftCost cost = module_cost(g_selected);
                        if (can_afford(cost)) {
                            spend_cost(cost);
                            g_world->set(g_place_x, g_place_y, g_selected);
                            g_modules.push_back(Module{g_place_x, g_place_y, g_selected, 0.0f});
                            g_surface_dirty = true;
                            g_place_cd = 0.25f;
                            
                            // Special messages for certain modules
                            if (g_selected == Block::CO2Factory) {
                                set_toast("Fabrica de CO2 colocada! Aquecendo o planeta...", 3.0f);
                            } else if (g_selected == Block::TerraformerBeacon) {
                                show_success("Terraformador ativo! (Requer fase de Degelo)");
                            }
                        } else {
                            show_error("Recursos insuficientes!");
                        }
                    }
                } else if (g_inventory[(int)g_selected] > 0) {
                    g_inventory[(int)g_selected]--;
                    g_world->set(g_place_x, g_place_y, g_selected);
                    g_surface_dirty = true;
                    g_place_cd = 0.12f;
                }
            }
        }
    }
    g_prev_rmb = rmb;

    // Atualizar drops e coletar por proximidade
    update_item_drops(dt);

    // Update particles
    for (auto& p : g_particles) {
        p.vel.y += 15.0f * dt;
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        p.life -= dt;
    }
    g_particles.erase(std::remove_if(g_particles.begin(), g_particles.end(),
        [](const Particle& p) { return p.life <= 0.0f; }), g_particles.end());
}

// ============= Window Procedure =============
// Variaveis para controle de camera com mouse
static int g_last_mouse_x = 0;
static int g_last_mouse_y = 0;
static bool g_mouse_captured = false;

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
        case WM_DESTROY:
            g_quit = true;
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE && g_state == GameState::Menu) {
                g_quit = true;
            }
            return 0;
        case WM_MOUSEWHEEL: {
            // Zoom da camera com scroll do mouse
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_camera.distance -= (float)delta * 0.005f;
            g_camera.distance = std::clamp(g_camera.distance, g_camera.min_distance, g_camera.max_distance);
            return 0;
        }
        case WM_RBUTTONDOWN:
            // Capturar mouse ao clicar com botao direito
            g_mouse_captured = true;
            SetCapture(hwnd);
            ShowCursor(FALSE);
            return 0;
        case WM_RBUTTONUP:
            // Liberar mouse
            g_mouse_captured = false;
            ReleaseCapture();
            ShowCursor(TRUE);
            return 0;
        case WM_MOUSEMOVE:
            if (g_mouse_captured && g_state == GameState::Playing) {
                int mx = LOWORD(lParam);
                int my = HIWORD(lParam);
                
                int delta_x = mx - g_last_mouse_x;
                int delta_y = my - g_last_mouse_y;
                
                // Rotacionar camera (mouse direita = camera gira direita)
                g_camera.yaw += delta_x * g_camera.sensitivity;
                g_camera.pitch -= delta_y * g_camera.sensitivity * 0.5f;
                
                // Clamp pitch
                g_camera.pitch = std::clamp(g_camera.pitch, g_camera.min_pitch, g_camera.max_pitch);
                
                // Normalizar yaw
                while (g_camera.yaw >= 360.0f) g_camera.yaw -= 360.0f;
                while (g_camera.yaw < 0.0f) g_camera.yaw += 360.0f;
                
                // Recentrar o mouse
                RECT rc;
                GetClientRect(hwnd, &rc);
                POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
                ClientToScreen(hwnd, &center);
                SetCursorPos(center.x, center.y);
                
                ScreenToClient(hwnd, &center);
                g_last_mouse_x = center.x;
                g_last_mouse_y = center.y;
            } else {
                g_last_mouse_x = LOWORD(lParam);
                g_last_mouse_y = HIWORD(lParam);
            }
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ============= WinMain =============
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Register window class
    WNDCLASSA wc = {};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "TerraFormer2DClass";

    if (!RegisterClassA(&wc)) {
        MessageBoxA(nullptr, "Failed to register window class", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Create window
    int win_w = 1280;
    int win_h = 720;
    RECT wr = {0, 0, win_w, win_h};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowA(
        "TerraFormer2DClass",
        "TerraFormer 2D",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        MessageBoxA(nullptr, "Failed to create window", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    HDC hdc = GetDC(hwnd);
    HGLRC hrc = setup_opengl(hdc);
    init_font(hdc);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Initialize world for menu background
    g_world = new World(WORLD_WIDTH, WORLD_HEIGHT, 1337);
    spawn_player_new_game(*g_world);
    g_cam_pos = g_player.pos;
    g_state = GameState::Menu;

    // Timing
    LARGE_INTEGER freq, last_time, cur_time;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last_time);

    // Main loop
    MSG msg;
    while (!g_quit) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_quit = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (g_quit) break;

        // Calculate delta time
        QueryPerformanceCounter(&cur_time);
        float dt = (float)(cur_time.QuadPart - last_time.QuadPart) / (float)freq.QuadPart;
        last_time = cur_time;
        dt = std::clamp(dt, 0.0001f, 0.1f); // Clamp to avoid huge jumps

        // Update
        update_game(dt, hwnd);

        // Render
        RECT rc;
        GetClientRect(hwnd, &rc);
        render_world(hdc, rc.right - rc.left, rc.bottom - rc.top);

        // Small sleep to avoid 100% CPU
        Sleep(1);
    }

    // Cleanup
    delete g_world;
    g_world = nullptr;

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(hrc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);

    return 0;
}
