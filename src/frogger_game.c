#include "frogger_game.h"

#include "debug.h"
#include "ecs.h"
#include "fs.h"
#include "gpu.h"
#include "heap.h"
#include "net.h"
#include "render.h"
#include "timer_object.h"
#include "transform.h"
#include "wm.h"

#include <stdlib.h>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>

#define NUM_CAR_ENTITIES 60

#define PLAYER_WIDTH 0.5f
#define PLAYER_HEIGHT 0.5f
#define SMALL_CAR_WIDTH 3.0f
#define SMALL_CAR_HEIGHT 1.2f
#define MEDIUM_CAR_WIDTH 3.5f
#define MEDIUM_CAR_HEIGHT 1.4f
#define LARGE_CAR_WIDTH 4.0f
#define LARGE_CAR_HEIGHT 1.6f

#define SPAWN_FREQ 1.0f

typedef struct transform_component_t
{
	transform_t transform;
} transform_component_t;

typedef struct camera_component_t
{
	mat4f_t projection;
	mat4f_t view;
} camera_component_t;

typedef struct model_component_t
{
	gpu_mesh_info_t* mesh_info;
	gpu_shader_info_t* shader_info;
} model_component_t;

typedef struct player_component_t
{
	int index;
	float speed;
	float width;
	float height;
} player_component_t;

typedef struct car_component_t
{
	float speed;
	float width;
	float height;
	int is_enabled;
	float time_to_live;
} car_component_t;

typedef struct name_component_t
{
	char name[32];
} name_component_t;

typedef struct frogger_game_t
{
	heap_t* heap;
	fs_t* fs;
	wm_window_t* window;
	render_t* render;
	net_t* net;
	ma_engine audio_engine;

	timer_object_t* timer;

	ecs_t* ecs;
	int transform_type;
	int camera_type;
	int model_type;
	int player_type;
	int car_type;
	int name_type;
	ecs_entity_ref_t player_ent;
	ecs_entity_ref_t camera_ent;
	ecs_entity_ref_t car_ents[NUM_CAR_ENTITIES];

	float time_until_spawn;

	gpu_mesh_info_t player_mesh;
	gpu_mesh_info_t car_small_mesh;
	gpu_mesh_info_t car_medium_mesh;
	gpu_mesh_info_t car_large_mesh;
	gpu_shader_info_t cube_shader;
	fs_work_t* vertex_shader_work;
	fs_work_t* fragment_shader_work;
} frogger_game_t;

static void load_resources(frogger_game_t* game);
static void unload_resources(frogger_game_t* game);
static void reset_player_position(frogger_game_t* game);
static void enable_car(frogger_game_t* game, ecs_entity_ref_t car_ent);
static void disable_car(frogger_game_t* game, ecs_entity_ref_t car_ent);
static void spawn_player(frogger_game_t* game, int index);
static void spawn_cars(frogger_game_t* game);
static void spawn_camera(frogger_game_t* game);
static void update_players(frogger_game_t* game);
static void update_cars(frogger_game_t* game);
static void draw_models(frogger_game_t* game);

frogger_game_t* frogger_game_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render, int argc, const char** argv)
{
	frogger_game_t* game = heap_alloc(heap, sizeof(frogger_game_t), 8);
	game->heap = heap;
	game->fs = fs;
	game->window = window;
	game->render = render;

	ma_engine_init(NULL, &game->audio_engine);

	game->timer = timer_object_create(heap, NULL);
	
	game->ecs = ecs_create(heap);
	game->transform_type = ecs_register_component_type(game->ecs, "transform", sizeof(transform_component_t), _Alignof(transform_component_t));
	game->camera_type = ecs_register_component_type(game->ecs, "camera", sizeof(camera_component_t), _Alignof(camera_component_t));
	game->model_type = ecs_register_component_type(game->ecs, "model", sizeof(model_component_t), _Alignof(model_component_t));
	game->player_type = ecs_register_component_type(game->ecs, "player", sizeof(player_component_t), _Alignof(player_component_t));
	game->car_type = ecs_register_component_type(game->ecs, "car", sizeof(car_component_t), _Alignof(car_component_t));
	game->name_type = ecs_register_component_type(game->ecs, "name", sizeof(name_component_t), _Alignof(name_component_t));

	game->net = net_create(heap, game->ecs);
	if (argc >= 2)
	{
		net_address_t server;
		if (net_string_to_address(argv[1], &server))
		{
			net_connect(game->net, &server);
		}
		else
		{
			debug_print(k_print_error, "Unable to resolve server address: %s\n", argv[1]);
		}
	}

	load_resources(game);
	spawn_player(game, 0);
	spawn_cars(game);
	spawn_camera(game);

	return game;
}

void frogger_game_destroy(frogger_game_t* game)
{
	ma_engine_uninit(&game->audio_engine);
	net_destroy(game->net);
	ecs_destroy(game->ecs);
	timer_object_destroy(game->timer);
	unload_resources(game);
	heap_free(game->heap, game);
}

void frogger_game_update(frogger_game_t* game)
{
	timer_object_update(game->timer);
	ecs_update(game->ecs);
	net_update(game->net);
	update_players(game);
	update_cars(game);
	draw_models(game);
	render_push_done(game->render);
}

static void load_resources(frogger_game_t* game)
{
	game->vertex_shader_work = fs_read(game->fs, "shaders/triangle.vert.spv", game->heap, false, false);
	game->fragment_shader_work = fs_read(game->fs, "shaders/triangle.frag.spv", game->heap, false, false);
	game->cube_shader = (gpu_shader_info_t)
	{
		.vertex_shader_data = fs_work_get_buffer(game->vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(game->vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(game->fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(game->fragment_shader_work),
		.uniform_buffer_count = 1,
	};

	static vec3f_t player_verts[] =
	{
		{ -1.0f, -PLAYER_WIDTH / 2.0f,  PLAYER_HEIGHT / 2.0f }, { 0.812f, 1.0f, 0.702f },
		{  1.0f, -PLAYER_WIDTH / 2.0f,  PLAYER_HEIGHT / 2.0f }, { 0.812f, 1.0f, 0.702f },
		{  1.0f,  PLAYER_WIDTH / 2.0f,  PLAYER_HEIGHT / 2.0f }, { 0.812f, 1.0f, 0.702f },
		{ -1.0f,  PLAYER_WIDTH / 2.0f,  PLAYER_HEIGHT / 2.0f }, { 0.812f, 1.0f, 0.702f },
		{ -1.0f, -PLAYER_WIDTH / 2.0f, -PLAYER_HEIGHT / 2.0f }, { 0.812f, 1.0f, 0.702f },
		{  1.0f, -PLAYER_WIDTH / 2.0f, -PLAYER_HEIGHT / 2.0f }, { 0.812f, 1.0f, 0.702f },
		{  1.0f,  PLAYER_WIDTH / 2.0f, -PLAYER_HEIGHT / 2.0f }, { 0.812f, 1.0f, 0.702f },
		{ -1.0f,  PLAYER_WIDTH / 2.0f, -PLAYER_HEIGHT / 2.0f }, { 0.812f, 1.0f, 0.702f },
	};
	static vec3f_t car_small_verts[] =
	{
		{ -0.0f, -SMALL_CAR_WIDTH / 2.0f,  SMALL_CAR_HEIGHT / 2.0f }, { 0.639f, 0.0f, 0.0824f },
		{  0.0f, -SMALL_CAR_WIDTH / 2.0f,  SMALL_CAR_HEIGHT / 2.0f }, { 0.639f, 0.0f, 0.0824f },
		{  0.0f,  SMALL_CAR_WIDTH / 2.0f,  SMALL_CAR_HEIGHT / 2.0f }, { 0.639f, 0.0f, 0.0824f },
		{ -0.0f,  SMALL_CAR_WIDTH / 2.0f,  SMALL_CAR_HEIGHT / 2.0f }, { 0.639f, 0.0f, 0.0824f },
		{ -0.0f, -SMALL_CAR_WIDTH / 2.0f, -SMALL_CAR_HEIGHT / 2.0f }, { 0.639f, 0.0f, 0.0824f },
		{  0.0f, -SMALL_CAR_WIDTH / 2.0f, -SMALL_CAR_HEIGHT / 2.0f }, { 0.639f, 0.0f, 0.0824f },
		{  0.0f,  SMALL_CAR_WIDTH / 2.0f, -SMALL_CAR_HEIGHT / 2.0f }, { 0.639f, 0.0f, 0.0824f },
		{ -0.0f,  SMALL_CAR_WIDTH / 2.0f, -SMALL_CAR_HEIGHT / 2.0f }, { 0.639f, 0.0f, 0.0824f },
	};
	static vec3f_t car_medium_verts[] =
	{
		{ -1.0f, -MEDIUM_CAR_WIDTH / 2.0f,  MEDIUM_CAR_HEIGHT / 2.0f }, { 0.937f, 0.463f, 0.478f },
		{  1.0f, -MEDIUM_CAR_WIDTH / 2.0f,  MEDIUM_CAR_HEIGHT / 2.0f }, { 0.937f, 0.463f, 0.478f },
		{  1.0f,  MEDIUM_CAR_WIDTH / 2.0f,  MEDIUM_CAR_HEIGHT / 2.0f }, { 0.937f, 0.463f, 0.478f },
		{ -1.0f,  MEDIUM_CAR_WIDTH / 2.0f,  MEDIUM_CAR_HEIGHT / 2.0f }, { 0.937f, 0.463f, 0.478f },
		{ -1.0f, -MEDIUM_CAR_WIDTH / 2.0f, -MEDIUM_CAR_HEIGHT / 2.0f }, { 0.937f, 0.463f, 0.478f },
		{  1.0f, -MEDIUM_CAR_WIDTH / 2.0f, -MEDIUM_CAR_HEIGHT / 2.0f }, { 0.937f, 0.463f, 0.478f },
		{  1.0f,  MEDIUM_CAR_WIDTH / 2.0f, -MEDIUM_CAR_HEIGHT / 2.0f }, { 0.937f, 0.463f, 0.478f },
		{ -1.0f,  MEDIUM_CAR_WIDTH / 2.0f, -MEDIUM_CAR_HEIGHT / 2.0f }, { 0.937f, 0.463f, 0.478f },
	};
	static vec3f_t car_large_verts[] =
	{
		{ -1.0f, -LARGE_CAR_WIDTH / 2.0f,  LARGE_CAR_HEIGHT / 2.0f }, { 0.725f, 0.6f, 0.373f },
		{  1.0f, -LARGE_CAR_WIDTH / 2.0f,  LARGE_CAR_HEIGHT / 2.0f }, { 0.725f, 0.6f, 0.373f },
		{  1.0f,  LARGE_CAR_WIDTH / 2.0f,  LARGE_CAR_HEIGHT / 2.0f }, { 0.725f, 0.6f, 0.373f },
		{ -1.0f,  LARGE_CAR_WIDTH / 2.0f,  LARGE_CAR_HEIGHT / 2.0f }, { 0.725f, 0.6f, 0.373f },
		{ -1.0f, -LARGE_CAR_WIDTH / 2.0f, -LARGE_CAR_HEIGHT / 2.0f }, { 0.725f, 0.6f, 0.373f },
		{  1.0f, -LARGE_CAR_WIDTH / 2.0f, -LARGE_CAR_HEIGHT / 2.0f }, { 0.725f, 0.6f, 0.373f },
		{  1.0f,  LARGE_CAR_WIDTH / 2.0f, -LARGE_CAR_HEIGHT / 2.0f }, { 0.725f, 0.6f, 0.373f },
		{ -1.0f,  LARGE_CAR_WIDTH / 2.0f, -LARGE_CAR_HEIGHT / 2.0f }, { 0.725f, 0.6f, 0.373f },
	};
	static uint16_t cube_indices[] =
	{
		0, 1, 2,
		2, 3, 0,
		1, 5, 6,
		6, 2, 1,
		7, 6, 5,
		5, 4, 7,
		4, 0, 3,
		3, 7, 4,
		4, 5, 1,
		1, 0, 4,
		3, 2, 6,
		6, 7, 3
	};

	game->player_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = player_verts,
		.vertex_data_size = sizeof(player_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
	game->car_small_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = car_small_verts,
		.vertex_data_size = sizeof(car_small_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
	game->car_medium_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = car_medium_verts,
		.vertex_data_size = sizeof(car_medium_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
	game->car_large_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = car_large_verts,
		.vertex_data_size = sizeof(car_large_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
}

static void unload_resources(frogger_game_t* game)
{
	heap_free(game->heap, fs_work_get_buffer(game->vertex_shader_work));
	heap_free(game->heap, fs_work_get_buffer(game->fragment_shader_work));
	fs_work_destroy(game->fragment_shader_work);
	fs_work_destroy(game->vertex_shader_work);
}

static void player_net_configure(ecs_t* ecs, ecs_entity_ref_t entity, int type, void* user)
{
	frogger_game_t* game = user;

	model_component_t* model_comp = ecs_entity_get_component(ecs, entity, game->model_type, true);
	model_comp->mesh_info = &game->player_mesh;
	model_comp->shader_info = &game->cube_shader;
}

static void reset_player_position(frogger_game_t* game)
{
	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);

	transform_t move;
	transform_identity(&move);
	move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), 4));
	transform_multiply(&transform_comp->transform, &move);

	// play frog sound
	ma_engine_play_sound(&game->audio_engine, "sounds/ribbit.mp3", NULL);
}

static void enable_car(frogger_game_t* game, ecs_entity_ref_t car_ent)
{
	car_component_t* car_comp = ecs_entity_get_component(game->ecs, car_ent, game->car_type, true);
	car_comp->is_enabled = 1;
	car_comp->time_to_live = 8.0f;

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, car_ent, game->transform_type, true);

	int lane = rand() % 3;
	if (lane == 0)
	{
		transform_comp->transform.translation.y = 12;
		transform_comp->transform.translation.z = -3;
		car_comp->speed = -4;
		car_comp->time_to_live = 8.0f;
	}
	else if (lane == 1)
	{
		transform_comp->transform.translation.y = -12;
		transform_comp->transform.translation.z = -0.5;
		car_comp->speed = 3.2f;
		car_comp->time_to_live = 10.0f;
	}
	else if (lane == 2)
	{
		transform_comp->transform.translation.y = 12;
		transform_comp->transform.translation.z = 2;
		car_comp->speed = -2;
		car_comp->time_to_live = 12.0f;
	}
}

static void disable_car(frogger_game_t* game, ecs_entity_ref_t car_ent)
{
	car_component_t* car_comp = ecs_entity_get_component(game->ecs, car_ent, game->car_type, true);
	car_comp->is_enabled = 0;

	// move the disabled car off-screen
	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, car_ent, game->transform_type, true);
	transform_comp->transform.translation.y = 100;
}

static void spawn_player(frogger_game_t* game, int index)
{
	uint64_t k_player_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->player_type) |
		(1ULL << game->name_type);
	game->player_ent = ecs_entity_add(game->ecs, k_player_ent_mask);

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "player");

	player_component_t* player_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->player_type, true);
	player_comp->index = index;
	player_comp->speed = 4;
	player_comp->width = 0.5f;
	player_comp->height = 0.5f;

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->model_type, true);
	model_comp->mesh_info = &game->player_mesh;
	model_comp->shader_info = &game->cube_shader;

	uint64_t k_player_ent_net_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->name_type);
	uint64_t k_player_ent_rep_mask =
		(1ULL << game->transform_type);
	net_state_register_entity_type(game->net, 0, k_player_ent_net_mask, k_player_ent_rep_mask, player_net_configure, game);

	net_state_register_entity_instance(game->net, 0, game->player_ent);

	reset_player_position(game);
}

static void spawn_cars(frogger_game_t* game)
{
	game->time_until_spawn = SPAWN_FREQ;

	uint64_t k_car_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->car_type);

	for (int i = 0; i < NUM_CAR_ENTITIES; ++i)
	{
		game->car_ents[i] = ecs_entity_add(game->ecs, k_car_ent_mask);

		transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->car_ents[i], game->transform_type, true);
		transform_identity(&transform_comp->transform);

		car_component_t* car_comp = ecs_entity_get_component(game->ecs, game->car_ents[i], game->car_type, true);
		car_comp->is_enabled = 0;

		model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->car_ents[i], game->model_type, true);
		model_comp->shader_info = &game->cube_shader;

		int car_type = rand() % 3;

		if (car_type == 0)
		{
			model_comp->mesh_info = &game->car_small_mesh;
			car_comp->width = SMALL_CAR_WIDTH;
			car_comp->height = SMALL_CAR_HEIGHT;
		}
		else if (car_type == 1)
		{
			model_comp->mesh_info = &game->car_medium_mesh;
			car_comp->width = MEDIUM_CAR_WIDTH;
			car_comp->height = MEDIUM_CAR_HEIGHT;
		}
		else if (car_type == 2)
		{
			model_comp->mesh_info = &game->car_large_mesh;
			car_comp->width = LARGE_CAR_WIDTH;
			car_comp->height = LARGE_CAR_HEIGHT;
		}

		disable_car(game, game->car_ents[i]);
	}
}

static void spawn_camera(frogger_game_t* game)
{
	uint64_t k_camera_ent_mask =
		(1ULL << game->camera_type) |
		(1ULL << game->name_type);
	game->camera_ent = ecs_entity_add(game->ecs, k_camera_ent_mask);

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "camera");

	camera_component_t* camera_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->camera_type, true);
	mat4f_make_orthographic(&camera_comp->projection, -10.0f, 10.0f, -5.0f, 5.0f, -10.0f, 10.0f);
	//mat4f_make_perspective(&camera_comp->projection, (float)M_PI / 2.0f, 16.0f / 9.0f, 0.1f, 100.0f);

	vec3f_t eye_pos = vec3f_scale(vec3f_forward(), -5.0f);
	vec3f_t forward = vec3f_forward();
	vec3f_t up = vec3f_up();
	mat4f_make_lookat(&camera_comp->view, &eye_pos, &forward, &up);
}

static void update_players(frogger_game_t* game)
{
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.001f;

	uint32_t key_mask = wm_get_key_mask(game->window);
	uint64_t k_query_mask = (1ULL << game->transform_type) | (1ULL << game->player_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		player_component_t* player_comp = ecs_query_get_component(game->ecs, &query, game->player_type);

		if (player_comp->index && transform_comp->transform.translation.z > 1.0f)
		{
			ecs_entity_remove(game->ecs, ecs_query_get_entity(game->ecs, &query), false);
		}

		transform_t move;
		transform_identity(&move);
		if (key_mask & k_key_up)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), -dt * player_comp->speed));
		}
		if (key_mask & k_key_down && transform_comp->transform.translation.z < 4.75)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), dt * player_comp->speed));
		}
		if (key_mask & k_key_left && transform_comp->transform.translation.y > -9.75)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), -dt * player_comp->speed));
		}
		if (key_mask & k_key_right && transform_comp->transform.translation.y < 9.75)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), dt * player_comp->speed));
		}
		transform_multiply(&transform_comp->transform, &move);

		if (transform_comp->transform.translation.z < -4.75)
		{
			reset_player_position(game);
		}

		// detect player collision with cars
		uint64_t k_car_query_mask = (1ULL << game->transform_type) | (1ULL << game->car_type);
		for (ecs_query_t query = ecs_query_create(game->ecs, k_car_query_mask);
			ecs_query_is_valid(game->ecs, &query);
			ecs_query_next(game->ecs, &query))
		{
			car_component_t* car_comp = ecs_query_get_component(game->ecs, &query, game->car_type);

			if (!car_comp->is_enabled)
			{
				continue;
			}

			transform_component_t* car_transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);

			float player_top_bound = transform_comp->transform.translation.z - player_comp->height / 2.0f;
			float player_bottom_bound = transform_comp->transform.translation.z + player_comp->height / 2.0f;
			float player_left_bound = transform_comp->transform.translation.y - player_comp->width / 2.0f;
			float player_right_bound = transform_comp->transform.translation.y + player_comp->width / 2.0f;

			float car_top_bound = car_transform_comp->transform.translation.z - car_comp->height / 2.0f;
			float car_bottom_bound = car_transform_comp->transform.translation.z + car_comp->height / 2.0f;
			float car_left_bound = car_transform_comp->transform.translation.y - car_comp->width / 2.0f;
			float car_right_bound = car_transform_comp->transform.translation.y + car_comp->width / 2.0f;

			if (player_left_bound < car_right_bound
				&& player_right_bound > car_left_bound
				&& player_top_bound < car_bottom_bound
				&& player_bottom_bound > car_top_bound)
			{
				reset_player_position(game);
			}
		}
	}
}

static void update_cars(frogger_game_t* game)
{
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.001f;

	game->time_until_spawn -= dt;
	if (game->time_until_spawn < 0)
	{
		game->time_until_spawn = SPAWN_FREQ; // reset spawn countdown

		// enable the first available car entity in the pool
		for (int i = 0; i < NUM_CAR_ENTITIES; ++i)
		{
			car_component_t* car_comp = ecs_entity_get_component(game->ecs, game->car_ents[i], game->car_type, true);
			if (car_comp && !car_comp->is_enabled)
			{
				enable_car(game, game->car_ents[i]);
				break;
			}
		}
	}

	uint64_t k_query_mask = (1ULL << game->transform_type) | (1ULL << game->car_type);
	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		car_component_t* car_comp = ecs_query_get_component(game->ecs, &query, game->car_type);

		if (!car_comp->is_enabled)
		{
			continue;
		}

		// disable cars when they have lived sufficiently long
		car_comp->time_to_live -= dt;
		if (car_comp->time_to_live < 0)
		{
			disable_car(game, ecs_query_get_entity(game->ecs, &query));
			continue;
		}

		// slide car in the appropriate direction
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		transform_t move;
		transform_identity(&move);
		move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), dt * car_comp->speed));
		transform_multiply(&transform_comp->transform, &move);
	}
}

static void draw_models(frogger_game_t* game)
{
	uint64_t k_camera_query_mask = (1ULL << game->camera_type);
	for (ecs_query_t camera_query = ecs_query_create(game->ecs, k_camera_query_mask);
		ecs_query_is_valid(game->ecs, &camera_query);
		ecs_query_next(game->ecs, &camera_query))
	{
		camera_component_t* camera_comp = ecs_query_get_component(game->ecs, &camera_query, game->camera_type);

		uint64_t k_model_query_mask = (1ULL << game->transform_type) | (1ULL << game->model_type);
		for (ecs_query_t query = ecs_query_create(game->ecs, k_model_query_mask);
			ecs_query_is_valid(game->ecs, &query);
			ecs_query_next(game->ecs, &query))
		{
			transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
			model_component_t* model_comp = ecs_query_get_component(game->ecs, &query, game->model_type);
			ecs_entity_ref_t entity_ref = ecs_query_get_entity(game->ecs, &query);

			struct
			{
				mat4f_t projection;
				mat4f_t model;
				mat4f_t view;
			} uniform_data;
			uniform_data.projection = camera_comp->projection;
			uniform_data.view = camera_comp->view;
			transform_to_matrix(&transform_comp->transform, &uniform_data.model);
			gpu_uniform_buffer_info_t uniform_info = { .data = &uniform_data, sizeof(uniform_data) };

			render_push_model(game->render, &entity_ref, model_comp->mesh_info, model_comp->shader_info, &uniform_info);
		}
	}
}
