cmake_minimum_required(VERSION 3.16)
project(TerraFormer2D LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# raylib (modernizacao: substitui Win32 + OpenGL 1.x fixed-function - ver plano em
# C:\Users\9173\.claude\plans\quero-refatorar-todo-o-serene-kazoo.md). Migracao completa:
# main() (plain, sem WinMain) + raylib cuida de janela/input/texto/textura/desenho; os poucos
# trechos que precisavam de shading por face/vertice customizado (render_cube_3d,
# render_wall_3d_tex e as funcoes locais equivalentes em main.cpp) foram portados
# mecanicamente para rlgl (API de baixo nivel da raylib) em vez de reescritos contra a API
# de alto nivel.
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
)
FetchContent_MakeAvailable(raylib)

add_executable(terraformer WIN32
  src/main.cpp
  src/math_core.cpp
  src/noise.cpp
  src/blocks.cpp
  src/textures.cpp
  src/config_io.cpp
  src/game_state.cpp
  src/world.cpp
  src/camera.cpp
  src/player_physics.cpp
  src/items_particles.cpp
  src/inventory_crafting.cpp
  src/modules_building.cpp
  src/font.cpp
  src/save_load.cpp
  src/minimap.cpp
  src/render_primitives.cpp
  src/render_player.cpp
  src/lighting.cpp
  src/sky.cpp
  src/ui_hud.cpp
  src/ui_menu.cpp
  src/building_interaction.cpp
  src/ui_build_menu.cpp
  src/input.cpp
  src/win32_platform.cpp
  src/objectives.cpp
  src/creatures.cpp
  src/audio.cpp
  src/terrain_mesh.cpp
  src/base_interior.cpp
  src/interiors.cpp
  src/base_exterior.cpp
  src/module_models.cpp
  src/resource_geology.cpp
)

if(MSVC)
  target_compile_definitions(terraformer PRIVATE _CRT_SECURE_NO_WARNINGS)
  # opengl32/gdi32/user32/kernel32 removed: raylib's own CMake target already provides these
  # as transitive/interface link dependencies (confirmed via an actual build in this session -
  # they appear in the real link command even without an explicit entry here). Keeping our own
  # explicit entries alongside raylib's caused a LNK2005 ShowCursor collision (raylib.h defines
  # its own ShowCursor() with the same name as user32's WinAPI ShowCursor(), and having two
  # separate reference paths to user32.lib in the same link pulled in conflicting object code
  # for it) - dropping the redundant explicit entries resolves it.
  target_link_libraries(terraformer PRIVATE raylib)
  # main() replaces WinMain now; these flags keep the console window from appearing on
  # Windows, matching the WIN32 keyword above on add_executable().
  target_link_options(terraformer PRIVATE "/SUBSYSTEM:WINDOWS" "/ENTRY:mainCRTStartup")
else()
  target_link_libraries(terraformer PRIVATE raylib)
endif()

message(STATUS "Configured TerraFormer2D - raylib")
