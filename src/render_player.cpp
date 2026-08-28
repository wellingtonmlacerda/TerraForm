#include "platform.h"
#include "render_player.h"

#include "render_primitives.h"   // render_circle/render_ellipse/render_quad
#include "player_physics.h"      // Player

#include <cmath>

// ============= TOP-DOWN PLAYER RENDERING =============
// Astronauta visto de cima com 4 direcoes
void render_player_topdown(float px, float py, float scale, const Player& player) {
    // Cores do traje - MAIS CONTRASTANTES
    const float suit_r = 1.0f, suit_g = 0.95f, suit_b = 0.90f;    // Traje branco brilhante
    const float visor_r = 0.10f, visor_g = 0.50f, visor_b = 0.90f; // Visor azul vivo
    const float pack_r = 0.35f, pack_g = 0.38f, pack_b = 0.42f;    // Mochila cinza escuro
    const float gold_r = 1.0f, gold_g = 0.70f, gold_b = 0.15f;     // Detalhes dourados vivos
    const float boot_r = 0.20f, boot_g = 0.22f, boot_b = 0.25f;    // Botas escuras
    const float outline_r = 0.0f, outline_g = 0.0f, outline_b = 0.0f; // Outline preto

    // TAMANHO AUMENTADO: era 14, agora 24
    float size = 24.0f * scale;
    float outline_w = 2.0f * scale; // Largura do outline

    // Animacao de caminhada
    float walk_offset = 0.0f;
    float leg_anim = 0.0f;
    if (player.is_moving) {
        walk_offset = std::sin(player.walk_timer * 10.0f) * 1.5f * scale;
        leg_anim = std::sin(player.walk_timer * 12.0f) * 4.0f * scale;
    }

    // Offset de direcao para elementos (mochila, visor)
    float dir_x = 0.0f, dir_y = 0.0f;
    switch (player.facing_dir) {
        case 0: dir_y = -1.0f; break;  // Norte
        case 1: dir_x = 1.0f;  break;  // Leste
        case 2: dir_y = 1.0f;  break;  // Sul
        case 3: dir_x = -1.0f; break;  // Oeste
    }

    float center_x = px;
    float center_y = py;

    // === SOMBRA GRANDE E VISIVEL ===
    render_ellipse(center_x + 3.0f * scale, center_y + 6.0f * scale,
                   size * 0.55f, size * 0.30f, 0.0f, 0.0f, 0.0f, 0.5f);

    // === MOCHILA (atras do jogador) ===
    float pack_offset = 7.0f * scale;
    float pack_x = center_x - dir_x * pack_offset;
    float pack_y = center_y - dir_y * pack_offset;

    // Outline da mochila
    render_circle(pack_x, pack_y, size * 0.32f + outline_w, outline_r, outline_g, outline_b, 1.0f);
    // Mochila
    render_circle(pack_x, pack_y, size * 0.32f, pack_r, pack_g, pack_b, 1.0f);
    // Tanques de oxigenio na mochila
    render_ellipse(pack_x - 3.0f * scale, pack_y, 2.5f * scale, 4.0f * scale, 0.50f, 0.55f, 0.60f, 1.0f);
    render_ellipse(pack_x + 3.0f * scale, pack_y, 2.5f * scale, 4.0f * scale, 0.50f, 0.55f, 0.60f, 1.0f);

    // === PERNAS (animadas) ===
    float leg_offset = 5.0f * scale;
    float leg_size = 4.0f * scale;

    // Perna esquerda
    float left_leg_x = center_x;
    float left_leg_y = center_y;
    if (player.facing_dir == 0 || player.facing_dir == 2) {
        left_leg_x -= leg_offset;
        left_leg_y += (player.facing_dir == 0 ? 1 : -1) * leg_anim * 0.3f;
    } else {
        left_leg_y -= leg_offset;
        left_leg_x += (player.facing_dir == 1 ? -1 : 1) * leg_anim * 0.3f;
    }
    // Outline perna esquerda
    render_circle(left_leg_x, left_leg_y, leg_size + outline_w, outline_r, outline_g, outline_b, 1.0f);
    render_circle(left_leg_x, left_leg_y, leg_size, boot_r, boot_g, boot_b, 1.0f);

    // Perna direita
    float right_leg_x = center_x;
    float right_leg_y = center_y;
    if (player.facing_dir == 0 || player.facing_dir == 2) {
        right_leg_x += leg_offset;
        right_leg_y -= (player.facing_dir == 0 ? 1 : -1) * leg_anim * 0.3f;
    } else {
        right_leg_y += leg_offset;
        right_leg_x -= (player.facing_dir == 1 ? -1 : 1) * leg_anim * 0.3f;
    }
    // Outline perna direita
    render_circle(right_leg_x, right_leg_y, leg_size + outline_w, outline_r, outline_g, outline_b, 1.0f);
    render_circle(right_leg_x, right_leg_y, leg_size, boot_r, boot_g, boot_b, 1.0f);

    // === CORPO (circulo principal) ===
    // Outline do corpo
    render_circle(center_x, center_y + walk_offset, size * 0.5f + outline_w, outline_r, outline_g, outline_b, 1.0f);
    render_circle(center_x, center_y + walk_offset, size * 0.5f, suit_r, suit_g, suit_b, 1.0f);

    // Detalhe do traje (faixa dourada)
    if (player.facing_dir == 0 || player.facing_dir == 2) {
        render_quad(center_x - size * 0.4f, center_y + walk_offset - 1.5f * scale,
                   size * 0.8f, 3.0f * scale, gold_r, gold_g * 0.8f, 0.2f, 0.9f);
    } else {
        render_quad(center_x - 1.5f * scale, center_y + walk_offset - size * 0.4f,
                   3.0f * scale, size * 0.8f, gold_r, gold_g * 0.8f, 0.2f, 0.9f);
    }

    // === CAPACETE (cabeca) ===
    float head_offset = size * 0.18f;
    float head_x = center_x + dir_x * head_offset;
    float head_y = center_y + walk_offset + dir_y * head_offset;

    // Outline do capacete
    render_circle(head_x, head_y, size * 0.40f + outline_w, outline_r, outline_g, outline_b, 1.0f);
    // Capacete branco
    render_circle(head_x, head_y, size * 0.40f, suit_r, suit_g, suit_b, 1.0f);

    // Borda dourada do capacete
    render_circle(head_x, head_y, size * 0.42f, gold_r, gold_g, gold_b, 0.4f);

    // === VISOR (indica direcao) ===
    float visor_dist = size * 0.25f;
    float visor_x = head_x + dir_x * visor_dist;
    float visor_y = head_y + dir_y * visor_dist;

    // Outline do visor
    render_circle(visor_x, visor_y, size * 0.20f + outline_w * 0.5f, outline_r, outline_g, outline_b, 1.0f);
    // Visor azul reflexivo
    render_circle(visor_x, visor_y, size * 0.20f, visor_r, visor_g, visor_b, 1.0f);

    // Reflexo no visor
    float ref_intensity = 0.5f + 0.2f * std::sin(player.anim_frame * 0.8f);
    render_circle(visor_x - 2.0f * scale * (1.0f - std::fabs(dir_x)),
                 visor_y - 2.0f * scale * (1.0f - std::fabs(dir_y)),
                 size * 0.08f, 1.0f, 1.0f, 1.0f, ref_intensity);

    // === ANTENA ===
    float antenna_x = head_x - dir_x * size * 0.28f + (dir_y != 0 ? 4.0f * scale : 0);
    float antenna_y = head_y - dir_y * size * 0.28f + (dir_x != 0 ? -4.0f * scale : 0);
    render_circle(antenna_x, antenna_y, 2.5f * scale, 0.3f, 0.32f, 0.35f, 1.0f);
    // Luz da antena (pisca)
    float blink = (std::sin(player.anim_frame * 4.0f) > 0.0f) ? 1.0f : 0.3f;
    render_circle(antenna_x, antenna_y, 1.5f * scale, 1.0f * blink, 0.2f * blink, 0.2f * blink, 1.0f);

    // === FERRAMENTA (quando minerando) ===
    if (player.is_mining) {
        float tool_dist = size * 0.7f;
        float tool_x = center_x + dir_x * tool_dist;
        float tool_y = center_y + dir_y * tool_dist;
        float mine_swing = std::sin(player.mine_anim * 15.0f) * 4.0f * scale;

        // Picareta - outline
        render_quad(tool_x - 2.0f * scale, tool_y - 2.0f * scale + mine_swing,
                   4.0f * scale, 12.0f * scale, 0.0f, 0.0f, 0.0f, 1.0f);
        render_quad(tool_x - 1.5f * scale, tool_y - 1.5f * scale + mine_swing,
                   3.0f * scale, 10.0f * scale, 0.55f, 0.35f, 0.2f, 1.0f);
        render_quad(tool_x - 5.0f * scale, tool_y - 3.0f * scale + mine_swing,
                   10.0f * scale, 3.0f * scale, 0.5f, 0.5f, 0.55f, 1.0f);
    }

    // === LUZ DE STATUS ===
    float status_x = center_x + (dir_x == 0 ? 5.0f * scale : 0);
    float status_y = center_y + walk_offset + (dir_y == 0 ? -5.0f * scale : 0);
    float status_blink = (std::sin(player.anim_frame * 3.0f) > 0.0f) ? 1.0f : 0.5f;
    render_circle(status_x, status_y, 2.0f * scale, 0.2f * status_blink, 1.0f * status_blink, 0.3f * status_blink, 1.0f);
}

// Wrapper para compatibilidade (ignora in_water em top-down)
void render_astronaut(float px, float py, float scale, const Player& player, bool /*in_water*/) {
    render_player_topdown(px, py, scale, player);
}
