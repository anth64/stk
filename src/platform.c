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

	    while (FindNextFileA(h, &fd) && i < count) {
		    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			    continue;
		    if (is_valid_module_file(fd.cFileName))
			    strncpy(list[i++], fd.cFileName, STK_PATH_MAX - 1);
	    }
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
	size_t i;
	int fd;
	int *tmp_fds;

	for (i = 0; i < ctx->watch.k.file_fd_count; i++) {
		close(ctx->watch.k.file_fds[i]);
	}
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

scan_loop:
	e = readdir(d);
	if (!e)
		goto scan_done;

	if (!is_valid_module_file(e->d_name))
		goto scan_loop;

	sprintf(f, "%s/%s", ctx->path, e->d_name);
	fd = open(f, O_RDONLY);
	if (fd < 0)
		goto scan_loop;

	tmp_fds = realloc(ctx->watch.k.file_fds,
			  sizeof(int) * (ctx->watch.k.file_fd_count + 1));
	if (!tmp_fds) {
		close(fd);
		goto scan_loop;
	}

	ctx->watch.k.file_fds = tmp_fds;
	ctx->watch.k.file_fds[ctx->watch.k.file_fd_count++] = fd;

	EV_SET(&ev, fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
	       NOTE_WRITE | NOTE_ATTRIB, 0, NULL);
	kevent(ctx->watch.k.kq, &ev, 1, NULL, 0, NULL);

	goto scan_loop;

scan_done:
	closedir(d);
}
#endif

void *platform_directory_watch_start(const char *path)
{
#if defined(__linux__)
	int fd = inotify_init1(IN_NONBLOCK);
	if (fd < 0)
		return NULL;
	inotify_add_watch(
	    fd, path, IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM);
	return (void *)(long)fd;
#else
	platform_watch_context_t *ctx =
	    calloc(1, sizeof(platform_watch_context_t));
	if (!ctx)
		return NULL;
	strncpy(ctx->path, path, STK_PATH_MAX - 1);

#ifdef _WIN32
	{
		WIN32_FIND_DATAA fd;
		HANDLE h;
		char s[STK_PATH_MAX_OS];
		void *tmp_snaps;

		ctx->watch.change_handle = FindFirstChangeNotificationA(
		    path, FALSE,
		    FILE_NOTIFY_CHANGE_FILE_NAME |
			FILE_NOTIFY_CHANGE_LAST_WRITE);

		sprintf(s, "%s\\*", path);
		h = FindFirstFileA(s, &fd);
		if (h == INVALID_HANDLE_VALUE)
			goto done;

	win_scan_loop:
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
		    is_valid_module_file(fd.cFileName)) {
			tmp_snaps = realloc(ctx->snaps,
					    (ctx->count + 1) *
						sizeof(platform_snapshot_t));
			if (tmp_snaps) {
				ctx->snaps = tmp_snaps;
				strncpy(ctx->snaps[ctx->count].filename,
					fd.cFileName, STK_PATH_MAX - 1);
				ctx->snaps[ctx->count++].mtime =
				    fd.ftLastWriteTime;
			}
		}
		if (FindNextFileA(h, &fd))
			goto win_scan_loop;

		FindClose(h);
	}
#else
	{
		DIR *d;
		struct dirent *e;
		struct stat st;
		char f[STK_PATH_MAX_OS];
		void *tmp_snaps;

		ctx->watch.k.kq = kqueue();
		ctx->watch.k.dir_fd = open(path, O_RDONLY);

		d = opendir(path);
		if (!d)
			goto bsd_setup;

	bsd_scan_loop:
		e = readdir(d);
		if (!e)
			goto bsd_scan_done;

		sprintf(f, "%s/%s", path, e->d_name);
		if (!is_valid_module_file(e->d_name))
			goto bsd_scan_loop;
		if (stat(f, &st) != 0 || !S_ISREG(st.st_mode))
			goto bsd_scan_loop;

		tmp_snaps = realloc(
		    ctx->snaps, (ctx->count + 1) * sizeof(platform_snapshot_t));
		if (!tmp_snaps)
			goto bsd_scan_loop;

		ctx->snaps = tmp_snaps;
		strncpy(ctx->snaps[ctx->count].filename, e->d_name,
			STK_PATH_MAX - 1);
		ctx->snaps[ctx->count++].mtime = st.st_mtime;

		goto bsd_scan_loop;

	bsd_scan_done:
		closedir(d);

	bsd_setup:
		update_watches(ctx);
	}
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
	FindCloseChangeNotification(ctx->watch.change_handle);
#else
	for (i = 0; i < ctx->watch.k.file_fd_count; i++) {
		close(ctx->watch.k.file_fds[i]);
	}
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
	size_t idx = 0;
	stk_module_event_t *evs;
	char *ptr;
	struct inotify_event *e;
	int event_type;

	len = read(fd, buf, sizeof(buf));
	if (len <= 0) {
		*out_count = 0;
		return NULL;
	}

	evs = malloc(8 * sizeof(stk_module_event_t));
	*file_list = malloc(8 * sizeof(**file_list));
	ptr = buf;

process_inotify_loop:
	if (ptr >= buf + len)
		goto linux_done;

	e = (struct inotify_event *)ptr;
	if (!e->len || !is_valid_module_file(e->name))
		goto next_event;

	strncpy((*file_list)[idx], e->name, STK_PATH_MAX - 1);

	event_type = STK_MOD_UNLOAD;
	if (e->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) {
		if (is_module_loaded(e->name, loaded_ids, loaded_count) >= 0)
			event_type = STK_MOD_RELOAD;
		else
			event_type = STK_MOD_LOAD;
	}
	evs[idx++] = event_type;

next_event:
	ptr += sizeof(struct inotify_event) + e->len;
	goto process_inotify_loop;

linux_done:
	*out_count = idx;
	return evs;

#else
	platform_watch_context_t *ctx = (platform_watch_context_t *)handle;
	platform_snapshot_t *new_snaps = NULL;
	size_t new_count = 0, i, j, ev_idx = 0;
	stk_module_event_t *evs = NULL;
	void *tmp_ptr;
	int found;

#ifdef _WIN32
	WIN32_FIND_DATAA fd;
	HANDLE h;
	char s[STK_PATH_MAX_OS];

	if (WaitForSingleObject(ctx->watch.change_handle, 0) != WAIT_OBJECT_0)
		goto no_change;

	FindNextChangeNotification(ctx->watch.change_handle);

	sprintf(s, "%s\\*", ctx->path);
	h = FindFirstFileA(s, &fd);
	if (h == INVALID_HANDLE_VALUE)
		goto build_diff;

win_snap_loop:
	if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
	    is_valid_module_file(fd.cFileName)) {
		tmp_ptr = realloc(new_snaps, (new_count + 1) *
						 sizeof(platform_snapshot_t));
		if (!tmp_ptr)
			goto win_next;
		new_snaps = tmp_ptr;
		strncpy(new_snaps[new_count].filename, fd.cFileName,
			STK_PATH_MAX - 1);
		new_snaps[new_count++].mtime = fd.ftLastWriteTime;
	}
win_next:
	if (FindNextFileA(h, &fd))
		goto win_snap_loop;
	FindClose(h);
	goto build_diff;

#else
	struct kevent kev;
	struct timespec ts = {0, 0};
	DIR *d;
	struct dirent *e;
	struct stat st;
	char f[STK_PATH_MAX_OS];

	if (kevent(ctx->watch.k.kq, NULL, 0, &kev, 1, &ts) <= 0)
		goto no_change;

	d = opendir(ctx->path);
	if (!d)
		goto bsd_update;

bsd_snap_loop:
	e = readdir(d);
	if (!e)
		goto bsd_snap_done;

	if (!is_valid_module_file(e->d_name))
		goto bsd_snap_loop;

	sprintf(f, "%s/%s", ctx->path, e->d_name);
	if (stat(f, &st) != 0 || !S_ISREG(st.st_mode))
		goto bsd_snap_loop;

	tmp_ptr =
	    realloc(new_snaps, (new_count + 1) * sizeof(platform_snapshot_t));
	if (!tmp_ptr)
		goto bsd_snap_loop;

	new_snaps = tmp_ptr;
	strncpy(new_snaps[new_count].filename, e->d_name, STK_PATH_MAX - 1);
	new_snaps[new_count++].mtime = st.st_mtime;

	goto bsd_snap_loop;

bsd_snap_done:
	closedir(d);

bsd_update:
	update_watches(ctx);
	goto build_diff;
#endif

build_diff:
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
#else
			if (ctx->snaps[i].mtime != new_snaps[j].mtime) {
#endif
				strncpy((*file_list)[ev_idx],
					new_snaps[j].filename,
					STK_PATH_MAX - 1);
				evs[ev_idx++] = STK_MOD_RELOAD;
			}
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

no_change:
	*out_count = 0;
	return NULL;
#endif
}
