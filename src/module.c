#include "stk.h"
#include <stdlib.h>

void *platform_load_library(const char *path);
void platform_unload_library(void *handle);
void *platform_get_symbol(void *handle, const char *symbol);

typedef void (*stk_module_func)(void);

static void **stk_handles = NULL;
static stk_module_func *stk_inits = NULL;
static stk_module_func *stk_shutdowns = NULL;

static size_t module_count = 0;

size_t stk_module_count(void) { return module_count; }

static int stk_module_load(const char *path)
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

	stk_handles[module_count] = handle;
	stk_inits[module_count] = init_func;
	stk_shutdowns[module_count] = shutdown_func;

	init_func();

	++module_count;
	return 0;
}

static void stk_module_unload(size_t index)
{
	size_t i;

	stk_shutdowns[index]();
	platform_unload_library(stk_handles[index]);

	for (i = index; i < module_count - 1; ++i) {
		stk_handles[i] = stk_handles[i + 1];
		stk_inits[i] = stk_inits[i + 1];
		stk_shutdowns[i] = stk_shutdowns[i + 1];
	}

	--module_count;
}

static void stk_free_file_list(char **list, size_t count)
{
	size_t i;
	for (i = 0; i < count; ++i)
		free(list[i]);

	free(list);
}
