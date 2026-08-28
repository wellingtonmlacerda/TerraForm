#pragma once

#include "blocks.h"

// ============= Distribuicao geologica de recursos =============
// O Passo 6 de World::gen() colocava minerio a partir de 5 campos fbm independentes gateados
// apenas por altura acima do mar, numa cadeia if/else onde o primeiro que passava ganhava. Dois
// problemas concretos:
//
//  1) Os dados AMBIENTAIS do Passo 1 (altitude, temperatura, umidade, cordilheira, vale, bioma)
//     eram calculados, usados pra escolher o tipo de solo no Passo 5, e depois DESCARTADOS. O
//     minerio nao sabia se estava numa montanha, num vale seco ou num pico congelado - por isso
//     "os recursos aparecem de forma muito aleatoria".
//  2) A cadeia if/else ACOPLA os recursos: medido nesta sessao, subir o limiar do carvao empurrou
//     tiles pro cristal e a abundancia dele quase dobrou sem ninguem tocar no campo do cristal.
//
// Aqui cada recurso tem uma REGRA com campo geologico proprio (escala = tamanho do deposito,
// offset = independencia) e pesos ambientais. Todas as regras sao avaliadas e a de MAIOR MARGEM
// ganha - nao a primeira da lista. Ajustar um recurso nao redistribui os outros.
//
// Nada de sistema paralelo: as regras leem os mapas que gen() ja produz, e a tabela e' o unico
// lugar pra ajustar distribuicao.

// Contexto ambiental de um tile. Preenchido por gen() a partir dos mapas que ele ja tem.
struct GeoContext {
    float alt;        // altitude normalizada 0..1 (heights[])
    float temp;       // temperatura 0..1 (temp_map[]) - ja inclui latitude e altitude
    float moist;      // umidade 0..1 (moist_map[])
    float ridge;      // ruido de cordilheira 0..1 (ridge_map[]) - le como "rochosidade"
    float valley;     // ruido de vale 0..1 (valley_map[])
    float slope;      // desnivel local em unidades de heightmap
    float volcanic;   // 0..1 - proximidade de vulcao/chamine
    int   biome;      // 0 planicie | 1 vale | 2 montanha | 3 plato | 4 gelo
    float wx, wy;     // coordenada do tile (pra amostrar o campo geologico do recurso)
};

// Uma regra de distribuicao. Tudo que define ONDE um recurso aparece esta aqui - e' o
// "ResourceDistributionConfig" pedido: pra deixar gelo mais alto, mexe alt_lo; pra deixar carvao
// mais de vale, mexe valley_w.
struct ResourceRule {
    Block block;
    const char* name;

    // ---- campo geologico proprio ----
    // scale menor = manchas MAIORES (o ruido varia mais devagar). E' o que da identidade de
    // deposito: pedra em grandes areas, metal em veios minusculos.
    float noise_scale;
    float noise_offset;     // deslocamento do dominio: campos independentes entre si
    int   noise_octaves;
    float gate;             // limiar no campo (0..1). Mais alto = mais raro e mais aglomerado.

    // ---- adequacao ambiental ----
    // Faixas preferidas, com transicao suave de `soft` fora das bordas. Fora da faixa + soft a
    // adequacao e' 0 e o recurso simplesmente nao ocorre ali.
    float alt_lo, alt_hi, alt_soft;
    float temp_lo, temp_hi, temp_soft;
    float moist_lo, moist_hi, moist_soft;

    // Pesos aditivos (0 = ignora). Somados a 1.0 e usados como multiplicador da adequacao, entao
    // um peso de 0.8 em ridge quase dobra a chance no alto de uma cordilheira.
    float ridge_w;
    float valley_w;
    float slope_w;
    float volcanic_w;

    // Anti-correlacao com outro recurso: quando > 0, a adequacao cai onde o campo do recurso
    // `anti_index` e' alto. E' o que faz "regiao rica em ferro -> pouco cobre".
    int   anti_index;       // -1 = nenhum
    float anti_w;

    float min_suit;         // abaixo disto nao tenta (corta a cauda longa)
};

// Ordem = indice em kResourceRules. Rarissimos primeiro NAO importa mais (a escolha e' por
// margem, nao por ordem), mas manter agrupado ajuda a ler a tabela.
enum ResourceRuleIndex {
    kRuleIron = 0,
    kRuleCoal,
    kRuleCopper,
    kRuleCrystal,
    kRuleMetal,
    kRuleComponents,
    kResourceRuleCount,
};

extern const ResourceRule kResourceRules[kResourceRuleCount];

// Adequacao ambiental 0..1 de um recurso num contexto (sem o campo geologico).
float resource_suitability(const ResourceRule& r, const GeoContext& c);

// Valor do campo geologico do recurso naquele ponto, 0..1.
float resource_field(const ResourceRule& r, float wx, float wy);

// Escolhe QUAL recurso ocorre neste tile, ou Block::Air. Avalia todas as regras e devolve a de
// maior margem normalizada - por isso mexer numa regra nao redistribui para as outras.
Block pick_resource(const GeoContext& c);

// ============= Adequacao de GELO/NEVE =============
// Separada das regras de minerio porque gelo e neve sao SOLO (Passo 5), nao objeto (Passo 6).
// Devolve 0..1: acima de kIceThreshold o tile e' congelado.
//
// Existe porque a condicao antiga era "biome == 4 || th >= snow_h || temp < 0.25", e o terceiro
// termo bastava sozinho: temp e' fbm com latitude, entao ha manchas frias em altitude BAIXA - era
// dali que vinha "gelo aparecendo aleatoriamente em planicies".
float ice_suitability(float alt, float temp);
constexpr float kIceThreshold = 0.42f;
