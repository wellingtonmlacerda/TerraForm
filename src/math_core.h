#pragma once

// ============= Math core: Vec2/Vec3, vector helpers, daylight, tile coords =============
// Extracted verbatim from main.cpp (original lines ~27-105).

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vec2 vec2_add(const Vec2& a, const Vec2& b);
Vec2 vec2_sub(const Vec2& a, const Vec2& b);
Vec2 vec2_scale(const Vec2& v, float s);
float vec2_dot(const Vec2& a, const Vec2& b);
float vec2_length(const Vec2& v);
Vec2 vec2_normalize(const Vec2& v);
Vec2 vec2_lerp(const Vec2& a, const Vec2& b, float t);

// Operacoes Vec3
Vec3 vec3_add(const Vec3& a, const Vec3& b);
Vec3 vec3_sub(const Vec3& a, const Vec3& b);
Vec3 vec3_scale(const Vec3& v, float s);
float vec3_dot(const Vec3& a, const Vec3& b);
float vec3_length(const Vec3& v);
Vec3 vec3_normalize(const Vec3& v);
Vec3 vec3_cross(const Vec3& a, const Vec3& b);

static constexpr float kPi = 3.1415926535f;
float clamp01(float v);
float smoothstep01(float edge0, float edge1, float x);

// Dia/noite coerente com a trajetoria do sol (mesma fase usada em render_alien_sky).
// Retorna 0..1 (0 = noite, 1 = pico do dia).
float compute_daylight(float day_phase);

// 0..1 (1 = noite escura, 0 = dia).
float compute_night_alpha(float day_phase);

// Escala vertical do heightmap (suaviza o relevo e evita "degraus" grandes por tile).
// 1 unidade no heightmap = kHeightScale unidades no mundo 3D.
static constexpr float kHeightScale = 0.25f;
static constexpr bool DEBUG_DRAW_COLLISIONS = false;

// Conversao consistente entre coordenadas de mundo e indice de tile.
// Os tiles renderizados sao centrados no indice inteiro (tx, tz), com bounds [tx-0.5, tx+0.5].
int world_to_tile(float v);
float tile_center(int t);
float tile_min(int t);
float tile_max(int t);
