#include "base_exterior.h"

#include "raylib_platform.h"
#include "math_core.h"          // Vec3, kPi, clamp01, kHeightScale
#include "noise.h"              // lerp
#include "blocks.h"             // Block
#include "world.h"              // World, g_world, object_block_at
#include "camera.h"             // g_camera (neblina)
#include "player_physics.h"     // get_player_render_pos
#include "lighting.h"           // g_lighting, sample_lightmap, apply_color_grading
#include "render_primitives.h"  // g_frame_fog, render_porthole_3d, render_glow_disc_3d
#include "sky.h"                // compute_night_alpha (vigias acesas de noite)
#include "interiors.h"          // kInteriors (as escotilhas ficam onde as portas estao)
#include "game_state.h"         // kDayLength

#include <algorithm>
#include <cmath>

extern int   g_base_x;
extern int   g_base_y;
extern float g_day_time;

// ============= A TABELA =============
// Composicao vinda das fotos de referencia (a base real "Mars Base 1" em Gansu): um DOMO central
// grande como nucleo, modulos cilindricos de tamanhos diferentes em volta, ligados por
// corredores-tubo curtos, mais area tecnica (tanques, paineis solares, mastros). Volumes grandes e
// poucos, nao muitos comodinhos.
//
// door_deg casa com as alcovas de kInteriors: azimute medido de +Z (0) pra +X (90), o mesmo
// referencial de axis_deg. N = 180, L = 90, S = 0, O = 270.
static constexpr float kNoDoor = -1000.0f;

const ExtPiece kExterior[] = {
    // ---- NUCLEO: domo central grande ----
    { ExtShape::Drum,   0,   0,  10.0f, 4.0f, 0, 0,   0.85f, 10, kNoDoor, 0.00f },

    // ---- 4 MODULOS PRINCIPAIS: cilindros com topo em domo, alturas diferentes ----
    { ExtShape::Drum,   0, -26,   5.0f, 6.5f, 0, 0,   0.55f,  6,  180.0f, 0.05f },  // N controle (torre)
    { ExtShape::Drum,  26,   0,   5.0f, 4.5f, 0, 0,   0.60f,  6,   90.0f, 0.00f },  // L laboratorio
    { ExtShape::Drum,   0,  26,   5.0f, 4.5f, 0, 0,   0.60f,  6,    0.0f, 0.00f },  // S eclusa (entrada)
    { ExtShape::Drum, -26,   0,   5.0f, 4.5f, 0, 0,   0.60f,  6,  270.0f, 0.00f },  // O dormitorio

    // ---- 4 CORREDORES-TUBO: nucleo -> modulo ----
    { ExtShape::Tube,   0, -16,   1.9f, 2.6f, 12.0f, 180.0f, 0, 0, kNoDoor, 0.18f },
    { ExtShape::Tube,  16,   0,   1.9f, 2.6f, 12.0f,  90.0f, 0, 0, kNoDoor, 0.18f },
    { ExtShape::Tube,   0,  16,   1.9f, 2.6f, 12.0f,   0.0f, 0, 0, kNoDoor, 0.18f },
    { ExtShape::Tube, -16,   0,   1.9f, 2.6f, 12.0f, 270.0f, 0, 0, kNoDoor, 0.18f },

    // ---- 2 MODULOS TECNICOS DIAGONAIS (quebram a simetria em cruz) ----
    { ExtShape::Drum,  18, -18,   4.0f, 3.8f, 0, 0,   0.70f,  5,    0.0f, 0.02f },  // NE estufa
    { ExtShape::Drum, -18,  18,   4.0f, 3.8f, 0, 0,   0.70f,  5,  180.0f, 0.02f },  // SO oficina
    // Tubos ligando os diagonais aos tubos axiais.
    // centro 8 / comprimento 13 (era 9 / 10): span x 1.5..14.5, encostando no tubo axial (que ocupa
    // x -1.9..1.9) de um lado e no tambor diagonal (borda em 14) do outro. Com 9/10 o span era
    // 4..14 e sobrava um vao de 2.1 tiles: o tubo acabava NO AR, e como draw_tube nao tinha tampa
    // de ponta dava pra olhar dentro do cilindro aberto - era o "tunel que nao termina".
    { ExtShape::Tube,   8, -18,   1.6f, 2.4f, 13.0f,  90.0f, 0, 0, kNoDoor, 0.18f },
    { ExtShape::Tube,  -8,  18,   1.6f, 2.4f, 13.0f, 270.0f, 0, 0, kNoDoor, 0.18f },

    // ---- AREA TECNICA: tanques de pressao ----
    { ExtShape::Drum,  13,  13,   2.4f, 3.0f, 0, 0,   0.95f,  0, kNoDoor, 0.35f },
    { ExtShape::Drum,  16,  16,   2.4f, 3.0f, 0, 0,   0.95f,  0, kNoDoor, 0.35f },
    { ExtShape::Drum, -13, -13,   2.4f, 3.0f, 0, 0,   0.95f,  0, kNoDoor, 0.35f },
    { ExtShape::Drum, -16, -16,   2.4f, 3.0f, 0, 0,   0.95f,  0, kNoDoor, 0.35f },

    // ---- MASTROS / ANTENAS: silhueta ----
    { ExtShape::Mast,   9, -12,   0.45f, 11.0f, 0, 0, 0, 0, kNoDoor, 0.50f },
    { ExtShape::Mast, -12,  -9,   0.45f,  9.0f, 0, 0, 0, 0, kNoDoor, 0.50f },
    { ExtShape::Mast,  12,   9,   0.45f, 13.0f, 0, 0, 0, 0, kNoDoor, 0.50f },
    { ExtShape::Mast, -26, -20,   0.45f, 10.0f, 0, 0, 0, 0, kNoDoor, 0.50f },

    // ---- PAINEIS SOLARES: fazenda de energia, so' desenho (pernas finas nao viram casca) ----
    // OS 3 PAINEIS SOLARES DECORATIVOS FORAM REMOVIDOS. Eram geometria pura (14 tiles de
    // comprimento cada) que produzia ZERO energia - base_annex_contains ate os pula. O jogador
    // abria o menu, construia um Painel Solar e nao conseguia dizer o que havia mudado, porque a
    // base ja estava cercada de paineis: "esta confuso, pois ao redor da base ja tem paineis
    // solares! E o que muda quando construo um?".
    // Agora o UNICO painel solar que existe no mundo e' um que o jogador construiu, e ele aparece
    // num slot do anel que estava visivelmente vazio. Custo: a base perde 3 pecas de decoracao -
    // continua com tambores, domos, tubos, tanques, mastros e escotilhas.
};
const int kExteriorCount = (int)(sizeof(kExterior) / sizeof(kExterior[0]));

// axis_deg -> vetor unitario no plano. 0 = +Z, 90 = +X (mesma convencao de Player::rotation).
static void axis_vec(float deg, float& ox, float& oz) {
    float r = deg * (kPi / 180.0f);
    ox = std::sin(r);
    oz = std::cos(r);
}

// ============= COLISAO =============
void base_exterior_stamp(World& world, int cx, int cz, int pad_h) {
    // UNIAO, nao sobrescrita: onde duas pecas se cruzam vale a MAIS ALTA. Antes esta funcao esvaziava
    // a coluna e reescrevia, e como os tubos sao estampados depois dos modulos, cada tubo CAVAVA um
    // rego de 3 de altura dentro do modulo que ele encosta - um vao de 5.7 de profundidade debaixo da
    // cupula desenhada, aberto por cima, onde o jogador descia de jetpack. Medido: deficit 5.70 no
    // modulo norte. Uniao e' a regra certa: peca sobreposta nunca abaixa o que ja existe.
    auto column = [&](int tx, int tz, int layers) {
        if (!world.in_bounds(tx, tz)) return;
        if (layers <= 0) return;
        int have = world.stack_height_at(tx, tz);
        if (have >= layers) return;   // ja ha algo mais alto aqui - nao rebaixa
        world.set_height(tx, tz, (int16_t)pad_h);
        if (object_block_at(world, tx, tz) != Block::Air) world.set(tx, tz, Block::Air);
        world.set_ground(tx, tz, Block::BaseFloor);
        world.set(tx, tz, Block::BaseFloor);
        for (int l = have; l < layers; ++l) world.stack_push(tx, tz, Block::BaseShell);
    };

    for (int i = 0; i < kExteriorCount; ++i) {
        const ExtPiece& p = kExterior[i];
        // Painel solar nao ganha casca: sao pernas finas e um plano inclinado no ar. Uma casca ali
        // seria um cubo solido invisivel debaixo do painel - parede invisivel, exatamente o que o
        // jogador rejeitou. Da' pra andar e voar por baixo dele.
        if (p.shape == ExtShape::Solar) continue;

        if (p.shape == ExtShape::Drum || p.shape == ExtShape::Mast) {
            // Casca 0.5 tile MENOR que o desenho (ver a nota de margem em base_exterior.h).
            float rr = std::max(0.5f, p.radius - 0.5f);
            // A casca ACOMPANHA O DOMO DO TOPO, em degraus. Antes ela parava na altura do corpo e o
            // domo era so' desenho, sem colisao: pousando de jetpack em cima de um modulo, o jogador
            // atravessava o domo e ficava DENTRO dele, olhando a casca por dentro. Era o mesmo bug
            // "atravessa tetos" ressurgindo nas calotas - e como a camera ficava dentro de uma casca
            // que cobre a tela inteira, tambem derrubava o FPS pra ~11.
            // Perfil calculado com o raio DESENHADO (nao rr), pra o degrau casar com a curva visivel.
            float dome_h = p.radius * p.dome_ratio;
            int span = (int)rr + 1;
            for (int dz = -span; dz <= span; ++dz)
                for (int dx = -span; dx <= span; ++dx) {
                    float d2 = (float)(dx * dx + dz * dz);
                    if (d2 > rr * rr) continue;
                    float t = clamp01(std::sqrt(d2) / std::max(0.001f, p.radius));
                    float h = p.height + dome_h * std::sqrt(std::max(0.0f, 1.0f - t * t));
                    int layers = std::max(1, (int)std::lround(h));
                    column(cx + (int)std::lround(p.dx) + dx, cz + (int)std::lround(p.dz) + dz, layers);
                }
        } else {  // Tube
            float ax, az;
            axis_vec(p.axis_deg, ax, az);
            float px = -az, pz = ax;             // perpendicular
            float rr = std::max(0.5f, p.radius - 0.5f);
            // Altura da casca = TOPO DESENHADO do tubo (eixo em h*0.55, mais o raio), nao p.height.
            // Com p.height a casca ficava mais baixa que o cilindro desenhado, e andando em cima do
            // tubo o tronco do jogador atravessava o desenho.
            int layers = std::max(1, (int)std::lround(p.height * 0.55f + p.radius));
            int steps = (int)std::lround(p.len) + 1;
            int wspan = (int)rr;
            for (int s = 0; s <= steps; ++s) {
                float t = (float)s - p.len * 0.5f;
                for (int w = -wspan; w <= wspan; ++w) {
                    int tx = cx + (int)std::lround(p.dx + ax * t + px * (float)w);
                    int tz = cz + (int)std::lround(p.dz + az * t + pz * (float)w);
                    column(tx, tz, layers);
                }
            }
        }
    }

    // ---- PATAMAR DE PORTA (nao alcova) ----
    // O tile onde o jogador fica pra usar a porta ja esta FORA da casca (kInteriors poe a distancia
    // 6 de um modulo de raio 5), entao aqui nao se escava nada - so' se garante piso nivelado e
    // limpo nele e nos vizinhos, pra a aproximacao nao ter degrau nem pedra no caminho.
    //
    // Nao existe entalhe na fachada de proposito: um tile esvaziado na parede viraria um POCO do chao
    // ao topo do modulo (uma coluna tem UMA pilha que comeca no terreno - nao da' pra ter solido em
    // cima e vazio embaixo), e o jogador descia nele de jetpack pra dentro da parede desenhada.
    for (int i = 0; i < kInteriorCount; ++i) {
        const InteriorDef& d = kInteriors[i];
        if (d.door_facing_deg <= -900.0f) continue;   // ambiente interno puro: nao tem porta externa
        float ax, az;
        axis_vec(d.door_facing_deg, ax, az);
        float px = -az, pz = ax;
        for (int w = -1; w <= 1; ++w) {
            for (int fwd = 0; fwd <= 2; ++fwd) {
                int tx = cx + d.door_dx + (int)std::lround(px * (float)w + ax * (float)fwd);
                int tz = cz + d.door_dz + (int)std::lround(pz * (float)w + az * (float)fwd);
                if (!world.in_bounds(tx, tz)) continue;
                if (world.stack_height_at(tx, tz) > 0) continue;   // nunca fura a casca
                world.set_height(tx, tz, (int16_t)pad_h);
                if (object_block_at(world, tx, tz) != Block::Air) world.set(tx, tz, Block::Air);
                world.set_ground(tx, tz, Block::LandingPad);
                world.set(tx, tz, Block::LandingPad);
            }
        }
    }
}

// ============= DESENHO =============
namespace {

void fog(float wx, float wy, float wz, float& r, float& g, float& b) {
    if (!g_frame_fog.enabled) return;
    float dx = wx - g_camera.position.x, dy = wy - g_camera.position.y, dz = wz - g_camera.position.z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    float span = std::max(0.0001f, g_frame_fog.end - g_frame_fog.start);
    float f = clamp01((g_frame_fog.end - dist) / span);
    r = lerp(g_frame_fog.r, r, f);
    g = lerp(g_frame_fog.g, g, f);
    b = lerp(g_frame_fog.b, b, f);
}

void lit(float wx, float wz, float& r, float& g, float& b) {
    if (!g_lighting.enabled) return;
    float lr, lg, lb;
    sample_lightmap(wx, wz, lr, lg, lb);
    r *= lr; g *= lg; b *= lb;
    apply_color_grading(r, g, b);
}

// Anel de quads entre duas circunferencias horizontais - o tijolo de todo cilindro/domo aqui.
// `skip_lo/skip_hi` recortam um setor angular (usado pro vao da escotilha).
void ring(float cx, float cz, float y0, float y1, float r0, float r1, int seg,
          float r, float g, float b, float a, float shade0, float shade1,
          bool has_skip, float skip_lo, float skip_hi) {
    rlBegin(RL_QUADS);
    for (int i = 0; i < seg; ++i) {
        float u0 = (float)i / (float)seg * 2.0f * kPi;
        float u1 = (float)(i + 1) / (float)seg * 2.0f * kPi;
        if (has_skip) {
            float mid = (u0 + u1) * 0.5f;
            if (mid >= skip_lo && mid <= skip_hi) continue;
            if (mid + 2.0f * kPi >= skip_lo && mid + 2.0f * kPi <= skip_hi) continue;
        }
        float c0 = std::cos(u0), s0 = std::sin(u0);
        float c1 = std::cos(u1), s1 = std::sin(u1);
        // Sombreamento por azimute: mais claro no lado +X/-Z, mais escuro no oposto. Da' volume ao
        // cilindro sem luz de verdade - mesma ideia das 3 sombras por face de render_cube_3d.
        float az0 = 0.72f + 0.28f * (c0 * 0.6f - s0 * 0.4f + 1.0f) * 0.5f;
        float az1 = 0.72f + 0.28f * (c1 * 0.6f - s1 * 0.4f + 1.0f) * 0.5f;
        float ra = r * shade0 * az0, ga = g * shade0 * az0, ba = b * shade0 * az0;
        float rb = r * shade1 * az1, gb = g * shade1 * az1, bb = b * shade1 * az1;
        fog(cx + c0 * r0, y0, cz + s0 * r0, ra, ga, ba);
        fog(cx + c1 * r1, y1, cz + s1 * r1, rb, gb, bb);
        rlColor4f(ra, ga, ba, a);
        rlVertex3f(cx + c0 * r0, y0, cz + s0 * r0);
        rlVertex3f(cx + c1 * r0, y0, cz + s1 * r0);
        rlColor4f(rb, gb, bb, a);
        rlVertex3f(cx + c1 * r1, y1, cz + s1 * r1);
        rlVertex3f(cx + c0 * r1, y1, cz + s0 * r1);
    }
    rlEnd();
}

// Cilindro vertical liso com topo em domo.
//
// A PAREDE E' CONTINUA - nao existe mais recorte de vao. Antes eu abria um buraco no cilindro no
// azimute da porta e punha a escotilha no meio dele: o resultado era um disco cinza gigante
// FLUTUANDO num rasgo negro, sem parede em volta. A escotilha e' uma PORTA FECHADA aplicada sobre a
// parede (e' assim na foto de referencia: um portao redondo sobre casco branco liso), e a alcova de
// bloco na frente dela e' onde o jogador se posiciona. Quem atravessa e' a transicao, nao o desenho.
void draw_drum(float wx, float wz, float base_y, float radius, float body_h, float dome_ratio,
               float r, float g, float b, int seg) {
    // Corpo: gradiente vertical continuo em poucas faixas. Antes eram body_h/1.3 faixas, cada uma com
    // uma nervura saliente na junta - num modulo de 4.5 isso dava 4 aneis e o cilindro lia como
    // colmeia/fardo de palha, nao como casco. Agora sao 2 faixas e UMA nervura, discreta.
    const int bands = 2;
    for (int i = 0; i < bands; ++i) {
        float y0 = base_y + body_h * (float)i / (float)bands;
        float y1 = base_y + body_h * (float)(i + 1) / (float)bands;
        float sh0 = 0.86f + 0.14f * (float)i / (float)bands;
        float sh1 = 0.86f + 0.14f * (float)(i + 1) / (float)bands;
        ring(wx, wz, y0, y1, radius, radius, seg, r, g, b, 1.0f, sh0, sh1, false, 0, 0);
    }
    // Nervura unica na junta do meio, quase rente (0.05 em vez de 0.10).
    {
        float y = base_y + body_h * 0.5f;
        ring(wx, wz, y - 0.06f, y + 0.06f, radius + 0.05f, radius + 0.05f, seg,
             r * 0.80f, g * 0.82f, b * 0.86f, 1.0f, 1.0f, 1.0f, false, 0, 0);
    }
    // Cinta de base (a "sapata" que assenta no solo) - esconde a costura com o terreno.
    ring(wx, wz, base_y, base_y + 0.30f, radius + 0.16f, radius + 0.16f, seg,
         r * 0.55f, g * 0.57f, b * 0.62f, 1.0f, 0.9f, 1.0f, false, 0, 0);

    // Domo do topo, em faixas de latitude.
    float dome_h = radius * dome_ratio;
    if (dome_h > 0.05f) {
        const int lat = std::max(3, (int)(6.0f * dome_ratio) + 3);
        for (int i = 0; i < lat; ++i) {
            float t0 = (float)i / (float)lat, t1 = (float)(i + 1) / (float)lat;
            float p0 = t0 * (kPi * 0.5f), p1 = t1 * (kPi * 0.5f);
            float r0 = std::cos(p0) * radius, r1 = std::cos(p1) * radius;
            float y0 = base_y + body_h + std::sin(p0) * dome_h;
            float y1 = base_y + body_h + std::sin(p1) * dome_h;
            ring(wx, wz, y0, y1, r0, r1, seg, r, g, b, 1.0f,
                 0.94f + 0.06f * t0, 0.94f + 0.06f * t1, false, 0, 0);
        }
        // Tampa do apice (o ultimo anel fecha num raio pequeno, nao em zero).
        rlBegin(RL_QUADS);
        float ty = base_y + body_h + dome_h;
        float cr = r, cg = g, cb = b;
        fog(wx, ty, wz, cr, cg, cb);
        rlColor4f(cr, cg, cb, 1.0f);
        float tr = std::cos((float)(lat - 1) / (float)lat * kPi * 0.5f) * radius * 0.35f;
        rlVertex3f(wx - tr, ty, wz - tr); rlVertex3f(wx + tr, ty, wz - tr);
        rlVertex3f(wx + tr, ty, wz + tr); rlVertex3f(wx - tr, ty, wz + tr);
        rlEnd();
    }
}

// Cilindro horizontal (corredor). Aneis de reforco a cada ~2.5 de comprimento.
void draw_tube(float wx, float wz, float base_y, float radius, float h,
               float len, float axis_deg, float r, float g, float b) {
    float ax, az;
    axis_vec(axis_deg, ax, az);
    float px = -az, pz = ax;
    const int seg = 12;
    const int slices = std::max(2, (int)(len / 1.6f));
    float cy = base_y + h * 0.55f;   // eixo do tubo, um pouco acima do meio (parece apoiado)
    for (int s = 0; s < slices; ++s) {
        float t0 = -len * 0.5f + len * (float)s / (float)slices;
        float t1 = -len * 0.5f + len * (float)(s + 1) / (float)slices;
        bool rib = (s % 2) == 0;
        float rr = radius * (rib ? 1.10f : 1.0f);
        float shade = rib ? 0.80f : 1.0f;
        rlBegin(RL_QUADS);
        for (int i = 0; i < seg; ++i) {
            float u0 = (float)i / (float)seg * 2.0f * kPi;
            float u1 = (float)(i + 1) / (float)seg * 2.0f * kPi;
            // Secao circular no plano (perpendicular, vertical).
            float c0 = std::cos(u0), s0 = std::sin(u0);
            float c1 = std::cos(u1), s1 = std::sin(u1);
            float sh0 = (0.70f + 0.30f * clamp01(s0 * 0.5f + 0.6f)) * shade;
            float sh1 = (0.70f + 0.30f * clamp01(s1 * 0.5f + 0.6f)) * shade;
            float x00 = wx + ax * t0 + px * (c0 * rr), z00 = wz + az * t0 + pz * (c0 * rr);
            float x10 = wx + ax * t1 + px * (c0 * rr), z10 = wz + az * t1 + pz * (c0 * rr);
            float x11 = wx + ax * t1 + px * (c1 * rr), z11 = wz + az * t1 + pz * (c1 * rr);
            float x01 = wx + ax * t0 + px * (c1 * rr), z01 = wz + az * t0 + pz * (c1 * rr);
            float y0 = cy + s0 * rr, y1 = cy + s1 * rr;
            float ra = r * sh0, ga = g * sh0, ba = b * sh0;
            float rb = r * sh1, gb = g * sh1, bb = b * sh1;
            fog(x00, y0, z00, ra, ga, ba);
            fog(x01, y1, z01, rb, gb, bb);
            rlColor4f(ra, ga, ba, 1.0f);
            rlVertex3f(x00, y0, z00); rlVertex3f(x10, y0, z10);
            rlColor4f(rb, gb, bb, 1.0f);
            rlVertex3f(x11, y1, z11); rlVertex3f(x01, y1, z01);
        }
        rlEnd();
    }
    // TAMPAS DE PONTA: sem elas o cilindro e' um tubo ABERTO, e como o backface culling esta
    // desligado da' pra olhar dentro dele - era metade do bug "tunel que nao termina" (a outra metade
    // era o tubo diagonal nao alcancar o axial, ver kExterior). Uma tampa em leque nas duas pontas.
    for (int e = 0; e < 2; ++e) {
        float t = (e == 0) ? (-len * 0.5f) : (len * 0.5f);
        float ex = wx + ax * t, ez = wz + az * t;
        float cr = r * 0.62f, cg = g * 0.64f, cb = b * 0.66f;
        fog(ex, cy, ez, cr, cg, cb);
        rlBegin(RL_TRIANGLES);
        rlColor4f(cr, cg, cb, 1.0f);
        for (int i = 0; i < seg; ++i) {
            float u0 = (float)i / (float)seg * 2.0f * kPi;
            float u1 = (float)(i + 1) / (float)seg * 2.0f * kPi;
            rlVertex3f(ex, cy, ez);
            rlVertex3f(ex + px * (std::cos(u0) * radius), cy + std::sin(u0) * radius,
                       ez + pz * (std::cos(u0) * radius));
            rlVertex3f(ex + px * (std::cos(u1) * radius), cy + std::sin(u1) * radius,
                       ez + pz * (std::cos(u1) * radius));
        }
        rlEnd();
    }
}

void draw_mast(float wx, float wz, float base_y, float radius, float h,
               float r, float g, float b) {
    const int seg = 8;
    ring(wx, wz, base_y, base_y + h, radius, radius * 0.55f, seg, r, g, b, 1.0f, 0.85f, 1.0f, false, 0, 0);
    // Prato de antena no topo, inclinado - um quad simples le como parabolica a distancia.
    float ty = base_y + h;
    float cr = 0.82f, cg = 0.84f, cb = 0.88f;
    fog(wx, ty, wz, cr, cg, cb);
    rlBegin(RL_QUADS);
    rlColor4f(cr, cg, cb, 1.0f);
    rlVertex3f(wx - 1.0f, ty, wz - 0.3f);
    rlVertex3f(wx + 1.0f, ty, wz - 0.3f);
    rlVertex3f(wx + 0.8f, ty + 1.3f, wz + 0.5f);
    rlVertex3f(wx - 0.8f, ty + 1.3f, wz + 0.5f);
    rlEnd();
    // 3 estais.
    rlBegin(RL_LINES);
    rlColor4f(0.25f, 0.26f, 0.28f, 0.9f);
    for (int i = 0; i < 3; ++i) {
        float a = (float)i / 3.0f * 2.0f * kPi;
        rlVertex3f(wx, base_y + h * 0.75f, wz);
        rlVertex3f(wx + std::cos(a) * h * 0.35f, base_y, wz + std::sin(a) * h * 0.35f);
    }
    rlEnd();
}

// Fazenda solar: plano azul-escuro inclinado sobre pernas finas. Sem casca de colisao (ver
// base_exterior_stamp) - da' pra andar e voar por baixo.
void draw_solar(float wx, float wz, float base_y, float half_w, float h,
                float len, float axis_deg) {
    float ax, az;
    axis_vec(axis_deg, ax, az);
    float px = -az, pz = ax;
    const int panels = std::max(2, (int)(len / 4.0f));
    for (int i = 0; i < panels; ++i) {
        float t = -len * 0.5f + len * ((float)i + 0.5f) / (float)panels;
        float pw = len / (float)panels * 0.42f;
        float bx = wx + ax * t, bz = wz + az * t;
        // Painel inclinado: borda baixa no lado -perp, alta no +perp.
        float y_lo = base_y + h * 0.55f, y_hi = base_y + h;
        float pr = 0.16f, pg = 0.20f, pb = 0.42f;
        fog(bx, y_hi, bz, pr, pg, pb);
        rlBegin(RL_QUADS);
        rlColor4f(pr, pg, pb, 1.0f);
        rlVertex3f(bx - ax * pw + px * -half_w, y_lo, bz - az * pw + pz * -half_w);
        rlVertex3f(bx + ax * pw + px * -half_w, y_lo, bz + az * pw + pz * -half_w);
        rlVertex3f(bx + ax * pw + px *  half_w, y_hi, bz + az * pw + pz *  half_w);
        rlVertex3f(bx - ax * pw + px *  half_w, y_hi, bz - az * pw + pz *  half_w);
        rlEnd();
        // 2 pernas.
        for (float s : {-1.0f, 1.0f}) {
            float lx = bx + px * (half_w * 0.6f * s), lz = bz + pz * (half_w * 0.6f * s);
            float top = (s < 0.0f) ? y_lo : y_hi;
            ring(lx, lz, base_y, top, 0.10f, 0.10f, 6, 0.40f, 0.41f, 0.44f, 1.0f, 0.8f, 1.0f, false, 0, 0);
        }
    }
}

} // namespace

void render_base_exterior() {
    if (!g_world) return;

    Vec2 rp = get_player_render_pos();
    float bdx = rp.x - (float)g_base_x, bdz = rp.y - (float)g_base_y;
    // Corte de distancia: alem disso o terreno da base ja saiu por culling e o modelo leria como
    // "flutuando no nada" - o mesmo raciocinio do corte que a cupula antiga usava.
    //
    // O corte era 190 tiles FIXOS, mas o alcance do terreno (view_radius, main.cpp) e' DINAMICO:
    // 110 com a camera rente ao chao, ate 380 voando alto, ainda multiplicado por g_render_quality.
    // Com view_radius em 110 e a base a 160, o terreno parava antes dela e o modelo continuava sendo
    // desenhado - sem nada na frente para ocluir e 100% enevoado, virava manchas claras contra o CEU
    // por cima da crista do vulcao ("estou vendo a base do outro lado do vulcao pela fumaca").
    // Estrutura nunca pode ser desenhada mais longe do que o mundo que a esconde, entao o corte
    // agora e' o MENOR entre o teto proprio e o horizonte real do frame.
    float cut = std::min(190.0f, g_frame_terrain_horizon);
    if (bdx * bdx + bdz * bdz > cut * cut) return;
    // Dentro de um interior nao se desenha o exterior: as salas ficam a ~1200 tiles, entao isto e'
    // so' uma guarda de clareza (o corte acima ja resolveria).
    if (interior_at(rp.x, rp.y) >= 0) return;

    rlSetTexture(rlGetTextureIdDefault());   // NAO rlSetTexture(0): pra id 0 o rlgl nao troca nada

    const float cx = (float)g_base_x, cz = (float)g_base_y;
    const float base_y = (float)g_world->height_at(g_base_x, g_base_y) * kHeightScale;

    float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
    float night = compute_night_alpha(day_phase);

    for (int i = 0; i < kExteriorCount; ++i) {
        const ExtPiece& p = kExterior[i];
        float wx = cx + p.dx, wz = cz + p.dz;

        // CAMERA DENTRO DA PECA: nao desenha. A camera fica ~4.8 atras do jogador, entao andando
        // rente a um modulo ela entra no volume - e uma casca vista por dentro cobre a tela INTEIRA,
        // com todos os quads dela sobrepostos. Foi o que derrubou o FPS pra 11 nos screenshots. O
        // culling de oclusao de camera que ja existe (camera_occluder_alpha_for_tile) so' vale pros
        // blocos do loop de terreno, e a casca deste modelo e' invisivel - nao passa por lá.
        {
            float cdx = g_camera.position.x - wx, cdz = g_camera.position.z - wz;
            float top = base_y + p.height + p.radius * p.dome_ratio + 0.5f;
            bool inside_xz = (p.shape == ExtShape::Tube)
                ? false   // tubo e' fino e baixo; a camera raramente entra, e o teste orientado
                          // custaria mais que o desenho dele
                : (cdx * cdx + cdz * cdz) < (p.radius + 0.6f) * (p.radius + 0.6f);
            if (inside_xz && g_camera.position.y > base_y - 1.0f && g_camera.position.y < top) continue;
        }

        // ILUMINACAO POR PECA + PISO DE AUTO-ILUMINACAO.
        // Antes: UMA amostra do lightmap no centro da base, aplicada em tudo. Duas consequencias
        // ruins: (1) a instalacao inteira ficava de uma cor unica e chapada, sem nenhuma variacao de
        // luz entre um modulo e outro; (2) de noite o ambiente externo e' ambient_min = 0.06, entao a
        // casca branca 0.90 virava 0.054 - PRETO. Era o "a base fica estranha no escuro".
        // Agora amostra na posicao DA PECA, e mistura com um piso: uma instalacao habitada tem luz
        // propria e nao apaga junto com o sol. De dia o piso nao interfere (lightmap ~1 -> ~1).
        float lr = 1.0f, lg = 1.0f, lb = 1.0f;
        if (g_lighting.enabled) {
            sample_lightmap(wx, wz, lr, lg, lb);
            apply_color_grading(lr, lg, lb);
            const float kSelfLit = 0.42f;   // 0.42 + 0.58*0.06 = 0.46 de noite; 1.0 ao meio-dia
            lr = kSelfLit + (1.0f - kSelfLit) * lr;
            lg = kSelfLit + (1.0f - kSelfLit) * lg;
            lb = kSelfLit + (1.0f - kSelfLit) * lb;
        }
        // Casco: branco-osso, escurecendo com `tint` pras pecas tecnicas (tubos, tanques, mastros).
        float r = lerp(0.90f, 0.46f, p.tint) * lr;
        float g = lerp(0.90f, 0.48f, p.tint) * lg;
        float b = lerp(0.93f, 0.52f, p.tint) * lb;

        switch (p.shape) {
            case ExtShape::Drum: {
                int seg = std::max(12, (int)(p.radius * 3.2f));
                bool has_door = (p.door_deg > -900.0f);
                // Azimute da escotilha no referencial de `ring` (u medido de +X pra +Z). door_deg e'
                // medido de +Z pra +X, entao u = pi/2 - rad.
                float u = kPi * 0.5f - p.door_deg * (kPi / 180.0f);
                while (u < 0.0f) u += 2.0f * kPi;
                float half = std::atan2(1.9f, std::max(1.0f, p.radius));
                draw_drum(wx, wz, base_y, p.radius, p.height, p.dome_ratio, r, g, b, seg);
                // Vigias em volta do corpo, na altura dos olhos. Acesas de noite - e' o que faz a
                // base parecer habitada de longe.
                // LOD: sao a peca mais cara do modelo (~32 quads cada, e ha ~40 delas). Alem de 75
                // tiles cada uma ocupa poucos pixels - some sem perda visivel.
                float pdx = rp.x - wx, pdz = rp.y - wz;
                bool near_enough = (pdx * pdx + pdz * pdz) < 75.0f * 75.0f;
                for (int k = 0; near_enough && k < p.portholes; ++k) {
                    float pu = (float)k / (float)std::max(1, p.portholes) * 2.0f * kPi + 0.2f;
                    if (has_door && std::fabs(std::atan2(std::sin(pu - u), std::cos(pu - u))) < half + 0.25f)
                        continue;
                    Vec3 c{wx + std::cos(pu) * (p.radius + 0.06f), base_y + p.height * 0.55f,
                           wz + std::sin(pu) * (p.radius + 0.06f)};
                    render_porthole_3d(c, pu, 0.42f, 0.14f,
                                       0.46f, 0.66f, 0.80f, 0.66f,
                                       0.52f, 0.55f, 0.60f, 12, 8, night * 0.85f);
                }
                // ESCOTILHA: portao redondo FECHADO aplicado sobre a parede continua, como na foto de
                // referencia. Antes ela flutuava no meio de um rasgo aberto no cilindro.
                // Duas camadas: aro/moldura larga por tras, folha mais escura na frente - o degrau de
                // profundidade e' o que da relevo sem luz de verdade.
                if (has_door) {
                    float px = std::cos(u), pz = std::sin(u);
                    // Portao pela funcao COMPARTILHADA (render_primitives.h). O interior chama a
                    // mesma - era o pedido "a porta de saida nao parece com a porta de entrada".
                    // +2.05, igual ao lado de dentro: com 1.45 o terco de baixo do portao ficava
                    // enterrado no chao. Mesma altura nos dois lados - a porta e' a mesma.
                    render_airlock_hatch_3d({wx + px * (p.radius + 0.05f), base_y + 2.05f,
                                             wz + pz * (p.radius + 0.05f)}, u, 1.55f, night * 0.35f);
                    // Soleira: uma faixa clara no chao na frente da porta, marcando a alcova. E' o
                    // sinal visual de "entra aqui" a distancia.
                    float tx0 = wx + px * (p.radius - 0.2f), tz0 = wz + pz * (p.radius - 0.2f);
                    float ox = -pz, oz = px;
                    float sr = 0.72f, sg = 0.60f, sb = 0.18f;
                    fog(tx0, base_y + 0.03f, tz0, sr, sg, sb);
                    rlBegin(RL_QUADS);
                    rlColor4f(sr, sg, sb, 0.9f);
                    rlVertex3f(tx0 + ox * 1.6f, base_y + 0.03f, tz0 + oz * 1.6f);
                    rlVertex3f(tx0 - ox * 1.6f, base_y + 0.03f, tz0 - oz * 1.6f);
                    rlVertex3f(tx0 - ox * 1.6f + px * 2.4f, base_y + 0.03f, tz0 - oz * 1.6f + pz * 2.4f);
                    rlVertex3f(tx0 + ox * 1.6f + px * 2.4f, base_y + 0.03f, tz0 + oz * 1.6f + pz * 2.4f);
                    rlEnd();
                }
                break;
            }
            case ExtShape::Tube:
                draw_tube(wx, wz, base_y, p.radius, p.height, p.len, p.axis_deg, r, g, b);
                break;
            case ExtShape::Mast:
                draw_mast(wx, wz, base_y, p.radius, p.height, r, g, b);
                break;
            case ExtShape::Solar:
                draw_solar(wx, wz, base_y, p.radius, p.height, p.len, p.axis_deg);
                break;
        }
    }

    // Luzes de baliza vermelhas no topo dos mastros - aditivas, depois de tudo solido.
    rlSetBlendMode(RL_BLEND_ADDITIVE);
    rlDisableDepthMask();
    float blink = 0.45f + 0.55f * std::fabs(std::sin(g_day_time * 1.7f));
    for (int i = 0; i < kExteriorCount; ++i) {
        const ExtPiece& p = kExterior[i];
        if (p.shape != ExtShape::Mast) continue;
        render_glow_disc_3d({cx + p.dx, base_y + p.height + 1.45f, cz + p.dz}, 0.55f,
                            1.0f, 0.22f, 0.18f, blink * 0.75f, 10);
    }
    rlEnableDepthMask();
    rlSetBlendMode(RL_BLEND_ALPHA);
    rlSetTexture(rlGetTextureIdDefault());
}

// Ver comentario da declaracao em base_exterior.h. Posicoes casadas com os mastros e o nucleo de
// kExterior, pra a luz sair de onde ha um poste desenhado.
const BaseFlood kBaseFloods[] = {
    {   0.0f,   0.0f, 13.0f, 1.00f, 0.95f, 0.86f, 26.0f, 0.55f },  // nucleo: ilumina o miolo
    {   9.0f, -12.0f, 12.0f, 1.00f, 0.94f, 0.84f, 22.0f, 0.42f },  // mastro NE
    { -12.0f,  -9.0f, 10.0f, 1.00f, 0.94f, 0.84f, 22.0f, 0.42f },  // mastro NO
    {  12.0f,   9.0f, 14.0f, 1.00f, 0.94f, 0.84f, 22.0f, 0.42f },  // mastro SE
    {   0.0f,  30.0f,  5.0f, 1.00f, 0.88f, 0.70f, 18.0f, 0.45f },  // eclusa sul: a porta principal
};
const int kBaseFloodCount = (int)(sizeof(kBaseFloods) / sizeof(kBaseFloods[0]));
