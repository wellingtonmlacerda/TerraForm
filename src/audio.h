#pragma once

// ============= Audio =============
// Sistema minimo de audio (pistola de laser, jetpack, musica ambiente) - pedido do
// jogador. Nao existe nenhum arquivo de asset de audio no projeto e nao ha como buscar/
// gerar gravacoes reais - tudo (efeitos + musica) e' sintetizado em codigo (ondas simples
// via raylib Wave/Sound e Music/LoadMusicStreamFromMemory) uma unica vez, na
// inicializacao (ver init_game_audio(), chamado de win32_platform.cpp logo apos
// InitWindow()).
void init_game_audio();
void shutdown_game_audio();

// Chamar 1x por frame (win32_platform.cpp, incondicional - musica/jetpack tocam mesmo com
// o jogo pausado, mesma expectativa comum de "musica de fundo continua"). Atualiza os
// streams de Music (obrigatorio pra raylib manter o buffer alimentado) e liga/desliga o
// loop do jetpack conforme g_player.jetpack_active mudar de estado.
void update_game_audio(float dt);

// Aplica volume/liga-desliga de musica e efeitos aos sons/musica ja carregados - chamado
// pelo menu de Configuracoes (ui_menu.cpp) sempre que o jogador ajusta um desses 4 campos
// de GameSettings (game_state.h), e 1x no init_game_audio() com os valores padrao.
void apply_audio_settings(float music_volume, bool music_enabled, float sfx_volume, bool sfx_enabled);

// Tocado a cada disparo (acerto ou erro) e a cada impacto real (terreno ou criatura) -
// ver try_fire_laser_pistol() em creatures.cpp. Respeita sfx_enabled/sfx_volume.
void play_laser_fire_sound();
void play_laser_impact_sound();

// Tocado quando um meteoro pousa de verdade (ver update_meteors(), main.cpp) - "boom"
// bem maior/mais grave que o impacto da pistola. Respeita sfx_enabled/sfx_volume.
void play_meteor_impact_sound();

// Tocado quando a agua apaga um tile de lava (ver update_water_flow(), world.cpp) - chiado de vapor.
// Respeita sfx_enabled/sfx_volume.
void play_steam_hiss_sound();
