#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <dlfcn.h>
#include <sys/inotify.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#define EVENT_BUFFER_SIZE 4096

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

	return (void *)(long)fd;
#elif defined(_WIN32)
	HANDLE handle;
	handle =
	    CreateFileA(path, FILE_LIST_DIRECTORY,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if (handle == INVALID_HANDLE_VALUE)
		return NULL;

	return (void *)handle;
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

	fd = (int)(long)handle;
	close(fd);
#elif defined(_WIN32)
	HANDLE h;
	h = (HANDLE)handle;
	CloseHandle(h);
#else
	(void)handle;
#endif
}

char **platform_directory_watch_check(void *handle, size_t *out_count)
{
#ifdef __linux__
	int fd;
	char buffer[EVENT_BUFFER_SIZE];
	ssize_t bytes_read;
	struct inotify_event *event;
	char *event_ptr;
	size_t file_count, index;
	char **file_list;

	fd = (int)(long)handle;
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
	HANDLE h;
	BYTE buffer[EVENT_BUFFER_SIZE];
	DWORD bytes_returned;
	FILE_NOTIFY_INFORMATION *info;
	BYTE *event_ptr;
	char **file_list;
	size_t file_count, index;
	int char_count;
	BOOL result;

	h = (HANDLE)handle;

	result = ReadDirectoryChangesW(h, buffer, sizeof(buffer), FALSE,
				       FILE_NOTIFY_CHANGE_FILE_NAME |
					   FILE_NOTIFY_CHANGE_LAST_WRITE,
				       &bytes_returned, NULL, NULL);
	if (!result || bytes_returned == 0) {
		*out_count = 0;
		return NULL;
	}

	file_count = 0;
	event_ptr = buffer;
	while (1) {
		info = (FILE_NOTIFY_INFORMATION *)event_ptr;
		file_count++;

		if (info->NextEntryOffset == 0)
			break;

		event_ptr += info->NextEntryOffset;
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
	while (1) {
		info = (FILE_NOTIFY_INFORMATION *)event_ptr;

		char_count = WideCharToMultiByte(
		    CP_UTF8, 0, info->FileName,
		    info->FileNameLength / sizeof(WCHAR), NULL, 0, NULL, NULL);

		if (char_count > 0) {
			file_list[index] = malloc(char_count + 1);
			if (file_list[index]) {
				WideCharToMultiByte(
				    CP_UTF8, 0, info->FileName,
				    info->FileNameLength / sizeof(WCHAR),
				    file_list[index], char_count, NULL, NULL);
				file_list[index][char_count] = '\0';
				index++;
			}
		}

		if (info->NextEntryOffset == 0)
			break;

		event_ptr += info->NextEntryOffset;
	}

	*out_count = index;
	return file_list;
#else
#endif
}
