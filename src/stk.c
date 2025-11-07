#include "stk.h"
#include "stk_log.h"
#include <stdio.h>
#include <string.h>

extern char **stk_module_ids;
extern size_t module_count;

static char stk_mod_dir[MOD_DIR_BUFFER_SIZE];
static void *watch_handle = NULL;

char **platform_directory_init_scan(const char *path, size_t *out_count);
void *platform_directory_watch_start(const char *path);
void platform_directory_watch_stop(void *handle);
size_t stk_module_count(void);
int stk_module_load(const char *path);
void stk_module_unload(size_t index);
void stk_module_unload_all(void);
int stk_module_init_memory(size_t capacity);
stk_module_event_t *platform_directory_watch_check(void *handle,
						   char ***file_list,
						   size_t *out_count);

int stk_init(const char *mod_dir)
{
	char **files;
	size_t file_count, i;
	char full_path[PATH_BUFFER_SIZE];

	if (mod_dir) {
		strncpy(stk_mod_dir, mod_dir, MOD_DIR_BUFFER_SIZE - 1);
		stk_mod_dir[MOD_DIR_BUFFER_SIZE - 1] = '\0';
	} else {
		strcpy(stk_mod_dir, "mods");
	}

	files = platform_directory_init_scan(stk_mod_dir, &file_count);

	if (file_count > 0 && stk_module_init_memory(file_count) != 0)
		return -1;

	if (!files)
		goto scanned;

	for (i = 0; i < file_count; ++i) {
		sprintf(full_path, "%s/%s", stk_mod_dir, files[i]);
		stk_module_load(full_path);
		free(files[i]);
	}

	free(files);

scanned:
	watch_handle = platform_directory_watch_start(stk_mod_dir);
	stk_log(stdout, "[stk] stk v%s initialized! Loaded %zu mod%s from %s/",
		STK_VERSION_STRING, module_count, module_count > 1 ? "s" : "",
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

	stk_log(stdout, "[stk] stk shutdown");
}

size_t stk_poll(void)
{
	char **file_list = NULL;
	stk_module_event_t *events = NULL;
	size_t file_count;
	size_t i;

	events = platform_directory_watch_check(watch_handle, &file_list,
						&file_count);
	if (!events)
		return 0;

	for (i = 0; i < file_count; ++i) {
		char full_path[PATH_BUFFER_SIZE];
		char *module_id;
		int existing_index = -1;
		size_t j = 0;

		module_id = file_list[i];
		sprintf(full_path, "%s/%s", stk_mod_dir, module_id);

		for (j = 0; j < module_count; ++j) {
			if (strcmp(module_id, stk_module_ids[j]) != 0)
				continue;

			existing_index = j;
			break;
		}

		switch (events[i]) {
		case STK_MOD_LOAD:
			break;
		case STK_MOD_UNLOAD:
			break;
		}
	}

	for (i = 0; i < file_count; i++)
		free(file_list[i]);

	free(file_list);
	free(events);

	return file_count;
}
