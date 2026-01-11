#include "stk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <dirent.h>
#include <dlfcn.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define EVENT_BUFFER_SIZE 4096

#if !defined(_WIN32)
static char **scan_directory(const char *path, size_t *out_count)
{
	DIR *dir;
	struct dirent *entry;
	struct stat file_stat;
	char full_path[PATH_BUFFER_SIZE];
	char **file_list = NULL;
	size_t count = 0, index = 0;

	dir = opendir(path);
	if (!dir) {
		mkdir(path, 0755);
		*out_count = 0;
		return NULL;
	}

	while ((entry = readdir(dir)) != NULL) {
		sprintf(full_path, "%s/%s", path, entry->d_name);
		if (stat(full_path, &file_stat) == 0 &&
		    S_ISREG(file_stat.st_mode))
			count++;
	}

	if (count == 0) {
		closedir(dir);
		*out_count = 0;
		return NULL;
	}

	rewinddir(dir);

	file_list = (char **)malloc(count * sizeof(char *));
	if (!file_list) {
		closedir(dir);
		*out_count = 0;
		return NULL;
	}

	while ((entry = readdir(dir)) != NULL && index < count) {
		sprintf(full_path, "%s/%s", path, entry->d_name);
		if (stat(full_path, &file_stat) != 0 ||
		    !S_ISREG(file_stat.st_mode))
			continue;

		file_list[index] = (char *)malloc(strlen(entry->d_name) + 1);
		if (file_list[index]) {
			strcpy(file_list[index], entry->d_name);
			index++;
		}
	}

	closedir(dir);
	*out_count = index;
	return file_list;
}

#endif

#if !defined(__linux__) && !defined(_WIN32)
typedef struct {
	char *filename;
	time_t mtime;
} file_snapshot_t;

typedef struct {
	int kq;
	int dir_fd;
	char path[PATH_BUFFER_SIZE];
	file_snapshot_t *snapshots;
	size_t snapshot_count;
} kqueue_watch_context_t;

static file_snapshot_t *create_snapshot(const char *path, size_t *out_count)
{
	DIR *dir;
	struct dirent *entry;
	struct stat file_stat;
	char full_path[PATH_BUFFER_SIZE];
	file_snapshot_t *snapshots = NULL;
	size_t count = 0, index = 0;

	dir = opendir(path);
	if (!dir) {
		*out_count = 0;
		return NULL;
	}

	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;
		snprintf(full_path, sizeof(full_path), "%s/%s", path,
			 entry->d_name);
		if (stat(full_path, &file_stat) == 0 &&
		    S_ISREG(file_stat.st_mode))
			count++;
	}

	if (count == 0) {
		closedir(dir);
		*out_count = 0;
		return NULL;
	}

	snapshots = malloc(count * sizeof(file_snapshot_t));
	if (!snapshots) {
		closedir(dir);
		*out_count = 0;
		return NULL;
	}

	rewinddir(dir);

	while ((entry = readdir(dir)) != NULL && index < count) {
		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;
		snprintf(full_path, sizeof(full_path), "%s/%s", path,
			 entry->d_name);
		if (stat(full_path, &file_stat) != 0 ||
		    !S_ISREG(file_stat.st_mode))
			continue;

		snapshots[index].filename = malloc(strlen(entry->d_name) + 1);
		if (snapshots[index].filename) {
			strcpy(snapshots[index].filename, entry->d_name);
			snapshots[index].mtime = file_stat.st_mtime;
			index++;
		}
	}

	closedir(dir);
	*out_count = index;
	return snapshots;
}

static void free_snapshot(file_snapshot_t *snapshots, size_t count)
{
	size_t i;
	if (!snapshots)
		return;
	for (i = 0; i < count; i++) {
		free(snapshots[i].filename);
	}
	free(snapshots);
}

static file_snapshot_t *find_in_snapshot(file_snapshot_t *snapshots,
					 size_t count, const char *filename)
{
	size_t i;
	for (i = 0; i < count; i++) {
		if (strcmp(snapshots[i].filename, filename) == 0)
			return &snapshots[i];
	}
	return NULL;
}

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
	int fd, wd;

	fd = inotify_init1(IN_NONBLOCK);
	if (fd < 0)
		return NULL;

	wd = inotify_add_watch(
	    fd, path, IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM);

	if (wd < 0) {
		close(fd);
		return NULL;
	}

	return (void *)(long)fd;

#elif defined(_WIN32)
	HANDLE handle =
	    CreateFileA(path, FILE_LIST_DIRECTORY,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if (handle == INVALID_HANDLE_VALUE)
		return NULL;

	return (void *)handle;

#else
	struct kevent event;
	kqueue_watch_context_t *ctx = malloc(sizeof(kqueue_watch_context_t));
	if (!ctx)
		return NULL;

	ctx->kq = kqueue();
	ctx->dir_fd = open(path, O_RDONLY);
	strncpy(ctx->path, path, PATH_BUFFER_SIZE - 1);
	ctx->path[PATH_BUFFER_SIZE - 1] = '\0';

	ctx->snapshots = create_snapshot(path, &ctx->snapshot_count);

	if (ctx->kq < 0 || ctx->dir_fd < 0) {
		if (ctx->kq >= 0)
			close(ctx->kq);
		if (ctx->dir_fd >= 0)
			close(ctx->dir_fd);
		free_snapshot(ctx->snapshots, ctx->snapshot_count);
		free(ctx);
		return NULL;
	}

	EV_SET(&event, ctx->dir_fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
	       NOTE_WRITE | NOTE_DELETE | NOTE_RENAME, 0, NULL);

	kevent(ctx->kq, &event, 1, NULL, 0, NULL);

	return (void *)ctx;
#endif
}

void platform_directory_watch_stop(void *handle)
{
#ifdef __linux__
	if (!handle)
		return;
	close((int)(long)handle);

#elif defined(_WIN32)
	CloseHandle((HANDLE)handle);

#else
	kqueue_watch_context_t *ctx = (kqueue_watch_context_t *)handle;
	if (!ctx)
		return;

	if (ctx->dir_fd >= 0)
		close(ctx->dir_fd);
	if (ctx->kq >= 0)
		close(ctx->kq);
	free_snapshot(ctx->snapshots, ctx->snapshot_count);
	free(ctx);
#endif
}

stk_module_event_t *platform_directory_watch_check(void *handle,
						   char ***file_list,
						   size_t *out_count)
{
#if defined(__linux__) || defined(_WIN32)
	size_t file_count = 0;
#endif
	size_t index = 0;
	stk_module_event_t *events = NULL;

#ifdef __linux__
	char buffer[EVENT_BUFFER_SIZE];
	ssize_t bytes_read;
	struct inotify_event *event;
	char *event_ptr;
	int fd = (int)(long)handle;

	bytes_read = read(fd, buffer, sizeof(buffer));
	if (bytes_read <= 0) {
		*out_count = 0;
		return NULL;
	}

	event_ptr = buffer;
	while (event_ptr < buffer + bytes_read) {
		event = (struct inotify_event *)event_ptr;
		if (event->len > 0)
			file_count++;
		event_ptr += sizeof(struct inotify_event) + event->len;
	}

	if (file_count == 0) {
		*out_count = 0;
		return NULL;
	}

	events = malloc(file_count * sizeof(stk_module_event_t));
	*file_list = malloc(file_count * sizeof(char *));
	if (!events || !*file_list) {
		free(events);
		free(*file_list);
		*out_count = 0;
		return NULL;
	}

	index = 0;
	event_ptr = buffer;
	while (event_ptr < buffer + bytes_read) {
		event = (struct inotify_event *)event_ptr;
		if (event->len > 0) {
			events[index] =
			    (event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO))
				? STK_MOD_LOAD
				: STK_MOD_UNLOAD;
			(*file_list)[index] = malloc(strlen(event->name) + 1);
			if ((*file_list)[index]) {
				strcpy((*file_list)[index], event->name);
				index++;
			}
		}
		event_ptr += sizeof(struct inotify_event) + event->len;
	}
#elif defined(_WIN32)
	HANDLE h = (HANDLE)handle;
	BYTE buffer[EVENT_BUFFER_SIZE];
	DWORD bytes_returned;
	FILE_NOTIFY_INFORMATION *info;
	BYTE *event_ptr;
	int char_count;

	BOOL result = ReadDirectoryChangesW(h, buffer, sizeof(buffer), FALSE,
					    FILE_NOTIFY_CHANGE_FILE_NAME |
						FILE_NOTIFY_CHANGE_LAST_WRITE,
					    &bytes_returned, NULL, NULL);
	if (!result || bytes_returned == 0) {
		*out_count = 0;
		return NULL;
	}

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

	events = malloc(file_count * sizeof(stk_module_event_t));
	*file_list = malloc(file_count * sizeof(char *));
	if (!events || !*file_list) {
		free(events);
		free(*file_list);
		*out_count = 0;
		return NULL;
	}

	index = 0;
	event_ptr = buffer;
	while (1) {
		info = (FILE_NOTIFY_INFORMATION *)event_ptr;
		events[index] = (info->Action == FILE_ACTION_ADDED ||
				 info->Action == FILE_ACTION_MODIFIED ||
				 info->Action == FILE_ACTION_RENAMED_NEW_NAME)
				    ? STK_MOD_LOAD
				    : STK_MOD_UNLOAD;
		char_count = WideCharToMultiByte(
		    CP_UTF8, 0, info->FileName,
		    info->FileNameLength / sizeof(WCHAR), NULL, 0, NULL, NULL);

		if (char_count > 0) {
			(*file_list)[index] = malloc(char_count + 1);
			if ((*file_list)[index]) {
				WideCharToMultiByte(CP_UTF8, 0, info->FileName,
						    info->FileNameLength /
							sizeof(WCHAR),
						    (*file_list)[index],
						    char_count, NULL, NULL);
				(*file_list)[index][char_count] = '\0';
				index++;
			}
		}

		if (info->NextEntryOffset == 0)
			break;
		event_ptr += info->NextEntryOffset;
	}
#else
	kqueue_watch_context_t *ctx = (kqueue_watch_context_t *)handle;
	struct kevent event;
	struct timespec timeout = {0, 0};
	int nevents;
	file_snapshot_t *new_snapshots, *old_snap, *new_snap;
	size_t new_count, i;
	size_t change_count = 0;

	nevents = kevent(ctx->kq, NULL, 0, &event, 1, &timeout);
	if (nevents <= 0) {
		*out_count = 0;
		return NULL;
	}

	new_snapshots = create_snapshot(ctx->path, &new_count);

	for (i = 0; i < ctx->snapshot_count; i++) {
		new_snap = find_in_snapshot(new_snapshots, new_count,
					    ctx->snapshots[i].filename);
		if (!new_snap)
			change_count++;
	}

	for (i = 0; i < new_count; i++) {
		old_snap = find_in_snapshot(ctx->snapshots, ctx->snapshot_count,
					    new_snapshots[i].filename);
		if (!old_snap || old_snap->mtime != new_snapshots[i].mtime)
			change_count++;
	}

	if (change_count == 0) {
		free_snapshot(new_snapshots, new_count);
		*out_count = 0;
		return NULL;
	}

	events = malloc(change_count * sizeof(stk_module_event_t));
	*file_list = malloc(change_count * sizeof(char *));
	if (!events || !*file_list) {
		free(events);
		free(*file_list);
		free_snapshot(new_snapshots, new_count);
		*out_count = 0;
		return NULL;
	}

	index = 0;
	for (i = 0; i < ctx->snapshot_count && index < change_count; i++) {
		new_snap = find_in_snapshot(new_snapshots, new_count,
					    ctx->snapshots[i].filename);
		if (!new_snap) {
			events[index] = STK_MOD_UNLOAD;
			(*file_list)[index] =
			    malloc(strlen(ctx->snapshots[i].filename) + 1);
			if ((*file_list)[index]) {
				strcpy((*file_list)[index],
				       ctx->snapshots[i].filename);
				index++;
			}
		}
	}

	for (i = 0; i < new_count && index < change_count; i++) {
		old_snap = find_in_snapshot(ctx->snapshots, ctx->snapshot_count,
					    new_snapshots[i].filename);
		if (!old_snap || old_snap->mtime != new_snapshots[i].mtime) {
			events[index] = STK_MOD_LOAD;
			(*file_list)[index] =
			    malloc(strlen(new_snapshots[i].filename) + 1);
			if ((*file_list)[index]) {
				strcpy((*file_list)[index],
				       new_snapshots[i].filename);
				index++;
			}
		}
	}

	free_snapshot(ctx->snapshots, ctx->snapshot_count);
	ctx->snapshots = new_snapshots;
	ctx->snapshot_count = new_count;
#endif
	*out_count = index;
	return events;
}

char **platform_directory_init_scan(const char *path, size_t *out_count)
{
#if defined(_WIN32)
	WIN32_FIND_DATAW find_data;
	HANDLE find_handle;
	WCHAR search_path[PATH_BUFFER_SIZE];
	char **file_list = NULL;
	size_t count = 0, index = 0;
	int utf8_len;

	swprintf(search_path, PATH_BUFFER_SIZE, L"%S\\*", path);

	find_handle = FindFirstFileW(search_path, &find_data);
	if (find_handle == INVALID_HANDLE_VALUE) {
		CreateDirectoryA(path, NULL);
		*out_count = 0;
		return NULL;
	}

	do {
		if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			count++;
	} while (FindNextFileW(find_handle, &find_data));

	FindClose(find_handle);

	if (count == 0) {
		*out_count = 0;
		return NULL;
	}

	find_handle = FindFirstFileW(search_path, &find_data);
	if (find_handle == INVALID_HANDLE_VALUE) {
		*out_count = 0;
		return NULL;
	}

	file_list = (char **)malloc(count * sizeof(char *));
	if (!file_list) {
		FindClose(find_handle);
		*out_count = 0;
		return NULL;
	}

	do {
		if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
		    index < count) {
			utf8_len =
			    WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName,
						-1, NULL, 0, NULL, NULL);
			if (utf8_len > 0) {
				file_list[index] = (char *)malloc(utf8_len);
				if (file_list[index]) {
					WideCharToMultiByte(
					    CP_UTF8, 0, find_data.cFileName, -1,
					    file_list[index], utf8_len, NULL,
					    NULL);
					index++;
				}
			}
		}
	} while (FindNextFileW(find_handle, &find_data) && index < count);

	FindClose(find_handle);
	*out_count = index;
	return file_list;
#else
	return scan_directory(path, out_count);
#endif
}
