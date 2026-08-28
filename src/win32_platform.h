#pragma once

// ============= Platform Layer (raylib main loop) =============
// Migrated from Win32 (WinMain/WindowProc/WGL context setup) to raylib's InitWindow()/
// WindowShouldClose()/CloseWindow(). This header intentionally declares nothing: main() is
// the OS-designated process entry point now (no WinMain, no window-procedure callback to wire
// up), and everything win32_platform.cpp needs from other modules it gets via its own local
// extern/forward declarations, same pattern as before this migration.
