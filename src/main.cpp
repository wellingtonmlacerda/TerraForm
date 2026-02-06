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
#include <cstdlib>
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

struct PlayerPhysicsInput;

static Vec2 vec2_add(const Vec2& a, const Vec2& b) { return {a.x + b.x, a.y + b.y}; }
static Vec2 vec2_sub(const Vec2& a, const Vec2& b) { return {a.x - b.x, a.y - b.y}; }
static Vec2 vec2_scale(const Vec2& v, float s) { return {v.x * s, v.y * s}; }
static float vec2_dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
static float vec2_length(const Vec2& v) { return std::sqrt(v.x * v.x + v.y * v.y); }
static Vec2 vec2_normalize(const Vec2& v) {
    float len = vec2_length(v);
    if (len < 1e-5f) return {0.0f, 0.0f};
    return {v.x / len, v.y / len};
}
static Vec2 vec2_lerp(const Vec2& a, const Vec2& b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

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

// Dia/noite coerente com a trajetoria do sol (mesma fase usada em render_alien_sky).
// Retorna 0..1 (0 = noite, 1 = pico do dia).
static float compute_daylight(float day_phase) {
    float sun_height = std::sin(day_phase * 2.0f * kPi - kPi * 0.5f);
    return std::max(0.0f, sun_height);
}

// 0..1 (1 = noite escura, 0 = dia).
static float compute_night_alpha(float day_phase) {
    float daylight = compute_daylight(day_phase);
    // Comeca a "apagar" estrelas apenas quando ja esta relativamente claro.
    return 1.0f - smoothstep01(0.05f, 0.30f, daylight);
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
static int g_settings_selection = 0;  // 0=sensibilidade, 1=inverter Y, 2=brilho, 3=escala UI, 4=iluminacao, 5=sombras, 6=bloom, 7=vinheta, 8=voltar
static int g_pause_selection = -1;     // -1=nenhum, 0=continuar, 1=salvar, 2=carregar, 3=config, 4=novo jogo
static int g_menu_selection = -1;      // -1=nenhum, 0=novo jogo, 1=carregar, 2=sair

// Posicao do mouse na tela
static int g_mouse_x = 0;
static int g_mouse_y = 0;
static bool g_mouse_left_clicked = false;  // Flag para clique esquerdo (single frame)

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

struct TerrainConfig {
    float macro_scale = 0.00115f;
    float ridge_scale = 0.0048f;
    float valley_scale = 0.0020f;
    float detail_scale = 0.0180f;
    float warp_scale = 0.0032f;
    float warp_strength = 26.0f;

    float macro_weight = 0.52f;
    float ridge_weight = 0.76f;
    float valley_weight = 0.42f;
    float detail_weight = 0.10f;

    float plateau_level = 0.62f;
    float plateau_flatten = 0.30f;

    float min_height = 2.0f;
    float max_height = 116.0f;
    float sea_height = 12.0f;
    float snow_height = 88.0f;

    int thermal_erosion_passes = 4;
    int hydraulic_erosion_passes = 3;
    int smooth_passes = 1;
    float erosion_strength = 0.34f;
    float thermal_talus = 0.026f;

    float temp_scale = 0.0016f;
    float moisture_scale = 0.0019f;
    float biome_blend = 0.18f;

    float fissure_scale = 0.010f;
    float fissure_depth = 0.09f;
    float crater_scale = 0.0050f;
    float crater_depth = 0.075f;
    float detail_object_density = 0.090f;
};

struct SkyConfig {
    float stars_density = 1250.0f;
    float stars_parallax = 0.010f;
    float nebula_alpha = 0.17f;
    float nebula_parallax = 0.020f;
    float cloud_alpha = 0.14f;
    float cloud_parallax = 0.060f;

    float planet_radius = 132.0f;
    float planet_distance = 1180.0f;
    float planet_orbit_speed = 0.085f;
    float planet_parallax = 0.034f;

    float sun_radius = 44.0f;
    float sun_distance = 760.0f;
    float sun_halo_size = 1.90f;
    float bloom_intensity = 0.30f;

    float moon_radius = 31.0f;
    float moon_distance = 900.0f;
    float moon_orbit_speed = 0.55f;
    float moon_parallax = 0.050f;

    float moon2_radius = 18.0f;
    float moon2_distance = 980.0f;
    float moon2_orbit_speed = 1.15f;
    float moon2_parallax = 0.060f;

    float atmosphere_horizon_boost = 0.32f;
    float atmosphere_zenith_boost = 0.17f;
    float horizon_fade = 0.24f;

    float fog_start_factor = 0.40f;
    float fog_end_factor = 0.92f;
    float fog_distance_bonus = 22.0f;

    float eclipse_frequency_days = 6.0f;
    float eclipse_strength = 0.45f;
};

static TerrainConfig g_terrain_cfg = {};
static SkyConfig g_sky_cfg = {};
static std::string g_terrain_config_path = "terrain_config.json";
static std::string g_sky_config_path = "sky_config.json";

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

    // ============= WORLD GENERATION (Macro Heightmap + Erosion + Biomes) =============
    void gen() {
        init_permutation(seed);

        std::fill(tiles.begin(), tiles.end(), Block::Air);
        std::fill(ground.begin(), ground.end(), Block::Dirt);
        std::fill(heightmap.begin(), heightmap.end(), 0);
        std::fill(surface_y.begin(), surface_y.end(), 0);

        const TerrainConfig& cfg = g_terrain_cfg;
        auto index_of = [this](int x, int y) -> size_t { return (size_t)y * (size_t)w + (size_t)x; };

        int min_h_i = std::max(0, (int)std::lround(cfg.min_height));
        int max_h_i = std::max(min_h_i + 2, (int)std::lround(cfg.max_height));
        int sea_h = std::clamp((int)std::lround(cfg.sea_height), min_h_i, max_h_i - 1);
        int snow_h = std::clamp((int)std::lround(cfg.snow_height), sea_h + 2, max_h_i);
        sea_level = sea_h;

        const size_t cell_count = (size_t)w * (size_t)h;
        std::vector<float> heights(cell_count, 0.0f);
        std::vector<float> temp_map(cell_count, 0.0f);
        std::vector<float> moist_map(cell_count, 0.0f);
        std::vector<float> ridge_map(cell_count, 0.0f);
        std::vector<float> valley_map(cell_count, 0.0f);
        std::vector<uint8_t> biome_map(cell_count, 0);

        // === Passo 1: macro shape (continentes, bacias, vales, cordilheiras) ===
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float fx = (float)x;
                float fy = (float)y;

                float warp_x = (fbm(fx * cfg.warp_scale + 41.0f, fy * cfg.warp_scale - 63.0f, 3) - 0.5f) * 2.0f;
                float warp_y = (fbm(fx * cfg.warp_scale - 97.0f, fy * cfg.warp_scale + 29.0f, 3) - 0.5f) * 2.0f;
                float wx = fx + warp_x * cfg.warp_strength;
                float wy = fy + warp_y * cfg.warp_strength;

                float macro = fbm(wx * cfg.macro_scale, wy * cfg.macro_scale, 6);
                float basin = 1.0f - fbm(wx * (cfg.macro_scale * 1.55f) + 1400.0f,
                                         wy * (cfg.macro_scale * 1.55f) + 1400.0f, 4);
                float ridge = ridged_fbm(wx * cfg.ridge_scale + 700.0f, wy * cfg.ridge_scale + 700.0f, 5);
                float valley = 1.0f - ridged_fbm(wx * cfg.valley_scale + 2500.0f, wy * cfg.valley_scale + 2500.0f, 4);
                float detail = fbm(wx * cfg.detail_scale + 3100.0f, wy * cfg.detail_scale + 3100.0f, 4);
                float hills = fbm(wx * (cfg.detail_scale * 0.52f) + 900.0f,
                                  wy * (cfg.detail_scale * 0.52f) + 900.0f, 3);

                float mountain_w = smoothstep01(0.56f, 0.90f, ridge) * smoothstep01(0.38f, 0.88f, macro);
                float valley_w = smoothstep01(0.52f, 0.92f, valley) * (1.0f - mountain_w * 0.58f);
                float plateau_w = smoothstep01(cfg.plateau_level - 0.10f, cfg.plateau_level + 0.12f, macro) *
                                  smoothstep01(0.35f, 0.74f, hills) * (1.0f - mountain_w * 0.75f);
                float plains_w = clamp01(1.0f - mountain_w - valley_w * 0.72f - plateau_w * 0.48f);

                float plains_h = 0.30f + (macro - 0.5f) * 0.12f + (hills - 0.5f) * 0.11f + (detail - 0.5f) * 0.07f;
                float valley_h = 0.24f + (macro - 0.5f) * 0.08f + (detail - 0.5f) * 0.05f - valley_w * 0.23f - basin * 0.08f;
                float mountain_h = 0.42f + std::pow(ridge, 1.85f) * 0.60f + (hills - 0.5f) * 0.08f;
                float plateau_h = 0.52f + std::pow(macro, 1.15f) * 0.30f + (detail - 0.5f) * 0.04f;
                plateau_h = lerp(plateau_h, std::floor(plateau_h * 9.0f) / 9.0f, cfg.plateau_flatten);

                float wsum = plains_w + valley_w + mountain_w + plateau_w + 0.0001f;
                float hn = (plains_h * plains_w + valley_h * valley_w + mountain_h * mountain_w + plateau_h * plateau_w) / wsum;
                hn += (macro - 0.5f) * cfg.macro_weight * 0.22f;
                hn += (ridge - 0.5f) * cfg.ridge_weight * 0.18f;
                hn -= valley_w * cfg.valley_weight * 0.15f;
                hn += (detail - 0.5f) * cfg.detail_weight;

                // Fendas e crateras suaves (antes da erosao para ficar natural).
                float fissure_line = std::fabs(perlin(wx * cfg.fissure_scale + 4300.0f, wy * cfg.fissure_scale + 4300.0f) - 0.5f);
                float fissure_cut = clamp01((0.018f - fissure_line) / 0.018f);
                float crater_shape = 1.0f - std::fabs(perlin(wx * cfg.crater_scale + 5200.0f, wy * cfg.crater_scale + 5200.0f) * 2.0f - 1.0f);
                float crater_core = smoothstep01(0.82f, 0.96f, crater_shape);
                float crater_rim = smoothstep01(0.62f, 0.80f, crater_shape) * (1.0f - crater_core);
                hn -= fissure_cut * cfg.fissure_depth;
                hn -= crater_core * cfg.crater_depth;
                hn += crater_rim * cfg.crater_depth * 0.42f;
                hn = clamp01(hn);

                float lat = 0.0f;
                if (h > 1) {
                    float ny = (fy / (float)(h - 1)) * 2.0f - 1.0f;
                    lat = std::fabs(ny);
                }

                float temp = fbm(wx * cfg.temp_scale + 900.0f, wy * cfg.temp_scale + 900.0f, 4);
                temp = clamp01(temp * 0.72f + (1.0f - lat) * 0.28f - hn * 0.38f);
                float moisture = fbm(wx * cfg.moisture_scale + 1300.0f, wy * cfg.moisture_scale + 1300.0f, 4);
                moisture = clamp01(moisture * 0.80f + basin * 0.20f);

                uint8_t biome = 0; // 0 Planicie | 1 Vale | 2 Montanha | 3 Plato | 4 Gelo
                if (hn > 0.72f && temp < 0.44f) biome = 4;
                else if (mountain_w >= valley_w && mountain_w >= plateau_w && mountain_w >= plains_w) biome = 2;
                else if (plateau_w >= valley_w && plateau_w >= plains_w) biome = 3;
                else if (valley_w >= plains_w) biome = 1;

                size_t idx = index_of(x, y);
                heights[idx] = hn;
                temp_map[idx] = temp;
                moist_map[idx] = moisture;
                ridge_map[idx] = ridge;
                valley_map[idx] = valley;
                biome_map[idx] = biome;
            }
        }

        // === Passo 2: erosao termica (remove "paredes") ===
        if (cfg.thermal_erosion_passes > 0) {
            std::vector<float> delta(cell_count, 0.0f);
            for (int pass = 0; pass < cfg.thermal_erosion_passes; ++pass) {
                std::fill(delta.begin(), delta.end(), 0.0f);
                for (int y = 1; y < h - 1; ++y) {
                    for (int x = 1; x < w - 1; ++x) {
                        size_t i = index_of(x, y);
                        float h0 = heights[i];
                        const int nx[4] = {1, -1, 0, 0};
                        const int ny[4] = {0, 0, 1, -1};
                        for (int k = 0; k < 4; ++k) {
                            size_t j = index_of(x + nx[k], y + ny[k]);
                            float diff = h0 - heights[j];
                            if (diff > cfg.thermal_talus) {
                                float move = (diff - cfg.thermal_talus) * cfg.erosion_strength * 0.22f;
                                delta[i] -= move;
                                delta[j] += move;
                            }
                        }
                    }
                }
                for (size_t i = 0; i < cell_count; ++i) {
                    heights[i] = clamp01(heights[i] + delta[i]);
                }
            }
        }

        // === Passo 3: erosao hidrica simplificada (alarga vales/bacias) ===
        if (cfg.hydraulic_erosion_passes > 0) {
            std::vector<float> copy = heights;
            for (int pass = 0; pass < cfg.hydraulic_erosion_passes; ++pass) {
                copy = heights;
                for (int y = 1; y < h - 1; ++y) {
                    for (int x = 1; x < w - 1; ++x) {
                        size_t i = index_of(x, y);
                        float center = copy[i];
                        float n = copy[index_of(x, y - 1)];
                        float s = copy[index_of(x, y + 1)];
                        float e = copy[index_of(x + 1, y)];
                        float wv = copy[index_of(x - 1, y)];
                        float ne = copy[index_of(x + 1, y - 1)];
                        float nw = copy[index_of(x - 1, y - 1)];
                        float se = copy[index_of(x + 1, y + 1)];
                        float sw = copy[index_of(x - 1, y + 1)];
                        float avg = (center * 2.0f + n + s + e + wv + ne + nw + se + sw) / 10.0f;
                        float min_n = std::min({center, n, s, e, wv, ne, nw, se, sw});
                        float slope = center - min_n;
                        float valley_boost = smoothstep01(0.60f, 0.95f, valley_map[i]) * 0.16f;
                        float blend = std::clamp(cfg.erosion_strength * (0.11f + slope * 1.1f) + valley_boost, 0.0f, 0.45f);
                        heights[i] = clamp01(lerp(center, avg, blend));
                    }
                }
            }
        }

        // === Passo 4: suavizacao final das encostas ===
        if (cfg.smooth_passes > 0) {
            std::vector<float> copy = heights;
            for (int pass = 0; pass < cfg.smooth_passes; ++pass) {
                copy = heights;
                for (int y = 1; y < h - 1; ++y) {
                    for (int x = 1; x < w - 1; ++x) {
                        size_t i = index_of(x, y);
                        float avg4 = (copy[index_of(x - 1, y)] + copy[index_of(x + 1, y)] +
                                      copy[index_of(x, y - 1)] + copy[index_of(x, y + 1)]) * 0.25f;
                        heights[i] = clamp01(lerp(copy[i], avg4, 0.15f + cfg.biome_blend * 0.18f));
                    }
                }
            }
        }

        // === Passo 5: converter heightmap e definir solo por bioma ===
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                size_t i = index_of(x, y);
                float hn = heights[i];
                int h_val = min_h_i + (int)std::lround(hn * (float)(max_h_i - min_h_i));
                int16_t th = (int16_t)std::clamp(h_val, min_h_i, max_h_i);
                set_height(x, y, th);

                float temp = temp_map[i];
                float moisture = moist_map[i];
                uint8_t biome = biome_map[i];

                Block g = Block::Dirt;
                if ((int)th <= sea_h) {
                    g = (temp < 0.44f) ? Block::Ice : Block::Water;
                } else if (biome == 4 || (int)th >= snow_h || temp < 0.25f) {
                    float snow_var = fbm((float)x * 0.045f + 7600.0f, (float)y * 0.045f + 7600.0f, 2);
                    g = (snow_var > 0.56f) ? Block::Ice : Block::Snow;
                } else if (biome == 1 && moisture > 0.66f) {
                    g = Block::Dirt; // vale mais umido
                } else if (moisture < 0.30f && temp > 0.52f) {
                    g = Block::Sand;
                } else if (biome == 3 && moisture < 0.36f) {
                    g = Block::Sand;
                } else {
                    g = Block::Dirt;
                }

                set_ground(x, y, g);
                set(x, y, g);
            }
        }

        // === Passo 6: detalhamento (rochas, fendas, pedregulhos, minerios) ===
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                Block g = get_ground(x, y);
                int16_t th = height_at(x, y);
                if ((int)th <= sea_h && g == Block::Water) continue;

                float fx = (float)x;
                float fy = (float)y;
                float ridge = ridge_map[index_of(x, y)];

                float h_c = (float)height_at(x, y);
                float h_e = (float)height_at(x + 1, y);
                float h_w = (float)height_at(x - 1, y);
                float h_n = (float)height_at(x, y - 1);
                float h_s = (float)height_at(x, y + 1);
                float slope = std::sqrt((h_e - h_w) * (h_e - h_w) + (h_s - h_n) * (h_s - h_n));

                float rock_n = fbm(fx * 0.060f + 2100.0f, fy * 0.060f + 2100.0f, 3);
                float boulder_n = fbm(fx * 0.022f + 3300.0f, fy * 0.022f + 3300.0f, 2);
                float fissure = std::fabs(perlin(fx * (cfg.fissure_scale * 1.65f) + 5200.0f,
                                                fy * (cfg.fissure_scale * 1.65f) + 5200.0f) - 0.5f);

                float obj_bias = rock_n + ridge * 0.55f + slope * 0.020f + cfg.detail_object_density;
                if (obj_bias > 1.30f || (boulder_n > 0.79f && slope > 2.1f)) {
                    set(x, y, Block::Stone);
                    continue;
                }

                float ore1 = fbm(fx * 0.11f + 200.0f, fy * 0.11f + 200.0f, 3);
                float ore2 = fbm(fx * 0.09f + 300.0f, fy * 0.09f + 300.0f, 3);
                float ore3 = fbm(fx * 0.14f + 400.0f, fy * 0.14f + 400.0f, 2);

                if (ore1 > 0.88f && (int)th > sea_h + 2) {
                    set(x, y, Block::Iron);
                } else if (ore1 > 0.85f && (int)th > sea_h + 1) {
                    set(x, y, Block::Coal);
                } else if (ore2 > 0.89f && (int)th > sea_h + 2) {
                    set(x, y, Block::Copper);
                } else if (ore3 > 0.91f && (g == Block::Snow || (int)th > snow_h - 2)) {
                    set(x, y, Block::Crystal);
                } else if (ore2 > 0.93f && ore3 > 0.93f) {
                    set(x, y, Block::Metal);
                } else if (fissure < 0.014f && (int)th > sea_h + 3) {
                    set(x, y, Block::Coal); // fendas escuras
                }

                if (get(x, y) == get_ground(x, y) && (int)th > sea_h + 1 && (int)th < snow_h - 2) {
                    float moisture = moist_map[index_of(x, y)];
                    float org = fbm(fx * 0.10f + 500.0f, fy * 0.10f + 500.0f, 2);
                    if (moisture > 0.70f && org > 0.92f) {
                        set(x, y, Block::Organic);
                    }
                }

                if (get(x, y) == get_ground(x, y)) {
                    float dry = 1.0f - moist_map[index_of(x, y)];
                    float tech = fbm(fx * 0.083f + 4200.0f, fy * 0.083f + 4200.0f, 2);
                    if (dry > 0.60f && tech > 0.93f) {
                        set(x, y, Block::Components);
                    }
                }
            }
        }

        rebuild_surface_cache();
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
static bool reload_physics_config(bool create_if_missing);
static bool reload_terrain_config(bool create_if_missing);
static bool reload_sky_config(bool create_if_missing);
static void reset_player_physics_runtime(bool clear_timers = true);
static void step_player_physics(const PlayerPhysicsInput& input, float frame_dt);
static void build_physics_test_map(World& world);

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
    float distance = 5.4f;      // Distancia do jogador
    float yaw = 180.0f;         // Rotacao horizontal (graus)
    float pitch = 18.0f;        // Rotacao vertical (mais baixa para ver o horizonte)
    float min_pitch = 8.0f;
    float max_pitch = 65.0f;
    float min_distance = 2.2f;
    float max_distance = 90.0f;
    float sensitivity = 0.18f;  // Sensibilidade mais suave
    float smooth_speed = 6.0f;  // Suavizacao do seguimento
    
    // Distancia efetiva (apos colisao)
    float effective_distance = 5.4f;
};
static Camera3D g_camera;
static void check_camera_collision();
static constexpr float kCameraSpawnDistance = 5.4f;
static constexpr float kCameraSpawnPitch = 18.0f;
static constexpr float kCameraSpawnYaw = 180.0f;

static void reset_camera_near_player(bool reset_angles) {
    g_camera.distance = std::clamp(kCameraSpawnDistance, g_camera.min_distance, g_camera.max_distance);
    g_camera.effective_distance = g_camera.distance;
    if (reset_angles) {
        g_camera.pitch = std::clamp(kCameraSpawnPitch, g_camera.min_pitch, g_camera.max_pitch);
        g_camera.yaw = kCameraSpawnYaw;
    }
}

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

static void update_camera_for_frame();

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

// Calcular direcao do ray a partir da posicao do mouse na tela
static Vec3 get_mouse_ray_direction(int mouse_x, int mouse_y, int win_w, int win_h) {
    // FOV e aspect ratio usados na projecao
    const float kFov = 74.0f;
    float aspect = (float)win_w / (float)win_h;
    float fov_rad = kFov * (kPi / 180.0f);
    float tan_half_fov = std::tan(fov_rad / 2.0f);
    
    // Normalizar coordenadas do mouse para [-1, 1]
    float ndc_x = (2.0f * (float)mouse_x / (float)win_w) - 1.0f;
    float ndc_y = 1.0f - (2.0f * (float)mouse_y / (float)win_h);  // Invertido porque Y da tela cresce para baixo
    
    // Direcao no espaco da camera (view space)
    float view_x = ndc_x * aspect * tan_half_fov;
    float view_y = ndc_y * tan_half_fov;
    float view_z = -1.0f;  // Aponta para frente da camera
    
    // Obter vetores da camera para transformar de view space para world space
    float yaw_rad = g_camera.yaw * (kPi / 180.0f);
    float pitch_rad = g_camera.pitch * (kPi / 180.0f);
    
    // Vetor forward da camera (para onde ela aponta)
    Vec3 cam_forward = vec3_normalize(vec3_sub(g_camera.target, g_camera.position));
    
    // Vetor right da camera
    Vec3 world_up = {0.0f, 1.0f, 0.0f};
    Vec3 cam_right = vec3_normalize(vec3_cross(cam_forward, world_up));
    
    // Vetor up da camera
    Vec3 cam_up = vec3_cross(cam_right, cam_forward);
    
    // Transformar direcao do view space para world space
    Vec3 ray_dir;
    ray_dir.x = cam_right.x * view_x + cam_up.x * view_y - cam_forward.x * view_z;
    ray_dir.y = cam_right.y * view_x + cam_up.y * view_y - cam_forward.y * view_z;
    ray_dir.z = cam_right.z * view_x + cam_up.z * view_y - cam_forward.z * view_z;
    
    return vec3_normalize(ray_dir);
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
    
    constexpr float kProbeStart = 0.18f;
    constexpr float kProbeStep = 0.18f;
    constexpr float kCollisionPadding = 0.32f;
    constexpr float kMinCollisionDistance = 0.75f;

    // Raycast do target em direcao a camera
    for (float t = kProbeStart; t < max_dist; t += kProbeStep) {
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
            float safe_dist = t - kCollisionPadding;
            g_camera.effective_distance = std::clamp(safe_dist, kMinCollisionDistance, g_camera.distance);
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
    
    // === JETPACK ===
    bool jetpack_active = false;  // Jetpack esta ativo
    float jetpack_fuel = 100.0f;  // Combustivel do jetpack (0-100)
    float jetpack_flame_anim = 0.0f; // Animacao da chama
    
    // Movimento suave
    float speed_mult = 1.0f;  // Multiplicador de velocidade (acelera gradualmente)
};

static Player g_player;

enum class TerrainPhysicsType : uint8_t {
    Normal = 0,
    Ice,
    Sand,
    Stone,
    Mud,
};

struct PhysicsConfig {
    float fixed_timestep = 1.0f / 120.0f;
    int max_substeps = 10;

    float max_speed = 4.8f;
    float run_multiplier = 1.42f;
    float ground_acceleration = 26.0f;
    float ground_deceleration = 22.0f;
    float air_acceleration = 9.0f;
    float air_deceleration = 6.5f;
    float ground_friction = 19.0f;
    float air_friction = 1.4f;

    float gravity = 24.0f;
    float rise_multiplier = 1.0f;
    float fall_multiplier = 2.05f;
    float jump_velocity = 8.1f;
    float jump_buffer = 0.12f;
    float coyote_time = 0.10f;
    float jump_cancel_multiplier = 2.8f;
    float terminal_velocity = 38.0f;

    float ground_snap = 0.20f;
    float ground_tolerance = 0.06f;

    float step_height = 0.62f;
    float step_probe_distance = 0.54f;

    float slope_limit_normal_y = 0.70f;
    float slope_slide_accel = 7.5f;
    float slope_uphill_speed_mult = 0.82f;
    float slope_downhill_speed_mult = 1.08f;

    float max_move_per_substep = 0.34f;
    float collision_skin = 0.0015f;
    float collider_width = 0.62f;
    float collider_depth = 0.62f;
    float collider_height = 1.80f;
    float rotation_smoothing = 14.0f;

    float terrain_ice_speed = 1.04f;
    float terrain_ice_accel = 0.55f;
    float terrain_ice_friction = 0.18f;
    float terrain_sand_speed = 0.74f;
    float terrain_sand_accel = 0.80f;
    float terrain_sand_friction = 1.30f;
    float terrain_stone_speed = 1.00f;
    float terrain_stone_accel = 1.00f;
    float terrain_stone_friction = 1.00f;
    float terrain_mud_speed = 0.58f;
    float terrain_mud_accel = 0.65f;
    float terrain_mud_friction = 1.95f;

    float jetpack_thrust = 12.0f;
    float jetpack_fuel_consume = 15.0f;
    float jetpack_fuel_regen = 25.0f;
    float jetpack_gravity_mult = 0.35f;
    float jetpack_max_up_speed = 6.0f;
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
    TerrainPhysicsType terrain = TerrainPhysicsType::Normal;
    std::string terrain_name = "Normal";
    Vec3 ground_normal = {0.0f, 1.0f, 0.0f};
    Vec2 collision_normal = {0.0f, 0.0f};

    std::array<PhysicsRayDebug, 8> debug_rays = {};
    int debug_ray_count = 0;
};

struct PlayerPhysicsInput {
    Vec2 move = {0.0f, 0.0f};
    bool has_move = false;
    bool run = false;
    bool jump_pressed = false;
    bool jump_held = false;
    bool jump_released = false;
};

static PhysicsConfig g_physics_cfg = {};
static PhysicsRuntime g_physics = {};
static std::string g_physics_config_path = "physics_config.json";

static Vec2 get_player_render_pos() { return g_physics.render_pos; }
static float get_player_render_y() { return g_physics.render_pos_y; }
static float get_player_render_rotation() { return g_physics.render_rotation; }

// Atualiza alvo/posicao da camera para o frame atual (inclui offset para estilo Minicraft: player levemente fora do centro).
static void update_camera_for_frame() {
    // Alvo base (altura do jogador + offset para ver horizonte)
    Vec2 rpos = get_player_render_pos();
    float ry = get_player_render_y();
    Vec3 base_target = {rpos.x, ry + 1.10f, rpos.y};

    // Sem offset horizontal - mira centralizada no jogador
    g_camera.target = base_target;

    // Distancia efetiva (colisao)
    g_camera.effective_distance = g_camera.distance;
    update_camera_position();
    check_camera_collision();
    update_camera_position();
}

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
static bool g_prev_f6 = false;
static bool g_prev_f7 = false;
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

// Eventos do ceu: estrelas cadentes (camera-relative para parecer "longe" do mundo).
struct ShootingStar {
    Vec3 offset;   // relativo ao camera (x/z). y em coordenada absoluta do ceu
    Vec3 vel;      // unidades por segundo (no espaco do offset)
    float life = 0.0f;
    float max_life = 0.0f;
    float length = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f;
};
static std::vector<ShootingStar> g_shooting_stars;

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
static int g_target_drop = -1; // indice em g_drops sob a mira (se houver)

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

// ============= SISTEMA DE ILUMINACAO 2D AVANCADA (RTX FAKE) =============
// Estrutura para fontes de luz dinamicas
struct Light2D {
    float x, y;           // Posicao no mundo 2D (tiles)
    float height;         // Altura Y no espaco 3D
    float radius;         // Raio de influencia (tiles)
    float intensity;      // Intensidade (0-1)
    float r, g, b;        // Cor RGB (0-1)
    float falloff;        // Tipo de atenuacao (1=linear, 2=quadratica)
    bool flicker;         // Luz piscante
    float flicker_speed;  // Velocidade do flicker
    bool is_emissive;     // Se a fonte emite glow/bloom
};

// Vetor de luzes ativas no frame
static std::vector<Light2D> g_lights;

// Lightmap - grade 2D para iluminacao acumulada
static constexpr int kLightmapSize = 96;      // Resolucao do lightmap (tiles)
static constexpr int kLightmapPixels = kLightmapSize * kLightmapSize;
static std::vector<float> g_lightmap_r(kLightmapPixels, 1.0f);
static std::vector<float> g_lightmap_g(kLightmapPixels, 1.0f);
static std::vector<float> g_lightmap_b(kLightmapPixels, 1.0f);

// Bloom buffer - para efeito de glow
static std::vector<float> g_bloom_r(kLightmapPixels, 0.0f);
static std::vector<float> g_bloom_g(kLightmapPixels, 0.0f);
static std::vector<float> g_bloom_b(kLightmapPixels, 0.0f);

// Buffer temporario para blur
static std::vector<float> g_temp_r(kLightmapPixels, 0.0f);
static std::vector<float> g_temp_g(kLightmapPixels, 0.0f);
static std::vector<float> g_temp_b(kLightmapPixels, 0.0f);

// Centro do lightmap no mundo (para mapeamento de coordenadas)
static int g_lightmap_center_x = 0;
static int g_lightmap_center_z = 0;

// Configuracoes de iluminacao
static struct LightingSettings {
    bool enabled = true;
    bool shadows_enabled = true;
    bool bloom_enabled = true;
    float bloom_intensity = 0.45f;
    float bloom_threshold = 0.75f;
    float shadow_softness = 0.6f;
    int shadow_samples = 8;          // Passos do raymarching
    float ambient_min = 0.06f;       // Luz ambiente minima (noite)
    float ambient_max = 0.92f;       // Luz ambiente maxima (dia)
    float contrast = 1.12f;
    float exposure = 1.05f;
    float saturation = 1.08f;
    float vignette_intensity = 0.25f;
    float vignette_radius = 0.85f;
    float depth_darkening = 0.5f;    // Escurecimento por profundidade
    bool color_grading = true;
} g_lighting;

// Debug
static bool g_debug_lightmap = false;
static bool g_debug_bloom = false;
static bool g_debug_lights = false;

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
    float cam_distance = kCameraSpawnDistance, cam_yaw = 180.0f, cam_pitch = kCameraSpawnPitch, cam_sensitivity = 0.25f;
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
    g_shooting_stars.clear();
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
    reset_camera_near_player(false);

    g_cam_pos = g_player.pos;
    reset_player_physics_runtime(true);
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
                    g_player.pos_y = surface_height_at(world, tx, ty);
                    g_player.ground_height = g_player.pos_y;
                    g_player.on_ground = true;
                    reset_player_physics_runtime(true);
                    return;
                }
            }
        }
    }
    // Fallback
    g_player.pos = {(float)x + 0.5f, (float)y + 0.5f};
    g_player.vel = {0.0f, 0.0f};
    g_player.pos_y = surface_height_at(world, x, y);
    g_player.ground_height = g_player.pos_y;
    g_player.on_ground = true;
    reset_player_physics_runtime(true);
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
    g_player.w = g_physics_cfg.collider_width;
    g_player.h = g_physics_cfg.collider_depth;
    reset_camera_near_player(true);
    reset_player_physics_runtime(true);
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

static void build_physics_test_map(World& world) {
    const int cz = world.h / 2;
    const int x0 = 24;
    const int x1 = std::min(world.w - 24, x0 + 380);
    const int z0 = std::max(4, cz - 40);
    const int z1 = std::min(world.h - 5, cz + 40);
    const int16_t base_h = 24;

    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            world.set(x, z, Block::Air);
            world.set_ground(x, z, Block::Stone);
            world.set_height(x, z, base_h);
        }
    }

    // Lanes de material: gelo, areia, pedra e lama.
    for (int x = x0; x <= x1; ++x) {
        for (int z = cz - 34; z <= cz - 26; ++z) world.set_ground(x, z, Block::Ice);
        for (int z = cz - 20; z <= cz - 12; ++z) world.set_ground(x, z, Block::Sand);
        for (int z = cz - 6; z <= cz + 2; ++z) world.set_ground(x, z, Block::Stone);
        for (int z = cz + 8; z <= cz + 16; ++z) world.set_ground(x, z, Block::Organic);
    }

    // Buracos e gaps.
    for (int x = 72; x <= 94; ++x) {
        for (int z = cz - 2; z <= cz + 2; ++z) world.set_height(x, z, 8);
    }
    for (int x = 146; x <= 157; ++x) {
        for (int z = cz + 10; z <= cz + 16; ++z) world.set_height(x, z, 4);
    }

    // Escadas.
    for (int i = 0; i < 10; ++i) {
        int sx = 110 + i * 2;
        int16_t h = (int16_t)(base_h + i * 2);
        for (int x = sx; x < sx + 2; ++x) {
            for (int z = cz + 20; z <= cz + 26; ++z) world.set_height(x, z, h);
        }
    }

    // Rampa longa.
    for (int x = 190; x <= 256; ++x) {
        int16_t h = (int16_t)(base_h + (x - 190) / 3);
        for (int z = cz + 22; z <= cz + 34; ++z) world.set_height(x, z, h);
    }

    // Plataformas altas.
    for (int x = 300; x <= 332; ++x) {
        for (int z = cz - 14; z <= cz - 2; ++z) world.set_height(x, z, base_h + 16);
    }
    for (int x = 334; x <= 366; ++x) {
        for (int z = cz - 14; z <= cz - 2; ++z) world.set_height(x, z, base_h + 24);
    }

    // Obstaculos para testar colisao/step.
    for (int x = 214; x <= 224; x += 2) world.set(x, cz - 1, Block::Stone);
    for (int x = 238; x <= 248; x += 2) world.set(x, cz - 1, Block::Iron);
    world.set(272, cz + 12, Block::Copper);
    world.set(274, cz + 12, Block::Coal);
    world.set(276, cz + 12, Block::Crystal);

    // Degraus baixos de 1 tile para step-climb.
    for (int i = 0; i < 8; ++i) {
        int x = 40 + i * 6;
        int16_t h = (int16_t)(base_h + ((i & 1) ? 2 : 1));
        for (int z = cz - 10; z <= cz - 6; ++z) world.set_height(x, z, h);
    }

    world.rebuild_surface_cache();
    g_surface_dirty = true;
    g_modules.clear();
    g_construction_queue.clear();
    g_alerts.clear();
    g_build_slots.clear();
    rebuild_modules_from_world();

    g_base_x = x0 + 8;
    g_base_y = cz - 1;
    spawn_player_at_base();
    g_cam_pos = g_player.pos;
    set_toast("Mapa de teste de fisica carregado (F6).", 4.0f);
}

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
struct TerrainPhysicsProfile {
    float speed_mult = 1.0f;
    float accel_mult = 1.0f;
    float decel_mult = 1.0f;
    float friction_mult = 1.0f;
    float slide_mult = 1.0f;
    const char* label = "Normal";
};

struct GroundProbeResult {
    bool has_hit = false;
    bool grounded = false;
    float height = 0.0f;
    Block surface = Block::Dirt;
    TerrainPhysicsType terrain = TerrainPhysicsType::Normal;
    Vec3 normal = {0.0f, 1.0f, 0.0f};
};

static bool file_exists(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool parse_json_number(const std::string& text, const char* key, float& out_value) {
    std::string needle = "\"";
    needle += key;
    needle += "\"";
    size_t key_pos = text.find(needle);
    if (key_pos == std::string::npos) return false;
    size_t colon = text.find(':', key_pos + needle.size());
    if (colon == std::string::npos) return false;
    const char* begin = text.c_str() + colon + 1;
    while (*begin == ' ' || *begin == '\t' || *begin == '\r' || *begin == '\n') begin++;
    char* end = nullptr;
    float parsed = std::strtof(begin, &end);
    if (end == begin) return false;
    out_value = parsed;
    return true;
}

static void write_default_physics_config(const std::string& path) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    f <<
"{\n"
"  \"fixed_timestep\": 0.008333333,\n"
"  \"max_substeps\": 10,\n"
"  \"max_speed\": 4.8,\n"
"  \"run_multiplier\": 1.42,\n"
"  \"ground_acceleration\": 26.0,\n"
"  \"ground_deceleration\": 22.0,\n"
"  \"air_acceleration\": 9.0,\n"
"  \"air_deceleration\": 6.5,\n"
"  \"ground_friction\": 19.0,\n"
"  \"air_friction\": 1.4,\n"
"  \"gravity\": 24.0,\n"
"  \"rise_multiplier\": 1.0,\n"
"  \"fall_multiplier\": 2.05,\n"
"  \"jump_velocity\": 8.1,\n"
"  \"jump_buffer\": 0.12,\n"
"  \"coyote_time\": 0.10,\n"
"  \"jump_cancel_multiplier\": 2.8,\n"
"  \"terminal_velocity\": 38.0,\n"
"  \"ground_snap\": 0.20,\n"
"  \"ground_tolerance\": 0.06,\n"
"  \"step_height\": 0.62,\n"
"  \"step_probe_distance\": 0.54,\n"
"  \"slope_limit_normal_y\": 0.70,\n"
"  \"slope_slide_accel\": 7.5,\n"
"  \"slope_uphill_speed_mult\": 0.82,\n"
"  \"slope_downhill_speed_mult\": 1.08,\n"
"  \"max_move_per_substep\": 0.34,\n"
"  \"collision_skin\": 0.0015,\n"
"  \"collider_width\": 0.62,\n"
"  \"collider_depth\": 0.62,\n"
"  \"collider_height\": 1.80,\n"
"  \"rotation_smoothing\": 14.0,\n"
"  \"terrain_ice_speed\": 1.04,\n"
"  \"terrain_ice_accel\": 0.55,\n"
"  \"terrain_ice_friction\": 0.18,\n"
"  \"terrain_sand_speed\": 0.74,\n"
"  \"terrain_sand_accel\": 0.80,\n"
"  \"terrain_sand_friction\": 1.30,\n"
"  \"terrain_stone_speed\": 1.00,\n"
"  \"terrain_stone_accel\": 1.00,\n"
"  \"terrain_stone_friction\": 1.00,\n"
"  \"terrain_mud_speed\": 0.58,\n"
"  \"terrain_mud_accel\": 0.65,\n"
"  \"terrain_mud_friction\": 1.95,\n"
"  \"jetpack_thrust\": 12.0,\n"
"  \"jetpack_fuel_consume\": 15.0,\n"
"  \"jetpack_fuel_regen\": 25.0,\n"
"  \"jetpack_gravity_mult\": 0.35,\n"
"  \"jetpack_max_up_speed\": 6.0\n"
"}\n";
}

static void apply_physics_config_overrides(const std::string& text, PhysicsConfig& cfg) {
    auto setf = [&](const char* key, float& value) {
        float parsed = 0.0f;
        if (parse_json_number(text, key, parsed)) value = parsed;
    };

    setf("fixed_timestep", cfg.fixed_timestep);
    setf("max_speed", cfg.max_speed);
    setf("run_multiplier", cfg.run_multiplier);
    setf("ground_acceleration", cfg.ground_acceleration);
    setf("ground_deceleration", cfg.ground_deceleration);
    setf("air_acceleration", cfg.air_acceleration);
    setf("air_deceleration", cfg.air_deceleration);
    setf("ground_friction", cfg.ground_friction);
    setf("air_friction", cfg.air_friction);
    setf("gravity", cfg.gravity);
    setf("rise_multiplier", cfg.rise_multiplier);
    setf("fall_multiplier", cfg.fall_multiplier);
    setf("jump_velocity", cfg.jump_velocity);
    setf("jump_buffer", cfg.jump_buffer);
    setf("coyote_time", cfg.coyote_time);
    setf("jump_cancel_multiplier", cfg.jump_cancel_multiplier);
    setf("terminal_velocity", cfg.terminal_velocity);
    setf("ground_snap", cfg.ground_snap);
    setf("ground_tolerance", cfg.ground_tolerance);
    setf("step_height", cfg.step_height);
    setf("step_probe_distance", cfg.step_probe_distance);
    setf("slope_limit_normal_y", cfg.slope_limit_normal_y);
    setf("slope_slide_accel", cfg.slope_slide_accel);
    setf("slope_uphill_speed_mult", cfg.slope_uphill_speed_mult);
    setf("slope_downhill_speed_mult", cfg.slope_downhill_speed_mult);
    setf("max_move_per_substep", cfg.max_move_per_substep);
    setf("collision_skin", cfg.collision_skin);
    setf("collider_width", cfg.collider_width);
    setf("collider_depth", cfg.collider_depth);
    setf("collider_height", cfg.collider_height);
    setf("rotation_smoothing", cfg.rotation_smoothing);

    setf("terrain_ice_speed", cfg.terrain_ice_speed);
    setf("terrain_ice_accel", cfg.terrain_ice_accel);
    setf("terrain_ice_friction", cfg.terrain_ice_friction);
    setf("terrain_sand_speed", cfg.terrain_sand_speed);
    setf("terrain_sand_accel", cfg.terrain_sand_accel);
    setf("terrain_sand_friction", cfg.terrain_sand_friction);
    setf("terrain_stone_speed", cfg.terrain_stone_speed);
    setf("terrain_stone_accel", cfg.terrain_stone_accel);
    setf("terrain_stone_friction", cfg.terrain_stone_friction);
    setf("terrain_mud_speed", cfg.terrain_mud_speed);
    setf("terrain_mud_accel", cfg.terrain_mud_accel);
    setf("terrain_mud_friction", cfg.terrain_mud_friction);

    setf("jetpack_thrust", cfg.jetpack_thrust);
    setf("jetpack_fuel_consume", cfg.jetpack_fuel_consume);
    setf("jetpack_fuel_regen", cfg.jetpack_fuel_regen);
    setf("jetpack_gravity_mult", cfg.jetpack_gravity_mult);
    setf("jetpack_max_up_speed", cfg.jetpack_max_up_speed);

    float substeps = (float)cfg.max_substeps;
    setf("max_substeps", substeps);
    cfg.max_substeps = std::max(1, (int)std::lround(substeps));

    cfg.fixed_timestep = std::clamp(cfg.fixed_timestep, 1.0f / 360.0f, 1.0f / 20.0f);
    cfg.max_speed = std::max(0.1f, cfg.max_speed);
    cfg.run_multiplier = std::max(1.0f, cfg.run_multiplier);
    cfg.ground_acceleration = std::max(0.0f, cfg.ground_acceleration);
    cfg.ground_deceleration = std::max(0.0f, cfg.ground_deceleration);
    cfg.air_acceleration = std::max(0.0f, cfg.air_acceleration);
    cfg.air_deceleration = std::max(0.0f, cfg.air_deceleration);
    cfg.gravity = std::max(0.0f, cfg.gravity);
    cfg.fall_multiplier = std::max(cfg.rise_multiplier + 0.01f, cfg.fall_multiplier);
    cfg.jump_velocity = std::max(0.0f, cfg.jump_velocity);
    cfg.jump_buffer = std::clamp(cfg.jump_buffer, 0.0f, 0.35f);
    cfg.coyote_time = std::clamp(cfg.coyote_time, 0.0f, 0.35f);
    cfg.jump_cancel_multiplier = std::max(1.0f, cfg.jump_cancel_multiplier);
    cfg.terminal_velocity = std::max(1.0f, cfg.terminal_velocity);
    cfg.step_height = std::clamp(cfg.step_height, 0.0f, 1.25f);
    cfg.collider_height = std::clamp(cfg.collider_height, 1.0f, 2.5f);
    cfg.collider_width = std::clamp(cfg.collider_width, 0.3f, 1.2f);
    cfg.collider_depth = std::clamp(cfg.collider_depth, 0.3f, 1.2f);
    cfg.max_move_per_substep = std::clamp(cfg.max_move_per_substep, 0.05f, 0.95f);
    cfg.collision_skin = std::clamp(cfg.collision_skin, 0.0002f, 0.02f);
    cfg.slope_limit_normal_y = std::clamp(cfg.slope_limit_normal_y, 0.10f, 0.98f);
}

static bool reload_physics_config(bool create_if_missing) {
    const char* candidates[] = {
        "physics_config.json",
        "..\\physics_config.json",
        "..\\..\\physics_config.json",
        "..\\..\\..\\physics_config.json"
    };

    std::string chosen_path;
    for (const char* c : candidates) {
        if (file_exists(c)) {
            chosen_path = c;
            break;
        }
    }

    if (chosen_path.empty()) {
        chosen_path = candidates[0];
        if (create_if_missing) write_default_physics_config(chosen_path);
    }

    PhysicsConfig cfg = PhysicsConfig{};
    bool loaded = false;
    std::ifstream f(chosen_path);
    if (f) {
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        apply_physics_config_overrides(text, cfg);
        loaded = true;
    } else if (create_if_missing) {
        write_default_physics_config(chosen_path);
    }

    g_physics_cfg = cfg;
    g_physics_config_path = chosen_path;
    return loaded;
}

static void write_default_terrain_config(const std::string& path) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    f <<
"{\n"
"  \"macro_scale\": 0.00115,\n"
"  \"ridge_scale\": 0.0048,\n"
"  \"valley_scale\": 0.0020,\n"
"  \"detail_scale\": 0.0180,\n"
"  \"warp_scale\": 0.0032,\n"
"  \"warp_strength\": 26.0,\n"
"  \"macro_weight\": 0.52,\n"
"  \"ridge_weight\": 0.76,\n"
"  \"valley_weight\": 0.42,\n"
"  \"detail_weight\": 0.10,\n"
"  \"plateau_level\": 0.62,\n"
"  \"plateau_flatten\": 0.30,\n"
"  \"min_height\": 2.0,\n"
"  \"max_height\": 116.0,\n"
"  \"sea_height\": 12.0,\n"
"  \"snow_height\": 88.0,\n"
"  \"thermal_erosion_passes\": 4,\n"
"  \"hydraulic_erosion_passes\": 3,\n"
"  \"smooth_passes\": 1,\n"
"  \"erosion_strength\": 0.34,\n"
"  \"thermal_talus\": 0.026,\n"
"  \"temp_scale\": 0.0016,\n"
"  \"moisture_scale\": 0.0019,\n"
"  \"biome_blend\": 0.18,\n"
"  \"fissure_scale\": 0.010,\n"
"  \"fissure_depth\": 0.090,\n"
"  \"crater_scale\": 0.0050,\n"
"  \"crater_depth\": 0.075,\n"
"  \"detail_object_density\": 0.090\n"
"}\n";
}

static void apply_terrain_config_overrides(const std::string& text, TerrainConfig& cfg) {
    auto setf = [&](const char* key, float& value) {
        float parsed = 0.0f;
        if (parse_json_number(text, key, parsed)) value = parsed;
    };
    auto seti = [&](const char* key, int& value) {
        float parsed = 0.0f;
        if (parse_json_number(text, key, parsed)) value = (int)std::lround(parsed);
    };

    setf("macro_scale", cfg.macro_scale);
    setf("ridge_scale", cfg.ridge_scale);
    setf("valley_scale", cfg.valley_scale);
    setf("detail_scale", cfg.detail_scale);
    setf("warp_scale", cfg.warp_scale);
    setf("warp_strength", cfg.warp_strength);
    setf("macro_weight", cfg.macro_weight);
    setf("ridge_weight", cfg.ridge_weight);
    setf("valley_weight", cfg.valley_weight);
    setf("detail_weight", cfg.detail_weight);
    setf("plateau_level", cfg.plateau_level);
    setf("plateau_flatten", cfg.plateau_flatten);
    setf("min_height", cfg.min_height);
    setf("max_height", cfg.max_height);
    setf("sea_height", cfg.sea_height);
    setf("snow_height", cfg.snow_height);
    seti("thermal_erosion_passes", cfg.thermal_erosion_passes);
    seti("hydraulic_erosion_passes", cfg.hydraulic_erosion_passes);
    seti("smooth_passes", cfg.smooth_passes);
    setf("erosion_strength", cfg.erosion_strength);
    setf("thermal_talus", cfg.thermal_talus);
    setf("temp_scale", cfg.temp_scale);
    setf("moisture_scale", cfg.moisture_scale);
    setf("biome_blend", cfg.biome_blend);
    setf("fissure_scale", cfg.fissure_scale);
    setf("fissure_depth", cfg.fissure_depth);
    setf("crater_scale", cfg.crater_scale);
    setf("crater_depth", cfg.crater_depth);
    setf("detail_object_density", cfg.detail_object_density);

    cfg.macro_scale = std::clamp(cfg.macro_scale, 0.0001f, 0.02f);
    cfg.ridge_scale = std::clamp(cfg.ridge_scale, 0.0005f, 0.04f);
    cfg.valley_scale = std::clamp(cfg.valley_scale, 0.0003f, 0.03f);
    cfg.detail_scale = std::clamp(cfg.detail_scale, 0.002f, 0.10f);
    cfg.warp_scale = std::clamp(cfg.warp_scale, 0.0003f, 0.03f);
    cfg.warp_strength = std::clamp(cfg.warp_strength, 0.0f, 80.0f);
    cfg.plateau_level = std::clamp(cfg.plateau_level, 0.25f, 0.9f);
    cfg.plateau_flatten = std::clamp(cfg.plateau_flatten, 0.0f, 0.8f);
    cfg.min_height = std::clamp(cfg.min_height, 0.0f, 160.0f);
    cfg.max_height = std::clamp(cfg.max_height, cfg.min_height + 4.0f, 255.0f);
    cfg.sea_height = std::clamp(cfg.sea_height, cfg.min_height, cfg.max_height - 1.0f);
    cfg.snow_height = std::clamp(cfg.snow_height, cfg.sea_height + 1.0f, cfg.max_height);
    cfg.thermal_erosion_passes = std::clamp(cfg.thermal_erosion_passes, 0, 12);
    cfg.hydraulic_erosion_passes = std::clamp(cfg.hydraulic_erosion_passes, 0, 12);
    cfg.smooth_passes = std::clamp(cfg.smooth_passes, 0, 8);
    cfg.erosion_strength = std::clamp(cfg.erosion_strength, 0.0f, 1.0f);
    cfg.thermal_talus = std::clamp(cfg.thermal_talus, 0.001f, 0.2f);
    cfg.temp_scale = std::clamp(cfg.temp_scale, 0.0002f, 0.02f);
    cfg.moisture_scale = std::clamp(cfg.moisture_scale, 0.0002f, 0.02f);
    cfg.biome_blend = std::clamp(cfg.biome_blend, 0.0f, 1.0f);
    cfg.fissure_scale = std::clamp(cfg.fissure_scale, 0.0005f, 0.05f);
    cfg.fissure_depth = std::clamp(cfg.fissure_depth, 0.0f, 0.4f);
    cfg.crater_scale = std::clamp(cfg.crater_scale, 0.0005f, 0.05f);
    cfg.crater_depth = std::clamp(cfg.crater_depth, 0.0f, 0.4f);
    cfg.detail_object_density = std::clamp(cfg.detail_object_density, 0.0f, 0.4f);
}

static bool reload_terrain_config(bool create_if_missing) {
    const char* candidates[] = {
        "terrain_config.json",
        "..\\terrain_config.json",
        "..\\..\\terrain_config.json",
        "..\\..\\..\\terrain_config.json"
    };

    std::string chosen_path;
    for (const char* c : candidates) {
        if (file_exists(c)) {
            chosen_path = c;
            break;
        }
    }

    if (chosen_path.empty()) {
        chosen_path = candidates[0];
        if (create_if_missing) write_default_terrain_config(chosen_path);
    }

    TerrainConfig cfg = TerrainConfig{};
    bool loaded = false;
    std::ifstream f(chosen_path);
    if (f) {
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        apply_terrain_config_overrides(text, cfg);
        loaded = true;
    } else if (create_if_missing) {
        write_default_terrain_config(chosen_path);
    }

    g_terrain_cfg = cfg;
    g_terrain_config_path = chosen_path;
    return loaded;
}

static void write_default_sky_config(const std::string& path) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    f <<
"{\n"
"  \"stars_density\": 1250.0,\n"
"  \"stars_parallax\": 0.010,\n"
"  \"nebula_alpha\": 0.17,\n"
"  \"nebula_parallax\": 0.020,\n"
"  \"cloud_alpha\": 0.14,\n"
"  \"cloud_parallax\": 0.060,\n"
"  \"planet_radius\": 132.0,\n"
"  \"planet_distance\": 1180.0,\n"
"  \"planet_orbit_speed\": 0.085,\n"
"  \"planet_parallax\": 0.034,\n"
"  \"sun_radius\": 44.0,\n"
"  \"sun_distance\": 760.0,\n"
"  \"sun_halo_size\": 1.90,\n"
"  \"bloom_intensity\": 0.30,\n"
"  \"moon_radius\": 31.0,\n"
"  \"moon_distance\": 900.0,\n"
"  \"moon_orbit_speed\": 0.55,\n"
"  \"moon_parallax\": 0.050,\n"
"  \"moon2_radius\": 18.0,\n"
"  \"moon2_distance\": 980.0,\n"
"  \"moon2_orbit_speed\": 1.15,\n"
"  \"moon2_parallax\": 0.060,\n"
"  \"atmosphere_horizon_boost\": 0.32,\n"
"  \"atmosphere_zenith_boost\": 0.17,\n"
"  \"horizon_fade\": 0.24,\n"
"  \"fog_start_factor\": 0.40,\n"
"  \"fog_end_factor\": 0.92,\n"
"  \"fog_distance_bonus\": 22.0,\n"
"  \"eclipse_frequency_days\": 6.0,\n"
"  \"eclipse_strength\": 0.45\n"
"}\n";
}

static void apply_sky_config_overrides(const std::string& text, SkyConfig& cfg) {
    auto setf = [&](const char* key, float& value) {
        float parsed = 0.0f;
        if (parse_json_number(text, key, parsed)) value = parsed;
    };

    setf("stars_density", cfg.stars_density);
    setf("stars_parallax", cfg.stars_parallax);
    setf("nebula_alpha", cfg.nebula_alpha);
    setf("nebula_parallax", cfg.nebula_parallax);
    setf("cloud_alpha", cfg.cloud_alpha);
    setf("cloud_parallax", cfg.cloud_parallax);
    setf("planet_radius", cfg.planet_radius);
    setf("planet_distance", cfg.planet_distance);
    setf("planet_orbit_speed", cfg.planet_orbit_speed);
    setf("planet_parallax", cfg.planet_parallax);
    setf("sun_radius", cfg.sun_radius);
    setf("sun_distance", cfg.sun_distance);
    setf("sun_halo_size", cfg.sun_halo_size);
    setf("bloom_intensity", cfg.bloom_intensity);
    setf("moon_radius", cfg.moon_radius);
    setf("moon_distance", cfg.moon_distance);
    setf("moon_orbit_speed", cfg.moon_orbit_speed);
    setf("moon_parallax", cfg.moon_parallax);
    setf("moon2_radius", cfg.moon2_radius);
    setf("moon2_distance", cfg.moon2_distance);
    setf("moon2_orbit_speed", cfg.moon2_orbit_speed);
    setf("moon2_parallax", cfg.moon2_parallax);
    setf("atmosphere_horizon_boost", cfg.atmosphere_horizon_boost);
    setf("atmosphere_zenith_boost", cfg.atmosphere_zenith_boost);
    setf("horizon_fade", cfg.horizon_fade);
    setf("fog_start_factor", cfg.fog_start_factor);
    setf("fog_end_factor", cfg.fog_end_factor);
    setf("fog_distance_bonus", cfg.fog_distance_bonus);
    setf("eclipse_frequency_days", cfg.eclipse_frequency_days);
    setf("eclipse_strength", cfg.eclipse_strength);

    cfg.stars_density = std::clamp(cfg.stars_density, 100.0f, 4000.0f);
    cfg.stars_parallax = std::clamp(cfg.stars_parallax, 0.0f, 0.15f);
    cfg.nebula_alpha = std::clamp(cfg.nebula_alpha, 0.0f, 1.0f);
    cfg.nebula_parallax = std::clamp(cfg.nebula_parallax, 0.0f, 0.2f);
    cfg.cloud_alpha = std::clamp(cfg.cloud_alpha, 0.0f, 1.0f);
    cfg.cloud_parallax = std::clamp(cfg.cloud_parallax, 0.0f, 0.2f);
    cfg.planet_radius = std::clamp(cfg.planet_radius, 20.0f, 500.0f);
    cfg.planet_distance = std::clamp(cfg.planet_distance, 300.0f, 3000.0f);
    cfg.planet_orbit_speed = std::clamp(cfg.planet_orbit_speed, 0.0f, 5.0f);
    cfg.planet_parallax = std::clamp(cfg.planet_parallax, 0.0f, 0.3f);
    cfg.sun_radius = std::clamp(cfg.sun_radius, 8.0f, 180.0f);
    cfg.sun_distance = std::clamp(cfg.sun_distance, 200.0f, 2500.0f);
    cfg.sun_halo_size = std::clamp(cfg.sun_halo_size, 1.0f, 4.0f);
    cfg.bloom_intensity = std::clamp(cfg.bloom_intensity, 0.0f, 1.5f);
    cfg.moon_radius = std::clamp(cfg.moon_radius, 5.0f, 150.0f);
    cfg.moon_distance = std::clamp(cfg.moon_distance, 200.0f, 3000.0f);
    cfg.moon_orbit_speed = std::clamp(cfg.moon_orbit_speed, 0.0f, 8.0f);
    cfg.moon_parallax = std::clamp(cfg.moon_parallax, 0.0f, 0.3f);
    cfg.moon2_radius = std::clamp(cfg.moon2_radius, 4.0f, 140.0f);
    cfg.moon2_distance = std::clamp(cfg.moon2_distance, 200.0f, 3000.0f);
    cfg.moon2_orbit_speed = std::clamp(cfg.moon2_orbit_speed, 0.0f, 8.0f);
    cfg.moon2_parallax = std::clamp(cfg.moon2_parallax, 0.0f, 0.3f);
    cfg.atmosphere_horizon_boost = std::clamp(cfg.atmosphere_horizon_boost, 0.0f, 1.0f);
    cfg.atmosphere_zenith_boost = std::clamp(cfg.atmosphere_zenith_boost, 0.0f, 1.0f);
    cfg.horizon_fade = std::clamp(cfg.horizon_fade, 0.0f, 1.0f);
    cfg.fog_start_factor = std::clamp(cfg.fog_start_factor, 0.1f, 0.9f);
    cfg.fog_end_factor = std::clamp(cfg.fog_end_factor, cfg.fog_start_factor + 0.05f, 1.3f);
    cfg.fog_distance_bonus = std::clamp(cfg.fog_distance_bonus, 0.0f, 160.0f);
    cfg.eclipse_frequency_days = std::clamp(cfg.eclipse_frequency_days, 0.5f, 40.0f);
    cfg.eclipse_strength = std::clamp(cfg.eclipse_strength, 0.0f, 1.0f);
}

static bool reload_sky_config(bool create_if_missing) {
    const char* candidates[] = {
        "sky_config.json",
        "..\\sky_config.json",
        "..\\..\\sky_config.json",
        "..\\..\\..\\sky_config.json"
    };

    std::string chosen_path;
    for (const char* c : candidates) {
        if (file_exists(c)) {
            chosen_path = c;
            break;
        }
    }

    if (chosen_path.empty()) {
        chosen_path = candidates[0];
        if (create_if_missing) write_default_sky_config(chosen_path);
    }

    SkyConfig cfg = SkyConfig{};
    bool loaded = false;
    std::ifstream f(chosen_path);
    if (f) {
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        apply_sky_config_overrides(text, cfg);
        loaded = true;
    } else if (create_if_missing) {
        write_default_sky_config(chosen_path);
    }

    g_sky_cfg = cfg;
    g_sky_config_path = chosen_path;
    return loaded;
}

static TerrainPhysicsType terrain_type_from_block(Block b) {
    switch (b) {
        case Block::Ice:
            return TerrainPhysicsType::Ice;
        case Block::Sand:
            return TerrainPhysicsType::Sand;
        case Block::Stone:
        case Block::Coal:
        case Block::Iron:
        case Block::Copper:
        case Block::Crystal:
        case Block::Metal:
        case Block::Components:
            return TerrainPhysicsType::Stone;
        case Block::Organic:
            return TerrainPhysicsType::Mud;
        default:
            return TerrainPhysicsType::Normal;
    }
}

static TerrainPhysicsProfile terrain_profile_for(TerrainPhysicsType t, const PhysicsConfig& cfg) {
    TerrainPhysicsProfile p{};
    switch (t) {
        case TerrainPhysicsType::Ice:
            p.speed_mult = cfg.terrain_ice_speed;
            p.accel_mult = cfg.terrain_ice_accel;
            p.decel_mult = cfg.terrain_ice_accel;
            p.friction_mult = cfg.terrain_ice_friction;
            p.slide_mult = 1.45f;
            p.label = "Gelo";
            break;
        case TerrainPhysicsType::Sand:
            p.speed_mult = cfg.terrain_sand_speed;
            p.accel_mult = cfg.terrain_sand_accel;
            p.decel_mult = cfg.terrain_sand_accel;
            p.friction_mult = cfg.terrain_sand_friction;
            p.slide_mult = 0.90f;
            p.label = "Areia";
            break;
        case TerrainPhysicsType::Stone:
            p.speed_mult = cfg.terrain_stone_speed;
            p.accel_mult = cfg.terrain_stone_accel;
            p.decel_mult = cfg.terrain_stone_accel;
            p.friction_mult = cfg.terrain_stone_friction;
            p.slide_mult = 1.0f;
            p.label = "Pedra";
            break;
        case TerrainPhysicsType::Mud:
            p.speed_mult = cfg.terrain_mud_speed;
            p.accel_mult = cfg.terrain_mud_accel;
            p.decel_mult = cfg.terrain_mud_accel;
            p.friction_mult = cfg.terrain_mud_friction;
            p.slide_mult = 0.80f;
            p.label = "Lama";
            break;
        default:
            p.speed_mult = 1.0f;
            p.accel_mult = 1.0f;
            p.decel_mult = 1.0f;
            p.friction_mult = 1.0f;
            p.slide_mult = 1.0f;
            p.label = "Normal";
            break;
    }
    return p;
}

static float sample_heightmap_continuous(const World& world, float x, float z) {
    if (world.w <= 0 || world.h <= 0) return 0.0f;

    x = std::clamp(x, 0.0f, (float)world.w - 1.001f);
    z = std::clamp(z, 0.0f, (float)world.h - 1.001f);

    int x0 = (int)std::floor(x);
    int z0 = (int)std::floor(z);
    int x1 = std::min(world.w - 1, x0 + 1);
    int z1 = std::min(world.h - 1, z0 + 1);
    float tx = x - (float)x0;
    float tz = z - (float)z0;

    float h00 = (float)world.height_at(x0, z0) * kHeightScale;
    float h10 = (float)world.height_at(x1, z0) * kHeightScale;
    float h01 = (float)world.height_at(x0, z1) * kHeightScale;
    float h11 = (float)world.height_at(x1, z1) * kHeightScale;
    float hx0 = lerp(h00, h10, tx);
    float hx1 = lerp(h01, h11, tx);
    return lerp(hx0, hx1, tz);
}

static Vec3 compute_surface_normal(const World& world, float x, float z) {
    int tx = (int)std::floor(x);
    int tz = (int)std::floor(z);
    if (world.in_bounds(tx, tz) && object_block_at(world, tx, tz) != Block::Air) {
        return {0.0f, 1.0f, 0.0f};
    }

    float h_l = sample_heightmap_continuous(world, x - 0.45f, z);
    float h_r = sample_heightmap_continuous(world, x + 0.45f, z);
    float h_d = sample_heightmap_continuous(world, x, z - 0.45f);
    float h_u = sample_heightmap_continuous(world, x, z + 0.45f);
    Vec3 n = vec3_normalize({h_l - h_r, 0.90f, h_d - h_u});
    if (vec3_length(n) < 1e-5f) return {0.0f, 1.0f, 0.0f};
    return n;
}

static float sample_support_height(const World& world, float cx, float cz, float width, float depth, Block* out_surface = nullptr) {
    float hw = width * 0.5f;
    float hd = depth * 0.5f;
    const Vec2 samples[5] = {
        {0.0f, 0.0f},
        {-hw, -hd},
        {hw, -hd},
        {-hw, hd},
        {hw, hd},
    };

    float best_h = -10000.0f;
    Block best_surface = Block::Dirt;
    for (const Vec2& off : samples) {
        int tx = (int)std::floor(cx + off.x);
        int tz = (int)std::floor(cz + off.y);
        if (!world.in_bounds(tx, tz)) continue;
        float h = surface_height_at(world, tx, tz);
        if (h > best_h) {
            best_h = h;
            best_surface = surface_block_at(world, tx, tz);
        }
    }

    if (best_h <= -9999.0f) best_h = 0.0f;
    if (out_surface) *out_surface = best_surface;
    return best_h;
}

static bool column_blocks_movement(const World& world, int tx, int tz, float foot_y, float head_y, float step_allow, float& out_top) {
    if (!world.in_bounds(tx, tz)) {
        out_top = foot_y + step_allow + 10.0f;
        return true;
    }

    float terrain_h = (float)world.height_at(tx, tz) * kHeightScale;
    Block obj = object_block_at(world, tx, tz);
    float top_h = terrain_h;
    if (obj != Block::Air) top_h += get_block_height(obj);
    out_top = top_h;

    if (obj != Block::Air && !is_ground_like(obj)) {
        float block_bottom = terrain_h;
        float block_top = top_h;
        bool intersects_vertical = !(head_y <= block_bottom || foot_y >= block_top);
        if (intersects_vertical && (block_top > foot_y + step_allow + 1e-4f)) {
            return true;
        }
    }

    return top_h > foot_y + step_allow + 1e-4f;
}

static bool overlaps_blocking_volume(const Player& p, const World& world, const PhysicsConfig& cfg,
                                     float test_x, float test_z, float foot_y, float head_y) {
    float left = test_x - p.w * 0.5f + cfg.collision_skin;
    float right = test_x + p.w * 0.5f - cfg.collision_skin;
    float front = test_z - p.h * 0.5f + cfg.collision_skin;
    float back = test_z + p.h * 0.5f - cfg.collision_skin;

    int x0 = (int)std::floor(left);
    int x1 = (int)std::floor(right);
    int z0 = (int)std::floor(front);
    int z1 = (int)std::floor(back);

    for (int tz = z0; tz <= z1; ++tz) {
        for (int tx = x0; tx <= x1; ++tx) {
            float tile_top = 0.0f;
            if (!column_blocks_movement(world, tx, tz, foot_y, head_y, 0.0f, tile_top)) continue;
            float tile_l = (float)tx;
            float tile_r = tile_l + 1.0f;
            float tile_f = (float)tz;
            float tile_b = tile_f + 1.0f;
            if (right > tile_l && left < tile_r && back > tile_f && front < tile_b) return true;
        }
    }
    return false;
}

static bool try_step_climb(Player& p, const World& world, const PhysicsConfig& cfg, const Vec2& move_dir) {
    if (!p.on_ground) return false;
    if (vec2_length(move_dir) < 1e-5f) return false;

    Vec2 dir = vec2_normalize(move_dir);
    Vec2 perp = {-dir.y, dir.x};
    float lateral = p.w * 0.30f;
    float best_front_h = -10000.0f;

    for (int i = -1; i <= 1; ++i) {
        float sx = p.pos.x + dir.x * cfg.step_probe_distance + perp.x * lateral * (float)i;
        float sz = p.pos.y + dir.y * cfg.step_probe_distance + perp.y * lateral * (float)i;
        int tx = (int)std::floor(sx);
        int tz = (int)std::floor(sz);
        if (!world.in_bounds(tx, tz)) return false;
        float h = sample_support_height(world, sx, sz, p.w * 0.90f, p.h * 0.90f);
        best_front_h = std::max(best_front_h, h);
    }

    if (best_front_h <= -9999.0f) return false;
    float rise = best_front_h - p.pos_y;
    if (rise <= cfg.collision_skin) return false;
    if (rise > cfg.step_height + cfg.collision_skin) return false;

    float new_foot = best_front_h + cfg.collision_skin;
    float new_head = new_foot + cfg.collider_height;
    if (overlaps_blocking_volume(p, world, cfg, p.pos.x, p.pos.y, new_foot, new_head)) return false;

    p.pos_y = new_foot;
    p.ground_height = best_front_h;
    p.vel_y = std::max(0.0f, p.vel_y);
    g_physics.stepped = true;
    return true;
}

static void resolve_axis_collision(Player& p, const World& world, const PhysicsConfig& cfg,
                                   float move_amount, bool axis_x, const Vec2& move_dir) {
    if (move_amount == 0.0f) return;

    float skin = cfg.collision_skin;
    float foot_y = p.pos_y + skin;
    float head_y = p.pos_y + cfg.collider_height - skin;
    float step_allow = p.on_ground ? cfg.step_height : 0.05f;

    if (axis_x) {
        float front = p.pos.y - p.h * 0.5f + skin;
        float back = p.pos.y + p.h * 0.5f - skin;
        int z0 = (int)std::floor(front);
        int z1 = (int)std::floor(back);
        if (z1 < z0) z1 = z0;

        if (move_amount > 0.0f) {
            int tx = (int)std::floor(p.pos.x + p.w * 0.5f);
            for (int tz = z0; tz <= z1; ++tz) {
                float tile_top = 0.0f;
                if (!column_blocks_movement(world, tx, tz, foot_y, head_y, step_allow, tile_top)) continue;

                if (try_step_climb(p, world, cfg, move_dir)) {
                    foot_y = p.pos_y + skin;
                    head_y = p.pos_y + cfg.collider_height - skin;
                    float post_step_top = 0.0f;
                    if (!column_blocks_movement(world, tx, tz, foot_y, head_y, step_allow, post_step_top)) continue;
                }

                p.pos.x = (float)tx - p.w * 0.5f - skin;
                p.vel.x = 0.0f;
                g_physics.hit_x = true;
                g_physics.collision_normal = {-1.0f, 0.0f};
                break;
            }
        } else {
            int tx = (int)std::floor(p.pos.x - p.w * 0.5f);
            for (int tz = z0; tz <= z1; ++tz) {
                float tile_top = 0.0f;
                if (!column_blocks_movement(world, tx, tz, foot_y, head_y, step_allow, tile_top)) continue;

                if (try_step_climb(p, world, cfg, move_dir)) {
                    foot_y = p.pos_y + skin;
                    head_y = p.pos_y + cfg.collider_height - skin;
                    float post_step_top = 0.0f;
                    if (!column_blocks_movement(world, tx, tz, foot_y, head_y, step_allow, post_step_top)) continue;
                }

                p.pos.x = (float)(tx + 1) + p.w * 0.5f + skin;
                p.vel.x = 0.0f;
                g_physics.hit_x = true;
                g_physics.collision_normal = {1.0f, 0.0f};
                break;
            }
        }
    } else {
        float left = p.pos.x - p.w * 0.5f + skin;
        float right = p.pos.x + p.w * 0.5f - skin;
        int x0 = (int)std::floor(left);
        int x1 = (int)std::floor(right);
        if (x1 < x0) x1 = x0;

        if (move_amount > 0.0f) {
            int tz = (int)std::floor(p.pos.y + p.h * 0.5f);
            for (int tx = x0; tx <= x1; ++tx) {
                float tile_top = 0.0f;
                if (!column_blocks_movement(world, tx, tz, foot_y, head_y, step_allow, tile_top)) continue;

                if (try_step_climb(p, world, cfg, move_dir)) {
                    foot_y = p.pos_y + skin;
                    head_y = p.pos_y + cfg.collider_height - skin;
                    float post_step_top = 0.0f;
                    if (!column_blocks_movement(world, tx, tz, foot_y, head_y, step_allow, post_step_top)) continue;
                }

                p.pos.y = (float)tz - p.h * 0.5f - skin;
                p.vel.y = 0.0f;
                g_physics.hit_z = true;
                g_physics.collision_normal = {0.0f, -1.0f};
                break;
            }
        } else {
            int tz = (int)std::floor(p.pos.y - p.h * 0.5f);
            for (int tx = x0; tx <= x1; ++tx) {
                float tile_top = 0.0f;
                if (!column_blocks_movement(world, tx, tz, foot_y, head_y, step_allow, tile_top)) continue;

                if (try_step_climb(p, world, cfg, move_dir)) {
                    foot_y = p.pos_y + skin;
                    head_y = p.pos_y + cfg.collider_height - skin;
                    float post_step_top = 0.0f;
                    if (!column_blocks_movement(world, tx, tz, foot_y, head_y, step_allow, post_step_top)) continue;
                }

                p.pos.y = (float)(tz + 1) + p.h * 0.5f + skin;
                p.vel.y = 0.0f;
                g_physics.hit_z = true;
                g_physics.collision_normal = {0.0f, 1.0f};
                break;
            }
        }
    }
}

static void move_player_horizontal(Player& p, const World& world, const PhysicsConfig& cfg, const Vec2& world_delta, const Vec2& move_dir) {
    float max_component = std::max(std::fabs(world_delta.x), std::fabs(world_delta.y));
    int substeps = std::max(1, (int)std::ceil(max_component / std::max(0.05f, cfg.max_move_per_substep)));
    Vec2 step_delta = vec2_scale(world_delta, 1.0f / (float)substeps);

    for (int i = 0; i < substeps; ++i) {
        p.pos.x += step_delta.x;
        resolve_axis_collision(p, world, cfg, step_delta.x, true, move_dir);

        p.pos.y += step_delta.y;
        resolve_axis_collision(p, world, cfg, step_delta.y, false, move_dir);
    }

    p.pos.x = std::clamp(p.pos.x, 1.0f, (float)world.w - 2.0f);
    p.pos.y = std::clamp(p.pos.y, 1.0f, (float)world.h - 2.0f);
}

static GroundProbeResult probe_ground(const Player& p, const World& world, const PhysicsConfig& cfg, bool capture_debug_rays) {
    GroundProbeResult result{};
    result.height = sample_support_height(world, p.pos.x, p.pos.y, p.w * 0.95f, p.h * 0.95f, &result.surface);

    float hw = p.w * 0.45f;
    float hd = p.h * 0.45f;
    const Vec2 offsets[5] = {
        {0.0f, 0.0f},
        {-hw, 0.0f},
        {hw, 0.0f},
        {0.0f, -hd},
        {0.0f, hd},
    };

    float ray_top = p.pos_y + cfg.ground_snap + 0.30f;
    float ray_bottom = p.pos_y - (cfg.step_height + cfg.ground_snap + 0.30f);
    float highest = -10000.0f;
    Block highest_block = Block::Dirt;
    Vec3 normal_accum = {0.0f, 0.0f, 0.0f};
    int hit_count = 0;

    if (capture_debug_rays) g_physics.debug_ray_count = 0;

    for (const Vec2& off : offsets) {
        float sx = p.pos.x + off.x;
        float sz = p.pos.y + off.y;
        int tx = (int)std::floor(sx);
        int tz = (int)std::floor(sz);
        bool in_bounds = world.in_bounds(tx, tz);
        float sample_h = in_bounds ? surface_height_at(world, tx, tz) : -10000.0f;
        bool hit = in_bounds && sample_h <= ray_top + cfg.ground_tolerance && sample_h >= ray_bottom;

        if (capture_debug_rays && g_physics.debug_ray_count < (int)g_physics.debug_rays.size()) {
            PhysicsRayDebug& dbg = g_physics.debug_rays[(size_t)g_physics.debug_ray_count++];
            dbg.from = {sx, ray_top, sz};
            dbg.to = {sx, hit ? sample_h : ray_bottom, sz};
            dbg.hit = hit;
        }

        if (!hit) continue;
        hit_count++;
        if (sample_h > highest) {
            highest = sample_h;
            highest_block = surface_block_at(world, tx, tz);
        }
        normal_accum = vec3_add(normal_accum, compute_surface_normal(world, sx, sz));
    }

    if (hit_count > 0) {
        result.has_hit = true;
        result.height = highest;
        result.surface = highest_block;
        result.normal = vec3_normalize(normal_accum);
        if (vec3_length(result.normal) < 1e-5f) result.normal = {0.0f, 1.0f, 0.0f};
        bool touching = p.pos_y <= result.height + cfg.ground_tolerance;
        bool snappable = (p.vel_y <= 0.0f) && (p.pos_y <= result.height + cfg.ground_snap);
        result.grounded = touching || snappable;
    } else {
        result.has_hit = false;
        result.grounded = false;
        result.normal = {0.0f, 1.0f, 0.0f};
    }

    result.terrain = terrain_type_from_block(result.surface);
    return result;
}

static float slope_speed_multiplier(const Vec3& normal, const Vec2& move_dir, const PhysicsConfig& cfg) {
    if (vec2_length(move_dir) < 1e-5f) return 1.0f;

    Vec2 uphill = vec2_normalize({-normal.x, -normal.z});
    if (vec2_length(uphill) < 1e-5f) return 1.0f;

    float along_uphill = vec2_dot(move_dir, uphill);
    float steepness = clamp01(1.0f - normal.y);
    if (along_uphill > 0.0f) {
        return lerp(1.0f, cfg.slope_uphill_speed_mult, steepness * along_uphill);
    }
    if (along_uphill < 0.0f) {
        return lerp(1.0f, cfg.slope_downhill_speed_mult, steepness * (-along_uphill));
    }
    return 1.0f;
}

static void apply_single_physics_step(const PlayerPhysicsInput& input, float fixed_dt) {
    if (!g_world) return;
    Player& p = g_player;
    const World& world = *g_world;
    const PhysicsConfig& cfg = g_physics_cfg;

    p.w = cfg.collider_width;
    p.h = cfg.collider_depth;

    g_physics.stepped = false;
    g_physics.hit_x = false;
    g_physics.hit_z = false;
    g_physics.sliding = false;
    g_physics.collision_normal = {0.0f, 0.0f};

    GroundProbeResult ground = probe_ground(p, world, cfg, true);
    p.on_ground = ground.grounded;
    p.ground_height = ground.height;
    g_physics.ground_normal = ground.normal;
    g_physics.terrain = ground.terrain;

    TerrainPhysicsProfile terrain = terrain_profile_for(ground.terrain, cfg);
    g_physics.terrain_name = terrain.label;

    if (p.on_ground) g_physics.coyote_timer = cfg.coyote_time;
    else g_physics.coyote_timer = std::max(0.0f, g_physics.coyote_timer - fixed_dt);

    if (input.jump_pressed) g_physics.jump_buffer_timer = cfg.jump_buffer;
    else g_physics.jump_buffer_timer = std::max(0.0f, g_physics.jump_buffer_timer - fixed_dt);

    bool consume_jump = (g_physics.jump_buffer_timer > 0.0f) && (g_physics.coyote_timer > 0.0f);
    if (consume_jump) {
        p.vel_y = cfg.jump_velocity;
        p.on_ground = false;
        g_physics.jump_buffer_timer = 0.0f;
        g_physics.coyote_timer = 0.0f;
    }

    if (input.jump_released && p.vel_y > 0.0f) {
        p.vel_y -= cfg.gravity * (cfg.jump_cancel_multiplier - 1.0f) * fixed_dt;
    }

    bool jetpack_now = !p.on_ground && input.jump_held && p.jetpack_fuel > 0.0f && p.vel_y <= cfg.jump_velocity * 0.60f;
    p.jetpack_active = jetpack_now;
    if (jetpack_now) {
        p.jetpack_fuel = std::max(0.0f, p.jetpack_fuel - cfg.jetpack_fuel_consume * fixed_dt);
        p.vel_y += cfg.jetpack_thrust * fixed_dt;
        p.vel_y = std::min(p.vel_y, cfg.jetpack_max_up_speed);
        p.jetpack_flame_anim += fixed_dt * 15.0f;
    } else if (p.on_ground) {
        p.jetpack_fuel = std::min(100.0f, p.jetpack_fuel + cfg.jetpack_fuel_regen * fixed_dt);
    }

    float gravity_mult = (p.vel_y < 0.0f) ? cfg.fall_multiplier : cfg.rise_multiplier;
    if (jetpack_now) gravity_mult *= cfg.jetpack_gravity_mult;
    p.vel_y -= cfg.gravity * gravity_mult * fixed_dt;
    p.vel_y = std::max(-cfg.terminal_velocity, p.vel_y);

    Vec2 move_dir = input.has_move ? vec2_normalize(input.move) : Vec2{0.0f, 0.0f};
    float slope_mult = slope_speed_multiplier(ground.normal, move_dir, cfg);
    float target_speed = cfg.max_speed * terrain.speed_mult * slope_mult * (input.run ? cfg.run_multiplier : 1.0f);
    Vec2 target_vel = input.has_move ? vec2_scale(move_dir, target_speed) : Vec2{0.0f, 0.0f};

    if (input.has_move) {
        float accel = p.on_ground ? (cfg.ground_acceleration * terrain.accel_mult) : cfg.air_acceleration;
        p.vel.x = approach(p.vel.x, target_vel.x, accel * fixed_dt);
        p.vel.y = approach(p.vel.y, target_vel.y, accel * fixed_dt);
    } else {
        float decel = p.on_ground ? (cfg.ground_deceleration * terrain.decel_mult) : cfg.air_deceleration;
        p.vel.x = approach(p.vel.x, 0.0f, decel * fixed_dt);
        p.vel.y = approach(p.vel.y, 0.0f, decel * fixed_dt);
    }

    float friction = p.on_ground ? (cfg.ground_friction * terrain.friction_mult) : cfg.air_friction;
    float speed = vec2_length(p.vel);
    if (speed > 1e-5f) {
        float damped = std::max(0.0f, speed - friction * fixed_dt);
        p.vel = vec2_scale(p.vel, damped / speed);
    }

    if (p.on_ground && ground.normal.y < cfg.slope_limit_normal_y) {
        Vec2 downhill = vec2_normalize({-ground.normal.x, -ground.normal.z});
        float slope_factor = clamp01((cfg.slope_limit_normal_y - ground.normal.y) / std::max(0.0001f, cfg.slope_limit_normal_y));
        p.vel = vec2_add(p.vel, vec2_scale(downhill, cfg.slope_slide_accel * terrain.slide_mult * slope_factor * fixed_dt));
        g_physics.sliding = slope_factor > 0.02f;
    }

    float max_hspeed = cfg.max_speed * cfg.run_multiplier * 2.0f;
    float hspeed = vec2_length(p.vel);
    if (hspeed > max_hspeed && hspeed > 1e-5f) {
        p.vel = vec2_scale(p.vel, max_hspeed / hspeed);
    }

    Vec2 horizontal_delta = vec2_scale(p.vel, fixed_dt);
    move_player_horizontal(p, world, cfg, horizontal_delta, move_dir);

    p.pos_y += p.vel_y * fixed_dt;

    GroundProbeResult post_ground = probe_ground(p, world, cfg, false);
    if (post_ground.has_hit) {
        bool landing = (p.vel_y <= 0.0f) && (p.pos_y <= post_ground.height + cfg.ground_tolerance);
        bool snap = (p.vel_y <= 0.0f) && (p.pos_y <= post_ground.height + cfg.ground_snap);
        if (landing || snap) {
            p.pos_y = post_ground.height;
            p.vel_y = 0.0f;
            p.on_ground = true;
            p.ground_height = post_ground.height;
            g_physics.coyote_timer = cfg.coyote_time;
        } else {
            p.on_ground = false;
            p.ground_height = post_ground.height;
        }
        g_physics.ground_normal = post_ground.normal;
        g_physics.terrain = post_ground.terrain;
        terrain = terrain_profile_for(post_ground.terrain, cfg);
        g_physics.terrain_name = terrain.label;
    } else {
        p.on_ground = false;
    }

    if (p.pos_y < 0.0f) {
        p.pos_y = 0.0f;
        p.vel_y = 0.0f;
        p.on_ground = true;
    }

    if (input.has_move) {
        p.target_rotation = std::atan2(move_dir.x, move_dir.y) * (180.0f / kPi);
        if (p.target_rotation < 0.0f) p.target_rotation += 360.0f;
    }

    float rot_diff = p.target_rotation - p.rotation;
    while (rot_diff > 180.0f) rot_diff -= 360.0f;
    while (rot_diff < -180.0f) rot_diff += 360.0f;
    p.rotation += rot_diff * std::min(1.0f, cfg.rotation_smoothing * fixed_dt);
    while (p.rotation >= 360.0f) p.rotation -= 360.0f;
    while (p.rotation < 0.0f) p.rotation += 360.0f;

    if (p.rotation >= 315.0f || p.rotation < 45.0f) p.facing_dir = 0;
    else if (p.rotation < 135.0f) p.facing_dir = 1;
    else if (p.rotation < 225.0f) p.facing_dir = 2;
    else p.facing_dir = 3;

    p.can_jump = !input.jump_held;
}

static void reset_player_physics_runtime(bool clear_timers) {
    g_physics.accumulator = 0.0f;
    g_physics.alpha = 0.0f;
    g_physics.prev_pos = g_player.pos;
    g_physics.prev_pos_y = g_player.pos_y;
    g_physics.prev_rotation = g_player.rotation;
    g_physics.render_pos = g_player.pos;
    g_physics.render_pos_y = g_player.pos_y;
    g_physics.render_rotation = g_player.rotation;
    g_physics.ground_normal = {0.0f, 1.0f, 0.0f};
    g_physics.collision_normal = {0.0f, 0.0f};
    g_physics.debug_ray_count = 0;
    g_physics.terrain = TerrainPhysicsType::Normal;
    g_physics.terrain_name = "Normal";
    g_physics.stepped = false;
    g_physics.hit_x = false;
    g_physics.hit_z = false;
    g_physics.sliding = false;
    if (clear_timers) {
        g_physics.jump_buffer_timer = 0.0f;
        g_physics.coyote_timer = 0.0f;
        g_physics.jump_was_held = false;
    }
    g_player.w = g_physics_cfg.collider_width;
    g_player.h = g_physics_cfg.collider_depth;
}

static void step_player_physics(const PlayerPhysicsInput& input, float frame_dt) {
    if (!g_world) return;

    float dt = std::clamp(frame_dt, 0.0001f, 0.1f);
    float fixed_dt = g_physics_cfg.fixed_timestep;
    if (fixed_dt <= 0.0f) fixed_dt = 1.0f / 120.0f;

    g_physics.accumulator += dt;
    float max_acc = fixed_dt * (float)std::max(1, g_physics_cfg.max_substeps);
    if (g_physics.accumulator > max_acc) g_physics.accumulator = max_acc;

    int steps = 0;
    while (g_physics.accumulator >= fixed_dt && steps < g_physics_cfg.max_substeps) {
        g_physics.prev_pos = g_player.pos;
        g_physics.prev_pos_y = g_player.pos_y;
        g_physics.prev_rotation = g_player.rotation;

        apply_single_physics_step(input, fixed_dt);

        g_physics.accumulator -= fixed_dt;
        steps++;
    }

    if (steps == 0) {
        g_physics.prev_pos = g_player.pos;
        g_physics.prev_pos_y = g_player.pos_y;
        g_physics.prev_rotation = g_player.rotation;
    }

    g_physics.alpha = clamp01(g_physics.accumulator / fixed_dt);
    g_physics.render_pos = vec2_lerp(g_physics.prev_pos, g_player.pos, g_physics.alpha);
    g_physics.render_pos_y = lerp(g_physics.prev_pos_y, g_player.pos_y, g_physics.alpha);

    float rot_a = g_physics.prev_rotation;
    float rot_b = g_player.rotation;
    float rot_delta = rot_b - rot_a;
    while (rot_delta > 180.0f) rot_delta -= 360.0f;
    while (rot_delta < -180.0f) rot_delta += 360.0f;
    g_physics.render_rotation = rot_a + rot_delta * g_physics.alpha;
    while (g_physics.render_rotation >= 360.0f) g_physics.render_rotation -= 360.0f;
    while (g_physics.render_rotation < 0.0f) g_physics.render_rotation += 360.0f;
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
    static constexpr float kRestOffset = 0.22f; // Altura do centro do drop acima do solo
    static constexpr float kGravity = 9.5f;
    static constexpr float kPickupRadius = 1.25f;   // estilo Minicraft: coleta "perto", sem precisar pisar exatamente
    static constexpr float kMagnetRadius = 2.75f;   // leve "imã" para o player (facilita coleta em 3D)
    static constexpr float kMagnetSpeed = 7.5f;     // tiles/s
    static constexpr float kAimPickupRadius = 1.65f;   // mirando no drop: coleta um pouco mais "fácil"
    static constexpr float kAimMagnetRadius = 4.25f;   // mirando: imã mais forte (melhora sensação de "vou pegar isso")
    static constexpr float kAimMagnetSpeed = 18.0f;

    const float pickup_r2 = kPickupRadius * kPickupRadius;
    const float magnet_r2 = kMagnetRadius * kMagnetRadius;
    const float aim_pickup_r2 = kAimPickupRadius * kAimPickupRadius;
    const float aim_magnet_r2 = kAimMagnetRadius * kAimMagnetRadius;

    for (size_t di = 0; di < g_drops.size(); ++di) {
        ItemDrop& d = g_drops[di];
        d.t += dt;
        d.pickup_delay -= dt;

        // Leve atracao ao jogador (apenas apos um pequeno delay, para dar feedback visual do drop)
        if (d.pickup_delay <= 0.0f) {
            float dx = g_player.pos.x - d.x;
            float dz = g_player.pos.y - d.z;
            float dist2 = dx * dx + dz * dz;

            bool aimed = ((int)di == g_target_drop);
            float use_magnet_r2 = aimed ? aim_magnet_r2 : magnet_r2;
            float use_magnet_speed = aimed ? kAimMagnetSpeed : kMagnetSpeed;

            if (dist2 <= use_magnet_r2 && dist2 > 1e-6f) {
                float dist = std::sqrt(dist2);
                float step = std::min(use_magnet_speed * dt, dist);
                float inv = 1.0f / dist;
                d.x += dx * inv * step;
                d.z += dz * inv * step;
            }
        }

        // Fisica simples (queda/bounce)
        d.vy -= kGravity * dt;
        d.y += d.vy * dt;

        // Repouso no chao REAL (altura do heightmap), nao em Y constante (montanhas!)
        float rest_y = kRestOffset;
        if (g_world) {
            int tx = (int)std::floor(d.x + 0.5f);
            int tz = (int)std::floor(d.z + 0.5f);
            if (g_world->in_bounds(tx, tz)) {
                rest_y = surface_height_at(*g_world, tx, tz) + kRestOffset;
            }
        }

        if (d.y < rest_y) {
            d.y = rest_y;
            if (std::fabs(d.vy) < 0.8f) d.vy = 0.0f;
            else d.vy = -d.vy * 0.28f;
        }
    }

    // Coleta por proximidade (considera distancia 2D e altura)
    for (size_t i = 0; i < g_drops.size();) {
        ItemDrop& d = g_drops[i];
        if (d.pickup_delay <= 0.0f) {
            float dx = d.x - g_player.pos.x;
            float dz = d.z - g_player.pos.y;
            float dy = d.y - g_player.pos_y;  // Diferenca de altura
            float dist2_horizontal = dx * dx + dz * dz;
            float height_diff = std::fabs(dy);
            
            float use_pickup_r2 = ((int)i == g_target_drop) ? aim_pickup_r2 : pickup_r2;
            
            // Coleta se estiver proximo horizontalmente E verticalmente (dentro de 2 blocos de altura)
            if (dist2_horizontal <= use_pickup_r2 && height_diff < 2.5f) {
                on_pickup_item(d.item, d.x, d.z);
                int removed_idx = (int)i;
                int last_idx = (int)g_drops.size() - 1;
                g_drops[i] = g_drops.back();
                g_drops.pop_back();
                if (g_target_drop == removed_idx) {
                    g_target_drop = -1;
                } else if (g_target_drop == last_idx) {
                    g_target_drop = removed_idx;
                }
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

static void update_shooting_stars(float dt, float day_phase);

static void update_modules(World& world, float dt) {
    g_day_time += dt;

    float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
    float daylight = compute_daylight(day_phase);

    update_shooting_stars(dt, day_phase);
    
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

// ============= SISTEMA DE ILUMINACAO 2D - FUNCOES =============

// Smoothstep para transicoes suaves
static float smoothstep(float edge0, float edge1, float x) {
    float t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

// Atenuacao de luz baseada na distancia
static float light_attenuation(float dist, float radius, float falloff) {
    if (dist >= radius) return 0.0f;
    float t = dist / radius;
    if (falloff <= 1.0f) return 1.0f - t;                    // Linear
    if (falloff <= 2.0f) return 1.0f - t * t;                // Quadratica
    return std::pow(1.0f - t, falloff);                      // Custom
}

// Obter luz para um tipo de modulo
static Light2D get_module_light(const Module& mod) {
    Light2D light = {};
    light.x = (float)mod.x + 0.5f;
    light.y = (float)mod.y + 0.5f;
    light.height = 1.5f;
    light.falloff = 2.0f;
    light.flicker = false;
    light.flicker_speed = 0.0f;
    light.is_emissive = true;
    
    switch (mod.type) {
        case Block::EnergyGenerator:
            light.r = 1.0f; light.g = 0.75f; light.b = 0.15f;
            light.radius = 12.0f;
            light.intensity = 0.95f;
            light.flicker = true;
            light.flicker_speed = 6.0f;
            break;
        case Block::SolarPanel:
            light.r = 0.3f; light.g = 0.5f; light.b = 0.9f;
            light.radius = 5.0f;
            light.intensity = 0.35f;
            break;
        case Block::OxygenGenerator:
            light.r = 0.2f; light.g = 0.95f; light.b = 0.4f;
            light.radius = 7.0f;
            light.intensity = 0.55f;
            light.flicker = true;
            light.flicker_speed = 3.0f;
            break;
        case Block::TerraformerBeacon:
            light.r = 0.85f; light.g = 0.25f; light.b = 0.95f;
            light.radius = 15.0f;
            light.intensity = 0.9f;
            light.flicker = true;
            light.flicker_speed = 2.0f;
            break;
        case Block::Greenhouse:
            light.r = 0.45f; light.g = 0.95f; light.b = 0.35f;
            light.radius = 6.0f;
            light.intensity = 0.45f;
            break;
        case Block::CO2Factory:
            light.r = 0.9f; light.g = 0.5f; light.b = 0.2f;
            light.radius = 8.0f;
            light.intensity = 0.6f;
            light.flicker = true;
            light.flicker_speed = 4.0f;
            break;
        case Block::Habitat:
            light.r = 1.0f; light.g = 0.92f; light.b = 0.7f;
            light.radius = 10.0f;
            light.intensity = 0.75f;
            break;
        case Block::Workshop:
            light.r = 0.9f; light.g = 0.85f; light.b = 0.6f;
            light.radius = 8.0f;
            light.intensity = 0.65f;
            light.flicker = true;
            light.flicker_speed = 8.0f;
            break;
        case Block::WaterExtractor:
            light.r = 0.3f; light.g = 0.7f; light.b = 1.0f;
            light.radius = 5.0f;
            light.intensity = 0.4f;
            break;
        default:
            light.intensity = 0.0f;
            break;
    }
    
    return light;
}

// Calcular luz ambiente baseada no ciclo dia/noite
static float compute_ambient_light() {
    float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
    float daylight = std::fmax(0.0f, std::sin(day_phase * kPi));
    
    // Interpolar entre luz minima (noite) e maxima (dia)
    float ambient = lerp(g_lighting.ambient_min, g_lighting.ambient_max, daylight);
    
    // Terraformacao aumenta luz ambiente levemente
    ambient += clamp01(g_atmosphere / 100.0f) * 0.08f;
    
    return clamp01(ambient);
}

// Obter cor da luz natural baseada no ciclo dia/noite
static void get_natural_light_color(float& r, float& g, float& b) {
    float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
    float daylight = std::fmax(0.0f, std::sin(day_phase * kPi));
    
    if (daylight > 0.7f) {
        // Meio-dia: branco/amarelo quente
        r = 1.0f; g = 0.97f; b = 0.88f;
    } else if (daylight > 0.4f) {
        // Transicao: laranja dourado
        float t = (daylight - 0.4f) / 0.3f;
        r = lerp(1.0f, 1.0f, t);
        g = lerp(0.65f, 0.97f, t);
        b = lerp(0.35f, 0.88f, t);
    } else if (daylight > 0.15f) {
        // Amanhecer/entardecer: laranja/rosa
        float t = (daylight - 0.15f) / 0.25f;
        r = lerp(0.85f, 1.0f, t);
        g = lerp(0.45f, 0.65f, t);
        b = lerp(0.55f, 0.35f, t);
    } else {
        // Noite: azul/roxo frio
        r = 0.35f; g = 0.4f; b = 0.65f;
    }
}

// Coletar todas as fontes de luz no mundo
static void collect_lights() {
    g_lights.clear();
    Vec2 rpos = get_player_render_pos();
    float rpy = get_player_render_y();
    
    // Luz do jogador (lanterna no capacete)
    {
        Light2D player_light;
        player_light.x = rpos.x;
        player_light.y = rpos.y;
        player_light.height = rpy + 1.6f;
        player_light.radius = 10.0f;
        player_light.intensity = 0.7f;
        player_light.r = 1.0f;
        player_light.g = 0.95f;
        player_light.b = 0.85f;
        player_light.falloff = 2.0f;
        player_light.flicker = true;
        player_light.flicker_speed = 12.0f;
        player_light.is_emissive = false;
        g_lights.push_back(player_light);
    }
    
    // Luz do jetpack se ativo
    if (g_player.jetpack_active && g_player.jetpack_fuel > 0.0f) {
        Light2D jet_light;
        jet_light.x = rpos.x;
        jet_light.y = rpos.y;
        jet_light.height = rpy + 0.3f;
        jet_light.radius = 6.0f;
        jet_light.intensity = 0.85f;
        jet_light.r = 1.0f;
        jet_light.g = 0.6f;
        jet_light.b = 0.15f;
        jet_light.falloff = 1.5f;
        jet_light.flicker = true;
        jet_light.flicker_speed = 20.0f;
        jet_light.is_emissive = true;
        g_lights.push_back(jet_light);
    }
    
    // Luzes dos modulos ativos
    for (const auto& mod : g_modules) {
        if (mod.status != ModuleStatus::Active) continue;
        Light2D light = get_module_light(mod);
        if (light.intensity > 0.0f) {
            g_lights.push_back(light);
        }
    }
    
    // Luzes de recursos emissivos (cristais)
    if (g_world) {
        int px = (int)g_player.pos.x;
        int pz = (int)g_player.pos.y;
        int check_radius = 20;
        
        for (int dz = -check_radius; dz <= check_radius; dz += 2) {
            for (int dx = -check_radius; dx <= check_radius; dx += 2) {
                int tx = px + dx;
                int tz = pz + dz;
                if (!g_world->in_bounds(tx, tz)) continue;
                
                Block obj = g_world->get(tx, tz);
                if (obj == Block::Crystal) {
                    Light2D crystal_light;
                    crystal_light.x = (float)tx + 0.5f;
                    crystal_light.y = (float)tz + 0.5f;
                    crystal_light.height = surface_height_at(*g_world, tx, tz) + 0.5f;
                    crystal_light.radius = 4.0f;
                    crystal_light.intensity = 0.5f;
                    crystal_light.r = 0.7f;
                    crystal_light.g = 0.9f;
                    crystal_light.b = 1.0f;
                    crystal_light.falloff = 2.0f;
                    crystal_light.flicker = true;
                    crystal_light.flicker_speed = 5.0f;
                    crystal_light.is_emissive = true;
                    g_lights.push_back(crystal_light);
                }
            }
        }
    }
}

// Calcular sombra por raymarching 2D
static float compute_shadow(float lx, float ly, float px, float py) {
    if (!g_world || !g_lighting.shadows_enabled) return 1.0f;
    
    float dx = px - lx;
    float dy = py - ly;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.5f) return 1.0f;  // Muito perto, sem sombra
    
    int steps = std::min(g_lighting.shadow_samples, (int)(dist * 2.0f));
    if (steps < 2) return 1.0f;
    
    float shadow = 1.0f;
    float inv_steps = 1.0f / (float)steps;
    
    for (int i = 1; i < steps; ++i) {
        float t = (float)i * inv_steps;
        int tx = (int)(lx + dx * t);
        int ty = (int)(ly + dy * t);
        
        if (g_world->in_bounds(tx, ty)) {
            Block obj = g_world->get(tx, ty);
            if (is_solid(obj) && obj != Block::Water) {
                // Sombra parcial - blocos nao bloqueiam totalmente
                shadow *= g_lighting.shadow_softness;
                if (shadow < 0.1f) break;
            }
        }
    }
    
    return shadow;
}

// Converter coordenadas do mundo para indice do lightmap
static int world_to_lightmap_index(float world_x, float world_z) {
    int lx = (int)(world_x - g_lightmap_center_x + kLightmapSize / 2);
    int lz = (int)(world_z - g_lightmap_center_z + kLightmapSize / 2);
    
    if (lx < 0 || lx >= kLightmapSize || lz < 0 || lz >= kLightmapSize) {
        return -1;
    }
    
    return lz * kLightmapSize + lx;
}

// Adicionar contribuicao de uma luz ao lightmap
static void add_light_to_lightmap(const Light2D& light) {
    float light_world_x = light.x;
    float light_world_z = light.y;
    
    // Aplicar flicker
    float flicker_mult = 1.0f;
    if (light.flicker) {
        float flicker = std::sin(g_day_time * light.flicker_speed) * 0.5f + 0.5f;
        flicker_mult = 0.85f + flicker * 0.15f;
    }
    
    float intensity = light.intensity * flicker_mult;
    int radius_int = (int)std::ceil(light.radius);
    
    // Iterar sobre a area de influencia da luz
    for (int dz = -radius_int; dz <= radius_int; ++dz) {
        for (int dx = -radius_int; dx <= radius_int; ++dx) {
            float px = light_world_x + (float)dx;
            float pz = light_world_z + (float)dz;
            
            // Distancia ao centro da luz
            float dist = std::sqrt((float)(dx * dx + dz * dz));
            if (dist > light.radius) continue;
            
            // Atenuacao
            float atten = light_attenuation(dist, light.radius, light.falloff);
            if (atten < 0.01f) continue;
            
            // Sombra
            float shadow = compute_shadow(light_world_x, light_world_z, px, pz);
            
            // Contribuicao final
            float contrib = intensity * atten * shadow;
            
            // Adicionar ao lightmap
            int idx = world_to_lightmap_index(px, pz);
            if (idx >= 0 && idx < kLightmapPixels) {
                g_lightmap_r[idx] += light.r * contrib;
                g_lightmap_g[idx] += light.g * contrib;
                g_lightmap_b[idx] += light.b * contrib;
            }
        }
    }
}

// Aplicar blur gaussiano 3x3 ao lightmap (para suavizar sombras)
static void blur_lightmap_pass(std::vector<float>& src, std::vector<float>& dst) {
    const float k0 = 0.0625f;  // 1/16
    const float k1 = 0.125f;   // 2/16
    const float k2 = 0.25f;    // 4/16
    
    for (int z = 1; z < kLightmapSize - 1; ++z) {
        for (int x = 1; x < kLightmapSize - 1; ++x) {
            int idx = z * kLightmapSize + x;
            
            float sum = 0.0f;
            sum += src[idx - kLightmapSize - 1] * k0;
            sum += src[idx - kLightmapSize] * k1;
            sum += src[idx - kLightmapSize + 1] * k0;
            sum += src[idx - 1] * k1;
            sum += src[idx] * k2;
            sum += src[idx + 1] * k1;
            sum += src[idx + kLightmapSize - 1] * k0;
            sum += src[idx + kLightmapSize] * k1;
            sum += src[idx + kLightmapSize + 1] * k0;
            
            dst[idx] = sum;
        }
    }
}

static void blur_lightmap() {
    // Blur horizontal + vertical (separavel)
    blur_lightmap_pass(g_lightmap_r, g_temp_r);
    blur_lightmap_pass(g_lightmap_g, g_temp_g);
    blur_lightmap_pass(g_lightmap_b, g_temp_b);
    
    // Copiar de volta
    std::copy(g_temp_r.begin(), g_temp_r.end(), g_lightmap_r.begin());
    std::copy(g_temp_g.begin(), g_temp_g.end(), g_lightmap_g.begin());
    std::copy(g_temp_b.begin(), g_temp_b.end(), g_lightmap_b.begin());
}

// Extrair brilho para bloom
static void extract_bloom() {
    float threshold = g_lighting.bloom_threshold;
    
    for (int i = 0; i < kLightmapPixels; ++i) {
        float brightness = (g_lightmap_r[i] + g_lightmap_g[i] + g_lightmap_b[i]) / 3.0f;
        
        if (brightness > threshold) {
            float excess = (brightness - threshold) / (1.0f - threshold + 0.001f);
            excess = std::min(excess, 2.0f);
            
            g_bloom_r[i] = g_lightmap_r[i] * excess;
            g_bloom_g[i] = g_lightmap_g[i] * excess;
            g_bloom_b[i] = g_lightmap_b[i] * excess;
        } else {
            g_bloom_r[i] = 0.0f;
            g_bloom_g[i] = 0.0f;
            g_bloom_b[i] = 0.0f;
        }
    }
}

// Blur maior para bloom (5x5 aproximado com 2 passadas de 3x3)
static void blur_bloom() {
    // Primeira passada
    blur_lightmap_pass(g_bloom_r, g_temp_r);
    blur_lightmap_pass(g_bloom_g, g_temp_g);
    blur_lightmap_pass(g_bloom_b, g_temp_b);
    
    std::copy(g_temp_r.begin(), g_temp_r.end(), g_bloom_r.begin());
    std::copy(g_temp_g.begin(), g_temp_g.end(), g_bloom_g.begin());
    std::copy(g_temp_b.begin(), g_temp_b.end(), g_bloom_b.begin());
    
    // Segunda passada
    blur_lightmap_pass(g_bloom_r, g_temp_r);
    blur_lightmap_pass(g_bloom_g, g_temp_g);
    blur_lightmap_pass(g_bloom_b, g_temp_b);
    
    std::copy(g_temp_r.begin(), g_temp_r.end(), g_bloom_r.begin());
    std::copy(g_temp_g.begin(), g_temp_g.end(), g_bloom_g.begin());
    std::copy(g_temp_b.begin(), g_temp_b.end(), g_bloom_b.begin());
}

// Computar lightmap completo
static void compute_lightmap() {
    if (!g_lighting.enabled) return;
    
    // Atualizar centro do lightmap
    Vec2 rpos = get_player_render_pos();
    g_lightmap_center_x = (int)rpos.x;
    g_lightmap_center_z = (int)rpos.y;
    
    // Obter cor da luz natural
    float nat_r, nat_g, nat_b;
    get_natural_light_color(nat_r, nat_g, nat_b);
    
    // Luz ambiente baseada no ciclo dia/noite
    float ambient = compute_ambient_light();
    
    // Inicializar lightmap com luz ambiente
    for (int i = 0; i < kLightmapPixels; ++i) {
        g_lightmap_r[i] = ambient * nat_r;
        g_lightmap_g[i] = ambient * nat_g;
        g_lightmap_b[i] = ambient * nat_b;
    }
    
    // Coletar luzes
    collect_lights();
    
    // Limitar numero de luzes para performance (prioriza mais proximas ao jogador)
    const int kMaxLights = 32;
    if (g_lights.size() > kMaxLights) {
        // Ordenar por distancia ao jogador
        std::sort(g_lights.begin(), g_lights.end(), [rpos](const Light2D& a, const Light2D& b) {
            float da = (a.x - rpos.x) * (a.x - rpos.x) +
                       (a.y - rpos.y) * (a.y - rpos.y);
            float db = (b.x - rpos.x) * (b.x - rpos.x) +
                       (b.y - rpos.y) * (b.y - rpos.y);
            return da < db;
        });
        g_lights.resize(kMaxLights);
    }
    
    // Adicionar contribuicao de cada luz
    for (const auto& light : g_lights) {
        add_light_to_lightmap(light);
    }
    
    // Blur para suavizar sombras
    if (g_lighting.shadows_enabled) {
        blur_lightmap();
    }
    
    // Extrair e processar bloom
    if (g_lighting.bloom_enabled) {
        extract_bloom();
        blur_bloom();
        
        // Adicionar bloom ao lightmap
        float bloom_int = g_lighting.bloom_intensity;
        for (int i = 0; i < kLightmapPixels; ++i) {
            g_lightmap_r[i] += g_bloom_r[i] * bloom_int;
            g_lightmap_g[i] += g_bloom_g[i] * bloom_int;
            g_lightmap_b[i] += g_bloom_b[i] * bloom_int;
        }
    }
}

// Amostrar iluminacao do lightmap para uma posicao do mundo
static void sample_lightmap(float world_x, float world_z, float& r, float& g, float& b) {
    if (!g_lighting.enabled) {
        r = g = b = 1.0f;
        return;
    }
    
    int idx = world_to_lightmap_index(world_x, world_z);
    
    if (idx >= 0 && idx < kLightmapPixels) {
        r = g_lightmap_r[idx];
        g = g_lightmap_g[idx];
        b = g_lightmap_b[idx];
    } else {
        // Fora do lightmap - usar luz ambiente
        float ambient = compute_ambient_light();
        float nat_r, nat_g, nat_b;
        get_natural_light_color(nat_r, nat_g, nat_b);
        r = ambient * nat_r;
        g = ambient * nat_g;
        b = ambient * nat_b;
    }
    
    // Clamp para evitar valores negativos ou muito altos
    r = std::clamp(r, 0.0f, 2.5f);
    g = std::clamp(g, 0.0f, 2.5f);
    b = std::clamp(b, 0.0f, 2.5f);
}

// Aplicar escurecimento por profundidade (para cavernas/areas baixas)
static float compute_depth_factor(float tile_height, float player_height) {
    if (!g_lighting.enabled) return 1.0f;
    
    float depth_diff = player_height - tile_height;
    if (depth_diff <= 0.0f) return 1.0f;
    
    // Escurecer areas mais baixas que o jogador
    float factor = 1.0f - clamp01(depth_diff / 8.0f) * g_lighting.depth_darkening;
    return std::max(0.2f, factor);
}

// Color grading e pos-processamento
static void apply_color_grading(float& r, float& g, float& b) {
    if (!g_lighting.color_grading) return;
    
    // Contraste
    r = (r - 0.5f) * g_lighting.contrast + 0.5f;
    g = (g - 0.5f) * g_lighting.contrast + 0.5f;
    b = (b - 0.5f) * g_lighting.contrast + 0.5f;
    
    // Exposure
    r *= g_lighting.exposure;
    g *= g_lighting.exposure;
    b *= g_lighting.exposure;
    
    // Saturacao
    float gray = r * 0.299f + g * 0.587f + b * 0.114f;
    r = lerp(gray, r, g_lighting.saturation);
    g = lerp(gray, g, g_lighting.saturation);
    b = lerp(gray, b, g_lighting.saturation);
    
    // Clamp final
    r = clamp01(r);
    g = clamp01(g);
    b = clamp01(b);
}

// Calcular vinheta para uma posicao da tela
static float compute_vignette(float screen_x, float screen_y, float screen_w, float screen_h) {
    if (g_lighting.vignette_intensity <= 0.0f) return 1.0f;
    
    float cx = screen_w * 0.5f;
    float cy = screen_h * 0.5f;
    float max_dist = std::sqrt(cx * cx + cy * cy);
    
    float dx = screen_x - cx;
    float dy = screen_y - cy;
    float dist = std::sqrt(dx * dx + dy * dy) / max_dist;
    
    float vignette = 1.0f - smoothstep(g_lighting.vignette_radius - 0.2f, 1.0f, dist) * g_lighting.vignette_intensity;
    return vignette;
}

// ============= ALIEN SKY SYSTEM (esferas reais + parallax) =============
struct SkyPalette {
    float hz_r = 0.0f, hz_g = 0.0f, hz_b = 0.0f;
    float zn_r = 0.0f, zn_g = 0.0f, zn_b = 0.0f;
};

static float hash01(float v) {
    float h = std::sin(v * 12.9898f + 78.233f) * 43758.5453f;
    return std::fmod(std::fabs(h), 1.0f);
}

static SkyPalette compute_sky_palette(float day_phase, float atmos_factor) {
    float daylight = compute_daylight(day_phase);
    float night = compute_night_alpha(day_phase);
    float sun_warm = smoothstep01(0.05f, 0.45f, daylight) * (1.0f - smoothstep01(0.75f, 1.0f, daylight));
    float atmos = clamp01(atmos_factor);

    SkyPalette p{};
    float night_hz_r = 0.05f, night_hz_g = 0.06f, night_hz_b = 0.11f;
    float night_zn_r = 0.02f, night_zn_g = 0.03f, night_zn_b = 0.07f;
    float day_hz_r = lerp(0.48f, 0.36f, atmos);
    float day_hz_g = lerp(0.37f, 0.52f, atmos);
    float day_hz_b = lerp(0.25f, 0.70f, atmos);
    float day_zn_r = lerp(0.18f, 0.19f, atmos);
    float day_zn_g = lerp(0.23f, 0.38f, atmos);
    float day_zn_b = lerp(0.35f, 0.74f, atmos);

    p.hz_r = lerp(night_hz_r, day_hz_r, daylight);
    p.hz_g = lerp(night_hz_g, day_hz_g, daylight);
    p.hz_b = lerp(night_hz_b, day_hz_b, daylight);
    p.zn_r = lerp(night_zn_r, day_zn_r, daylight);
    p.zn_g = lerp(night_zn_g, day_zn_g, daylight);
    p.zn_b = lerp(night_zn_b, day_zn_b, daylight);

    p.hz_r += sun_warm * g_sky_cfg.atmosphere_horizon_boost * 0.32f;
    p.hz_g += sun_warm * g_sky_cfg.atmosphere_horizon_boost * 0.16f;
    p.hz_b += sun_warm * g_sky_cfg.atmosphere_horizon_boost * 0.07f;

    p.zn_r += daylight * g_sky_cfg.atmosphere_zenith_boost * 0.05f;
    p.zn_g += daylight * g_sky_cfg.atmosphere_zenith_boost * 0.11f;
    p.zn_b += daylight * g_sky_cfg.atmosphere_zenith_boost * 0.18f;

    // Horizon fade at night for more depth.
    float fade = night * g_sky_cfg.horizon_fade;
    p.hz_r = lerp(p.hz_r, p.zn_r, fade * 0.45f);
    p.hz_g = lerp(p.hz_g, p.zn_g, fade * 0.45f);
    p.hz_b = lerp(p.hz_b, p.zn_b, fade * 0.45f);

    p.hz_r = clamp01(p.hz_r); p.hz_g = clamp01(p.hz_g); p.hz_b = clamp01(p.hz_b);
    p.zn_r = clamp01(p.zn_r); p.zn_g = clamp01(p.zn_g); p.zn_b = clamp01(p.zn_b);
    return p;
}

static void render_sky_gradient_dome(float cam_x, float cam_z, const SkyPalette& p) {
    constexpr int kRings = 18;
    constexpr int kSegs = 64;
    constexpr float kRadius = 1850.0f;
    constexpr float kBaseY = -120.0f;

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);

    for (int ring = 0; ring < kRings; ++ring) {
        float t0 = (float)ring / (float)kRings;
        float t1 = (float)(ring + 1) / (float)kRings;
        float e0 = t0 * (kPi * 0.5f);
        float e1 = t1 * (kPi * 0.5f);
        float y0 = kBaseY + std::sin(e0) * kRadius;
        float y1 = kBaseY + std::sin(e1) * kRadius;
        float r0 = std::cos(e0) * kRadius;
        float r1 = std::cos(e1) * kRadius;

        float c0 = smoothstep01(0.0f, 1.0f, t0);
        float c1 = smoothstep01(0.0f, 1.0f, t1);
        float c0r = lerp(p.hz_r, p.zn_r, c0);
        float c0g = lerp(p.hz_g, p.zn_g, c0);
        float c0b = lerp(p.hz_b, p.zn_b, c0);
        float c1r = lerp(p.hz_r, p.zn_r, c1);
        float c1g = lerp(p.hz_g, p.zn_g, c1);
        float c1b = lerp(p.hz_b, p.zn_b, c1);

        glBegin(GL_TRIANGLE_STRIP);
        for (int i = 0; i <= kSegs; ++i) {
            float a = (float)i / (float)kSegs * 2.0f * kPi;
            float ca = std::cos(a);
            float sa = std::sin(a);
            glColor4f(c1r, c1g, c1b, 1.0f);
            glVertex3f(cam_x + ca * r1, y1, cam_z + sa * r1);
            glColor4f(c0r, c0g, c0b, 1.0f);
            glVertex3f(cam_x + ca * r0, y0, cam_z + sa * r0);
        }
        glEnd();
    }
}

static void render_billboard_disc(const Vec3& center, float radius, float r, float g, float b, float a, int segments = 28) {
    Vec3 to_cam = vec3_sub(g_camera.position, center);
    if (vec3_length(to_cam) < 0.001f) to_cam = {0.0f, 0.0f, 1.0f};
    to_cam = vec3_normalize(to_cam);
    Vec3 up = {0.0f, 1.0f, 0.0f};
    Vec3 right = vec3_cross(up, to_cam);
    if (vec3_length(right) < 0.001f) right = {1.0f, 0.0f, 0.0f};
    right = vec3_normalize(right);
    Vec3 disc_up = vec3_normalize(vec3_cross(to_cam, right));

    glBegin(GL_TRIANGLE_FAN);
    glColor4f(r, g, b, a);
    glVertex3f(center.x, center.y, center.z);
    for (int i = 0; i <= segments; ++i) {
        float ang = (float)i / (float)segments * 2.0f * kPi;
        float ca = std::cos(ang);
        float sa = std::sin(ang);
        Vec3 p = vec3_add(center, vec3_add(vec3_scale(right, ca * radius), vec3_scale(disc_up, sa * radius)));
        glColor4f(r, g, b, 0.0f);
        glVertex3f(p.x, p.y, p.z);
    }
    glEnd();
}

static void render_lit_sphere(const Vec3& center, float radius, const Vec3& light_dir, const Vec3& view_pos,
                              float base_r, float base_g, float base_b, float alpha,
                              float ambient, float diffuse_mul, float spec_mul,
                              float noise_freq = 0.0f, float noise_amp = 0.0f,
                              int lat_seg = 18, int lon_seg = 24) {
    Vec3 ldir = vec3_normalize(light_dir);
    for (int lat = 0; lat < lat_seg; ++lat) {
        float v0 = -0.5f + (float)lat / (float)lat_seg;
        float v1 = -0.5f + (float)(lat + 1) / (float)lat_seg;
        float p0 = v0 * kPi;
        float p1 = v1 * kPi;
        float y0 = std::sin(p0);
        float y1 = std::sin(p1);
        float r0 = std::cos(p0);
        float r1 = std::cos(p1);

        glBegin(GL_QUAD_STRIP);
        for (int lon = 0; lon <= lon_seg; ++lon) {
            float u = (float)lon / (float)lon_seg * 2.0f * kPi;
            float cu = std::cos(u);
            float su = std::sin(u);

            auto emit = [&](float rr, float yy) {
                Vec3 n = vec3_normalize({cu * rr, yy, su * rr});
                Vec3 p = vec3_add(center, vec3_scale(n, radius));
                float ndl = std::max(0.0f, vec3_dot(n, ldir));
                Vec3 vdir = vec3_normalize(vec3_sub(view_pos, p));
                Vec3 h = vec3_normalize(vec3_add(ldir, vdir));
                float spec = std::pow(std::max(0.0f, vec3_dot(n, h)), 26.0f) * spec_mul;
                float nvar = 0.0f;
                if (noise_freq > 0.00001f) {
                    nvar = (perlin(p.x * noise_freq + 133.0f, p.z * noise_freq + 617.0f) - 0.5f) * noise_amp;
                }
                float lit = std::max(0.0f, ambient + ndl * diffuse_mul + nvar);
                float cr = clamp01(base_r * lit + spec);
                float cg = clamp01(base_g * lit + spec * 0.95f);
                float cb = clamp01(base_b * lit + spec * 0.90f);
                glColor4f(cr, cg, cb, alpha);
                glVertex3f(p.x, p.y, p.z);
            };

            emit(r1, y1);
            emit(r0, y0);
        }
        glEnd();
    }
}

static void render_star_layer(float cam_x, float cam_z, float day_phase, float night_alpha) {
    if (night_alpha < 0.03f) return;
    int star_count = (int)std::lround(g_sky_cfg.stars_density);
    star_count = std::clamp(star_count, 120, 4000);

    float origin_x = cam_x * g_sky_cfg.stars_parallax;
    float origin_z = cam_z * g_sky_cfg.stars_parallax;

    glPointSize(1.4f);
    glBegin(GL_POINTS);
    for (int i = 0; i < star_count; ++i) {
        float u = hash01((float)i * 1.11f + 13.0f);
        float v = hash01((float)i * 1.71f + 31.0f);
        float w = hash01((float)i * 2.47f + 79.0f);
        float theta = u * 2.0f * kPi;
        float y01 = 0.22f + v * 0.76f;
        float rr = std::sqrt(std::max(0.0f, 1.0f - y01 * y01));
        float dist = 1300.0f + w * 900.0f;
        float sx = origin_x + std::cos(theta) * rr * dist;
        float sy = 190.0f + y01 * 980.0f;
        float sz = origin_z + std::sin(theta) * rr * dist;
        float twinkle = 0.45f + 0.55f * std::sin((float)i * 0.37f + day_phase * 12.0f);
        float a = night_alpha * twinkle * 0.9f;
        float sr = 0.82f + 0.16f * u;
        float sg = 0.82f + 0.16f * v;
        float sb = 0.90f + 0.10f * w;
        glColor4f(sr, sg, sb, a);
        glVertex3f(sx, sy, sz);
    }
    glEnd();
    glPointSize(1.0f);
}

static void render_nebula_layer(float cam_x, float cam_z, float day_phase, float night_alpha) {
    float alpha = night_alpha * g_sky_cfg.nebula_alpha;
    if (alpha < 0.01f) return;
    float origin_x = cam_x * g_sky_cfg.nebula_parallax;
    float origin_z = cam_z * g_sky_cfg.nebula_parallax;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for (int i = 0; i < 5; ++i) {
        float u = hash01((float)i * 9.3f + 21.0f);
        float v = hash01((float)i * 17.7f + 55.0f);
        float ang = day_phase * 0.35f + u * 2.0f * kPi;
        Vec3 c = {
            origin_x + std::cos(ang) * (900.0f + 420.0f * u),
            260.0f + 260.0f * v,
            origin_z + std::sin(ang) * (780.0f + 380.0f * v)
        };
        float rad = 220.0f + 170.0f * u;
        float nr = 0.30f + 0.30f * u;
        float ng = 0.18f + 0.28f * v;
        float nb = 0.42f + 0.32f * (1.0f - u);
        render_billboard_disc(c, rad, nr, ng, nb, alpha * (0.25f + 0.35f * v), 34);
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void render_cloud_layer(float cam_x, float cam_z, float day_phase, float atmos_factor) {
    float alpha = g_sky_cfg.cloud_alpha * (0.35f + atmos_factor * 0.65f);
    if (alpha < 0.01f) return;
    float origin_x = cam_x * g_sky_cfg.cloud_parallax;
    float origin_z = cam_z * g_sky_cfg.cloud_parallax;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (int i = 0; i < 6; ++i) {
        float t = (float)i * 1.71f;
        float u = hash01(t + 17.0f);
        float v = hash01(t + 63.0f);
        float spin = day_phase * 1.8f + u * 2.0f * kPi;
        Vec3 c = {
            origin_x + std::cos(spin) * (460.0f + 380.0f * u),
            320.0f + 160.0f * v,
            origin_z + std::sin(spin) * (420.0f + 320.0f * v)
        };
        float rad = 130.0f + 110.0f * u;
        render_billboard_disc(c, rad, 0.88f, 0.90f, 0.94f, alpha * (0.35f + 0.30f * v), 30);
    }
}

static void update_shooting_stars(float dt, float day_phase) {
    for (auto& s : g_shooting_stars) {
        s.life -= dt;
        s.offset = vec3_add(s.offset, vec3_scale(s.vel, dt));
    }
    g_shooting_stars.erase(
        std::remove_if(g_shooting_stars.begin(), g_shooting_stars.end(),
            [](const ShootingStar& s) { return s.life <= 0.0f; }),
        g_shooting_stars.end());

    float night_alpha = compute_night_alpha(day_phase);
    if (night_alpha < 0.55f) return;
    if (g_shooting_stars.size() >= 4) return;

    float spawn_rate = 0.05f + 0.12f * (night_alpha - 0.55f);
    if (rng_next_f01() > dt * spawn_rate) return;

    ShootingStar s;
    s.max_life = 0.75f + rng_next_f01() * 0.55f;
    s.life = s.max_life;
    s.length = 120.0f + rng_next_f01() * 180.0f;
    float start_radius = 1100.0f + rng_next_f01() * 450.0f;
    float start_ang = rng_next_f01() * 2.0f * kPi;
    s.offset.x = std::cos(start_ang) * start_radius;
    s.offset.z = std::sin(start_ang) * start_radius;
    s.offset.y = 420.0f + rng_next_f01() * 520.0f;
    float dir_ang = start_ang + (0.90f + rng_next_f01() * 0.60f) * (((rng_next_u32() & 1u) != 0u) ? 1.0f : -1.0f);
    float spd = 650.0f + rng_next_f01() * 450.0f;
    s.vel.x = std::cos(dir_ang) * spd;
    s.vel.z = std::sin(dir_ang) * spd;
    s.vel.y = -(120.0f + rng_next_f01() * 260.0f);
    float tint = 0.86f + rng_next_f01() * 0.14f;
    s.r = tint;
    s.g = tint;
    s.b = 0.95f + rng_next_f01() * 0.05f;
    g_shooting_stars.push_back(s);
}

static void render_shooting_stars(float cam_x, float cam_y, float cam_z, float night_alpha) {
    if (night_alpha < 0.20f) return;
    if (g_shooting_stars.empty()) return;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    for (const auto& s : g_shooting_stars) {
        float progress = 1.0f - (s.life / std::max(0.001f, s.max_life));
        float fade_in = smoothstep01(0.00f, 0.12f, progress);
        float fade_out = 1.0f - smoothstep01(0.70f, 1.00f, progress);
        float a = night_alpha * fade_in * fade_out;
        if (a <= 0.01f) continue;
        Vec3 head = {cam_x + s.offset.x, s.offset.y, cam_z + s.offset.z};
        Vec3 dir = vec3_normalize(s.vel);
        Vec3 tail = vec3_sub(head, vec3_scale(dir, s.length));
        glColor4f(s.r, s.g, s.b, a);
        glVertex3f(tail.x, tail.y, tail.z);
        glVertex3f(head.x, head.y, head.z);
    }
    glEnd();
    glLineWidth(1.0f);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// Funcao principal para renderizar todo o ceu alienigena
static void render_alien_sky(float cam_x, float cam_y, float cam_z, float day_phase, float atmos_factor) {
    float night_alpha = compute_night_alpha(day_phase);
    SkyPalette palette = compute_sky_palette(day_phase, atmos_factor);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);

    render_sky_gradient_dome(cam_x, cam_z, palette);
    render_star_layer(cam_x, cam_z, day_phase, night_alpha);
    render_nebula_layer(cam_x, cam_z, day_phase, night_alpha);

    Vec3 camera_ref = {cam_x, cam_y, cam_z};
    float sun_angle = day_phase * 2.0f * kPi - kPi * 0.5f;
    Vec3 sun_pos = {
        cam_x + std::cos(sun_angle) * g_sky_cfg.sun_distance,
        85.0f + std::sin(sun_angle) * 315.0f,
        cam_z - 200.0f + std::sin(sun_angle * 0.5f) * 100.0f
    };
    Vec3 sun_dir = vec3_normalize(vec3_sub(sun_pos, camera_ref));

    float planet_phase = g_day_time / (kDayLength * std::max(0.1f, g_sky_cfg.planet_orbit_speed * 12.0f));
    float planet_ang = planet_phase * 2.0f * kPi + 0.75f;
    Vec3 planet_pos = {
        cam_x * g_sky_cfg.planet_parallax + std::cos(planet_ang) * g_sky_cfg.planet_distance,
        140.0f + std::sin(planet_ang * 0.65f) * 190.0f,
        cam_z * g_sky_cfg.planet_parallax + std::sin(planet_ang) * g_sky_cfg.planet_distance
    };
    render_lit_sphere(planet_pos, g_sky_cfg.planet_radius, sun_dir, g_camera.position,
                      0.20f, 0.28f, 0.42f, 0.98f,
                      0.22f, 0.90f, 0.18f,
                      0.010f, 0.22f, 20, 28);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    render_billboard_disc(planet_pos, g_sky_cfg.planet_radius * 1.45f, 0.46f, 0.60f, 0.90f, night_alpha * 0.16f, 34);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    Vec3 moon1_pos = {
        cam_x * g_sky_cfg.moon_parallax + std::cos(day_phase * g_sky_cfg.moon_orbit_speed * 2.0f * kPi + 1.1f) * g_sky_cfg.moon_distance,
        220.0f + std::sin(day_phase * g_sky_cfg.moon_orbit_speed * 2.0f * kPi + 1.1f) * 150.0f,
        cam_z * g_sky_cfg.moon_parallax + std::sin(day_phase * g_sky_cfg.moon_orbit_speed * 2.0f * kPi + 1.1f) * (g_sky_cfg.moon_distance * 0.58f)
    };
    Vec3 moon2_pos = {
        cam_x * g_sky_cfg.moon2_parallax + std::cos(day_phase * g_sky_cfg.moon2_orbit_speed * 2.0f * kPi + 2.7f) * g_sky_cfg.moon2_distance,
        250.0f + std::sin(day_phase * g_sky_cfg.moon2_orbit_speed * 2.0f * kPi + 2.7f) * 120.0f,
        cam_z * g_sky_cfg.moon2_parallax + std::sin(day_phase * g_sky_cfg.moon2_orbit_speed * 2.0f * kPi + 2.7f) * (g_sky_cfg.moon2_distance * 0.75f)
    };

    float moon_alpha = 0.35f + night_alpha * 0.65f;
    render_lit_sphere(moon1_pos, g_sky_cfg.moon_radius, sun_dir, g_camera.position,
                      0.64f, 0.58f, 0.54f, moon_alpha,
                      0.12f, 0.95f, 0.10f,
                      0.030f, 0.30f, 16, 22);
    render_lit_sphere(moon2_pos, g_sky_cfg.moon2_radius, sun_dir, g_camera.position,
                      0.58f, 0.68f, 0.82f, moon_alpha * 0.92f,
                      0.12f, 0.95f, 0.14f,
                      0.045f, 0.24f, 14, 20);

    float eclipse_cycle = 0.5f + 0.5f * std::sin((g_day_time / (kDayLength * g_sky_cfg.eclipse_frequency_days)) * 2.0f * kPi);
    float sun_align = vec3_dot(vec3_normalize(vec3_sub(moon1_pos, camera_ref)), sun_dir);
    float eclipse = smoothstep01(0.996f, 0.9998f, sun_align) * smoothstep01(0.78f, 1.0f, eclipse_cycle) * g_sky_cfg.eclipse_strength;

    if (sun_pos.y > 40.0f) {
        float sun_alpha = 1.0f - eclipse;
        render_lit_sphere(sun_pos, g_sky_cfg.sun_radius, sun_dir, g_camera.position,
                          1.0f, 0.84f, 0.50f, sun_alpha,
                          0.95f, 0.55f, 0.05f,
                          0.0f, 0.0f, 18, 24);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        float halo_mul = g_sky_cfg.sun_halo_size;
        render_billboard_disc(sun_pos, g_sky_cfg.sun_radius * halo_mul, 1.0f, 0.70f, 0.35f, (0.12f + 0.20f * g_sky_cfg.bloom_intensity) * sun_alpha, 34);
        render_billboard_disc(sun_pos, g_sky_cfg.sun_radius * (halo_mul * 1.8f), 1.0f, 0.52f, 0.22f, (0.05f + 0.10f * g_sky_cfg.bloom_intensity) * sun_alpha, 34);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    render_cloud_layer(cam_x, cam_z, day_phase, atmos_factor);
    render_shooting_stars(cam_x, cam_y, cam_z, night_alpha);

    glEnable(GL_DEPTH_TEST);
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

static void render_physics_debug_3d() {
    if (!g_debug) return;

    Vec2 rp = get_player_render_pos();
    float ry = get_player_render_y();
    float hw = g_player.w * 0.5f;
    float hd = g_player.h * 0.5f;
    float foot = ry + g_physics_cfg.collision_skin;
    float head = foot + g_physics_cfg.collider_height;

    glDisable(GL_TEXTURE_2D);
    glLineWidth(1.8f);

    // Collider AABB.
    glColor4f(0.10f, 0.95f, 1.0f, 0.95f);
    glBegin(GL_LINE_LOOP);
    glVertex3f(rp.x - hw, foot, rp.y - hd);
    glVertex3f(rp.x + hw, foot, rp.y - hd);
    glVertex3f(rp.x + hw, foot, rp.y + hd);
    glVertex3f(rp.x - hw, foot, rp.y + hd);
    glEnd();

    glBegin(GL_LINE_LOOP);
    glVertex3f(rp.x - hw, head, rp.y - hd);
    glVertex3f(rp.x + hw, head, rp.y - hd);
    glVertex3f(rp.x + hw, head, rp.y + hd);
    glVertex3f(rp.x - hw, head, rp.y + hd);
    glEnd();

    glBegin(GL_LINES);
    glVertex3f(rp.x - hw, foot, rp.y - hd); glVertex3f(rp.x - hw, head, rp.y - hd);
    glVertex3f(rp.x + hw, foot, rp.y - hd); glVertex3f(rp.x + hw, head, rp.y - hd);
    glVertex3f(rp.x + hw, foot, rp.y + hd); glVertex3f(rp.x + hw, head, rp.y + hd);
    glVertex3f(rp.x - hw, foot, rp.y + hd); glVertex3f(rp.x - hw, head, rp.y + hd);
    glEnd();

    // Ground rays.
    for (int i = 0; i < g_physics.debug_ray_count; ++i) {
        const PhysicsRayDebug& ray = g_physics.debug_rays[(size_t)i];
        if (ray.hit) glColor4f(0.20f, 1.0f, 0.30f, 0.90f);
        else glColor4f(1.0f, 0.20f, 0.20f, 0.90f);
        glBegin(GL_LINES);
        glVertex3f(ray.from.x, ray.from.y, ray.from.z);
        glVertex3f(ray.to.x, ray.to.y, ray.to.z);
        glEnd();
    }

    // Ground normal.
    Vec3 n0 = {rp.x, g_player.ground_height + 0.03f, rp.y};
    Vec3 n1 = {n0.x + g_physics.ground_normal.x * 1.1f,
               n0.y + g_physics.ground_normal.y * 1.1f,
               n0.z + g_physics.ground_normal.z * 1.1f};
    glColor4f(0.30f, 0.70f, 1.0f, 1.0f);
    glBegin(GL_LINES);
    glVertex3f(n0.x, n0.y, n0.z);
    glVertex3f(n1.x, n1.y, n1.z);
    glEnd();

    // Velocity vector.
    Vec3 v0 = {rp.x, ry + 0.90f, rp.y};
    Vec3 v1 = {
        v0.x + g_player.vel.x * 0.20f,
        v0.y + g_player.vel_y * 0.10f,
        v0.z + g_player.vel.y * 0.20f
    };
    glColor4f(1.0f, 0.85f, 0.25f, 1.0f);
    glBegin(GL_LINES);
    glVertex3f(v0.x, v0.y, v0.z);
    glVertex3f(v1.x, v1.y, v1.z);
    glEnd();

    // Collision normal.
    if (g_physics.hit_x || g_physics.hit_z) {
        Vec3 c0 = {rp.x, foot + 0.15f, rp.y};
        Vec3 c1 = {c0.x + g_physics.collision_normal.x * 0.7f, c0.y, c0.z + g_physics.collision_normal.y * 0.7f};
        glColor4f(1.0f, 0.2f, 1.0f, 1.0f);
        glBegin(GL_LINES);
        glVertex3f(c0.x, c0.y, c0.z);
        glVertex3f(c1.x, c1.y, c1.z);
        glEnd();
    }

    glLineWidth(1.0f);
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
    
    // Atualizar camera (target + colisao) para o frame atual
    update_camera_for_frame();
    
    // Aplicar view matrix
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    apply_look_at();

    float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
    float atmos_factor = clamp01(g_atmosphere / 100.0f);
    SkyPalette sky_palette = compute_sky_palette(day_phase, atmos_factor);
    float sky_r = lerp(sky_palette.hz_r, sky_palette.zn_r, 0.35f);
    float sky_g = lerp(sky_palette.hz_g, sky_palette.zn_g, 0.35f);
    float sky_b = lerp(sky_palette.hz_b, sky_palette.zn_b, 0.35f);
    
    // Clear com cor do ceu alienigena
    glClearColor(sky_r, sky_g, sky_b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // === RENDERIZAR ELEMENTOS DO CEU (sol, luas, estrelas, anel) ===
    render_alien_sky(g_camera.position.x, g_camera.position.y, g_camera.position.z, day_phase, atmos_factor);

    // === COMPUTAR LIGHTMAP 2D (RTX FAKE) ===
    compute_lightmap();

    // Calcular area visivel baseada na posicao do jogador (culling)
    Vec2 rpos = get_player_render_pos();
    float rpy = get_player_render_y();
    int player_tile_x = (int)std::floor(rpos.x);
    int player_tile_z = (int)std::floor(rpos.y);  // Y do 2D = Z no 3D
    int view_radius = (int)std::clamp(g_camera.distance * 3.8f + 55.0f, 110.0f, 200.0f);
    int wall_radius = std::clamp(view_radius - 45, 80, view_radius);
    int obj_radius = std::clamp(view_radius - 30, 90, view_radius);
    int view_radius2 = view_radius * view_radius;
    int wall_radius2 = wall_radius * wall_radius;
    int obj_radius2 = obj_radius * obj_radius;

    // Fog de distancia por bioma para profundidade e esconder limite do mapa.
    {
        Block fog_surface = Block::Dirt;
        if (g_world->in_bounds(player_tile_x, player_tile_z)) {
            fog_surface = surface_block_at(*g_world, player_tile_x, player_tile_z);
        }

        float fog_mul_r = 1.0f, fog_mul_g = 1.0f, fog_mul_b = 1.0f;
        float fog_start_mul = 1.0f, fog_end_mul = 1.0f;
        switch (fog_surface) {
            case Block::Ice:
            case Block::Snow:
                fog_mul_r = 0.95f; fog_mul_g = 1.02f; fog_mul_b = 1.12f;
                fog_start_mul = 0.86f; fog_end_mul = 0.86f;
                break;
            case Block::Sand:
                fog_mul_r = 1.08f; fog_mul_g = 1.00f; fog_mul_b = 0.86f;
                fog_start_mul = 0.92f; fog_end_mul = 0.93f;
                break;
            case Block::Stone:
            case Block::Coal:
            case Block::Iron:
                fog_mul_r = 0.88f; fog_mul_g = 0.92f; fog_mul_b = 0.98f;
                fog_start_mul = 0.84f; fog_end_mul = 0.88f;
                break;
            case Block::Water:
                fog_mul_r = 0.82f; fog_mul_g = 0.95f; fog_mul_b = 1.08f;
                fog_start_mul = 0.80f; fog_end_mul = 0.84f;
                break;
            default:
                break;
        }

        float fog_col[4] = {
            clamp01(sky_r * fog_mul_r),
            clamp01(sky_g * fog_mul_g),
            clamp01(sky_b * fog_mul_b),
            1.0f
        };
        glEnable(GL_FOG);
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogfv(GL_FOG_COLOR, fog_col);
        glHint(GL_FOG_HINT, GL_NICEST);

        float fog_start = std::max(70.0f, (float)view_radius * g_sky_cfg.fog_start_factor * fog_start_mul);
        float fog_end = std::max(fog_start + 110.0f,
                                 (float)view_radius * g_sky_cfg.fog_end_factor * fog_end_mul + g_sky_cfg.fog_distance_bonus);
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

                    // Edge blending entre terrenos adjacentes (transicao visual suave).
                    float neigh_r = 0.0f, neigh_g = 0.0f, neigh_b = 0.0f;
                    int neigh_count = 0;
                    int diff_count = 0;
                    const int nx[4] = {1, -1, 0, 0};
                    const int nz[4] = {0, 0, 1, -1};
                    for (int ni = 0; ni < 4; ++ni) {
                        int sx = tx + nx[ni];
                        int sz = tz + nz[ni];
                        if (!g_world->in_bounds(sx, sz)) continue;
                        Block sb = surface_block_at(*g_world, sx, sz);
                        BlockTex sbtex = block_tex(sb);
                        float sr = 1.0f, sg = 1.0f, sbb = 1.0f;
                        if (sbtex.uses_tint || sbtex.transparent) {
                            float cr, cg, cb, ca;
                            block_color(sb, sz, g_world->h, cr, cg, cb, ca);
                            if (sbtex.uses_tint) { sr = cr; sg = cg; sbb = cb; }
                        }
                        neigh_r += sr;
                        neigh_g += sg;
                        neigh_b += sbb;
                        neigh_count++;
                        if (sb != surface) diff_count++;
                    }
                    if (neigh_count > 0 && diff_count > 0) {
                        float inv = 1.0f / (float)neigh_count;
                        neigh_r *= inv;
                        neigh_g *= inv;
                        neigh_b *= inv;
                        float edge_blend = ((float)diff_count / 4.0f) * 0.34f;
                        tint_r = lerp(tint_r, neigh_r, edge_blend);
                        tint_g = lerp(tint_g, neigh_g, edge_blend);
                        tint_b = lerp(tint_b, neigh_b, edge_blend);
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
                    
                    // === ILUMINACAO 2D (RTX FAKE) ===
                    if (g_lighting.enabled) {
                        float light_r, light_g, light_b;
                        sample_lightmap((float)tx, (float)tz, light_r, light_g, light_b);
                        
                        // Escurecimento por profundidade
                        float depth_factor = compute_depth_factor(base_y, rpy);
                        light_r *= depth_factor;
                        light_g *= depth_factor;
                        light_b *= depth_factor;
                        
                        // Aplicar iluminacao
                        tint_r *= light_r;
                        tint_g *= light_g;
                        tint_b *= light_b;
                        
                        // Color grading
                        apply_color_grading(tint_r, tint_g, tint_b);
                    }

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
                    
                    // === ILUMINACAO 2D PARA OBJETOS (RTX FAKE) ===
                    if (g_lighting.enabled) {
                        float light_r, light_g, light_b;
                        sample_lightmap((float)tx, (float)tz, light_r, light_g, light_b);
                        
                        // Objetos emissivos (modulos, cristais) recebem boost de luz
                        bool is_emissive = is_module(obj) || obj == Block::Crystal;
                        if (is_emissive) {
                            light_r = std::max(light_r, 0.7f);
                            light_g = std::max(light_g, 0.7f);
                            light_b = std::max(light_b, 0.7f);
                        }
                        
                        // Escurecimento por profundidade
                        float depth_factor = compute_depth_factor(base_y, rpy);
                        light_r *= depth_factor;
                        light_g *= depth_factor;
                        light_b *= depth_factor;
                        
                        tint_r *= light_r;
                        tint_g *= light_g;
                        tint_b *= light_b;
                        
                        apply_color_grading(tint_r, tint_g, tint_b);
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
        for (size_t di = 0; di < g_drops.size(); ++di) {
            const auto& d = g_drops[di];
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
            
            // Iluminacao 2D para drops
            if (g_lighting.enabled) {
                float light_r, light_g, light_b;
                sample_lightmap(d.x, d.z, light_r, light_g, light_b);
                tint_r *= light_r;
                tint_g *= light_g;
                tint_b *= light_b;
                apply_color_grading(tint_r, tint_g, tint_b);
            }

            bool aimed = ((int)di == g_target_drop);
            float bob = 0.03f * std::sin(d.t * 4.0f);
            float size = aimed ? 0.42f : 0.34f;
            float aa = aimed ? 1.0f : a;
            render_cube_3d_tex(d.x, d.y + bob, d.z, size, tex.top, tex.side, tex.bottom, tint_r, tint_g, tint_b, aa, true);
        }
    }

    if (use_textures) {
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }
    
    // === RENDERIZAR PLAYER 3D (Estilo Minicraft - Blocky) ===
    {
        float px = rpos.x;
        // OFFSET PARA ELEVAR O JOGADOR ACIMA DO SOLO (evita pes afundados)
        float player_y_offset = 0.15f;
        float py = rpy + player_y_offset;  // Altura real + offset
        float pz = rpos.y;  // Y do 2D = Z no 3D
        
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
        float rot_rad = get_player_render_rotation() * (kPi / 180.0f);
        float sin_rot = std::sin(rot_rad);
        float cos_rot = std::cos(rot_rad);
        
        // Animacao de movimento (bob up/down)
        float bob = g_player.is_moving ? std::sin(g_player.walk_timer * 14.0f) * 0.04f : 0.0f;
        float leg_swing = g_player.is_moving ? std::sin(g_player.walk_timer * 10.0f) * 0.12f : 0.0f;
        
        // === CHAMA DO JETPACK (renderizar primeiro, atras do jogador) ===
        if (g_player.jetpack_active && g_player.jetpack_fuel > 0.0f) {
            float pack_dist = 0.25f;
            float flame_x = px - sin_rot * pack_dist;
            float flame_z = pz - cos_rot * pack_dist;
            
            // Animacao da chama (flicker)
            float flame_flicker = 0.8f + 0.4f * std::sin(g_player.jetpack_flame_anim * 2.0f);
            float flame_size = 0.15f + 0.05f * std::sin(g_player.jetpack_flame_anim * 3.0f);
            
            // Chama principal (laranja/amarela)
            for (int i = 0; i < 3; ++i) {
                float flame_y = py + 0.10f - i * 0.15f;
                float size = flame_size * (1.0f - i * 0.25f);
                float intensity = flame_flicker * (1.0f - i * 0.2f);
                
                // Nucleo amarelo
                render_cube_3d(flame_x, flame_y, flame_z, size * 0.6f, 
                    1.0f * intensity, 0.95f * intensity, 0.3f * intensity, 0.95f, false);
                // Chama laranja
                render_cube_3d(flame_x, flame_y - 0.08f, flame_z, size * 0.8f, 
                    1.0f * intensity, 0.55f * intensity, 0.1f * intensity, 0.85f, false);
                // Borda vermelha
                render_cube_3d(flame_x, flame_y - 0.15f, flame_z, size, 
                    0.95f * intensity, 0.25f * intensity, 0.05f * intensity, 0.7f, false);
            }
            
            // Particulas de fogo (pequenos cubos caindo)
            for (int i = 0; i < 4; ++i) {
                float particle_offset = std::sin(g_player.jetpack_flame_anim * 5.0f + i * 1.5f) * 0.08f;
                float particle_y = py - 0.1f - std::fmod(g_player.jetpack_flame_anim * 0.5f + i * 0.25f, 0.5f);
                float alpha = 0.8f - std::fmod(g_player.jetpack_flame_anim * 0.5f + i * 0.25f, 0.5f) * 1.5f;
                if (alpha > 0.0f) {
                    render_cube_3d(flame_x + particle_offset, particle_y, flame_z + particle_offset * 0.5f, 
                        0.06f, 1.0f, 0.6f, 0.1f, alpha, false);
                }
            }
        }
        
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
        // Mochila brilha quando jetpack ativo
        float pack_r = 0.45f, pack_g = 0.47f, pack_b = 0.50f;
        if (g_player.jetpack_active) {
            pack_r = 0.55f; pack_g = 0.50f; pack_b = 0.45f;
        }
        render_cube_3d(pack_x, py + 0.35f + bob, pack_z, 0.30f, pack_r, pack_g, pack_b, 1.0f, true);
        
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

    if (g_debug) {
        render_physics_debug_3d();
    }

    // === SELECAO DO ALVO / PLACE (Estilo Minicraft) ===
    // Desenha um contorno no bloco/tile sob a mira para deixar claro o que sera minerado/coletado/colocado.
    auto draw_tile_outline = [&](int tx, int tz, float y, float size, float r, float g, float b, float a, float lw) {
        float half = size * 0.5f;
        glLineWidth(lw);
        glColor4f(r, g, b, a);
        glBegin(GL_LINE_LOOP);
        glVertex3f((float)tx - half, y, (float)tz - half);
        glVertex3f((float)tx + half, y, (float)tz - half);
        glVertex3f((float)tx + half, y, (float)tz + half);
        glVertex3f((float)tx - half, y, (float)tz + half);
        glEnd();
    };

    if (g_has_target && g_world->in_bounds(g_target_x, g_target_y)) {
        Block tb = g_world->get(g_target_x, g_target_y);
        float base_y = (float)g_world->height_at(g_target_x, g_target_y) * kHeightScale;

        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Contorno preto + contorno branco por cima (boa leitura em qualquer tile)
        if (tb == Block::Air || tb == Block::Leaves || tb == Block::Water || is_ground_like(tb)) {
            float y = base_y + 0.018f;
            if (tb == Block::Leaves) y = base_y + 0.60f + 0.004f;
            else if (tb == Block::Water) y = base_y - 0.18f + 0.004f;
            draw_tile_outline(g_target_x, g_target_y, y, 1.03f, 0.0f, 0.0f, 0.0f, 0.85f, 2.5f);
            draw_tile_outline(g_target_x, g_target_y, y, 1.03f, 1.0f, 1.0f, 1.0f, 0.80f, 1.5f);
        } else {
            float cy = base_y + 0.5f;
            render_cube_outline_3d((float)g_target_x, cy, (float)g_target_y, 1.04f, 2.5f);

            // Outline branco leve
            glLineWidth(1.5f);
            glColor4f(1.0f, 1.0f, 1.0f, 0.55f);
            float half = 1.04f * 0.5f;
            glBegin(GL_LINE_LOOP);
            glVertex3f((float)g_target_x - half, cy + half, (float)g_target_y - half);
            glVertex3f((float)g_target_x + half, cy + half, (float)g_target_y - half);
            glVertex3f((float)g_target_x + half, cy + half, (float)g_target_y + half);
            glVertex3f((float)g_target_x - half, cy + half, (float)g_target_y + half);
            glEnd();
        }
    }

    if (g_has_place_target && g_world->in_bounds(g_place_x, g_place_y)) {
        // Mostra um contorno azul para o tile onde o RMB vai colocar
        Block pb = g_world->get(g_place_x, g_place_y);
        float base_y = (float)g_world->height_at(g_place_x, g_place_y) * kHeightScale;
        float y = base_y + 0.020f;
        if (pb == Block::Leaves) y = base_y + 0.60f + 0.004f;
        else if (pb == Block::Water) y = base_y - 0.18f + 0.004f;
        draw_tile_outline(g_place_x, g_place_y, y, 1.05f, 0.05f, 0.65f, 1.0f, 0.65f, 2.0f);
    }

    // === EFEITO DE MINERACAO (cracks) - SEM WIREFRAME ===
    if (g_has_target) {
        float target_x = (float)g_target_x;
        float target_z = (float)g_target_y;
        Block tb = g_world->get(g_target_x, g_target_y);
        float base_y = (float)g_world->height_at(g_target_x, g_target_y) * kHeightScale;

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
    }
    
    // === MUDAR PARA PROJECAO 2D PARA HUD ===
    glDisable(GL_FOG);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, win_w, win_h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    // === VINHETA (RTX FAKE - efeito cinematico) ===
    if (g_lighting.enabled && g_lighting.vignette_intensity > 0.0f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        // Desenhar vinheta como gradiente radial usando quads
        float cx = win_w * 0.5f;
        float cy = win_h * 0.5f;
        float max_dist = std::sqrt(cx * cx + cy * cy);
        float vignette_start = g_lighting.vignette_radius * max_dist;
        
        // Criar overlay de vinheta com gradiente
        int segments = 32;
        for (int ring = 0; ring < 8; ++ring) {
            float inner_r = vignette_start + ring * (max_dist - vignette_start) / 8.0f;
            float outer_r = vignette_start + (ring + 1) * (max_dist - vignette_start) / 8.0f;
            float inner_alpha = (float)ring / 8.0f * g_lighting.vignette_intensity;
            float outer_alpha = (float)(ring + 1) / 8.0f * g_lighting.vignette_intensity;
            
            glBegin(GL_QUAD_STRIP);
            for (int i = 0; i <= segments; ++i) {
                float angle = (float)i / segments * 2.0f * kPi;
                float cos_a = std::cos(angle);
                float sin_a = std::sin(angle);
                
                glColor4f(0.0f, 0.0f, 0.0f, outer_alpha);
                glVertex2f(cx + outer_r * cos_a, cy + outer_r * sin_a);
                glColor4f(0.0f, 0.0f, 0.0f, inner_alpha);
                glVertex2f(cx + inner_r * cos_a, cy + inner_r * sin_a);
            }
            glEnd();
        }
    }
    
    // === DEBUG: VISUALIZAR LIGHTMAP ===
    if (g_debug_lightmap && g_lighting.enabled) {
        float debug_size = 150.0f;
        float debug_x = win_w - debug_size - 10.0f;
        float debug_y = 10.0f;
        float cell_size = debug_size / kLightmapSize;
        
        // Fundo
        glColor4f(0.0f, 0.0f, 0.0f, 0.8f);
        glBegin(GL_QUADS);
        glVertex2f(debug_x - 5, debug_y - 5);
        glVertex2f(debug_x + debug_size + 5, debug_y - 5);
        glVertex2f(debug_x + debug_size + 5, debug_y + debug_size + 5);
        glVertex2f(debug_x - 5, debug_y + debug_size + 5);
        glEnd();
        
        // Lightmap pixels
        for (int z = 0; z < kLightmapSize; ++z) {
            for (int x = 0; x < kLightmapSize; ++x) {
                int idx = z * kLightmapSize + x;
                float r = std::min(1.0f, g_lightmap_r[idx]);
                float g = std::min(1.0f, g_lightmap_g[idx]);
                float b = std::min(1.0f, g_lightmap_b[idx]);
                
                glColor3f(r, g, b);
                float px = debug_x + x * cell_size;
                float py = debug_y + z * cell_size;
                glBegin(GL_QUADS);
                glVertex2f(px, py);
                glVertex2f(px + cell_size, py);
                glVertex2f(px + cell_size, py + cell_size);
                glVertex2f(px, py + cell_size);
                glEnd();
            }
        }
        
        // Label
        draw_text(debug_x, debug_y + debug_size + 10.0f, "LIGHTMAP DEBUG", 0.9f, 0.9f, 0.3f, 1.0f);
    }
    
    // === DEBUG: VISUALIZAR LUZES ===
    if (g_debug_lights && g_lighting.enabled) {
        float debug_y = g_debug_lightmap ? 180.0f : 10.0f;
        char buf[128];
        snprintf(buf, sizeof(buf), "Luzes ativas: %d", (int)g_lights.size());
        draw_text(win_w - 200.0f, debug_y, buf, 0.9f, 0.9f, 0.3f, 1.0f);
        
        float y_offset = debug_y + 20.0f;
        for (size_t i = 0; i < std::min(g_lights.size(), (size_t)8); ++i) {
            const auto& light = g_lights[i];
            snprintf(buf, sizeof(buf), "L%d: (%.1f,%.1f) r=%.1f i=%.2f", 
                (int)i, light.x, light.y, light.radius, light.intensity);
            draw_text(win_w - 200.0f, y_offset, buf, light.r, light.g, light.b, 1.0f);
            y_offset += 15.0f;
        }
    }
    
    // === CROSSHAIR SEGUINDO O MOUSE (Estilo Minicraft) ===
    {
        float cx = (float)g_mouse_x;
        float cy = (float)g_mouse_y;
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
        float left_panel_h = bar_gap * 10 + 100.0f;  // Altura aproximada do painel esquerdo (incluindo jetpack)
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
        
        // Jetpack Fuel (amarelo-laranja)
        float jet_pct = g_player.jetpack_fuel / 100.0f;
        bool jet_active = g_player.jetpack_active;
        float jet_r = jet_active ? 1.0f : 0.85f;
        float jet_g = jet_active ? 0.65f : 0.55f;
        float jet_b = 0.15f;
        float jet_pulse = jet_active ? (0.8f + 0.2f * std::sin(g_player.jetpack_flame_anim)) : 1.0f;
        render_bar(x0, y0 + bar_gap * 4, bar_w, bar_h, jet_pct, 
            jet_r * jet_pulse, jet_g * jet_pulse, jet_b);
        std::string jet_label = jet_active ? "JETPACK ATIVO" : "Jetpack " + std::to_string((int)g_player.jetpack_fuel) + "%";
        draw_text(x0 + 6.0f, y0 + bar_gap * 4 + 11.0f, jet_label, 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.90f);
        
        // === LEFT PANEL: BASE STATUS ===
        y0 += bar_gap * 5 + 15.0f;
        
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
        
        // Funcao auxiliar para verificar se mouse esta sobre um slot
        auto mouse_over_slot = [&](float sx, float sy, float ss) -> bool {
            return g_mouse_x >= sx && g_mouse_x <= sx + ss && 
                   g_mouse_y >= sy && g_mouse_y <= sy + ss;
        };
        
        // Desenhar slots de recursos
        for (int i = 0; i < res_count; ++i) {
            float bx = hx + i * (slot_size + slot_gap);
            
            // Detectar clique do mouse no slot
            if (g_mouse_left_clicked && mouse_over_slot(bx, hy, slot_size) && g_state == GameState::Playing) {
                g_selected = resource_slots[i];
                bounce_hotbar_slot(i);
                g_mouse_left_clicked = false;
            }
            
            bool sel = (g_selected == resource_slots[i]);
            // Highlight se mouse esta sobre o slot
            bool hovered = mouse_over_slot(bx, hy, slot_size);
            int count = std::max(0, g_inventory[(int)resource_slots[i]]);
            
            // Desenhar com efeito de hover
            if (hovered && !sel) {
                render_quad(bx - 2.0f, hy - 2.0f, slot_size + 4.0f, slot_size + 4.0f, 0.55f, 0.65f, 0.85f, 0.35f);
            }
            draw_minicraft_slot(bx, hy, slot_size, sel, resource_slots[i], i + 1, count);
        }
        
        // Separador visual entre recursos e modulos
        if (!module_slots.empty()) {
            float sep_x = hx + res_count * (slot_size + slot_gap) - slot_gap * 0.5f;
            render_quad(sep_x - 1.0f, hy + 4.0f, 2.0f, slot_size - 8.0f, 0.40f, 0.40f, 0.45f, 0.80f);
        }
        
        // Desenhar slots de modulos
        for (int i = 0; i < (int)module_slots.size(); ++i) {
            float bx = hx + (res_count + i) * (slot_size + slot_gap);
            
            // Detectar clique do mouse no slot de modulo
            if (g_mouse_left_clicked && mouse_over_slot(bx, hy, slot_size) && g_state == GameState::Playing) {
                g_selected = module_slots[i];
                bounce_hotbar_slot(res_count + i);
                g_mouse_left_clicked = false;
            }
            
            bool sel = (g_selected == module_slots[i]);
            bool hovered = mouse_over_slot(bx, hy, slot_size);
            CraftCost c = module_cost(module_slots[i]);
            bool can_build = can_afford(c);
            int key_num = -1;
            if (i < 4) key_num = (i < 3) ? (7 + i) : 0;
            
            // Desenhar com efeito de hover
            if (hovered && !sel) {
                render_quad(bx - 2.0f, hy - 2.0f, slot_size + 4.0f, slot_size + 4.0f, 0.55f, 0.65f, 0.85f, 0.35f);
            }
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
            char buf[256];
            snprintf(buf, sizeof(buf), "XZ: %.1f,%.1f  Y: %.2f  Chao: %.1f  %s  Mat: %s  VelXY: %.2f",
                g_player.pos.x, g_player.pos.y, g_player.pos_y, g_player.ground_height,
                g_player.on_ground ? "NO CHAO" : "NO AR",
                g_physics.terrain_name.c_str(),
                vec2_length(g_player.vel));
            draw_text(20.0f, win_h - 136.0f, buf, 0.85f, 0.85f, 0.90f, 0.95f);
             
            snprintf(buf, sizeof(buf), "VelY: %.2f  Normal:(%.2f, %.2f, %.2f)  Coy:%.2f Buf:%.2f  %s%s%s",
                g_player.vel_y,
                g_physics.ground_normal.x, g_physics.ground_normal.y, g_physics.ground_normal.z,
                g_physics.coyote_timer, g_physics.jump_buffer_timer,
                g_physics.sliding ? "SLIDE " : "",
                g_physics.stepped ? "STEP " : "",
                (g_physics.hit_x || g_physics.hit_z) ? "HIT" : "");
            draw_text(20.0f, win_h - 118.0f, buf, 0.85f, 0.85f, 0.90f, 0.95f);

            snprintf(buf, sizeof(buf), "Cam: yaw=%.0f pitch=%.0f dist=%.1f  Phys: dt=%.4f alpha=%.2f",
                g_camera.yaw, g_camera.pitch, g_camera.distance, g_physics_cfg.fixed_timestep, g_physics.alpha);
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

    // Overlays - Menus estilo Minecraft
    if (g_state == GameState::Paused || g_state == GameState::Menu) {
        // Fundo escurecido
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.0f, 0.0f, 0.0f, g_state == GameState::Paused ? 0.55f : 0.70f);
        
        // Funcao para desenhar botao estilo Minecraft
        auto draw_mc_button = [&](float x, float y, float w, float h, const std::string& text, bool hovered, bool enabled = true) {
            // Cores base do botao Minecraft
            float bg_r = enabled ? 0.45f : 0.30f;
            float bg_g = enabled ? 0.45f : 0.30f;
            float bg_b = enabled ? 0.50f : 0.35f;
            
            if (hovered && enabled) {
                bg_r = 0.55f; bg_g = 0.65f; bg_b = 0.85f;  // Highlight azul
            }
            
            // Sombra (borda inferior/direita escura)
            render_quad(x + 3.0f, y + 3.0f, w, h, 0.05f, 0.05f, 0.08f, 0.95f);
            
            // Corpo do botao
            render_quad(x, y, w, h, bg_r * 0.7f, bg_g * 0.7f, bg_b * 0.7f, 0.98f);
            
            // Borda superior clara (3D effect)
            render_quad(x, y, w, 3.0f, bg_r * 1.3f, bg_g * 1.3f, bg_b * 1.3f, 0.95f);
            render_quad(x, y, 3.0f, h, bg_r * 1.3f, bg_g * 1.3f, bg_b * 1.3f, 0.95f);
            
            // Borda inferior escura
            render_quad(x, y + h - 3.0f, w, 3.0f, bg_r * 0.4f, bg_g * 0.4f, bg_b * 0.4f, 0.95f);
            render_quad(x + w - 3.0f, y, 3.0f, h, bg_r * 0.4f, bg_g * 0.4f, bg_b * 0.4f, 0.95f);
            
            // Interior do botao (gradiente sutil)
            render_quad(x + 3.0f, y + 3.0f, w - 6.0f, h - 6.0f, bg_r, bg_g, bg_b, 0.98f);
            
            // Texto centralizado
            float text_w = estimate_text_w_px(text);
            float text_r = enabled ? 1.0f : 0.55f;
            float text_g = enabled ? 1.0f : 0.55f;
            float text_b = enabled ? 1.0f : 0.55f;
            if (hovered && enabled) {
                text_r = 1.0f; text_g = 1.0f; text_b = 0.65f;  // Texto amarelado quando hover
            }
            draw_text(x + (w - text_w) * 0.5f, y + h * 0.5f + 5.0f, text, text_r, text_g, text_b, 1.0f);
        };
        
        // Verifica se mouse esta sobre um retangulo
        auto mouse_in_rect = [&](float x, float y, float w, float h) -> bool {
            return g_mouse_x >= x && g_mouse_x <= x + w && g_mouse_y >= y && g_mouse_y <= y + h;
        };
        
        if (g_state == GameState::Paused) {
            // === MENU DE PAUSA ESTILO MINECRAFT ===
            float btn_w = 280.0f;
            float btn_h = 40.0f;
            float btn_gap = 8.0f;
            float start_y = win_h * 0.22f;
            float center_x = win_w * 0.5f - btn_w * 0.5f;
            
            // Titulo "Jogo Pausado"
            std::string title = "Jogo Pausado";
            float title_w = estimate_text_w_px(title);
            draw_text(win_w * 0.5f - title_w * 0.5f, start_y, title, 1.0f, 1.0f, 1.0f, 1.0f);
            
            start_y += 50.0f;
            
            // Botoes do menu de pausa
            struct PauseButton { std::string text; int id; };
            PauseButton buttons[] = {
                {"Continuar", 0},
                {"Salvar Jogo", 1},
                {"Carregar Jogo", 2},
                {"Configuracoes", 3},
                {"Novo Jogo", 4}
            };
            
            g_pause_selection = -1;  // Reset selection
            
            for (int i = 0; i < 5; ++i) {
                float by = start_y + i * (btn_h + btn_gap);
                bool hovered = mouse_in_rect(center_x, by, btn_w, btn_h);
                if (hovered) g_pause_selection = buttons[i].id;
                draw_mc_button(center_x, by, btn_w, btn_h, buttons[i].text, hovered);
            }
            
            // Separador
            start_y += 5 * (btn_h + btn_gap) + 15.0f;
            render_quad(center_x, start_y, btn_w, 2.0f, 0.5f, 0.5f, 0.55f, 0.6f);
            
            // Controles (texto menor)
            start_y += 15.0f;
            draw_text(center_x, start_y, "CONTROLES:", 0.75f, 0.80f, 0.90f, 0.90f);
            start_y += 22.0f;
            draw_text(center_x, start_y, "WASD - Mover", 0.65f, 0.65f, 0.70f, 0.85f);
            start_y += 18.0f;
            draw_text(center_x, start_y, "Espaco - Pular  |  Shift - Correr", 0.65f, 0.65f, 0.70f, 0.85f);
            start_y += 18.0f;
            draw_text(center_x, start_y, "Botao Direito - Rotacionar Camera", 0.65f, 0.65f, 0.70f, 0.85f);
            start_y += 18.0f;
            draw_text(center_x, start_y, "Scroll - Zoom  |  1-9 - Selecionar Item", 0.65f, 0.65f, 0.70f, 0.85f);
            start_y += 18.0f;
            draw_text(center_x, start_y, "Botao Esquerdo - Minerar/Construir", 0.65f, 0.65f, 0.70f, 0.85f);
            
        } else if (g_state == GameState::Menu) {
            // === MENU PRINCIPAL ESTILO MINECRAFT ===
            float btn_w = 320.0f;
            float btn_h = 45.0f;
            float btn_gap = 10.0f;
            
            // Logo/Titulo grande
            std::string title = "TERRAFORMER";
            float title_w = estimate_text_w_px(title);
            // Sombra do titulo
            draw_text(win_w * 0.5f - title_w * 0.5f + 3.0f, win_h * 0.18f + 3.0f, title, 0.15f, 0.15f, 0.15f, 0.9f);
            // Titulo principal
            draw_text(win_w * 0.5f - title_w * 0.5f, win_h * 0.18f, title, 0.95f, 0.85f, 0.25f, 1.0f);
            
            // Subtitulo
            std::string subtitle = "Colonize. Construa. Terraforma.";
            float sub_w = estimate_text_w_px(subtitle);
            draw_text(win_w * 0.5f - sub_w * 0.5f, win_h * 0.18f + 35.0f, subtitle, 0.70f, 0.75f, 0.80f, 0.90f);
            
            float start_y = win_h * 0.38f;
            float center_x = win_w * 0.5f - btn_w * 0.5f;
            
            // Botoes do menu principal
            struct MenuButton { std::string text; int id; };
            MenuButton buttons[] = {
                {"Novo Jogo", 0},
                {"Carregar Jogo", 1},
                {"Sair", 2}
            };
            
            g_menu_selection = -1;  // Reset selection
            
            for (int i = 0; i < 3; ++i) {
                float by = start_y + i * (btn_h + btn_gap);
                bool hovered = mouse_in_rect(center_x, by, btn_w, btn_h);
                if (hovered) g_menu_selection = buttons[i].id;
                draw_mc_button(center_x, by, btn_w, btn_h, buttons[i].text, hovered);
            }
            
            // Versao no canto
            draw_text(10.0f, win_h - 20.0f, "TerraFormer v1.0", 0.5f, 0.5f, 0.55f, 0.7f);
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
        
        float menu_w = 480.0f;
        float menu_h = 520.0f;  // Aumentado para opcoes de iluminacao
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
        
        // === OPCOES DE ILUMINACAO (RTX FAKE) ===
        draw_text(label_x, row_y + 5.0f, "--- Iluminacao RTX ---", 0.9f, 0.75f, 0.3f, 0.9f);
        row_y += row_h * 0.7f;
        
        // Opcao: Iluminacao Ativada
        bool sel4 = (g_settings_selection == 4);
        if (sel4) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Iluminacao 2D", sel4 ? 1.0f : 0.8f, sel4 ? 1.0f : 0.8f, sel4 ? 1.0f : 0.8f, 1.0f);
        const char* light_str = g_lighting.enabled ? "Ativada" : "Desativada";
        draw_text(value_x, row_y + 5.0f, light_str, g_lighting.enabled ? 0.3f : 0.8f, g_lighting.enabled ? 0.9f : 0.4f, 0.3f, 1.0f);
        row_y += row_h;
        
        // Opcao: Sombras
        bool sel5 = (g_settings_selection == 5);
        if (sel5) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Sombras 2D", sel5 ? 1.0f : 0.8f, sel5 ? 1.0f : 0.8f, sel5 ? 1.0f : 0.8f, 1.0f);
        const char* shadow_str = g_lighting.shadows_enabled ? "Ativadas" : "Desativadas";
        draw_text(value_x, row_y + 5.0f, shadow_str, g_lighting.shadows_enabled ? 0.3f : 0.8f, g_lighting.shadows_enabled ? 0.9f : 0.4f, 0.3f, 1.0f);
        row_y += row_h;
        
        // Opcao: Bloom
        bool sel6 = (g_settings_selection == 6);
        if (sel6) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Bloom/Glow", sel6 ? 1.0f : 0.8f, sel6 ? 1.0f : 0.8f, sel6 ? 1.0f : 0.8f, 1.0f);
        char bloom_buf[32];
        snprintf(bloom_buf, sizeof(bloom_buf), "< %.0f%% >", g_lighting.bloom_intensity * 100.0f);
        draw_text(value_x, row_y + 5.0f, bloom_buf, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;
        
        // Opcao: Vinheta
        bool sel7 = (g_settings_selection == 7);
        if (sel7) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Vinheta", sel7 ? 1.0f : 0.8f, sel7 ? 1.0f : 0.8f, sel7 ? 1.0f : 0.8f, 1.0f);
        char vignette_buf[32];
        snprintf(vignette_buf, sizeof(vignette_buf), "< %.0f%% >", g_lighting.vignette_intensity * 100.0f);
        draw_text(value_x, row_y + 5.0f, vignette_buf, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;
        
        // Opcao: Voltar
        bool sel8 = (g_settings_selection == 8);
        if (sel8) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Voltar", sel8 ? 1.0f : 0.8f, sel8 ? 1.0f : 0.8f, sel8 ? 1.0f : 0.8f, 1.0f);
        
        // Instrucoes
        draw_text(menu_x + 30.0f, menu_y + menu_h - 40.0f, "W/S: Navegar | A/D: Ajustar | Esc/Enter: Voltar | F3: Debug Lightmap", 0.6f, 0.65f, 0.70f, 0.9f);
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

    // Resetar clique do mouse no final do frame
    g_mouse_left_clicked = false;

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
    bool f6_pressed = key_pressed(VK_F6, g_prev_f6);
    bool f7_pressed = key_pressed(VK_F7, g_prev_f7);
    bool h_pressed = key_pressed('H', g_prev_h);
    bool tab_pressed = key_pressed(VK_TAB, g_prev_tab);
    bool b_pressed = key_pressed('B', g_prev_b);

    // F3 alterna entre modos de debug: normal -> lightmap -> lights -> off
    if (f3_pressed) {
        if (!g_debug && !g_debug_lightmap && !g_debug_lights) {
            g_debug = true;  // Primeiro: debug basico
        } else if (g_debug && !g_debug_lightmap) {
            g_debug = false;
            g_debug_lightmap = true;  // Segundo: lightmap
        } else if (g_debug_lightmap && !g_debug_lights) {
            g_debug_lightmap = false;
            g_debug_lights = true;  // Terceiro: luzes
        } else {
            g_debug = false;
            g_debug_lightmap = false;
            g_debug_lights = false;  // Desliga tudo
        }
    }

    // State machine
    if (g_state == GameState::Menu) {
        // Clique do mouse nos botoes do menu principal
        if (g_mouse_left_clicked && g_menu_selection >= 0) {
            g_mouse_left_clicked = false;
            switch (g_menu_selection) {
                case 0:  // Novo Jogo
                    delete g_world;
                    g_world = new World(WORLD_WIDTH, WORLD_HEIGHT, (unsigned)GetTickCount());
                    spawn_player_new_game(*g_world);
                    g_cam_pos = g_player.pos;
                    g_day_time = kDayLength * 0.25f;
                    g_modules.clear();
                    g_particles.clear();
                    g_shooting_stars.clear();
                    g_construction_queue.clear();
                    g_alerts.clear();
                    g_build_slots.clear();
                    g_collect_popups.clear();
                    g_drops.clear();
                    g_onboarding = OnboardingState();
                    g_state = GameState::Playing;
                    show_tip("WASD para mover, Espaco para pular, Botao direito para girar camera", g_onboarding.shown_first_move);
                    return;
                case 1:  // Carregar Jogo
                    if (load_game(kSavePath)) {
                        set_toast("Jogo carregado!");
                        g_state = GameState::Playing;
                    } else {
                        set_toast("Nenhum save encontrado.");
                    }
                    return;
                case 2:  // Sair
                    g_quit = true;
                    return;
            }
        }
        
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
            g_shooting_stars.clear();
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
        // Clique do mouse nos botoes do menu de pausa
        if (g_mouse_left_clicked && g_pause_selection >= 0) {
            g_mouse_left_clicked = false;
            switch (g_pause_selection) {
                case 0:  // Continuar
                    g_state = GameState::Playing;
                    return;
                case 1:  // Salvar Jogo
                    if (save_game(kSavePath)) set_toast("Jogo salvo!");
                    else set_toast("Falha ao salvar!");
                    return;
                case 2:  // Carregar Jogo
                    if (load_game(kSavePath)) {
                        set_toast("Jogo carregado!");
                        g_state = GameState::Playing;
                    } else {
                        set_toast("Falha ao carregar!");
                    }
                    return;
                case 3:  // Configuracoes
                    g_state = GameState::Settings;
                    g_settings_selection = 0;
                    return;
                case 4:  // Novo Jogo
                    g_state = GameState::Menu;
                    return;
            }
        }
        
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
            g_settings_selection = (g_settings_selection - 1 + 9) % 9;
        }
        key_w_held = w_now;
        
        // Navegar para baixo
        if (s_now && !key_s_held) {
            g_settings_selection = (g_settings_selection + 1) % 9;
        }
        key_s_held = s_now;
        
        // F3 para debug lightmap
        static bool f3_held = false;
        bool f3_now = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
        if (f3_now && !f3_held) {
            g_debug_lightmap = !g_debug_lightmap;
        }
        f3_held = f3_now;
        
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
                case 4: // Iluminacao 2D
                    g_lighting.enabled = !g_lighting.enabled;
                    break;
                case 5: // Sombras
                    g_lighting.shadows_enabled = !g_lighting.shadows_enabled;
                    break;
                case 6: // Bloom
                    g_lighting.bloom_intensity = std::clamp(g_lighting.bloom_intensity + delta * 0.1f, 0.0f, 1.0f);
                    g_lighting.bloom_enabled = (g_lighting.bloom_intensity > 0.0f);
                    break;
                case 7: // Vinheta
                    g_lighting.vignette_intensity = std::clamp(g_lighting.vignette_intensity + delta * 0.1f, 0.0f, 0.6f);
                    break;
                case 8: // Voltar
                    break;
            }
        }
        
        // ESC ou Enter no "Voltar" fecha o menu
        if (esc_pressed || (enter_pressed && g_settings_selection == 8)) {
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

    if (f7_pressed) {
        reload_physics_config(true);
        reload_terrain_config(true);
        reload_sky_config(true);
        reset_player_physics_runtime(false);
        set_toast(std::string("Configs recarregadas: ") + g_physics_config_path + " | " + g_terrain_config_path + " | " + g_sky_config_path, 3.0f);
    }

    if (f6_pressed) {
        build_physics_test_map(*g_world);
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

    // ============= MOVIMENTO 3D (TIMESTEP FIXO) =============
    float cam_yaw_rad = g_camera.yaw * (kPi / 180.0f);
    float cam_forward_x = -std::sin(cam_yaw_rad);
    float cam_forward_z = -std::cos(cam_yaw_rad);
    float cam_right_x = std::cos(cam_yaw_rad);
    float cam_right_z = -std::sin(cam_yaw_rad);

    float input_forward = 0.0f;
    float input_right = 0.0f;
    if (key_down('W') || key_down(VK_UP)) input_forward += 1.0f;
    if (key_down('S') || key_down(VK_DOWN)) input_forward -= 1.0f;
    if (key_down('A') || key_down(VK_LEFT)) input_right -= 1.0f;
    if (key_down('D') || key_down(VK_RIGHT)) input_right += 1.0f;

    Vec2 move_world = {
        input_forward * cam_forward_x + input_right * cam_right_x,
        input_forward * cam_forward_z + input_right * cam_right_z
    };
    bool has_input = (move_world.x != 0.0f || move_world.y != 0.0f);
    if (has_input) move_world = vec2_normalize(move_world);

    bool run_key = key_down(VK_SHIFT);
    bool jump_held = key_down(VK_SPACE);
    bool jump_pressed = jump_held && !g_physics.jump_was_held;
    bool jump_released = !jump_held && g_physics.jump_was_held;
    g_physics.jump_was_held = jump_held;

    PlayerPhysicsInput physics_input{};
    physics_input.move = move_world;
    physics_input.has_move = has_input;
    physics_input.run = run_key;
    physics_input.jump_pressed = jump_pressed;
    physics_input.jump_held = jump_held;
    physics_input.jump_released = jump_released;
    step_player_physics(physics_input, dt);

    if (key_down(VK_ADD) || key_down(VK_OEM_PLUS)) {
        g_camera.distance = std::max(g_camera.min_distance, g_camera.distance - 10.0f * dt);
    }
    if (key_down(VK_SUBTRACT) || key_down(VK_OEM_MINUS)) {
        g_camera.distance = std::min(g_camera.max_distance, g_camera.distance + 10.0f * dt);
    }

    g_player.anim_frame += dt;
    g_player.is_moving = vec2_length(g_player.vel) > 0.15f;
    if (g_player.is_moving) g_player.walk_timer += dt * (run_key ? 1.5f : 1.0f);
    else g_player.walk_timer *= 0.9f;
    
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

    // Camera follow sincronizado com interpolacao da fisica
    float cam_speed = 6.0f;
    Vec2 render_pos = get_player_render_pos();
    g_cam_pos.x = approach(g_cam_pos.x, render_pos.x, cam_speed * dt * std::fabs(render_pos.x - g_cam_pos.x) + 0.5f * dt);
    g_cam_pos.y = approach(g_cam_pos.y, render_pos.y, cam_speed * dt * std::fabs(render_pos.y - g_cam_pos.y) + 0.5f * dt);

    // Mouse targeting
    POINT cursor;
    GetCursorPos(&cursor);
    ScreenToClient(hwnd, &cursor);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int win_w = rc.right - rc.left;
    int win_h = rc.bottom - rc.top;

    // Atualizar camera antes do targeting, para a mira (+) do centro bater com o raycast.
    update_camera_for_frame();

    // ============= TARGETING 3D (Estilo Minicraft) =============
    // A mira (+) segue o mouse; fazemos raycast a partir da camera na direcao do mouse.
    const float kReach = 4.2f; // alcance de interacao (minerar/colocar)

    g_has_target = false;
    g_target_in_range = false;
    g_has_place_target = false;
    g_place_in_range = false;
    g_target_drop = -1;

    auto placeable_tile = [&](Block b) -> bool {
        if (is_base_structure(b)) return false;
        if (is_module(b)) return false;
        if (b == Block::Air || b == Block::Water) return true;
        return !is_solid(b); // walkable pode ser substituido
    };

    auto blocks_raycast = [&](Block b) -> bool {
        if (b == Block::Air) return false;
        if (b == Block::Water) return true;
        if (b == Block::Leaves) return true;
        if (is_base_structure(b)) return true;
        if (is_module(b)) return true;
        return is_solid(b);
    };

    // Ray da camera (mira) - agora baseado na posicao do mouse
    Vec3 ray_o = g_camera.position;
    Vec3 ray_d = get_mouse_ray_direction(g_mouse_x, g_mouse_y, win_w, win_h);
    float ray_max = std::clamp(g_camera.effective_distance + kReach + 3.0f, 8.0f, 55.0f);

    // Primeiro: tentar mirar um drop (para facilitar coleta visual)
    {
        float best_t = std::numeric_limits<float>::infinity();
        float best_perp2 = 0.0f;
        for (int i = 0; i < (int)g_drops.size(); ++i) {
            const ItemDrop& d = g_drops[(size_t)i];
            Vec3 c = {d.x, d.y, d.z};
            Vec3 rel = vec3_sub(c, ray_o);
            float t = vec3_dot(rel, ray_d);
            if (t < 0.2f || t > ray_max) continue;
            Vec3 closest = vec3_add(ray_o, vec3_scale(ray_d, t));
            Vec3 diff = vec3_sub(c, closest);
            float perp2 = vec3_dot(diff, diff);

            // "hitbox" da mira para o drop (um pouco generoso)
            if (perp2 <= 0.26f * 0.26f) {
                // E so considera se o drop nao estiver muito longe do player (alcance real)
                float dx = d.x - g_player.pos.x;
                float dz = d.z - g_player.pos.y;
                float d2 = dx * dx + dz * dz;
                if (d2 <= (kReach + 1.5f) * (kReach + 1.5f)) {
                    if (t < best_t || (std::fabs(t - best_t) < 0.15f && perp2 < best_perp2)) {
                        best_t = t;
                        best_perp2 = perp2;
                        g_target_drop = i;
                    }
                }
            }
        }
    }

    int last_place_x = -1;
    int last_place_y = -1;
    int last_in_bounds_x = -1;
    int last_in_bounds_y = -1;

    auto sample_hits_tile = [&](int tx, int tz, const Vec3& p, Block b) -> bool {
        float base_y = (float)g_world->height_at(tx, tz) * kHeightScale;
        if (b == Block::Air) {
            // Tile vazio: mirar/colocar no "chao" (ground layer) daquele tile.
            float y = base_y + 0.01f;
            return std::fabs(p.y - y) <= 0.40f;
        }
        if (b == Block::Leaves) {
            float y = base_y + 0.60f;
            return std::fabs(p.y - y) <= 0.20f;
        }
        if (b == Block::Water) {
            float y = base_y - 0.18f;
            return std::fabs(p.y - y) <= 0.26f;
        }
        if (!is_ground_like(b)) {
            return (p.y >= base_y - 0.05f && p.y <= base_y + 1.05f);
        }
        // Solo: tratar como "plano" pro raycast (fica facil mirar no chao para minerar/colocar)
        float y = base_y + 0.01f;
        return std::fabs(p.y - y) <= 0.40f;
    };

    // Raymarch em tiles (X/Z), com um criterio simples de altura para evitar "mirar o chao" quando a mira esta no ceu.
    int prev_tx = std::numeric_limits<int>::min();
    int prev_tz = std::numeric_limits<int>::min();
    for (float t = 0.35f; t <= ray_max; t += 0.12f) {
        Vec3 p = vec3_add(ray_o, vec3_scale(ray_d, t));
        int tx = (int)std::floor(p.x);
        int tz = (int)std::floor(p.z);
        if (tx == prev_tx && tz == prev_tz) continue;
        prev_tx = tx; prev_tz = tz;

        if (!g_world->in_bounds(tx, tz)) break;
        last_in_bounds_x = tx;
        last_in_bounds_y = tz;

        Block b = g_world->get(tx, tz);
        if (!sample_hits_tile(tx, tz, p, b)) continue;

        if (placeable_tile(b)) {
            last_place_x = tx;
            last_place_y = tz;
        }

        if (blocks_raycast(b)) {
            g_target_x = tx;
            g_target_y = tz;
            g_has_target = true;

            float dx = (g_target_x + 0.5f) - g_player.pos.x;
            float dz = (g_target_y + 0.5f) - g_player.pos.y;
            float dist = std::sqrt(dx * dx + dz * dz);
            g_target_in_range = (dist <= kReach);

            // Alvo de colocacao: se o alvo for substituivel, coloca nele; senao, no tile anterior "placeable"
            if (placeable_tile(b)) {
                g_place_x = tx;
                g_place_y = tz;
                g_has_place_target = true;
                g_place_in_range = g_target_in_range;
            } else if (last_place_x != -1) {
                g_place_x = last_place_x;
                g_place_y = last_place_y;
                g_has_place_target = true;
                // range baseado no place target (nao no bloqueador)
                float pdx = (g_place_x + 0.5f) - g_player.pos.x;
                float pdz = (g_place_y + 0.5f) - g_player.pos.y;
                g_place_in_range = (std::sqrt(pdx * pdx + pdz * pdz) <= kReach);
            }

            if (!g_onboarding.shown_first_mine && is_mineable(b)) {
                show_tip("Segure clique esquerdo (ou E) para minerar blocos", g_onboarding.shown_first_mine);
            }
            break;
        }
    }

    // Se nao encontrou nada "bloqueando", usar o ultimo tile valido na direcao da mira
    if (!g_has_target) {
        if (last_place_x != -1) {
            g_target_x = last_place_x;
            g_target_y = last_place_y;
            g_has_target = true;
        } else if (last_in_bounds_x != -1) {
            g_target_x = last_in_bounds_x;
            g_target_y = last_in_bounds_y;
            g_has_target = true;
        }

        if (g_has_target) {
            float dx = (g_target_x + 0.5f) - g_player.pos.x;
            float dz = (g_target_y + 0.5f) - g_player.pos.y;
            g_target_in_range = (std::sqrt(dx * dx + dz * dz) <= kReach);

            Block b = g_world->get(g_target_x, g_target_y);
            if (placeable_tile(b)) {
                g_place_x = g_target_x;
                g_place_y = g_target_y;
                g_has_place_target = true;
                g_place_in_range = g_target_in_range;
            }
        }
    }

    // Fallback: se nao houver tile de colocacao direto, tenta um adjacente do alvo atual.
    if (!g_has_place_target && g_has_target) {
        float best_d2 = std::numeric_limits<float>::infinity();
        int best_x = -1;
        int best_y = -1;
        for (int oz = -1; oz <= 1; ++oz) {
            for (int ox = -1; ox <= 1; ++ox) {
                if (ox == 0 && oz == 0) continue;
                int tx = g_target_x + ox;
                int tz = g_target_y + oz;
                if (!g_world->in_bounds(tx, tz)) continue;
                Block nb = g_world->get(tx, tz);
                if (!placeable_tile(nb)) continue;
                float dx = (tx + 0.5f) - g_player.pos.x;
                float dz = (tz + 0.5f) - g_player.pos.y;
                float d2 = dx * dx + dz * dz;
                if (d2 < best_d2) {
                    best_d2 = d2;
                    best_x = tx;
                    best_y = tz;
                }
            }
        }
        if (best_x != -1) {
            g_place_x = best_x;
            g_place_y = best_y;
            g_has_place_target = true;
            g_place_in_range = (best_d2 <= kReach * kReach);
        }
    }

    // Cooldowns (apenas colocacao)
    if (g_place_cd > 0.0f) g_place_cd -= dt;

    bool lmb = key_down(VK_LBUTTON);
    bool rmb = key_down(VK_RBUTTON);

    bool e_key = key_down('E');
    g_prev_e = e_key;

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

            // Para blocos de terreno, remover volume real para feedback visual claro.
            if (is_ground_like(b)) {
                int16_t h = g_world->height_at(g_target_x, g_target_y);
                if (h > 0) g_world->set_height(g_target_x, g_target_y, (int16_t)(h - 1));
                Block g = g_world->get_ground(g_target_x, g_target_y);
                if (g == b || g == Block::Snow || g == Block::Ice || g == Block::Sand || g == Block::Dirt || g == Block::Grass) {
                    g_world->set_ground(g_target_x, g_target_y, Block::Stone);
                }
            }

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
        case WM_MBUTTONDOWN:
            // Capturar mouse ao clicar com botao do meio para rotacionar camera
            g_mouse_captured = true;
            SetCapture(hwnd);
            ShowCursor(FALSE);
            return 0;
        case WM_MBUTTONUP:
            // Liberar mouse
            g_mouse_captured = false;
            ReleaseCapture();
            ShowCursor(TRUE);
            return 0;
        case WM_RBUTTONDOWN:
            // Clique direito do mouse - usado para construir (processado no update)
            g_mouse_x = LOWORD(lParam);
            g_mouse_y = HIWORD(lParam);
            return 0;
        case WM_LBUTTONDOWN:
            // Clique esquerdo do mouse - usado para selecionar/minerar
            g_mouse_left_clicked = true;
            g_mouse_x = LOWORD(lParam);
            g_mouse_y = HIWORD(lParam);
            return 0;
        case WM_MOUSEMOVE:
            // Sempre atualiza posicao do mouse
            g_mouse_x = LOWORD(lParam);
            g_mouse_y = HIWORD(lParam);
            
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

    reload_physics_config(true);
    reload_terrain_config(true);
    reload_sky_config(true);

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
