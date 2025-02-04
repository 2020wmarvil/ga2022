#include "fs.h"

#include "event.h"
#include "heap.h"
#include "queue.h"
#include "thread.h"

#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "lz4/lz4.h"

typedef struct fs_t
{
	heap_t* heap;

	queue_t* file_queue;
	thread_t* file_thread;

	queue_t* file_compression_queue;
	thread_t* file_compression_thread;
} fs_t;

typedef enum fs_work_op_t
{
	k_fs_work_op_read,
	k_fs_work_op_write,
} fs_work_op_t;

typedef struct fs_work_t
{
	fs_t* fs;
	heap_t* heap;
	fs_work_op_t op;
	char path[1024];
	bool null_terminate;
	bool use_compression;
	bool append_mode;
	void* buffer;
	size_t size;
	event_t* done;
	int result;
} fs_work_t;

static int file_thread_func(void* user);
static int file_compression_thread_func(void* user);

fs_t* fs_create(heap_t* heap, int queue_capacity)
{
	fs_t* fs = heap_alloc(heap, sizeof(fs_t), 8);
	fs->heap = heap;

	fs->file_queue = queue_create(heap, queue_capacity);
	fs->file_thread = thread_create(file_thread_func, fs);

	fs->file_compression_queue = queue_create(heap, queue_capacity);
	fs->file_compression_thread = thread_create(file_compression_thread_func, fs);

	return fs;
}

void fs_destroy(fs_t* fs)
{
	queue_push(fs->file_queue, NULL);
	thread_destroy(fs->file_thread);
	queue_destroy(fs->file_queue);

	queue_push(fs->file_compression_queue, NULL);
	thread_destroy(fs->file_compression_thread);
	queue_destroy(fs->file_compression_queue);

	heap_free(fs->heap, fs);
}

fs_work_t* fs_read(fs_t* fs, const char* path, heap_t* heap, bool null_terminate, bool use_compression)
{
	fs_work_t* work = heap_alloc(fs->heap, sizeof(fs_work_t), 8);
	work->fs = fs;
	work->heap = heap;
	work->op = k_fs_work_op_read;
	strcpy_s(work->path, sizeof(work->path), path);
	work->buffer = NULL;
	work->size = 0;
	work->done = event_create();
	work->result = 0;
	work->null_terminate = null_terminate;
	work->use_compression = use_compression;
	work->append_mode = false;
	queue_push(fs->file_queue, work);
	return work;
}

fs_work_t* fs_write(fs_t* fs, const char* path, const void* buffer, size_t size, bool use_compression, bool append_mode)
{
	fs_work_t* work = heap_alloc(fs->heap, sizeof(fs_work_t), 8);
	work->fs = fs;
	work->heap = fs->heap;
	work->op = k_fs_work_op_write;
	strcpy_s(work->path, sizeof(work->path), path);
	work->buffer = (void*)buffer;
	work->size = size;
	work->done = event_create();
	work->result = 0;
	work->null_terminate = false;
	work->use_compression = use_compression;
	work->append_mode = append_mode;

	if (use_compression)
	{
		queue_push(fs->file_compression_queue, work);
	}
	else
	{
		queue_push(fs->file_queue, work);
	}

	return work;
}

bool fs_work_is_done(fs_work_t* work)
{
	return work ? event_is_raised(work->done) : true;
}

void fs_work_wait(fs_work_t* work)
{
	if (work)
	{
		event_wait(work->done);
	}
}

int fs_work_get_result(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->result : -1;
}

void* fs_work_get_buffer(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->buffer : NULL;
}

size_t fs_work_get_size(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->size : 0;
}

void fs_work_destroy(fs_work_t* work)
{
	if (work)
	{
		event_wait(work->done);
		event_destroy(work->done);
		heap_free(work->heap, work);
	}
}

static void file_read(fs_work_t* work)
{
	wchar_t wide_path[1024];
	if (MultiByteToWideChar(CP_UTF8, 0, work->path, -1, wide_path, sizeof(wide_path)) <= 0)
	{
		work->result = -1;
		event_signal(work->done);
		return;
	}

	HANDLE handle = CreateFile(wide_path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		work->result = GetLastError();
		event_signal(work->done);
		return;
	}

	if (!GetFileSizeEx(handle, (PLARGE_INTEGER)&work->size))
	{
		work->result = GetLastError();
		CloseHandle(handle);
		event_signal(work->done);
		return;
	}

	work->buffer = heap_alloc(work->heap, work->null_terminate ? work->size + 1 : work->size, 8);

	DWORD bytes_read = 0;
	if (!ReadFile(handle, work->buffer, (DWORD)work->size, &bytes_read, NULL))
	{
		work->result = GetLastError();
		CloseHandle(handle);
		event_signal(work->done);
		return;
	}

	work->size = bytes_read;
	if (work->null_terminate)
	{
		((char*)work->buffer)[bytes_read] = 0;
	}

	CloseHandle(handle);

	if (work->use_compression)
	{
		queue_push(work->fs->file_compression_queue, work);
	}
	else
	{
		event_signal(work->done);
	}
}

static void file_write(fs_work_t* work)
{
	wchar_t wide_path[1024];
	if (MultiByteToWideChar(CP_UTF8, 0, work->path, -1, wide_path, sizeof(wide_path)) <= 0)
	{
		work->result = -1;
		event_signal(work->done);
		return;
	}

	HANDLE handle = INVALID_HANDLE_VALUE;

	DWORD dwAttrib = GetFileAttributes(wide_path);
	bool should_append = work->append_mode && dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY);

	// Only write in append mode if the file already exists
	if (should_append)
	{
		handle = CreateFile(wide_path, FILE_APPEND_DATA, FILE_SHARE_WRITE, NULL,
			OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	}
	else
	{
		handle = CreateFile(wide_path, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	}

	if (handle == INVALID_HANDLE_VALUE)
	{
		work->result = GetLastError();
		event_signal(work->done);
		return;
	}

	DWORD bytes_written = 0;

	// move file pointer to eof
	if (should_append)
	{
		bytes_written = SetFilePointer(handle, 0l, NULL, FILE_END);
	}

	if (!WriteFile(handle, work->buffer, (DWORD)work->size, &bytes_written, NULL) || bytes_written == INVALID_SET_FILE_POINTER)
	{
		work->result = GetLastError();
		CloseHandle(handle);
		event_signal(work->done);
		return;
	}

	work->size = bytes_written;

	CloseHandle(handle);

	event_signal(work->done);
}

static int file_thread_func(void* user)
{
	fs_t* fs = user;
	while (true)
	{
		fs_work_t* work = queue_pop(fs->file_queue);
		if (work == NULL)
		{
			break;
		}
		
		switch (work->op)
		{
		case k_fs_work_op_read:
			file_read(work);
			break;
		case k_fs_work_op_write:
			file_write(work);
			break;
		}
	}
	return 0;
}

static void file_compress(fs_work_t* work)
{
	char* result = heap_alloc(work->heap, work->size, 8);
	int compressed_size = LZ4_compress_default(work->buffer, result, (int)work->size, (int)work->size + 12);
	work->buffer = result;
	work->size = compressed_size;

	if (compressed_size == 0) // LZ4_compress_default returns 0 when it fails
	{
		work->result = -1;
	}

	queue_push(work->fs->file_queue, work); // queue another write in order to output our compression
}

static void file_decompress(fs_work_t* work)
{
	char* result = heap_alloc(work->heap, LZ4_MAX_INPUT_SIZE, 8);
	int uncompressed_size = LZ4_decompress_safe(work->buffer, result, (int)work->size, LZ4_MAX_INPUT_SIZE);

	work->buffer = result;
	((char*)work->buffer)[uncompressed_size] = 0; // null terminate
	work->size = uncompressed_size;

	if (uncompressed_size < 0) // LZ4_decompress_safe returns negative values when it fails
	{
		work->result = -1;
	}

	event_signal(work->done);
}

static int file_compression_thread_func(void* user)
{
	fs_t* fs = user;
	while (true)
	{
		fs_work_t* work = queue_pop(fs->file_compression_queue);
		if (work == NULL)
		{
			break;
		}

		switch (work->op)
		{
		case k_fs_work_op_read:
			file_decompress(work);
			break;
		case k_fs_work_op_write:
			file_compress(work);
			break;
		}
	}

	return 0;
}
