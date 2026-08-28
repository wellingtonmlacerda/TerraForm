#pragma once

// ============= Perlin Noise =============
// Extracted verbatim from main.cpp (original lines ~127-194).
// NOTE: lerp() is a generic linear-interpolation helper that historically lived in this
// section of main.cpp and is called from many unrelated places across the codebase
// (camera, world gen, sky, physics), so it keeps external linkage here rather than
// becoming file-local, even though its name doesn't imply "noise".

void init_permutation(unsigned seed = 1337);

float lerp(float a, float b, float t);
float perlin(float x, float y);
float fbm(float x, float y, int octaves = 5);

// Ridged fBm (gera cristas/cordilheiras mais definidas, bom para montanhas e desfiladeiros).
// Saida: 0..1
float ridged_fbm(float x, float y, int octaves = 4);
