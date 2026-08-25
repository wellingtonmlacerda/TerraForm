#pragma once

struct World;

// ============= EXTERIOR DA BASE: modelo proprio + colisao da MESMA tabela =============
// Pedido do jogador: "o exterior deve ser um modelo/estrutura propria", com cara de instalacao
// espacial modular (domos, modulos cilindricos, corredores-tubo, areas tecnicas), e nao um predio de
// comodos. E antes disso: "nao consegui acessar o interior" / "nao parece a base espacial que pedi".
//
// Duas coisas ficam separadas de proposito, e AS DUAS SAEM DESTA MESMA TABELA:
//
//   APARENCIA  - render_base_exterior() desenha geometria LISA (cilindros com topo em domo, tubos,
//                tanques, mastros, escotilhas). Cubos empilhados nunca vao parecer uma base
//                espacial: leem como muro de blocos. Esta era a causa de "nao parece a base que
//                pedi" - a versao anterior mostrava a propria casca de colisao.
//   COLISAO    - base_exterior_stamp() estampa Block::BaseShell (invisivel, ver blocks.h) nas
//                mesmas posicoes/raios/alturas. E' o que mantem a instalacao VEDADA contra o
//                jetpack: nao existe vao interno pra invadir.
//
// Ler as duas da mesma tabela e' obrigatorio, nao elegancia: duplicar geometria da base em 2 lugares
// ja causou bug real neste projeto (ver a nota sobre g_shelter_door_x/y em modules_building.h). Se a
// forma desenhada e a forma solida discordarem, o resultado e' exatamente a "parede invisivel" que o
// jogador rejeitou.
//
// MARGEM DE 0.5 TILE: o modelo desenhado e' meio tile MAIOR que a casca de blocos. Blocos formam uma
// escada; um cilindro liso do mesmo raio deixaria quinas de bloco pra fora dele, e o jogador bateria
// em algo que nao ve. Com o desenho por fora, ele para pouco ANTES da parede visivel - erro na
// direcao certa.
//
// COMO MUDAR A COMPOSICAO DA BASE: mexer em kExterior[]. A colisao acompanha sozinha.

enum class ExtShape {
    Drum,     // cilindro vertical com topo em domo (o domo central, modulos, tanques)
    Tube,     // cilindro horizontal (corredor de ligacao entre modulos)
    Mast,     // pilar fino com antena no topo
    Solar,    // painel solar inclinado sobre pernas (area tecnica, sem colisao de casca)
};

struct ExtPiece {
    ExtShape shape;
    float dx, dz;         // centro, em tiles relativos a (g_base_x, g_base_y)
    float radius;         // Drum/Tube/Mast: raio. Solar: meia-largura
    float height;         // altura do corpo em unidades de mundo (= camadas de bloco na colisao)
    float len;            // Tube/Solar: comprimento ao longo de axis_deg
    float axis_deg;       // Tube/Solar: direcao no plano (0 = +Z, 90 = +X)
    float dome_ratio;     // Drum: altura do domo do topo / raio (0 = topo plano)
    int   portholes;      // vigias em volta do corpo (0 = nenhuma)
    float door_deg;       // Drum: azimute da escotilha (-1000 = sem porta)
    float tint;           // 0 = casco branco, 1 = metal tecnico mais escuro
};

extern const ExtPiece kExterior[];
extern const int kExteriorCount;

// Estampa a casca de colisao (Block::BaseShell) de todas as pecas. Chamado por generate_base().
// `pad_h` e' a altura de heightmap unica de todo o complexo.
void base_exterior_stamp(World& world, int cx, int cz, int pad_h);

// Desenha o modelo. Chamar dentro da regiao 3D de render_world(), junto do resto do mundo (precisa
// do fog/lightmap do frame). Nao desenha nada se o jogador estiver longe da base.
void render_base_exterior();

// Holofotes EXTERNOS da base, registrados no lightmap 2D (lighting.cpp/collect_lights). Sem eles a
// base ficava acesa (por auto-iluminacao) sobre um chao PRETO de noite - o que le pior que tudo
// escuro. Sao poucos e de raio grande: o objetivo e' o solo em volta da instalacao ter luz, nao
// desenhar pocas.
struct BaseFlood {
    float dx, dz;      // relativo a (g_base_x, g_base_y)
    float y;           // guardado; o lightmap e' 2D
    float r, g, b;
    float radius, intensity;
};
extern const BaseFlood kBaseFloods[];
extern const int kBaseFloodCount;
