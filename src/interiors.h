#pragma once

#include "blocks.h"   // Block

struct World;

// ============= EXTERIOR SELADO + COMPLEXO INTERNO INDEPENDENTE =============
// Arquitetura pedida: "o exterior da construcao e o interior jogavel sao tratados como espacos
// separados". O motivo nao e' estetico, e' uma limitacao MEDIDA do motor:
//
//   World guarda UMA contagem de pilha por coluna (world.h) e column_blocks_movement
//   (player_physics.cpp) fixa o fundo de qualquer bloco no terreno. Um bloco "de teto" seria um pilar
//   solido do chao pra cima => TETO DE BLOCO E' IMPOSSIVEL. Logo qualquer sala OCA construida no
//   mundo jogavel e' aberta por cima, e com jetpack entrar por ali e' trivial. Nenhum collider
//   conserta isso - o interior e o exterior seriam o mesmo espaco.
//
// Duas metades:
//   1) EXTERIOR = volume MACICO invisivel (Block::BaseShell) com um modelo liso desenhado por cima
//      (base_exterior.h). Nao existe "dentro" pra invadir; pousar no telhado e' pousar no topo de uma
//      pilha, que o motor resolve nativamente. Jetpack 100% livre lá fora.
//   2) INTERIOR = este arquivo. Um COMPLEXO conectado num distrito reservado, longe da base, com
//      TETO EXPLICITO por ambiente (um limite de Y - a unica forma que o motor tem de expressar teto).
//
// ESCALA (reescrita a pedido: "os interiores parecem salas pequenas e apertadas"):
// A versao anterior tinha 6 caixas ISOLADAS de 3.2 de pe-direito - com um jogador de 1.80 isso deixa
// 1.4 acima da cabeca, claustrofobico, e nao havia circulacao nenhuma (cada sala so' era alcancada por
// teleporte da porta externa dela). Agora:
//   - Saguao central de 25x25 com 16.0 de pe-direito (~9x a altura do jogador);
//   - corredores principais de 7 tiles de largura e 9.0 de altura, ligando tudo POR DENTRO;
//   - salas de 13.0 a 14.0 de altura, com vigas, tubulacao, passarelas e luminarias no teto - e' o
//     que faz a escala LER como instalacao em vez de sala grande;
//   - jetpack utilizavel de verdade no saguao e nas salas altas: e' consequencia da arquitetura, nao
//     um bloqueio. O teto so' impede SAIR do complexo.
//
// ESTADO DERIVADO DA POSICAO, nao salvo: "em que ambiente estou" e' funcao pura de (x,z) - ver
// interior_at(). Sem global pra ficar obsoleto depois de carregar um save (footgun que ja causou bug
// real aqui) e sem mudanca de formato: os ambientes sao tiles normais, persistem de graca.
//
// PERFORMANCE: o distrito fica a ~1200 tiles da base, muito alem de view_radius - custa ZERO estando
// fora, e vice-versa. E' o "carregado sob demanda" pedido, sem sistema de streaming novo. As paredes
// sao Block::BaseShell (invisiveis) e a aparencia e' desenhada como paineis grandes por
// base_interior.cpp: alguns poucos quads em vez de milhares de cubos.
//
// COMO ADICIONAR UM AMBIENTE: uma linha em kInteriors[]. Retangulos que se TOCAM ficam conectados
// automaticamente (a parede so' nasce onde nao ha piso vizinho), entao ligar uma sala nova a um
// corredor e' so' fazer os retangulos se encostarem.

struct InteriorDef {
    const char* name;

    // --- Retangulo de PISO, inclusive, em tiles relativos ao centro do distrito ---
    int x0, z0, x1, z1;

    float ceiling;      // pe-direito em unidades de mundo
    bool  is_room;      // false = corredor/circulacao (menos detalhe desenhado no teto)

    // --- Porta exterior. door_facing_deg <= -900 = ambiente interno puro, sem porta pro exterior ---
    int   door_dx, door_dz;      // tile do patamar, relativo a (g_base_x, g_base_y)
    float door_facing_deg;       // pra onde o jogador olha ao SAIR (pra fora da base)
    int   ret_dx, ret_dz;        // ponto de retorno no exterior

    // --- Chegada e gatilho de saida, em tiles do DISTRITO ---
    int   spawn_x, spawn_z;
    float spawn_facing_deg;      // pra onde olha ao CHEGAR (pra dentro do ambiente)
    int   exit_x, exit_z;

    Block floor_block;
    int   build_slots;
    bool  planters;
};

extern const InteriorDef kInteriors[];
extern const int kInteriorCount;

// Origem absoluta do distrito (o centro do saguao). Longe da base, que nasce em torno do centro do
// mapa 3072x1536 (+/-70 em x, +/-45 em z).
constexpr int kInteriorDistrictX = 200;
constexpr int kInteriorDistrictZ = 200;

// Folga da parede acima do teto de cada ambiente. A parede tem que passar do teto pra ler como parede
// e pra nao haver como espiar por cima; o que impede de SAIR e' o teto, nao a altura da parede.
constexpr int kInteriorWallExtra = 2;

// Centro absoluto (tiles) do distrito.
void interior_district_center(int& out_x, int& out_z);

// Constroi o complexo. Chamado por generate_base().
void build_interiors(World& world);

// Em que ambiente a posicao (wx,wz) esta - -1 = fora do complexo. Inclui o anel de parede de
// proposito: o teto tem que valer na espessura da parede tambem, senao daria pra subir ficando
// exatamente na coluna dela.
int interior_at(float wx, float wz);

// Teto absoluto (Y de mundo) na posicao, ou um valor enorme fora do complexo. E' isso que impede sair
// voando de dentro - e a razao de o exterior continuar com liberdade total: fora do distrito esta
// funcao nunca limita nada.
float interior_ceiling_at(float wx, float wz);

// Y do piso do complexo (todo o complexo fica num nivel unico).
float interior_floor_y();

// Porta EXTERIOR que o jogador esta pisando (-1 = nenhuma); saida INTERNA do ambiente atual.
int interior_exterior_door_at(float wx, float wz);
int interior_exit_at(float wx, float wz);

const char* interior_prompt();
void interiors_update(bool interact);

// Todo ambiente do complexo e' abrigo (o jogador esta dentro de um modulo pressurizado da base).
bool interior_is_shelter(int i);
