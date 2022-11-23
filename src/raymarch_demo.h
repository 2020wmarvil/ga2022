#pragma once

// Raymarched SDF Rendering Demo 
// This demonstrates our engine's added capacity to render raymarched signed distance fields
// in an effort to support constructive solid geometry as a valid method for authoring content

typedef struct raymarch_demo_t raymarch_demo_t;

typedef struct fs_t fs_t;
typedef struct heap_t heap_t;
typedef struct render_t render_t;
typedef struct audio_t audio_t;
typedef struct sound_t sound_t;
typedef struct wm_window_t wm_window_t;

// Create an instance of frogger game.
raymarch_demo_t* raymarch_demo_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render, audio_t* audio, int argc, const char** argv);

// Destroy an instance of frogger game.
void raymarch_demo_destroy(raymarch_demo_t* demo);

// Per-frame update for our frogger game.
void raymarch_demo_update(raymarch_demo_t* demo);
