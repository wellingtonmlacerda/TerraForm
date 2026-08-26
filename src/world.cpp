#include "world.h"

#include "math_core.h"   // clamp01, smoothstep01, kHeightScale
#include "noise.h"       // init_permutation, perlin, fbm, ridged_fbm, lerp
#include "config_types.h" // TerrainConfig, MiningConfig (types of the extern globals below)
#include "game_state.h"  // rng_next_u32, rng_next_f01, set_toast
#include "render_primitives.h"  // render_plane_3d/render_glow_disc_3d (efeitos de agua/vapor)
#include "raylib_platform.h"    // rlSetTexture/rlSetBlendMode (efeitos)
#include "audio.h"              // play_steam_hiss_sound (chiado ao apagar lava)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Globais de estado de jogo ainda definidas em main.cpp (extracao completa para
// game_state/config_io/camera/player_physics e uma fase posterior ou paralela do plano de
// refatoracao). Removido o "static" delas em main.cpp para dar linkage externo, ja que
// World::gen()/update_phase()/terraform_step()/recompute_terraform_score()/
// melt_ice_around() abaixo precisam le-las/escreve-las de outra unidade de traducao.
// Mesmo padrao de g_oxygen/g_water_res/g_temperature/g_atmosphere em textures.cpp e de
// g_terrain_cfg/g_mining_cfg em config_io.cpp.
extern TerrainConfig g_terrain_cfg;
extern MiningConfig g_mining_cfg;
extern TerraPhase g_phase;
extern float g_terraform;
extern bool g_victory;
extern float g_co2_level;
extern bool g_surface_dirty;
extern float g_temperature;
extern float g_atmosphere;
extern float g_oxygen;
extern float g_water_res;

// Limiares de temperatura usados por update_phase()/melt_ice_around() abaixo.
// kTempHabitable/kTempTarget vieram de main.cpp (eram "static constexpr float" la, so
// usados por update_phase(), que se mudou para ca). kTempThawing tambem era static la,
// mas main.cpp ainda tem sua propria copia identica (o timer global de degelo em
// update_modules() a usa diretamente) - como e um literal em tempo de compilacao, e nao
// estado mutavel, duplicar a constante file-local em vez de compartilhar via extern.
static constexpr float kTempThawing = 0.0f;     // Water can be liquid
static constexpr float kTempHabitable = 15.0f;  // Can plant outside
static constexpr float kTempTarget = 22.0f;     // Ideal Earth-like

// O unico World do jogo (ver comentario em world.h).
World* g_world = nullptr;

// ============= WORLD GENERATION (Macro Heightmap + Erosion + Biomes) =============
// Extraido verbatim de main.cpp (era um metodo inline "void gen() { ... }" dentro da
// struct World); corpo inalterado, so a assinatura virou out-of-line.
void World::gen() {
    init_permutation(seed);

    std::fill(tiles.begin(), tiles.end(), Block::Air);
    std::fill(ground.begin(), ground.end(), Block::Dirt);
    std::fill(heightmap.begin(), heightmap.end(), 0);
    std::fill(surface_y.begin(), surface_y.end(), 0);

    const TerrainConfig& cfg = g_terrain_cfg;
    auto index_of = [this](int x, int y) -> size_t { return (size_t)y * (size_t)w + (size_t)x; };

    int min_h_i = std::max(0, (int)std::lround(cfg.min_height));
    int max_h_i = std::max(min_h_i + 2, (int)std::lround(cfg.max_height));
    int sea_h = std::clamp((int)std::lround(cfg.sea_height), min_h_i, max_h_i - 1);
    int snow_h = std::clamp((int)std::lround(cfg.snow_height), sea_h + 2, max_h_i);
    sea_level = sea_h;

    const size_t cell_count = (size_t)w * (size_t)h;
    std::vector<float> heights(cell_count, 0.0f);
    std::vector<float> temp_map(cell_count, 0.0f);
    std::vector<float> moist_map(cell_count, 0.0f);
    std::vector<float> ridge_map(cell_count, 0.0f);
    std::vector<float> valley_map(cell_count, 0.0f);
    std::vector<uint8_t> biome_map(cell_count, 0);
    // Preenchido no Passo 1.5 (vulcoes), lido no Passo 5 pra pintar o fundo da cratera com
    // Block::Lava - precisa sobreviver alem do escopo do bloco que o gera.
    std::vector<std::pair<int, int>> volcano_centers;
    static constexpr float kVolcanoCraterRadius = 9.0f;
    // Piso da cratera vira Lava. Precisa cobrir a cratera INTEIRA (kVolcanoCraterRadius=9),
    // nao so' 5: medido na geracao real, o anel r=5..9 caia no ramo seguinte do Passo 5 e,
    // quando o piso da cratera ficava perto/abaixo do nivel do mar, era pintado de AGUA -
    // o vulcao virava um pontinho de lava cercado de agua achatada.
    static constexpr float kVolcanoLavaRadius = 9.0f;
    // Chamines vulcanicas pequenas (pedido do jogador: "coloque lava e pequenas chamines
    // vulcanicas em certos biomas") - diferentes dos vulcoes grandes acima (raio 42,
    // erguem o terreno): sao so' um pontinho de lava (raio ~1.5) cravado num bioma rochoso,
    // sem sculpir heightmap nenhum - populado logo apos os vulcoes grandes (Passo 1.5),
    // lido no Passo 5 igual a volcano_centers.
    std::vector<std::pair<int, int>> vent_centers;
    static constexpr float kVentLavaRadius = 1.5f;

    // === Passo 1: macro shape (continentes, bacias, vales, cordilheiras) ===
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float fx = (float)x;
            float fy = (float)y;

            float warp_x = (fbm(fx * cfg.warp_scale + 41.0f, fy * cfg.warp_scale - 63.0f, 3) - 0.5f) * 2.0f;
            float warp_y = (fbm(fx * cfg.warp_scale - 97.0f, fy * cfg.warp_scale + 29.0f, 3) - 0.5f) * 2.0f;
            float wx = fx + warp_x * cfg.warp_strength;
            float wy = fy + warp_y * cfg.warp_strength;

            float macro = fbm(wx * cfg.macro_scale, wy * cfg.macro_scale, 6);
            float basin = 1.0f - fbm(wx * (cfg.macro_scale * 1.55f) + 1400.0f,
                                     wy * (cfg.macro_scale * 1.55f) + 1400.0f, 4);
            float ridge = ridged_fbm(wx * cfg.ridge_scale + 700.0f, wy * cfg.ridge_scale + 700.0f, 6);
            float valley = 1.0f - ridged_fbm(wx * cfg.valley_scale + 2500.0f, wy * cfg.valley_scale + 2500.0f, 4);
            float detail = fbm(wx * cfg.detail_scale + 3100.0f, wy * cfg.detail_scale + 3100.0f, 4);
            float hills = fbm(wx * (cfg.detail_scale * 0.52f) + 900.0f,
                              wy * (cfg.detail_scale * 0.52f) + 900.0f, 3);

            float mountain_w = smoothstep01(0.56f, 0.90f, ridge) * smoothstep01(0.38f, 0.88f, macro);
            float valley_w = smoothstep01(0.52f, 0.92f, valley) * (1.0f - mountain_w * 0.58f);
            float plateau_w = smoothstep01(cfg.plateau_level - 0.10f, cfg.plateau_level + 0.12f, macro) *
                              smoothstep01(0.35f, 0.74f, hills) * (1.0f - mountain_w * 0.75f);
            float plains_w = clamp01(1.0f - mountain_w - valley_w * 0.72f - plateau_w * 0.48f);

            float plains_h = 0.30f + (macro - 0.5f) * 0.12f + (hills - 0.5f) * 0.11f + (detail - 0.5f) * 0.07f;
            float valley_h = 0.24f + (macro - 0.5f) * 0.08f + (detail - 0.5f) * 0.05f - valley_w * 0.23f - basin * 0.08f;
            // Expoente mais baixo (1.85 -> 1.65) alarga o "planalto quase no topo" que sobrevive
            // as passadas de erosao termica/hidraulica abaixo - antes o pico ficava fino demais
            // (1-2 celulas) e a erosao sempre o achatava antes de cruzar snow_height.
            float mountain_h = 0.42f + std::pow(ridge, 1.65f) * 0.60f + (hills - 0.5f) * 0.08f;
            float plateau_h = 0.52f + std::pow(macro, 1.15f) * 0.30f + (detail - 0.5f) * 0.04f;
            plateau_h = lerp(plateau_h, std::floor(plateau_h * 9.0f) / 9.0f, cfg.plateau_flatten);

            float wsum = plains_w + valley_w + mountain_w + plateau_w + 0.0001f;
            float hn = (plains_h * plains_w + valley_h * valley_w + mountain_h * mountain_w + plateau_h * plateau_w) / wsum;
            hn += (macro - 0.5f) * cfg.macro_weight * 0.22f;
            hn += (ridge - 0.5f) * cfg.ridge_weight * 0.18f;
            hn -= valley_w * cfg.valley_weight * 0.15f;
            hn += (detail - 0.5f) * cfg.detail_weight;

            // Fendas e crateras suaves (antes da erosao para ficar natural).
            float fissure_line = std::fabs(perlin(wx * cfg.fissure_scale + 4300.0f, wy * cfg.fissure_scale + 4300.0f) - 0.5f);
            float fissure_cut = clamp01((0.018f - fissure_line) / 0.018f);
            float crater_shape = 1.0f - std::fabs(perlin(wx * cfg.crater_scale + 5200.0f, wy * cfg.crater_scale + 5200.0f) * 2.0f - 1.0f);
            float crater_core = smoothstep01(0.82f, 0.96f, crater_shape);
            float crater_rim = smoothstep01(0.62f, 0.80f, crater_shape) * (1.0f - crater_core);
            hn -= fissure_cut * cfg.fissure_depth;
            hn -= crater_core * cfg.crater_depth;
            hn += crater_rim * cfg.crater_depth * 0.42f;
            hn = clamp01(hn);

            float lat = 0.0f;
            if (h > 1) {
                float ny = (fy / (float)(h - 1)) * 2.0f - 1.0f;
                lat = std::fabs(ny);
            }

            float temp = fbm(wx * cfg.temp_scale + 900.0f, wy * cfg.temp_scale + 900.0f, 4);
            temp = clamp01(temp * 0.72f + (1.0f - lat) * 0.28f - hn * 0.38f);
            float moisture = fbm(wx * cfg.moisture_scale + 1300.0f, wy * cfg.moisture_scale + 1300.0f, 4);
            moisture = clamp01(moisture * 0.80f + basin * 0.20f);

            uint8_t biome = 0; // 0 Planicie | 1 Vale | 2 Montanha | 3 Plato | 4 Gelo
            if (hn > 0.72f && temp < 0.44f) biome = 4;
            else if (mountain_w >= valley_w && mountain_w >= plateau_w && mountain_w >= plains_w) biome = 2;
            else if (plateau_w >= valley_w && plateau_w >= plains_w) biome = 3;
            else if (valley_w >= plains_w) biome = 1;

            size_t idx = index_of(x, y);
            heights[idx] = hn;
            temp_map[idx] = temp;
            moist_map[idx] = moisture;
            ridge_map[idx] = ridge;
            valley_map[idx] = valley;
            biome_map[idx] = biome;
        }
    }

    // === Passo 1.5: vulcoes (relevo geometrico esparso - cone + cratera no pico, plantado
    // ANTES da erosao pra "assentar" e ficar com bordas naturais, igual as crateras de
    // impacto do Passo 1 acima). Sementes: maximos locais em celulas de bioma Montanha com
    // ridge bem alto, espacados pra nao empilhar 2 vulcoes vizinhos. Puramente cosmetico -
    // sem lava simulada, so um bloco Lava estatico no fundo da cratera (ver Passo 5/blocks.cpp).
    {
        struct SeedCandidate { int x, y; float score; float boost_mult = 1.0f; };
        std::vector<SeedCandidate> candidates;
        for (int y = 2; y < h - 2; ++y) {
            for (int x = 2; x < w - 2; ++x) {
                size_t i = index_of(x, y);
                // NAO exigir biome_map[i]==2 (Montanha) - medido com uma sonda standalone
                // (mesmas formulas deste arquivo): biome Montanha cobre so' ~0.18% do mapa
                // (e' uma classificacao por "quem venceu" entre 4 pesos independentes, nao
                // o mesmo criterio de elevacao que deixa o relevo visualmente montanhoso),
                // entao exigi-lo aqui reduzia os candidatos de ~18 mil pra ~30 e so' 2 dos 7
                // vulcoes pedidos sobreviviam ao espacamento minimo - o resto do mapa nunca
                // tinha vulcao nenhum pra achar. ridge+altura direto e' o criterio certo pra
                // "isso parece um pico" (o que realmente importa aqui).
                if (ridge_map[i] <= 0.60f || heights[i] <= 0.55f) continue;
                float hc = heights[i];
                bool is_max = true;
                for (int dy = -1; dy <= 1 && is_max; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        if (heights[index_of(x + dx, y + dy)] > hc) { is_max = false; break; }
                    }
                }
                if (is_max) candidates.push_back({x, y, hc});
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const SeedCandidate& a, const SeedCandidate& b) {
            return a.score > b.score;
        });

        int volcano_count = std::max(0, cfg.volcano_seed_count);
        // 220 -> 150: com o filtro de candidatos agora medido (~250+ picos elegiveis num
        // mapa de 3072x1536, ver comentario acima), 150 ja da espaco de sobra pra espalhar
        // os volcano_seed_count vulcoes pelo mapa inteiro sem 2 ficarem colados.
        const float kVolcanoMinSpacing2 = 150.0f * 150.0f;
        // NENHUM vulcao perto da base (pedido do jogador: "nao quero o vulcao muito proximo da
        // base"). A base nasce sempre dentro da caixa centro +-70 x +-45 (generate_base), ou seja
        // a <= ~83 tiles do centro do mapa - entao exigir 300 do CENTRO garante >= ~215 tiles entre
        // a cratera e a base. Folga de sobra: o cone tem raio 30 e os riachos de lava morrem em
        // poucas dezenas de passos. O mapa e' 3072x1536, entao 300 de exclusao sobra area enorme
        // para os vulcoes naturais.
        constexpr int kVolcanoBaseKeepOut = 300;
        constexpr float kVolcanoBaseKeepOut2 = (float)kVolcanoBaseKeepOut * (float)kVolcanoBaseKeepOut;
        std::vector<SeedCandidate> volcanoes;
        // Distribuicao ESPACIAL por celulas, nao "os N mais altos". Medido instrumentando a
        // geracao de verdade: os 7 vulcoes naturais sairam TODOS entre x=2370..3007 e
        // y=4..404 (canto nordeste), a 1118-1594 tiles do centro do mapa - o jogador nunca
        // ia topar com nenhum. Causa raiz: hn e' clampado em 1.0 (clamp01 no Passo 1) em
        // areas amplas de montanha, entao centenas de candidatos empatam com score
        // EXATAMENTE 1.0; std::sort nao e' estavel, o desempate virou a ordem de varredura
        // (row-major, y crescente) e a busca gulosa consumiu as 7 vagas no primeiro macico
        // que encontrou. Dividir o mapa em celulas e permitir no maximo 1 vulcao por celula
        // forca espalhamento independente de empate de score.
        const int kVolcanoGridCols = 4;
        const int kVolcanoGridRows = 3;
        std::vector<uint8_t> grid_used((size_t)kVolcanoGridCols * (size_t)kVolcanoGridRows, 0);
        auto grid_slot_of = [&](int x, int y) -> size_t {
            int gx = std::clamp(x * kVolcanoGridCols / std::max(1, w), 0, kVolcanoGridCols - 1);
            int gy = std::clamp(y * kVolcanoGridRows / std::max(1, h), 0, kVolcanoGridRows - 1);
            return (size_t)gy * (size_t)kVolcanoGridCols + (size_t)gx;
        };
        for (const auto& c : candidates) {
            if ((int)volcanoes.size() >= volcano_count) break;
            // Borda: um cone de raio 42 carimbado a 4 tiles da borda do mapa fica cortado pela
            // metade (3 dos 7 vulcoes medidos sairam com y=4/y=10, meio truncados).
            if (c.x < 60 || c.x >= w - 60 || c.y < 60 || c.y >= h - 60) continue;
            {   // exclusao em torno da base - ver kVolcanoBaseKeepOut acima
                float bdx = (float)(c.x - w / 2), bdy = (float)(c.y - h / 2);
                if (bdx * bdx + bdy * bdy < kVolcanoBaseKeepOut2) continue;
            }
            size_t slot = grid_slot_of(c.x, c.y);
            if (grid_used[slot]) continue;
            bool far_enough = true;
            for (const auto& v : volcanoes) {
                float dx = (float)(c.x - v.x), dy = (float)(c.y - v.y);
                if (dx * dx + dy * dy < kVolcanoMinSpacing2) { far_enough = false; break; }
            }
            if (!far_enough) continue;
            grid_used[slot] = 1;
            volcanoes.push_back(c);
        }

        // Garantia INCONDICIONAL: 1 vulcao PERTO da base, em terreno seco e alto, pra o
        // jogador achar um sem depender de sorte de seed nem de caminhar 1000 tiles.
        //
        // Historico (medido instrumentando a geracao real, nao suposicao): a versao anterior
        // carimbava numa posicao FIXA (w/2+200, h/2-70) = 212 tiles do centro. Naquele ponto
        // o terreno era uma bacia costeira: o vulcao saiu com o piso da cratera em altura 10
        // (ABAIXO do nivel do mar 20), com mar entre ele e a base, os 3 riachos de lava
        // morreram em 9-10 passos ("chegou no nivel do mar") e o anel r=5..9 da cratera foi
        // pintado de AGUA. Ou seja: existia, mas era um poco alagado do outro lado do mar.
        //
        // Agora procura o melhor ponto num ANEL ao redor do centro do mapa (a base sempre
        // nasce a <= ~70x45 do centro, ver generate_base()):
        //  - raio 300..390: o anel 85..115 que estava aqui punha o cone DENTRO do alcance de visao
        //    da base de proposito, e foi exatamente isso que o jogador rejeitou ("nao quero o vulcao
        //    muito proximo da base"). Agora e' o mesmo kVolcanoBaseKeepOut dos vulcoes naturais, ou
        //    seja >= ~215 tiles da base: continua sendo o vulcao mais perto do mapa e o unico com
        //    posicao garantida por seed, mas exige viajar ate ele - que e' o pedido.
        //  - exige terreno bem acima do nivel do mar (hn > sea_hn + 0.10) pra nao repetir o
        //    vulcao-lagoa: assim a cratera fica acima do mar e os riachos de lava tem desnivel
        //    de sobra pra escorrer de verdade.
        //  - escolhe o ponto mais ALTO do anel que satisfaca isso (encosta natural ajuda o
        //    cone a parecer parte do relevo em vez de um cone solto no plano).
        {
            int ccx = w / 2, ccy = h / 2;
            float sea_hn_guard = (float)(sea_h - min_h_i) / (float)(max_h_i - min_h_i) + 0.10f;
            int best_x = -1, best_y = -1;
            float best_h = -1.0f;
            for (int rr = kVolcanoBaseKeepOut; rr <= kVolcanoBaseKeepOut + 90; rr += 8) {
                // 24 direcoes por anel - amostragem suficiente pra achar a melhor encosta sem
                // varrer a area toda.
                for (int a = 0; a < 24; ++a) {
                    float ang = (float)a * (2.0f * 3.14159265f / 24.0f);
                    int cx2 = ccx + (int)std::lround(std::cos(ang) * (float)rr);
                    int cy2 = ccy + (int)std::lround(std::sin(ang) * (float)rr);
                    if (cx2 < 60 || cx2 >= w - 60 || cy2 < 60 || cy2 >= h - 60) continue;
                    float hn_here = heights[index_of(cx2, cy2)];
                    if (hn_here <= sea_hn_guard) continue; // seco e acima do mar, nao bacia
                    if (hn_here > best_h) { best_h = hn_here; best_x = cx2; best_y = cy2; }
                }
            }
            if (best_x >= 0) {
                // boost 1.5 (nao 2.2): em terreno ja elevado, 2.2 estourava o teto de altura e
                // era o unico vulcao com inclinacao acima do talus da erosao termica (0.021),
                // por isso o unico que perdia altura na erosao. 1.5 num ponto alto da' um cone
                // bem visivel que a erosao nem toca.
                volcanoes.push_back({best_x, best_y, 1.0f, 1.5f});
            }
        }

        // Raio 42 -> 30: o cone de 42 tiles com ~20 unidades de altura tinha uma inclinacao
        // muito mansa (grade ~0.5), lia como "morro largo" e nao como vulcao - o jogador
        // reclamou que "nao ta muito com cara de uma". Mais estreito na mesma altura = a
        // silhueta conica classica. Ainda abaixo do talus da erosao termica na maioria dos
        // casos, entao a erosao continua praticamente nao mexendo nele.
        const float kVolcanoRadius = 30.0f;
        const float kVolcanoHeightBoost = 0.30f;
        for (const auto& v : volcanoes) {
            volcano_centers.push_back({v.x, v.y});
            int rad = (int)kVolcanoRadius + 2;
            int x0 = std::max(1, v.x - rad), x1 = std::min(w - 2, v.x + rad);
            int y0 = std::max(1, v.y - rad), y1 = std::min(h - 2, v.y + rad);
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    float dx = (float)(x - v.x), dy = (float)(y - v.y);
                    float dist = std::sqrt(dx * dx + dy * dy);

                    float cone = smoothstep01(kVolcanoRadius, 0.0f, dist) * kVolcanoHeightBoost * v.boost_mult;

                    float crater_t = clamp01(1.0f - dist / kVolcanoCraterRadius);
                    float v_crater_core = smoothstep01(0.55f, 0.90f, crater_t);
                    float v_crater_rim = smoothstep01(0.25f, 0.55f, crater_t) * (1.0f - v_crater_core);

                    size_t i = index_of(x, y);
                    heights[i] = clamp01(heights[i] + cone -
                                          v_crater_core * (kVolcanoHeightBoost * v.boost_mult + 0.12f) +
                                          v_crater_rim * 0.08f);
                }
            }
        }

        // Chamines vulcanicas pequenas: espalhadas em biomas rochosos (Montanha=2/
        // Plato=3), longe da base/vulcoes grandes/umas das outras - amostragem por
        // rejeicao (mesmo espirito da selecao de POIs mais abaixo), sem alterar o
        // heightmap (so' um pontinho de lava, nao uma cratera).
        {
            const int kVentCount = 18;
            const float kVentMinSpacing2 = 70.0f * 70.0f;
            const float kVentVolcanoMinDist2 = 60.0f * 60.0f;
            const float kVentBaseExclusion2 = 150.0f * 150.0f;
            int cx = w / 2, cy = h / 2; // centro do mapa - a base sempre nasce por perto
            int attempts = 0;
            while ((int)vent_centers.size() < kVentCount && attempts < 4000) {
                ++attempts;
                int x = 2 + (int)(rng_next_u32() % (uint32_t)std::max(1, w - 4));
                int y = 2 + (int)(rng_next_u32() % (uint32_t)std::max(1, h - 4));
                size_t vi = index_of(x, y);
                uint8_t vbiome = biome_map[vi];
                if (vbiome != 2 && vbiome != 3) continue;

                float dxc = (float)(x - cx), dyc = (float)(y - cy);
                if (dxc * dxc + dyc * dyc < kVentBaseExclusion2) continue;

                bool ok = true;
                for (const auto& vc : volcano_centers) {
                    float dx = (float)(x - vc.first), dy = (float)(y - vc.second);
                    if (dx * dx + dy * dy < kVentVolcanoMinDist2) { ok = false; break; }
                }
                if (!ok) continue;
                for (const auto& vc : vent_centers) {
                    float dx = (float)(x - vc.first), dy = (float)(y - vc.second);
                    if (dx * dx + dy * dy < kVentMinSpacing2) { ok = false; break; }
                }
                if (!ok) continue;

                vent_centers.push_back({x, y});
            }
        }
    }

    // === Passo 2: erosao termica (remove "paredes") ===
    if (cfg.thermal_erosion_passes > 0) {
        std::vector<float> delta(cell_count, 0.0f);
        for (int pass = 0; pass < cfg.thermal_erosion_passes; ++pass) {
            std::fill(delta.begin(), delta.end(), 0.0f);
            for (int y = 1; y < h - 1; ++y) {
                for (int x = 1; x < w - 1; ++x) {
                    size_t i = index_of(x, y);
                    float h0 = heights[i];
                    const int nx[4] = {1, -1, 0, 0};
                    const int ny[4] = {0, 0, 1, -1};
                    for (int k = 0; k < 4; ++k) {
                        size_t j = index_of(x + nx[k], y + ny[k]);
                        float diff = h0 - heights[j];
                        if (diff > cfg.thermal_talus) {
                            float move = (diff - cfg.thermal_talus) * cfg.erosion_strength * 0.22f;
                            delta[i] -= move;
                            delta[j] += move;
                        }
                    }
                }
            }
            for (size_t i = 0; i < cell_count; ++i) {
                heights[i] = clamp01(heights[i] + delta[i]);
            }
        }
    }

    // === Passo 3: erosao hidrica simplificada (alarga vales/bacias) ===
    if (cfg.hydraulic_erosion_passes > 0) {
        std::vector<float> copy = heights;
        for (int pass = 0; pass < cfg.hydraulic_erosion_passes; ++pass) {
            copy = heights;
            for (int y = 1; y < h - 1; ++y) {
                for (int x = 1; x < w - 1; ++x) {
                    size_t i = index_of(x, y);
                    float center = copy[i];
                    float n = copy[index_of(x, y - 1)];
                    float s = copy[index_of(x, y + 1)];
                    float e = copy[index_of(x + 1, y)];
                    float wv = copy[index_of(x - 1, y)];
                    float ne = copy[index_of(x + 1, y - 1)];
                    float nw = copy[index_of(x - 1, y - 1)];
                    float se = copy[index_of(x + 1, y + 1)];
                    float sw = copy[index_of(x - 1, y + 1)];
                    float avg = (center * 2.0f + n + s + e + wv + ne + nw + se + sw) / 10.0f;
                    float min_n = std::min({center, n, s, e, wv, ne, nw, se, sw});
                    float slope = center - min_n;
                    float valley_boost = smoothstep01(0.60f, 0.95f, valley_map[i]) * 0.16f;
                    float blend = std::clamp(cfg.erosion_strength * (0.11f + slope * 1.1f) + valley_boost, 0.0f, 0.45f);
                    heights[i] = clamp01(lerp(center, avg, blend));
                }
            }
        }
    }

    // === Passo 4: suavizacao final das encostas ===
    if (cfg.smooth_passes > 0) {
        std::vector<float> copy = heights;
        for (int pass = 0; pass < cfg.smooth_passes; ++pass) {
            copy = heights;
            for (int y = 1; y < h - 1; ++y) {
                for (int x = 1; x < w - 1; ++x) {
                    size_t i = index_of(x, y);
                    float avg4 = (copy[index_of(x - 1, y)] + copy[index_of(x + 1, y)] +
                                  copy[index_of(x, y - 1)] + copy[index_of(x, y + 1)]) * 0.25f;
                    heights[i] = clamp01(lerp(copy[i], avg4, 0.15f + cfg.biome_blend * 0.18f));
                }
            }
        }
    }

    // === Passo 3.5: rios e lagos (tracado por steepest-descent a partir de picos de
    // montanha, DEPOIS de toda erosao/suavizacao pra o canal nao ser borrado de volta).
    // river_map e so um array de trabalho desta funcao (nao um campo de World) - o resultado
    // final vira Block::Water/Ice normal nos arrays tiles/ground do Passo 5, exatamente como
    // a agua do nivel do mar ja funciona hoje, entao nao precisa de nenhuma mudanca de save.
    std::vector<uint8_t> river_map(cell_count, 0);
    {
        struct RiverSeed { int x, y; float score; };
        std::vector<RiverSeed> candidates;
        for (int y = 2; y < h - 2; ++y) {
            for (int x = 2; x < w - 2; ++x) {
                size_t i = index_of(x, y);
                if (biome_map[i] != 2 || heights[i] < 0.55f) continue;
                float hc = heights[i];
                bool is_max = true;
                for (int dy = -1; dy <= 1 && is_max; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        if (heights[index_of(x + dx, y + dy)] > hc) { is_max = false; break; }
                    }
                }
                if (is_max) candidates.push_back({x, y, hc});
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const RiverSeed& a, const RiverSeed& b) {
            return a.score > b.score;
        });

        int river_count = std::max(0, cfg.river_seed_count);
        const float kRiverMinSpacing2 = 150.0f * 150.0f;
        std::vector<RiverSeed> seeds;
        for (const auto& c : candidates) {
            if ((int)seeds.size() >= river_count) break;
            bool far_enough = true;
            for (const auto& s : seeds) {
                float dx = (float)(c.x - s.x), dy = (float)(c.y - s.y);
                if (dx * dx + dy * dy < kRiverMinSpacing2) { far_enough = false; break; }
            }
            if (far_enough) seeds.push_back(c);
        }

        // Preenche um "lago" limitado (BFS num raio pequeno, sem watershed de mapa inteiro)
        // quando um rio para num poco sem saida - nivela tudo que ficou abaixo da borda de
        // saida mais baixa encontrada.
        auto flood_fill_lake = [&](int sx, int sy) {
            const int R = 60;
            int gx0 = std::max(1, sx - R), gx1 = std::min(w - 2, sx + R);
            int gy0 = std::max(1, sy - R), gy1 = std::min(h - 2, sy + R);
            int gw = gx1 - gx0 + 1;
            auto local_idx = [&](int x, int y) -> size_t {
                return (size_t)(y - gy0) * (size_t)gw + (size_t)(x - gx0);
            };
            std::vector<uint8_t> visited((size_t)gw * (size_t)(gy1 - gy0 + 1), 0);
            std::vector<std::pair<int, int>> queue;
            std::vector<std::pair<int, int>> filled;
            queue.push_back({sx, sy});
            visited[local_idx(sx, sy)] = 1;
            float sink_h = heights[index_of(sx, sy)];
            float rim_height = 2.0f;
            const int kNx4[4] = {1, -1, 0, 0};
            const int kNy4[4] = {0, 0, 1, -1};
            size_t qi = 0;
            while (qi < queue.size() && filled.size() < 2500) {
                auto [px, py] = queue[qi++];
                filled.push_back({px, py});
                for (int k = 0; k < 4; ++k) {
                    int nx2 = px + kNx4[k], ny2 = py + kNy4[k];
                    if (nx2 < gx0 || nx2 > gx1 || ny2 < gy0 || ny2 > gy1) continue;
                    size_t li = local_idx(nx2, ny2);
                    if (visited[li]) continue;
                    visited[li] = 1;
                    float nh = heights[index_of(nx2, ny2)];
                    if (nh <= sink_h + 0.05f) {
                        queue.push_back({nx2, ny2});
                    } else {
                        rim_height = std::min(rim_height, nh);
                    }
                }
            }
            // Nivela TODO o lago numa unica altura (a da borda de saida mais baixa) - um lago
            // de verdade se acomoda num unico nivel plano, nao segue o relevo irregular do
            // fundo tile a tile. Antes disso, cada tile so era clampado (min(altura_propria,
            // borda)) e mantinha sua propria altura de heightmap quando ja estava abaixo da
            // borda - o lago ficava um fundo irregular ("buraco"), e tiles vizinhos com
            // alturas quase iguais mas nao identicas geravam paredes de altura quase-zero que
            // davam z-fighting (piscando) nas bordas da agua.
            float lake_hn = rim_height - 0.01f;
            for (const auto& p : filled) {
                size_t i = index_of(p.first, p.second);
                river_map[i] = 1;
                heights[i] = lake_hn;
            }
        };

        const int kNx8[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        const int kNy8[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        float sea_hn = (float)(sea_h - min_h_i) / (float)(max_h_i - min_h_i);

        for (const auto& seed : seeds) {
            int cx = seed.x, cy = seed.y;
            for (int step = 0; step < 4000; ++step) {
                size_t ci = index_of(cx, cy);
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        int nx2 = cx + ox, ny2 = cy + oy;
                        if (nx2 < 1 || nx2 >= w - 1 || ny2 < 1 || ny2 >= h - 1) continue;
                        size_t ni = index_of(nx2, ny2);
                        river_map[ni] = 1;
                        heights[ni] = clamp01(std::min(heights[ni], heights[ci]) - 0.012f);
                    }
                }

                if (heights[ci] <= sea_hn) break; // chegou ao mar

                int best_nx = -1, best_ny = -1;
                float best_h = heights[ci];
                for (int k = 0; k < 8; ++k) {
                    int nx2 = cx + kNx8[k], ny2 = cy + kNy8[k];
                    if (nx2 < 1 || nx2 >= w - 1 || ny2 < 1 || ny2 >= h - 1) continue;
                    float nh = heights[index_of(nx2, ny2)];
                    if (nh < best_h) { best_h = nh; best_nx = nx2; best_ny = ny2; }
                }

                if (best_nx < 0) {
                    flood_fill_lake(cx, cy); // poco sem saida - vira lago, encerra o rio
                    break;
                }
                cx = best_nx;
                cy = best_ny;
            }
        }
    }

    // === Passo 3.6: rios de lava escorrendo vulcao abaixo ("vulcao derramando lava" -
    // pedido do jogador) - mesma tecnica de steepest-descent dos rios acima (precisa rodar
    // depois da erosao/suavizacao, senao o canal fica borrado de volta), so' que a partir da
    // borda da cratera de cada vulcao grande em vez de picos de montanha, e sem carvar o
    // heightmap (lava escorre POR CIMA do relevo existente do cone, nao abre canal, igual a
    // piscina estatica da cratera ja funciona hoje). lava_flow_map e' lido no Passo 5 junto
    // de volcano_centers/vent_centers (mesmo bool in_volcano_lava).
    std::vector<uint8_t> lava_flow_map(cell_count, 0);
    if (!volcano_centers.empty()) {
        const int kNx8L[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        const int kNy8L[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        float sea_hn = (float)(sea_h - min_h_i) / (float)(max_h_i - min_h_i);
        // 6 riachos de lava por vulcao (era 3), saindo da borda da cratera em direcoes bem
        // espalhadas - mais riachos = muito mais chance de o jogador cruzar com um, e o cone
        // fica visualmente "sangrando" lava de varios lados em vez de ter um risquinho so.
        // Raio 12: fica logo fora da cratera (kVolcanoCraterRadius=9) e bem dentro do cone
        // (kVolcanoRadius=42), na parte mais inclinada da encosta - e' onde o steepest-descent
        // tem desnivel de sobra pra correr longe morro abaixo.
        const int kFlowStartOffsets[6][2] = {
            {12, 0}, {-12, 0}, {0, 12}, {0, -12}, {9, 9}, {-9, -9}
        };

        for (const auto& vc : volcano_centers) {
            for (const auto& off : kFlowStartOffsets) {
                int cx = vc.first + off[0];
                int cy = vc.second + off[1];
                if (cx < 2 || cx >= w - 2 || cy < 2 || cy >= h - 2) continue;

                // 60 -> 220 passos: medido na geracao real, varios riachos batiam no teto de
                // 60 ainda descendo (bail=0), ou seja o comprimento estava sendo cortado pelo
                // limite e nao pelo relevo. Lava escorrendo montanha abaixo por 200 tiles e'
                // exatamente o "lava escorrendo" que o jogador pediu.
                for (int step = 0; step < 220; ++step) {
                    size_t ci = index_of(cx, cy);
                    if (river_map[ci]) break; // nao invade rio/lago/mar ja tracado

                    // Largura do riacho: 3x3 (raio 1) no comeco, afinando pra 1 tile depois de
                    // uns 60 passos - perto do vulcao e' um rio de lava largo, longe vira um
                    // fio, como lava de verdade esfriando/estreitando.
                    int flow_r = (step < 60) ? 1 : 0;
                    for (int oy = -flow_r; oy <= flow_r; ++oy) {
                        for (int ox = -flow_r; ox <= flow_r; ++ox) {
                            int nx2 = cx + ox, ny2 = cy + oy;
                            if (nx2 < 1 || nx2 >= w - 1 || ny2 < 1 || ny2 >= h - 1) continue;
                            size_t ni = index_of(nx2, ny2);
                            if (!river_map[ni]) lava_flow_map[ni] = 1;
                        }
                    }

                    if (heights[ci] <= sea_hn + 0.02f) break; // nao chega no nivel do mar

                    int best_nx = -1, best_ny = -1;
                    float best_h = heights[ci];
                    for (int k = 0; k < 8; ++k) {
                        int nx2 = cx + kNx8L[k], ny2 = cy + kNy8L[k];
                        if (nx2 < 1 || nx2 >= w - 1 || ny2 < 1 || ny2 >= h - 1) continue;
                        float nh = heights[index_of(nx2, ny2)];
                        if (nh < best_h) { best_h = nh; best_nx = nx2; best_ny = ny2; }
                    }
                    if (best_nx < 0) {
                        // Poco local (nenhum vizinho mais baixo). Antes: "break" imediato - e'
                        // por isso que a maioria dos riachos medidos morria em 10-40 passos
                        // (bail=3, muito antes do limite de passos). Erosao/suavizacao deixam o
                        // terreno cheio de minimos locais rasos de 1 unidade; lava de verdade
                        // enche o pocinho e transborda. Aqui: procura num raio 3 o vizinho mais
                        // baixo (mesmo que na mesma altura) pra "transbordar" e seguir descendo,
                        // e so' desiste se estiver mesmo numa bacia fechada.
                        float spill_h = heights[ci] + 0.0001f;
                        for (int oy = -3; oy <= 3 && best_nx < 0; ++oy) {
                            for (int ox = -3; ox <= 3; ++ox) {
                                if (ox == 0 && oy == 0) continue;
                                int nx2 = cx + ox, ny2 = cy + oy;
                                if (nx2 < 1 || nx2 >= w - 1 || ny2 < 1 || ny2 >= h - 1) continue;
                                size_t ni2 = index_of(nx2, ny2);
                                if (lava_flow_map[ni2] || river_map[ni2]) continue; // nao volta por onde veio
                                if (heights[ni2] <= spill_h) {
                                    best_nx = nx2; best_ny = ny2;
                                    break;
                                }
                            }
                        }
                        if (best_nx < 0) break; // bacia fechada de verdade - vira piscina de lava
                    }
                    cx = best_nx;
                    cy = best_ny;
                }
            }
        }
    }

    // === Passo 5: converter heightmap e definir solo por bioma ===
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t i = index_of(x, y);
            float hn = heights[i];
            int h_val = min_h_i + (int)std::lround(hn * (float)(max_h_i - min_h_i));
            int16_t th = (int16_t)std::clamp(h_val, min_h_i, max_h_i);

            float temp = temp_map[i];
            float moisture = moist_map[i];
            uint8_t biome = biome_map[i];

            Block g = Block::Dirt;
            bool in_volcano_lava = false;
            for (const auto& vc : volcano_centers) {
                float vdx = (float)(x - vc.first), vdy = (float)(y - vc.second);
                if (vdx * vdx + vdy * vdy <= kVolcanoLavaRadius * kVolcanoLavaRadius) {
                    in_volcano_lava = true;
                    break;
                }
            }
            if (!in_volcano_lava) {
                for (const auto& vc : vent_centers) {
                    float vdx = (float)(x - vc.first), vdy = (float)(y - vc.second);
                    if (vdx * vdx + vdy * vdy <= kVentLavaRadius * kVentLavaRadius) {
                        in_volcano_lava = true; // reusa o mesmo nome/ramo - mesmo efeito (Lava)
                        break;
                    }
                }
            }
            if (!in_volcano_lava && lava_flow_map[i]) in_volcano_lava = true; // riacho de lava (Passo 3.6)
            if (in_volcano_lava) {
                g = Block::Lava;
            } else if (river_map[i] || (int)th <= sea_h) {
                g = (temp < 0.44f) ? Block::Ice : Block::Water;
                // Mar aberto (nao veio de rio/lago, que ja foram nivelados acima) tambem
                // precisa ser uma superficie plana no nivel do mar - mesma razao do
                // achatamento de lago em flood_fill_lake: sem isso, qualquer tile que
                // simplesmente ficou baixo o bastante no heightmap virava agua na sua
                // propria altura crua (bumpy), lendo como buraco em vez de mar/lago, e
                // causando paredes quase-zero (z-fighting/piscando) entre tiles de agua
                // vizinhos com alturas quase iguais.
                if (!river_map[i]) th = (int16_t)sea_h;
            } else if (biome == 4 || (int)th >= snow_h || temp < 0.25f) {
                float snow_var = fbm((float)x * 0.045f + 7600.0f, (float)y * 0.045f + 7600.0f, 2);
                g = (snow_var > 0.56f) ? Block::Ice : Block::Snow;
            } else if (biome == 1 && moisture > 0.66f) {
                g = Block::Dirt; // vale mais umido
            } else if (moisture < 0.30f && temp > 0.52f) {
                g = Block::Sand;
            } else if (biome == 3 && moisture < 0.36f) {
                g = Block::Sand;
            } else {
                g = Block::Dirt;
            }

            set_height(x, y, th);
            set_ground(x, y, g);
            set(x, y, g);
        }
    }

    // === Passo 6: detalhamento (rochas, fendas, pedregulhos, minerios) ===
    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            Block g = get_ground(x, y);
            int16_t th = height_at(x, y);
            // Cobre mar + rios/lagos (agua acima do nivel do mar) + gelo + lava - nenhum
            // desses deve ganhar minerio/rocha/cristal/organico por cima.
            if (g == Block::Water || g == Block::Ice || g == Block::Lava) continue;

            float fx = (float)x;
            float fy = (float)y;
            float ridge = ridge_map[index_of(x, y)];

            float h_c = (float)height_at(x, y);
            float h_e = (float)height_at(x + 1, y);
            float h_w = (float)height_at(x - 1, y);
            float h_n = (float)height_at(x, y - 1);
            float h_s = (float)height_at(x, y + 1);
            float slope = std::sqrt((h_e - h_w) * (h_e - h_w) + (h_s - h_n) * (h_s - h_n));

            float rock_n = fbm(fx * 0.060f + 2100.0f, fy * 0.060f + 2100.0f, 3);
            float boulder_n = fbm(fx * 0.022f + 3300.0f, fy * 0.022f + 3300.0f, 2);
            float fissure = std::fabs(perlin(fx * (cfg.fissure_scale * 1.65f) + 5200.0f,
                                            fy * (cfg.fissure_scale * 1.65f) + 5200.0f) - 0.5f);

            float obj_bias = rock_n + ridge * 0.55f + slope * 0.020f + cfg.detail_object_density;
            if (obj_bias > 1.30f || (boulder_n > 0.79f && slope > 2.1f)) {
                set(x, y, Block::Stone);
                continue;
            }

            float ore1 = fbm(fx * 0.11f + 200.0f, fy * 0.11f + 200.0f, 3);
            float ore2 = fbm(fx * 0.09f + 300.0f, fy * 0.09f + 300.0f, 3);
            float ore3 = fbm(fx * 0.14f + 400.0f, fy * 0.14f + 400.0f, 2);
            // Campos de cristal bioluminescente: ruido de baixa frequencia (era minerio
            // esparso via ore3>0.91) vira uma zona contigua e reconhecivel no bioma gelado,
            // em vez de "specks" isolados - ore3 continua intocado (ainda alimenta o limiar
            // de Metal abaixo).
            float crystal_field = fbm(fx * 0.020f + 900.0f, fy * 0.020f + 900.0f, 3);

            if (ore1 > 0.88f && (int)th > sea_h + 2) {
                set(x, y, Block::Iron);
            } else if (ore1 > 0.85f && (int)th > sea_h + 1) {
                set(x, y, Block::Coal);
            } else if (ore2 > 0.89f && (int)th > sea_h + 2) {
                set(x, y, Block::Copper);
            } else if (crystal_field > 0.58f && (g == Block::Snow || (int)th > snow_h - 2)) {
                set(x, y, Block::Crystal);
            } else if (ore2 > 0.93f && ore3 > 0.93f) {
                set(x, y, Block::Metal);
            } else if (fissure < 0.014f && (int)th > sea_h + 3) {
                set(x, y, Block::Coal); // fendas escuras
            }

            if (get(x, y) == get_ground(x, y) && (int)th > sea_h + 1 && (int)th < snow_h - 2) {
                float moisture = moist_map[index_of(x, y)];
                // Bolsoes de flora alienigena: mesmo tratamento - ruido de baixa frequencia
                // (era org>0.92 a 0.10 de escala) vira clareiras verdes reconheciveis na
                // faixa temperada/umida, mantendo o mesmo gate de umidade de antes.
                float organic_field = fbm(fx * 0.018f + 950.0f, fy * 0.018f + 950.0f, 3);
                if (moisture > 0.70f && organic_field > 0.55f) {
                    set(x, y, Block::Organic);
                }
            }

            if (get(x, y) == get_ground(x, y)) {
                float dry = 1.0f - moist_map[index_of(x, y)];
                float tech = fbm(fx * 0.083f + 4200.0f, fy * 0.083f + 4200.0f, 2);
                if (dry > 0.60f && tech > 0.93f) {
                    set(x, y, Block::Components);
                }
            }
        }
    }

    // === Passo 7: Pontos de Interesse (POIs) - pequenos caches de loot pra dar um motivo
    // real pra explorar alem de minerio repetido (levantamento desta sessao: "o mundo nao
    // tem nada pra descobrir"). Decoracao com blocos ja existentes no jogo, ciclando 4
    // arquetipos visuais (RocketNose/Fin/Window "sonda destruida", Antenna/DomeFrame/PipeH/
    // PipeV "rele antigo", RocketHull/Engine/Door "casco esmagado", DomeGlass/LandingPad
    // "cupula soterrada") - ja tem textura/cor/is_base_structure completos (blocks.cpp/
    // textures.cpp), entao ja saem nao-mineraveis de graca (so decoracao) e o minimapa ja
    // colore especial qualquer tile is_base_structure - o POI vira visivel ao ser explorado,
    // sem UI nova (e o scanner - tecla T, minimap.cpp - acha o mais proximo de proposito). O
    // loot em si e minerio de verdade cravado no terreno (Crystal/Metal/Components via set()),
    // NAO um ItemDrop solto - g_drops nunca e salvo (save_load.cpp so faz clear() no load),
    // enquanto blocos de tile ja tem save/load de graca e mineracao ja dispara
    // spawn_item_drop() pelo caminho normal (building_interaction.cpp), zero mudanca la.
    {
        constexpr int kPoiCount = 8; // era 4 - validado por simulacao que o orcamento de
                                     // 400 tentativas (mesmas regras abaixo) da conta
        // Longe do centro do mapa - a base sempre nasce dentro de uma caixa ~+-70x+-45 ao
        // redor desse centro (generate_base(), modules_building.cpp) - e longe uns dos
        // outros, mesmo espirito de kVolcanoMinSpacing2 acima (Passo 1.5).
        constexpr float kPoiMinDistFromCenter2 = 180.0f * 180.0f;
        constexpr float kPoiMinSpacing2 = 220.0f * 220.0f;
        std::vector<std::pair<int, int>> poi_centers;
        int cx_map = w / 2, cy_map = h / 2;

        int attempts = 0;
        while ((int)poi_centers.size() < kPoiCount && attempts < 400) {
            ++attempts;
            int px = 60 + (int)(rng_next_u32() % (uint32_t)std::max(1, w - 120));
            int py = 60 + (int)(rng_next_u32() % (uint32_t)std::max(1, h - 120));

            float dcx = (float)(px - cx_map), dcy = (float)(py - cy_map);
            if (dcx * dcx + dcy * dcy < kPoiMinDistFromCenter2) continue;

            bool far_enough = true;
            for (const auto& p : poi_centers) {
                float dx = (float)(px - p.first), dy = (float)(py - p.second);
                if (dx * dx + dy * dy < kPoiMinSpacing2) { far_enough = false; break; }
            }
            if (!far_enough) continue;

            Block surf = get_ground(px, py);
            if (surf == Block::Water || surf == Block::Ice || surf == Block::Lava) continue;

            int he = (int)height_at(px + 1, py), hw = (int)height_at(px - 1, py);
            int hn = (int)height_at(px, py - 1), hs = (int)height_at(px, py + 1);
            int slope = std::abs(he - hw) + std::abs(hs - hn);
            if (slope > 6) continue; // muito inclinado

            poi_centers.push_back({px, py});
        }

        for (size_t i = 0; i < poi_centers.size(); ++i) {
            int px = poi_centers[i].first, py = poi_centers[i].second;
            int archetype = (int)(i % 4);

            switch (archetype) {
                case 0: // "sonda destruida"
                    set(px, py, Block::RocketNose);
                    set(px + 1, py, Block::RocketFin);
                    set(px - 1, py, Block::RocketFin);
                    set(px, py + 1, Block::RocketWindow);
                    break;
                case 1: // "rele antigo"
                    set(px, py, Block::Antenna);
                    set(px + 1, py, Block::DomeFrame);
                    set(px - 1, py, Block::DomeFrame);
                    set(px, py + 1, Block::PipeH);
                    set(px, py - 1, Block::PipeV);
                    break;
                case 2: // "casco esmagado" - casco largo (3 tiles) com motor e escotilha
                    set(px, py, Block::RocketHull);
                    set(px + 1, py, Block::RocketHull);
                    set(px - 1, py, Block::RocketHull);
                    set(px, py + 1, Block::RocketEngine);
                    set(px, py - 1, Block::RocketDoor);
                    break;
                default: // "cupula soterrada" - cupula com destroços de plataforma ao redor
                    set(px, py, Block::DomeGlass);
                    set(px + 1, py, Block::LandingPad);
                    set(px - 1, py, Block::LandingPad);
                    set(px, py + 1, Block::LandingPad);
                    set(px, py - 1, Block::LandingPad);
                    break;
            }

            static const int kLootOffsets[5][2] = {{2, 0}, {-2, 0}, {0, 2}, {0, -2}, {1, 1}};
            static const Block kLootBlocks[5] = {
                Block::Crystal, Block::Crystal, Block::Metal, Block::Metal, Block::Components
            };
            for (int j = 0; j < 5; ++j) {
                int lx = px + kLootOffsets[j][0];
                int ly = py + kLootOffsets[j][1];
                if (!in_bounds(lx, ly)) continue;
                Block lsurf = get_ground(lx, ly);
                if (lsurf == Block::Water || lsurf == Block::Ice || lsurf == Block::Lava) continue;
                set(lx, ly, kLootBlocks[j]);
            }
        }
    }

    // === Passo 7.5: rebordo decorativo das chamines vulcanicas pequenas (ver vent_centers,
    // Passo 1.5) - so' pedras crua ao redor do pontinho de lava, a ~2 tiles (o proprio raio
    // de lava, kVentLavaRadius=1.5, ja cobre os vizinhos ortogonais/diagonais imediatos) -
    // da o "chamine" sem esculpir heightmap nenhum.
    for (const auto& vc : vent_centers) {
        int vx = vc.first, vy = vc.second;
        static const int kRimOffsets[4][2] = {{2, 0}, {-2, 0}, {0, 2}, {0, -2}};
        for (const auto& off : kRimOffsets) {
            int rx = vx + off[0], ry = vy + off[1];
            if (!in_bounds(rx, ry)) continue;
            Block rsurf = get_ground(rx, ry);
            if (rsurf == Block::Water || rsurf == Block::Ice || rsurf == Block::Lava) continue;
            set(rx, ry, Block::Stone);
        }
    }

    rebuild_surface_cache();
}

// Altura adicional de um bloco acima do terreno (para colisao/ground height).
float get_block_height(Block b) {
    if (b == Block::Air) return 0.0f;
    if (is_ground_like(b)) return 0.0f; // Solo (inclui agua/gelo), sem volume acima
    if (b == Block::Leaves) return 0.0f; // Folhagem e tratada como plano

    // Colisao de mobilia: ANTES dos testes genericos abaixo (que devolvem 1.0 pra tudo). E' o unico
    // lugar do motor com altura sub-1.0, e e' exatamente por isso que a mobilia mora na slot de
    // OBJETO e nao numa camada de pilha: camada de pilha e' fixa em 1.0 em render E colisao, entao
    // o jogador pisaria 1.0 acima do movel (flutuando sobre a cama). Aqui a altura de colisao casa
    // com a altura desenhada em base_interior.cpp.
    if (b == Block::FurnitureLow) return 0.70f;   // cama, caixas, bancos
    if (b == Block::FurnitureMid) return 1.10f;   // mesa, bancada, console, pia
    if (b == Block::FurnitureTall) return 2.10f;  // armario, tanque, prateleira
    if (b == Block::FurnitureHuge) return 4.20f;  // maquinario/reator/tanque industrial

    // Objetos (rochas/minerios/modulos/estruturas): cubo 1x1x1 sobre o solo.
    // Se quiser modulos mais altos no futuro, troque por um box/prisma (nao cubo uniforme).
    if (is_module(b)) return 1.0f;
    if (is_base_structure(b)) return 1.0f;
    if (is_solid(b)) return 1.0f;
    return 0.0f;
}

Block surface_block_at(const World& world, int tx, int tz) {
    Block top = world.get(tx, tz);
    if (top != Block::Air && is_ground_like(top)) return top;
    return world.get_ground(tx, tz);
}

Block object_block_at(const World& world, int tx, int tz) {
    Block top = world.get(tx, tz);
    if (top != Block::Air && !is_ground_like(top)) return top;
    return Block::Air;
}

float surface_height_at(const World& world, int tx, int tz) {
    float h = (float)world.height_at(tx, tz) * kHeightScale;
    Block obj = object_block_at(world, tx, tz);
    if (obj != Block::Air) h += get_block_height(obj);
    return h;
}

float stack_top_height_at(const World& world, int tx, int tz) {
    return surface_height_at(world, tx, tz) + (float)world.stack_height_at(tx, tz) * 1.0f;
}

Block stack_top_block_at(const World& world, int tx, int tz) {
    int sh = world.stack_height_at(tx, tz);
    if (sh > 0) return world.stack_block_at(tx, tz, sh - 1);
    // Sem pilha: mesma regra original de surface_block_at (ignora objetos soltos como
    // rochas/minerios e retorna o tipo de solo/bioma) - preserva o comportamento de
    // terrain_type_from_block (gelo/areia/lama) que ja existia antes do empilhamento.
    return surface_block_at(world, tx, tz);
}

bool is_mineable(Block b) {
    // LAVA e' mineravel: o jogador pediu pra poder coleta-la. Agua continua fora - nao ha recipiente
    // nem uso pra ela no inventario, e "minerar agua" leria como bug.
    if (b == Block::Air || b == Block::Water) return false;
    if (is_base_structure(b)) return false;
    return true;
}

int block_hits_required(Block b) {
    switch (b) {
        case Block::Sand:
            return g_mining_cfg.hits_sand;
        case Block::Grass:
        case Block::Dirt:
        case Block::Organic:
        case Block::Leaves:
        case Block::BuildSlot:
            return g_mining_cfg.hits_dirt;
        case Block::Ice:
            return g_mining_cfg.hits_ice;
        case Block::Snow:
            return g_mining_cfg.hits_snow;
        case Block::Stone:
            return g_mining_cfg.hits_stone;
        case Block::Basalt:
            // Crosta de lava resfriada: um pouco mais duro que pedra comum.
            return g_mining_cfg.hits_stone + 1;
        case Block::Lava:
            // Mais golpes que pedra: recolher lava e' lento e perigoso (o dano por contato ja existe,
            // ver a queimadura em main.cpp) - nao pode ser mais barato que cavar terra.
            return g_mining_cfg.hits_stone + 2;
        case Block::Coal:
        case Block::Iron:
        case Block::Copper:
            return g_mining_cfg.hits_ore;
        case Block::Metal:
        case Block::Components:
            return g_mining_cfg.hits_metal;
        case Block::Crystal:
            return g_mining_cfg.hits_crystal;
        case Block::Wood:
            return g_mining_cfg.hits_wood;
        default:
            break;
    }
    if (is_module(b)) return g_mining_cfg.hits_modules;
    return std::max(2, g_mining_cfg.hits_stone);
}

// ============= Terraforming Simulation =============
void try_spawn_tree(World& world, int x, int y) {
    // Planeta inospito no comeco: so gera vegetacao em fase habitavel/terraformed.
    if (g_phase < TerraPhase::Habitable) return;
    if (x < 2 || x >= world.w - 2 || y < 2 || y >= world.h - 2) return;

    // Apenas em grama e sem objetos/estruturas/modulos.
    if (world.get_ground(x, y) != Block::Grass) return;
    if (is_base_structure(world.get_ground(x, y))) return;
    if (object_block_at(world, x, y) != Block::Air) return;

    // Evitar encostar em modulos/base (mantem legibilidade e evita conflito com construcoes).
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            Block t = world.get(x + dx, y + dy);
            if (is_module(t) || is_base_structure(t)) return;
        }
    }

    // Arvore simples estilo Minicraft: tronco (cubo) + copa (folhas em volta).
    world.set(x, y, Block::Wood);
    for (int oy = -2; oy <= 2; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            if (std::abs(ox) + std::abs(oy) > 3) continue;
            int tx = x + ox;
            int ty = y + oy;
            if (!world.in_bounds(tx, ty)) continue;
            if (tx == x && ty == y) continue;

            Block cur = world.get(tx, ty);
            if (is_module(cur) || is_base_structure(cur)) continue;
            if (object_block_at(world, tx, ty) != Block::Air) continue;

            world.set(tx, ty, Block::Leaves);
        }
    }
}

void terraform_step(World& world, int cx, int cy) {
    int radius = 10;
    for (int i = 0; i < 3; ++i) {
        float ang = rng_next_f01() * 6.2831853f;
        float rr = rng_next_f01() * radius;
        int x = cx + (int)std::round(std::cos(ang) * rr);
        int y = cy + (int)std::round(std::sin(ang) * rr);
        if (!world.in_bounds(x, y)) continue;

        Block top = world.get(x, y);
        if (is_module(top) || is_base_structure(top)) continue;
        if (is_base_structure(world.get_ground(x, y))) continue;

        auto set_ground_surface = [&](int tx, int ty, Block nb) {
            world.set_ground(tx, ty, nb);
            Block t = world.get(tx, ty);
            if (t != Block::Air && is_ground_like(t) && !is_base_structure(t) && !is_module(t)) {
                world.set(tx, ty, nb);
            }
        };

        Block g = world.get_ground(x, y);

        if (g == Block::Sand && g_oxygen >= 12.0f && g_water_res >= 12.0f) {
            set_ground_surface(x, y, Block::Dirt);
            g_surface_dirty = true;
        } else if (g == Block::Dirt && g_phase >= TerraPhase::Habitable &&
            g_oxygen >= 28.0f && g_water_res >= 18.0f) {
            set_ground_surface(x, y, Block::Grass);
            g_surface_dirty = true;
        } else if (g == Block::Grass && g_phase >= TerraPhase::Habitable &&
            g_oxygen >= 45.0f && g_water_res >= 35.0f) {
            if ((rng_next_u32() % 100u) < 2u) {
                try_spawn_tree(world, x, y);
                g_surface_dirty = true;
            }
        }
    }
}

void recompute_terraform_score(World& world) {
    int grass_tiles = 0;
    int tree_tiles = 0;
    int water_tiles = 0;

    for (int y = 0; y < world.h; ++y) {
        for (int x = 0; x < world.w; ++x) {
            Block g = world.get_ground(x, y);
            if (g == Block::Grass) grass_tiles++;
            if (g == Block::Water) water_tiles++;

            Block obj = object_block_at(world, x, y);
            if (obj == Block::Wood) tree_tiles++;
        }
    }

    float total = (float)std::max(1, world.w * world.h);
    float grass = (float)grass_tiles / total;
    float trees = (float)tree_tiles / total;
    float water = (float)water_tiles / total;

    float base = grass * 60.0f + trees * 20.0f + water * 20.0f;
    float env = 0.4f + 0.6f * (0.5f * clamp01(g_oxygen / 100.0f) + 0.5f * clamp01(g_water_res / 100.0f));
    g_terraform = std::clamp(base * env, 0.0f, 100.0f);
    // Victory is no longer decided here: objectives.cpp's final milestone
    // (TerraformComplete, checked via g_phase == TerraPhase::Terraformed) is now the
    // single source of truth for g_victory, replacing this and update_phase()'s old
    // redundant, differently-thresholded check below.
}

void update_phase() {
    // Update terraforming phase based on temperature
    TerraPhase old_phase = g_phase;

    if (g_temperature >= kTempHabitable && g_atmosphere >= 60.0f) {
        g_phase = TerraPhase::Habitable;
    } else if (g_temperature >= kTempThawing) {
        g_phase = TerraPhase::Thawing;
    } else if (g_co2_level > 10.0f) {
        g_phase = TerraPhase::Warming;
    } else {
        g_phase = TerraPhase::Frozen;
    }

    // Terraformed is the final phase; objectives.cpp watches for it and sets g_victory
    // once (see recompute_terraform_score()'s comment above for why this replaced the
    // old inline g_victory/toast side effects here).
    if (g_temperature >= kTempTarget && g_atmosphere >= 80.0f && g_terraform >= 70.0f) {
        g_phase = TerraPhase::Terraformed;
    }

    // Notify phase changes
    if (old_phase != g_phase && !g_victory) {
        set_toast(std::string("Fase: ") + phase_name(g_phase), 4.0f);
    }
}

void melt_ice_around(World& world, int cx, int cy, int radius) {
    if (g_temperature < kTempThawing) return; // Too cold to melt

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx*dx + dy*dy > radius*radius) continue;
            int x = cx + dx;
            int y = cy + dy;
            if (!world.in_bounds(x, y)) continue;

            // Derreter gelo do SOLO (ground). O topo (tiles) pode estar Air/objeto.
            if (world.get_ground(x, y) == Block::Ice && !is_base_structure(world.get_ground(x, y))) {
                world.set_ground(x, y, Block::Water);
                Block t = world.get(x, y);
                if (t != Block::Air && is_ground_like(t) && !is_base_structure(t) && !is_module(t)) {
                    world.set(x, y, Block::Water);
                }
                g_surface_dirty = true;
            }
        }
    }
}


// ============= AGUA FLUIDA (ver comentarios em world.h) =============
namespace {

struct WaterFlowCell {
    int x, z;
    int16_t level;   // cota (heightmap) da superficie da agua que esta enchendo este tile
};

// Lava a ser apagada: tile de lava encostado em agua.
struct LavaQuenchCell {
    int x, z;
};

// Filas de espalhamento. Duplicatas sao inofensivas de proposito: cada tile e' REVALIDADO no momento
// em que sai da fila, entao processar o mesmo duas vezes nao faz nada na segunda - o que dispensa um
// conjunto de visitados (que teria custo de memoria e precisaria ser limpo).
std::vector<WaterFlowCell> g_water_flow;
size_t g_water_head = 0;
std::vector<LavaQuenchCell> g_lava_quench;
float g_water_flow_timer = 0.0f;
float g_lava_quench_timer = 0.0f;

constexpr size_t kWaterFlowMaxQueue = 8192;   // teto de memoria
// 1 tile por tick de 0.09s (~11 tiles/s). Era 18 por 0.07s (~257/s): um buraco de poucos tiles
// enchia num piscar e nao dava pra VER a agua entrando - reclamacao do jogador ("faz isso muito
// rapido sem efeito de que esta preenchendo"). O que da a leitura de fluido e' o ritmo, nao o efeito.
constexpr int    kWaterFlowPerTick  = 1;
constexpr float  kWaterFlowTick     = 0.09f;
// Lava apaga BEM mais devagar que a agua enche: e' o momento dramatico, precisa ser visto tile a tile.
constexpr float  kLavaQuenchTick    = 0.40f;

// ---- Efeitos visuais (respingo ao encher, vapor ao apagar lava) ----
// Lista propria, desenhada por render_water_fx(). NAO usa spawn_block_particles: o vetor g_particles
// e' atualizado mas NUNCA desenhado em lugar nenhum (sobra da era pre-3D), entao qualquer efeito
// jogado nele fica invisivel.
struct WaterFx {
    float x, y, z;
    float timer, dur;
    uint8_t kind;    // 0 = respingo de agua, 1 = vapor de lava apagada, 2 = lava escorrendo
};
std::vector<WaterFx> g_water_fx;
constexpr size_t kWaterFxMax = 96;

void spawn_water_fx(float x, float y, float z, bool steam) {
    if (g_water_fx.size() >= kWaterFxMax) return;
    float d = steam ? 1.6f : 0.55f;
    g_water_fx.push_back({x, y, z, d, d, (uint8_t)(steam ? 1 : 0)});
}

void spawn_lava_fx(float x, float y, float z) {
    if (g_water_fx.size() >= kWaterFxMax) return;
    g_water_fx.push_back({x, y, z, 1.1f, 1.1f, 2});
}

// Hash deterministico 0..1 - mesma tecnica das brasas de lava. Sem rand(): as gotas nao podem
// tremer de frame em frame.
float fx_hash01(int i, float salt) {
    float v = std::sin((float)i * 12.9898f + salt * 78.233f) * 43758.5453f;
    return v - std::floor(v);
}

// Barreira: o que a agua nunca invade.
bool water_flow_blocked(const World& world, int x, int z) {
    if (world.stack_height_at(x, z) > 0) return true;   // parede construida represa
    if (is_base_structure(world.get_ground(x, z))) return true;
    if (is_base_structure(world.get(x, z))) return true;
    return false;
}

bool water_flow_is_liquid(Block b) {
    return b == Block::Water || b == Block::Ice || b == Block::Lava;
}
void water_flow_enqueue(int x, int z, int16_t level) {
    if (g_water_flow.size() - g_water_head >= kWaterFlowMaxQueue) return;
    g_water_flow.push_back({x, z, level});
}

// Consumo FIFO das filas de fluido. Com LIFO (pop_back) o fluido andava em PROFUNDIDADE: saia vagando
// por um ramo so' e as outras frentes ficavam soterradas no fundo da pilha. Medido: um canal de 4
// tiles ao lado da lava ficava com 0 tiles cheios depois de 6 ticks, enquanto a lava se espalhava por
// 6 tiles em OUTRA direcao. Fluido avanca em todas as frentes ao mesmo tempo - isso e' fila, nao
// pilha. O cursor `head` evita erase() no comeco do vetor; quando drena, limpa tudo de uma vez.
template <typename T>
bool fluid_pop(std::vector<T>& q, size_t& head, T& out) {
    if (head >= q.size()) { q.clear(); head = 0; return false; }
    out = q[head++];
    if (head >= q.size()) { q.clear(); head = 0; }
    return true;
}

// Fila de espalhamento da LAVA. Mesma mecanica da agua, ritmo MUITO mais lento: lava e' espessa.
// `head` = cota da FONTE do derramamento. E' o teto absoluto de acumulo: liquido nao sobe acima da
// propria carga (nao ha bomba). Sem isso, um tile de lava cercado de lava se considerava "sem saida"
// e subia indefinidamente - medido: 9 tiles acima da cota da borda, o "plato no ar" de volta.
struct LavaFlowCell { int x, z; int16_t level; int16_t head; int16_t budget; bool rise; };
std::vector<LavaFlowCell> g_lava_flow;
size_t g_lava_head = 0;
// ORCAMENTO por evento de vazamento: cada derramamento espalha no maximo este numero de tiles.
// Sem ele a lava seria ILIMITADA - num vulcao em crista ha encosta abaixo sem fim, e cavar perto
// dele iniciaria uma inundacao lenta que desceria a montanha inteira e nunca pararia. O terreno
// limita a agua (bacias sao niveladas na geracao), mas nao limita lava correndo ladeira abaixo.
constexpr int16_t kLavaSpillBudget = 190;  // 24 -> 80: com o modo ACUMULAR, cada camada que sobe
                                          // num poco consome orcamento. 24 nao enchia nem um buraco
                                          // pequeno. 80 acoes a 0.42s = ~34s de escorrimento visivel.
int g_lava_spill_left = 0;   // tiles restantes no vazamento ATUAL (ver a nota do orcamento acima)
float g_lava_flow_timer = 0.0f;
// 1 tile a cada 0.85s contra 0.09s da agua - ~9x mais lenta. E' o que faz ela "escorrer" em vez de
// preencher. Pedido do jogador: "bem mais espessa que a agua".
constexpr float kLavaFlowTick = 0.42f;   // 0.85 -> 0.42 a pedido do jogador ("acelere um pouco"):
                                         // ainda ~4.7x mais lenta que a agua (0.09), continua lendo
                                         // como fluido espesso, sem a sensacao de travado.

// A lava pode entrar neste tile, vindo de uma poca na cota `level`?
bool lava_can_enter(const World& world, int x, int z, int16_t level) {
    if (!world.in_bounds(x, z)) return false;
    Block g = world.get_ground(x, z);
    if (g == Block::Lava) return false;                       // ja e' lava
    if (g == Block::Water || g == Block::Ice) return false;    // agua/gelo: quem reage e' o apagamento
    if (g == Block::Basalt) return false;                      // crosta resfriada REPRESA o fluxo
    if (water_flow_blocked(world, x, z)) return false;         // base e paredes represam igual
    // `<=`, nao `<`: lava atravessa terreno PLANO tambem (fluido espesso escorre e se espalha), so'
    // nunca SOBE. Era `>= level -> rejeita`, o que exigia degrau pra baixo em cada passo e travava a
    // lava na primeira borda plana.
    if (world.height_at(x, z) > level) return false;
    return true;
}

void lava_flow_enqueue(int x, int z, int16_t level, int16_t head, int16_t budget, bool rise) {
    if (budget <= 0) return;   // vazamento esgotou o orcamento: para de espalhar
    if (g_lava_flow.size() - g_lava_head >= kWaterFlowMaxQueue) return;
    g_lava_flow.push_back({x, z, level, head, budget, rise});
}

// Enfileira a lava vizinha de (x,z) pra ser apagada.
void lava_quench_enqueue_around(const World& world, int x, int z) {
    const int nx4[4] = {1, -1, 0, 0}, nz4[4] = {0, 0, 1, -1};
    for (int k = 0; k < 4; ++k) {
        int tx = x + nx4[k], tz = z + nz4[k];
        if (!world.in_bounds(tx, tz)) continue;
        if (world.get_ground(tx, tz) != Block::Lava) continue;
        if (g_lava_quench.size() >= kWaterFlowMaxQueue) return;
        g_lava_quench.push_back({tx, tz});
    }
}

} // namespace

void water_flood_from(World& world, int x, int y) {
    if (!world.in_bounds(x, y)) return;
    const int nx4b[4] = {1, -1, 0, 0}, nz4b[4] = {0, 0, 1, -1};
    // SEMENTE EM CIMA DE AGUA: o tile em si e' a fonte, entao espalha pros VIZINHOS. Sem isto,
    // semear a partir de um tile que ja e' agua nao fazia nada (a revalidacao o descartava por ja
    // estar cheio) e nada mais era enfileirado - a agua nao saia do lugar. Isso importa porque quem
    // cava semeia o tile cavado E os 4 vizinhos, e vizinho de buraco costuma ser justamente agua.
    if (world.get_ground(x, y) == Block::Water) {
        lava_quench_enqueue_around(world, x, y);
        int16_t lvl = world.height_at(x, y);
        for (int k = 0; k < 4; ++k) {
            int tx = x + nx4b[k], tz = y + nz4b[k];
            if (!world.in_bounds(tx, tz)) continue;
            if (water_flow_is_liquid(world.get_ground(tx, tz))) continue;
            if (water_flow_blocked(world, tx, tz)) continue;
            if (world.height_at(tx, tz) >= lvl) continue;
            water_flow_enqueue(tx, tz, lvl);
        }
        return;
    }

    // Nivel da agua mais ALTA entre os 4 vizinhos: a agua desce da fonte mais alta disponivel.
    const int nx4[4] = {1, -1, 0, 0}, nz4[4] = {0, 0, 1, -1};
    bool found = false;
    int16_t level = 0;
    for (int k = 0; k < 4; ++k) {
        int tx = x + nx4[k], tz = y + nz4[k];
        if (!world.in_bounds(tx, tz)) continue;
        if (world.get_ground(tx, tz) != Block::Water) continue;
        int16_t h = world.height_at(tx, tz);
        if (!found || h > level) { level = h; found = true; }
    }
    if (!found) return;
    water_flow_enqueue(x, y, level);
}

void lava_flood_from(World& world, int x, int y) {
    if (!world.in_bounds(x, y)) return;
    const int nx4[4] = {1, -1, 0, 0}, nz4[4] = {0, 0, 1, -1};

    // SEMENTE EM CIMA DE LAVA: o tile e' a fonte, espalha pros vizinhos (mesmo raciocinio da agua -
    // quem cava semeia o tile cavado E os 4 vizinhos, e vizinho de buraco costuma ser a propria lava).
    // Fila vazia = VAZAMENTO NOVO: recarrega o orcamento. Se ja ha um vazamento em curso, ele
    // continua com o que sobrou - senao cavar repetidamente perto da lava recarregaria o orcamento
    // a cada golpe e o limite nao limitaria nada.
    if (g_lava_flow.size() <= g_lava_head) g_lava_spill_left = kLavaSpillBudget;

    if (world.get_ground(x, y) == Block::Lava) {
        int16_t lvl = world.height_at(x, y);
        for (int k = 0; k < 4; ++k) {
            int tx = x + nx4[k], tz = y + nz4[k];
            if (!lava_can_enter(world, tx, tz, lvl)) continue;
            lava_flow_enqueue(tx, tz, lvl, lvl, kLavaSpillBudget, false);
        }
        return;
    }

    bool found = false;
    int16_t level = 0;
    for (int k = 0; k < 4; ++k) {
        int tx = x + nx4[k], tz = y + nz4[k];
        if (!world.in_bounds(tx, tz)) continue;
        if (world.get_ground(tx, tz) != Block::Lava) continue;
        int16_t h = world.height_at(tx, tz);
        if (!found || h > level) { level = h; found = true; }
    }
    if (!found) return;
    if (!lava_can_enter(world, x, y, level)) return;
    lava_flow_enqueue(x, y, level, level, kLavaSpillBudget, false);
}

void update_water_flow(World& world, float dt) {
    // ---- Efeitos: envelhecem sempre, mesmo com as filas vazias ----
    for (size_t i = 0; i < g_water_fx.size();) {
        g_water_fx[i].timer -= dt;
        if (g_water_fx[i].timer <= 0.0f) {
            g_water_fx[i] = g_water_fx.back();
            g_water_fx.pop_back();
        } else {
            ++i;
        }
    }

    // ---- LAVA APAGANDO: 1 tile por kLavaQuenchTick ----
    if (!g_lava_quench.empty()) {
        g_lava_quench_timer += dt;
        if (g_lava_quench_timer >= kLavaQuenchTick) {
            g_lava_quench_timer = 0.0f;
            while (!g_lava_quench.empty()) {
                LavaQuenchCell c = g_lava_quench.back();
                g_lava_quench.pop_back();
                if (!world.in_bounds(c.x, c.z)) continue;
                if (world.get_ground(c.x, c.z) != Block::Lava) continue;   // revalidacao
                // Tem que continuar encostada em agua - senao a fila apagaria lava que ficou longe.
                // A agua tambem precisa estar na MESMA FAIXA DE ALTURA: dois tiles vizinhos podem ter
                // cotas muito diferentes, e agua num lago 10 unidades abaixo nao toca a lava que corre
                // na encosta acima. Sem esta condicao, um rio de lava descendo a serra seria apagado
                // inteiro por um lago no pe dela.
                bool touches_water = false;
                int16_t wlvl = 0;
                int16_t lava_h = world.height_at(c.x, c.z);
                const int nx4[4] = {1, -1, 0, 0}, nz4[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4 && !touches_water; ++k) {
                    int tx = c.x + nx4[k], tz = c.z + nz4[k];
                    if (!world.in_bounds(tx, tz)) continue;
                    if (world.get_ground(tx, tz) != Block::Water) continue;
                    int16_t h = world.height_at(tx, tz);
                    if (std::abs((int)h - (int)lava_h) > 2) continue;   // fora da linha d'agua
                    touches_water = true;
                    wlvl = h;
                }
                if (!touches_water) continue;

                // ENDURECE EM BASALTO, na PROPRIA cota. Duas mudancas em relacao a versao anterior:
                //
                //  1) Virava Dirt, porque nao havia bloco de rocha escura ground-like. Agora ha
                //     (Block::Basalt) - o pedido era "a lava deveria endurecer ao encostar na agua",
                //     e terra nao le como lava endurecida.
                //  2) NAO baixa mais a cota. Antes eu assentava a rocha 1 unidade abaixo da linha
                //     d'agua pra a agua correr por cima e a reacao continuar em cadeia - mas o
                //     resultado media 5 tiles virando AGUA e 0 virando rocha: a crosta desaparecia
                //     submersa. Endurecer quer dizer que a rocha FICA, visivel, na altura onde a lava
                //     estava.
                //
                // A cadeia agora se propaga pela LINHA DE CONTATO, nao por submersao: cada tile de
                // agua enfileira os vizinhos de lava dele, entao uma frente de lava chegando num lago
                // endurece tile a tile ao longo da margem. E a crosta REPRESA o que vem atras
                // (lava_can_enter rejeita Basalt), que e' o comportamento fisico e o limite natural.
                world.set_ground(c.x, c.z, Block::Basalt);
                if (world.get(c.x, c.z) == Block::Lava) world.set(c.x, c.z, Block::Basalt);
                (void)wlvl;   // a cota da agua nao e' mais usada: a rocha fica onde a lava estava
                g_surface_dirty = true;

                float wy = (float)world.height_at(c.x, c.z) * kHeightScale;
                spawn_water_fx((float)c.x, wy, (float)c.z, true);
                play_steam_hiss_sound();

                // A pedra nova NAO chama water_flood_from: ela e' terra agora, na cota onde a lava
                // estava - a agua nao deve subir por cima dela (era isso que fazia a crosta
                // desaparecer submersa). A cadeia continua pela linha de contato: cada tile de agua
                // enfileira seus proprios vizinhos de lava.
                lava_quench_enqueue_around(world, c.x, c.z);
                break;   // 1 tile por tick: o efeito precisa ser visto acontecendo
            }
        }
    }

    // ---- LAVA ESCORRENDO / ACUMULANDO: 1 acao por kLavaFlowTick ----
    if (g_lava_flow.size() > g_lava_head) {
        g_lava_flow_timer += dt;
        if (g_lava_flow_timer >= kLavaFlowTick) {
            g_lava_flow_timer = 0.0f;
            LavaFlowCell c;
            while (fluid_pop(g_lava_flow, g_lava_head, c)) {
                // ORCAMENTO TOTAL do vazamento, nao por profundidade. Com limite por celula a lava
                // enchia TUDO que estivesse a N tiles de distancia: medido, 200 tiles em 200 ticks
                // numa planicie rebaixada, sem parar. Um teto de ACOES TOTAIS por derramamento e' o
                // que realmente limita - e subir de nivel tambem conta como acao (e' volume).
                if (g_lava_spill_left <= 0) { g_lava_flow.clear(); g_lava_head = 0; break; }

                const int nx4[4] = {1, -1, 0, 0}, nz4[4] = {0, 0, 1, -1};

                // ================= MODO ACUMULAR (rise) =================
                // Um tile de lava sem NENHUMA saida (nenhum vizinho de cota igual ou menor onde ela
                // possa entrar) e' um fundo de poco. Fluido nessa situacao nao para: ele ACUMULA.
                // Aqui a lava sobe 1 unidade de cota e tenta de novo no tick seguinte.
                //
                // Isto era o que faltava: eu tinha tirado o set_height pra matar o "plato no ar",
                // e com isso a lava virou uma camada que NUNCA sobe - cobria o fundo do buraco e
                // parava, mesmo com lava caindo de cima (bug reportado). O plato no ar vinha de subir
                // ate a cota da FONTE; subir 1 unidade por vez, e so' quando nao ha escape, nao tem
                // esse problema: no instante em que a cota iguala a de um vizinho, aquele vizinho
                // passa a ser saida valida e o acumulo PARA. Ou seja, a lava nunca passa da borda
                // mais baixa que a contem - que e' exatamente o comportamento de um liquido.
                if (c.rise) {
                    if (world.get_ground(c.x, c.z) != Block::Lava) continue;   // revalidacao
                    int16_t h = world.height_at(c.x, c.z);
                    bool escape = false;
                    for (int k = 0; k < 4; ++k)
                        if (lava_can_enter(world, c.x + nx4[k], c.z + nz4[k], h)) { escape = true; break; }
                    if (escape) {
                        // Achou borda: transborda em vez de continuar subindo.
                        // Transborda pelo ponto MAIS BAIXO da borda, nao por toda a volta - mesma
                        // regra do passo de descida: um liquido verte onde a borda cede primeiro.
                        int16_t rim = 32767;
                        for (int k = 0; k < 4; ++k) {
                            int tx = c.x + nx4[k], tz = c.z + nz4[k];
                            if (!lava_can_enter(world, tx, tz, h)) continue;
                            int16_t nh = world.height_at(tx, tz);
                            if (nh < rim) rim = nh;
                        }
                        for (int k = 0; k < 4; ++k) {
                            int tx = c.x + nx4[k], tz = c.z + nz4[k];
                            if (!lava_can_enter(world, tx, tz, h)) continue;
                            if (world.height_at(tx, tz) != rim) continue;
                            lava_flow_enqueue(tx, tz, h, c.head, (int16_t)(c.budget - 1), false);
                        }
                        continue;   // sem custo: nao adicionou volume, so' redirecionou
                    }

                    // NIVELAMENTO: se algum vizinho de lava esta MAIS BAIXO, ele sobe primeiro. Sem
                    // isto o ultimo tile do poco a ficar sem saida subia sozinho e virava um PILAR de
                    // lava dentro do buraco (medido: 9 tiles acima da cota da borda, 1 unico tile do
                    // fundo cheio). O repasse sempre desce de cota, entao nao ha ciclo; com a fila
                    // FIFO os tiles do fundo se alternam e a superficie da poca sobe plana.
                    int low_k = -1; int16_t low_h = h;
                    for (int k = 0; k < 4; ++k) {
                        int tx = c.x + nx4[k], tz = c.z + nz4[k];
                        if (!world.in_bounds(tx, tz)) continue;
                        if (world.get_ground(tx, tz) != Block::Lava) continue;
                        int16_t nh = world.height_at(tx, tz);
                        if (nh < low_h) { low_h = nh; low_k = k; }
                    }
                    if (low_k >= 0) {
                        lava_flow_enqueue(c.x + nx4[low_k], c.z + nz4[low_k], low_h, c.head, c.budget, true);
                        continue;   // sem custo: passou a vez, nao adicionou volume
                    }

                    // TETO DE CARGA: nao existe bomba - liquido nao sobe acima da cota da propria
                    // fonte. Sem este teto, um tile cercado de lava (que lava_can_enter recusa como
                    // saida, por ja ser lava) se considerava "sem saida" e subia sem limite.
                    // Vem DEPOIS do nivelamento de proposito: antes dele, o token que chegava ao teto
                    // morria neste `continue` e os outros tiles do fundo ficavam sem entrada na fila -
                    // medido, a poca parava 1 unidade abaixo da borda (1 de 9 tiles nivelados).
                    if (h + 1 > c.head) continue;
                    g_lava_spill_left--;
                    world.set_height(c.x, c.z, (int16_t)(h + 1));
                    g_surface_dirty = true;
                    spawn_lava_fx((float)c.x, (float)(h + 1) * kHeightScale, (float)c.z);
                    lava_flow_enqueue(c.x, c.z, (int16_t)(h + 1), c.head, (int16_t)(c.budget - 1), true);
                    world.rebuild_surface_cache();
                    break;   // 1 acao por tick
                }

                // ================= MODO ESPALHAR =================
                if (!lava_can_enter(world, c.x, c.z, c.level)) continue;   // revalidacao
                g_lava_spill_left--;

                // Mantem a altura do PROPRIO tile (nao sobe pra cota da fonte): e' o que faz a lava
                // escorrer por cima do relevo em vez de construir plato. Desenhada em base_y+kTopEps,
                // sobre o terreno, entao camada fina renderiza certo. As LATERAIS do tile sao
                // desenhadas como rocha (main.cpp/terrain_mesh.cpp) - lava e' camada, nao bloco.
                int16_t here = world.height_at(c.x, c.z);
                world.set_ground(c.x, c.z, Block::Lava);
                world.set(c.x, c.z, Block::Lava);
                g_surface_dirty = true;
                spawn_lava_fx((float)c.x, (float)here * kHeightScale, (float)c.z);

                // Encostou em agua? Endurece na hora - a interacao vale nos dois sentidos.
                bool near_water = false;
                for (int k = 0; k < 4 && !near_water; ++k)
                    if (world.in_bounds(c.x + nx4[k], c.z + nz4[k]) &&
                        world.get_ground(c.x + nx4[k], c.z + nz4[k]) == Block::Water) near_water = true;
                if (near_water) {
                    if (g_lava_quench.size() < kWaterFlowMaxQueue) g_lava_quench.push_back({c.x, c.z});
                } else {
                    // DESCE PELA PARTE MAIS BAIXA, nao em leque (pedido do jogador). O passo antigo
                    // enfileirava TODOS os 4 vizinhos elegiveis, entao a lava se abria como poca em
                    // qualquer terreno plano-ou-descendente. Fluido espesso nao faz isso: ele procura
                    // a linha de maior declive e corre por ela.
                    //
                    // Como: primeiro acha a MENOR cota entre os vizinhos que aceitam lava; depois
                    // enfileira somente os que estao nessa cota. Se existe vizinho mais baixo, so' ele
                    // recebe - a lava vira um filete que desce a encosta. Se nao existe (terreno
                    // plano), `low` continua sendo a cota deste tile e o laco espalha nos vizinhos de
                    // mesma cota, que e' o que permite a poca crescer e achar a borda.
                    int16_t low = here;
                    for (int k = 0; k < 4; ++k) {
                        int tx = c.x + nx4[k], tz = c.z + nz4[k];
                        if (!lava_can_enter(world, tx, tz, here)) continue;
                        int16_t nh = world.height_at(tx, tz);
                        if (nh < low) low = nh;
                    }
                    int spread = 0;
                    for (int k = 0; k < 4; ++k) {
                        int tx = c.x + nx4[k], tz = c.z + nz4[k];
                        if (!lava_can_enter(world, tx, tz, here)) continue;
                        if (world.height_at(tx, tz) != low) continue;   // so' o caminho mais baixo
                        // Propaga a altura DESTE tile, nao a da fonte: desce degrau por degrau
                        // seguindo o relevo.
                        lava_flow_enqueue(tx, tz, here, c.head, (int16_t)(c.budget - 1), false);
                        spread++;
                    }
                    // Beco sem saida => vira poco: entra em modo ACUMULAR.
                    if (spread == 0)
                        lava_flow_enqueue(c.x, c.z, here, c.head, (int16_t)(c.budget - 1), true);
                }
                world.rebuild_surface_cache();
                break;   // 1 acao por tick
            }
        }
    }

    // ---- AGUA ENCHENDO ----
    if (g_water_flow.size() <= g_water_head) return;
    g_water_flow_timer += dt;
    if (g_water_flow_timer < kWaterFlowTick) return;
    g_water_flow_timer = 0.0f;

    const int nx4[4] = {1, -1, 0, 0}, nz4[4] = {0, 0, 1, -1};
    int processed = 0;
    while (processed < kWaterFlowPerTick) {
        WaterFlowCell c;
        if (!fluid_pop(g_water_flow, g_water_head, c)) break;
        if (!world.in_bounds(c.x, c.z)) continue;

        // REVALIDACAO (e' o que torna duplicatas na fila inofensivas).
        if (water_flow_is_liquid(world.get_ground(c.x, c.z))) continue;   // ja encheu
        if (water_flow_blocked(world, c.x, c.z)) continue;
        if (world.height_at(c.x, c.z) >= c.level) continue;               // acima da linha d'agua

        // Enche: sobe a coluna ate a cota da agua e marca como agua.
        world.set_height(c.x, c.z, c.level);
        world.set_ground(c.x, c.z, Block::Water);
        world.set(c.x, c.z, Block::Water);
        g_surface_dirty = true;
        processed++;

        spawn_water_fx((float)c.x, (float)c.level * kHeightScale, (float)c.z, false);
        // Encheu encostando em lava? Entao a lava vai apagar.
        lava_quench_enqueue_around(world, c.x, c.z);

        // Continua pelos vizinhos que ainda estao abaixo da linha d'agua. Como World::gen nivela cada
        // bacia, o terreno em volta de um lago ja esta na cota dele ou acima - entao na pratica isto
        // enche APENAS o que o jogador cavou, e para sozinho. O teto da fila e' so' rede de seguranca.
        for (int k = 0; k < 4; ++k) {
            int tx = c.x + nx4[k], tz = c.z + nz4[k];
            if (!world.in_bounds(tx, tz)) continue;
            if (water_flow_is_liquid(world.get_ground(tx, tz))) continue;
            if (water_flow_blocked(world, tx, tz)) continue;
            if (world.height_at(tx, tz) >= c.level) continue;
            water_flow_enqueue(tx, tz, c.level);
        }
    }
    if (processed > 0) world.rebuild_surface_cache();
}

void render_water_fx() {
    if (g_water_fx.empty()) return;
    rlSetTexture(rlGetTextureIdDefault());   // NAO rlSetTexture(0): pra id 0 o rlgl nao troca nada

    for (size_t i = 0; i < g_water_fx.size(); ++i) {
        const WaterFx& fx = g_water_fx[i];
        float t = 1.0f - clamp01(fx.timer / fx.dur);   // 0 no nascimento, 1 no fim
        int seed = (int)i * 17 + (int)(fx.x * 7.0f) + (int)(fx.z * 13.0f);

        if (fx.kind == 0) {
            // RESPINGO: anel de gotas achatadas se abrindo rente a superficie + um flash claro no
            // centro. render_plane_3d (nao cubos) porque e' efeito de chao - cubo leria como bloco
            // flutuando, licao ja aprendida na onda de choque do pouso.
            const int kDrops = 7;
            for (int d = 0; d < kDrops; ++d) {
                float ang = fx_hash01(seed + d, 3.0f) * 6.2831853f;
                float rr = (0.15f + t * 0.55f) * (0.7f + fx_hash01(seed + d, 4.0f) * 0.6f);
                float size = (0.20f - t * 0.10f) * (0.8f + fx_hash01(seed + d, 5.0f) * 0.5f);
                if (size <= 0.01f) continue;
                render_plane_3d(fx.x + std::cos(ang) * rr, fx.y + 0.06f, fx.z + std::sin(ang) * rr,
                                size, 0.70f, 0.88f, 1.0f, (1.0f - t) * 0.75f);
            }
            render_plane_3d(fx.x, fx.y + 0.05f, fx.z, 0.55f * (1.0f - t * 0.5f),
                            0.85f, 0.95f, 1.0f, (1.0f - t) * 0.45f);
        } else if (fx.kind == 1) {
            // VAPOR: baforadas brancas subindo e abrindo, mais um brilho aditivo na base (a lava
            // ainda quente por baixo). E' o efeito que o jogador pediu ao apagar a lava.
            const int kPuffs = 6;
            for (int pi = 0; pi < kPuffs; ++pi) {
                float phase = fx_hash01(seed + pi, 9.0f) * 0.35f;
                float lt = clamp01((t - phase) / std::max(0.05f, 1.0f - phase));
                if (lt <= 0.0f) continue;
                float ang = fx_hash01(seed + pi, 11.0f) * 6.2831853f;
                float rr = 0.10f + lt * 0.55f;
                float yy = fx.y + 0.15f + lt * 2.4f;
                float size = (0.28f + lt * 0.42f);
                float a = (1.0f - lt) * (1.0f - lt) * 0.60f;
                render_plane_3d(fx.x + std::cos(ang) * rr, yy, fx.z + std::sin(ang) * rr,
                                size, 0.94f, 0.96f, 0.98f, a);
            }
            rlSetBlendMode(RL_BLEND_ADDITIVE);
            rlDisableDepthMask();
            render_glow_disc_3d({fx.x, fx.y + 0.10f, fx.z}, 0.45f * (1.0f - t),
                                1.0f, 0.55f, 0.20f, (1.0f - t) * 0.50f, 10);
            rlEnableDepthMask();
            rlSetBlendMode(RL_BLEND_ALPHA);
        } else {
            // LAVA ESCORRENDO: brasas subindo pouco + brilho laranja forte na base. Diferente do
            // vapor de proposito - vapor sobe alto e clareia, lava fica rente ao chao e queima.
            rlSetBlendMode(RL_BLEND_ADDITIVE);
            rlDisableDepthMask();
            render_glow_disc_3d({fx.x, fx.y + 0.08f, fx.z}, 0.70f * (1.0f - t * 0.4f),
                                1.0f, 0.42f, 0.10f, (1.0f - t) * 0.65f, 10);
            const int kEmbers = 5;
            for (int e = 0; e < kEmbers; ++e) {
                float phase = fx_hash01(seed + e, 21.0f) * 0.4f;
                float lt = clamp01((t - phase) / std::max(0.05f, 1.0f - phase));
                if (lt <= 0.0f) continue;
                float ang = fx_hash01(seed + e, 23.0f) * 6.2831853f;
                float rr = 0.10f + lt * 0.35f;
                render_plane_3d(fx.x + std::cos(ang) * rr, fx.y + 0.12f + lt * 0.75f,
                                fx.z + std::sin(ang) * rr, 0.13f * (1.0f - lt),
                                1.0f, 0.62f, 0.18f, (1.0f - lt) * 0.85f);
            }
            rlEnableDepthMask();
            rlSetBlendMode(RL_BLEND_ALPHA);
        }
    }
    rlSetTexture(rlGetTextureIdDefault());
}
