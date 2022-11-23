#include "raymarch_demo.h"

#include "audio.h"
#include "debug.h"
#include "ecs.h"
#include "fs.h"
#include "gpu.h"
#include "heap.h"
#include "math.h"
#include "render.h"
#include "timer_object.h"
#include "transform.h"
#include "wm.h"

#define CAMERA_SPEED 4.0f
#define MOUSE_SENSITIVITY 0.1f

typedef struct transform_component_t
{
	transform_t transform;
} transform_component_t;

typedef struct camera_component_t
{
	mat4f_t projection;
	mat4f_t view;

	// used to compose the view matrix
	vec3f_t eye_pos;
	vec3f_t forward;
	vec3f_t up;
	float yaw;
	float pitch;
} camera_component_t;

typedef struct model_component_t
{
	gpu_mesh_info_t* mesh_info;
	gpu_shader_info_t* shader_info;
} model_component_t;

typedef struct raymarch_demo_t
{
	heap_t* heap;
	fs_t* fs;
	wm_window_t* window;
	render_t* render;
	audio_t* audio;

	timer_object_t* timer;

	ecs_t* ecs;
	int transform_type;
	int camera_type;
	int model_type;

	ecs_entity_ref_t camera_ent;
	ecs_entity_ref_t screen_quad_ent;

	gpu_mesh_info_t quad_mesh;
	gpu_shader_info_t raymarch_shader;
	fs_work_t* vertex_shader_work;
	fs_work_t* fragment_shader_work;

	int sound_index_background;
} raymarch_demo_t;

static void load_resources(raymarch_demo_t* demo);
static void unload_resources(raymarch_demo_t* demo);
static void spawn_camera(raymarch_demo_t* demo);
static void spawn_screen_quad(raymarch_demo_t* demo);
static void update_camera(raymarch_demo_t* demo);
static void draw_models(raymarch_demo_t* demo);

raymarch_demo_t* raymarch_demo_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render, audio_t* audio, int argc, const char** argv)
{
	raymarch_demo_t* demo = heap_alloc(heap, sizeof(raymarch_demo_t), 8);
	demo->heap = heap;
	demo->fs = fs;
	demo->window = window;
	demo->render = render;
	demo->audio = audio;

	demo->timer = timer_object_create(heap, NULL);
	
	demo->ecs = ecs_create(heap);
	demo->transform_type = ecs_register_component_type(demo->ecs, "transform", sizeof(transform_component_t), _Alignof(transform_component_t));
	demo->camera_type = ecs_register_component_type(demo->ecs, "camera", sizeof(camera_component_t), _Alignof(camera_component_t));
	demo->model_type = ecs_register_component_type(demo->ecs, "model", sizeof(model_component_t), _Alignof(model_component_t));

	load_resources(demo);
	spawn_screen_quad(demo);
	spawn_camera(demo);

	demo->sound_index_background = audio_load_sound_from_file(demo->audio, "sounds/background.mp3");
	audio_loop_sound(demo->audio, demo->sound_index_background, 1);
	audio_start_sound(demo->audio, demo->sound_index_background);

	return demo;
}

void raymarch_demo_destroy(raymarch_demo_t* demo)
{
	audio_stop_sound(demo->audio, demo->sound_index_background);
	ecs_destroy(demo->ecs);
	timer_object_destroy(demo->timer);
	unload_resources(demo);
	heap_free(demo->heap, demo);
}

void raymarch_demo_update(raymarch_demo_t* demo)
{
	timer_object_update(demo->timer);
	ecs_update(demo->ecs);
	update_camera(demo);
	draw_models(demo);
	render_push_done(demo->render);
}

static void load_resources(raymarch_demo_t* demo)
{
	demo->vertex_shader_work = fs_read(demo->fs, "shaders/raymarch.vert.spv", demo->heap, false, false);
	demo->fragment_shader_work = fs_read(demo->fs, "shaders/raymarch.frag.spv", demo->heap, false, false);
	demo->raymarch_shader = (gpu_shader_info_t)
	{
		.vertex_shader_data = fs_work_get_buffer(demo->vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(demo->vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(demo->fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(demo->fragment_shader_work),
		.uniform_buffer_count = 1,
	};

	static vec3f_t quad_verts[] =
	{
		{  0.0f, -1.0f,  1.0f },
		{  0.0f,  1.0f,  1.0f },
		{  0.0f, -1.0f, -1.0f },
		{  0.0f,  1.0f, -1.0f },
	};

	static uint16_t quad_indices[] =
	{
		0, 2, 3,
		3, 1, 0,
	};

	demo->quad_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_i2,
		.vertex_data = quad_verts,
		.vertex_data_size = sizeof(quad_verts),
		.index_data = quad_indices,
		.index_data_size = sizeof(quad_indices),
	};
}

static void unload_resources(raymarch_demo_t* demo)
{
	heap_free(demo->heap, fs_work_get_buffer(demo->vertex_shader_work));
	heap_free(demo->heap, fs_work_get_buffer(demo->fragment_shader_work));
	fs_work_destroy(demo->fragment_shader_work);
	fs_work_destroy(demo->vertex_shader_work);
}

static void spawn_camera(raymarch_demo_t* demo)
{
	uint64_t k_camera_ent_mask = (1ULL << demo->camera_type);
	demo->camera_ent = ecs_entity_add(demo->ecs, k_camera_ent_mask);

	camera_component_t* camera_comp = ecs_entity_get_component(demo->ecs, demo->camera_ent, demo->camera_type, true);
	mat4f_make_perspective(&camera_comp->projection, (float)M_PI / 2.0f, 16.0f / 9.0f, 0.1f, 100.0f);

	camera_comp->eye_pos = vec3f_scale(vec3f_forward(), -5.0f);
	camera_comp->forward = vec3f_forward();
	camera_comp->up = vec3f_up();
	mat4f_make_lookat(&camera_comp->view, &camera_comp->eye_pos, &camera_comp->forward, &camera_comp->up);

	camera_comp->yaw = 0.0f;
	camera_comp->pitch = 180.0f;
}

static void spawn_screen_quad(raymarch_demo_t* demo)
{
	uint64_t k_screen_quad_ent_mask = (1ULL << demo->transform_type) | (1ULL << demo->model_type);
	demo->screen_quad_ent = ecs_entity_add(demo->ecs, k_screen_quad_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(demo->ecs, demo->screen_quad_ent, demo->transform_type, true);
	transform_identity(&transform_comp->transform);

	model_component_t* model_comp = ecs_entity_get_component(demo->ecs, demo->screen_quad_ent, demo->model_type, true);
	model_comp->mesh_info = &demo->quad_mesh;
	model_comp->shader_info = &demo->raymarch_shader;
}

static void update_camera(raymarch_demo_t* demo)
{
	float dt = (float)timer_object_get_delta_ms(demo->timer) * 0.001f;

	uint32_t key_mask = wm_get_key_mask(demo->window);

	int mouse_x;
	int mouse_y;
	wm_get_mouse_move(demo->window, &mouse_x, &mouse_y);

	camera_component_t* camera_comp = ecs_entity_get_component(demo->ecs, demo->camera_ent, demo->camera_type, true);
	
	// Adapted from the learnopengl.com camera script
	float xoffset = mouse_x * MOUSE_SENSITIVITY;
	float yoffset = mouse_y * MOUSE_SENSITIVITY;

    camera_comp->pitch -= xoffset;
    camera_comp->yaw -= yoffset;

    // make sure that when pitch is out of bounds, screen doesn't get flipped
	if (camera_comp->yaw > 89.0f)
	{
		camera_comp->yaw = 89.0f;
	}
	else if (camera_comp->yaw < -89.0f)
	{
		camera_comp->yaw = -89.0f;
	}

    vec3f_t forward;
    forward.x = (float)(cos(degrees_to_radians(camera_comp->yaw)) * cos(degrees_to_radians(camera_comp->pitch)));
    forward.y = (float)(sin(degrees_to_radians(camera_comp->pitch)));
    forward.z = (float)(sin(degrees_to_radians(camera_comp->yaw)) * cos(degrees_to_radians(camera_comp->pitch)));
    camera_comp->forward = vec3f_norm(forward);

	mat4f_make_lookat(&camera_comp->view, &camera_comp->eye_pos, &camera_comp->forward, &camera_comp->up);

	vec3f_t right = vec3f_norm(vec3f_cross(camera_comp->forward, camera_comp->up));
	vec3f_t up = vec3f_norm(vec3f_cross(camera_comp->forward, right));

	if (key_mask & (k_key_up | k_key_w))
	{
		camera_comp->eye_pos = vec3f_add(camera_comp->eye_pos, vec3f_scale(camera_comp->forward, dt * CAMERA_SPEED));
	}
	if (key_mask & (k_key_down | k_key_s))
	{
		camera_comp->eye_pos = vec3f_add(camera_comp->eye_pos, vec3f_scale(camera_comp->forward, dt * -CAMERA_SPEED));
	}
	if (key_mask & (k_key_left | k_key_a))
	{
		camera_comp->eye_pos = vec3f_add(camera_comp->eye_pos, vec3f_scale(right, dt * -CAMERA_SPEED));
	}
	if (key_mask & (k_key_right | k_key_d))
	{
		camera_comp->eye_pos = vec3f_add(camera_comp->eye_pos, vec3f_scale(right, dt * CAMERA_SPEED));
	}
	if (key_mask & k_key_q)
	{
		camera_comp->eye_pos = vec3f_add(camera_comp->eye_pos, vec3f_scale(up, dt * -CAMERA_SPEED));
	}
	if (key_mask & k_key_e)
	{
		camera_comp->eye_pos = vec3f_add(camera_comp->eye_pos, vec3f_scale(up, dt * CAMERA_SPEED));
	}
}

static void draw_models(raymarch_demo_t* demo)
{
	camera_component_t* camera_comp = ecs_entity_get_component(demo->ecs, demo->camera_ent, demo->camera_type, true);
	model_component_t* model_comp = ecs_entity_get_component(demo->ecs, demo->screen_quad_ent, demo->model_type, true);
	transform_component_t* model_trans_comp = ecs_entity_get_component(demo->ecs, demo->screen_quad_ent, demo->transform_type, true);

	struct
	{
		mat4f_t projection;
		mat4f_t model;
		mat4f_t view;
	} uniform_data;
	uniform_data.projection = camera_comp->projection;
	uniform_data.view = camera_comp->view;
	transform_to_matrix(&model_trans_comp->transform, &uniform_data.model);
	gpu_uniform_buffer_info_t uniform_info = { .data = &uniform_data, sizeof(uniform_data) };
	
	render_push_model(demo->render, &demo->screen_quad_ent, model_comp->mesh_info, model_comp->shader_info, &uniform_info);
}
