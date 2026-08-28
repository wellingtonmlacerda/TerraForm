#include "resource_geology.h"

#include "noise.h"
#include "math_core.h"

#include <algorithm>
#include <cmath>

namespace {

// Faixa suave: 1.0 dentro de [lo, hi], caindo linearmente ate 0 em `soft` alem de cada borda.
// Preferida a um "if (v > X)" porque a transicao gradual e' o que evita a borda dura de recurso
// (uma linha reta no mapa onde o minerio simplesmente para).
float band(float v, float lo, float hi, float soft) {
    if (soft <= 0.0001f) return (v >= lo && v <= hi) ? 1.0f : 0.0f;
    if (v < lo) return clamp01(1.0f - (lo - v) / soft);
    if (v > hi) return clamp01(1.0f - (v - hi) / soft);
    return 1.0f;
}

} // namespace

// ============================================================================
// TABELA DE DISTRIBUICAO
// ============================================================================
// Cada linha responde "onde este recurso ocorre naturalmente". Os numeros foram escolhidos contra
// a demanda real da campanha (ferro ~445, cobre ~270, carvao 50, cristal ~70, metal ~90,
// componentes ~70) e verificados por medicao estatistica no mapa gerado.
//
// noise_scale controla o TAMANHO do deposito e e' o que da identidade a cada recurso:
//   0.010 -> campos enormes (cristal: "formacao de cristal" reconhecivel)
//   0.030 -> depositos grandes (gelo, areia)
//   0.10  -> depositos medios (ferro, cobre)
//   0.16  -> manchas pequenas (carvao, componentes)
//   0.26  -> veios minusculos (metal)
const ResourceRule kResourceRules[kResourceRuleCount] = {
    // ---------------- FERRO: rocha e encosta, altitude media-alta ----------------
    // Campo proprio de deposito medio, puxado por ridge (cordilheira = rocha exposta). Fica
    // deliberadamente FORA das cotas mais altas: o topo congelado e' territorio de gelo/cristal.
    { Block::Iron, "Ferro",
      /*scale*/ 0.105f, /*offset*/ 700.0f, /*oct*/ 3, /*gate*/ 0.788f,
      /*alt */ 0.30f, 0.82f, 0.20f,
      /*temp*/ 0.00f, 1.00f, 0.00f,
      /*moist*/0.00f, 1.00f, 0.00f,
      /*ridge*/ 0.85f, /*valley*/ 0.0f, /*slope*/ 0.10f, /*volcanic*/ 0.0f,
      /*anti*/ -1, 0.0f, /*min_suit*/ 0.18f },

    // ---------------- CARVAO: vales e baixadas, manchas + fendas ----------------
    // Distribuicao OPOSTA a do ferro: puxado por valley e por altitude baixa-media. Carvao e'
    // materia organica sepultada - bacia, nao pico. As fendas escuras (Passo 6) continuam como
    // fonte secundaria, preservando a identidade visual.
    { Block::Coal, "Carvao",
      /*scale*/ 0.16f, /*offset*/ 200.0f, /*oct*/ 3, /*gate*/ 0.845f,
      /*alt */ 0.12f, 0.58f, 0.18f,
      /*temp*/ 0.00f, 1.00f, 0.00f,
      /*moist*/0.00f, 1.00f, 0.00f,
      /*ridge*/ 0.0f, /*valley*/ 0.90f, /*slope*/ 0.0f, /*volcanic*/ 0.0f,
      /*anti*/ -1, 0.0f, /*min_suit*/ 0.20f },

    // ---------------- COBRE: encosta rochosa, ANTI-CORRELACIONADO com ferro ----------------
    // Mesmo tipo de terreno do ferro, mas o campo e' outro (offset diferente) E a adequacao cai
    // onde o campo do ferro e' alto (anti_w). E' o que produz "regiao rica em ferro -> pouco
    // cobre" e vice-versa, em vez de os dois seguirem o mesmo relevo e sempre coexistirem.
    { Block::Copper, "Cobre",
      /*scale*/ 0.088f, /*offset*/ 3100.0f, /*oct*/ 3, /*gate*/ 0.800f,
      /*alt */ 0.24f, 0.74f, 0.22f,
      /*temp*/ 0.00f, 1.00f, 0.00f,
      /*moist*/0.00f, 1.00f, 0.00f,
      /*ridge*/ 0.45f, /*valley*/ 0.15f, /*slope*/ 0.06f, /*volcanic*/ 0.0f,
      /*anti*/ kRuleIron, 0.92f, /*min_suit*/ 0.18f },

    // ---------------- CRISTAL: campos contiguos no alto e no frio ----------------
    // scale bem baixa = manchas GRANDES: a intencao e' o jogador reconhecer "aqui existe uma
    // formacao de cristal", nao topar com specks isolados. Exige altitude alta E temperatura
    // baixa, entao ele fica junto do gelo - a "montanha gelada -> Gelo + Cristal" pedida.
    { Block::Crystal, "Cristal",
      /*scale*/ 0.020f, /*offset*/ 900.0f, /*oct*/ 3, /*gate*/ 0.545f,
      /*alt */ 0.66f, 1.00f, 0.14f,
      /*temp*/ 0.00f, 0.42f, 0.12f,
      /*moist*/0.00f, 1.00f, 0.00f,
      /*ridge*/ 0.35f, /*valley*/ 0.0f, /*slope*/ 0.0f, /*volcanic*/ 0.0f,
      /*anti*/ -1, 0.0f, /*min_suit*/ 0.30f },

    // ---------------- METAL: veios minusculos e rarissimos ----------------
    // scale alta (campo varia rapido) + gate alto = poucos tiles, isolados. Puxado por vulcanico:
    // metal nativo perto de atividade geologica. Continua raro de proposito - a fonte confiavel
    // sao os 8 destrocos (2 Metal cada), que o Passo 7 preserva intactos.
    { Block::Metal, "Metal",
      /*scale*/ 0.26f, /*offset*/ 1100.0f, /*oct*/ 2, /*gate*/ 0.845f,
      /*alt */ 0.28f, 0.86f, 0.16f,
      /*temp*/ 0.00f, 1.00f, 0.00f,
      /*moist*/0.00f, 1.00f, 0.00f,
      /*ridge*/ 0.40f, /*valley*/ 0.0f, /*slope*/ 0.0f, /*volcanic*/ 0.55f,
      /*anti*/ -1, 0.0f, /*min_suit*/ 0.25f },

    // ---------------- COMPONENTES: pequenas ocorrencias em regiao SECA ----------------
    // Preserva a ideia da condicao antiga (dry > 0.60 && tech > 0.93): regiao seca + campo
    // tecnologico proprio e estreito. Sao restos tecnologicos preservados pela aridez, e por isso
    // ficam junto da areia - a "regiao seca -> Areia + Componentes" pedida.
    { Block::Components, "Componentes",
      /*scale*/ 0.150f, /*offset*/ 4200.0f, /*oct*/ 2, /*gate*/ 0.812f,
      /*alt */ 0.15f, 0.80f, 0.20f,
      /*temp*/ 0.00f, 1.00f, 0.00f,
      /*moist*/0.00f, 0.36f, 0.14f,   // SECO: umidade alta zera a ocorrencia
      /*ridge*/ 0.0f, /*valley*/ 0.0f, /*slope*/ 0.0f, /*volcanic*/ 0.0f,
      /*anti*/ -1, 0.0f, /*min_suit*/ 0.28f },
};

float resource_field(const ResourceRule& r, float wx, float wy) {
    return fbm(wx * r.noise_scale + r.noise_offset,
               wy * r.noise_scale + r.noise_offset, r.noise_octaves);
}

float resource_suitability(const ResourceRule& r, const GeoContext& c) {
    // Faixas: se o tile esta fora de qualquer uma das tres, a adequacao morre. E' isso que faz
    // cristal nao existir em planicie quente e componentes nao existirem em terreno umido.
    float s = band(c.alt, r.alt_lo, r.alt_hi, r.alt_soft);
    if (s <= 0.0001f) return 0.0f;
    s *= band(c.temp, r.temp_lo, r.temp_hi, r.temp_soft);
    if (s <= 0.0001f) return 0.0f;
    s *= band(c.moist, r.moist_lo, r.moist_hi, r.moist_soft);
    if (s <= 0.0001f) return 0.0f;

    // Pesos aditivos: cada um empurra a adequacao pra cima onde o fator ambiental e' forte.
    float boost = 1.0f;
    if (r.ridge_w    > 0.0f) boost += r.ridge_w    * clamp01(c.ridge);
    if (r.valley_w   > 0.0f) boost += r.valley_w   * clamp01(c.valley);
    if (r.slope_w    > 0.0f) boost += r.slope_w    * clamp01(c.slope / 6.0f);
    if (r.volcanic_w > 0.0f) boost += r.volcanic_w * clamp01(c.volcanic);
    s *= boost;

    // Anti-correlacao: cai onde o campo do outro recurso domina.
    if (r.anti_index >= 0 && r.anti_index < kResourceRuleCount && r.anti_w > 0.0f) {
        float other = resource_field(kResourceRules[r.anti_index], c.wx, c.wy);
        // Só penaliza quando o outro campo esta acima do PROPRIO gate dele - ou seja, onde ele
        // realmente ocorreria. Longe disso os dois convivem normalmente.
        float dom = clamp01((other - kResourceRules[r.anti_index].gate) /
                            std::max(0.01f, 1.0f - kResourceRules[r.anti_index].gate));
        s *= (1.0f - r.anti_w * dom);
    }
    return clamp01(s);
}

Block pick_resource(const GeoContext& c) {
    // Escolha por MAIOR MARGEM NORMALIZADA, nao por ordem na lista. A cadeia if/else antiga fazia
    // o primeiro que passasse ganhar, entao mexer no limiar de um recurso empurrava tiles pro
    // seguinte (medido: subir o limiar do carvao quase dobrou o cristal). Com margem normalizada
    // cada regra e' independente: se o campo do ferro esta 0.70 num gate de 0.62, a margem e'
    // (0.70-0.62)/(1-0.62) = 0.21, comparavel com a de qualquer outro recurso na mesma escala.
    float best_margin = 0.0f;
    Block best = Block::Air;
    for (int i = 0; i < kResourceRuleCount; ++i) {
        const ResourceRule& r = kResourceRules[i];
        float suit = resource_suitability(r, c);
        if (suit < r.min_suit) continue;
        float field = resource_field(r, c.wx, c.wy);
        // A adequacao ambiental EMPURRA o campo: num terreno perfeito o campo efetivo sobe, num
        // terreno marginal ele cai. Assim o mesmo campo geologico rende deposito numa montanha
        // rochosa e nada numa planicie.
        float eff = field * (0.55f + 0.45f * suit);
        if (eff <= r.gate) continue;
        float margin = (eff - r.gate) / std::max(0.01f, 1.0f - r.gate);
        if (margin > best_margin) { best_margin = margin; best = r.block; }
    }
    return best;
}

float ice_suitability(float alt, float temp) {
    // Curva de ALTITUDE (nao um "if height > X"): praticamente 0 na baixada, subindo devagar no
    // meio e forte no alto. Foi o pedido explicito - transicao natural, nao corte seco.
    //   alt 0.00-0.45 -> ~0      alt 0.60 -> ~0.25
    //   alt 0.75      -> ~0.65   alt 0.90+ -> ~1.0
    float a = clamp01((alt - 0.45f) / 0.45f);
    a = a * a * (3.0f - 2.0f * a);         // smoothstep: encosta suave, pico saturado

    // Frio: 1 abaixo de 0.30, caindo a 0 em 0.55. Uma regiao quente nao congela nem no pico.
    float cold = clamp01((0.55f - temp) / 0.25f);

    // MULTIPLICATIVO, nao aditivo: precisa de altitude E frio. A condicao antiga era um OR, e o
    // termo "temp < 0.25" sozinho colocava gelo em manchas frias de altitude baixa - a causa do
    // "gelo aleatorio na planicie". Com produto, frio sem altitude da zero.
    return clamp01(a * cold * 1.35f);
}
