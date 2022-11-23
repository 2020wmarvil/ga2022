#pragma once

typedef struct audio_t audio_t;
typedef struct sound_t sound_t;
typedef struct heap_t heap_t;

// Create an audio system.
audio_t* audio_create(heap_t* heap);

// Destroy a audio system.
void audio_destroy(audio_t* audio);

// Play a sound file
void audio_play_file(audio_t* audio, char* filepath);

// Load a sound file into a sound object. Returns the sound index in the audio engine.
int audio_load_sound_from_file(audio_t* audio, char* filepath);

// Start playing a sound object
void audio_start_sound(audio_t* audio, int sound_index);

// Stop playing a sound object
void audio_stop_sound(audio_t* audio, int sound_index);

// Enable/Disable looping on a sound object
void audio_loop_sound(audio_t* audio, int sound_index, int should_loop);
