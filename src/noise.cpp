#include "noise.h"

#include "math_core.h"

#include <algorithm>
#include <cmath>
#include <vector>

static int perm[512];

void init_permutation(unsigned seed) {
    std::vector<int> p(256);
    for (int i = 0; i < 256; ++i) p[i] = i;
    unsigned s = seed;
    for (int i = 255; i > 0; --i) {
        s = 1664525u * s + 1013904223u;
        int j = (int)(s % (unsigned)(i + 1));
        std::swap(p[i], p[j]);
    }
    for (int i = 0; i < 512; ++i) perm[i] = p[i & 255];
}

static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
float lerp(float a, float b, float t) { return a + t * (b - a); }
static float grad(int hash, float x, float y) {
    int h = hash & 7;
    float u = (h < 4) ? x : y;
    float v = (h < 4) ? y : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
}

float perlin(float x, float y) {
    int xi = (int)std::floor(x) & 255;
    int yi = (int)std::floor(y) & 255;
    float xf = x - std::floor(x);
    float yf = y - std::floor(y);
    float u = fade(xf);
    float v = fade(yf);
    int aa = perm[perm[xi] + yi];
    int ab = perm[perm[xi] + yi + 1];
    int ba = perm[perm[xi + 1] + yi];
    int bb = perm[perm[xi + 1] + yi + 1];
    float x1 = lerp(grad(aa, xf, yf), grad(ba, xf - 1, yf), u);
    float x2 = lerp(grad(ab, xf, yf - 1), grad(bb, xf - 1, yf - 1), u);
    return lerp(x1, x2, v) * 0.5f + 0.5f;
}

float fbm(float x, float y, int octaves) {
    float value = 0.0f;
    float amp = 0.55f;
    float freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        value += perlin(x * freq, y * freq) * amp;
        freq *= 2.0f;
        amp *= 0.5f;
    }
    return clamp01(value);
}

// Ridged fBm (gera cristas/cordilheiras mais definidas, bom para montanhas e desfiladeiros).
// Saida: 0..1
float ridged_fbm(float x, float y, int octaves) {
    float value = 0.0f;
    float amp = 0.55f;
    float freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        float n = perlin(x * freq, y * freq);            // 0..1
        n = 1.0f - std::fabs(n * 2.0f - 1.0f);           // 0..1 (cristas)
        value += n * amp;
        freq *= 2.0f;
        amp *= 0.5f;
    }
    return clamp01(value);
}
