#include "stk.h"
#include <stdlib.h>
#include <string.h>

void *platform_load_library(const char *path);
void platform_unload_library(void *handle);
void *platform_get_symbol(void *handle, const char *symbol);

typedef void (*stk_module_func)(void);

char **stk_module_ids = NULL;
static void **stk_handles = NULL;
static stk_module_func *stk_inits = NULL;
static stk_module_func *stk_shutdowns = NULL;

size_t module_count = 0;

size_t stk_module_count(void) { return module_count; }

char *extract_module_id(const char *path)
{
	char *id, *dot;
	const char *basename = strrchr(path, '/');
	if (!basename)
		basename = path;
	else
		basename++;

	id = malloc(strlen(basename) + 1);
	strcpy(id, basename);

	dot = strrchr(id, '.');
	if (dot)
		*dot = '\0';

	return id;
}

int stk_module_load(const char *path)
{
	void *handle;
	stk_module_func init_func;
	stk_module_func shutdown_func;

	handle = platform_load_library(path);
	if (!handle)
		return -1;

	init_func =
	    (stk_module_func)platform_get_symbol(handle, "stk_module_init");
	shutdown_func =
	    (stk_module_func)platform_get_symbol(handle, "stk_module_shutdown");

	if (!init_func || !shutdown_func) {
		platform_unload_library(handle);
		return -2;
	}

	stk_module_ids[module_count] = extract_module_id(path);
	stk_handles[module_count] = handle;
	stk_inits[module_count] = init_func;
	stk_shutdowns[module_count] = shutdown_func;

	init_func();

	++module_count;
	return 0;
}

void stk_module_unload(size_t index)
{
	size_t i;

	stk_shutdowns[index]();
	platform_unload_library(stk_handles[index]);

	for (i = index; i < module_count - 1; ++i) {
		stk_module_ids[i] = stk_module_ids[i + 1];
		stk_handles[i] = stk_handles[i + 1];
		stk_inits[i] = stk_inits[i + 1];
		stk_shutdowns[i] = stk_shutdowns[i + 1];
	}

	--module_count;
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
	stk_module_ids = malloc(capacity * sizeof(char *));
	stk_handles = malloc(capacity * sizeof(void *));
	stk_inits = malloc(capacity * sizeof(stk_module_func));
	stk_shutdowns = malloc(capacity * sizeof(stk_module_func));

	if (!stk_handles || !stk_inits || !stk_shutdowns) {
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
