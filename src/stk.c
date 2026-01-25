#include "stk.h"
#include "stk_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*stk_module_func)(void);

extern void **stk_handles;
extern stk_module_func *stk_inits;
extern stk_module_func *stk_shutdowns;
extern char (*stk_module_ids)[STK_MOD_ID_BUFFER];

extern size_t module_count;

static char stk_mod_dir[STK_PATH_MAX_OS] = "mods";
static char stk_tmp_name[STK_MOD_ID_BUFFER] = ".tmp";
static char stk_tmp_dir[STK_PATH_MAX_OS] = "mods/.tmp";
static void *watch_handle = NULL;

char (*platform_directory_init_scan(const char *path,
				    size_t *out_count))[STK_PATH_MAX];
void *platform_directory_watch_start(const char *path);
void platform_directory_watch_stop(void *handle);
stk_module_event_t *platform_directory_watch_check(
    void *handle, char (**file_list)[STK_PATH_MAX], size_t *out_count,
    char (*loaded_module_ids)[STK_MOD_ID_BUFFER], const size_t loaded_count);
int platform_mkdir(const char *path);
int platform_copy_file(const char *from, const char *to);
int platform_remove_dir(const char *path);

char *extract_module_id(const char *path);
size_t stk_module_count(void);
int stk_module_load(const char *path, int index);
int stk_module_load_init(const char *path, int index);
void stk_module_unload(size_t index);
void stk_module_unload_all(void);
int stk_module_init_memory(size_t capacity);

int stk_init(void)
{
	char (*files)[STK_PATH_MAX] = NULL;
	size_t file_count, i;
	char full_path[STK_PATH_MAX_OS + STK_PATH_MAX];
	char tmp_path[STK_PATH_MAX_OS + STK_PATH_MAX];

	platform_mkdir(stk_tmp_dir);
	files = platform_directory_init_scan(stk_mod_dir, &file_count);

	if (file_count > 0 && stk_module_init_memory(file_count) != 0)
		return -1;

	if (!files)
		goto scanned;

	for (i = 0; i < file_count; ++i) {
		sprintf(full_path, "%s/%s", stk_mod_dir, files[i]);
		sprintf(tmp_path, "%s/%s", stk_tmp_dir, files[i]);
		if (platform_copy_file(full_path, tmp_path) == 0)
			stk_module_load_init(tmp_path, i);
	}

	free(files);

scanned:
	watch_handle = platform_directory_watch_start(stk_mod_dir);
	stk_log(stdout, "[stk] stk v%s initialized! Loaded %lu mod%s from %s/",
		STK_VERSION_STRING, module_count, module_count != 1 ? "s" : "",
		stk_mod_dir);
	return 0;
}

void stk_shutdown(void)
{
	if (watch_handle) {
		platform_directory_watch_stop(watch_handle);
		watch_handle = NULL;
	}

	stk_module_unload_all();
	platform_remove_dir(stk_tmp_dir);
	stk_log(stdout, "[stk] stk shutdown");
}

size_t stk_poll(void)
{
	char (*file_list)[STK_PATH_MAX] = NULL;
	stk_module_event_t *events = NULL;
	size_t file_count = 0, i;

	events = platform_directory_watch_check(watch_handle, &file_list,
						&file_count, stk_module_ids,
						module_count);
	if (!events)
		return 0;

	for (i = 0; i < file_count; ++i) {
		switch (events[i]) {
		case STK_MOD_RELOAD:
			/* TODO: Implement reload */
			stk_log(stdout, "STK_MOD_RELOAD");
			break;
		case STK_MOD_LOAD:
			/* TODO: Implement load */
			stk_log(stdout, "STK_MOD_LOAD");
			break;
		case STK_MOD_UNLOAD:
			/* TODO: Implement unload */
			stk_log(stdout, "STK_MOD_UNLOAD");
			break;
		}
	}

	free(events);
	free(file_list);

	return file_count;
}

void stk_set_mod_dir(const char *path)
{
	if (!path)
		return;

	strncpy(stk_mod_dir, path, STK_PATH_MAX_OS - 1);
	stk_mod_dir[STK_PATH_MAX_OS - 1] = '\0';

	strncpy(stk_tmp_dir, stk_mod_dir, STK_PATH_MAX_OS - 1);
	stk_tmp_dir[STK_PATH_MAX_OS - 1] = '\0';

	strncat(stk_tmp_dir, "/", STK_PATH_MAX_OS - strlen(stk_tmp_dir) - 1);

	strncat(stk_tmp_dir, stk_tmp_name,
		STK_PATH_MAX_OS - strlen(stk_tmp_dir) - 1);
}

void stk_set_tmp_dir_name(const char *name)
{
	if (!name)
		return;

	strncpy(stk_tmp_name, name, STK_MOD_ID_BUFFER - 1);
	stk_tmp_name[STK_MOD_ID_BUFFER - 1] = '\0';

	strncpy(stk_tmp_dir, stk_mod_dir, STK_PATH_MAX_OS - 1);
	stk_tmp_dir[STK_PATH_MAX_OS - 1] = '\0';

	strncat(stk_tmp_dir, "/", STK_PATH_MAX_OS - strlen(stk_tmp_dir) - 1);
	strncat(stk_tmp_dir, stk_tmp_name,
		STK_PATH_MAX_OS - strlen(stk_tmp_dir) - 1);
}
