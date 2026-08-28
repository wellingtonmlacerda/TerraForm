#include "ui_build_menu.h"

#include "raylib_platform.h"
#include "blocks.h"
#include "textures.h"
#include "font.h"
#include "render_primitives.h"
#include "game_state.h"
#include "modules_building.h"
#include "inventory_crafting.h"
#include "world.h"
#include "player_physics.h"
#include "minimap.h"            // add_waypoint (marca onde a obra esta sendo erguida)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

// Globais de outros modulos (mesmo padrao "extern local" usado em todo o projeto).
extern bool g_show_build_menu;
extern int  g_build_menu_selection;   // indice em kBuildables (nao no vetor da categoria)
extern float g_base_energy, g_base_water, g_base_oxygen, g_base_food, g_base_integrity;
extern float g_day_time;
extern int g_base_x, g_base_y;
bool key_down(int vk);

// Tetos das reservas da base. Duplicados como static constexpr em 4 unidades de traducao ja hoje
// (main.cpp/modules_building.cpp/building_interaction.cpp/ui_hud.cpp) - seguindo o mesmo padrao em
// vez de introduzir um header novo so' pra isso.
static constexpr float kBaseEnergyMax = 500.0f;
static constexpr float kBaseWaterMax = 200.0f;
static constexpr float kBaseOxygenMax = 200.0f;
static constexpr float kBaseFoodMax = 200.0f;
static constexpr float kBaseIntegrityMax = 100.0f;

namespace {

// ---- PALETA: hierarquia, nao arco-iris ----
// O menu antigo pintava quase toda linha de uma cor diferente (verde de producao, laranja de
// consumo, vermelho de status, azul de titulo, cinza de descricao) na MESMA linha - o olho nao
// tinha onde descansar. Aqui: texto e' branco/cinza, ciano marca estrutura e foco, e verde/ambar/
// vermelho aparecem SO' em estado (disponivel / sem recurso / bloqueado).
const float kTxt[3]     = {0.92f, 0.94f, 0.97f};   // primario
const float kTxtDim[3]  = {0.58f, 0.62f, 0.70f};   // secundario
const float kTxtFaint[3]= {0.40f, 0.44f, 0.52f};   // rotulos auxiliares
const float kAccent[3]  = {0.38f, 0.78f, 0.95f};   // ciano - estrutura/foco
const float kOk[3]      = {0.36f, 0.88f, 0.52f};   // positivo
const float kWarn[3]    = {0.95f, 0.72f, 0.28f};   // alerta
const float kBad[3]     = {0.92f, 0.42f, 0.38f};   // erro
const float kPanel[3]   = {0.055f, 0.070f, 0.095f};
const float kPanelHi[3] = {0.085f, 0.105f, 0.140f};

// Animacao de abertura. Detectada aqui dentro (transicao de g_show_build_menu) pra nao exigir que
// quem abre o menu avise nada.
float g_open_anim = 0.0f;      // 0 fechado, 1 aberto
bool  g_prev_open = false;
float g_focus_pulse = 0.0f;    // brilho curto ao trocar selecao/categoria
int   g_prev_sel = -1;

void tx(float x, float y, const std::string& s, const float c[3], float a = 1.0f) {
    draw_text(x, y, s, c[0], c[1], c[2], a);
}

// Regua fina de 1px - o elemento que mais faz a interface ler como computador de bordo.
void rule(float x, float y, float w, const float c[3], float a) {
    render_quad(x, y, w, 1.0f, c[0], c[1], c[2], a);
}

// Cantos em L: 4 marcas geometricas curtas nos vertices de um retangulo. Barato e le como
// enquadramento de HUD sci-fi sem encher a tela de neon.
void corner_marks(float x, float y, float w, float h, float len, const float c[3], float a) {
    render_quad(x, y, len, 1.0f, c[0], c[1], c[2], a);
    render_quad(x, y, 1.0f, len, c[0], c[1], c[2], a);
    render_quad(x + w - len, y, len, 1.0f, c[0], c[1], c[2], a);
    render_quad(x + w - 1.0f, y, 1.0f, len, c[0], c[1], c[2], a);
    render_quad(x, y + h - 1.0f, len, 1.0f, c[0], c[1], c[2], a);
    render_quad(x, y + h - len, 1.0f, len, c[0], c[1], c[2], a);
    render_quad(x + w - len, y + h - 1.0f, len, 1.0f, c[0], c[1], c[2], a);
    render_quad(x + w - 1.0f, y + h - len, 1.0f, len, c[0], c[1], c[2], a);
}

// ---- GLIFOS ----
// Nao existem icones de energia/agua/O2/comida no projeto (o atlas so' tem texturas de BLOCO), e
// o pedido foi explicito em nao acrescentar dependencia grafica. Sao marcas geometricas montadas
// com os primitivos 2D que ja existem - pequenas (10-14px), suficientes pra identificar a linha
// sem virar desenho.
enum class Glyph { Energy, Water, Oxygen, Food, Integrity, Time, Lock, Check, Cross, Diamond };

void glyph(Glyph g, float x, float y, float s, const float c[3], float a) {
    const float r = c[0], gg = c[1], b = c[2];
    switch (g) {
        case Glyph::Energy:   // raio: dois paralelogramos escalonados
            render_quad(x + s * 0.42f, y, s * 0.22f, s * 0.55f, r, gg, b, a);
            render_quad(x + s * 0.24f, y + s * 0.45f, s * 0.22f, s * 0.55f, r, gg, b, a);
            render_quad(x + s * 0.30f, y + s * 0.40f, s * 0.40f, s * 0.14f, r, gg, b, a);
            break;
        case Glyph::Water:    // gota: triangulo por cima de um disco
            for (int i = 0; i < 4; ++i) {
                float t = (float)i / 3.0f;
                float w = s * 0.16f + t * s * 0.34f;
                render_quad(x + s * 0.5f - w * 0.5f, y + t * s * 0.42f, w, s * 0.12f, r, gg, b, a);
            }
            render_circle(x + s * 0.5f, y + s * 0.66f, s * 0.28f, r, gg, b, a, 10);
            break;
        case Glyph::Oxygen:   // anel
            render_circle(x + s * 0.5f, y + s * 0.5f, s * 0.46f, r, gg, b, a, 12);
            render_circle(x + s * 0.5f, y + s * 0.5f, s * 0.24f, kPanel[0], kPanel[1], kPanel[2], 1.0f, 12);
            break;
        case Glyph::Food:     // folha: elipse inclinada + haste
            render_ellipse(x + s * 0.52f, y + s * 0.44f, s * 0.34f, s * 0.44f, r, gg, b, a, 12);
            render_quad(x + s * 0.46f, y + s * 0.55f, s * 0.08f, s * 0.45f, r * 0.7f, gg * 0.7f, b * 0.7f, a);
            break;
        case Glyph::Integrity: // escudo: retangulo com base afunilada
            render_quad(x + s * 0.16f, y, s * 0.68f, s * 0.52f, r, gg, b, a);
            for (int i = 0; i < 3; ++i) {
                float t = (float)i / 2.0f;
                float w = s * 0.68f * (1.0f - t * 0.75f);
                render_quad(x + s * 0.5f - w * 0.5f, y + s * 0.50f + t * s * 0.17f, w, s * 0.18f, r, gg, b, a);
            }
            break;
        case Glyph::Time:     // relogio: anel + dois ponteiros
            render_circle(x + s * 0.5f, y + s * 0.5f, s * 0.46f, r, gg, b, a * 0.85f, 12);
            render_circle(x + s * 0.5f, y + s * 0.5f, s * 0.32f, kPanel[0], kPanel[1], kPanel[2], 1.0f, 12);
            render_quad(x + s * 0.47f, y + s * 0.22f, s * 0.07f, s * 0.30f, r, gg, b, a);
            render_quad(x + s * 0.47f, y + s * 0.47f, s * 0.28f, s * 0.07f, r, gg, b, a);
            break;
        case Glyph::Lock:     // cadeado: corpo + arco
            render_quad(x + s * 0.18f, y + s * 0.42f, s * 0.64f, s * 0.52f, r, gg, b, a);
            render_quad(x + s * 0.30f, y + s * 0.14f, s * 0.40f, s * 0.10f, r, gg, b, a);
            render_quad(x + s * 0.28f, y + s * 0.18f, s * 0.09f, s * 0.26f, r, gg, b, a);
            render_quad(x + s * 0.63f, y + s * 0.18f, s * 0.09f, s * 0.26f, r, gg, b, a);
            break;
        case Glyph::Check:    // dois tracos formando o V
            render_quad(x + s * 0.14f, y + s * 0.48f, s * 0.10f, s * 0.30f, r, gg, b, a);
            render_quad(x + s * 0.22f, y + s * 0.66f, s * 0.22f, s * 0.10f, r, gg, b, a);
            render_quad(x + s * 0.40f, y + s * 0.44f, s * 0.12f, s * 0.28f, r, gg, b, a);
            render_quad(x + s * 0.50f, y + s * 0.22f, s * 0.12f, s * 0.28f, r, gg, b, a);
            render_quad(x + s * 0.60f, y + s * 0.06f, s * 0.12f, s * 0.22f, r, gg, b, a);
            break;
        case Glyph::Cross:
            for (int i = 0; i < 5; ++i) {
                float t = (float)i / 4.0f;
                render_quad(x + s * 0.18f + t * s * 0.56f, y + s * 0.18f + t * s * 0.56f,
                            s * 0.16f, s * 0.16f, r, gg, b, a);
                render_quad(x + s * 0.74f - t * s * 0.56f, y + s * 0.18f + t * s * 0.56f,
                            s * 0.16f, s * 0.16f, r, gg, b, a);
            }
            break;
        case Glyph::Diamond:
            for (int i = 0; i < 4; ++i) {
                float t = (float)i / 3.0f;
                float w = s * (1.0f - std::fabs(t - 0.5f) * 2.0f) * 0.9f + s * 0.1f;
                render_quad(x + s * 0.5f - w * 0.5f, y + t * s * 0.8f, w, s * 0.26f, r, gg, b, a);
            }
            break;
    }
}

// Icone de RECURSO/MODULO. Usa block_icon_tex (nao block_tex): os 5 minerios compartilham a matriz
// de rocha cinza no mundo e como icone davam cinco quadrados iguais - ver textures.h.
void resource_icon(Block b, float x, float y, float s, float a) {
    if (g_tex_atlas != 0) {
        BlockTex bt = block_icon_tex(b);
        float tr = 1.0f, tg = 1.0f, tb = 1.0f;
        if (bt.uses_tint) {
            float cr, cg, cb, ca;
            block_color(b, 128, 256, cr, cg, cb, ca);
            tr = cr; tg = cg; tb = cb;
        }
        render_quad_tex(x, y, s, s, bt.top, tr, tg, tb, a);
    } else {
        float cr, cg, cb, ca;
        block_color(b, 128, 256, cr, cg, cb, ca);
        const float c[3] = {cr, cg, cb};
        glyph(Glyph::Diamond, x, y, s, c, a);
    }
}

// Barra de progresso fina, estilo painel industrial: trilha escura + preenchimento + marcas de
// escala a cada 25%.
void meter(float x, float y, float w, float h, float pct, const float c[3], float a) {
    render_quad(x, y, w, h, 0.10f, 0.12f, 0.16f, 0.95f * a);
    pct = std::clamp(pct, 0.0f, 1.0f);
    if (pct > 0.0f) render_quad(x, y, w * pct, h, c[0], c[1], c[2], 0.92f * a);
    for (int i = 1; i < 4; ++i) {
        render_quad(x + w * (float)i * 0.25f, y, 1.0f, h, 0.03f, 0.04f, 0.06f, 0.85f * a);
    }
    rule(x, y, w, kAccent, 0.14f * a);
}

// Estado de uma construcao. Calculado UMA vez e usado pelo card e pelo painel de detalhes, pra os
// dois nunca discordarem (o menu antigo recalculava status em lugares diferentes).
struct BuildState {
    bool unlocked;
    bool affordable;
    bool building;
    float progress;
    int active_count;
    const char* label;
    const float* color;
};

BuildState state_of(Block t) {
    BuildState s{};
    s.unlocked = is_unlocked(t);
    s.affordable = can_afford(get_module_cost(t));
    for (const auto& job : g_construction_queue) {
        if (job.active && job.module_type == t) {
            s.building = true;
            s.progress = 1.0f - (job.time_remaining / std::max(0.01f, job.total_time));
            break;
        }
    }
    for (const auto& m : g_modules) if (m.type == t) ++s.active_count;
    // Ordem importa: bloqueio por progressao vem ANTES de recurso, senao um modulo bloqueado mas
    // pagavel leria "DISPONIVEL" e seria recusado na hora de construir.
    if (s.building)        { s.label = "CONSTRUINDO";  s.color = kWarn; }
    else if (!s.unlocked)  { s.label = "BLOQUEADO";    s.color = kBad; }
    else if (s.affordable) { s.label = "DISPONIVEL";   s.color = kOk; }
    else                   { s.label = "SEM RECURSOS"; s.color = kWarn; }
    return s;
}

// Producao/consumo em linhas estruturadas (glifo + texto), em vez da string concatenada que o menu
// antigo montava. Devolve quantas linhas escreveu.
struct RateLine { Glyph g; std::string text; const float* color; };

int rate_lines(const ModuleStats& st, RateLine* out, int max_out) {
    char buf[48];
    int n = 0;
    auto add = [&](Glyph g, const char* fmt, float v, const float* c) {
        if (n >= max_out || v <= 0.0f) return;
        snprintf(buf, sizeof(buf), fmt, v);
        out[n++] = {g, std::string(buf), c};
    };
    add(Glyph::Energy, "+%.0f Energia/min", st.energy_production, kOk);
    add(Glyph::Oxygen, "+%.1f O2/min", st.oxygen_production, kOk);
    add(Glyph::Water,  "+%.1f Agua/min", st.water_production, kOk);
    add(Glyph::Food,   "+%.1f Comida/min", st.food_production, kOk);
    add(Glyph::Integrity, "+%.0f Reparo/min", st.integrity_bonus, kOk);
    add(Glyph::Diamond, "+%.1f CO2/min", st.co2_production, kAccent);
    if (st.energy_consumption > 0.0f && n < max_out) {
        snprintf(buf, sizeof(buf), "-%.1f Energia/min", st.energy_consumption);
        out[n++] = {Glyph::Energy, std::string(buf), kWarn};
    }
    return n;
}

// A categoria do modulo atualmente selecionado.
BuildCategory current_category() {
    int sel = std::clamp(g_build_menu_selection, 0, kBuildableCount - 1);
    return kBuildables[sel].category;
}

} // namespace

// ============================================================================
void render_build_menu(int win_w, int win_h) {
    // --- animacao de abertura/fechamento (curta: interface tem que continuar rapida) ---
    float dt = GetFrameTime();
    if (dt > 0.1f) dt = 0.1f;
    bool open = (g_show_build_menu && g_state == GameState::Playing);
    if (open != g_prev_open) { g_prev_open = open; g_focus_pulse = 1.0f; }
    g_open_anim += (open ? 1.0f : -1.0f) * dt * 7.0f;
    g_open_anim = std::clamp(g_open_anim, 0.0f, 1.0f);
    if (g_open_anim <= 0.001f) return;

    if (g_build_menu_selection != g_prev_sel) { g_prev_sel = g_build_menu_selection; g_focus_pulse = 1.0f; }
    g_focus_pulse = std::max(0.0f, g_focus_pulse - dt * 4.0f);

    // Suavizacao (ease-out) + leve slide de baixo pra cima.
    float e = 1.0f - (1.0f - g_open_anim) * (1.0f - g_open_anim);
    float A = e;                       // alpha global
    float slide = (1.0f - e) * 26.0f;  // deslocamento em px

    // --- geometria responsiva: nada de coordenada fixa ---
    float pw = std::clamp((float)win_w * 0.86f, 760.0f, 1180.0f);
    float ph = std::clamp((float)win_h * 0.84f, 560.0f, 860.0f);
    if (pw > (float)win_w - 24.0f) pw = (float)win_w - 24.0f;
    if (ph > (float)win_h - 24.0f) ph = (float)win_h - 24.0f;
    float px = (float)win_w * 0.5f - pw * 0.5f;
    float py = (float)win_h * 0.5f - ph * 0.5f + slide;

    // Escurece a cena e desenha o casco do painel.
    render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.0f, 0.0f, 0.0f, 0.72f * A);
    render_quad(px, py, pw, ph, kPanel[0], kPanel[1], kPanel[2], 0.985f * A);
    corner_marks(px, py, pw, ph, 22.0f, kAccent, 0.85f * A);
    rule(px, py, pw, kAccent, 0.55f * A);
    rule(px, py + ph - 1.0f, pw, kAccent, 0.30f * A);

    // ---------------- HEADER ----------------
    const float head_h = 54.0f;
    tx(px + 20.0f, py + 26.0f, "CONSTRUCAO DA COLONIA", kTxt, A);
    tx(px + 20.0f, py + 42.0f, "Infraestrutura disponivel", kTxtFaint, 0.9f * A);
    {
        std::string close = "[TAB] FECHAR";
        float w = estimate_text_w_px(close);
        tx(px + pw - 20.0f - w, py + 30.0f, close, kTxtDim, 0.9f * A);
    }
    // Indicador de "sistema ativo": ponto pulsando, o tipo de detalhe que faz ler como terminal.
    {
        float blink = 0.55f + 0.45f * std::sin(g_day_time * 3.0f);
        render_circle(px + pw - 150.0f, py + 26.0f, 3.0f, kOk[0], kOk[1], kOk[2], blink * A, 8);
        tx(px + pw - 142.0f, py + 30.0f, "ONLINE", kTxtFaint, 0.75f * A);
    }
    rule(px + 16.0f, py + head_h, pw - 32.0f, kAccent, 0.35f * A);

    // ---------------- COLUNAS ----------------
    const float foot_h = 62.0f;
    float body_y = py + head_h + 12.0f;
    float body_h = ph - head_h - foot_h - 24.0f;
    float cat_w = 168.0f;
    float det_w = std::min(330.0f, pw * 0.30f);
    float grid_x = px + 16.0f + cat_w + 14.0f;
    float grid_w = pw - 32.0f - cat_w - det_w - 28.0f;
    float det_x = px + pw - 16.0f - det_w;

    // ---- CATEGORIAS ----
    {
        float cx = px + 16.0f;
        tx(cx, body_y + 12.0f, "CATEGORIAS", kTxtFaint, 0.85f * A);
        rule(cx, body_y + 18.0f, cat_w, kAccent, 0.18f * A);
        float row_h = 40.0f;
        float y = body_y + 28.0f;
        BuildCategory cur = current_category();
        static const Glyph kCatGlyph[kBuildCategoryCount] = {
            Glyph::Energy, Glyph::Oxygen, Glyph::Integrity, Glyph::Diamond
        };
        for (int c = 0; c < kBuildCategoryCount; ++c) {
            BuildCategory bc = (BuildCategory)c;
            bool active = (bc == cur);
            // Quantos itens desta categoria estao construiveis AGORA - o numero que responde
            // "o que posso construir?" sem ler nenhum card.
            int total = 0, ready = 0;
            for (int i = 0; i < kBuildableCount; ++i) {
                if (kBuildables[i].category != bc) continue;
                ++total;
                BuildState s = state_of(kBuildables[i].type);
                if (s.unlocked && s.affordable && !s.building) ++ready;
            }
            if (active) {
                render_quad(cx, y, cat_w, row_h - 4.0f, kPanelHi[0], kPanelHi[1], kPanelHi[2], 0.95f * A);
                render_quad(cx, y, 2.5f, row_h - 4.0f, kAccent[0], kAccent[1], kAccent[2],
                            (0.85f + 0.15f * g_focus_pulse) * A);
            }
            const float* gc = active ? kAccent : kTxtFaint;
            glyph(kCatGlyph[c], cx + 12.0f, y + 10.0f, 15.0f, gc, (active ? 0.95f : 0.6f) * A);
            tx(cx + 34.0f, y + 18.0f, build_category_name(bc), active ? kTxt : kTxtDim,
               (active ? 1.0f : 0.85f) * A);
            char cnt[24];
            snprintf(cnt, sizeof(cnt), "%d/%d", ready, total);
            float cw = estimate_text_w_px(cnt);
            tx(cx + cat_w - 10.0f - cw, y + 18.0f, cnt, ready > 0 ? kOk : kTxtFaint, 0.85f * A);
            tx(cx + 34.0f, y + 31.0f, ready > 0 ? "pronto" : "indisponivel", kTxtFaint, 0.6f * A);
            y += row_h;
        }
        render_quad(cx + cat_w + 7.0f, body_y + 8.0f, 1.0f, body_h - 8.0f,
                    kAccent[0], kAccent[1], kAccent[2], 0.16f * A);
    }

    // ---- GRADE DE CARDS ----
    BuildCategory cur = current_category();
    {
        tx(grid_x, body_y + 12.0f, build_category_name(cur), kTxtFaint, 0.85f * A);
        rule(grid_x, body_y + 18.0f, grid_w, kAccent, 0.18f * A);

        int cols = (grid_w >= 420.0f) ? 2 : 1;
        float gap = 12.0f;
        float card_w = (grid_w - gap * (float)(cols - 1)) / (float)cols;
        float card_h = 128.0f;
        float gy = body_y + 30.0f;
        int slot = 0;
        for (int i = 0; i < kBuildableCount; ++i) {
            if (kBuildables[i].category != cur) continue;
            Block t = kBuildables[i].type;
            ModuleStats st = get_module_stats(t);
            BuildState s = state_of(t);
            bool sel = (i == g_build_menu_selection);

            float cxx = grid_x + (float)(slot % cols) * (card_w + gap);
            float cyy = gy + (float)(slot / cols) * (card_h + gap);
            ++slot;

            // Corpo do card. Selecionado ganha fundo mais claro e cantos ciano; construivel ganha
            // uma barra de acento a esquerda - e' o "destaque visual" pedido pra PODE CONSTRUIR.
            bool ready = s.unlocked && s.affordable && !s.building;
            render_quad(cxx, cyy, card_w, card_h, sel ? kPanelHi[0] : 0.070f,
                        sel ? kPanelHi[1] : 0.085f, sel ? kPanelHi[2] : 0.115f, 0.95f * A);
            if (sel) {
                float g = 0.55f + 0.45f * g_focus_pulse;
                corner_marks(cxx, cyy, card_w, card_h, 14.0f, kAccent, (0.55f + 0.45f * g) * A);
                rule(cxx, cyy, card_w, kAccent, 0.9f * A);
            }
            render_quad(cxx, cyy, 3.0f, card_h,
                        ready ? kOk[0] : (s.unlocked ? kWarn[0] : kBad[0]),
                        ready ? kOk[1] : (s.unlocked ? kWarn[1] : kBad[1]),
                        ready ? kOk[2] : (s.unlocked ? kWarn[2] : kBad[2]),
                        (ready ? 0.95f : 0.55f) * A);

            // Icone grande do modulo.
            float icon = 40.0f;
            resource_icon(t, cxx + 12.0f, cyy + 14.0f, icon, (s.unlocked ? 1.0f : 0.35f) * A);
            if (!s.unlocked) {
                glyph(Glyph::Lock, cxx + 12.0f + icon * 0.5f - 8.0f, cyy + 14.0f + icon * 0.5f - 8.0f,
                      16.0f, kBad, 0.95f * A);
            }

            // Nome + contagem de ativos. A contagem responde "eu ja tenho um destes?", que era
            // parte da confusao de "ja tem paineis solares ao redor da base".
            std::string nm = st.name;
            if (s.active_count > 0) nm += "  x" + std::to_string(s.active_count);
            tx(cxx + 62.0f, cyy + 26.0f, nm, s.unlocked ? kTxt : kTxtDim, A);

            // A informacao MAIS importante do card: o que produz. Uma linha, nao seis.
            RateLine rl[7];
            int nr = rate_lines(st, rl, 7);
            if (nr > 0) {
                glyph(rl[0].g, cxx + 62.0f, cyy + 34.0f, 12.0f, rl[0].color, 0.9f * A);
                tx(cxx + 78.0f, cyy + 44.0f, rl[0].text, rl[0].color, 0.95f * A);
            } else {
                tx(cxx + 62.0f, cyy + 44.0f, "Terraformacao", kAccent, 0.9f * A);
            }

            // Tempo de construcao.
            char ts[24];
            snprintf(ts, sizeof(ts), "%ds", (int)st.construction_time);
            glyph(Glyph::Time, cxx + 62.0f, cyy + 54.0f, 11.0f, kTxtFaint, 0.8f * A);
            tx(cxx + 77.0f, cyy + 63.0f, ts, kTxtDim, 0.9f * A);

            // Faixa de estado no pe do card.
            rule(cxx + 10.0f, cyy + card_h - 28.0f, card_w - 20.0f, kAccent, 0.12f * A);
            glyph(ready ? Glyph::Check : (s.unlocked ? Glyph::Cross : Glyph::Lock),
                  cxx + 12.0f, cyy + card_h - 22.0f, 12.0f, s.color, 0.95f * A);
            tx(cxx + 30.0f, cyy + card_h - 12.0f, s.label, s.color, 0.95f * A);

            // Barra de progresso quando em construcao.
            if (s.building) {
                meter(cxx + 10.0f, cyy + card_h - 8.0f, card_w - 20.0f, 4.0f, s.progress, kWarn, A);
            }
        }
    }

    // ---- PAINEL DE DETALHES + STATUS DA COLONIA ----
    {
        render_quad(det_x - 8.0f, body_y + 4.0f, det_w + 8.0f, body_h - 4.0f,
                    0.040f, 0.052f, 0.072f, 0.92f * A);
        render_quad(det_x - 8.0f, body_y + 4.0f, 1.0f, body_h - 4.0f,
                    kAccent[0], kAccent[1], kAccent[2], 0.22f * A);

        int sel = std::clamp(g_build_menu_selection, 0, kBuildableCount - 1);
        Block t = kBuildables[sel].type;
        ModuleStats st = get_module_stats(t);
        BuildState s = state_of(t);
        float y = body_y + 22.0f;

        tx(det_x, y, "DETALHES", kTxtFaint, 0.85f * A); y += 8.0f;
        rule(det_x, y, det_w - 10.0f, kAccent, 0.18f * A); y += 20.0f;
        tx(det_x, y, st.name, kTxt, A); y += 20.0f;

        // Descricao com quebra de linha por largura (a fonte e' monoespacada, entao da' pra quebrar
        // contando caracteres pela largura medida).
        {
            float avail = det_w - 12.0f;
            float chw = std::max(1.0f, estimate_text_w_px("M"));
            int per_line = std::max(8, (int)(avail / chw));
            std::string d = st.description;
            size_t pos = 0;
            while (pos < d.size()) {
                size_t take = std::min((size_t)per_line, d.size() - pos);
                if (pos + take < d.size()) {
                    size_t sp = d.rfind(' ', pos + take);
                    if (sp != std::string::npos && sp > pos) take = sp - pos;
                }
                tx(det_x, y, d.substr(pos, take), kTxtDim, 0.92f * A);
                y += 14.0f;
                pos += take;
                while (pos < d.size() && d[pos] == ' ') ++pos;
            }
            y += 6.0f;
        }

        // Producao/consumo - todas as linhas (o card mostra so' a principal).
        {
            RateLine rl[7];
            int nr = rate_lines(st, rl, 7);
            tx(det_x, y, "ESTE MODULO", kTxtFaint, 0.8f * A); y += 8.0f;
            rule(det_x, y, det_w - 10.0f, kAccent, 0.14f * A); y += 15.0f;
            if (nr == 0) {
                tx(det_x, y, "Acelera a terraformacao", kAccent, 0.9f * A); y += 15.0f;
            }
            for (int i = 0; i < nr; ++i) {
                glyph(rl[i].g, det_x, y - 9.0f, 12.0f, rl[i].color, 0.9f * A);
                tx(det_x + 17.0f, y, rl[i].text, rl[i].color, 0.95f * A);
                y += 15.0f;
            }
            y += 6.0f;
        }

        // CUSTO (ou requisito de desbloqueio, quando bloqueado) - linha por recurso, com marca.
        {
            ResourceReq rows[10];
            int nrow;
            bool locked = !s.unlocked;
            if (locked) {
                tx(det_x, y, "REQUISITO DE DESBLOQUEIO", kTxtFaint, 0.8f * A);
                nrow = module_unlock_breakdown(t, rows, 10);
            } else {
                tx(det_x, y, "CUSTO", kTxtFaint, 0.8f * A);
                nrow = module_cost_breakdown(t, rows, 10);
            }
            y += 8.0f;
            rule(det_x, y, det_w - 10.0f, kAccent, 0.14f * A); y += 16.0f;

            for (int i = 0; i < nrow; ++i) {
                bool ok = rows[i].have >= rows[i].need;
                const float* c = ok ? kOk : kBad;
                glyph(ok ? Glyph::Check : Glyph::Cross, det_x, y - 10.0f, 11.0f, c, 0.95f * A);
                resource_icon(rows[i].block, det_x + 16.0f, y - 11.0f, 13.0f, 0.95f * A);
                tx(det_x + 34.0f, y, rows[i].name, kTxt, 0.92f * A);
                char q[32];
                snprintf(q, sizeof(q), "%d / %d", std::min(rows[i].have, 99999), rows[i].need);
                float qw = estimate_text_w_px(q);
                tx(det_x + det_w - 12.0f - qw, y, q, c, 0.95f * A);
                y += 16.0f;
            }
            if (nrow == 0) { tx(det_x, y, "Sem custo", kTxtDim, 0.9f * A); y += 16.0f; }
            y += 4.0f;
        }

        // TEMPO
        {
            char ts[32];
            snprintf(ts, sizeof(ts), "%d segundos", (int)st.construction_time);
            glyph(Glyph::Time, det_x, y - 10.0f, 12.0f, kTxtDim, 0.85f * A);
            tx(det_x + 17.0f, y, ts, kTxtDim, 0.92f * A);
            y += 20.0f;
        }

        // Botao CONSTRUIR - habilitado/desabilitado com motivo explicito abaixo.
        {
            bool can = s.unlocked && s.affordable && !s.building;
            float bh = 30.0f;
            float bw = det_w - 12.0f;
            render_quad(det_x, y, bw, bh, can ? 0.10f : 0.075f, can ? 0.20f : 0.085f,
                        can ? 0.26f : 0.105f, 0.95f * A);
            const float* bc = can ? kOk : kTxtFaint;
            corner_marks(det_x, y, bw, bh, 10.0f, bc, (can ? 0.85f : 0.35f) * A);
            std::string lab = s.building ? "EM CONSTRUCAO" : "[ENTER] CONSTRUIR";
            float lw = estimate_text_w_px(lab);
            tx(det_x + bw * 0.5f - lw * 0.5f, y + 20.0f, lab, can ? kOk : kTxtFaint,
               (can ? 1.0f : 0.6f) * A);
            y += bh + 6.0f;
            if (!s.unlocked)        tx(det_x, y, "Bloqueado - colete os recursos acima", kBad, 0.9f * A);
            else if (s.building)    tx(det_x, y, "Aguarde a conclusao", kWarn, 0.9f * A);
            else if (!s.affordable) tx(det_x, y, "Recursos insuficientes", kWarn, 0.9f * A);
            else                    tx(det_x, y, "Sera erguido num slot livre da base", kTxtFaint, 0.85f * A);
            y += 20.0f;
        }

        // ---- STATUS DA COLONIA (barras + balanco medido) ----
        {
            rule(det_x, y, det_w - 10.0f, kAccent, 0.22f * A); y += 16.0f;
            tx(det_x, y, "STATUS DA COLONIA", kTxtFaint, 0.85f * A); y += 14.0f;

            auto bar = [&](Glyph g, const char* label, float v, float mx, const float* c) {
                char q[40];
                snprintf(q, sizeof(q), "%d / %d", (int)v, (int)mx);
                glyph(g, det_x, y - 9.0f, 11.0f, c, 0.9f * A);
                tx(det_x + 16.0f, y, label, kTxtDim, 0.9f * A);
                float qw = estimate_text_w_px(q);
                tx(det_x + det_w - 12.0f - qw, y, q, kTxt, 0.92f * A);
                y += 5.0f;
                meter(det_x, y, det_w - 12.0f, 5.0f, v / mx, c, A);
                y += 16.0f;
            };
            bar(Glyph::Energy, "ENERGIA", g_base_energy, kBaseEnergyMax, kWarn);
            bar(Glyph::Water, "AGUA", g_base_water, kBaseWaterMax, kAccent);
            bar(Glyph::Oxygen, "OXIGENIO", g_base_oxygen, kBaseOxygenMax, kOk);
            bar(Glyph::Food, "COMIDA", g_base_food, kBaseFoodMax, kWarn);
            const float* ic = g_base_integrity > 50.0f ? kOk : (g_base_integrity > 25.0f ? kWarn : kBad);
            bar(Glyph::Integrity, "INTEGRIDADE", g_base_integrity, kBaseIntegrityMax, ic);

            y += 2.0f;
            // BALANCO LIQUIDO (g_rate_*, modules_building.h): a taxa REAL medida por diferenca no
            // tick, nao uma soma paralela de tabelas. E' o numero que responde "o que muda quando
            // construo um Painel Solar?" - a energia sai de negativa pra positiva e da' pra ver.
            // Substitui a linha fixa "-1 O2/min -2 Energia/min -1 Agua/min", que mostrava so' o
            // dreno constante e ignorava tudo que os modulos produzem.
            tx(det_x, y, "BALANCO DA COLONIA (medido)", kTxtFaint, 0.75f * A); y += 14.0f;
            auto rate = [&](Glyph g, float v, float col_x) {
                const float* c = (v > 0.05f) ? kOk : ((v < -0.05f) ? kBad : kTxtFaint);
                glyph(g, det_x + col_x, y - 9.0f, 10.0f, c, 0.85f * A);
                char b[24];
                snprintf(b, sizeof(b), "%+.1f/min", v);
                tx(det_x + col_x + 14.0f, y, b, c, 0.9f * A);
            };
            float col = (det_w - 12.0f) * 0.5f;
            rate(Glyph::Energy, g_rate_energy, 0.0f);
            rate(Glyph::Water, g_rate_water, col);
            y += 15.0f;
            rate(Glyph::Oxygen, g_rate_oxygen, 0.0f);
            rate(Glyph::Food, g_rate_food, col);
        }
    }

    // ---------------- RODAPE: recursos + atalhos ----------------
    {
        float fy = py + ph - foot_h;
        rule(px + 16.0f, fy, pw - 32.0f, kAccent, 0.30f * A);
        // Barra de recursos: icone de bloco real + sigla + quantidade. Reusa kElementSlots? Nao -
        // a barra da HUD e' de itens selecionaveis; aqui interessam os recursos que aparecem em
        // CUSTO de construcao, que e' exatamente a lista de campos de CraftCost.
        static const Block kShow[] = {
            Block::Stone, Block::Iron, Block::Copper, Block::Coal, Block::Ice,
            Block::Crystal, Block::Metal, Block::Organic, Block::Components,
        };
        const int nshow = (int)(sizeof(kShow) / sizeof(kShow[0]));
        float slot_w = (pw - 32.0f) / (float)nshow;
        float ry = fy + 10.0f;
        for (int i = 0; i < nshow; ++i) {
            float sx = px + 16.0f + (float)i * slot_w;
            resource_icon(kShow[i], sx, ry, 16.0f, 0.95f * A);
            int have = std::max(0, g_inventory[(int)kShow[i]]);
            char q[16];
            snprintf(q, sizeof(q), "%d", have);
            tx(sx + 21.0f, ry + 12.0f, q, have > 0 ? kTxt : kTxtFaint, 0.95f * A);
            // Nome curto abaixo, pequeno - identificacao por icone + nome + quantidade.
            std::string nm = block_name(kShow[i]);
            if (nm.size() > 8) nm = nm.substr(0, 8);
            tx(sx, ry + 26.0f, nm, kTxtFaint, 0.7f * A);
            if (i > 0) {
                render_quad(sx - 6.0f, ry - 2.0f, 1.0f, 30.0f, kAccent[0], kAccent[1], kAccent[2], 0.10f * A);
            }
        }
        std::string hint = "W/S Construcao    A/D Categoria    ENTER Construir    TAB Fechar";
        float hw = estimate_text_w_px(hint);
        tx(px + pw * 0.5f - hw * 0.5f, py + ph - 8.0f, hint, kTxtFaint, 0.8f * A);
    }
}

// ============================================================================
bool update_build_menu_input() {
    if (!g_show_build_menu) return false;

    static bool prev_w = false, prev_s = false, prev_a = false, prev_d = false, prev_enter = false;
    bool w_now = key_down(KEY_W) || key_down(KEY_UP);
    bool s_now = key_down(KEY_S) || key_down(KEY_DOWN);
    bool a_now = key_down(KEY_A) || key_down(KEY_LEFT);
    bool d_now = key_down(KEY_D) || key_down(KEY_RIGHT);
    bool enter_now = key_down(KEY_ENTER);

    if (g_build_menu_selection < 0) g_build_menu_selection = 0;
    if (g_build_menu_selection >= kBuildableCount) g_build_menu_selection = kBuildableCount - 1;

    // Indices da categoria corrente. kBuildables e' ordenada por categoria, mas a busca explicita
    // evita depender dessa ordem - se alguem reordenar a tabela, a navegacao continua correta.
    BuildCategory cur = kBuildables[g_build_menu_selection].category;
    int idx[16]; int n = 0; int pos = 0;
    for (int i = 0; i < kBuildableCount && n < 16; ++i) {
        if (kBuildables[i].category != cur) continue;
        if (i == g_build_menu_selection) pos = n;
        idx[n++] = i;
    }

    // W/S: construcao anterior/proxima DENTRO da categoria (ciclico).
    if (w_now && !prev_w && n > 0) {
        pos = (pos - 1 + n) % n;
        g_build_menu_selection = idx[pos];
        bounce_hotbar_slot(pos);
    }
    if (s_now && !prev_s && n > 0) {
        pos = (pos + 1) % n;
        g_build_menu_selection = idx[pos];
        bounce_hotbar_slot(pos);
    }

    // A/D: categoria anterior/proxima. Pula pro primeiro item da categoria destino.
    auto goto_category = [](int delta) {
        int c = (int)kBuildables[std::clamp(g_build_menu_selection, 0, kBuildableCount - 1)].category;
        for (int step = 0; step < kBuildCategoryCount; ++step) {
            c = (c + delta + kBuildCategoryCount) % kBuildCategoryCount;
            for (int i = 0; i < kBuildableCount; ++i) {
                if ((int)kBuildables[i].category == c) { g_build_menu_selection = i; return; }
            }
            // Categoria sem itens: continua procurando (nao existe hoje, mas nao trava se surgir).
        }
    };
    if (a_now && !prev_a) goto_category(-1);
    if (d_now && !prev_d) goto_category(+1);

    // Enter: construir. A logica e' a MESMA de antes (nenhuma regra de gameplay mudou aqui):
    // rejeita duplicata na fila, exige recurso, acha slot livre ou cria um perto da base.
    if (enter_now && !prev_enter) {
        Block module_type = kBuildables[std::clamp(g_build_menu_selection, 0, kBuildableCount - 1)].type;
        CraftCost cost = get_module_cost(module_type);

        bool already_building = false;
        for (const auto& job : g_construction_queue) {
            if (job.active && job.module_type == module_type) { already_building = true; break; }
        }

        if (already_building) {
            show_error("Ja em construcao!");
        } else if (!is_unlocked(module_type)) {
            // Guarda defensiva que o menu antigo nao tinha. Hoje ela NAO dispara: todo requisito de
            // desbloqueio e' um subconjunto do custo de construcao com valores menores (~60%), e os
            // totais de desbloqueio sao acumulados na vida - logo "consegue pagar" implica
            // "esta desbloqueado". Fica porque a UI mostra os dois estados como independentes, e um
            // rebalanceamento futuro dos numeros faria a divergencia aparecer aqui em silencio.
            show_error("Modulo bloqueado! Colete mais recursos.");
        } else if (can_afford(cost)) {
            int slot_index = -1;
            for (int si = 0; si < (int)g_build_slots.size(); ++si) {
                if (g_build_slots[si].assigned_module == Block::Air) { slot_index = si; break; }
            }
            if (slot_index < 0 && g_world) {
                for (int dx = -30; dx <= 30; ++dx) {
                    int txp = g_base_x + dx;
                    if (txp < 0 || txp >= g_world->w) continue;
                    int typ = g_base_y - 1;
                    Block current = g_world->get(txp, typ);
                    if (current == Block::Air || current == Block::BuildSlot) {
                        BuildSlotInfo new_slot;
                        new_slot.x = txp;
                        new_slot.y = typ;
                        new_slot.assigned_module = Block::Air;
                        new_slot.label = "Auto";
                        g_build_slots.push_back(new_slot);
                        g_world->set_ground(txp, typ, Block::BuildSlot);
                        slot_index = (int)g_build_slots.size() - 1;
                        break;
                    }
                }
            }
            if (slot_index >= 0) {
                start_construction(module_type, slot_index);
                g_build_slots[slot_index].assigned_module = module_type;
                // FEEDBACK DE ONDE. O modulo e' erguido num slot do anel de raio 38 - quase sempre
                // fora da tela quando o menu fecha, e antes disso o jogador so' via "Construido: X"
                // varios segundos depois, sem saber onde. Agora recebe rumo + distancia e um
                // waypoint no minimapa, que e' removido quando a obra termina (update_modules).
                ModuleStats st = get_module_stats(module_type);
                int sx = g_build_slots[slot_index].x, sy = g_build_slots[slot_index].y;
                float ddx = (float)sx - g_player.pos.x, ddy = (float)sy - g_player.pos.y;
                float dist = std::sqrt(ddx * ddx + ddy * ddy);
                const char* dir = "";
                if (std::fabs(ddx) > std::fabs(ddy)) dir = (ddx > 0.0f) ? "leste" : "oeste";
                else                                 dir = (ddy > 0.0f) ? "sul" : "norte";
                char msg[160];
                snprintf(msg, sizeof(msg), "Obra iniciada: %s - %ds, a %.0fm ao %s (marcado no mapa)",
                         st.name, (int)st.construction_time, dist, dir);
                set_toast(msg, 4.0f);
                add_waypoint(sx, sy, st.name);
            } else {
                show_error("Sem espaco para construir!");
            }
        } else {
            show_error("Recursos insuficientes!");
        }
    }

    prev_w = w_now; prev_s = s_now; prev_a = a_now; prev_d = d_now; prev_enter = enter_now;
    return true;
}
