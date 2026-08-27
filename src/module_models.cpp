#include "module_models.h"

#include "raylib_platform.h"
#include "blocks.h"
#include "world.h"
#include "camera.h"
#include "lighting.h"
#include "render_primitives.h"
#include "modules_building.h"
#include "game_state.h"

#include <algorithm>
#include <cmath>

extern World* g_world;
extern float g_day_time;
extern int g_base_x, g_base_y;

namespace {

// Iluminacao: mesma abordagem de base_exterior.cpp - amostra o lightmap na posicao do modulo e
// aplica um piso de auto-iluminacao, senao de noite o modulo fica um bloco preto (eles tem luz
// propria de operacao). A neblina ja e' aplicada dentro de cada primitivo (apply_frame_fog).
struct Shade { float r, g, b; };

Shade module_shade(float wx, float wz, float self_lit) {
    Shade s{1.0f, 1.0f, 1.0f};
    if (!g_lighting.enabled) return s;
    float lr, lg, lb;
    sample_lightmap(wx, wz, lr, lg, lb);
    s.r = std::max(self_lit, lr);
    s.g = std::max(self_lit, lg);
    s.b = std::max(self_lit, lb);
    return s;
}

// Atalhos: cor final = cor base * sombreamento.
struct Ctx {
    float x, y, z;      // canto/centro do tile e altura do solo
    float yaw;          // orientacao (aponta pro centro da base)
    Shade sh;
    float t;            // tempo, pra animacao discreta
    float scale;        // 1.0 normal, maior quando aprimorado
    bool  active;       // false = sem energia/danificado: apaga os emissivos
};

Vec3 P(const Ctx& c, float fwd, float up, float side) {
    float cs = std::cos(c.yaw), sn = std::sin(c.yaw);
    // Mesma base local de render_box_oriented_3d: frente = (sn, cos), direita = (cs, -sn).
    return Vec3{ c.x + sn * fwd + cs * side, c.y + up, c.z + cs * fwd - sn * side };
}

void box(const Ctx& c, float fwd, float up, float side, float sx, float sy, float sz,
         float r, float g, float b, float a = 1.0f) {
    render_box_oriented_3d(P(c, fwd * c.scale, up * c.scale, side * c.scale),
                           sx * c.scale, sy * c.scale, sz * c.scale, c.yaw,
                           r * c.sh.r, g * c.sh.g, b * c.sh.b, a);
}

void tilted(const Ctx& c, float fwd, float up, float side, float sx, float sy, float sz,
            float pitch, float r, float g, float b, float a = 1.0f) {
    render_box_tilted_3d(P(c, fwd * c.scale, up * c.scale, side * c.scale),
                         sx * c.scale, sy * c.scale, sz * c.scale, c.yaw, pitch,
                         r * c.sh.r, g * c.sh.g, b * c.sh.b, a);
}

// Emissivo: NAO multiplica pelo lightmap (e' luz propria) e apaga quando o modulo esta parado -
// e' o que da leitura de "ligado/desligado" sem HUD.
void emit(const Ctx& c, float fwd, float up, float side, float sx, float sy, float sz,
          float r, float g, float b) {
    float k = c.active ? 1.0f : 0.22f;
    render_box_oriented_3d(P(c, fwd * c.scale, up * c.scale, side * c.scale),
                           sx * c.scale, sy * c.scale, sz * c.scale, c.yaw,
                           r * k, g * k, b * k, 1.0f);
}

void glow(const Ctx& c, float fwd, float up, float side, float radius, float r, float g, float b, float a) {
    if (!c.active) return;
    rlSetBlendMode(RL_BLEND_ADDITIVE);
    rlDisableDepthMask();
    render_glow_disc_3d(P(c, fwd * c.scale, up * c.scale, side * c.scale), radius * c.scale, r, g, b, a, 14);
    rlEnableDepthMask();
    rlSetBlendMode(RL_BLEND_ALPHA);
}

// Cilindro vertical aproximado por N caixas giradas - nao existe primitivo de cilindro em
// render_primitives (base_exterior.cpp tem um local, mas privado). 4 caixas a 45 graus dao uma
// silhueta octogonal, suficiente na escala de um modulo.
void cylinder(const Ctx& c, float fwd, float up, float side, float radius, float height,
              float r, float g, float b) {
    for (int i = 0; i < 2; ++i) {
        float extra = (float)i * (kPi * 0.25f);
        render_box_oriented_3d(P(c, fwd * c.scale, up * c.scale, side * c.scale),
                               radius * 2.0f * c.scale, height * c.scale, radius * 2.0f * c.scale,
                               c.yaw + extra, r * c.sh.r, g * c.sh.g, b * c.sh.b, 1.0f);
    }
}

// ---------------------------------------------------------------- PAINEL SOLAR
// Duas mesas fotovoltaicas INCLINADAS sobre um mastro central, com travessas claras marcando as
// celulas. A inclinacao e' o que identifica um painel solar - era exatamente o que faltava.
void model_solar(const Ctx& c) {
    const float pitch = -0.42f;   // ~24 graus, borda de tras levantada
    box(c, 0.0f, 0.10f, 0.0f, 0.34f, 0.20f, 0.34f, 0.30f, 0.32f, 0.36f);            // base
    box(c, 0.0f, 0.34f, 0.0f, 0.10f, 0.46f, 0.10f, 0.42f, 0.44f, 0.48f);            // mastro
    for (float sgn : {-1.0f, 1.0f}) {
        // Mesa: quase plana, larga, inclinada.
        tilted(c, 0.0f, 0.60f, 0.36f * sgn, 0.62f, 0.045f, 0.72f, pitch, 0.10f, 0.16f, 0.42f);
        // Travessas de celula (3 por mesa) - mais claras, ligeiramente acima da mesa.
        for (int k = -1; k <= 1; ++k) {
            tilted(c, (float)k * 0.22f, 0.60f + 0.030f + (float)k * 0.09f, 0.36f * sgn,
                   0.60f, 0.012f, 0.055f, pitch, 0.62f, 0.68f, 0.82f);
        }
        // Moldura da borda de baixo.
        tilted(c, -0.33f, 0.60f - 0.135f, 0.36f * sgn, 0.62f, 0.05f, 0.06f, pitch, 0.52f, 0.55f, 0.60f);
        // Braco de sustentacao.
        box(c, -0.16f, 0.44f, 0.30f * sgn, 0.07f, 0.28f, 0.07f, 0.38f, 0.40f, 0.44f);
    }
    // Piloto de operacao na base.
    emit(c, 0.16f, 0.22f, 0.0f, 0.05f, 0.05f, 0.05f, 0.35f, 0.95f, 1.0f);
}

// ---------------------------------------------------------------- GERADOR DE ENERGIA
// Bloco de maquina com PILHA DE ALETAS de radiador, escapamento e visor de arco eletrico pulsando.
void model_generator(const Ctx& c) {
    box(c, 0.0f, 0.06f, 0.0f, 0.80f, 0.12f, 0.80f, 0.26f, 0.27f, 0.30f);            // laje
    box(c, -0.06f, 0.42f, 0.0f, 0.62f, 0.60f, 0.56f, 0.44f, 0.40f, 0.22f);          // corpo
    // Aletas de radiador inclinadas na traseira - a assinatura visual de um gerador.
    for (int k = 0; k < 5; ++k) {
        tilted(c, -0.34f, 0.30f + (float)k * 0.13f, 0.0f, 0.52f, 0.03f, 0.26f, -0.30f,
               0.58f, 0.60f, 0.64f);
    }
    // Escapamento: tubo + coifa.
    cylinder(c, 0.20f, 0.86f, 0.20f, 0.07f, 0.34f, 0.30f, 0.31f, 0.34f);
    box(c, 0.20f, 1.04f, 0.20f, 0.20f, 0.06f, 0.20f, 0.22f, 0.23f, 0.26f);
    // Visor do arco: pulsa forte - le como motor girando.
    float arc = 0.55f + 0.45f * std::sin(c.t * 7.0f);
    emit(c, 0.28f, 0.44f, 0.0f, 0.06f, 0.24f, 0.30f, 0.35f * arc + 0.4f, 0.85f * arc, 1.0f * arc);
    glow(c, 0.34f, 0.44f, 0.0f, 0.22f, 0.5f, 0.85f, 1.0f, 0.30f * arc);
}

// ---------------------------------------------------------------- EXTRATOR / PURIFICADOR DE AGUA
// Torre de bomba com haste que ENTRA NO CHAO, tanque cilindrico ao lado e visor de nivel.
void model_water(const Ctx& c) {
    box(c, 0.0f, 0.06f, 0.0f, 0.78f, 0.12f, 0.78f, 0.26f, 0.28f, 0.32f);
    // Torre da bomba.
    box(c, 0.16f, 0.52f, -0.16f, 0.26f, 0.80f, 0.26f, 0.40f, 0.44f, 0.50f);
    box(c, 0.16f, 0.96f, -0.16f, 0.34f, 0.10f, 0.34f, 0.30f, 0.33f, 0.38f);
    // Haste que desce pro solo (sobe e desce devagar: bomba trabalhando).
    float stroke = c.active ? std::sin(c.t * 2.2f) * 0.10f : 0.0f;
    box(c, 0.16f, 0.30f + stroke, -0.16f, 0.08f, 0.66f, 0.08f, 0.62f, 0.66f, 0.72f);
    // Tanque cilindrico com faixa de nivel azul.
    cylinder(c, -0.20f, 0.44f, 0.20f, 0.24f, 0.64f, 0.52f, 0.56f, 0.62f);
    emit(c, -0.20f, 0.30f, 0.20f, 0.36f, 0.16f, 0.36f, 0.20f, 0.55f, 0.95f);
    box(c, -0.20f, 0.80f, 0.20f, 0.30f, 0.08f, 0.30f, 0.34f, 0.37f, 0.42f);
    // Tubo ligando torre e tanque.
    box(c, 0.0f, 0.72f, 0.02f, 0.50f, 0.08f, 0.08f, 0.46f, 0.50f, 0.56f);
    glow(c, -0.20f, 0.30f, 0.20f, 0.26f, 0.25f, 0.6f, 1.0f, 0.20f);
}

// ---------------------------------------------------------------- GERADOR DE OXIGENIO
// Tres cilindros de gas verticais com valvulas em cima e uma grade de ventilacao na frente.
void model_oxygen(const Ctx& c) {
    box(c, 0.0f, 0.06f, 0.0f, 0.78f, 0.12f, 0.78f, 0.24f, 0.28f, 0.28f);
    const float ox[3] = { -0.18f, 0.02f, 0.22f };
    const float oz[3] = {  0.18f, -0.14f, 0.16f };
    for (int i = 0; i < 3; ++i) {
        cylinder(c, ox[i], 0.52f, oz[i], 0.16f, 0.80f, 0.78f, 0.82f, 0.86f);
        box(c, ox[i], 0.94f, oz[i], 0.14f, 0.10f, 0.14f, 0.34f, 0.38f, 0.40f);      // valvula
        box(c, ox[i], 0.62f, oz[i], 0.34f, 0.05f, 0.34f, 0.30f, 0.60f, 0.45f);      // cinta verde
    }
    // Grade de ventilacao (lamelas) na face da frente.
    for (int k = 0; k < 4; ++k) {
        box(c, 0.36f, 0.20f + (float)k * 0.10f, 0.0f, 0.06f, 0.05f, 0.44f, 0.36f, 0.40f, 0.42f);
    }
    // Piloto verde + brilho suave: O2 fluindo.
    emit(c, 0.30f, 0.66f, -0.28f, 0.06f, 0.06f, 0.06f, 0.30f, 1.0f, 0.55f);
    glow(c, 0.0f, 0.14f, 0.0f, 0.40f, 0.3f, 1.0f, 0.6f, 0.14f);
}

// ---------------------------------------------------------------- ESTUFA
// Abobada de VIDRO sobre canteiro, com plantas visiveis dentro e lampadas de cultivo magenta.
void model_greenhouse(const Ctx& c) {
    box(c, 0.0f, 0.08f, 0.0f, 0.86f, 0.16f, 0.86f, 0.32f, 0.26f, 0.20f);            // canteiro
    box(c, 0.0f, 0.19f, 0.0f, 0.74f, 0.08f, 0.74f, 0.24f, 0.16f, 0.10f);            // terra
    // Plantas: 5 tufos deterministicos (posicao por indice, nao aleatoria por frame).
    for (int i = 0; i < 5; ++i) {
        float a = (float)i * 1.2566f;
        float rr = 0.22f;
        float h = 0.16f + 0.06f * std::sin((float)i * 2.1f);
        box(c, std::cos(a) * rr, 0.26f + h * 0.5f, std::sin(a) * rr, 0.07f, h, 0.07f, 0.20f, 0.55f, 0.22f);
        box(c, std::cos(a) * rr, 0.26f + h, std::sin(a) * rr, 0.20f, 0.05f, 0.20f, 0.28f, 0.72f, 0.30f);
    }
    // Abobada de vidro: 4 paineis inclinados formando um telhado de duas aguas + cumeeira.
    for (float sgn : {-1.0f, 1.0f}) {
        tilted(c, 0.0f, 0.62f, 0.24f * sgn, 0.86f, 0.035f, 0.52f, 0.0f, 0.62f, 0.82f, 0.92f, 0.34f);
        render_box_tilted_3d(P(c, 0.0f, (0.52f) * c.scale, 0.42f * sgn * c.scale),
                             0.86f * c.scale, 0.035f * c.scale, 0.34f * c.scale,
                             c.yaw + (sgn > 0.0f ? 0.0f : kPi), -0.9f,
                             0.60f * c.sh.r, 0.80f * c.sh.g, 0.90f * c.sh.b, 0.34f);
    }
    box(c, 0.0f, 0.70f, 0.0f, 0.88f, 0.05f, 0.06f, 0.50f, 0.54f, 0.58f);            // cumeeira
    for (float sgn : {-1.0f, 1.0f}) {                                                // montantes
        box(c, 0.40f * sgn, 0.40f, 0.0f, 0.05f, 0.44f, 0.86f, 0.46f, 0.50f, 0.54f);
    }
    // Lampada de cultivo: magenta, a leitura instantanea de estufa.
    emit(c, 0.0f, 0.62f, 0.0f, 0.40f, 0.04f, 0.10f, 1.0f, 0.40f, 0.85f);
    glow(c, 0.0f, 0.24f, 0.0f, 0.44f, 1.0f, 0.35f, 0.8f, 0.20f);
}

// ---------------------------------------------------------------- OFICINA
// Galpao com telhado de duas aguas, porta de enrolar e bancada com ferramentas na frente.
void model_workshop(const Ctx& c) {
    box(c, 0.0f, 0.06f, 0.0f, 0.86f, 0.12f, 0.86f, 0.28f, 0.28f, 0.30f);
    box(c, 0.0f, 0.42f, 0.0f, 0.76f, 0.60f, 0.72f, 0.48f, 0.44f, 0.36f);            // corpo
    // Telhado de duas aguas.
    for (float sgn : {-1.0f, 1.0f}) {
        render_box_tilted_3d(P(c, 0.0f, 0.80f * c.scale, 0.22f * sgn * c.scale),
                             0.88f * c.scale, 0.05f * c.scale, 0.50f * c.scale,
                             c.yaw + (sgn > 0.0f ? 0.0f : kPi), -0.55f,
                             0.40f * c.sh.r, 0.36f * c.sh.g, 0.30f * c.sh.b, 1.0f);
    }
    box(c, 0.0f, 0.92f, 0.0f, 0.90f, 0.05f, 0.06f, 0.34f, 0.32f, 0.28f);
    // Porta de enrolar (lamelas) na frente.
    for (int k = 0; k < 5; ++k) {
        box(c, 0.39f, 0.18f + (float)k * 0.11f, 0.0f, 0.04f, 0.09f, 0.44f, 0.62f, 0.64f, 0.66f);
    }
    // Bancada lateral + ferramentas.
    box(c, 0.10f, 0.30f, 0.46f, 0.44f, 0.06f, 0.22f, 0.42f, 0.30f, 0.18f);
    box(c, 0.20f, 0.38f, 0.46f, 0.08f, 0.14f, 0.08f, 0.70f, 0.72f, 0.76f);
    box(c, 0.02f, 0.36f, 0.46f, 0.16f, 0.10f, 0.10f, 0.60f, 0.45f, 0.20f);
    // Faisca de solda intermitente - o sinal de "oficina trabalhando".
    float weld = std::sin(c.t * 11.0f);
    if (weld > 0.55f) {
        emit(c, 0.20f, 0.46f, 0.46f, 0.07f, 0.07f, 0.07f, 1.0f, 0.95f, 0.80f);
        glow(c, 0.20f, 0.46f, 0.46f, 0.16f, 1.0f, 0.9f, 0.7f, 0.35f);
    }
}

// ---------------------------------------------------------------- FABRICA DE CO2
// Bloco industrial com DUAS CHAMINES e pluma de gas subindo.
void model_co2(const Ctx& c) {
    box(c, 0.0f, 0.06f, 0.0f, 0.84f, 0.12f, 0.84f, 0.24f, 0.24f, 0.26f);
    box(c, -0.04f, 0.40f, 0.0f, 0.66f, 0.56f, 0.66f, 0.40f, 0.34f, 0.30f);
    // Reator cilindrico ao lado.
    cylinder(c, 0.24f, 0.50f, 0.26f, 0.18f, 0.72f, 0.52f, 0.46f, 0.38f);
    // Chamines com aba no topo.
    for (float sgn : {-1.0f, 1.0f}) {
        cylinder(c, -0.16f, 0.86f, 0.22f * sgn, 0.10f, 0.52f, 0.34f, 0.30f, 0.28f);
        box(c, -0.16f, 1.14f, 0.22f * sgn, 0.26f, 0.06f, 0.26f, 0.26f, 0.23f, 0.21f);
        // Pluma: 3 baforadas subindo em ciclo, escurecendo. Aditivo suave.
        if (c.active) {
            rlSetBlendMode(RL_BLEND_ADDITIVE);
            rlDisableDepthMask();
            for (int k = 0; k < 3; ++k) {
                float ph = std::fmod(c.t * 0.35f + (float)k * 0.333f, 1.0f);
                float yy = 1.20f + ph * 0.80f;
                float rr = (0.12f + ph * 0.20f);
                float aa = (1.0f - ph) * 0.16f;
                render_glow_disc_3d(P(c, -0.16f * c.scale, yy * c.scale, 0.22f * sgn * c.scale),
                                    rr * c.scale, 0.75f, 0.72f, 0.66f, aa, 10);
            }
            rlEnableDepthMask();
            rlSetBlendMode(RL_BLEND_ALPHA);
        }
    }
    // Tubulacao e visor quente.
    box(c, 0.10f, 0.72f, 0.0f, 0.44f, 0.08f, 0.08f, 0.46f, 0.42f, 0.36f);
    emit(c, 0.32f, 0.38f, 0.0f, 0.06f, 0.18f, 0.24f, 1.0f, 0.55f, 0.20f);
}

// ---------------------------------------------------------------- HABITAT
// Modulo habitacional: cilindro deitado sobre bercos, com escotilha, vigias e antena.
void model_habitat(const Ctx& c) {
    box(c, 0.0f, 0.06f, 0.0f, 0.86f, 0.12f, 0.86f, 0.26f, 0.27f, 0.30f);
    // Casco: caixa larga + cantos chanfrados (2 caixas giradas) = leitura de cilindro deitado.
    box(c, 0.0f, 0.48f, 0.0f, 0.80f, 0.56f, 0.74f, 0.80f, 0.82f, 0.86f);
    render_box_tilted_3d(P(c, 0.0f, 0.76f * c.scale, 0.0f),
                         0.68f * c.scale, 0.22f * c.scale, 0.68f * c.scale, c.yaw + kPi * 0.25f, 0.0f,
                         0.74f * c.sh.r, 0.76f * c.sh.g, 0.80f * c.sh.b, 1.0f);
    // Bercos.
    for (float sgn : {-1.0f, 1.0f}) {
        box(c, 0.26f * sgn, 0.18f, 0.0f, 0.14f, 0.24f, 0.80f, 0.34f, 0.36f, 0.40f);
    }
    // Escotilha de entrada na frente + vigias nas laterais.
    box(c, 0.41f, 0.42f, 0.0f, 0.05f, 0.38f, 0.38f, 0.42f, 0.45f, 0.50f);
    emit(c, 0.44f, 0.42f, 0.0f, 0.03f, 0.22f, 0.22f, 0.35f, 0.75f, 0.95f);
    for (float sgn : {-1.0f, 1.0f}) {
        emit(c, -0.10f, 0.52f, 0.38f * sgn, 0.20f, 0.16f, 0.04f, 0.95f, 0.85f, 0.55f);
    }
    // Antena curta.
    box(c, -0.28f, 0.94f, 0.24f, 0.04f, 0.34f, 0.04f, 0.50f, 0.52f, 0.56f);
    emit(c, -0.28f, 1.13f, 0.24f, 0.05f, 0.05f, 0.05f, 1.0f, 0.35f, 0.30f);
    glow(c, 0.44f, 0.42f, 0.0f, 0.20f, 0.4f, 0.8f, 1.0f, 0.16f);
}

// ---------------------------------------------------------------- TERRAFORMADOR (BEACON)
// Mastro alto com aneis suspensos girando e um nucleo emissivo no topo - o unico modulo que precisa
// ler como "aparelho que muda o planeta", entao e' o mais alto e o unico com anel animado.
void model_terraformer(const Ctx& c) {
    box(c, 0.0f, 0.08f, 0.0f, 0.88f, 0.16f, 0.88f, 0.24f, 0.24f, 0.28f);
    // Tres contrafortes.
    for (int i = 0; i < 3; ++i) {
        float a = (float)i * 2.0944f;
        box(c, std::cos(a) * 0.30f, 0.36f, std::sin(a) * 0.30f, 0.10f, 0.48f, 0.10f, 0.38f, 0.40f, 0.46f);
    }
    cylinder(c, 0.0f, 0.90f, 0.0f, 0.13f, 1.50f, 0.46f, 0.48f, 0.54f);              // mastro
    // Aneis girando em alturas diferentes (giro = yaw somado ao tempo).
    if (true) {
        for (int i = 0; i < 3; ++i) {
            float spin = c.active ? c.t * (0.6f + 0.25f * (float)i) : 0.0f;
            float yy = 1.05f + (float)i * 0.34f;
            float rr = 0.42f - (float)i * 0.08f;
            render_box_tilted_3d(P(c, 0.0f, yy * c.scale, 0.0f),
                                 rr * 2.0f * c.scale, 0.05f * c.scale, 0.09f * c.scale,
                                 c.yaw + spin, 0.0f,
                                 0.70f * c.sh.r, 0.50f * c.sh.g, 0.90f * c.sh.b, 1.0f);
            render_box_tilted_3d(P(c, 0.0f, yy * c.scale, 0.0f),
                                 0.09f * c.scale, 0.05f * c.scale, rr * 2.0f * c.scale,
                                 c.yaw + spin, 0.0f,
                                 0.70f * c.sh.r, 0.50f * c.sh.g, 0.90f * c.sh.b, 1.0f);
        }
    }
    // Nucleo pulsante no topo.
    float p = 0.65f + 0.35f * std::sin(c.t * 2.4f);
    if (c.active) {
        render_sphere_3d(c.x, c.y + 1.80f * c.scale, c.z, 0.17f * c.scale,
                         0.85f * p, 0.55f * p, 1.0f * p, 1.0f);
    } else {
        render_sphere_3d(c.x, c.y + 1.80f * c.scale, c.z, 0.17f * c.scale, 0.22f, 0.18f, 0.26f, 1.0f);
    }
    glow(c, 0.0f, 1.80f, 0.0f, 0.52f, 0.8f, 0.45f, 1.0f, 0.30f * p);
    // Feixe curto pro ceu.
    if (c.active) {
        rlSetBlendMode(RL_BLEND_ADDITIVE);
        rlDisableDepthMask();
        render_beam_3d({c.x, c.y + 1.90f * c.scale, c.z}, {c.x, c.y + 4.20f * c.scale, c.z},
                       0.10f * c.scale, 0.75f, 0.50f, 1.0f, 0.22f * p);
        rlEnableDepthMask();
        rlSetBlendMode(RL_BLEND_ALPHA);
    }
}

// Andaime de obra: o que aparece no slot ENQUANTO o modulo esta sendo construido. Antes o slot
// ficava visualmente vazio durante os 15-120s da obra e nada indicava que algo estava acontecendo
// ali (o jogador so' via o toast do menu).
void model_scaffold(const Ctx& c, float progress) {
    float h = 0.30f + progress * 0.80f;
    box(c, 0.0f, 0.05f, 0.0f, 0.80f, 0.10f, 0.80f, 0.30f, 0.28f, 0.24f);
    for (float sx : {-1.0f, 1.0f}) for (float sz : {-1.0f, 1.0f}) {
        box(c, 0.34f * sz, h * 0.5f, 0.34f * sx, 0.06f, h, 0.06f, 0.70f, 0.58f, 0.20f);
    }
    box(c, 0.0f, h, 0.0f, 0.76f, 0.05f, 0.76f, 0.72f, 0.60f, 0.22f);
    // Faixa de progresso na base + luz de obra piscando.
    box(c, 0.0f, 0.13f, 0.0f, 0.72f * progress, 0.05f, 0.20f, 0.95f, 0.75f, 0.20f);
    if (std::sin(c.t * 4.0f) > 0.0f) {
        emit(c, 0.0f, h + 0.10f, 0.0f, 0.08f, 0.08f, 0.08f, 1.0f, 0.65f, 0.15f);
        glow(c, 0.0f, h + 0.10f, 0.0f, 0.22f, 1.0f, 0.6f, 0.15f, 0.30f);
    }
}

void draw_one(Block type, int tx, int tz, bool upgraded, bool active, float progress, bool building) {
    if (!g_world) return;
    float wx = tile_center(tx), wz = tile_center(tz);
    float base_y = stack_top_height_at(*g_world, tx, tz);

    Ctx c{};
    c.x = wx; c.y = base_y; c.z = wz;
    // Aponta pro centro da base: a fileira de modulos no anel fica "olhando pra dentro", o que le
    // como instalacao organizada em vez de pecas jogadas em angulos aleatorios.
    c.yaw = std::atan2((float)g_base_x - wx, (float)g_base_y - wz);
    c.sh = module_shade(wx, wz, 0.34f);
    c.t = g_day_time;
    c.scale = upgraded ? 1.16f : 1.0f;   // aprimorado e' visivelmente maior
    c.active = active;

    if (building) { model_scaffold(c, progress); return; }

    switch (type) {
        case Block::SolarPanel:        model_solar(c); break;
        case Block::EnergyGenerator:   model_generator(c); break;
        case Block::WaterExtractor:    model_water(c); break;
        case Block::OxygenGenerator:   model_oxygen(c); break;
        case Block::Greenhouse:        model_greenhouse(c); break;
        case Block::Workshop:          model_workshop(c); break;
        case Block::CO2Factory:        model_co2(c); break;
        case Block::Habitat:           model_habitat(c); break;
        case Block::TerraformerBeacon: model_terraformer(c); break;
        default: break;
    }

    // Marca de aprimorado: anel dourado na base. Antes o upgrade (tecla R) nao tinha NENHUMA
    // representacao no mundo - o jogador pagava e nada mudava na tela.
    if (upgraded) {
        rlSetBlendMode(RL_BLEND_ADDITIVE);
        rlDisableDepthMask();
        render_glow_disc_3d({wx, base_y + 0.03f, wz}, 0.58f, 1.0f, 0.82f, 0.30f, 0.22f, 16);
        rlEnableDepthMask();
        rlSetBlendMode(RL_BLEND_ALPHA);
    }
}

} // namespace

void render_module_models() {
    if (!g_world) return;
    rlSetTexture(rlGetTextureIdDefault());   // NAO rlSetTexture(0): pra id 0 o rlgl nao troca nada

    // Corte por distancia: mesmo raciocinio de render_base_exterior - alem do horizonte de terreno
    // nao ha mundo pra esconder o modelo, e ele apareceria flutuando na neblina.
    float cull = std::min(190.0f, g_frame_terrain_horizon);

    // ITERA OS TILES DO MUNDO, nao g_modules. Isso importa: g_modules e' um cache reconstruivel
    // (rebuild_modules_from_world no load) e ui_menu.cpp o LIMPA depois de spawn_player_new_game.
    // Desenhando a partir dele, qualquer tile de modulo sem entrada correspondente ficaria
    // INVISIVEL - antes o cubo vinha do tile e sempre aparecia, entao trocar a fonte por g_modules
    // seria uma regressao silenciosa. O tile e' a fonte da verdade de "existe um modulo aqui";
    // g_modules serve so' pros extras (aprimorado, status).
    Vec2 rp = get_player_render_pos();
    int px = world_to_tile(rp.x), pz = world_to_tile(rp.y);
    int rad = (int)std::min(cull, 72.0f);   // modulos ficam perto da base ou onde o jogador colocou
    for (int tz = pz - rad; tz <= pz + rad; ++tz) {
        for (int tx = px - rad; tx <= px + rad; ++tx) {
            if (!g_world->in_bounds(tx, tz)) continue;
            Block b = g_world->get(tx, tz);
            if (!is_module(b)) continue;
            float ddx = (float)tx - rp.x, ddz = (float)tz - rp.y;
            if (ddx * ddx + ddz * ddz > cull * cull) continue;
            // Extras vindos de g_modules quando existe entrada (busca linear: o vetor tem ~15
            // itens, e so' roda pros poucos tiles que sao modulo).
            bool upgraded = false, active = true;
            for (const Module& m : g_modules) {
                if (m.x != tx || m.y != tz) continue;
                upgraded = m.upgraded;
                active = (m.status != ModuleStatus::NoPower && m.status != ModuleStatus::Damaged);
                break;
            }
            draw_one(b, tx, tz, upgraded, active, 1.0f, false);
        }
    }

    // Obras em andamento: andaime no slot, com a barra de progresso real. Estas SO' existem em
    // g_construction_queue (o tile do modulo ainda nao foi escrito no mundo), entao aqui a fonte
    // e' a fila mesmo.
    for (const ConstructionJob& job : g_construction_queue) {
        if (!job.active) continue;
        if (job.slot_index < 0 || job.slot_index >= (int)g_build_slots.size()) continue;
        const BuildSlotInfo& s = g_build_slots[job.slot_index];
        float ddx = (float)s.x - rp.x, ddz = (float)s.y - rp.y;
        if (ddx * ddx + ddz * ddz > cull * cull) continue;
        float prog = 1.0f - (job.time_remaining / std::max(0.01f, job.total_time));
        draw_one(job.module_type, s.x, s.y, false, true, std::clamp(prog, 0.0f, 1.0f), true);
    }

    rlSetTexture(rlGetTextureIdDefault());
}
