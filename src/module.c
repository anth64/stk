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

char (*stk_meta_names)[STK_MOD_NAME_BUFFER] = NULL;
size_t *stk_meta_name_indices = NULL;
size_t stk_meta_name_count = 0;

char (*stk_meta_versions)[STK_MOD_VERSION_BUFFER] = NULL;
size_t *stk_meta_version_indices = NULL;
size_t stk_meta_version_count = 0;

char (*stk_meta_descs)[STK_MOD_DESC_BUFFER] = NULL;
size_t *stk_meta_desc_indices = NULL;
size_t stk_meta_desc_count = 0;

extern unsigned char stk_flags;

static char stk_mod_init_name[STK_MOD_FUNC_NAME_BUFFER] = "stk_mod_init";
static char stk_mod_shutdown_name[STK_MOD_FUNC_NAME_BUFFER] =
    "stk_mod_shutdown";
static char stk_mod_name_fn[STK_MOD_NAME_BUFFER] = "stk_mod_name";
static char stk_mod_version_fn[STK_MOD_VERSION_BUFFER] = "stk_mod_version";
static char stk_mod_description_fn[STK_MOD_DESC_BUFFER] = "stk_mod_description";

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
		const char *(*meta_func)(void);
	} u;
	size_t len;
	const char *meta_str;

	char (*new_meta_names)[STK_MOD_NAME_BUFFER] = NULL;
	size_t *new_meta_name_indices = NULL;
	char (*new_meta_versions)[STK_MOD_VERSION_BUFFER] = NULL;
	size_t *new_meta_version_indices = NULL;
	char (*new_meta_descs)[STK_MOD_DESC_BUFFER] = NULL;
	size_t *new_meta_desc_indices = NULL;

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

	u.obj = platform_get_symbol(handle, stk_mod_name_fn);
	if (!u.obj)
		goto skip_name;
	meta_str = u.meta_func();
	if (!meta_str)
		goto skip_name;

	new_meta_names = realloc(stk_meta_names, (stk_meta_name_count + 1) *
						     sizeof(*stk_meta_names));
	new_meta_name_indices = realloc(
	    stk_meta_name_indices, (stk_meta_name_count + 1) * sizeof(size_t));
	if (!new_meta_names || !new_meta_name_indices)
		goto skip_name;

	stk_meta_names = new_meta_names;
	stk_meta_name_indices = new_meta_name_indices;
	strncpy(stk_meta_names[stk_meta_name_count], meta_str,
		STK_MOD_NAME_BUFFER - 1);
	stk_meta_names[stk_meta_name_count][STK_MOD_NAME_BUFFER - 1] = '\0';
	stk_meta_name_indices[stk_meta_name_count] = (size_t)index;
	stk_meta_name_count++;

skip_name:
	u.obj = platform_get_symbol(handle, stk_mod_version_fn);
	if (!u.obj)
		goto skip_version;

	meta_str = u.meta_func();
	if (!meta_str)
		goto skip_version;

	new_meta_versions =
	    realloc(stk_meta_versions,
		    (stk_meta_version_count + 1) * sizeof(*stk_meta_versions));
	new_meta_version_indices =
	    realloc(stk_meta_version_indices,
		    (stk_meta_version_count + 1) * sizeof(size_t));
	if (!new_meta_versions || !new_meta_version_indices)
		goto skip_version;

	stk_meta_versions = new_meta_versions;
	stk_meta_version_indices = new_meta_version_indices;
	strncpy(stk_meta_versions[stk_meta_version_count], meta_str,
		STK_MOD_VERSION_BUFFER - 1);
	stk_meta_versions[stk_meta_version_count][STK_MOD_VERSION_BUFFER - 1] =
	    '\0';
	stk_meta_version_indices[stk_meta_version_count] = (size_t)index;
	stk_meta_version_count++;

skip_version:
	u.obj = platform_get_symbol(handle, stk_mod_description_fn);
	if (!u.obj)
		goto skip_description;

	meta_str = u.meta_func();
	if (!meta_str)
		goto skip_description;

	new_meta_descs = realloc(stk_meta_descs, (stk_meta_desc_count + 1) *
						     sizeof(*stk_meta_descs));
	new_meta_desc_indices = realloc(
	    stk_meta_desc_indices, (stk_meta_desc_count + 1) * sizeof(size_t));
	if (!new_meta_descs || !new_meta_desc_indices)
		goto skip_description;
	stk_meta_descs = new_meta_descs;
	stk_meta_desc_indices = new_meta_desc_indices;
	strncpy(stk_meta_descs[stk_meta_desc_count], meta_str,
		STK_MOD_DESC_BUFFER - 1);
	stk_meta_descs[stk_meta_desc_count][STK_MOD_DESC_BUFFER - 1] = '\0';
	stk_meta_desc_indices[stk_meta_desc_count] = (size_t)index;
	stk_meta_desc_count++;

skip_description:
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
	size_t i;
	char (*new_meta_names)[STK_MOD_NAME_BUFFER] = NULL;
	size_t *new_meta_name_indices = NULL;
	char (*new_meta_versions)[STK_MOD_VERSION_BUFFER] = NULL;
	size_t *new_meta_version_indices = NULL;
	char (*new_meta_descs)[STK_MOD_DESC_BUFFER] = NULL;
	size_t *new_meta_desc_indices = NULL;
	size_t new_count;

	stk_shutdowns[index]();
	platform_unload_library(stk_handles[index]);
	stk_handles[index] = NULL;
	stk_inits[index] = NULL;
	stk_shutdowns[index] = NULL;
	stk_module_ids[index][0] = '\0';

	new_count = 0;
	for (i = 0; i < stk_meta_name_count; i++)
		if (stk_meta_name_indices[i] != index)
			new_count++;

	if (new_count == 0)
		goto clear_names;

	new_meta_names = malloc(new_count * sizeof(*new_meta_names));
	new_meta_name_indices = malloc(new_count * sizeof(size_t));
	if (!new_meta_names || !new_meta_name_indices)
		goto clear_names;

	new_count = 0;
	for (i = 0; i < stk_meta_name_count; i++) {
		if (stk_meta_name_indices[i] == index)
			continue;
		memcpy(new_meta_names[new_count], stk_meta_names[i],
		       STK_MOD_NAME_BUFFER);
		new_meta_name_indices[new_count] = stk_meta_name_indices[i];
		new_count++;
	}

clear_names:
	free(stk_meta_names);
	free(stk_meta_name_indices);
	stk_meta_names = new_meta_names;
	stk_meta_name_indices = new_meta_name_indices;
	stk_meta_name_count = new_count;

	new_count = 0;
	for (i = 0; i < stk_meta_version_count; i++)
		if (stk_meta_version_indices[i] != index)
			new_count++;

	if (new_count == 0)
		goto clear_versions;

	new_meta_versions = malloc(new_count * sizeof(*new_meta_versions));
	new_meta_version_indices = malloc(new_count * sizeof(size_t));
	if (!new_meta_versions || !new_meta_version_indices)
		goto clear_versions;

	new_count = 0;
	for (i = 0; i < stk_meta_version_count; i++) {
		if (stk_meta_version_indices[i] == index)
			continue;
		memcpy(new_meta_versions[new_count], stk_meta_versions[i],
		       STK_MOD_VERSION_BUFFER);
		new_meta_version_indices[new_count] =
		    stk_meta_version_indices[i];
		new_count++;
	}

clear_versions:
	free(stk_meta_versions);
	free(stk_meta_version_indices);
	stk_meta_versions = new_meta_versions;
	stk_meta_version_indices = new_meta_version_indices;
	stk_meta_version_count = new_count;

	new_count = 0;
	for (i = 0; i < stk_meta_desc_count; i++)
		if (stk_meta_desc_indices[i] != index)
			new_count++;

	if (new_count == 0)
		goto clear_descs;

	new_meta_descs = malloc(new_count * sizeof(*new_meta_descs));
	new_meta_desc_indices = malloc(new_count * sizeof(size_t));
	if (!new_meta_descs || !new_meta_desc_indices)
		goto clear_descs;

	new_count = 0;
	for (i = 0; i < stk_meta_desc_count; i++) {
		if (stk_meta_desc_indices[i] == index)
			continue;
		memcpy(new_meta_descs[new_count], stk_meta_descs[i],
		       STK_MOD_DESC_BUFFER);
		new_meta_desc_indices[new_count] = stk_meta_desc_indices[i];
		new_count++;
	}

clear_descs:
	free(stk_meta_descs);
	free(stk_meta_desc_indices);
	stk_meta_descs = new_meta_descs;
	stk_meta_desc_indices = new_meta_desc_indices;
	stk_meta_desc_count = new_count;
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

	free(stk_meta_names);
	free(stk_meta_name_indices);
	free(stk_meta_versions);
	free(stk_meta_version_indices);
	free(stk_meta_descs);
	free(stk_meta_desc_indices);

	stk_meta_names = NULL;
	stk_meta_name_indices = NULL;
	stk_meta_name_count = 0;
	stk_meta_versions = NULL;
	stk_meta_version_indices = NULL;
	stk_meta_version_count = 0;
	stk_meta_descs = NULL;
	stk_meta_desc_indices = NULL;
	stk_meta_desc_count = 0;
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

	stk_meta_names = NULL;
	stk_meta_name_indices = NULL;
	stk_meta_name_count = 0;
	stk_meta_versions = NULL;
	stk_meta_version_indices = NULL;
	stk_meta_version_count = 0;
	stk_meta_descs = NULL;
	stk_meta_desc_indices = NULL;
	stk_meta_desc_count = 0;

	return STK_INIT_SUCCESS;
}

unsigned char stk_module_realloc_memory(size_t new_capacity)
{
	char (*new_module_ids)[STK_MOD_ID_BUFFER] = NULL;
	void **new_handles = NULL;
	stk_init_mod_func *new_inits = NULL;
	stk_shutdown_mod_func *new_shutdowns = NULL;
	char (*new_meta_names)[STK_MOD_NAME_BUFFER] = NULL;
	size_t *new_meta_name_indices = NULL;
	char (*new_meta_versions)[STK_MOD_VERSION_BUFFER] = NULL;
	size_t *new_meta_version_indices = NULL;
	char (*new_meta_descs)[STK_MOD_DESC_BUFFER] = NULL;
	size_t *new_meta_desc_indices = NULL;
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

	if (stk_meta_name_count == 0)
		goto skip_meta_names;

	new_meta_names = malloc(stk_meta_name_count * sizeof(*stk_meta_names));
	new_meta_name_indices = malloc(stk_meta_name_count * sizeof(size_t));
	if (!new_meta_names || !new_meta_name_indices) {
		if (new_meta_names)
			free(new_meta_names);

		if (new_meta_name_indices)
			free(new_meta_name_indices);

		new_meta_names = NULL;
		new_meta_name_indices = NULL;
		goto skip_meta_names;
	}

	for (i = 0; i < stk_meta_name_count; i++) {
		strncpy(new_meta_names[i], stk_meta_names[i],
			STK_MOD_NAME_BUFFER - 1);
		new_meta_names[i][STK_MOD_NAME_BUFFER - 1] = '\0';
		new_meta_name_indices[i] = stk_meta_name_indices[i];
	}

skip_meta_names:
	if (stk_meta_version_count == 0)
		goto skip_meta_versions;

	new_meta_versions =
	    malloc(stk_meta_version_count * sizeof(*stk_meta_versions));
	new_meta_version_indices =
	    malloc(stk_meta_version_count * sizeof(size_t));
	if (!new_meta_versions || !new_meta_version_indices) {
		if (new_meta_versions)
			free(new_meta_versions);
		if (new_meta_version_indices)
			free(new_meta_version_indices);
		new_meta_versions = NULL;
		new_meta_version_indices = NULL;
		goto skip_meta_versions;
	}

	for (i = 0; i < stk_meta_version_count; i++) {
		strncpy(new_meta_versions[i], stk_meta_versions[i],
			STK_MOD_VERSION_BUFFER - 1);
		new_meta_versions[i][STK_MOD_VERSION_BUFFER - 1] = '\0';
		new_meta_version_indices[i] = stk_meta_version_indices[i];
	}

skip_meta_versions:
	if (stk_meta_desc_count == 0)
		goto skip_meta_descs;

	new_meta_descs = malloc(stk_meta_desc_count * sizeof(*stk_meta_descs));
	new_meta_desc_indices = malloc(stk_meta_desc_count * sizeof(size_t));
	if (!new_meta_descs || !new_meta_desc_indices) {
		if (new_meta_descs)
			free(new_meta_descs);
		if (new_meta_desc_indices)
			free(new_meta_desc_indices);
		new_meta_descs = NULL;
		new_meta_desc_indices = NULL;
		goto skip_meta_descs;
	}

	for (i = 0; i < stk_meta_desc_count; i++) {
		strncpy(new_meta_descs[i], stk_meta_descs[i],
			STK_MOD_DESC_BUFFER - 1);
		new_meta_descs[i][STK_MOD_DESC_BUFFER - 1] = '\0';
		new_meta_desc_indices[i] = stk_meta_desc_indices[i];
	}

skip_meta_descs:

	stk_module_free_memory();

	stk_module_ids = new_module_ids;
	stk_handles = new_handles;
	stk_inits = new_inits;
	stk_shutdowns = new_shutdowns;

	stk_meta_names = new_meta_names;
	stk_meta_name_indices = new_meta_name_indices;
	stk_meta_versions = new_meta_versions;
	stk_meta_version_indices = new_meta_version_indices;
	stk_meta_descs = new_meta_descs;
	stk_meta_desc_indices = new_meta_desc_indices;

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

void stk_set_module_name_fn(const char *name)
{
	if (!name || (stk_flags & STK_FLAG_INITIALIZED))
		return;

	strncpy(stk_mod_name_fn, name, STK_MOD_NAME_BUFFER - 1);
	stk_mod_name_fn[STK_MOD_NAME_BUFFER - 1] = '\0';
}

void stk_set_module_version_fn(const char *name)
{
	if (!name || (stk_flags & STK_FLAG_INITIALIZED))
		return;

	strncpy(stk_mod_version_fn, name, STK_MOD_VERSION_BUFFER - 1);
	stk_mod_version_fn[STK_MOD_VERSION_BUFFER - 1] = '\0';
}

void stk_set_module_description_fn(const char *name)
{
	if (!name || (stk_flags & STK_FLAG_INITIALIZED))
		return;

	strncpy(stk_mod_description_fn, name, STK_MOD_DESC_BUFFER - 1);
	stk_mod_description_fn[STK_MOD_DESC_BUFFER - 1] = '\0';
}
