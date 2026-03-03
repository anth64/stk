#include "platform.h"
#include "stk.h"
#include "stk_log.h"
#include <stdlib.h>
#include <string.h>

#define STK_MOD_FUNC_NAME_BUFFER 64

typedef int (*stk_init_mod_func)(void);
typedef void (*stk_shutdown_mod_func)(void);

typedef struct {
	unsigned char major;
	unsigned char minor;
	unsigned char patch;
	char op;
} stk_version_t;

typedef struct {
	void *handle;
	stk_init_mod_func init;
	stk_shutdown_mod_func shutdown;
	char id[STK_MOD_ID_BUFFER];
	char name[STK_MOD_NAME_BUFFER];
	char version[STK_MOD_VERSION_BUFFER];
	char desc[STK_MOD_DESC_BUFFER];
	stk_dep_t *deps;
	size_t dep_count;
} stk_mod_t;

void *platform_load_library(const char *path);
void platform_unload_library(void *handle);
void *platform_get_symbol(void *handle, const char *symbol);

stk_mod_t *stk_modules = NULL;

extern unsigned char stk_flags;

static char stk_mod_init_name[STK_MOD_FUNC_NAME_BUFFER] = "stk_mod_init";
static char stk_mod_shutdown_name[STK_MOD_FUNC_NAME_BUFFER] =
    "stk_mod_shutdown";
static char stk_mod_name_fn[STK_MOD_FUNC_NAME_BUFFER] = "stk_mod_name";
static char stk_mod_version_fn[STK_MOD_FUNC_NAME_BUFFER] = "stk_mod_version";
static char stk_mod_description_fn[STK_MOD_FUNC_NAME_BUFFER] =
    "stk_mod_description";
static char stk_mod_deps_sym[STK_MOD_FUNC_NAME_BUFFER] = "stk_mod_deps";

size_t module_count = 0;

static stk_version_t stk_parse_version(const char *str)
{
	stk_version_t v;
	v.major = 0;
	v.minor = 0;
	v.patch = 0;
	v.op = '>';

	if (!str || !*str)
		return v;

	if (*str == '=' && *(str + 1) != '=') {
		v.op = '=';
		str++;
	} else if (*str == '>' && *(str + 1) == '=') {
		v.op = '>';
		str += 2;
	} else if (*str == '^') {
		v.op = '^';
		str++;
	}

	v.major = (unsigned char)strtol(str, (char **)&str, 10);
	if (*str == '.')
		str++;
	v.minor = (unsigned char)strtol(str, (char **)&str, 10);
	if (*str == '.')
		str++;
	v.patch = (unsigned char)strtol(str, NULL, 10);

	return v;
}

static int stk_compare_version(stk_version_t a, stk_version_t b)
{
	if (a.major != b.major)
		return a.major - b.major;
	if (a.minor != b.minor)
		return a.minor - b.minor;
	return a.patch - b.patch;
}

static int stk_validate_constraint(const char *constraint, const char *loaded)
{
	stk_version_t req = stk_parse_version(constraint);
	stk_version_t have = stk_parse_version(loaded);
	int cmp = stk_compare_version(have, req);

	switch (req.op) {
	case '=':
		return cmp == 0;
	case '^':
		return have.major == req.major && cmp >= 0;
	default:
		return cmp >= 0;
	}
}

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
		if (strncmp(stk_modules[i].id, module_name,
			    STK_MOD_ID_BUFFER) == 0)
			return i;

	return -1;
}

unsigned char stk_validate_dependencies(size_t count)
{
	size_t i, d;
	int found;

	for (i = 0; i < count; i++) {
		if (stk_modules[i].dep_count == 0)
			continue;

		for (d = 0; d < stk_modules[i].dep_count; d++) {
			found = is_mod_loaded(stk_modules[i].deps[d].id);
			if (found < 0)
				return STK_MOD_DEP_NOT_FOUND_ERROR;

			if (!stk_modules[i].deps[d].version[0])
				continue;

			if (!stk_validate_constraint(
				stk_modules[i].deps[d].version,
				stk_modules[found].version))
				return STK_MOD_DEP_VERSION_MISMATCH_ERROR;
		}
	}

	return STK_MOD_INIT_SUCCESS;
}

unsigned char stk_topo_sort(size_t count, size_t *order)
{
	size_t *in_degree = NULL;
	size_t *queue = NULL;
	size_t head, tail, sorted, i, d;
	int dep_index;
	unsigned char result = STK_MOD_INIT_SUCCESS;

	if (count == 0)
		goto done;

	in_degree = malloc(count * sizeof(size_t));
	queue = malloc(count * sizeof(size_t));

	if (!in_degree || !queue) {
		result = STK_MOD_REALLOC_FAILURE;
		goto done;
	}

	for (i = 0; i < count; i++)
		in_degree[i] = 0;

	for (i = 0; i < count; i++)
		for (d = 0; d < stk_modules[i].dep_count; d++) {
			dep_index = is_mod_loaded(stk_modules[i].deps[d].id);
			if (dep_index >= 0)
				in_degree[i]++;
		}

	head = tail = sorted = 0;

	for (i = 0; i < count; i++)
		if (in_degree[i] == 0)
			queue[tail++] = i;

	while (head < tail) {
		size_t mod = queue[head++];
		order[sorted++] = mod;

		for (i = 0; i < count; i++) {
			for (d = 0; d < stk_modules[i].dep_count; d++) {
				dep_index =
				    is_mod_loaded(stk_modules[i].deps[d].id);
				if (dep_index != (int)mod)
					continue;
				if (--in_degree[i] == 0)
					queue[tail++] = i;
				break;
			}
		}
	}

	if (sorted != count) {
		size_t j;
		int in_order;
		for (i = 0; i < count; i++) {
			in_order = 0;
			for (j = 0; j < sorted; j++) {
				if (order[j] == i) {
					in_order = 1;
					break;
				}
			}

			if (!in_order)
				stk_log(STK_LOG_ERROR,
					"Circular dependency detected with %s",
					stk_modules[i].id);
		}

		result = STK_MOD_DEP_CIRCULAR_ERROR;
	}
done:
	if (in_degree)
		free(in_degree);
	if (queue)
		free(queue);

	return result;
}

unsigned char stk_module_load(const char *path, int index)
{
	void *handle;
	char module_id[STK_MOD_ID_BUFFER];
	size_t len;
	union {
		void *obj;
		stk_init_mod_func init_func;
		stk_shutdown_mod_func shutdown_func;
		const char *(*meta_func)(void);
	} u;
	const char *meta_str;
	const stk_dep_t *deps;
	size_t dep_count;
	stk_dep_t *dep_arr;

	handle = platform_load_library(path);
	if (!handle)
		return STK_MOD_LIBRARY_LOAD_ERROR;

	u.obj = platform_get_symbol(handle, stk_mod_init_name);
	if (!u.obj) {
		platform_unload_library(handle);
		return STK_MOD_SYMBOL_NOT_FOUND_ERROR;
	}
	stk_modules[index].init = u.init_func;

	u.obj = platform_get_symbol(handle, stk_mod_shutdown_name);
	if (!u.obj) {
		platform_unload_library(handle);
		return STK_MOD_SYMBOL_NOT_FOUND_ERROR;
	}
	stk_modules[index].shutdown = u.shutdown_func;

	extract_module_id(path, module_id);

	if (stk_modules[index].init() != STK_MOD_INIT_SUCCESS) {
		platform_unload_library(handle);
		return STK_MOD_INIT_FAILURE;
	}

	stk_modules[index].handle = handle;

	len = strlen(module_id);
	if (len >= STK_MOD_ID_BUFFER)
		len = STK_MOD_ID_BUFFER - 1;
	memcpy(stk_modules[index].id, module_id, len);
	stk_modules[index].id[len] = '\0';

	stk_modules[index].name[0] = '\0';
	u.obj = platform_get_symbol(handle, stk_mod_name_fn);
	if (u.obj) {
		meta_str = u.meta_func();
		if (meta_str) {
			strncpy(stk_modules[index].name, meta_str,
				STK_MOD_NAME_BUFFER - 1);
			stk_modules[index].name[STK_MOD_NAME_BUFFER - 1] = '\0';
		}
	}

	stk_modules[index].version[0] = '\0';
	u.obj = platform_get_symbol(handle, stk_mod_version_fn);
	if (u.obj) {
		meta_str = u.meta_func();
		if (meta_str) {
			stk_version_t v = stk_parse_version(meta_str);
			if (v.major == 0 && v.minor == 0 && v.patch == 0 &&
			    meta_str[0] != '0') {
				strncpy(stk_modules[index].version, "0.0.0",
					STK_MOD_VERSION_BUFFER - 1);
			} else {
				strncpy(stk_modules[index].version, meta_str,
					STK_MOD_VERSION_BUFFER - 1);
			}
			stk_modules[index].version[STK_MOD_VERSION_BUFFER - 1] =
			    '\0';
		}
	}
	if (!stk_modules[index].version[0])
		strncpy(stk_modules[index].version, "0.0.0",
			STK_MOD_VERSION_BUFFER - 1);

	stk_modules[index].desc[0] = '\0';
	u.obj = platform_get_symbol(handle, stk_mod_description_fn);
	if (u.obj) {
		meta_str = u.meta_func();
		if (meta_str) {
			strncpy(stk_modules[index].desc, meta_str,
				STK_MOD_DESC_BUFFER - 1);
			stk_modules[index].desc[STK_MOD_DESC_BUFFER - 1] = '\0';
		}
	}

	stk_modules[index].deps = NULL;
	stk_modules[index].dep_count = 0;
	u.obj = platform_get_symbol(handle, stk_mod_deps_sym);
	if (!u.obj)
		goto skip_deps;

	deps = (const stk_dep_t *)u.obj;

	dep_count = 0;
	while (deps[dep_count].id[0] != '\0')
		dep_count++;

	if (dep_count == 0)
		goto skip_deps;

	dep_arr = malloc(dep_count * sizeof(stk_dep_t));
	if (!dep_arr)
		goto skip_deps;

	{
		size_t d;
		for (d = 0; d < dep_count; d++) {
			strncpy(dep_arr[d].id, deps[d].id,
				STK_MOD_ID_BUFFER - 1);
			dep_arr[d].id[STK_MOD_ID_BUFFER - 1] = '\0';
			strncpy(dep_arr[d].version, deps[d].version,
				STK_MOD_VERSION_BUFFER - 1);
			dep_arr[d].version[STK_MOD_VERSION_BUFFER - 1] = '\0';
		}
	}

	stk_modules[index].deps = dep_arr;
	stk_modules[index].dep_count = dep_count;

skip_deps:
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
	stk_modules[index].shutdown();
	platform_unload_library(stk_modules[index].handle);

	stk_modules[index].handle = NULL;
	stk_modules[index].init = NULL;
	stk_modules[index].shutdown = NULL;
	stk_modules[index].id[0] = '\0';
	stk_modules[index].name[0] = '\0';
	stk_modules[index].version[0] = '\0';
	stk_modules[index].desc[0] = '\0';

	if (stk_modules[index].deps) {
		free(stk_modules[index].deps);
		stk_modules[index].deps = NULL;
	}
	stk_modules[index].dep_count = 0;
}

void stk_module_free_memory(void)
{
	if (stk_modules) {
		size_t i;
		for (i = 0; i < module_count; i++) {
			if (stk_modules[i].deps)
				free(stk_modules[i].deps);
		}
		free(stk_modules);
		stk_modules = NULL;
	}
	module_count = 0;
}

unsigned char stk_module_init_memory(size_t capacity)
{
	stk_modules = malloc(capacity * sizeof(stk_mod_t));
	if (!stk_modules)
		return STK_INIT_MEMORY_ERROR;

	return STK_INIT_SUCCESS;
}

unsigned char stk_module_realloc_memory(size_t new_capacity)
{
	stk_mod_t *new_modules;
	size_t i, copy_count;

	if (new_capacity == 0) {
		stk_module_free_memory();
		return 0;
	}

	new_modules = malloc(new_capacity * sizeof(stk_mod_t));
	if (!new_modules)
		return STK_MOD_REALLOC_FAILURE;

	copy_count =
	    (module_count < new_capacity) ? module_count : new_capacity;

	for (i = 0; i < copy_count; i++)
		new_modules[i] = stk_modules[i];

	for (i = copy_count; i < new_capacity; i++) {
		new_modules[i].handle = NULL;
		new_modules[i].init = NULL;
		new_modules[i].shutdown = NULL;
		new_modules[i].id[0] = '\0';
		new_modules[i].name[0] = '\0';
		new_modules[i].version[0] = '\0';
		new_modules[i].desc[0] = '\0';
		new_modules[i].deps = NULL;
		new_modules[i].dep_count = 0;
	}

	free(stk_modules);
	stk_modules = new_modules;

	return 0;
}

void stk_module_unload_all(void)
{
	size_t i;
	for (i = module_count; i > 0; --i)
		stk_module_unload(i - 1);

	stk_module_free_memory();
}

static void stk_set_fn_name(char *dst, const char *name)
{
	if (!name || (stk_flags & STK_FLAG_INITIALIZED))
		return;

	strncpy(dst, name, STK_MOD_FUNC_NAME_BUFFER - 1);
	dst[STK_MOD_FUNC_NAME_BUFFER - 1] = '\0';
}

void stk_set_module_init_fn(const char *name)
{
	stk_set_fn_name(stk_mod_init_name, name);
}

void stk_set_module_shutdown_fn(const char *name)
{
	stk_set_fn_name(stk_mod_shutdown_name, name);
}

void stk_set_module_name_fn(const char *name)
{
	stk_set_fn_name(stk_mod_name_fn, name);
}

void stk_set_module_version_fn(const char *name)
{
	stk_set_fn_name(stk_mod_version_fn, name);
}

void stk_set_module_description_fn(const char *name)
{
	stk_set_fn_name(stk_mod_description_fn, name);
}

void stk_set_module_deps_sym(const char *name)
{
	stk_set_fn_name(stk_mod_deps_sym, name);
}
