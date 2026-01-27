#include "stk.h"
#include "stk_log.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*stk_module_func)(void);

extern void **stk_handles;
extern stk_module_func *stk_inits;
extern stk_module_func *stk_shutdowns;
extern char (*stk_module_ids)[STK_MOD_ID_BUFFER];

extern size_t module_count;

uint8_t stk_initialized = 0;

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

char *extract_module_id(const char *path, char *out_id);
int is_mod_loaded(const char *module_id);

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

	stk_initialized = 1;
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
	stk_initialized = 0;
	stk_log(stdout, "[stk] stk shutdown");
}

size_t stk_poll(void)
{
	char (*file_list)[STK_PATH_MAX] = NULL;
	stk_module_event_t *events = NULL;
	size_t i, file_count = 0, reload_count = 0, load_count = 0,
		  unload_count = 0, reload_index = 0, load_index = 0,
		  unload_index = 0;
	int *reloaded_mods, *unloaded_mods, *loaded_mods;
	events = platform_directory_watch_check(watch_handle, &file_list,
						&file_count, stk_module_ids,
						module_count);
	if (!events)
		goto finish_stk_poll;

	for (i = 0; i < file_count; ++i) {
		switch (events[i]) {
		case STK_MOD_RELOAD:
			++reload_count;
			break;
		case STK_MOD_LOAD:
			++load_count;
			break;
		case STK_MOD_UNLOAD:
			++unload_count;
			break;
		}
	}

	reloaded_mods = malloc(reload_count * sizeof(int));
	unloaded_mods = malloc(unload_count * sizeof(int));
	loaded_mods = malloc(load_count * sizeof(int));

	for (i = 0; i < file_count; ++i) {
		char mod_id[STK_MOD_ID_BUFFER];
		extract_module_id(file_list[i], mod_id);

		switch (events[i]) {
		case STK_MOD_RELOAD:
			reloaded_mods[reload_index++] = is_mod_loaded(mod_id);
			stk_log(stdout, "STK_MOD_RELOAD %s %ld", mod_id,
				reload_index - 1);
			break;

		case STK_MOD_LOAD:
			loaded_mods[load_index++] = i;
			stk_log(stdout, "STK_MOD_LOAD %s %ld", mod_id, i);
			break;
		case STK_MOD_UNLOAD:
			unloaded_mods[unload_index++] = is_mod_loaded(mod_id);
			stk_log(stdout, "STK_MOD_UNLOAD %s %ld", mod_id,
				unload_index - 1);
			break;
		}
	}

	free(reloaded_mods);
	free(unloaded_mods);
	free(loaded_mods);

	free(events);
	free(file_list);

finish_stk_poll:
	return file_count;
}

void stk_set_mod_dir(const char *path)
{
	if (!path || stk_initialized)
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
	if (!name || stk_initialized)
		return;

	strncpy(stk_tmp_name, name, STK_MOD_ID_BUFFER - 1);
	stk_tmp_name[STK_MOD_ID_BUFFER - 1] = '\0';

	strncpy(stk_tmp_dir, stk_mod_dir, STK_PATH_MAX_OS - 1);
	stk_tmp_dir[STK_PATH_MAX_OS - 1] = '\0';

	strncat(stk_tmp_dir, "/", STK_PATH_MAX_OS - strlen(stk_tmp_dir) - 1);
	strncat(stk_tmp_dir, stk_tmp_name,
		STK_PATH_MAX_OS - strlen(stk_tmp_dir) - 1);
}
