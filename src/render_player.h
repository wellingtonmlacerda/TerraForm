#pragma once

struct Player;

// ============= Player Rendering (top-down astronaut) =============
// Extracted verbatim from main.cpp (original lines ~758-919, interleaved between
// render_rounded_rect() and render_cube_outline_3d() in the render_primitives cluster -
// same stage, split into its own file since this pair needs Player from player_physics.h
// and draws itself out of render_primitives' render_circle()/render_ellipse()/render_quad()).
//
// Note (verified via grep across all of src/ before this extraction): neither function is
// currently called from anywhere else in the codebase - the game renders the player inline
// in render_world() via render_cube_3d()/render_plane_3d(), not through this top-down path.
// They lose "static" anyway (matching the module-boundary convention used throughout this
// refactor) so they remain available with external linkage for whoever ends up calling them
// (this looked like vestigial/alternate-view code already, not something this stage should
// judge or remove - purely mechanical move).
void render_player_topdown(float px, float py, float scale, const Player& player);
void render_astronaut(float px, float py, float scale, const Player& player, bool /*in_water*/);
