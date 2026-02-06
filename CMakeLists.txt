cmake_minimum_required(VERSION 3.16)
project(TerraFormer2D LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Win32 + OpenGL (no external deps)
add_executable(terraformer WIN32
  src/main.cpp
)

if(WIN32)
  # Windows (MSVC ou MinGW)
  if(MSVC)
    target_compile_definitions(terraformer PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  target_link_libraries(terraformer PRIVATE opengl32 gdi32 user32 kernel32)
else()
  # Linux/Unix
  target_link_libraries(terraformer PRIVATE GL X11)
endif()

message(STATUS "Configured TerraFormer2D - Win32 Standalone")
