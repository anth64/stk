#include "stk.h"
#include "platform.h"
#include "stk_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*stk_init_mod_func)(void);
typedef void (*stk_shutdown_mod_func)(void);

typedef struct {
	char desc[STK_MOD_DESC_BUFFER];
	char name[STK_MOD_NAME_BUFFER];
	char id[STK_MOD_ID_BUFFER];
	char version[STK_MOD_VERSION_BUFFER];
	void *handle;
	stk_init_mod_func init;
	stk_shutdown_mod_func shutdown;
	stk_dep_t *deps;
	size_t dep_count;
} stk_mod_t;

extern stk_mod_t *stk_modules;
extern size_t module_count;

unsigned char stk_flags = STK_FLAG_LOGGING_ENABLED;

static char stk_mod_dir[STK_PATH_MAX_OS] = "mods";
static char stk_tmp_name[STK_MOD_ID_BUFFER] = ".tmp";
static char stk_tmp_dir[STK_PATH_MAX_OS] = "";
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
unsigned char stk_module_preload(const char *path, int index);
unsigned char stk_module_activate(size_t index);
unsigned char stk_validate_dependencies_single(size_t index);
void stk_log_dependency_failures(size_t index, const char *action);
void stk_module_discard(size_t index);
unsigned char stk_module_load(const char *path, int index);
unsigned char stk_module_load_init(const char *path, int index);
unsigned char stk_module_init_memory(size_t capacity);
unsigned char stk_module_realloc_memory(size_t new_capacity);
void stk_module_unload(size_t index);
void stk_module_unload_all(void);
unsigned char stk_validate_dependencies(size_t count);
unsigned char stk_topo_sort(size_t count, size_t *order);
void stk_pending_add(const char *path);
void stk_pending_add_batch(const char (*paths)[STK_PATH_MAX_OS], size_t count);
void stk_pending_remove(const char *id);
size_t stk_pending_retry(void);
void stk_sort_unload_order(size_t *indices, size_t n);
void stk_collect_dependents(size_t *indices, size_t *count, size_t capacity);
void stk_sort_load_order(int *file_indices, size_t n,
			 char (*file_names)[STK_PATH_MAX], const char *tmp_dir);

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
	case STK_MOD_DEP_NOT_FOUND_ERROR:
		return "dependency not found";
	case STK_MOD_DEP_VERSION_MISMATCH_ERROR:
		return "dependency version mismatch";
	case STK_MOD_DEP_CIRCULAR_ERROR:
		return "circular dependency detected";
	default:
		return "unknown error";
	}
}

static void stk_log_module(size_t index)
{
	const char *name =
	    stk_modules[index].name[0] ? stk_modules[index].name : NULL;
	const char *desc =
	    stk_modules[index].desc[0] ? stk_modules[index].desc : NULL;

	if (name && desc)
		stk_log(STK_LOG_INFO, "  %s v%s - %s (%s)",
			stk_modules[index].id, stk_modules[index].version, desc,
			name);
	else if (name)
		stk_log(STK_LOG_INFO, "  %s v%s (%s)", stk_modules[index].id,
			stk_modules[index].version, name);
	else if (desc)
		stk_log(STK_LOG_INFO, "  %s v%s - %s", stk_modules[index].id,
			stk_modules[index].version, desc);
	else
		stk_log(STK_LOG_INFO, "  %s v%s", stk_modules[index].id,
			stk_modules[index].version);
}

static void stk_log_modules(void)
{
	size_t i;
	stk_log(STK_LOG_INFO,
		"Loaded modules (%lu):", (unsigned long)module_count);
	for (i = 0; i < module_count; i++)
		stk_log_module(i);
}

unsigned char stk_init(void)
{
	char (*files)[STK_PATH_MAX] = NULL;
	char (*test_scan)[STK_PATH_MAX];
	size_t file_count, i, j, write, successful_loads = 0;
	size_t index, test_count;
	char full_path[STK_PATH_MAX_OS];
	char tmp_path[STK_PATH_MAX_OS];
	int load_result;
	unsigned char dep_result;
	size_t *order = NULL;
	char (*init_batch)[STK_PATH_MAX_OS] = NULL;
	size_t init_batch_count = 0;

	platform_mkdir(stk_mod_dir);
	build_path(stk_tmp_dir, sizeof(stk_tmp_dir), stk_mod_dir, stk_tmp_name);
	if (platform_mkdir(stk_tmp_dir) != STK_PLATFORM_OPERATION_SUCCESS) {
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

		load_result = stk_module_preload(tmp_path, successful_loads);

		if (load_result != STK_MOD_INIT_SUCCESS) {
			stk_log(STK_LOG_ERROR,
				"Failed to preload module %s: %s", files[i],
				stk_error_string(load_result));
		} else {
			successful_loads++;
			module_count++;
		}
	}

	if (successful_loads < file_count)
		stk_module_realloc_memory(successful_loads);

	free(files);

	if (module_count == 0)
		goto scanned;

	order = malloc(module_count * sizeof(size_t));
	if (order) {
		dep_result = stk_topo_sort(module_count, order);
		if (dep_result != STK_MOD_INIT_SUCCESS)
			stk_log(STK_LOG_ERROR, "Dependency sort failed: %s",
				stk_error_string(dep_result));
	}

	init_batch = malloc(module_count * sizeof(*init_batch));

	for (j = 0; j < module_count; j++) {
		index = order ? order[j] : j;
		dep_result = stk_validate_dependencies_single(index);
		if (dep_result != STK_MOD_INIT_SUCCESS) {
			stk_log_dependency_failures(index, "Deferring");
			if (init_batch) {
				build_path(init_batch[init_batch_count],
					   sizeof(init_batch[init_batch_count]),
					   stk_tmp_dir, stk_modules[index].id);
				strncat(
				    init_batch[init_batch_count],
				    STK_MODULE_EXT,
				    sizeof(init_batch[init_batch_count]) -
					strlen(init_batch[init_batch_count]) -
					1);
				init_batch_count++;
			}
			stk_module_discard(index);
			continue;
		}
		if (stk_module_activate(index) != STK_MOD_INIT_SUCCESS) {
			stk_log(STK_LOG_ERROR, "Failed to init module %s",
				stk_modules[index].id);
			stk_module_discard(index);
		}
	}

	if (init_batch_count > 0)
		stk_pending_add_batch(
		    (const char (*)[STK_PATH_MAX_OS])init_batch,
		    init_batch_count);

	free(init_batch);
	init_batch = NULL;

	if (order) {
		free(order);
		order = NULL;
	}

	write = 0;
	for (j = 0; j < module_count; j++) {
		if (stk_modules[j].handle != NULL) {
			if (write != j)
				stk_modules[write] = stk_modules[j];
			write++;
		}
	}
	module_count = write;

scanned:
	watch_handle = platform_directory_watch_start(stk_mod_dir);
	if (!watch_handle) {
		stk_log(STK_LOG_ERROR,
			"FATAL: Cannot start directory watch on %s",
			stk_mod_dir);
		stk_module_unload_all();
		return STK_INIT_WATCH_ERROR;
	}

	stk_pending_retry();

	stk_log(STK_LOG_INFO, "stk v%s initialized, watching %s/",
		STK_VERSION_STRING, stk_mod_dir);
	if (module_count > 0)
		stk_log_modules();

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
	size_t new_capacity;
	char full_path[STK_PATH_MAX_OS], tmp_path[STK_PATH_MAX_OS];
	char mod_id[STK_MOD_ID_BUFFER];
	int load_result;
	size_t successful_appends = 0;
	char (*module_ids)[STK_MOD_ID_BUFFER] = NULL;
	unsigned char dep_result;
	size_t *order = NULL;
	size_t *unload_order = NULL;
	size_t expanded_count;
	size_t index, oi;
	int is_orig;
	size_t write;
	int file_index, mod_index;
	size_t *cascade_indices = NULL;
	size_t cascade_count;
	size_t j, k, cascade_write;
	char (*dep_batch)[STK_PATH_MAX_OS] = NULL;
	size_t dep_batch_count = 0;
	char (*cascade_batch)[STK_PATH_MAX_OS] = NULL;
	size_t cascade_batch_count = 0;
	char (*load_batch)[STK_PATH_MAX_OS] = NULL;
	size_t load_batch_count = 0;

	if (module_count > 0) {
		module_ids = malloc(module_count * sizeof(*module_ids));
		if (module_ids) {
			for (i = 0; i < module_count; i++) {
				strncpy(module_ids[i], stk_modules[i].id,
					STK_MOD_ID_BUFFER - 1);
				module_ids[i][STK_MOD_ID_BUFFER - 1] = '\0';
			}
		}
	}

	events = platform_directory_watch_check(
	    watch_handle, &file_list, &file_count, module_ids, module_count);

	if (module_ids)
		free(module_ids);

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
	new_capacity = module_count + load_count;
	if (stk_module_realloc_memory(new_capacity) != STK_MOD_INIT_SUCCESS)
		goto free_poll;

begin_operations:
	unload_order = malloc(module_count * sizeof(size_t));
	if (unload_order) {
		expanded_count = unload_count;

		for (i = 0; i < unload_count; i++)
			unload_order[i] = (size_t)unloaded_mod_indices[i];

		stk_collect_dependents(unload_order, &expanded_count,
				       module_count);
		stk_sort_unload_order(unload_order, expanded_count);

		dep_batch = malloc(expanded_count * sizeof(*dep_batch));
		dep_batch_count = 0;

		for (i = 0; i < expanded_count; i++) {
			index = unload_order[i];

			stk_log(STK_LOG_INFO, "Unloaded module: %s",
				stk_modules[index].id);
			stk_pending_remove(stk_modules[index].id);

			is_orig = 0;
			for (oi = 0; oi < unload_count; oi++) {
				if ((size_t)unloaded_mod_indices[oi] == index) {
					is_orig = 1;
					break;
				}
			}
			if (!is_orig && dep_batch) {
				build_path(dep_batch[dep_batch_count],
					   sizeof(dep_batch[dep_batch_count]),
					   stk_tmp_dir, stk_modules[index].id);
				strncat(
				    dep_batch[dep_batch_count], STK_MODULE_EXT,
				    sizeof(dep_batch[dep_batch_count]) -
					strlen(dep_batch[dep_batch_count]) - 1);
				dep_batch_count++;
			}

			stk_module_unload(index);
		}

		if (dep_batch_count > 0)
			stk_pending_add_batch(
			    (const char (*)[STK_PATH_MAX_OS])dep_batch,
			    dep_batch_count);

		free(dep_batch);
		dep_batch = NULL;
		free(unload_order);
		unload_order = NULL;
	} else {
		for (i = 0; i < unload_count; ++i) {
			stk_log(STK_LOG_INFO, "Unloaded module: %s",
				stk_modules[unloaded_mod_indices[i]].id);
			stk_pending_remove(
			    stk_modules[unloaded_mod_indices[i]].id);
			stk_module_unload(unloaded_mod_indices[i]);
		}
	}

	if (unload_count > 0) {
		write = 0;
		for (i = 0; i < module_count; i++) {
			if (stk_modules[i].handle != NULL) {
				if (write != i)
					stk_modules[write] = stk_modules[i];
				write++;
			}
		}
		module_count = write;
		if (module_count > 0)
			stk_module_realloc_memory(module_count);
	}

	for (i = 0; i < reload_count; ++i) {
		file_index = reloaded_mod_file_indices[i];
		mod_index = reloaded_mod_indices[i];

		build_path(full_path, sizeof(full_path), stk_mod_dir,
			   file_list[file_index]);
		build_path(tmp_path, sizeof(tmp_path), stk_tmp_dir,
			   file_list[file_index]);

		stk_module_unload(mod_index);

		if (platform_copy_file(full_path, tmp_path) !=
		    STK_PLATFORM_OPERATION_SUCCESS) {
			stk_log(STK_LOG_ERROR, "Failed to copy %s for reload",
				file_list[file_index]);
			continue;
		}

		load_result = stk_module_load(tmp_path, mod_index);
		if (load_result != STK_MOD_INIT_SUCCESS)
			stk_log(STK_LOG_ERROR, "Failed to reload module %s: %s",
				file_list[file_index],
				stk_error_string(load_result));
	}

	for (i = 0; i < load_count; i++) {
		file_index = loaded_mod_indices[i];
		build_path(full_path, sizeof(full_path), stk_mod_dir,
			   file_list[file_index]);
		build_path(tmp_path, sizeof(tmp_path), stk_tmp_dir,
			   file_list[file_index]);
		platform_copy_file(full_path, tmp_path);
	}

	if (load_count > 1)
		stk_sort_load_order(loaded_mod_indices, load_count, file_list,
				    stk_tmp_dir);

	load_batch = malloc(load_count * sizeof(*load_batch));
	load_batch_count = 0;

	for (i = 0; i < load_count; ++i) {
		file_index = loaded_mod_indices[i];

		build_path(tmp_path, sizeof(tmp_path), stk_tmp_dir,
			   file_list[file_index]);

		load_result = stk_module_load(tmp_path, module_count +
							    successful_appends);
		if (load_result == STK_MOD_DEP_NOT_FOUND_ERROR ||
		    load_result == STK_MOD_DEP_VERSION_MISMATCH_ERROR) {
			if (load_batch)
				memcpy(load_batch[load_batch_count++], tmp_path,
				       STK_PATH_MAX_OS);
		} else if (load_result != STK_MOD_INIT_SUCCESS) {
			stk_log(STK_LOG_ERROR, "Failed to load module %s: %s",
				file_list[file_index],
				stk_error_string(load_result));
		} else {
			successful_appends++;
		}
	}

	module_count += successful_appends;

	if (successful_appends < load_count)
		stk_module_realloc_memory(module_count);

	if (load_batch_count > 0)
		stk_pending_add_batch(
		    (const char (*)[STK_PATH_MAX_OS])load_batch,
		    load_batch_count);

	free(load_batch);
	load_batch = NULL;

	goto validate_deps;

validate_deps:
	if (module_count == 0)
		goto free_poll;

	do {
		cascade_count = 0;

		cascade_indices = malloc(module_count * sizeof(size_t));
		if (!cascade_indices)
			break;

		for (j = 0; j < module_count; j++) {
			if (stk_modules[j].dep_count == 0)
				continue;
			for (k = 0; k < stk_modules[j].dep_count; k++) {
				if (is_mod_loaded(stk_modules[j].deps[k].id) <
				    0) {
					cascade_indices[cascade_count++] = j;
					break;
				}
			}
		}

		if (cascade_count == 0) {
			free(cascade_indices);
			cascade_indices = NULL;
			break;
		}

		cascade_batch = malloc(cascade_count * sizeof(*cascade_batch));
		cascade_batch_count = 0;

		for (j = 0; j < cascade_count; j++) {
			index = cascade_indices[j];
			stk_log_dependency_failures(index, "Unloading");
			if (cascade_batch) {
				build_path(
				    cascade_batch[cascade_batch_count],
				    sizeof(cascade_batch[cascade_batch_count]),
				    stk_tmp_dir, stk_modules[index].id);
				strncat(
				    cascade_batch[cascade_batch_count],
				    STK_MODULE_EXT,
				    sizeof(cascade_batch[cascade_batch_count]) -
					strlen(cascade_batch
						   [cascade_batch_count]) -
					1);
				cascade_batch_count++;
			}
			stk_module_unload(index);
		}

		if (cascade_batch_count > 0)
			stk_pending_add_batch(
			    (const char (*)[STK_PATH_MAX_OS])cascade_batch,
			    cascade_batch_count);

		free(cascade_batch);
		cascade_batch = NULL;

		cascade_write = 0;
		for (j = 0; j < module_count; j++) {
			if (stk_modules[j].handle != NULL) {
				if (cascade_write != j)
					stk_modules[cascade_write] =
					    stk_modules[j];
				cascade_write++;
			}
		}
		module_count = cascade_write;

		free(cascade_indices);
		cascade_indices = NULL;

	} while (cascade_count > 0);

	if (module_count > 0)
		stk_module_realloc_memory(module_count);

	order = malloc(module_count * sizeof(size_t));
	if (order) {
		dep_result = stk_topo_sort(module_count, order);
		if (dep_result != STK_MOD_INIT_SUCCESS)
			stk_log(STK_LOG_ERROR, "Dependency sort failed: %s",
				stk_error_string(dep_result));
		free(order);
	}

free_poll:
	stk_pending_retry();

	if (module_count > 0)
		stk_log_modules();

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
