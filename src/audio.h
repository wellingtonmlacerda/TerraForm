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

// ============= Combate com criaturas =============
// Sintetizados como todo o resto deste modulo (nao ha arquivo WAV no projeto). `pitch` vem de
// EnemyArchetype::sound_pitch: grave nos pesados (0.48 no Alpha), agudo nos leves (1.35 no
// Crawler) - e' o mesmo som de base reaproveitado, com pitch/volume variados, em vez de dezenas
// de arquivos. Cada chamada aplica ainda um jitter pequeno pra o tiro repetido nao soar identico.
//
// Antes NAO existia som nenhum ao acertar criatura nem ao ser atingido por ela - o jogador so'
// via o numero de HP mudar.
void play_creature_hit_light_sound(float pitch);    // impacto em corpo mole (Crawler/Stalker)
void play_creature_hit_armored_sound(float pitch);  // impacto em carapaca (Brute/Alpha)
void play_creature_death_sound(float pitch);        // morte
void play_creature_windup_sound(float pitch);       // telegrafe: criatura iniciando o golpe
void play_creature_attack_hit_sound(float pitch);   // criatura ACERTOU o jogador
