#include "math_core.h"

#include <algorithm>
#include <cmath>

Vec2 vec2_add(const Vec2& a, const Vec2& b) { return {a.x + b.x, a.y + b.y}; }
Vec2 vec2_sub(const Vec2& a, const Vec2& b) { return {a.x - b.x, a.y - b.y}; }
Vec2 vec2_scale(const Vec2& v, float s) { return {v.x * s, v.y * s}; }
float vec2_dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
float vec2_length(const Vec2& v) { return std::sqrt(v.x * v.x + v.y * v.y); }
Vec2 vec2_normalize(const Vec2& v) {
    float len = vec2_length(v);
    if (len < 1e-5f) return {0.0f, 0.0f};
    return {v.x / len, v.y / len};
}
Vec2 vec2_lerp(const Vec2& a, const Vec2& b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

// Operacoes Vec3
Vec3 vec3_add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 vec3_sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 vec3_scale(const Vec3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }
float vec3_dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float vec3_length(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 vec3_normalize(const Vec3& v) {
    float len = vec3_length(v);
    if (len < 0.0001f) return {0, 0, 0};
    return {v.x / len, v.y / len, v.z / len};
}
Vec3 vec3_cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float clamp01(float v) { return std::fmax(0.0f, std::fmin(1.0f, v)); }
float smoothstep01(float edge0, float edge1, float x) {
    if (edge0 == edge1) return (x < edge0) ? 0.0f : 1.0f;
    float t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

// Dia/noite coerente com a trajetoria do sol (mesma fase usada em render_alien_sky).
// Retorna 0..1 (0 = noite, 1 = pico do dia).
float compute_daylight(float day_phase) {
    float sun_height = std::sin(day_phase * 2.0f * kPi - kPi * 0.5f);
    return std::max(0.0f, sun_height);
}

// 0..1 (1 = noite escura, 0 = dia).
float compute_night_alpha(float day_phase) {
    float daylight = compute_daylight(day_phase);
    // Comeca a "apagar" estrelas apenas quando ja esta relativamente claro.
    return 1.0f - smoothstep01(0.05f, 0.30f, daylight);
}

// Conversao consistente entre coordenadas de mundo e indice de tile.
// Os tiles renderizados sao centrados no indice inteiro (tx, tz), com bounds [tx-0.5, tx+0.5].
int world_to_tile(float v) { return (int)std::floor(v + 0.5f); }
float tile_center(int t) { return (float)t; }
float tile_min(int t) { return (float)t - 0.5f; }
float tile_max(int t) { return (float)t + 0.5f; }
