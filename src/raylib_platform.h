#pragma once

// Substitui platform.h (windows.h + GL/gl.h) para arquivos ja migrados para raylib - ver
// plano de modernizacao em C:\Users\9173\.claude\plans\quero-refatorar-todo-o-serene-kazoo.md.
// Nao inclua este header no mesmo arquivo que platform.h: raylib.h define varios nomes que
// colidem com windows.h (Rectangle, CloseWindow, ShowCursor, DrawText, etc.) - cada arquivo
// usa um ou outro, nunca os dois, ate platform.h ser removido de vez na etapa final.

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>
