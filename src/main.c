#include "debug.h"
#include "fs.h"
#include "heap.h"
#include "render.h"
//#include "simple_game.h"
#include "frogger_game.h"
#include "timer.h"
#include "wm.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"


int main(int argc, const char* argv[])
{
	debug_set_print_mask(k_print_info | k_print_warning | k_print_error);
	debug_install_exception_handler();

	timer_startup();

	heap_t* heap = heap_create(2 * 1024 * 1024);
	fs_t* fs = fs_create(heap, 8);
	wm_window_t* window = wm_create(heap);
	render_t* render = render_create(heap, window);

	ma_engine engine;
	if (ma_engine_init(NULL, &engine) != MA_SUCCESS) 
	{
		printf("Failed to initialize audio engine.");
		return -1;
	}

	// Play test sound 
	char* sound_filepath = "sounds/ribbit.mp3";
	ma_engine_play_sound(&engine, sound_filepath, NULL);

	//simple_game_t* game = simple_game_create(heap, fs, window, render, argc, argv);
	frogger_game_t* game = frogger_game_create(heap, fs, window, render, argc, argv);

	while (!wm_pump(window))
	{
		//simple_game_update(game);
		frogger_game_update(game);
	}

	/* XXX: Shutdown render before the game. Render uses game resources. */
	render_destroy(render);

	//simple_game_destroy(game);
	frogger_game_destroy(game);

	ma_engine_uninit(&engine);

	wm_destroy(window);
	fs_destroy(fs);
	heap_destroy(heap);

	return 0;
}
