#include "audio.h"
#include "heap.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"

#define MAX_SOUND_OBJECTS 32

typedef struct sound_t
{
	ma_sound sound;
} sound_t;

typedef struct audio_t
{
	ma_engine audio_engine;
	heap_t* heap;

	ma_sound sounds[MAX_SOUND_OBJECTS];
	int num_sounds;
} audio_t;

// Create an audio system.
audio_t* audio_create(heap_t* heap)
{
	audio_t* audio = heap_alloc(heap, sizeof(audio_t), 8);

	ma_engine_init(NULL, &audio->audio_engine);
	audio->heap = heap;
	audio->num_sounds = 0;

	return audio;
}

// Destroy a audio system.
void audio_destroy(audio_t* audio)
{
	ma_engine_uninit(&audio->audio_engine);
	heap_free(audio->heap, audio);
}

// Play a sound file
void audio_play_file(audio_t* audio, char* filepath)
{
	ma_engine_play_sound(&audio->audio_engine, filepath, NULL);
}

// Load a sound file into a sound object
int audio_load_sound_from_file(audio_t* audio, char* filepath)
{
	if (audio->num_sounds >= MAX_SOUND_OBJECTS)
	{
		return -1;
	}

	ma_sound_init_from_file(&audio->audio_engine, filepath, 0, NULL, NULL, &audio->sounds[audio->num_sounds]);
	return audio->num_sounds++;
}

// Start playing a sound object
void audio_start_sound(audio_t* audio, int sound_index)
{
	ma_sound_start(&audio->sounds[sound_index]);
}

// Stop playing a sound object
void audio_stop_sound(audio_t* audio, int sound_index)
{
	ma_sound_stop(&audio->sounds[sound_index]);
}

// Enable/Disable looping on a sound object
void audio_loop_sound(audio_t* audio, int sound_index, int should_loop)
{
	ma_sound_set_looping(&audio->sounds[sound_index], should_loop);
}
