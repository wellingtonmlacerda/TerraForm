#include "audio.h"
#include "raylib_platform.h"
#include "player_physics.h"   // g_player.jetpack_active
#include "game_state.h"       // GameState, g_state

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <vector>

// Sons sintetizados em codigo (sem asset externo) - ver audio.h pro porque. Tudo via
// Sound/LoadSoundFromWave (nao Music/streaming) - a 1a versao usava Music pra musica
// ambiente/jetpack (loop nativo), mas o resultado soou mal ("ruido de internet discada",
// feedback do jogador) - Sound + reinicio manual no fim do loop e' o MESMO caminho ja
// confirmado funcionando bem pros efeitos da pistola de laser, entao os loops longos
// passam a usar exatamente essa mesma rota, sem depender do decoder de streaming.
namespace {

constexpr unsigned int kSampleRate = 22050;
constexpr float kPi2 = 6.28318530718f;
// Duracoes exatas dos loops sintetizados abaixo (synth_ambient/synth_jetpack) - o reinicio
// em update_game_audio() usa isso diretamente (timer por dt), NAO IsSoundPlaying(): checar
// "ainda esta tocando" e reiniciar quando some e' fragil aqui (relatorio do jogador: musica
// as vezes nem tocava, efeitos "gaguejavam") - se IsSoundPlaying() der um falso-negativo
// por 1-2 frames logo apos o PlaySound (latencia normal de qualquer backend de audio), o
// codigo reiniciava o som DE NOVO no mesmo instante, repetidas vezes por segundo, o que soa
// como gagueira/nada tocando em vez de um loop continuo. Um timer conhecido (a duracao que
// a propria sintese usou) elimina essa corrida por completo.
constexpr float kAmbientLoopDuration = 8.0f; // 4 compassos de 2.0s (ver synth_ambient)
constexpr float kJetpackLoopDuration = 2.0f;

Sound g_laser_fire_sound{};
Sound g_laser_impact_sound{};
Sound g_meteor_impact_sound{};
Sound g_steam_hiss_sound{};
Sound g_ambient_sound{};
Sound g_jetpack_sound{};
bool g_audio_ready = false;
bool g_jetpack_playing = false;
float g_ambient_timer = 0.0f;
float g_jetpack_timer = 0.0f;

float g_music_volume = 0.6f;
bool g_music_enabled = true;
float g_sfx_volume = 0.8f;
bool g_sfx_enabled = true;

int16_t to_sample(float v) {
    return (int16_t)std::clamp(v, -32000.0f, 32000.0f);
}

Wave make_wave(unsigned int frame_count) {
    Wave w{};
    w.frameCount = frame_count;
    w.sampleRate = kSampleRate;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = malloc((size_t)frame_count * sizeof(int16_t));
    return w;
}

// "Pew" de laser: varredura de frequencia descendente (1900Hz -> 600Hz) com decaimento
// rapido, mais um harmonico leve pra nao soar tao "puro"/sintetico demais.
Sound synth_laser_fire() {
    const float duration = 0.14f;
    unsigned int n = (unsigned int)(duration * (float)kSampleRate);
    Wave w = make_wave(n);
    int16_t* samples = (int16_t*)w.data;
    for (unsigned int i = 0; i < n; ++i) {
        float t = (float)i / (float)kSampleRate;
        float progress = (float)i / (float)n;
        float freq = 1900.0f - progress * 1300.0f;
        float phase = kPi2 * freq * t;
        float envelope = std::pow(1.0f - progress, 1.6f);
        float sample = std::sin(phase) * envelope;
        sample += 0.25f * std::sin(phase * 2.0f) * envelope;
        samples[i] = to_sample(sample * 9000.0f);
    }
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

// Impacto: ruido filtrado (LCG simples) + um "thump" grave descendo, decaimento rapido -
// le como uma pancada/explosao curta, nao um bipe.
Sound synth_laser_impact() {
    const float duration = 0.18f;
    unsigned int n = (unsigned int)(duration * (float)kSampleRate);
    Wave w = make_wave(n);
    int16_t* samples = (int16_t*)w.data;
    uint32_t seed = 12345u;
    for (unsigned int i = 0; i < n; ++i) {
        float t = (float)i / (float)kSampleRate;
        float progress = (float)i / (float)n;
        seed = seed * 1664525u + 1013904223u;
        float noise = ((float)(seed & 0xFFFFu) / 32768.0f) - 1.0f;
        float thump_freq = 180.0f - progress * 80.0f;
        float thump = std::sin(kPi2 * thump_freq * t);
        float envelope = std::pow(1.0f - progress, 2.2f);
        float sample = (noise * 0.55f + thump * 0.65f) * envelope;
        samples[i] = to_sample(sample * 9000.0f);
    }
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

// Impacto de meteoro - "boom" bem maior/mais grave/mais longo que o da pistola: sub-grave
// descendo bem devagar + ruido passa-baixa forte (rugido, mesma tecnica do rugido do
// jetpack) + um estalo inicial breve de ruido cru (o "crack" do impacto antes do rugido).
Sound synth_meteor_impact() {
    const float duration = 0.55f;
    unsigned int n = (unsigned int)(duration * (float)kSampleRate);
    Wave w = make_wave(n);
    int16_t* samples = (int16_t*)w.data;
    uint32_t seed = 54321u;
    float roar_prev = 0.0f;
    for (unsigned int i = 0; i < n; ++i) {
        float t = (float)i / (float)kSampleRate;
        float progress = (float)i / (float)n;

        seed = seed * 1664525u + 1013904223u;
        float noise = ((float)(seed & 0xFFFFu) / 32768.0f) - 1.0f;
        roar_prev = roar_prev * 0.92f + noise * 0.08f; // rugido grave filtrado (igual ao jetpack)

        float crack_env = std::pow(std::max(0.0f, 1.0f - progress * 14.0f), 1.5f); // so' o 1o instante
        float boom_freq = 90.0f - progress * 55.0f;
        float boom = std::sin(kPi2 * boom_freq * t);
        float boom_env = std::pow(1.0f - progress, 1.3f);

        float sample = noise * crack_env * 0.9f + roar_prev * boom_env * 0.85f + boom * boom_env * 0.6f;
        samples[i] = to_sample(sample * 10500.0f);
    }
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}


// Agua apagando lava - CHIADO de vapor: ruido passa-alta (o "sss") com envelope de ataque rapido e
// cauda longa, mais um sopro grave por baixo (a massa de vapor saindo). Sem tom definido de proposito:
// vapor nao tem altura, e qualquer seno reconhecivel aqui soaria como apito de chaleira.
Sound synth_steam_hiss() {
    const float duration = 0.85f;
    unsigned int n = (unsigned int)(duration * (float)kSampleRate);
    Wave w = make_wave(n);
    int16_t* samples = (int16_t*)w.data;
    uint32_t seed = 9182736u;
    float lp_prev = 0.0f, hp_prev = 0.0f, hp_prev_in = 0.0f;
    for (unsigned int i = 0; i < n; ++i) {
        float progress = (float)i / (float)n;
        seed = seed * 1664525u + 1013904223u;
        float noise = ((float)(seed & 0xFFFFu) / 32768.0f) - 1.0f;

        // Passa-alta simples (1a ordem): deixa o sibilo e corta o corpo -> "sss" em vez de "shh".
        float hp = 0.86f * (hp_prev + noise - hp_prev_in);
        hp_prev = hp; hp_prev_in = noise;
        // Sopro grave por baixo (passa-baixa do mesmo ruido).
        lp_prev = lp_prev * 0.94f + noise * 0.06f;

        // Ataque em ~4% da duracao, cauda longa: e' o perfil de um jato de vapor escapando.
        float attack = std::min(1.0f, progress / 0.04f);
        float decay = std::pow(1.0f - progress, 1.7f);
        float env = attack * decay;
        // Tremulo lento - vapor nao sai constante.
        float flutter = 0.82f + 0.18f * std::sin(kPi2 * 11.0f * progress);

        float sample = (hp * 0.85f + lp_prev * 0.55f) * env * flutter;
        samples[i] = to_sample(sample * 8200.0f);
    }
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

// ---- Helpers de sintese "chiptune" (NES-like: pulso/quadrada + triangulo + ruido) ----
// A 1a versao da musica ambiente era um pad de senos puros "respirando" - som de synth
// ambiente, nao de jogo. Pedido do jogador: soar como trilha de Nintendo dos anos 80/90/
// 2000 - o que da esse carater NAO e' so a melodia, e' o TIMBRE (onda quadrada/pulso pra
// melodia/harmonia, triangulo pro baixo, ruido curto pra percussao - os 4 "canais" que o
// NES realmente tinha). Reescrito do zero como composicao de verdade (melodia + baixo +
// arpejo + percussao), nao mais um drone.

float note_freq(int semitone_from_a4) {
    return 440.0f * std::pow(2.0f, (float)semitone_from_a4 / 12.0f);
}

// Onda quadrada/pulso (duty=0.5 e' quadrada pura, duty menor e' "pulso" mais fino/nasal -
// o NES tinha varios duty cycles por canal, usado aqui pra diferenciar melodia de harmonia).
float pulse_wave(float phase_cycles, float duty) {
    float ph = phase_cycles - std::floor(phase_cycles);
    return (ph < duty) ? 1.0f : -1.0f;
}

// Onda triangular (canal de baixo classico do NES - mais suave que quadrada, sem harmonicos
// pares fortes).
float triangle_wave(float phase_cycles) {
    float ph = phase_cycles - std::floor(phase_cycles);
    if (ph < 0.5f) return -1.0f + 4.0f * ph;
    return 3.0f - 4.0f * ph;
}

// Envelope simples (ataque linear rapido + sustain + release linear) pra cada nota
// individual nao "clicar" na fronteira com a proxima.
float note_envelope(float t_rel, float dur, float attack, float release) {
    if (t_rel < attack) return t_rel / attack;
    float rel_start = dur - release;
    if (t_rel > rel_start) return std::max(0.0f, (dur - t_rel) / release);
    return 1.0f;
}

struct MelodyNote { int semitone; int eighths; };

// Musica ambiente - composicao de verdade em La menor (progressao i-VI-III-VII: Am-F-C-G,
// bem comum em trilhas de aventura/misterio classicas), 4 compassos, tocada via reinicio
// manual (ver update_game_audio/kAmbientLoopDuration). Camadas: melodia (pulso 50%),
// arpejo de acompanhamento (pulso 25%, 1 oitava acima, ciclando fundamental-terca-quinta-
// terca em semicolcheias - a forma classica do NES "fingir" acordes com so' 1 nota por
// vez), baixo (triangulo, fundamental sustentada por compasso) e percussao simples
// (ruido: batida forte no tempo 1 de cada compasso, "chimbal" leve nos demais tempos).
Sound synth_ambient() {
    const float eighth_dur = 0.25f;
    const float bar_dur = eighth_dur * 8.0f; // 2.0s
    const float T = bar_dur * 4.0f;          // 8.0s - deve bater com kAmbientLoopDuration
    unsigned int n = (unsigned int)(T * (float)kSampleRate);
    Wave w = make_wave(n);
    int16_t* raw = (int16_t*)w.data;
    std::vector<float> mix(n, 0.0f);

    // Melodia por compasso (Am / F / C / G) - frase unica que sobe e depois resolve.
    const std::vector<std::vector<MelodyNote>> bar_melody = {
        {{0, 1}, {3, 1}, {2, 1}, {0, 1}, {-5, 1}, {0, 1}, {-2, 1}, {-5, 1}},
        {{-4, 1}, {0, 1}, {-2, 1}, {-4, 1}, {-5, 1}, {-7, 1}, {-9, 1}, {-10, 1}},
        {{0, 1}, {3, 1}, {5, 1}, {3, 1}, {2, 1}, {0, 1}, {-2, 1}, {-5, 1}},
        {{-4, 1}, {-5, 1}, {-7, 1}, {-9, 1}, {-10, 1}, {-12, 3}},
    };
    const int bass_note[4] = {-12, -16, -21, -14};  // A3, F3, C3, G3
    const int arp_root[4]  = {0, -4, -9, -2};
    const int arp_third[4] = {3, 0, -5, 2};         // menor (Am) ou maior (F/C/G) conforme o acorde
    const int arp_fifth[4] = {7, 3, -2, 5};

    // Melodia (pulso 50%).
    for (int bar = 0; bar < 4; ++bar) {
        float bar_start = (float)bar * bar_dur;
        float cursor = 0.0f;
        for (const MelodyNote& note : bar_melody[(size_t)bar]) {
            float note_start = bar_start + cursor;
            float note_dur = (float)note.eighths * eighth_dur;
            float freq = note_freq(note.semitone);
            unsigned int i0 = (unsigned int)(note_start * (float)kSampleRate);
            unsigned int i1 = std::min(n, (unsigned int)((note_start + note_dur) * (float)kSampleRate));
            float attack = std::min(0.01f, note_dur * 0.2f);
            float release = std::min(0.04f, note_dur * 0.3f);
            for (unsigned int i = i0; i < i1; ++i) {
                float t = (float)i / (float)kSampleRate;
                float env = note_envelope(t - note_start, note_dur, attack, release);
                mix[i] += pulse_wave(freq * t, 0.5f) * env * 0.34f;
            }
            cursor += note_dur;
        }
    }

    // Baixo (triangulo, fundamental sustentada o compasso inteiro).
    for (int bar = 0; bar < 4; ++bar) {
        float bar_start = (float)bar * bar_dur;
        float freq = note_freq(bass_note[bar]);
        unsigned int i0 = (unsigned int)(bar_start * (float)kSampleRate);
        unsigned int i1 = std::min(n, (unsigned int)((bar_start + bar_dur) * (float)kSampleRate));
        for (unsigned int i = i0; i < i1; ++i) {
            float t = (float)i / (float)kSampleRate;
            float env = note_envelope(t - bar_start, bar_dur, 0.02f, 0.05f);
            mix[i] += triangle_wave(freq * t) * env * 0.30f;
        }
    }

    // Arpejo (pulso 25%, 1 oitava acima) - fundamental/terca/quinta/terca em semicolcheias.
    const float sixteenth_dur = eighth_dur * 0.5f;
    for (int bar = 0; bar < 4; ++bar) {
        float bar_start = (float)bar * bar_dur;
        int pattern[4] = {arp_root[bar], arp_third[bar], arp_fifth[bar], arp_third[bar]};
        for (int s = 0; s < 16; ++s) {
            float note_start = bar_start + (float)s * sixteenth_dur;
            float freq = note_freq(pattern[s % 4] + 12);
            unsigned int i0 = (unsigned int)(note_start * (float)kSampleRate);
            unsigned int i1 = std::min(n, (unsigned int)((note_start + sixteenth_dur) * (float)kSampleRate));
            float release = std::min(0.03f, sixteenth_dur * 0.4f);
            for (unsigned int i = i0; i < i1; ++i) {
                float t = (float)i / (float)kSampleRate;
                float env = note_envelope(t - note_start, sixteenth_dur, 0.003f, release);
                mix[i] += pulse_wave(freq * t, 0.25f) * env * 0.15f;
            }
        }
    }

    // Percussao (ruido) - batida no tempo 1 de cada compasso (grave, com um "thump" de
    // seno por baixo pra dar peso) + chimbal leve nos outros 7 tempos.
    uint32_t seed = 999u;
    for (int bar = 0; bar < 4; ++bar) {
        for (int e = 0; e < 8; ++e) {
            float note_start = (float)bar * bar_dur + (float)e * eighth_dur;
            bool is_kick = (e == 0);
            float dur = is_kick ? 0.14f : 0.05f;
            unsigned int i0 = (unsigned int)(note_start * (float)kSampleRate);
            unsigned int i1 = std::min(n, (unsigned int)((note_start + dur) * (float)kSampleRate));
            for (unsigned int i = i0; i < i1; ++i) {
                float t_rel = (float)(i - i0) / (float)kSampleRate;
                float env = std::pow(std::max(0.0f, 1.0f - t_rel / dur), is_kick ? 1.5f : 2.5f);
                seed = seed * 1664525u + 1013904223u;
                float noise = ((float)(seed & 0xFFFFu) / 32768.0f) - 1.0f;
                float sample = noise * env * (is_kick ? 0.22f : 0.08f);
                if (is_kick) sample += std::sin(kPi2 * 62.0f * t_rel) * env * 0.20f;
                mix[i] += sample;
            }
        }
    }

    // Fade in/out global bem curto (evita clique no reinicio manual do loop, ver
    // update_game_audio) + normalizacao final pra int16.
    unsigned int fade_n = (unsigned int)(0.015f * (float)kSampleRate);
    for (unsigned int i = 0; i < n; ++i) {
        float g = 1.0f;
        if (i < fade_n) g = (float)i / (float)fade_n;
        else if (i > n - fade_n) g = (float)(n - i) / (float)fade_n;
        raw[i] = to_sample(mix[i] * g * 12000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

// Propulsor do jetpack: dominado por ruido grave filtrado (rugido/whoosh de foguete, nao
// um zumbido de motor eletrico) - a 1a versao era tom-dominante (2 senos + pouco ruido) e
// soou fraco/sintetico ("nao ficou nada bom", feedback do jogador). Reformulado: ruido
// passa-baixa forte (filtro de 1 polo bem lento) pro "rugido" grave de fundo, um sub-grave
// senoidal por peso, uma camada de "chiado" de escape (ruido menos filtrado, bem baixo) e
// uma leve flutuacao de amplitude (turbulencia de combustao) - tudo somado da o carater de
// rugido de foguete em vez de hum eletronico.
Sound synth_jetpack() {
    const float T = kJetpackLoopDuration;
    unsigned int n = (unsigned int)(T * (float)kSampleRate);
    Wave w = make_wave(n);
    int16_t* samples = (int16_t*)w.data;

    auto quantize = [&](float f) { return std::round(f * T) / T; };
    float sub_f = quantize(58.0f);
    float flutter_f = quantize(11.0f);

    uint32_t seed = 4242u;
    float roar_prev = 0.0f;
    float hiss_prev = 0.0f;
    for (unsigned int i = 0; i < n; ++i) {
        float t = (float)i / (float)kSampleRate;

        seed = seed * 1664525u + 1013904223u;
        float noise_a = ((float)(seed & 0xFFFFu) / 32768.0f) - 1.0f;
        roar_prev = roar_prev * 0.90f + noise_a * 0.10f; // rugido grave, bem filtrado

        seed = seed * 1664525u + 1013904223u;
        float noise_b = ((float)(seed & 0xFFFFu) / 32768.0f) - 1.0f;
        hiss_prev = hiss_prev * 0.55f + noise_b * 0.45f; // chiado de escape, bem menos filtrado

        float sub = std::sin(kPi2 * sub_f * t);
        float flutter = 0.85f + 0.15f * std::sin(kPi2 * flutter_f * t);

        float sample = (roar_prev * 0.75f + sub * 0.35f + hiss_prev * 0.18f) * flutter;
        samples[i] = to_sample(sample * 8500.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

} // namespace

void init_game_audio() {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;

    g_laser_fire_sound = synth_laser_fire();
    g_laser_impact_sound = synth_laser_impact();
    g_meteor_impact_sound = synth_meteor_impact();
    g_steam_hiss_sound = synth_steam_hiss();
    g_ambient_sound = synth_ambient();
    g_jetpack_sound = synth_jetpack();
    g_audio_ready = true;

    apply_audio_settings(g_music_volume, g_music_enabled, g_sfx_volume, g_sfx_enabled);
    // NAO toca aqui - a musica so' deve comecar quando o jogo de fato comecar (pedido do
    // jogador: tocava desde a tela de titulo). update_game_audio() cuida de ligar assim que
    // g_state sair de GameState::Menu.
}

void shutdown_game_audio() {
    if (!g_audio_ready) return;
    UnloadSound(g_laser_fire_sound);
    UnloadSound(g_laser_impact_sound);
    UnloadSound(g_meteor_impact_sound);
    UnloadSound(g_steam_hiss_sound);
    UnloadSound(g_ambient_sound);
    UnloadSound(g_jetpack_sound);
    CloseAudioDevice();
    g_audio_ready = false;
}

void update_game_audio(float dt) {
    if (!g_audio_ready) return;

    // Musica ambiente: so' toca depois que o jogo de verdade comecar (nao na tela de
    // titulo) - pedido do jogador. Continua tocando durante pausa/configuracoes/morte
    // (mesma sessao de jogo), so' para de verdade ao voltar pro Menu Principal.
    bool should_play_music = g_music_enabled && g_state != GameState::Menu;
    static bool music_session_active = false;
    if (should_play_music && !music_session_active) {
        PlaySound(g_ambient_sound);
        g_ambient_timer = 0.0f;
        music_session_active = true;
    } else if (!should_play_music && music_session_active) {
        StopSound(g_ambient_sound);
        g_ambient_timer = 0.0f;
        music_session_active = false;
    }
    if (should_play_music) {
        g_ambient_timer += dt;
        if (g_ambient_timer >= kAmbientLoopDuration) {
            g_ambient_timer -= kAmbientLoopDuration;
            PlaySound(g_ambient_sound);
        }
    }

    bool active = g_player.jetpack_active && g_sfx_enabled;
    if (active) {
        if (!g_jetpack_playing) {
            PlaySound(g_jetpack_sound);
            g_jetpack_playing = true;
            g_jetpack_timer = 0.0f;
        } else {
            g_jetpack_timer += dt;
            if (g_jetpack_timer >= kJetpackLoopDuration) {
                g_jetpack_timer -= kJetpackLoopDuration;
                PlaySound(g_jetpack_sound);
            }
        }
    } else if (g_jetpack_playing) {
        StopSound(g_jetpack_sound);
        g_jetpack_playing = false;
        g_jetpack_timer = 0.0f;
    }
}

void apply_audio_settings(float music_volume, bool music_enabled, float sfx_volume, bool sfx_enabled) {
    g_music_volume = std::clamp(music_volume, 0.0f, 1.0f);
    g_music_enabled = music_enabled;
    g_sfx_volume = std::clamp(sfx_volume, 0.0f, 1.0f);
    g_sfx_enabled = sfx_enabled;
    if (!g_audio_ready) return;

    SetSoundVolume(g_ambient_sound, g_music_volume);
    SetSoundVolume(g_jetpack_sound, g_sfx_volume * 0.8f);

    // So' mexe no volume aqui - iniciar/parar de tocar de verdade e' todo decidido em
    // update_game_audio() (unico lugar que sabe o estado atual, g_music_enabled + g_state) -
    // ter os dois lugares decidindo isso causava tiro duplo (musica tocando 2x sobreposta)
    // ao ligar pelo menu de Configuracoes.
    if (!g_sfx_enabled && g_jetpack_playing) {
        StopSound(g_jetpack_sound);
        g_jetpack_playing = false;
        g_jetpack_timer = 0.0f;
    }
}

void play_laser_fire_sound() {
    if (!g_audio_ready || !g_sfx_enabled) return;
    SetSoundVolume(g_laser_fire_sound, g_sfx_volume);
    PlaySound(g_laser_fire_sound);
}

void play_laser_impact_sound() {
    if (!g_audio_ready || !g_sfx_enabled) return;
    SetSoundVolume(g_laser_impact_sound, g_sfx_volume);
    PlaySound(g_laser_impact_sound);
}

void play_meteor_impact_sound() {
    if (!g_audio_ready || !g_sfx_enabled) return;
    SetSoundVolume(g_meteor_impact_sound, g_sfx_volume);
    PlaySound(g_meteor_impact_sound);
}

void play_steam_hiss_sound() {
    if (!g_audio_ready || !g_sfx_enabled) return;
    SetSoundVolume(g_steam_hiss_sound, g_sfx_volume);
    PlaySound(g_steam_hiss_sound);
}
