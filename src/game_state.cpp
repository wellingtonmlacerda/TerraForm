#include "game_state.h"

// ============= RNG =============
// Extracted verbatim from main.cpp (original lines ~1550-1561).
static uint32_t g_rng = 0xA341316Cu;

uint32_t rng_next_u32() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

float rng_next_f01() {
    return (rng_next_u32() & 0x00FFFFFFu) / (float)0x01000000u;
}

// ============= Game state machine =============
GameState g_state = GameState::Playing;

// ============= Toast / feedback globals =============
float g_toast_time = 0.0f;
std::string g_toast;

float g_screen_flash_red = 0.0f;   // Flash vermelho (erro/dano)
float g_screen_flash_green = 0.0f; // Flash verde (sucesso)
float g_hotbar_bounce = 0.0f;      // Animacao de bounce na hotbar

// g_hotbar_bounce_slot: only ever written by bounce_hotbar_slot(), never read anywhere
// else in the codebase (verified by grep across main.cpp before this extraction). Stays
// `static` (internal linkage) here instead of being exposed via game_state.h.
static int g_hotbar_bounce_slot = -1;     // Slot que esta animando

// Popup de coleta flutuante
std::vector<CollectPopup> g_collect_popups;

// Popup de conquista/desbloqueio
float g_unlock_popup_timer = 0.0f;
std::string g_unlock_popup_text = "";
std::string g_unlock_popup_subtitle = "";

// ============= SISTEMA DE ONBOARDING =============
OnboardingState g_onboarding;

void set_toast(const std::string& msg, float seconds) {
    g_toast = msg;
    g_toast_time = seconds;
}

// ============= FUNCOES DE FEEDBACK VISUAL =============

// Mostrar erro com flash vermelho
void show_error(const std::string& msg) {
    set_toast(msg, 2.0f);
    g_screen_flash_red = 0.25f;
}

// Mostrar sucesso com flash verde
void show_success(const std::string& msg) {
    set_toast(msg, 2.0f);
    g_screen_flash_green = 0.20f;
}

// Adicionar popup de coleta flutuante
void add_collect_popup(float x, float y, const std::string& text, float r, float g, float b,
                        Block item, int amount) {
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
void show_unlock_popup(const std::string& title, const std::string& subtitle) {
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
void bounce_hotbar_slot(int slot) {
    g_hotbar_bounce = 0.3f;
    g_hotbar_bounce_slot = slot;
}

// ============= FUNCOES DE ONBOARDING =============

// Mostrar dica contextual (apenas uma vez)
void show_tip(const std::string& tip, bool& shown_flag) {
    if (shown_flag) return;
    shown_flag = true;
    g_onboarding.current_tip = tip;
    g_onboarding.tip_timer = 4.0f;
}

// Atualizar sistema de onboarding
void update_onboarding(float dt) {
    if (g_onboarding.tip_timer > 0.0f) {
        g_onboarding.tip_timer -= dt;
        if (g_onboarding.tip_timer <= 0.0f) {
            g_onboarding.current_tip = "";
        }
    }
}
