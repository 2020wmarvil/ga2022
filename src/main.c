#include "audio.h"
#include "debug.h"
#include "fs.h"
#include "heap.h"
#include "raymarch_demo.h"
#include "simple_game.h"
#include "render.h"
#include "timer.h"
#include "wm.h"

#include "cpp_test.h"

int main(int argc, const char* argv[])
{
	debug_set_print_mask(k_print_info | k_print_warning | k_print_error);
	debug_install_exception_handler();

	timer_startup();

	cpp_test_function(42);

	heap_t* heap = heap_create(2 * 1024 * 1024);
	fs_t* fs = fs_create(heap, 8);
	wm_window_t* window = wm_create(heap);
	render_t* render = render_create(heap, window);
	audio_t* audio = audio_create(heap);

	raymarch_demo_t* demo = raymarch_demo_create(heap, fs, window, render, audio, argc, argv);

	while (!wm_pump(window))
	{
		raymarch_demo_update(demo);
	}

	/* XXX: Shutdown render before the game. Render uses game resources. */
	render_destroy(render);
	audio_destroy(audio);

	raymarch_demo_destroy(demo);

	wm_destroy(window);
	fs_destroy(fs);
	heap_destroy(heap);

	return 0;
}
