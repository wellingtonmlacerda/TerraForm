#include "interiors.h"

#include "world.h"            // World, g_world, object_block_at
#include "player_physics.h"   // g_player, teleport_player_to
#include "camera.h"           // g_camera (orientacao coerente com a porta)
#include "game_state.h"       // set_toast
#include "math_core.h"        // kHeightScale, kPi, clamp01
#include "modules_building.h" // g_build_slots, BuildSlotInfo

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

extern int g_base_x;
extern int g_base_y;

static constexpr float kNoDoor = -1000.0f;

// ============= O COMPLEXO =============
// Planta baixa (tiles relativos ao centro do saguao; x cresce pra LESTE, z pra SUL):
//
//                        [ CONTROLE 19x14  h13 ]        z -36..-23
//                                  |
//                          Corredor Norte  h9            z -22..-13   (7 de largura)
//                                  |  \__ Ramal NE h8 __ [ ESTUFA 19x15 h14 ]
//                                  |
//   [ DORMITORIO ]--Corr.Oeste--[ S A G U A O   C E N T R A L  25x25  h16 ]--Corr.Leste--[ LABORATORIO ]
//        h9            h9                        |                            h9              h13
//                                  Corredor Sul  h9                           z 13..22
//                                  |  \__ Ramal SO h8 __ [ OFICINA 18x15 h12 ]
//                                  |
//                        [ ECLUSA 17x12  h10 ]           z 23..34
//
// Retangulos que se TOCAM ficam conectados: a parede so' nasce onde nao ha piso vizinho (ver
// build_interiors). Toda a circulacao entre modulos e' POR DENTRO - antes cada sala era uma caixa
// isolada, alcancada so' pela porta externa dela.
//
// PE-DIREITO contra um jogador de 1.80: corredor 9.0 = 5x a altura dele; saguao 16.0 = ~9x. A
// referencia visual pedida ("tubulacao/iluminacao/cabos, ESPACO LIVRE, personagem") sai disso: as
// estruturas de teto desenhadas por base_interior.cpp ficam nos 2-3 metros de cima, e sobram 6+ de ar
// livre acima da cabeca.
const InteriorDef kInteriors[] = {
 // nome                x0   z0   x1   z1  ceil room  door_dx door_dz  face   ret_dx ret_dz  spawn_x spawn_z sface  exit_x exit_z  piso                slots plant
  { "Saguao Central",  -12, -12,  12,  12, 16.0f, true,      0,      0, kNoDoor,     0,   0,       0,     0,   0.0f,     0,    0, Block::BaseFloor,   0, false },
  { "Corredor Norte",   -3, -22,   3, -13,  9.0f, false,     0,      0, kNoDoor,     0,   0,       0,   -18,   0.0f,     0,  -18, Block::BaseFloor,   0, false },
  { "Corredor Sul",     -3,  13,   3,  22,  9.0f, false,     0,      0, kNoDoor,     0,   0,       0,    18,   0.0f,     0,   18, Block::BaseFloor,   0, false },
  { "Corredor Leste",   13,  -3,  22,   3,  9.0f, false,     0,      0, kNoDoor,     0,   0,      18,     0,   0.0f,    18,    0, Block::BaseFloor,   0, false },
  { "Corredor Oeste",  -22,  -3, -13,   3,  9.0f, false,     0,      0, kNoDoor,     0,   0,     -18,     0,   0.0f,   -18,    0, Block::BaseFloor,   0, false },
  { "Ramal Nordeste",     4, -20,  14, -14,  8.0f, false,    0,      0, kNoDoor,     0,   0,       9,   -17,   0.0f,     9,  -17, Block::BaseFloor,   0, false },
  { "Ramal Sudoeste",   -14,  14,  -4,  20,  8.0f, false,    0,      0, kNoDoor,     0,   0,      -9,    17,   0.0f,    -9,   17, Block::BaseFloor,   0, false },

  // --- Ambientes com porta pro EXTERIOR. A escotilha externa fica sempre na parede OPOSTA a
  //     ligacao interna, entao entrar pela porta te deixa no fundo da sala olhando pra dentro dela.
  { "Eclusa",            -8,  23,   8,  34, 10.0f, true,     0,     32,    0.0f,     0,  35,       0,    31, 180.0f,     0,   33, Block::BaseFloor,   0, false },
  { "Laboratorio",       23,  -9,  41,   9, 13.0f, true,    32,      0,   90.0f,    35,   0,      38,     0, 270.0f,    40,    0, Block::BaseFloor,   0, false },
  { "Sala de Controle",  -9, -36,   9, -23, 13.0f, true,     0,    -32,  180.0f,     0, -35,       0,   -33,   0.0f,     0,  -35, Block::BaseFloor,   0, false },
  { "Dormitorio",       -40,  -9, -23,   9,  9.0f, true,   -32,      0,  270.0f,   -35,   0,     -37,     0,  90.0f,   -39,    0, Block::BaseFloor,   0, false },
  { "Estufa",            15, -26,  33, -12, 14.0f, true,    18,    -13,    0.0f,    18, -10,      30,   -19, 270.0f,    32,  -19, Block::BaseFloor,   2, true  },
  { "Oficina",          -32,  12, -15,  26, 12.0f, true,   -18,     13,  180.0f,   -18,  10,     -29,    19,  90.0f,   -31,   19, Block::BaseFloor,   1, false },
};
const int kInteriorCount = (int)(sizeof(kInteriors) / sizeof(kInteriors[0]));

void interior_district_center(int& out_x, int& out_z) {
    out_x = kInteriorDistrictX;
    out_z = kInteriorDistrictZ;
}

// ============= CONSTRUCAO =============
void build_interiors(World& world) {
    int ox, oz;
    interior_district_center(ox, oz);
    if (!world.in_bounds(ox, oz)) return;
    const int16_t floor_h = world.height_at(ox, oz);

    // Caixa que cobre o complexo inteiro + margem de 2 (parede + folga).
    int bx0 = 0, bz0 = 0, bx1 = 0, bz1 = 0;
    for (int i = 0; i < kInteriorCount; ++i) {
        bx0 = std::min(bx0, kInteriors[i].x0); bz0 = std::min(bz0, kInteriors[i].z0);
        bx1 = std::max(bx1, kInteriors[i].x1); bz1 = std::max(bz1, kInteriors[i].z1);
    }
    bx0 -= 3; bz0 -= 3; bx1 += 3; bz1 += 3;
    const int bw = bx1 - bx0 + 1, bh = bz1 - bz0 + 1;

    // Mapa de piso + altura de parede exigida por tile vizinho. Fazer isto num buffer, e nao tile a
    // tile direto no mundo, e' o que faz retangulos que se tocam ficarem ABERTOS entre si sem nenhuma
    // regra especial: a parede so' nasce onde nao ha piso.
    std::vector<int16_t> cell((size_t)bw * bh, -1);
    auto at = [&](int dx, int dz) -> int16_t& { return cell[(size_t)(dz - bz0) * bw + (dx - bx0)]; };

    for (int i = 0; i < kInteriorCount; ++i) {
        const InteriorDef& d = kInteriors[i];
        for (int dz = d.z0; dz <= d.z1; ++dz)
            for (int dx = d.x0; dx <= d.x1; ++dx)
                at(dx, dz) = (int16_t)i;
    }

    // Achatamento + limpeza de toda a caixa: sem isso o terreno original encostaria nas paredes.
    for (int dz = bz0; dz <= bz1; ++dz)
        for (int dx = bx0; dx <= bx1; ++dx) {
            int tx = ox + dx, tz = oz + dz;
            if (!world.in_bounds(tx, tz)) continue;
            world.set_height(tx, tz, floor_h);
            if (object_block_at(world, tx, tz) != Block::Air) world.set(tx, tz, Block::Air);
            while (world.stack_height_at(tx, tz) > 0) world.stack_pop(tx, tz);
        }

    // Piso.
    for (int dz = bz0; dz <= bz1; ++dz)
        for (int dx = bx0; dx <= bx1; ++dx) {
            int16_t ci = at(dx, dz);
            if (ci < 0) continue;
            int tx = ox + dx, tz = oz + dz;
            if (!world.in_bounds(tx, tz)) continue;
            world.set_ground(tx, tz, kInteriors[ci].floor_block);
            world.set(tx, tz, kInteriors[ci].floor_block);
        }

    // Parede: todo tile SEM piso que toca piso (4-conexo). Altura = teto do ambiente vizinho mais
    // alto + folga. Bloco INVISIVEL (BaseShell): a aparencia e' desenhada como painel grande por
    // base_interior.cpp, o que da controle visual e custa alguns quads em vez de milhares de cubos.
    for (int dz = bz0 + 1; dz <= bz1 - 1; ++dz) {
        for (int dx = bx0 + 1; dx <= bx1 - 1; ++dx) {
            if (at(dx, dz) >= 0) continue;
            float need = 0.0f;
            const int nx4[4] = {1,-1,0,0}, nz4[4] = {0,0,1,-1};
            for (int k = 0; k < 4; ++k) {
                int16_t n = at(dx + nx4[k], dz + nz4[k]);
                if (n >= 0) need = std::max(need, kInteriors[n].ceiling);
            }
            // Diagonais tambem: sem isto sobra uma fresta diagonal nas quinas concavas, e o collider
            // de 0.62 atravessa canto-a-canto (defeito real que ja apareceu neste projeto).
            const int dxs[4] = {1,1,-1,-1}, dzs[4] = {1,-1,1,-1};
            for (int k = 0; k < 4; ++k) {
                int16_t n = at(dx + dxs[k], dz + dzs[k]);
                if (n >= 0) need = std::max(need, kInteriors[n].ceiling);
            }
            if (need <= 0.0f) continue;
            // ALTURA UNIFORME (o teto MAIS ALTO do complexo + folga), nao a do ambiente vizinho.
            // Com altura por ambiente, na costura entre um ambiente alto e a parede de um baixo o
            // jogador ganhava o teto do alto (16) tendo ao lado uma parede de 11 - e passava por cima
            // dela com o jetpack. Medido: 33 tiles, pior caso 5.00 de sobra. Como a parede e'
            // INVISIVEL (a aparencia e' o painel desenhado, que vai do piso ate o teto DAQUELE
            // ambiente), deixar todas com a mesma altura nao muda nada visualmente e elimina a classe
            // de bug inteira, em vez de calibrar caso a caso.
            float tallest = 0.0f;
            for (int k = 0; k < kInteriorCount; ++k) tallest = std::max(tallest, kInteriors[k].ceiling);
            int layers = std::min(World::kMaxStackExtra,
                                  (int)std::ceil(tallest) + kInteriorWallExtra);
            (void)need;
            int tx = ox + dx, tz = oz + dz;
            if (!world.in_bounds(tx, tz)) continue;
            world.set_ground(tx, tz, Block::BaseFloor);
            world.set(tx, tz, Block::BaseFloor);
            for (int l = 0; l < layers; ++l) world.stack_push(tx, tz, Block::BaseShell);
        }
    }

    // Canteiros da estufa: 2 blocos laterais, corredor central largo.
    for (int i = 0; i < kInteriorCount; ++i) {
        const InteriorDef& d = kInteriors[i];
        if (!d.planters) continue;
        int cxm = (d.x0 + d.x1) / 2, czm = (d.z0 + d.z1) / 2;
        for (int dz = d.z0 + 2; dz <= d.z1 - 2; ++dz)
            for (int adx = 3; adx <= (d.x1 - d.x0) / 2 - 2; ++adx) {
                for (int s = -1; s <= 1; s += 2) {
                    int tx = ox + cxm + s * adx, tz = oz + dz;
                    if (!world.in_bounds(tx, tz)) continue;
                    if (world.stack_height_at(tx, tz) > 0) continue;
                    world.set_ground(tx, tz, Block::PlanterBed);
                    world.set(tx, tz, Block::PlanterBed);
                }
            }
        (void)czm;
    }

    // Slots de construcao dentro dos ambientes (estufa/oficina). Producao de modulo e' independente
    // de posicao (update_modules so' conta entradas em g_modules) e rebuild_modules_from_world()
    // varre o mundo, entao um modulo construido aqui produz e sobrevive a save/load.
    for (int i = 0; i < kInteriorCount; ++i) {
        const InteriorDef& d = kInteriors[i];
        for (int s = 0; s < d.build_slots; ++s) {
            int cxm = (d.x0 + d.x1) / 2;
            int sx = ox + cxm + (s == 0 ? -2 : 2);
            int sz = oz + d.z0 + 2;
            if (!world.in_bounds(sx, sz)) continue;
            if (world.stack_height_at(sx, sz) > 0) continue;
            world.set_ground(sx, sz, Block::BuildSlot);
            world.set(sx, sz, Block::BuildSlot);
            g_build_slots.push_back({sx, sz, Block::Air,
                                     std::string(d.name) + " " + std::to_string(s + 1)});
        }
    }
}

// ============= CONSULTAS (estado derivado da posicao) =============
int interior_at(float wx, float wz) {
    int ox, oz;
    interior_district_center(ox, oz);
    float dx = wx - (float)ox, dz = wz - (float)oz;
    for (int i = 0; i < kInteriorCount; ++i) {
        const InteriorDef& d = kInteriors[i];
        // +/-1.5: inclui o anel de parede. Se o teto valesse so' no piso, daria pra subir ficando
        // exatamente na coluna da parede e sair por cima.
        if (dx >= (float)d.x0 - 1.5f && dx <= (float)d.x1 + 1.5f &&
            dz >= (float)d.z0 - 1.5f && dz <= (float)d.z1 + 1.5f) return i;
    }
    return -1;
}

float interior_floor_y() {
    if (!g_world) return 0.0f;
    int ox, oz;
    interior_district_center(ox, oz);
    return (float)g_world->height_at(ox, oz) * kHeightScale;
}

float interior_ceiling_at(float wx, float wz) {
    if (!g_world) return 1.0e9f;
    int ox, oz;
    interior_district_center(ox, oz);
    float dx = wx - (float)ox, dz = wz - (float)oz;

    // 1) Dentro do retangulo de piso de um ambiente: o teto DELE.
    for (int i = 0; i < kInteriorCount; ++i) {
        const InteriorDef& d = kInteriors[i];
        if (dx >= (float)d.x0 - 0.5f && dx <= (float)d.x1 + 0.5f &&
            dz >= (float)d.z0 - 0.5f && dz <= (float)d.z1 + 0.5f)
            return interior_floor_y() + d.ceiling;
    }
    // 2) Na espessura da parede: o MENOR teto entre os ambientes vizinhos. Usar o primeiro que casa
    // (ou o maior) abriria uma fuga real: parado sobre a coluna de parede entre o saguao (teto 16) e um
    // corredor (parede de 11), o jogador ganharia teto 16, subiria acima da parede do corredor e
    // sairia por cima dela. Com o minimo, ele nunca passa da parede mais baixa que o cerca.
    float best = 1.0e9f;
    for (int i = 0; i < kInteriorCount; ++i) {
        const InteriorDef& d = kInteriors[i];
        if (dx >= (float)d.x0 - 1.6f && dx <= (float)d.x1 + 1.6f &&
            dz >= (float)d.z0 - 1.6f && dz <= (float)d.z1 + 1.6f)
            best = std::min(best, d.ceiling);
    }
    if (best >= 1.0e8f) return 1.0e9f;   // fora do complexo: NENHUM teto em lugar nenhum
    return interior_floor_y() + best;
}

bool interior_is_shelter(int i) { return i >= 0 && i < kInteriorCount; }

// Raio de gatilho generoso: 1.3 exigia acertar quase o tile exato com um collider de 0.62 - foi a
// causa de "nao consegui acessar o interior".
constexpr float kDoorTrigger = 2.6f;
constexpr float kDoorHint = 11.0f;

static int door_hit(float wx, float wz, bool exterior) {
    const float kT2 = kDoorTrigger * kDoorTrigger;
    int ox, oz;
    interior_district_center(ox, oz);
    for (int i = 0; i < kInteriorCount; ++i) {
        const InteriorDef& d = kInteriors[i];
        if (d.door_facing_deg <= -900.0f) continue;
        float tx, tz;
        if (exterior) { tx = (float)(g_base_x + d.door_dx); tz = (float)(g_base_y + d.door_dz); }
        else          { tx = (float)(ox + d.exit_x);        tz = (float)(oz + d.exit_z); }
        float dx = wx - tx, dz = wz - tz;
        if (dx * dx + dz * dz <= kT2) return i;
    }
    return -1;
}

int interior_exterior_door_at(float wx, float wz) {
    if (interior_at(wx, wz) >= 0) return -1;
    return door_hit(wx, wz, true);
}

int interior_exit_at(float wx, float wz) {
    int inside = interior_at(wx, wz);
    if (inside < 0) return -1;
    int hit = door_hit(wx, wz, false);
    return (hit == inside) ? hit : -1;   // so' a saida DO ambiente em que se esta
}

// ============= TRANSICAO =============
static char s_prompt[96] = {0};
static bool s_has_prompt = false;

const char* interior_prompt() { return s_has_prompt ? s_prompt : nullptr; }

// Orientacao coerente com a porta. reset_camera_near_player() (chamada por teleport_player_to)
// devolve a camera pro spawn_yaw fixo do config, entao sem isto o jogador chegaria olhando pra um lado
// e a camera pra outro.
static void face_player(float deg) {
    while (deg < 0.0f) deg += 360.0f;
    while (deg >= 360.0f) deg -= 360.0f;
    g_player.rotation = deg;
    g_player.target_rotation = deg;
    if (deg >= 315.0f || deg < 45.0f)      g_player.facing_dir = 0;
    else if (deg < 135.0f)                 g_player.facing_dir = 1;
    else if (deg < 225.0f)                 g_player.facing_dir = 2;
    else                                   g_player.facing_dir = 3;
    // A camera olha em -(sin yaw, cos yaw) (update_camera_position, camera.cpp) e o personagem encara
    // (sin deg, cos deg) - logo yaw = deg + 180 poe as duas no mesmo rumo.
    g_camera.yaw = deg + 180.0f;
}

// Este mundo tem o complexo? Sonda a parede sul do saguao. Um save anterior a esta arquitetura nao tem
// ambiente nenhum NEM patamar de porta na base - sem esta checagem o jogador aperta V na frente de uma
// parede lisa e NADA acontece, sem explicacao.
static const World* s_probed = nullptr;
static bool s_has_district = false;
static bool district_exists() {
    if (!g_world) return false;
    if (s_probed != g_world) {
        s_probed = g_world;
        int ox, oz;
        interior_district_center(ox, oz);
        // Sonda a QUINA noroeste do saguao. Tem que ser um tile que nao e' piso de nenhum ambiente:
        // (x0-1, z0) e (x0, z0-1) sao piso dos corredores oeste/norte, entao sondar ali daria "sem
        // distrito" num mundo novo e a porta falharia calada - exatamente o bug que esta checagem
        // existe pra evitar.
        int wx = ox + kInteriors[0].x0 - 1;
        int wz = oz + kInteriors[0].z0 - 1;
        s_has_district = g_world->in_bounds(wx, wz) && g_world->stack_height_at(wx, wz) > 0;
    }
    return s_has_district;
}

void interiors_update(bool interact) {
    s_has_prompt = false;
    if (!g_world) return;

    float px = g_player.pos.x, pz = g_player.pos.y;
    int ox, oz;
    interior_district_center(ox, oz);

    if (!district_exists()) {
        float bdx = px - (float)g_base_x, bdz = pz - (float)g_base_y;
        if (bdx * bdx + bdz * bdz < 45.0f * 45.0f) {
            std::snprintf(s_prompt, sizeof(s_prompt), "Base de save antigo - inicie um Novo Jogo");
            s_has_prompt = true;
            if (interact) set_toast("Esta base e' de um mundo antigo, sem os modulos internos. "
                                    "Inicie um Novo Jogo.", 4.0f);
        }
        return;
    }

    // DICA A DISTANCIA: a transicao e' por tecla, entao sem prompt a porta seria indescobrivel.
    int inside_now = interior_at(px, pz);
    if (inside_now < 0) {
        int best = -1; float best_d2 = kDoorHint * kDoorHint;
        for (int i = 0; i < kInteriorCount; ++i) {
            if (kInteriors[i].door_facing_deg <= -900.0f) continue;
            float dx = px - (float)(g_base_x + kInteriors[i].door_dx);
            float dz = pz - (float)(g_base_y + kInteriors[i].door_dz);
            float d2 = dx * dx + dz * dz;
            if (d2 < best_d2) { best_d2 = d2; best = i; }
        }
        if (best >= 0 && best_d2 > kDoorTrigger * kDoorTrigger) {
            std::snprintf(s_prompt, sizeof(s_prompt), "%s a %dm - chegue na escotilha",
                          kInteriors[best].name, (int)std::lround(std::sqrt(best_d2)));
            s_has_prompt = true;
        }
    }

    int ext = interior_exterior_door_at(px, pz);
    if (ext >= 0) {
        std::snprintf(s_prompt, sizeof(s_prompt), "[V] Entrar - %s", kInteriors[ext].name);
        s_has_prompt = true;
        if (interact) {
            const InteriorDef& d = kInteriors[ext];
            teleport_player_to(ox + d.spawn_x, oz + d.spawn_z);
            face_player(d.spawn_facing_deg);
            set_toast(std::string(d.name), 2.0f);
        }
        return;
    }

    int inside = interior_exit_at(px, pz);
    if (inside >= 0) {
        std::snprintf(s_prompt, sizeof(s_prompt), "[V] Sair - %s", kInteriors[inside].name);
        s_has_prompt = true;
        if (interact) {
            const InteriorDef& d = kInteriors[inside];
            teleport_player_to(g_base_x + d.ret_dx, g_base_y + d.ret_dz);
            face_player(d.door_facing_deg);
            set_toast("Exterior da base", 2.0f);
        }
    }
}
