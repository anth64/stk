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

#if defined(__linux__) || defined(_WIN32)
#define STK_EVENT_BUFFER 4096
#endif

#if defined(_WIN32)
#define STK_MODULE_EXT ".dll"
#elif defined(__APPLE__)
#define STK_MODULE_EXT ".dylib"
#else
#define STK_MODULE_EXT ".so"
#endif

#define STK_MODULE_EXT_LEN (sizeof(STK_MODULE_EXT) - 1)

static uint8_t is_valid_module_file(const char *filename)
{
	const char *ext;
	size_t name_len;

	if (!filename)
		return 0;

	name_len = strlen(filename);

	if (name_len <= STK_MODULE_EXT_LEN)
		return 0;

	ext = filename + (name_len - STK_MODULE_EXT_LEN);
	return strcmp(ext, STK_MODULE_EXT) == 0;
}

static uint8_t is_module_loaded(const char *filename,
				char (*loaded_module_ids)[STK_MOD_ID_BUFFER],
				size_t loaded_count)
{
	char module_id[STK_MOD_ID_BUFFER];
	const char *basename;
	char *dot;
	size_t i;

	basename = strrchr(filename, '/');
#ifdef _WIN32
	if (!basename)
		basename = strrchr(filename, '\\');
#endif
	if (!basename)
		basename = filename;
	else
		basename++;

	strncpy(module_id, basename, STK_MOD_ID_BUFFER - 1);
	module_id[STK_MOD_ID_BUFFER - 1] = '\0';

	dot = strrchr(module_id, '.');
	if (dot)
		*dot = '\0';

	for (i = 0; i < loaded_count; i++)
		if (strcmp(loaded_module_ids[i], module_id) == 0)
			return 1;

	return 0;
}

#if !defined(__linux__) && !defined(_WIN32)
typedef struct {
	char filename[STK_PATH_MAX];
	time_t mtime;
} file_snapshot_t;

typedef struct {
	int kq;
	int dir_fd;
	char path[STK_PATH_MAX];
	file_snapshot_t *snapshots;
	size_t snapshot_count;
	size_t snapshot_capacity;
} kqueue_watch_context_t;

static file_snapshot_t *create_snapshot(const char *path, size_t *out_count,
					size_t *out_capacity)
{
	DIR *dir;
	struct dirent *entry;
	struct stat file_stat;
	char work_path[STK_PATH_MAX_OS];
	file_snapshot_t *snapshots;
	size_t count, capacity, index;

	snapshots = NULL;
	count = 0;
	capacity = 0;
	index = 0;

	dir = opendir(path);
	if (!dir) {
		*out_count = 0;
		*out_capacity = 0;
		return NULL;
	}

	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;

		if (!is_valid_module_file(entry->d_name))
			continue;

		sprintf(work_path, "%s/%s", path, entry->d_name);
		if (stat(work_path, &file_stat) == 0 &&
		    S_ISREG(file_stat.st_mode))
			count++;
	}

	if (count == 0) {
		closedir(dir);
		*out_count = 0;
		*out_capacity = 0;
		return NULL;
	}

	capacity = count + 8;
	snapshots = malloc(capacity * sizeof(file_snapshot_t));
	if (!snapshots) {
		closedir(dir);
		*out_count = 0;
		*out_capacity = 0;
		return NULL;
	}

	rewinddir(dir);

	while ((entry = readdir(dir)) != NULL && index < count) {
		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;

		if (!is_valid_module_file(entry->d_name))
			continue;

		sprintf(work_path, "%s/%s", path, entry->d_name);
		if (stat(work_path, &file_stat) != 0 ||
		    !S_ISREG(file_stat.st_mode))
			continue;

		strncpy(snapshots[index].filename, entry->d_name,
			STK_PATH_MAX - 1);
		snapshots[index].filename[STK_PATH_MAX - 1] = '\0';
		snapshots[index].mtime = file_stat.st_mtime;
		index++;
	}

	closedir(dir);
	*out_count = index;
	*out_capacity = capacity;
	return snapshots;
}

static file_snapshot_t *find_in_snapshot(file_snapshot_t *snapshots,
					 size_t count, const char *filename)
{
	size_t i;
	for (i = 0; i < count; i++) {
		if (strcmp(snapshots[i].filename, filename) == 0) {
			return &snapshots[i];
		}
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
	return (handle == INVALID_HANDLE_VALUE) ? NULL : (void *)handle;
#else
	struct kevent event;
	kqueue_watch_context_t *ctx;

	ctx = malloc(sizeof(kqueue_watch_context_t));
	if (!ctx)
		return NULL;

	ctx->kq = kqueue();
	ctx->dir_fd = open(path, O_RDONLY);
	strncpy(ctx->path, path, STK_PATH_MAX - 1);
	ctx->path[STK_PATH_MAX - 1] = '\0';
	ctx->snapshots = create_snapshot(path, &ctx->snapshot_count,
					 &ctx->snapshot_capacity);

	if (ctx->kq < 0 || ctx->dir_fd < 0) {
		if (ctx->kq >= 0)
			close(ctx->kq);
		if (ctx->dir_fd >= 0)
			close(ctx->dir_fd);
		free(ctx->snapshots);
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
	if (handle)
		close((int)(long)handle);
#elif defined(_WIN32)
	if (handle)
		CloseHandle((HANDLE)handle);
#else
	kqueue_watch_context_t *ctx;
	ctx = (kqueue_watch_context_t *)handle;
	if (!ctx)
		return;
	if (ctx->dir_fd >= 0)
		close(ctx->dir_fd);
	if (ctx->kq >= 0)
		close(ctx->kq);
	free(ctx->snapshots);
	free(ctx);
#endif
}

stk_module_event_t *platform_directory_watch_check(
    void *handle, char (**file_list)[STK_PATH_MAX], size_t *out_count,
    char (*loaded_module_ids)[STK_MOD_ID_BUFFER], const size_t loaded_count)
{
	size_t index = 0;
	stk_module_event_t *events = NULL;

#ifdef __linux__
	char buffer[STK_EVENT_BUFFER];
	ssize_t bytes_read;
	struct inotify_event *event;
	char *event_ptr;
	int fd;
	size_t file_count;

	fd = (int)(long)handle;
	file_count = 0;

	bytes_read = read(fd, buffer, sizeof(buffer));
	if (bytes_read <= 0) {
		*out_count = 0;
		return NULL;
	}

	event_ptr = buffer;
	while (event_ptr < buffer + bytes_read) {
		event = (struct inotify_event *)event_ptr;
		if (event->len > 0 && is_valid_module_file(event->name))
			file_count++;
		event_ptr += sizeof(struct inotify_event) + event->len;
	}

	if (file_count == 0) {
		*out_count = 0;
		return NULL;
	}

	events = malloc(file_count * sizeof(stk_module_event_t));
	*file_list = malloc(file_count * sizeof(**file_list));

	index = 0;
	event_ptr = buffer;
	while (event_ptr < buffer + bytes_read) {
		event = (struct inotify_event *)event_ptr;
		if (event->len > 0 && is_valid_module_file(event->name)) {
			events[index] =
			    (event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO))
				? (is_module_loaded(event->name,
						    loaded_module_ids,
						    loaded_count)
				       ? STK_MOD_RELOAD
				       : STK_MOD_LOAD)
				: STK_MOD_UNLOAD;
			strncpy((*file_list)[index], event->name,
				STK_PATH_MAX - 1);
			(*file_list)[index][STK_PATH_MAX - 1] = '\0';
			index++;
		}
		event_ptr += sizeof(struct inotify_event) + event->len;
	}
#elif defined(_WIN32)
	HANDLE h;
	BYTE buffer[STK_EVENT_BUFFER];
	DWORD bytes_returned;
	FILE_NOTIFY_INFORMATION *info;
	BYTE *event_ptr;
	size_t file_count;
	char temp_filename[STK_PATH_MAX];
	int len;
	BOOL result;

	h = (HANDLE)handle;
	file_count = 0;

	result = ReadDirectoryChangesW(h, buffer, sizeof(buffer), FALSE,
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
		len = WideCharToMultiByte(CP_UTF8, 0, info->FileName,
					  info->FileNameLength / sizeof(WCHAR),
					  temp_filename, STK_PATH_MAX - 1, NULL,
					  NULL);
		if (len > 0) {
			temp_filename[len] = '\0';
			if (is_valid_module_file(temp_filename))
				file_count++;
		}
		if (info->NextEntryOffset == 0)
			break;
		event_ptr += info->NextEntryOffset;
	}

	if (file_count == 0) {
		*out_count = 0;
		return NULL;
	}

	events = malloc(file_count * sizeof(stk_module_event_t));
	*file_list = malloc(file_count * sizeof(**file_list));

	index = 0;
	event_ptr = buffer;
	while (1) {
		info = (FILE_NOTIFY_INFORMATION *)event_ptr;
		len = WideCharToMultiByte(CP_UTF8, 0, info->FileName,
					  info->FileNameLength / sizeof(WCHAR),
					  (*file_list)[index], STK_PATH_MAX - 1,
					  NULL, NULL);
		if (len > 0) {
			(*file_list)[index][len] = '\0';
			if (is_valid_module_file((*file_list)[index])) {
				events[index] =
				    (info->Action != FILE_ACTION_REMOVED &&
				     info->Action !=
					 FILE_ACTION_RENAMED_OLD_NAME)
					? (is_module_loaded((*file_list)[index],
							    loaded_module_ids,
							    loaded_count)
					       ? STK_MOD_RELOAD
					       : STK_MOD_LOAD)
					: STK_MOD_UNLOAD;
				index++;
			}
		}
		if (info->NextEntryOffset == 0)
			break;
		event_ptr += info->NextEntryOffset;
	}
#else
	kqueue_watch_context_t *ctx;
	struct kevent event;
	struct timespec timeout;
	file_snapshot_t *new_snapshots, *old_snap;
	size_t new_count, new_capacity, i, change_count;

	ctx = (kqueue_watch_context_t *)handle;
	timeout.tv_sec = 0;
	timeout.tv_nsec = 0;
	change_count = 0;

	if (kevent(ctx->kq, NULL, 0, &event, 1, &timeout) <= 0) {
		*out_count = 0;
		return NULL;
	}

	new_snapshots = create_snapshot(ctx->path, &new_count, &new_capacity);

	for (i = 0; i < ctx->snapshot_count; i++)
		if (!find_in_snapshot(new_snapshots, new_count,
				      ctx->snapshots[i].filename))
			change_count++;

	for (i = 0; i < new_count; i++) {
		old_snap = find_in_snapshot(ctx->snapshots, ctx->snapshot_count,
					    new_snapshots[i].filename);
		if (!old_snap || old_snap->mtime != new_snapshots[i].mtime)
			change_count++;
	}

	if (change_count == 0) {
		free(new_snapshots);
		*out_count = 0;
		return NULL;
	}

	events = malloc(change_count * sizeof(stk_module_event_t));
	*file_list = malloc(change_count * sizeof(**file_list));

	index = 0;

	for (i = 0; i < ctx->snapshot_count && index < change_count; i++) {
		if (!find_in_snapshot(new_snapshots, new_count,
				      ctx->snapshots[i].filename)) {
			events[index] = STK_MOD_UNLOAD;
			strncpy((*file_list)[index], ctx->snapshots[i].filename,
				STK_PATH_MAX - 1);
			(*file_list)[index][STK_PATH_MAX - 1] = '\0';
			index++;
		}
	}

	for (i = 0; i < new_count && index < change_count; i++) {
		old_snap = find_in_snapshot(ctx->snapshots, ctx->snapshot_count,
					    new_snapshots[i].filename);
		if (!old_snap || old_snap->mtime != new_snapshots[i].mtime) {
			events[index] =
			    is_module_loaded(new_snapshots[i].filename,
					     loaded_module_ids, loaded_count)
				? STK_MOD_RELOAD
				: STK_MOD_LOAD;
			strncpy((*file_list)[index], new_snapshots[i].filename,
				STK_PATH_MAX - 1);
			(*file_list)[index][STK_PATH_MAX - 1] = '\0';
			index++;
		}
	}

	free(ctx->snapshots);
	ctx->snapshots = new_snapshots;
	ctx->snapshot_count = new_count;
	ctx->snapshot_capacity = new_capacity;
#endif
	*out_count = index;
	return events;
}

char (*platform_directory_init_scan(const char *mod_dir,
				    size_t *out_count))[STK_PATH_MAX]
{
	char(*file_list)[STK_PATH_MAX] = NULL;
	char work_path[STK_PATH_MAX_OS];
	size_t count = 0, index = 0;

#if defined(_WIN32)
	WIN32_FIND_DATAW find_data;
	HANDLE find_handle;
	WCHAR search_path[STK_PATH_MAX_OS];
	char temp_filename[STK_PATH_MAX];
	int len;

	swprintf(search_path, STK_PATH_MAX_OS, L"%S\\*", mod_dir);
	find_handle = FindFirstFileW(search_path, &find_data);

	if (find_handle == INVALID_HANDLE_VALUE) {
		CreateDirectoryA(mod_dir, NULL);
		*out_count = 0;
		return NULL;
	}

	do {
		if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;

		len = WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName, -1,
					  temp_filename, STK_PATH_MAX - 1, NULL,
					  NULL);
		if (len > 0) {
			temp_filename[len] = '\0';
			if (is_valid_module_file(temp_filename))
				count++;
		}
	} while (FindNextFileW(find_handle, &find_data));
	FindClose(find_handle);

	if (count == 0) {
		*out_count = 0;
		return NULL;
	}

	find_handle = FindFirstFileW(search_path, &find_data);
	file_list = malloc(count * sizeof(*file_list));

	do {
		if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;

		len = WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName, -1,
					  file_list[index], STK_PATH_MAX - 1,
					  NULL, NULL);
		if (len > 0 && index < count) {
			file_list[index][len] = '\0';
			if (is_valid_module_file(file_list[index]))
				index++;
		}
	} while (FindNextFileW(find_handle, &find_data) && index < count);
	FindClose(find_handle);
#else
	DIR *dir;
	struct dirent *entry;
	struct stat file_stat;

	dir = opendir(mod_dir);
	if (!dir) {
		mkdir(mod_dir, 0755);
		*out_count = 0;
		return NULL;
	}

	while ((entry = readdir(dir)) != NULL) {
		if (!is_valid_module_file(entry->d_name))
			continue;

		sprintf(work_path, "%s/%s", mod_dir, entry->d_name);
		if (stat(work_path, &file_stat) == 0 &&
		    S_ISREG(file_stat.st_mode))
			count++;
	}

	if (count == 0) {
		closedir(dir);
		*out_count = 0;
		return NULL;
	}

	rewinddir(dir);
	file_list = malloc(count * sizeof(*file_list));
	if (!file_list) {
		closedir(dir);
		*out_count = 0;
		return NULL;
	}

	while ((entry = readdir(dir)) != NULL && index < count) {
		if (!is_valid_module_file(entry->d_name))
			continue;

		sprintf(work_path, "%s/%s", mod_dir, entry->d_name);
		if (stat(work_path, &file_stat) != 0 ||
		    !S_ISREG(file_stat.st_mode))
			continue;

		strncpy(file_list[index], entry->d_name, STK_PATH_MAX - 1);
		file_list[index][STK_PATH_MAX - 1] = '\0';
		index++;
	}
	closedir(dir);
#endif
	*out_count = index;
	return file_list;
}
