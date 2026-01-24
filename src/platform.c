#include "stk.h"
#include <stdint.h>
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
#define WIN32_LEAN_AND_MEAN
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

int is_module_loaded(const char *filename,
		     char (*loaded_ids)[STK_MOD_ID_BUFFER], size_t count);
uint8_t is_valid_module_file(const char *filename);

#ifdef _WIN32
static uint8_t is_file_ready(const char *dir_path, const char *filename)
{
	char full_path[STK_PATH_MAX_OS];
	HANDLE h;

	sprintf(full_path, "%s\\%s", dir_path, filename);
	h = CreateFileA(full_path, GENERIC_READ, 0, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	CloseHandle(h);
	return 1;
}
#endif

#ifndef __linux__
typedef struct {
	char filename[STK_PATH_MAX];
#ifdef _WIN32
	FILETIME mtime;
#else
	time_t mtime;
#endif
} platform_snapshot_t;

typedef struct {
	char path[STK_PATH_MAX];
	platform_snapshot_t *snaps;
	size_t count;
	union {
#ifdef _WIN32
		HANDLE change_handle;
#else
		struct {
			int kq;
			int dir_fd;
			int *file_fds;
			size_t file_fd_count;
		} k;
#endif
	} watch;
} platform_watch_context_t;
#endif

int platform_mkdir(const char *path)
{
#ifdef _WIN32
	return CreateDirectoryA(path, NULL) ? 0 : -1;
#else
	return mkdir(path, 0755);
#endif
}

int platform_remove_file(const char *path)
{
#ifdef _WIN32
	return DeleteFileA(path) ? 0 : -1;
#else
	return unlink(path);
#endif
}

int platform_copy_file(const char *from, const char *to)
{
#ifdef _WIN32
	return CopyFileA(from, to, FALSE) ? 0 : -1;
#else
	FILE *src = NULL, *dst = NULL;
	char buf[STK_PATH_MAX_OS];
	size_t n;
	int ret = -1;

	src = fopen(from, "rb");
	if (!src)
		goto cleanup;

	dst = fopen(to, "wb");
	if (!dst)
		goto cleanup;

	while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
		fwrite(buf, 1, n, dst);

	ret = 0;

cleanup:
	if (src)
		fclose(src);

	if (dst)
		fclose(dst);

	return ret;
#endif
}

int platform_remove_dir(const char *path)
{
#ifdef _WIN32
	WIN32_FIND_DATAA fd;
	HANDLE h;
	char s[STK_PATH_MAX_OS], f[STK_PATH_MAX_OS];
	sprintf(s, "%s\\*", path);

	h = FindFirstFileA(s, &fd);
	if (h == INVALID_HANDLE_VALUE)
		goto remove_dir;

	do {
		if (strcmp(fd.cFileName, ".") == 0 ||
		    strcmp(fd.cFileName, "..") == 0)
			continue;

		sprintf(f, "%s\\%s", path, fd.cFileName);
		DeleteFileA(f);
	} while (FindNextFileA(h, &fd));

	FindClose(h);

remove_dir:
	return RemoveDirectoryA(path) ? 0 : -1;
#else
	DIR *dir;
	struct dirent *entry;
	char filepath[STK_PATH_MAX_OS];

	dir = opendir(path);
	if (!dir)
		return -1;

loop:
	entry = readdir(dir);
	if (!entry)
		goto loop_end;

	if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
		goto loop;

	sprintf(filepath, "%s/%s", path, entry->d_name);
	unlink(filepath);
	goto loop;

loop_end:
	closedir(dir);
	return rmdir(path);
#endif
}

void *platform_load_library(const char *path)
{
#ifdef _WIN32
	return (void *)LoadLibraryA(path);
#else
	return dlopen(path, RTLD_NOW);
#endif
}

void platform_unload_library(void *h)
{
#ifdef _WIN32
	FreeLibrary((HMODULE)h);
#else
	dlclose(h);
#endif
}

void *platform_get_symbol(void *h, const char *s)
{
#ifdef _WIN32
	return (void *)(intptr_t)GetProcAddress((HMODULE)h, s);
#else
	return dlsym(h, s);
#endif
}

char (*platform_directory_init_scan(const char *dir_path, size_t *out_count))
    [STK_PATH_MAX] {
	    size_t count = 0, i = 0;
	    char (*list)[STK_PATH_MAX] = NULL;
#ifdef _WIN32
	    WIN32_FIND_DATAA fd;
	    HANDLE h;
	    char s[STK_PATH_MAX_OS];

	    sprintf(s, "%s\\*", dir_path);
	    h = FindFirstFileA(s, &fd);
	    if (h == INVALID_HANDLE_VALUE)
		    goto create_and_exit;

	    do {
		    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			    continue;
		    if (is_valid_module_file(fd.cFileName))
			    count++;
	    } while (FindNextFileA(h, &fd));

	    FindClose(h);

	    if (count == 0)
		    goto exit;

	    list = malloc(count * sizeof(*list));
	    if (!list)
		    goto exit;

	    h = FindFirstFileA(s, &fd);
	    if (h == INVALID_HANDLE_VALUE)
		    goto exit;

	    do {
		    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			    continue;
		    if (is_valid_module_file(fd.cFileName) && i < count)
			    strncpy(list[i++], fd.cFileName, STK_PATH_MAX - 1);
	    } while (FindNextFileA(h, &fd));

	    FindClose(h);
	    goto exit;

    create_and_exit:
	    platform_mkdir(dir_path);
    exit:
	    *out_count = i;
	    return list;
#else
	    DIR *d;
	    struct dirent *e;
	    struct stat st;
	    char f[STK_PATH_MAX_OS];

	    d = opendir(dir_path);
	    if (!d)
		    goto create_and_exit;

    count_loop:
	    e = readdir(d);
	    if (!e)
		    goto count_done;

	    sprintf(f, "%s/%s", dir_path, e->d_name);
	    if (!is_valid_module_file(e->d_name))
		    goto count_loop;

	    if (stat(f, &st) != 0 || !S_ISREG(st.st_mode))
		    goto count_loop;

	    count++;
	    goto count_loop;

    count_done:
	    if (count == 0)
		    goto close_and_exit;

	    rewinddir(d);
	    list = malloc(count * sizeof(*list));
	    if (!list)
		    goto close_and_exit;

    fill_loop:
	    e = readdir(d);
	    if (!e || i >= count)
		    goto close_and_exit;

	    sprintf(f, "%s/%s", dir_path, e->d_name);
	    if (!is_valid_module_file(e->d_name))
		    goto fill_loop;

	    if (stat(f, &st) != 0 || !S_ISREG(st.st_mode))
		    goto fill_loop;

	    strncpy(list[i++], e->d_name, STK_PATH_MAX - 1);
	    goto fill_loop;

    create_and_exit:
	    platform_mkdir(dir_path);
	    *out_count = 0;
	    return NULL;

    close_and_exit:
	    closedir(d);
	    *out_count = i;
	    return list;
#endif
    }

#if !defined(__linux__) && !defined(_WIN32)
static void update_watches(platform_watch_context_t *ctx)
{
	struct kevent ev;
	DIR *d;
	struct dirent *e;
	char f[STK_PATH_MAX_OS];
	size_t i, count = 0;
	int fd;
	int *new_fds = NULL;

	for (i = 0; i < ctx->watch.k.file_fd_count; i++)
		close(ctx->watch.k.file_fds[i]);

	free(ctx->watch.k.file_fds);
	ctx->watch.k.file_fds = NULL;
	ctx->watch.k.file_fd_count = 0;

	EV_SET(&ev, ctx->watch.k.dir_fd, EVFILT_VNODE,
	       EV_ADD | EV_ENABLE | EV_CLEAR,
	       NOTE_WRITE | NOTE_DELETE | NOTE_RENAME, 0, NULL);
	kevent(ctx->watch.k.kq, &ev, 1, NULL, 0, NULL);
	d = opendir(ctx->path);
	if (!d)
		return;

count_loop:
	e = readdir(d);
	if (!e)
		goto count_done;

	if (is_valid_module_file(e->d_name))
		count++;

	goto count_loop;
count_done:
	if (count == 0)
		goto cleanup;

	new_fds = malloc(count * sizeof(int));
	if (!new_fds)
		goto cleanup;

	rewinddir(d);
	i = 0;
scan_loop:
	e = readdir(d);
	if (!e || i >= count)
		goto scan_done;

	if (!is_valid_module_file(e->d_name))
		goto scan_loop;

	sprintf(f, "%s/%s", ctx->path, e->d_name);
	fd = open(f, O_RDONLY);
	if (fd < 0)
		goto scan_loop;

	new_fds[i++] = fd;
	EV_SET(&ev, fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
	       NOTE_WRITE | NOTE_ATTRIB, 0, NULL);
	kevent(ctx->watch.k.kq, &ev, 1, NULL, 0, NULL);
	goto scan_loop;
scan_done:
	ctx->watch.k.file_fds = new_fds;
	ctx->watch.k.file_fd_count = i;
cleanup:
	closedir(d);
}
#endif

void *platform_directory_watch_start(const char *path)
{
#ifdef __linux__
	int fd = inotify_init1(IN_NONBLOCK);
	if (fd < 0)
		return NULL;

	inotify_add_watch(
	    fd, path, IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM);
	return (void *)(long)fd;
#else
#ifdef _WIN32
	WIN32_FIND_DATAA fd;
	HANDLE h;
	char s[STK_PATH_MAX_OS];
	size_t count = 0, i = 0;
#else
	DIR *d;
	struct dirent *e;
	struct stat st;
	char f[STK_PATH_MAX_OS];
	size_t count = 0, i = 0;
#endif
	platform_watch_context_t *ctx =
	    calloc(1, sizeof(platform_watch_context_t));
	if (!ctx)
		return NULL;

	strncpy(ctx->path, path, STK_PATH_MAX - 1);
#ifdef _WIN32
	ctx->watch.change_handle =
	    CreateFileA(path, FILE_LIST_DIRECTORY,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if (ctx->watch.change_handle == INVALID_HANDLE_VALUE)
		goto done;

	sprintf(s, "%s\\*", path);
	h = FindFirstFileA(s, &fd);
	if (h == INVALID_HANDLE_VALUE)
		goto done;

	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
		    is_valid_module_file(fd.cFileName))
			count++;
	} while (FindNextFileA(h, &fd));

	FindClose(h);
	if (count == 0)
		goto done;

	ctx->snaps = malloc(count * sizeof(platform_snapshot_t));
	if (!ctx->snaps)
		goto done;

	h = FindFirstFileA(s, &fd);
	if (h == INVALID_HANDLE_VALUE)
		goto done;

	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
		    is_valid_module_file(fd.cFileName) && i < count) {
			strncpy(ctx->snaps[i].filename, fd.cFileName,
				STK_PATH_MAX - 1);
			ctx->snaps[i].mtime = fd.ftLastWriteTime;
			i++;
		}
	} while (FindNextFileA(h, &fd));

	FindClose(h);
	ctx->count = i;

#else

	ctx->watch.k.kq = kqueue();
	ctx->watch.k.dir_fd = open(path, O_RDONLY);
	d = opendir(path);
	if (!d)
		goto bsd_setup;

bsd_count_loop:
	e = readdir(d);
	if (!e)
		goto bsd_count_done;

	sprintf(f, "%s/%s", path, e->d_name);
	if (!is_valid_module_file(e->d_name))
		goto bsd_count_loop;

	if (stat(f, &st) != 0 || !S_ISREG(st.st_mode))
		goto bsd_count_loop;

	count++;
	goto bsd_count_loop;

bsd_count_done:
	if (count == 0)
		goto bsd_setup;

	ctx->snaps = malloc(count * sizeof(platform_snapshot_t));
	if (!ctx->snaps)
		goto bsd_setup;

	rewinddir(d);
bsd_scan_loop:
	e = readdir(d);
	if (!e || i >= count)
		goto bsd_scan_done;

	sprintf(f, "%s/%s", path, e->d_name);
	if (!is_valid_module_file(e->d_name))
		goto bsd_scan_loop;

	if (stat(f, &st) != 0 || !S_ISREG(st.st_mode))
		goto bsd_scan_loop;

	strncpy(ctx->snaps[i].filename, e->d_name, STK_PATH_MAX - 1);
	ctx->snaps[i].mtime = st.st_mtime;
	i++;
	goto bsd_scan_loop;

bsd_scan_done:
	closedir(d);
	ctx->count = i;
bsd_setup:
	update_watches(ctx);
#endif

#ifdef _WIN32
done:
#endif
	return ctx;
#endif
}

void platform_directory_watch_stop(void *handle)
{
#if defined(__linux__)
	if (handle)
		close((int)(long)handle);
#else
#ifndef _WIN32
	size_t i;
#endif
	platform_watch_context_t *ctx = (platform_watch_context_t *)handle;
	if (!ctx)
		return;
#ifdef _WIN32
	CloseHandle(ctx->watch.change_handle);
#else
	for (i = 0; i < ctx->watch.k.file_fd_count; i++)
		close(ctx->watch.k.file_fds[i]);
	free(ctx->watch.k.file_fds);
	close(ctx->watch.k.kq);
	close(ctx->watch.k.dir_fd);
#endif
	free(ctx->snaps);
	free(ctx);
#endif
}

stk_module_event_t *platform_directory_watch_check(
    void *handle, char (**file_list)[STK_PATH_MAX], size_t *out_count,
    char (*loaded_ids)[STK_MOD_ID_BUFFER], const size_t loaded_count)
{
#if defined(__linux__)
	int fd = (int)(long)handle;
	char buf[STK_EVENT_BUFFER];
	ssize_t len;
	size_t idx = 0, count = 0;
	stk_module_event_t *evs;
	char *ptr, *end;
	struct inotify_event *e;
	int event_type;

	len = read(fd, buf, sizeof(buf));
	if (len <= 0) {
		*out_count = 0;
		return NULL;
	}

	ptr = buf;
	end = buf + len;
	while (ptr < end) {
		e = (struct inotify_event *)ptr;
		if (e->len && is_valid_module_file(e->name))
			count++;
		ptr += sizeof(struct inotify_event) + e->len;
	}

	if (count == 0) {
		*out_count = 0;
		return NULL;
	}

	evs = malloc(count * sizeof(stk_module_event_t));
	if (!evs) {
		*out_count = 0;
		return NULL;
	}

	*file_list = malloc(count * sizeof(**file_list));
	if (!*file_list) {
		free(evs);
		*out_count = 0;
		return NULL;
	}

	ptr = buf;
	while (ptr < end && idx < count) {
		e = (struct inotify_event *)ptr;
		if (e->len && is_valid_module_file(e->name)) {
			if (e->mask & (IN_DELETE | IN_MOVED_FROM)) {
				char *ptr2 =
				    ptr + sizeof(struct inotify_event) + e->len;
				int has_create = 0;

				while (ptr2 < end) {
					struct inotify_event *e2 =
					    (struct inotify_event *)ptr2;
					if (e2->len &&
					    strcmp(e->name, e2->name) == 0 &&
					    (e2->mask &
					     (IN_CLOSE_WRITE | IN_MOVED_TO))) {
						has_create = 1;
						break;
					}
					ptr2 += sizeof(struct inotify_event) +
						e2->len;
				}

				if (has_create) {
					ptr += sizeof(struct inotify_event) +
					       e->len;
					continue;
				}
			}

			strncpy((*file_list)[idx], e->name, STK_PATH_MAX - 1);

			event_type = STK_MOD_UNLOAD;
			if (e->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) {
				if (is_module_loaded(e->name, loaded_ids,
						     loaded_count) >= 0)
					event_type = STK_MOD_RELOAD;
				else
					event_type = STK_MOD_LOAD;
			}

			evs[idx++] = event_type;
		}

		ptr += sizeof(struct inotify_event) + e->len;
	}

	*out_count = idx;
	return evs;

#else
	platform_watch_context_t *ctx = (platform_watch_context_t *)handle;
	platform_snapshot_t *new_snaps = NULL;
	size_t new_count = 0, i, j, ev_idx = 0;
	stk_module_event_t *evs = NULL;
	int found;
#ifdef _WIN32
	WIN32_FIND_DATAA fd;
	HANDLE h;
	char s[STK_PATH_MAX_OS];
	size_t count = 0;

	sprintf(s, "%s\\*", ctx->path);
	h = FindFirstFileA(s, &fd);
	if (h == INVALID_HANDLE_VALUE)
		goto build_diff;

	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
		    is_valid_module_file(fd.cFileName))
			count++;
	} while (FindNextFileA(h, &fd));

	FindClose(h);
	if (count == 0)
		goto build_diff;

	new_snaps = malloc(count * sizeof(platform_snapshot_t));
	if (!new_snaps)
		goto build_diff;

	h = FindFirstFileA(s, &fd);
	if (h == INVALID_HANDLE_VALUE) {
		free(new_snaps);
		new_snaps = NULL;
		goto build_diff;
	}

	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
		    is_valid_module_file(fd.cFileName) && new_count < count) {
			strncpy(new_snaps[new_count].filename, fd.cFileName,
				STK_PATH_MAX - 1);
			new_snaps[new_count].mtime = fd.ftLastWriteTime;
			new_count++;
		}
	} while (FindNextFileA(h, &fd));

	FindClose(h);
#else
	struct kevent kev;
	struct timespec ts = {0, 0};
	DIR *d;
	struct dirent *e;
	struct stat st;
	char f[STK_PATH_MAX_OS];
	size_t count = 0;

	if (kevent(ctx->watch.k.kq, NULL, 0, &kev, 1, &ts) <= 0)
		goto no_change;

	d = opendir(ctx->path);
	if (!d)
		goto bsd_update;

bsd_count_loop:
	e = readdir(d);
	if (!e)
		goto bsd_count_done;

	if (!is_valid_module_file(e->d_name))
		goto bsd_count_loop;

	sprintf(f, "%s/%s", ctx->path, e->d_name);
	if (stat(f, &st) != 0 || !S_ISREG(st.st_mode))
		goto bsd_count_loop;

	count++;
	goto bsd_count_loop;

bsd_count_done:
	if (count == 0) {
		closedir(d);
		goto bsd_update;
	}

	new_snaps = malloc(count * sizeof(platform_snapshot_t));
	if (!new_snaps) {
		closedir(d);
		goto bsd_update;
	}

	rewinddir(d);
bsd_snap_loop:
	e = readdir(d);
	if (!e || new_count >= count)
		goto bsd_snap_done;

	if (!is_valid_module_file(e->d_name))
		goto bsd_snap_loop;

	sprintf(f, "%s/%s", ctx->path, e->d_name);
	if (stat(f, &st) != 0 || !S_ISREG(st.st_mode))
		goto bsd_snap_loop;

	strncpy(new_snaps[new_count].filename, e->d_name, STK_PATH_MAX - 1);
	new_snaps[new_count].mtime = st.st_mtime;
	new_count++;
	goto bsd_snap_loop;
bsd_snap_done:
	closedir(d);
bsd_update:
	update_watches(ctx);
#endif

#ifdef _WIN32
build_diff:
#endif
	evs = malloc((ctx->count + new_count + 1) * sizeof(stk_module_event_t));
	*file_list = malloc((ctx->count + new_count + 1) * sizeof(**file_list));
	if (!evs || !*file_list)
		goto cleanup_error;

	for (i = 0; i < ctx->count; i++) {
		found = 0;
		for (j = 0; j < new_count; j++) {
			if (strcmp(ctx->snaps[i].filename,
				   new_snaps[j].filename) != 0)
				continue;

			found = 1;
#ifdef _WIN32
			if (CompareFileTime(&ctx->snaps[i].mtime,
					    &new_snaps[j].mtime) != 0) {
				if (is_file_ready(ctx->path,
						  new_snaps[j].filename)) {
					strncpy((*file_list)[ev_idx],
						new_snaps[j].filename,
						STK_PATH_MAX - 1);
					evs[ev_idx++] = STK_MOD_RELOAD;
				} else {
					new_snaps[j].mtime =
					    ctx->snaps[i].mtime;
				}
			}
#else
			if (ctx->snaps[i].mtime != new_snaps[j].mtime) {
				strncpy((*file_list)[ev_idx],
					new_snaps[j].filename,
					STK_PATH_MAX - 1);
				evs[ev_idx++] = STK_MOD_RELOAD;
			}
#endif
			break;
		}

		if (!found) {
			strncpy((*file_list)[ev_idx], ctx->snaps[i].filename,
				STK_PATH_MAX - 1);
			evs[ev_idx++] = STK_MOD_UNLOAD;
		}
	}

	for (j = 0; j < new_count; j++) {
		found = 0;
		for (i = 0; i < ctx->count; i++) {
			if (strcmp(new_snaps[j].filename,
				   ctx->snaps[i].filename) == 0) {
				found = 1;
				break;
			}
		}

		if (!found) {
			strncpy((*file_list)[ev_idx], new_snaps[j].filename,
				STK_PATH_MAX - 1);
			evs[ev_idx++] = STK_MOD_LOAD;
		}
	}

	if (ev_idx == 0)
		goto cleanup_empty;

	free(ctx->snaps);
	ctx->snaps = new_snaps;
	ctx->count = new_count;
	*out_count = ev_idx;
	return evs;

cleanup_error:
	if (evs)
		free(evs);

	if (*file_list)
		free(*file_list);

cleanup_empty:
	free(new_snaps);

#ifndef _WIN32
no_change:
#endif
	*out_count = 0;
	return NULL;
#endif
}
