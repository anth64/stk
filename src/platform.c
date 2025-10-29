#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <stdint.h>
#include <sys/inotify.h>
#include <unistd.h>
#define PLATFORM_INOTIFY_EVENT_BUFFER_SIZE 4096
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

void *platform_load_library(const char *path)
{
#ifdef _WIN32
	return (void *)LoadLibraryA(path);
#else
	return dlopen(path, RTLD_NOW);
#endif
}

void platform_unload_library(void *handle)
{
#ifdef _WIN32
	FreeLibrary((HMODULE)handle);
#else
	dlclose(handle);
#endif
}

void *platform_get_symbol(void *handle, const char *symbol)
{
#ifdef _WIN32
	return (void *)GetProcAddress((HMODULE)handle, symbol);
#else
	return dlsym(handle, symbol);
#endif
}

void *platform_directory_watch_start(const char *path)
{
#ifdef __linux__
	int fd;
	int wd;

	fd = inotify_init1(IN_NONBLOCK);
	if (fd < 0)
		return NULL;

	wd = inotify_add_watch(fd, path,
			       IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_TO |
				   IN_MOVED_FROM);
	if (wd < 0) {
		close(fd);
		return NULL;
	}

	return (void *)(intptr_t)fd;
#elif defined(_WIN32)
	return NULL
#else
	return NULL
#endif
}

void platform_directory_watch_stop(void *handle)
{
#ifdef __linux__
	int fd;
	if (!handle)
		return;

	fd = (int)(intptr_t)handle;
	close(fd);
#elif defined(_WIN32)
	(void)handle;
#else
	(void)handle;
#endif
}

char **platform_directory_watch_check(void *handle, size_t *out_count)
{
#ifdef __linux__
	int fd;
	char buffer[PLATFORM_INOTIFY_EVENT_BUFFER_SIZE];
	ssize_t bytes_read;
	struct inotify_event *event;
	char *event_ptr;
	size_t file_count, index;
	char **file_list;

	fd = (int)(intptr_t)handle;
	bytes_read = read(fd, buffer, sizeof(buffer));
	if (bytes_read <= 0) {
		*out_count = 0;
		return NULL;
	}

	file_count = 0;
	event_ptr = buffer;
	while (event_ptr < buffer + bytes_read) {
		event = (struct inotify_event *)event_ptr;
		if (event->len > 0)
			++file_count;

		event_ptr += sizeof(struct inotify_event) + event->len;
	}

	if (file_count == 0) {
		*out_count = 0;
		return NULL;
	}

	file_list = malloc(file_count * sizeof(char *));
	if (!file_list) {
		*out_count = 0;
		return NULL;
	}

	index = 0;
	event_ptr = buffer;
	while (event_ptr < buffer + bytes_read) {
		event = (struct inotify_event *)event_ptr;
		if (event->len > 0) {
			file_list[index] = malloc(strlen(event->name) + 1);
			if (file_list[index]) {
				strcpy(file_list[index], event->name);
				index++;
			}
		}

		event_ptr += sizeof(struct inotify_event) + event->len;
	}

	*out_count = index;
	return file_list;
#elif defined(_WIN32)
#else
#endif
}
