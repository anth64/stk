#include "platform.h"
#include "stk.h"
#include <stdlib.h>
#include <string.h>

#define STK_MOD_FUNC_NAME_BUFFER 64

typedef int (*stk_init_mod_func)(void);
typedef void (*stk_shutdown_mod_func)(void);

void *platform_load_library(const char *path);
void platform_unload_library(void *handle);
void *platform_get_symbol(void *handle, const char *symbol);

char (*stk_module_ids)[STK_MOD_ID_BUFFER] = NULL;
void **stk_handles = NULL;
stk_init_mod_func *stk_inits = NULL;
stk_shutdown_mod_func *stk_shutdowns = NULL;

extern unsigned char stk_flags;

static char stk_mod_init_name[STK_MOD_FUNC_NAME_BUFFER] = "stk_mod_init";
static char stk_mod_shutdown_name[STK_MOD_FUNC_NAME_BUFFER] =
    "stk_mod_shutdown";

size_t module_count = 0;

size_t stk_module_count(void) { return module_count; }

void extract_module_id(const char *path, char *out_id)
{
	char *dot;
	const char *basename = strrchr(path, STK_PATH_SEP);

	basename = (basename) ? basename + 1 : path;

	strncpy(out_id, basename, STK_MOD_ID_BUFFER - 1);
	out_id[STK_MOD_ID_BUFFER - 1] = '\0';

	dot = strrchr(out_id, '.');
	if (dot)
		*dot = '\0';
}

unsigned char is_valid_module_file(const char *filename)
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

int is_mod_loaded(const char *module_name)
{
	size_t i;

	for (i = 0; i < module_count; i++)
		if (strncmp(stk_module_ids[i], module_name,
			    STK_MOD_ID_BUFFER) == 0)
			return i;

	return -1;
}

unsigned char stk_module_load(const char *path, int index)
{
	void *handle;
	stk_init_mod_func init_func;
	stk_shutdown_mod_func shutdown_func;
	char module_id[STK_MOD_ID_BUFFER];
	union {
		void *obj;
		stk_init_mod_func init_func;
		stk_shutdown_mod_func shutdown_func;
	} u;
	size_t len;

	handle = platform_load_library(path);
	if (!handle)
		return STK_MOD_LIBRARY_LOAD_ERROR;

	u.obj = platform_get_symbol(handle, stk_mod_init_name);
	init_func = u.init_func;

	u.obj = platform_get_symbol(handle, stk_mod_shutdown_name);
	shutdown_func = u.shutdown_func;

	if (!init_func || !shutdown_func) {
		platform_unload_library(handle);
		return STK_MOD_SYMBOL_NOT_FOUND_ERROR;
	}

	extract_module_id(path, module_id);

	if (init_func() != STK_MOD_INIT_SUCCESS) {
		platform_unload_library(handle);
		return STK_MOD_INIT_FAILURE;
	}

	len = strlen(module_id);
	if (len >= STK_MOD_ID_BUFFER)
		len = STK_MOD_ID_BUFFER - 1;

	memcpy(stk_module_ids[index], module_id, len);
	stk_module_ids[index][len] = '\0';

	stk_handles[index] = handle;
	stk_inits[index] = init_func;
	stk_shutdowns[index] = shutdown_func;

	return STK_MOD_INIT_SUCCESS;
}

unsigned char stk_module_load_init(const char *path, int index)
{
	int result;
	result = stk_module_load(path, index);

	if (result == STK_MOD_INIT_SUCCESS)
		++module_count;

	return result;
}

void stk_module_unload(size_t index)
{
	stk_shutdowns[index]();
	platform_unload_library(stk_handles[index]);
	stk_handles[index] = NULL;
	stk_inits[index] = NULL;
	stk_shutdowns[index] = NULL;
	stk_module_ids[index][0] = '\0';
}

void stk_module_free_memory(void)
{
	free(stk_module_ids);
	free(stk_handles);
	free(stk_inits);
	free(stk_shutdowns);

	stk_module_ids = NULL;
	stk_handles = NULL;
	stk_inits = NULL;
	stk_shutdowns = NULL;
}

unsigned char stk_module_init_memory(size_t capacity)
{
	stk_module_ids = malloc(capacity * sizeof(*stk_module_ids));
	stk_handles = malloc(capacity * sizeof(void *));
	stk_inits = malloc(capacity * sizeof(stk_init_mod_func));
	stk_shutdowns = malloc(capacity * sizeof(stk_shutdown_mod_func));

	if (!stk_module_ids || !stk_handles || !stk_inits || !stk_shutdowns) {
		stk_module_free_memory();
		return STK_INIT_MEMORY_ERROR;
	}

	return STK_INIT_SUCCESS;
}

unsigned char stk_module_realloc_memory(size_t new_capacity)
{
	char (*new_module_ids)[STK_MOD_ID_BUFFER] = NULL;
	void **new_handles = NULL;
	stk_init_mod_func *new_inits = NULL;
	stk_shutdown_mod_func *new_shutdowns = NULL;
	size_t i, copy_count;

	if (new_capacity == 0) {
		stk_module_free_memory();
		return 0;
	}

	new_module_ids = malloc(new_capacity * sizeof(*stk_module_ids));
	new_handles = malloc(new_capacity * sizeof(*new_handles));
	new_inits = malloc(new_capacity * sizeof(stk_init_mod_func));
	new_shutdowns = malloc(new_capacity * sizeof(stk_shutdown_mod_func));

	if (!new_module_ids || !new_handles || !new_inits || !new_shutdowns) {
		if (new_module_ids)
			free(new_module_ids);

		if (new_handles)
			free(new_handles);

		if (new_inits)
			free(new_inits);

		if (new_shutdowns)
			free(new_shutdowns);

		return STK_MOD_REALLOC_FAILURE;
	}

	copy_count =
	    (module_count < new_capacity) ? module_count : new_capacity;

	if (stk_module_ids) {
		for (i = 0; i < copy_count; i++) {
			strncpy(new_module_ids[i], stk_module_ids[i],
				STK_MOD_ID_BUFFER - 1);
			new_module_ids[i][STK_MOD_ID_BUFFER - 1] = '\0';
		}
	}

	if (stk_handles)
		memcpy(new_handles, stk_handles, copy_count * sizeof(void *));

	if (stk_inits)
		memcpy(new_inits, stk_inits,
		       copy_count * sizeof(stk_init_mod_func));

	if (stk_shutdowns)
		memcpy(new_shutdowns, stk_shutdowns,
		       copy_count * sizeof(stk_shutdown_mod_func));

	for (i = copy_count; i < new_capacity; i++) {
		new_module_ids[i][0] = '\0';
		new_handles[i] = NULL;
		new_inits[i] = NULL;
		new_shutdowns[i] = NULL;
	}

	stk_module_free_memory();

	stk_module_ids = new_module_ids;
	stk_handles = new_handles;
	stk_inits = new_inits;
	stk_shutdowns = new_shutdowns;

	return 0;
}

void stk_module_unload_all(void)
{
	size_t i;
	for (i = module_count; i > 0; --i)
		stk_module_unload(i - 1);

	stk_module_free_memory();
}

void stk_set_module_init_fn(const char *name)
{
	if (!name || (stk_flags & STK_FLAG_INITIALIZED))
		return;

	strncpy(stk_mod_init_name, name, STK_MOD_FUNC_NAME_BUFFER - 1);
	stk_mod_init_name[STK_MOD_FUNC_NAME_BUFFER - 1] = '\0';
}

void stk_set_module_shutdown_fn(const char *name)
{
	if (!name || (stk_flags & STK_FLAG_INITIALIZED))
		return;

	strncpy(stk_mod_shutdown_name, name, STK_MOD_FUNC_NAME_BUFFER - 1);
	stk_mod_shutdown_name[STK_MOD_FUNC_NAME_BUFFER - 1] = '\0';
}
