#include "stk.h"
#include "platform.h"
#include "stk_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*stk_init_mod_func)(void);
typedef void (*stk_shutdown_mod_func)(void);

extern void **stk_handles;
extern stk_init_mod_func *stk_inits;
extern stk_shutdown_mod_func *stk_shutdowns;
extern char (*stk_module_ids)[STK_MOD_ID_BUFFER];

extern size_t module_count;

unsigned char stk_flags = STK_FLAG_LOGGING_ENABLED;

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
unsigned char platform_mkdir(const char *path);
unsigned char platform_copy_file(const char *from, const char *to);
unsigned char platform_remove_dir(const char *path);

void extract_module_id(const char *path, char *out_id);
int is_mod_loaded(const char *module_id);

size_t stk_module_count(void);
unsigned char stk_module_load(const char *path, int index);
unsigned char stk_module_load_init(const char *path, int index);
unsigned char stk_module_init_memory(size_t capacity);
unsigned char stk_module_realloc_memory(size_t new_capacity);
void stk_module_unload(size_t index);
void stk_module_unload_all(void);

static void build_path(char *dest, size_t dest_size, const char *dir,
		       const char *file)
{
	dest[0] = '\0';
	strncat(dest, dir, dest_size - 1);
	strncat(dest, STK_PATH_SEP_STR, dest_size - strlen(dest) - 1);
	strncat(dest, file, dest_size - strlen(dest) - 1);
}

static const char *stk_error_string(int error_code)
{
	switch (error_code) {
	case STK_MOD_LIBRARY_LOAD_ERROR:
		return "library load error";
	case STK_MOD_SYMBOL_NOT_FOUND_ERROR:
		return "symbol not found";
	case STK_MOD_INIT_FAILURE:
		return "init failure";
	case STK_MOD_REALLOC_FAILURE:
		return "memory reallocation failed";
	default:
		return "unknown error";
	}
}

unsigned char stk_init(void)
{
	char (*files)[STK_PATH_MAX] = NULL;
	size_t file_count, i, successful_loads = 0;
	char full_path[STK_PATH_MAX_OS];
	char tmp_path[STK_PATH_MAX_OS];
	int load_result;

	if (platform_mkdir(stk_tmp_dir) != STK_PLATFORM_OPERATION_SUCCESS) {
		char (*test_scan)[STK_PATH_MAX];
		size_t test_count;

		test_scan =
		    platform_directory_init_scan(stk_tmp_dir, &test_count);
		if (test_scan)
			free(test_scan);
		if (!test_scan && test_count == 0) {
			stk_log(STK_LOG_ERROR,
				"FATAL: Cannot create temp directory: %s",
				stk_tmp_dir);
			return STK_INIT_TMPDIR_ERROR;
		}
	}

	files = platform_directory_init_scan(stk_mod_dir, &file_count);

	if (file_count > 0 && stk_module_init_memory(file_count) != 0) {
		stk_log(STK_LOG_ERROR, "FATAL: Memory allocation failed");
		return STK_INIT_MEMORY_ERROR;
	}

	if (!files)
		goto scanned;

	for (i = 0; i < file_count; ++i) {
		build_path(full_path, sizeof(full_path), stk_mod_dir, files[i]);
		build_path(tmp_path, sizeof(tmp_path), stk_tmp_dir, files[i]);

		if (platform_copy_file(full_path, tmp_path) !=
		    STK_PLATFORM_OPERATION_SUCCESS) {
			stk_log(STK_LOG_ERROR,
				"Failed to copy %s to temp directory",
				files[i]);
			continue;
		}

		load_result = stk_module_load_init(tmp_path, successful_loads);

		if (load_result != STK_MOD_INIT_SUCCESS) {
			stk_log(STK_LOG_ERROR, "Failed to load module %s: %s",
				files[i], stk_error_string(load_result));
		} else {
			successful_loads++;
		}
	}

	if (successful_loads < file_count) {
		stk_module_realloc_memory(successful_loads);
	}

	free(files);

scanned:
	watch_handle = platform_directory_watch_start(stk_mod_dir);
	if (!watch_handle) {
		stk_log(STK_LOG_ERROR,
			"FATAL: Cannot start directory watch on %s",
			stk_mod_dir);
		stk_module_unload_all();
		return STK_INIT_WATCH_ERROR;
	}

	stk_log(STK_LOG_INFO, "stk v%s initialized! Loaded %lu mod%s from %s/",
		STK_VERSION_STRING, module_count, module_count != 1 ? "s" : "",
		stk_mod_dir);

	stk_flags |= STK_FLAG_INITIALIZED;
	return STK_INIT_SUCCESS;
}

void stk_shutdown(void)
{
	if (watch_handle) {
		platform_directory_watch_stop(watch_handle);
		watch_handle = NULL;
	}

	stk_module_unload_all();

	if (platform_remove_dir(stk_tmp_dir) !=
	    STK_PLATFORM_OPERATION_SUCCESS) {
		stk_log(STK_LOG_WARN,
			"Warning: failed to remove temp directory %s",
			stk_tmp_dir);
	}

	stk_flags &= ~STK_FLAG_INITIALIZED;
	stk_log(STK_LOG_INFO, "stk shutdown");
}

size_t stk_poll(void)
{
	char (*file_list)[STK_PATH_MAX] = NULL;
	stk_module_event_t *events = NULL;
	size_t i, file_count = 0, reload_count = 0, load_count = 0,
		  unload_count = 0;
	int *reloaded_mod_indices = NULL, *reloaded_mod_file_indices = NULL,
	    *unloaded_mod_indices = NULL, *loaded_mod_indices = NULL;
	size_t remaining_loads, new_capacity, holes_to_fill;
	size_t write_pos, read_pos;
	char full_path[STK_PATH_MAX_OS], tmp_path[STK_PATH_MAX_OS];
	char mod_id[STK_MOD_ID_BUFFER];
	int load_result;
	size_t successful_appends = 0;

	events = platform_directory_watch_check(watch_handle, &file_list,
						&file_count, stk_module_ids,
						module_count);
	if (!events)
		goto finish_poll;

	for (i = 0; i < file_count; ++i) {
		switch (events[i]) {
		case STK_MOD_LOAD:
			++load_count;
			break;
		case STK_MOD_RELOAD:
			++reload_count;
			break;
		case STK_MOD_UNLOAD:
			++unload_count;
			break;
		}
	}

	reloaded_mod_indices = malloc(reload_count * sizeof(int));
	reloaded_mod_file_indices = malloc(reload_count * sizeof(int));
	unloaded_mod_indices = malloc(unload_count * sizeof(int));
	loaded_mod_indices = malloc(load_count * sizeof(int));

	reload_count = 0;
	unload_count = 0;
	load_count = 0;

	for (i = 0; i < file_count; ++i) {
		int mod_index;
		extract_module_id(file_list[i], mod_id);
		switch (events[i]) {
		case STK_MOD_LOAD:
			loaded_mod_indices[load_count++] = i;
			break;
		case STK_MOD_RELOAD:
			mod_index = is_mod_loaded(mod_id);
			if (mod_index >= 0) {
				reloaded_mod_file_indices[reload_count] = i;
				reloaded_mod_indices[reload_count] = mod_index;
				reload_count++;
			}
			break;
		case STK_MOD_UNLOAD:
			mod_index = is_mod_loaded(mod_id);
			if (mod_index >= 0) {
				unloaded_mod_indices[unload_count] = mod_index;
				unload_count++;
			}
			break;
		}
	}

	if (load_count > unload_count)
		goto handle_grow;

	goto begin_operations;

handle_grow:
	remaining_loads = load_count - unload_count;
	new_capacity = module_count + remaining_loads;
	if (stk_module_realloc_memory(new_capacity) != STK_MOD_INIT_SUCCESS) {
		goto free_poll;
	}

begin_operations:
	for (i = 0; i < unload_count; ++i)
		stk_module_unload(unloaded_mod_indices[i]);

	for (i = 0; i < reload_count; ++i) {
		int file_index = reloaded_mod_file_indices[i];
		int mod_index = reloaded_mod_indices[i];

		build_path(full_path, sizeof(full_path), stk_mod_dir,
			   file_list[file_index]);
		build_path(tmp_path, sizeof(tmp_path), stk_tmp_dir,
			   file_list[file_index]);

		if (platform_copy_file(full_path, tmp_path) !=
		    STK_PLATFORM_OPERATION_SUCCESS) {
			stk_log(STK_LOG_ERROR, "Failed to copy %s for reload",
				file_list[file_index]);
			continue;
		}

		stk_module_unload(mod_index);

		load_result = stk_module_load(tmp_path, mod_index);
		if (load_result != STK_MOD_INIT_SUCCESS) {
			stk_log(STK_LOG_ERROR, "Failed to reload module %s: %s",
				file_list[file_index],
				stk_error_string(load_result));
		}
	}

	holes_to_fill = (load_count < unload_count) ? load_count : unload_count;
	for (i = 0; i < holes_to_fill; ++i) {
		int target_index = unloaded_mod_indices[i];
		int file_index = loaded_mod_indices[i];

		build_path(full_path, sizeof(full_path), stk_mod_dir,
			   file_list[file_index]);
		build_path(tmp_path, sizeof(tmp_path), stk_tmp_dir,
			   file_list[file_index]);

		if (platform_copy_file(full_path, tmp_path) !=
		    STK_PLATFORM_OPERATION_SUCCESS) {
			stk_log(STK_LOG_ERROR, "Failed to copy %s for loading",
				file_list[file_index]);
			continue;
		}

		load_result = stk_module_load(tmp_path, target_index);
		if (load_result != STK_MOD_INIT_SUCCESS) {
			stk_log(STK_LOG_ERROR, "Failed to load module %s: %s",
				file_list[file_index],
				stk_error_string(load_result));
		}
	}

	if (load_count > unload_count)
		goto append_modules;

	if (unload_count > load_count)
		goto trim_arrays;

	goto free_poll;

append_modules:
	for (; i < load_count; ++i) {
		int file_index = loaded_mod_indices[i];

		build_path(full_path, sizeof(full_path), stk_mod_dir,
			   file_list[file_index]);
		build_path(tmp_path, sizeof(tmp_path), stk_tmp_dir,
			   file_list[file_index]);

		if (platform_copy_file(full_path, tmp_path) !=
		    STK_PLATFORM_OPERATION_SUCCESS) {
			stk_log(STK_LOG_ERROR, "Failed to copy %s for loading",
				file_list[file_index]);
			continue;
		}

		load_result = stk_module_load(tmp_path, module_count +
							    successful_appends);
		if (load_result != STK_MOD_INIT_SUCCESS) {
			stk_log(STK_LOG_ERROR, "Failed to load module %s: %s",
				file_list[file_index],
				stk_error_string(load_result));
		} else {
			successful_appends++;
		}
	}

	module_count += successful_appends;

	if (successful_appends < (load_count - holes_to_fill)) {
		stk_module_realloc_memory(module_count);
	}

	goto free_poll;

trim_arrays:
	write_pos = unloaded_mod_indices[holes_to_fill];
	for (i = holes_to_fill + 1; i < unload_count; ++i) {
		if (unloaded_mod_indices[i] < write_pos)
			write_pos = unloaded_mod_indices[i];
	}

	for (read_pos = write_pos + 1; read_pos < module_count; ++read_pos) {
		if (stk_handles[read_pos] != NULL) {
			stk_handles[write_pos] = stk_handles[read_pos];
			stk_inits[write_pos] = stk_inits[read_pos];
			stk_shutdowns[write_pos] = stk_shutdowns[read_pos];
			memcpy(stk_module_ids[write_pos],
			       stk_module_ids[read_pos], STK_MOD_ID_BUFFER);
			++write_pos;
		}
	}

	module_count = write_pos;
	stk_module_realloc_memory(module_count);

free_poll:
	free(reloaded_mod_indices);
	free(reloaded_mod_file_indices);
	free(unloaded_mod_indices);
	free(loaded_mod_indices);
	free(events);
	free(file_list);

finish_poll:
	return file_count;
}

void stk_set_mod_dir(const char *path)
{
	if (!path || (stk_flags & STK_FLAG_INITIALIZED))
		return;

	strncpy(stk_mod_dir, path, STK_PATH_MAX_OS - 1);
	stk_mod_dir[STK_PATH_MAX_OS - 1] = '\0';

	strncpy(stk_tmp_dir, stk_mod_dir, STK_PATH_MAX_OS - 1);
	stk_tmp_dir[STK_PATH_MAX_OS - 1] = '\0';

	strncat(stk_tmp_dir, STK_PATH_SEP_STR,
		STK_PATH_MAX_OS - strlen(stk_tmp_dir) - 1);

	strncat(stk_tmp_dir, stk_tmp_name,
		STK_PATH_MAX_OS - strlen(stk_tmp_dir) - 1);
}

void stk_set_tmp_dir_name(const char *name)
{
	if (!name || (stk_flags & STK_FLAG_INITIALIZED))
		return;

	strncpy(stk_tmp_name, name, STK_MOD_ID_BUFFER - 1);
	stk_tmp_name[STK_MOD_ID_BUFFER - 1] = '\0';

	strncpy(stk_tmp_dir, stk_mod_dir, STK_PATH_MAX_OS - 1);
	stk_tmp_dir[STK_PATH_MAX_OS - 1] = '\0';

	strncat(stk_tmp_dir, "/", STK_PATH_MAX_OS - strlen(stk_tmp_dir) - 1);
	strncat(stk_tmp_dir, stk_tmp_name,
		STK_PATH_MAX_OS - strlen(stk_tmp_dir) - 1);
}

void stk_set_logging_enabled(unsigned char enabled)
{
	if (enabled)
		stk_flags |= STK_FLAG_LOGGING_ENABLED;
	else
		stk_flags &= ~STK_FLAG_LOGGING_ENABLED;
}

unsigned char stk_is_logging_enabled(void)
{
	return (stk_flags & STK_FLAG_LOGGING_ENABLED) != 0;
}
