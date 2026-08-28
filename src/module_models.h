#pragma once

// ============= Modelos 3D dos modulos da colonia =============
// Todo modulo era desenhado como UM CUBO TEXTURIZADO pelo laco de objetos de main.cpp: is_module()
// so' ligava emissivo e contorno, nunca mudava a FORMA. A textura e' um 16x16 com listras pintadas,
// entao um Painel Solar aparecia no mundo como uma caixa azul listrada. Reclamacao direta do
// jogador: "o painel solar nao parece um painel solar! As construcoes tem que parecer com o que
// elas dizem ser."
//
// Mesma solucao que o exterior da base ja usa (base_exterior.h): o BLOCO no mundo continua sendo
// colisao/ancora, e a APARENCIA vem de geometria propria desenhada aqui. O laco de objetos passa a
// pular os cubos de modulo (como ja pula mobilia e casca da base).
//
// Fonte dos dados: g_modules (posicao, tipo, upgraded, status) e g_construction_queue (obra em
// andamento). Este arquivo nao declara nenhum dado de gameplay - so' desenha.

// Desenha todos os modulos construidos e as obras em andamento. Chamar de render_world() (main.cpp)
// depois do terreno e antes do jogador, com fog/lightmap do frame ja preenchidos.
void render_module_models();
