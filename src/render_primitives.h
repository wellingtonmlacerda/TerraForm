#pragma once

#include "textures.h"    // Tile, UvRect, atlas_uv - used by the *_tex primitive signatures
#include "math_core.h"   // Vec3 - used by render_geodesic_dome's signature

// ============= Render Primitives (2D quads/bars/circles + 3D cubes/walls/sphere) =============
// Extracted verbatim from main.cpp (original lines ~697-1033, plus the wall-texture cluster
// at ~2065-2187 that sat right after the sky functions): the basic immediate-mode drawing
// helpers used by HUD, menus, minimap and the 3D world renderer. render_quad() had already
// lost its "static" in an earlier stage (minimap.cpp needs it); the rest lose it now for the
// same reason - main.cpp's render_world()/update_game()/HUD code (out of scope for this
// stage) and render_cube_3d_tex() (stays in main.cpp, not part of this stage's list, but
// calls render_cube_outline_3d()) all call these from another translation unit now.
//
// render_player.cpp (a sibling module extracted in the same stage) includes this header
// too: render_player_topdown() draws itself out of render_circle()/render_ellipse()/
// render_quad().
void render_quad(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f);
void render_quad_tex(float x, float y, float w, float h, Tile tile, float tint_r, float tint_g, float tint_b, float a = 1.0f);
void render_bar(float x, float y, float w, float h, float pct, float r, float g, float b);
void render_circle(float cx, float cy, float radius, float r, float g, float b, float a, int segments = 16);
void render_ellipse(float cx, float cy, float rx, float ry, float r, float g, float b, float a, int segments = 16);
void render_rounded_rect(float x, float y, float w, float h, float radius, float r, float g, float b, float a);

void render_cube_outline_3d(float x, float y, float z, float size, float line_width = 1.5f);
void render_cube_3d(float x, float y, float z, float size, float r, float g, float b, float a = 1.0f, bool outline = false);
// Segmento de linha 3D simples (RL_LINES) - substituido pelo render_beam_3d abaixo como
// origem do traco do tiro da pistola de laser (linha de 1px nao tinha espessura/brilho
// nenhum - feedback do jogador). Mantido pra quem mais usar (nenhum call site hoje).
void render_line_3d(Vec3 a, Vec3 b, float r, float g, float b_col, float alpha = 1.0f);
// Faixa (quad) esticada de `a` ate `b`, largura `width` - usada pro feixe da pistola de
// laser (creatures.cpp), 2 chamadas por tiro (nucleo fino + brilho largo, RL_BLEND_ADDITIVE).
// "Vertical" (perpendicular = normalize(cross(beam_dir, up)), fallback {1,0,0} se o feixe
// for quase vertical) - NAO billboard-pra-camera: com a camera atras do jogador olhando
// quase na mesma direcao do tiro (o caso mais comum), cross(beam_dir, to_camera) encolhe
// pra perto de zero na maior parte do tempo, piscando. Uma faixa vertical fixa e' o padrao
// certo pra tracos/lasers.
void render_beam_3d(Vec3 a, Vec3 b, float width, float r, float g, float b_col, float alpha = 1.0f);
// Disco brilhante billboard-pra-camera (aditivo, alpha cai pra 0 na borda) - usado pro
// flash de disparo/impacto da pistola de laser (creatures.cpp, main.cpp). Duplica de
// proposito a tecnica de render_billboard_disc (sky.cpp, static/nao exposto) em vez de
// acoplar creatures.cpp/main.cpp aos internals do ceu - mesmo raciocinio de nao reusar
// render_line_3d/render_plane_3d entre arquivos sem promove-los primeiro.
void render_glow_disc_3d(Vec3 center, float radius, float r, float g, float b_col, float alpha, int segments = 20);
// Plano horizontal simples (chao/agua/decais) - lido/escrito por main.cpp desde sempre;
// perdeu o "static" que tinha la (creatures.cpp precisa dele agora pra marca de queimado da
// pistola de laser - render_cube_3d le como "bloco flutuando", nao decal de chao).
void render_plane_3d(float x, float y, float z, float size, float r, float g, float b, float a = 1.0f);
// Esfera solida pequena e barata (capacete/juntas do jogador - "personagem muito quadrado",
// pedido do jogador) - malha em faixas de latitude/longitude (mesma tecnica de
// render_geodesic_dome, so a esfera INTEIRA 0..pi em vez de so' o hemisferio superior) com
// sombreamento simples por altura (mais claro no topo, mais escuro embaixo, mesmo espirito
// visual das 3 sombras de render_cube_3d) e neblina aplicada 1x no centro (nao por vertice -
// barato, e a esfera e' pequena o bastante pra nao notar gradiente de neblina dentro dela).
// scale_y/scale_z (padrao 1.0 = esfera perfeita, mesmo comportamento de antes) deixam
// esticar em elipsoide - usado pro torso do jogador ("corpo muito quadrado", pedido do
// jogador: um torso e' mais alto/fundo que largo, nao uma bola perfeita).
void render_sphere_3d(float x, float y, float z, float radius, float r, float g, float b, float a = 1.0f,
                       int lat_seg = 6, int lon_seg = 10, float scale_y = 1.0f, float scale_z = 1.0f);

// Escotilha/vigia de vidro (estilo submarino) numa parede CILINDRICA: disco de vidro + aro metalico
// grosso com degrau + parafusos no aro + 2 barras cruzadas (mullion). O disco fica no plano TANGENTE
// ao cilindro no angulo `wall_angle_rad` (normal radial pra fora) - NAO e' billboard-pra-camera, de
// proposito: mesmo raciocinio de render_beam_3d, uma janela e' parte da estrutura e tem que girar
// junto com ela, senao ela "desliza" pela parede conforme a camera orbita.
//   center       - centro do vidro no mundo (o chamador ja empurra pra fora do raio da parede)
//   glass_radius - raio do vidro; frame_width - espessura do aro em volta dele
//   bolts        - quantos parafusos ao redor do aro (0 = nenhum)
//   glow         - 0..1, "interior aceso visto de fora": aplicado como COR (lerp pra um branco
//                  quente + alpha maior), NAO como blend aditivo - trocar blend mode dentro de um
//                  primitivo obrigaria todo call site a salvar/restaurar esse estado. Quem quiser
//                  bloom de verdade chama render_glow_disc_3d por fora, em ADDITIVE.
void render_porthole_3d(Vec3 center, float wall_angle_rad, float glass_radius, float frame_width,
                        float gr, float gg, float gb, float galpha,
                        float fr, float fg, float fb,
                        int segments = 16, int bolts = 10, float glow = 0.0f);

// Cupula decorativa (saia cilindrica/fundacao + hemisferio geodesico por cima) - a malha em
// si nao tem colisao (e so desenho, nao mexe em World/is_solid); a colisao de verdade sao as
// PILHAS DE BLOCOS do anel de parede do hub (kHubWallInner/Outer, modules_building.h), logo dentro
// deste raio - a barreira cilindrica invisivel que existia aqui foi removida. base_center e o centro no
// chao; a fundacao sobe de y=0 a y=+skirt_height (cor metalica, mais fria que a casca tan),
// e o hemisferio comeca dali e sobe mais +radius (mesma tecnica de faixas de latitude/
// longitude de render_lit_sphere em sky.cpp, so sem luz solar - cor solida + gradiente de
// altura, e um 2o passe em linhas pelas mesmas faixas pro padrao triangulado/geodesico).
// door_facing_rad/door_half_angle/door_height (todos >0 pra ter porta - door_half_angle<=0
// desenha tudo fechado, sem porta nenhuma) marcam o arco de um VAO DE VERDADE na fundacao: a faixa
// 0..door_height e' OMITIDA ali (sobra a verga acima), e em volta dela vem a moldura de eclusa
// (batentes, verga saliente, soleira com faixas de advertencia, rebites, luzes de status), desenhada
// nas duas faces da parede. door_count reparte N vaos igualmente espacados a partir de
// door_facing_rad - o complexo modular tem 4 corredores (N/L/S/O) e a saia precisa de 4 aberturas.
// Antes isso era um alcapao SEMPRE FECHADO, correto enquanto a colisao era uma barreira cilindrica
// invisivel de 360 graus atravessada por teleporte de proximidade; com paredes de blocos e vaos
// reais, uma porta desenhada fechada onde se atravessa a pe passou a ser a mentira.
void render_geodesic_dome(Vec3 base_center, float radius, float r, float g, float b, float a,
                           int lat_seg = 8, int lon_seg = 16,
                           float door_facing_rad = 0.0f, float door_half_angle = 0.0f,
                           float door_height = 0.0f, float skirt_height = 3.0f,
                           // Anel de escotilhas de vidro na saia (0 = nenhuma). O arco de CADA vao
                           // e' PULADO automaticamente - essa decisao fica aqui, junto de
                           // door_facing_rad/door_half_angle/door_count, e nao no chamador:
                           // duplicar a geometria da porta em 2 lugares ja causou bug real neste
                           // projeto (ver a nota sobre g_shelter_door_x/y em modules_building.h).
                           int porthole_count = 0, float porthole_radius = 0.0f,
                           float porthole_glow = 0.0f, int door_count = 1);


// ============= Escotilha de eclusa (a MESMA nos dois lados da porta) =============
// Monta o portao redondo completo: chapa externa clara com aro metalico e parafusos, e por cima um
// disco interno ESCURO com aro proprio e as barras cruzadas. O contraste claro-fora / escuro-dentro e'
// o que faz o conjunto ler como porta; sem ele o disco desaparece numa parede clara.
//
// Existe como funcao unica porque o jogador reportou "a porta de saida nao parece com a porta de
// entrada": o exterior (base_exterior.cpp) e o interior (base_interior.cpp) tinham cada um a sua
// receita, e a de dentro estava com as cores invertidas (folha clara sobre parede clara) - a porta
// simplesmente nao aparecia. Com uma funcao so', elas nao podem divergir de novo.
//   center     - centro do portao, no plano da parede (o chamador ja poe na altura certa)
//   wall_angle - normal da parede (0 = +X, pi/2 = +Z), mesma convencao de render_porthole_3d
//   radius     - raio da chapa externa; todo o resto e' proporcional a ele
//   glow       - brilho do disco interno (usado pra "acender" a porta de noite)
void render_airlock_hatch_3d(Vec3 center, float wall_angle_rad, float radius, float glow);

// Caixa com as 3 dimensoes INDEPENDENTES e girada em torno de Y. render_cube_3d so' faz cubos (um
// unico `size`), e quase nada de equipamento e' cubico: a mochila a jato, por exemplo, tinha o
// comentario "corpo achatado, nao um cubo" mas era literalmente um cubo de 0.30, porque nao havia
// outra opcao. Pior: cubos posicionados com sin/cos do jogador continuam ALINHADOS AOS EIXOS, entao
// uma peca larga ficava torta quando o personagem olhava na diagonal.
//   center  - centro geometrico da caixa
//   sx      - largura (eixo "direita" do objeto)   sy - altura   sz - profundidade (eixo "frente")
//   yaw_rad - rotacao: a frente do objeto fica em (sin yaw, cos yaw), a MESMA convencao de
//             Player::rotation e do resto do corpo do personagem
// Mesmas 3 sombras por face de render_cube_3d (topo claro, 2 lados medios, 2 escuros) e neblina
// aplicada 1x no centro.
void render_box_oriented_3d(Vec3 center, float sx, float sy, float sz, float yaw_rad,
                            float r, float g, float b, float a = 1.0f);
// ============= Per-frame fog parameters (raylib migration) =============
// Legacy OpenGL fixed-function fog (glFogf/glFogi/glFogfv, enabled for the whole terrain/
// object/player/beacon render pass in main.cpp's render_world()) has no rlgl/raylib
// equivalent. render_world() computes the fog color/start/end once per frame (same values the
// old glFogfv/glFogf calls used) and stores them here; render_cube_3d()/render_wall_3d_tex()
// (this file) and the local render_plane_3d()/render_plane_3d_tex()/render_cube_3d_tex()
// functions in main.cpp all read this to manually lerp their face colors toward the fog color
// based on distance from the camera, reproducing the old per-pixel GL_LINEAR fog effect (as a
// per-quad/per-face approximation using each shape's center position instead of true
// per-vertex distance - see the migration report for why this is an acceptable simplification).
// render_world() resets fog.enabled=false once the fogged region of the frame ends (mirrors
// the original glDisable(GL_FOG) inside ui_hud.cpp's render_hud()/sky.cpp's render_alien_sky()).
struct FrameFogParams {
    bool enabled = false;
    float start = 0.0f;
    float end = 1000.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f;
};
extern FrameFogParams g_frame_fog;

// HORIZONTE DE TERRENO deste frame, em tiles: o mesmo view_radius que o laco de terreno usa para
// cortar tiles. Preenchido por render_world() logo depois de calcular view_radius, e lido por quem
// desenha estrutura FIXA fora do laco (base_exterior.cpp, base_interior.cpp).
//
// Por que existe: esses modelos cortavam por uma distancia FIXA (190/150 tiles), mas view_radius e'
// DINAMICO (110 a 380, conforme zoom da camera, altitude e g_render_quality). Com a camera rente ao
// chao ele fica em 110 - ou seja, o terreno para em 110 e a base continuava sendo desenhada a 160.
// Sem terreno na frente para ocluir, e totalmente enevoada, ela virava manchas claras contra o CEU
// acima da crista do vulcao ("estou vendo a base do outro lado do vulcao"). Estrutura nao pode ser
// desenhada mais longe do que o mundo que a esconde.
extern float g_frame_terrain_horizon;

// Parede vertical texturizada (para diferenca de altura entre tiles vizinhos). Antes eram 4
// funcoes quase identicas (render_wall_3d_tex_xpos/xneg/zpos/zneg), uma por face/eixo
// extrudado - colapsadas aqui numa unica funcao parametrizada por face (Fase 1b do plano de
// refatoracao: unica mudanca de call-site deste estagio). A ordem de vertices/winding de cada
// caso do switch (em render_primitives.cpp) e uma copia exata da funcao original
// correspondente - so a selecao de eixo/sinal virou um parametro.
enum class WallFace { XPos, XNeg, ZPos, ZNeg };
// flat=true amostra um UNICO ponto da textura (o centro do tile) nos 4 vertices em vez de
// esticar o retangulo inteiro - usado pra paredes bem baixas (diferenca de altura pequena
// entre tiles vizinhos, comum em relevo com ridge forte tipo montanha/neve): esticar a
// textura inteira numa faixa fina de poucos pixels de altura na tela causa aliasing/shimmer
// visivel (minificacao sem mipmap) que lia como "chao piscando" - amostrar um ponto so' e'
// uma cor solida de verdade, sem gradiente nenhum pra "tremer" com o movimento da camera.
void render_wall_3d_tex(WallFace face, float x, float z, float y0, float y1, Tile tile,
                         float tint_r, float tint_g, float tint_b, float a, float shade,
                         bool flat = false);
