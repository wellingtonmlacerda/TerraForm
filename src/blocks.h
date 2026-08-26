#pragma once

#include <cstdint>

// ============= Blocks =============
// Extracted verbatim from main.cpp (original lines ~195-380).
enum class Block : uint8_t {
    Air = 0,
    Grass,
    Dirt,
    Stone,
    Sand,
    Water,
    Ice,           // Frozen water (before warming)
    Snow,          // Snow (top-down biomes)
    Wood,
    Leaves,
    Coal,
    Iron,
    Copper,        // New resource for advanced modules
    Crystal,       // Rare crystal for energy systems
    Metal,         // Refined metal
    Organic,       // Organic material for food/plants
    Components,    // Electronic components
    // Modules
    SolarPanel,
    EnergyGenerator,  // Main power source
    WaterExtractor,
    OxygenGenerator,
    Greenhouse,    // Food production
    CO2Factory,    // Releases CO2 for warming
    Habitat,       // Living quarters
    Workshop,      // Repairs and crafting
    TerraformerBeacon,
    // Base structures (not buildable, generated)
    RocketHull,    // Landed rocket
    RocketEngine,  // Rocket engine
    RocketWindow,  // Rocket window
    RocketNose,    // Rocket nose cone
    RocketFin,     // Rocket fins
    RocketDoor,    // Rocket door/hatch
    DomeGlass,     // Habitat dome glass
    DomeFrame,     // Dome metal frame
    LandingPad,    // Landing pad floor
    BuildSlot,     // Empty slot for building modules
    PipeH,         // Horizontal pipe
    PipeV,         // Vertical pipe
    Antenna,       // Communication antenna
    // Anexado no FIM de proposito: Block e salvo como uint8_t bruto (save_load.cpp), entao
    // inserir no meio do enum reordenaria (corromperia) saves existentes.
    Lava,          // Fundo de cratera vulcanica - puramente cosmetico, sem fluxo/dano
    // Idem: anexado apos Lava (fim real do enum). Recurso de inventario puro (like Metal/
    // Components), nunca colocado como tile do mundo - so existe em g_inventory, produzido
    // via try_refine_at_workshop() (modules_building.cpp, tecla G).
    RefinedAlloy,  // Liga metalica refinada (Oficina) - da a Metal seu 2o uso de verdade
    // Idem: anexado apos RefinedAlloy (fim real do enum). Item de posse pura (0 ou 1 em
    // g_inventory, nunca colocado como tile do mundo) - fabricado 1x na Oficina via
    // try_craft_laser_pistol() (modules_building.cpp, tecla P), selecionavel no hotbar pra
    // atirar em criaturas (creatures.cpp).
    LaserPistol,   // Pistola de laser
    // Idem: anexados APOS LaserPistol (fim real do enum). Os dois sao ground-like (altura 0, sem
    // colisao, andaveis) E is_base_structure (nao mineraveis, imunes a cratera de meteoro e a
    // terraform_step) - exatamente a combinacao que LandingPad/BuildSlot ja usavam, os 2 unicos
    // blocos de solo indestrutiveis que existiam. Nenhum dos dois entra em inventario/hotbar (a
    // hotbar e' uma lista fixa montada a mao em ui_hud.cpp, nao uma varredura do enum) e nao ha
    // receita pra nenhum deles - sao colocados so' por generate_base().
    BaseFloor,     // Piso INTERNO da base (cupula/corredor/estufa) - distinto da pista externa
    PlanterBed,    // Canteiro de cultivo da estufa (nao mineravel = sem exploit de recurso)
    // COLISAO DE MOBILIA (pedido do jogador: "os moveis na base estao sem fisica"). Sao blocos
    // INVISIVEIS (o loop de objetos e o de pilhas pulam eles em main.cpp) colocados na slot de
    // OBJETO nos tiles que a mobilia ocupa - a geometria bonita continua sendo desenhada por
    // base_interior.cpp, e a colisao vem de graca do resolvedor por tile que ja existe.
    //
    // Por que 3 valores em vez de 1: get_block_height() devolve UMA altura por valor de Block, e
    // e' o UNICO lugar do motor onde existe altura sub-1.0. Camada de pilha e' fixa em 1.0 em
    // render E colisao, entao mobilia empilhada faria o jogador pisar 1.0 acima do movel (flutuando
    // sobre a cama). Na slot de objeto, cada faixa de altura casa com o movel desenhado.
    //
    // NAO podem ser ground-like: is_ground_like e' testado ANTES de tudo em get_block_height e
    // zeraria a altura, matando a colisao. Sao is_base_structure pra herdar nao-mineravel + imune a
    // meteoro/terraform/arvore + nao-substituivel.
    FurnitureLow,  // 0.70 - cama, caixas, bancos, canteiro elevado
    FurnitureMid,  // 1.10 - mesa, bancada, console, pia
    FurnitureTall, // 2.10 - armario, tanque de agua, prateleira

    // Casca de COLISAO do exterior da base. (BaseShell fica antes de FurnitureHuge na ordem do enum
    // por causa de compatibilidade de save: Block e' gravado como uint8_t bruto por tile, entao valor
    // novo so' no FIM.)
    // Casca de COLISAO do exterior da base. Invisivel (pulado no render, como a mobilia) mas solido
    // de verdade: e' o que torna a instalacao vedada contra o jetpack. A APARENCIA vem de um modelo
    // proprio, desenhado por render_base_exterior() (base_exterior.h) - cilindros/domos/tubos lisos
    // no lugar de cubos serrilhados. Separar as duas coisas era o pedido do jogador ("o exterior deve
    // ser um modelo/estrutura propria"), e de quebra economiza os ~8500 quads que os blocos custavam.
    // O modelo desenhado e' 0.5 tile MAIOR que a casca de blocos de proposito (base_exterior.cpp):
    // o jogador para pouco antes de encostar na parede visivel, nunca depois - o contrario daria
    // "parede invisivel", exatamente o que ele rejeitou.
    BaseShell,

    // 4.20 - maquinario / reator / tanque industrial. Existe porque numa sala de 13-16 de pe-direito
    // um movel de 2.10 le como brinquedo, e desenhar uma maquina de 4+ com collider de 2.10 deixaria
    // o jogador atravessar a metade de cima dela com o jetpack.
    FurnitureHuge,

    // Rocha de lava resfriada. Existe porque o jogador pediu que a lava "endurecesse ao encostar na
    // agua": antes ela virava Dirt, o que eu mesmo tinha declarado como compromisso ruim - lava
    // apagada nao e' terra. Alem do visual, Basalto tem uma funcao MECANICA: lava nao escoa pra
    // dentro dele (lava_can_enter, world.cpp), entao a crosta resfriada REPRESA o fluxo atras dela.
    // E' ground-like (obrigatorio: sem isso get_block_height o trataria como objeto de 1.0 e o
    // caminho de escavacao deixaria de baixar a coluna) e mineravel - da' pra abrir a crosta.
    Basalt,
};

static constexpr int kBlockTypeCount = (int)Block::Basalt + 1;

// True pros 3 blocos de colisao de mobilia acima - usado pelos skips de render (main.cpp), de
// oclusao de camera (camera.cpp) e de sombra (lighting.cpp). Um helper em vez de 3 comparacoes
// repetidas em 4 lugares diferentes.
bool is_furniture_collider(Block b);

// True pros blocos que NAO devem ser desenhados como cubo: a mobilia (desenhada por
// base_interior.cpp) e a casca do exterior (desenhada por base_exterior.cpp). Diferente de
// is_furniture_collider de proposito: a casca do exterior DEVE ocluir a camera e projetar sombra (e'
// um predio de verdade), entao camera.cpp/lighting.cpp continuam usando o predicado mais estreito.
bool is_invisible_collider(Block b);

bool is_transparent(Block b);

// Top-down: tiles que bloqueiam movimento (inverso de walkable)
bool is_solid(Block b);

bool is_module(Block b);

bool is_base_structure(Block b);

// Blocos que representam "solo/superficie" (nao sao objetos acima do terreno).
// Usado para separar terreno (ground) de objetos (rochas, minerios, modulos, etc).
bool is_ground_like(Block b);

// Top-down: tiles que permitem movimento do jogador
bool is_walkable(Block b);

const char* block_name(Block b);

// ============= Terraforming Phases =============
enum class TerraPhase {
    Frozen = 0,      // Starting phase: -60°C, no liquid water, need suits
    Warming,         // CO2 being released, temperature rising
    Thawing,         // 0°C+, ice melting, liquid water possible
    Habitable,       // 15°C+, can plant outside, atmosphere forming
    Terraformed,     // Earth-like conditions achieved!
};

const char* phase_name(TerraPhase p);
