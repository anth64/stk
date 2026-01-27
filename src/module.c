#include "stk.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define STK_MOD_FUNC_NAME_BUFFER 64

void *platform_load_library(const char *path);
void platform_unload_library(void *handle);
void *platform_get_symbol(void *handle, const char *symbol);

typedef void (*stk_module_func)(void);

char (*stk_module_ids)[STK_MOD_ID_BUFFER] = NULL;
void **stk_handles = NULL;
stk_module_func *stk_inits = NULL;
stk_module_func *stk_shutdowns = NULL;

extern uint8_t stk_initialized;

static char stk_mod_init_name[STK_MOD_FUNC_NAME_BUFFER] = "stk_mod_init";
static char stk_mod_shutdown_name[STK_MOD_FUNC_NAME_BUFFER] =
    "stk_mod_shutdown";

size_t module_count = 0;

size_t stk_module_count(void) { return module_count; }

void extract_module_id(const char *path, char *out_id)
{
	const char *basename;
	char *dot;

	basename = strrchr(path, '/');
#ifdef _WIN32
	if (!basename)
		basename = strrchr(path, '\\');
#endif
	if (!basename)
		basename = path;
	else
		basename++;

	strncpy(out_id, basename, STK_MOD_ID_BUFFER - 1);
	out_id[STK_MOD_ID_BUFFER - 1] = '\0';

	dot = strrchr(out_id, '.');
	if (dot)
		*dot = '\0';
}

uint8_t is_valid_module_file(const char *filename)
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

int stk_module_load(const char *path, int index)
{
	void *handle;
	stk_module_func init_func;
	stk_module_func shutdown_func;
	const char *basename;
	char *dot;
	char module_id[STK_MOD_ID_BUFFER];
	union {
		void *obj;
		stk_module_func func;
	} u;

	handle = platform_load_library(path);
	if (!handle)
		return -1;

	u.obj = platform_get_symbol(handle, stk_mod_init_name);
	init_func = u.func;

	u.obj = platform_get_symbol(handle, stk_mod_shutdown_name);
	shutdown_func = u.func;

	if (!init_func || !shutdown_func) {
		platform_unload_library(handle);
		return -2;
	}

	if (index == -1)
		index = module_count;

	basename = strrchr(path, '/');
	if (!basename)
		basename = path;
	else
		basename++;

	strncpy(module_id, basename, STK_MOD_ID_BUFFER - 1);
	module_id[STK_MOD_ID_BUFFER - 1] = '\0';

	dot = strrchr(module_id, '.');
	if (dot)
		*dot = '\0';

	strncpy(stk_module_ids[index], module_id, STK_MOD_ID_BUFFER - 1);
	stk_module_ids[index][STK_MOD_ID_BUFFER - 1] = '\0';

	stk_handles[index] = handle;
	stk_inits[index] = init_func;
	stk_shutdowns[index] = shutdown_func;

	init_func(); /* TODO eventually, this should have some sort of check */

	return 0;
}

int stk_module_load_init(const char *path, int index)
{
	int result;
	result = stk_module_load(path, index);

	if (result == 0)
		++module_count;

	return result;
}

void stk_module_unload(size_t index)
{
	stk_shutdowns[index]();
	platform_unload_library(stk_handles[index]);
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

int stk_module_init_memory(size_t capacity)
{
	stk_module_ids = malloc(capacity * sizeof(*stk_module_ids));
	stk_handles = malloc(capacity * sizeof(void *));
	stk_inits = malloc(capacity * sizeof(stk_module_func));
	stk_shutdowns = malloc(capacity * sizeof(stk_module_func));

	if (!stk_module_ids || !stk_handles || !stk_inits || !stk_shutdowns) {
		stk_module_free_memory();
		return -1;
	}

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
	if (!name || stk_initialized)
		return;

	strncpy(stk_mod_init_name, name, STK_MOD_FUNC_NAME_BUFFER - 1);
	stk_mod_init_name[STK_MOD_FUNC_NAME_BUFFER - 1] = '\0';
}

void stk_set_module_shutdown_fn(const char *name)
{
	if (!name || stk_initialized)
		return;

	strncpy(stk_mod_shutdown_name, name, STK_MOD_FUNC_NAME_BUFFER - 1);
	stk_mod_shutdown_name[STK_MOD_FUNC_NAME_BUFFER - 1] = '\0';
}
