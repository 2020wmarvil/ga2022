#include "trace.h"

#include "debug.h"
#include "fs.h"
#include "heap.h"
#include "math.h"
#include "mutex.h"
#include "thread.h"
#include "timer.h"
#include "timer_object.h"

#include <stddef.h>
#include <string.h>
#include <stdio.h>

#define MAX_TRACE_FILEPATH_LEN 64
#define MAX_TRACE_EVENT_NAME_LEN 64

typedef enum trace_event_type_t
{
	k_trace_event_type_pop_duration,
	k_trace_event_type_push_duration,
} trace_event_type_t;

typedef struct trace_event_t
{
	char name[MAX_TRACE_EVENT_NAME_LEN];
	trace_event_type_t event_type;
	uint64_t ticks_since_creation;
	int thread_id;
} trace_event_t;

typedef struct trace_t
{
	// event queue
	trace_event_t* events;
	size_t num_events;
	size_t event_capacity;
	mutex_t* mutex;

	// file io
	fs_t* fs;
	char file_path[MAX_TRACE_FILEPATH_LEN];

	// internal
	char* out_buffer;
	heap_t* heap;
} trace_t;

static void trace_flush_events(trace_t* trace);

trace_t* trace_create(heap_t* heap, int event_capacity)
{
	trace_t* trace = heap_alloc(heap, sizeof(trace_t), 8);
	trace->events = heap_alloc(heap, sizeof(trace_event_t) * event_capacity, 8);
	trace->num_events = 0;
	trace->event_capacity = event_capacity;
	trace->mutex = mutex_create();

	trace->heap = heap;
	trace->out_buffer = heap_alloc(heap, (long long)event_capacity * MAX_TRACE_EVENT_NAME_LEN, 8);

	return trace;
}

void trace_destroy(trace_t* trace)
{
	mutex_destroy(trace->mutex);
	heap_free(trace->heap, trace->out_buffer);
	heap_free(trace->heap, trace->events);
	heap_free(trace->heap, trace);
}

void trace_duration_push(trace_t* trace, const char* name)
{
	if (!trace)
	{
		debug_print(k_print_error, "Trace duration push \"%s\" failed, trace has not been created.\n", name);
		return;
	}

	if (!trace->fs)
	{
		debug_print(k_print_error, "Trace duration push \"%s\" failed, trace has not been started.\n", name);
		return;
	}

	if (trace->num_events >= trace->event_capacity)
	{
		trace_flush_events(trace);
	}

	mutex_lock(trace->mutex);
	trace->events[trace->num_events].event_type = k_trace_event_type_push_duration;
	memcpy(trace->events[trace->num_events].name, name, strlen(name));
	trace->events[trace->num_events].ticks_since_creation = timer_get_ticks();
	trace->events[trace->num_events].thread_id = get_current_thread_id();
	trace->num_events++;
	mutex_unlock(trace->mutex);
}

void trace_duration_pop(trace_t* trace)
{
	if (!trace)
	{
		debug_print(k_print_error, "Trace duration pop failed, trace has not been created.\n");
		return;
	}

	if (!trace->fs)
	{
		debug_print(k_print_error, "Trace duration pop failed, trace has not been started.\n");
		return;
	}

	if (trace->num_events >= trace->event_capacity)
	{
		trace_flush_events(trace);
	}

	mutex_lock(trace->mutex);
	trace->events[trace->num_events].event_type = k_trace_event_type_pop_duration;
	// don't need to set the event name, it is implicit given that the event list is a stack
	trace->events[trace->num_events].ticks_since_creation = timer_get_ticks();
	trace->events[trace->num_events].thread_id = get_current_thread_id();
	trace->num_events++;
	mutex_unlock(trace->mutex);
}

void trace_capture_start(trace_t* trace, const char* path)
{
	// create file system
	trace->fs = fs_create(trace->heap, /*queue_capacity=*/10);

	// create file
	memcpy(trace->file_path, path, strlen(path));
	fs_work_t* work = fs_write(trace->fs, trace->file_path, NULL, 0, false, false);
	fs_work_get_result(work);
	fs_work_destroy(work);
}

void trace_capture_stop(trace_t* trace)
{
	trace_flush_events(trace);
	fs_destroy(trace->fs);
}

static void trace_flush_events(trace_t* trace)
{
	if (!trace || !trace->fs)
	{
		debug_print(k_print_warning, "Flush failed\n");
		return;
	}

	mutex_lock(trace->mutex);

	char header[] = "{\n\t\"displayTimeUnit\": \"ms\", \"traceEvents\": [\n\0";
	char footer[] = "\t]\n}\0";

	trace->out_buffer[0] = '\0'; // reset out_buffer

	strcat_s(trace->out_buffer, strlen(trace->out_buffer) + sizeof(header), header);

	// process event queue
	for (size_t event_index = 0; event_index < trace->num_events; ++event_index)
	{
		trace_event_t* ev = &trace->events[event_index];
		char event_type = ev->event_type == k_trace_event_type_push_duration ? 'B' : 'E';

		char time_buffer[128];
		sprintf_s(time_buffer, sizeof(time_buffer),
			"\t\t{\"name\":\"%s\", \"ph\" : \"%c\", \"pid\" : 0, \"tid\" : \"%d\", \"ts\" : \"%d\"}\0",
			ev->name,
			event_type,
			ev->thread_id,
			timer_ticks_to_ms(ev->ticks_since_creation) * MICRO_TO_MILLI
		);

		strcat_s(trace->out_buffer, strlen(trace->out_buffer) + strlen(time_buffer) + 2, time_buffer);

		size_t buffer_len = strlen(trace->out_buffer);

		if (event_index < trace->num_events - 1)
		{
			trace->out_buffer[buffer_len++] = ',';
		}

		trace->out_buffer[buffer_len++] = '\n';
		trace->out_buffer[buffer_len++] = '\0';
	}

	strcat_s(trace->out_buffer, strlen(trace->out_buffer) + sizeof(footer), footer);

	fs_work_t* work = fs_write(trace->fs, trace->file_path, trace->out_buffer, strlen(trace->out_buffer), false, true);
	fs_work_get_result(work);
	fs_work_destroy(work);

	trace->num_events = 0;

	mutex_unlock(trace->mutex);
}
