#pragma once

#include "blocks.h"   // Block

struct World;

// ============= Interior do complexo: aparencia =============
// Tudo o que e' DESENHADO dentro do complexo de interiores: paineis de parede, teto com vigas,
// tubulacao, passarelas, luminarias, marcacoes de piso, mobilia/maquinario e as plantas da estufa.
// Geometria imediata (rlgl).
//
// O que NAO esta aqui: piso e paredes sao BLOCOS de verdade criados por build_interiors()
// (interiors.cpp), so' que INVISIVEIS (Block::BaseShell). A geometria dos ambientes (retangulos, teto)
// vem de interiors.h - fonte unica lida pela construcao E por este desenho.
//
// POR QUE A PAREDE E' INVISIVEL E DESENHADA AQUI: com pe-direito de 9 a 16 (a escala pedida), uma
// parede de cubos custaria milhares de cubos empilhados e leria como muro de blocos. Como painel
// desenhado, um trecho de parede e' 1 quad do piso ao teto - alguns poucos quads por ambiente - e da'
// pra por nervuras, faixas e detalhe tecnico de graca.
//
// MOBILIA/MAQUINARIO COM FISICA: kFurniture e' a fonte unica. generate_base() a le pra estampar
// blocos INVISIVEIS de colisao (FurnitureLow/Mid/Tall/Huge) e render_base_interior() le a MESMA
// tabela pra desenhar - a forma visivel e a que colide nao podem divergir.

// Peca de mobilia/maquinario. Coordenadas em TILES relativas ao CENTRO DO DISTRITO (o mesmo
// referencial dos retangulos de kInteriors), nao ao centro da sala: com ambientes de tamanhos
// diferentes e conectados, um referencial unico e' bem mais facil de posicionar.
struct FurnPiece {
    int interior;        // indice em kInteriors
    int dx, dz;
    int w_dx, w_dz;      // footprint em tiles (impar = centrado)
    float height;        // altura VISUAL; casa com get_block_height(collider)
    Block collider;
    float r, g, b;
    bool emissive;
};
extern const FurnPiece kFurniture[];
extern const int kFurnitureCount;

void base_interior_stamp_furniture(World& world);

// Luminarias: UMA tabela, lida pelo desenho e pela coleta de luzes do lightmap 2D
// (lighting.cpp/collect_lights).
struct BaseLamp {
    int interior;
    float dx, dz;        // relativo ao centro do distrito
    float y;             // altura do bocal acima do piso
    float r, g, b;
    float radius;
    float intensity;
};
extern const BaseLamp kBaseLamps[];
extern const int      kBaseLampCount;

// Luz AMBIENTE ABSOLUTA dentro do complexo. Retorna false no exterior (o chamador usa o ambiente de
// dia/noite normal); true dentro, preenchendo a cor.
//
// Era um MULTIPLICADOR (indoor_ambient_mul = 0.45) aplicado sobre o ambiente de dia/noite. De noite
// isso dava 0.06 * 0.45 = 0.027, e ainda tingido do azul da noite: paredes e teto renderizavam PRETO
// e o interior virava um vazio escuro com uma bola de luz flutuando - o bug do screenshot. Uma
// instalacao pressurizada tem iluminacao artificial 24h; o ambiente dela NAO pode depender do sol.
// Valor absoluto elimina a falha por construcao, em vez de calibrar um multiplicador que sempre vai
// colapsar em alguma hora do dia.
bool base_interior_ambient(float world_x, float world_z, float& out_r, float& out_g, float& out_b);

// Desenha o complexo em volta do jogador (nada se ele estiver no exterior). Chamar dentro da regiao 3D
// de render_world(), DEPOIS do loop de terreno e ANTES do jogador. Entrada: depth test ON, depth mask
// ON, backface culling OFF, blend ALPHA, g_frame_fog preenchido.
void render_base_interior();
