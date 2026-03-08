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

static char (*stk_pending)[STK_PATH_MAX_OS] = NULL;
static size_t stk_pending_count = 0;

void stk_pending_free(void)
{
	if (stk_pending) {
		free(stk_pending);
		stk_pending = NULL;
	}

	stk_pending_count = 0;
}

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
	unsigned char result = STK_MOD_INIT_SUCCESS;

	for (i = 0; i < count; i++) {
		if (stk_modules[i].dep_count == 0)
			continue;

		for (d = 0; d < stk_modules[i].dep_count; d++) {
			found = is_mod_loaded(stk_modules[i].deps[d].id);
			if (found < 0) {
				stk_log(STK_LOG_ERROR,
					"Module '%s' requires '%s'",
					stk_modules[i].id,
					stk_modules[i].deps[d].id);
				result = STK_MOD_DEP_NOT_FOUND_ERROR;
				continue;
			}

			if (!stk_modules[i].deps[d].version[0])
				continue;

			if (!stk_validate_constraint(
				stk_modules[i].deps[d].version,
				stk_modules[found].version)) {
				stk_log(
				    STK_LOG_ERROR,
				    "Module '%s' requires '%s' %s but has %s",
				    stk_modules[i].id,
				    stk_modules[i].deps[d].id,
				    stk_modules[i].deps[d].version,
				    stk_modules[found].version);
				result = STK_MOD_DEP_VERSION_MISMATCH_ERROR;
			}
		}
	}

	return result;
}

typedef int (*stk_dep_query_fn)(size_t i, size_t j, void *ctx);

static unsigned char stk_kahn_sort(size_t count, size_t *order,
				   stk_dep_query_fn has_dep, void *ctx,
				   void (*on_cycle)(size_t i))
{
	size_t *in_degree = NULL;
	size_t *queue = NULL;
	size_t head, tail, sorted, i, j;
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
		for (j = 0; j < count; j++)
			if (i != j && has_dep(i, j, ctx))
				in_degree[i]++;

	head = tail = sorted = 0;

	for (i = 0; i < count; i++)
		if (in_degree[i] == 0)
			queue[tail++] = i;

	while (head < tail) {
		size_t mod = queue[head++];
		order[sorted++] = mod;

		for (i = 0; i < count; i++) {
			if (i == mod || !has_dep(i, mod, ctx))
				continue;
			if (--in_degree[i] == 0)
				queue[tail++] = i;
		}
	}

	if (sorted != count) {
		size_t k;
		int in_order;
		for (i = 0; i < count; i++) {
			in_order = 0;
			for (k = 0; k < sorted; k++) {
				if (order[k] == i) {
					in_order = 1;
					break;
				}
			}
			if (!in_order && on_cycle)
				on_cycle(i);
		}
		result = STK_MOD_DEP_CIRCULAR_ERROR;
	}

done:
	free(in_degree);
	free(queue);
	return result;
}

static int stk_loaded_has_dep(size_t i, size_t j, void *ctx)
{
	size_t d;
	(void)ctx;
	for (d = 0; d < stk_modules[i].dep_count; d++)
		if (is_mod_loaded(stk_modules[i].deps[d].id) == (int)j)
			return 1;
	return 0;
}

static void stk_log_cycle(size_t i)
{
	stk_log(STK_LOG_ERROR, "Circular dependency detected with %s",
		stk_modules[i].id);
}

unsigned char stk_topo_sort(size_t count, size_t *order)
{
	return stk_kahn_sort(count, order, stk_loaded_has_dep, NULL,
			     stk_log_cycle);
}

unsigned char stk_module_preload(const char *path, int index)
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

void stk_module_discard(size_t index)
{
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

unsigned char stk_module_activate(size_t index)
{
	if (stk_modules[index].init() != STK_MOD_INIT_SUCCESS) {
		stk_module_discard(index);
		return STK_MOD_INIT_FAILURE;
	}
	return STK_MOD_INIT_SUCCESS;
}

unsigned char stk_validate_dependencies_single(size_t index)
{
	size_t d;
	int found;

	if (stk_modules[index].dep_count == 0)
		return STK_MOD_INIT_SUCCESS;

	for (d = 0; d < stk_modules[index].dep_count; d++) {
		found = is_mod_loaded(stk_modules[index].deps[d].id);
		if (found < 0)
			return STK_MOD_DEP_NOT_FOUND_ERROR;
		if (stk_modules[index].deps[d].version[0] &&
		    !stk_validate_constraint(stk_modules[index].deps[d].version,
					     stk_modules[found].version))
			return STK_MOD_DEP_VERSION_MISMATCH_ERROR;
	}

	return STK_MOD_INIT_SUCCESS;
}

void stk_log_dependency_failures(size_t index, const char *action)
{
	char buf[STK_MOD_DEP_LOG_BUFFER];
	size_t d, pos, len;
	int found;
	int first = 1;

	if (stk_modules[index].dep_count == 0)
		return;

	pos = 0;
	for (d = 0; d < stk_modules[index].dep_count; d++) {
		found = is_mod_loaded(stk_modules[index].deps[d].id);

		if (found >= 0 &&
		    (!stk_modules[index].deps[d].version[0] ||
		     stk_validate_constraint(stk_modules[index].deps[d].version,
					     stk_modules[found].version)))
			continue;

		if (!first && pos < sizeof(buf) - 2) {
			buf[pos++] = ',';
			buf[pos++] = ' ';
		}
		first = 0;

		if (found < 0) {
			len = strlen(stk_modules[index].deps[d].id);
			if (pos + len + 12 < sizeof(buf)) {
				memcpy(buf + pos, stk_modules[index].deps[d].id,
				       len);
				pos += len;
				memcpy(buf + pos, " (not found)", 12);
				pos += 12;
			}
		} else {
			len = strlen(stk_modules[index].deps[d].id);
			if (pos + len < sizeof(buf)) {
				memcpy(buf + pos, stk_modules[index].deps[d].id,
				       len);
				pos += len;
			}
			if (pos < sizeof(buf) - 1) {
				buf[pos++] = ' ';
			}
			buf[pos++] = '(';
			len = strlen("requires ");
			if (pos + len < sizeof(buf)) {
				memcpy(buf + pos, "requires ", len);
				pos += len;
			}
			len = strlen(stk_modules[index].deps[d].version);
			if (pos + len < sizeof(buf)) {
				memcpy(buf + pos,
				       stk_modules[index].deps[d].version, len);
				pos += len;
			}
			len = strlen(", have ");
			if (pos + len < sizeof(buf)) {
				memcpy(buf + pos, ", have ", len);
				pos += len;
			}
			len = strlen(stk_modules[found].version);
			if (pos + len < sizeof(buf)) {
				memcpy(buf + pos, stk_modules[found].version,
				       len);
				pos += len;
			}
			if (pos < sizeof(buf) - 1)
				buf[pos++] = ')';
		}
	}

	if (first)
		return;

	buf[pos] = '\0';
	stk_log(STK_LOG_WARN, "%s '%s': unmet deps: %s", action,
		stk_modules[index].id, buf);
}

unsigned char stk_module_load(const char *path, int index)
{
	unsigned char result;

	result = stk_module_preload(path, index);
	if (result != STK_MOD_INIT_SUCCESS)
		return result;

	result = stk_validate_dependencies_single(index);
	if (result != STK_MOD_INIT_SUCCESS) {
		stk_log_dependency_failures(index, "Deferring");
		stk_module_discard(index);
		return result;
	}

	return stk_module_activate(index);
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
	stk_pending_free();
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

typedef struct {
	int *file_indices;
	char (*file_names)[STK_PATH_MAX];
	const char *tmp_dir;
} stk_batch_dep_ctx_t;

static int stk_batch_has_dep(size_t i, size_t j, void *ctx)
{
	stk_batch_dep_ctx_t *c = (stk_batch_dep_ctx_t *)ctx;
	char id_j[STK_MOD_ID_BUFFER];
	char tmp_i[STK_PATH_MAX_OS];
	void *h;
	const stk_dep_t *deps;
	size_t d, dep_count;
	union {
		void *obj;
	} u;
	int result = 0;

	extract_module_id(c->file_names[c->file_indices[j]], id_j);

	tmp_i[0] = '\0';
	strncat(tmp_i, c->tmp_dir, STK_PATH_MAX_OS - 1);
	strncat(tmp_i, STK_PATH_SEP_STR, STK_PATH_MAX_OS - strlen(tmp_i) - 1);
	strncat(tmp_i, c->file_names[c->file_indices[i]],
		STK_PATH_MAX_OS - strlen(tmp_i) - 1);

	h = platform_load_library(tmp_i);
	if (!h)
		return 0;

	u.obj = platform_get_symbol(h, stk_mod_deps_sym);
	if (u.obj) {
		deps = (const stk_dep_t *)u.obj;
		dep_count = 0;
		while (deps[dep_count].id[0] != '\0')
			dep_count++;
		for (d = 0; d < dep_count; d++) {
			if (strncmp(deps[d].id, id_j, STK_MOD_ID_BUFFER) == 0) {
				result = 1;
				break;
			}
		}
	}

	platform_unload_library(h);
	return result;
}

void stk_sort_load_order(int *file_indices, size_t n,
			 char (*file_names)[STK_PATH_MAX], const char *tmp_dir)
{
	size_t *order = NULL;
	int *result = NULL;
	stk_batch_dep_ctx_t ctx;
	size_t i;

	if (n <= 1)
		return;

	order = malloc(n * sizeof(size_t));
	result = malloc(n * sizeof(int));
	if (!order || !result)
		goto cleanup;

	ctx.file_indices = file_indices;
	ctx.file_names = file_names;
	ctx.tmp_dir = tmp_dir;

	if (stk_kahn_sort(n, order, stk_batch_has_dep, &ctx, NULL) !=
	    STK_MOD_INIT_SUCCESS)
		goto cleanup;

	for (i = 0; i < n; i++)
		result[i] = file_indices[order[i]];
	for (i = 0; i < n; i++)
		file_indices[i] = result[i];

cleanup:
	free(order);
	free(result);
}

void stk_collect_dependents(size_t *indices, size_t *count, size_t capacity)
{
	size_t i, d;
	int in_set, changed;

	do {
		changed = 0;
		for (i = 0; i < module_count; i++) {
			in_set = 0;
			{
				size_t k;
				for (k = 0; k < *count; k++) {
					if (indices[k] == i) {
						in_set = 1;
						break;
					}
				}
			}
			if (in_set)
				continue;

			for (d = 0; d < stk_modules[i].dep_count; d++) {
				int dep_index =
				    is_mod_loaded(stk_modules[i].deps[d].id);
				if (dep_index < 0)
					continue;
				{
					size_t k;
					for (k = 0; k < *count; k++) {
						if (indices[k] ==
						    (size_t)dep_index) {
							if (*count >= capacity)
								goto next_module;
							indices[(*count)++] = i;
							changed = 1;
							goto next_module;
						}
					}
				}
			}
		next_module:;
		}
	} while (changed);
}

void stk_sort_unload_order(size_t *indices, size_t n)
{
	size_t *topo = NULL;
	size_t *result = NULL;
	size_t i, j, k;
	int in_set;

	if (n <= 1)
		return;

	topo = malloc(module_count * sizeof(size_t));
	result = malloc(n * sizeof(size_t));

	if (!topo || !result)
		goto fallback;

	if (stk_topo_sort(module_count, topo) != STK_MOD_INIT_SUCCESS)
		goto fallback;

	k = 0;
	for (i = module_count; i > 0; --i) {
		size_t mod = topo[i - 1];
		in_set = 0;
		for (j = 0; j < n; j++) {
			if (indices[j] == mod) {
				in_set = 1;
				break;
			}
		}
		if (in_set)
			result[k++] = mod;
	}

	if (k == n) {
		for (i = 0; i < n; i++)
			indices[i] = result[i];
	}

	free(topo);
	free(result);
	return;

fallback:
	free(topo);
	free(result);
	for (i = 0; i < n / 2; i++) {
		size_t tmp = indices[i];
		indices[i] = indices[n - 1 - i];
		indices[n - 1 - i] = tmp;
	}
}

void stk_module_unload_all(void)
{
	size_t i;
	size_t *order = NULL;

	if (module_count == 0)
		goto free_mem;

	order = malloc(module_count * sizeof(size_t));
	if (order) {
		for (i = 0; i < module_count; i++)
			order[i] = i;
		stk_sort_unload_order(order, module_count);
		for (i = 0; i < module_count; i++)
			stk_module_unload(order[i]);
		free(order);
	} else {
		for (i = module_count; i > 0; --i)
			stk_module_unload(i - 1);
	}

free_mem:
	stk_module_free_memory();
}

void stk_pending_add(const char *path)
{
	char (*new_pending)[STK_PATH_MAX_OS];
	char incoming_id[STK_MOD_ID_BUFFER];
	char existing_id[STK_MOD_ID_BUFFER];
	size_t i;

	extract_module_id(path, incoming_id);

	for (i = 0; i < stk_pending_count; i++) {
		extract_module_id(stk_pending[i], existing_id);
		if (strncmp(existing_id, incoming_id, STK_MOD_ID_BUFFER) == 0) {
			strncpy(stk_pending[i], path, STK_PATH_MAX_OS - 1);
			stk_pending[i][STK_PATH_MAX_OS - 1] = '\0';
			return;
		}
	}

	new_pending = malloc((stk_pending_count + 1) * sizeof(*stk_pending));
	if (!new_pending)
		return;

	for (i = 0; i < stk_pending_count; i++)
		memcpy(new_pending[i], stk_pending[i], STK_PATH_MAX_OS);

	free(stk_pending);
	stk_pending = new_pending;

	strncpy(stk_pending[stk_pending_count], path, STK_PATH_MAX_OS - 1);
	stk_pending[stk_pending_count][STK_PATH_MAX_OS - 1] = '\0';
	stk_pending_count++;
}

void stk_pending_add_batch(const char (*paths)[STK_PATH_MAX_OS], size_t count)
{
	char (*new_pending)[STK_PATH_MAX_OS];
	char incoming_id[STK_MOD_ID_BUFFER];
	char existing_id[STK_MOD_ID_BUFFER];
	size_t i, j, new_count;

	if (!paths || count == 0)
		return;

	new_count = 0;
	for (i = 0; i < count; i++) {
		int found = 0;
		extract_module_id(paths[i], incoming_id);

		for (j = 0; j < stk_pending_count; j++) {
			extract_module_id(stk_pending[j], existing_id);
			if (strncmp(existing_id, incoming_id,
				    STK_MOD_ID_BUFFER) == 0) {
				strncpy(stk_pending[j], paths[i],
					STK_PATH_MAX_OS - 1);
				stk_pending[j][STK_PATH_MAX_OS - 1] = '\0';
				found = 1;
				break;
			}
		}

		if (!found)
			new_count++;
	}

	if (new_count == 0)
		return;

	new_pending =
	    malloc((stk_pending_count + new_count) * sizeof(*stk_pending));
	if (!new_pending)
		return;

	for (i = 0; i < stk_pending_count; i++)
		memcpy(new_pending[i], stk_pending[i], STK_PATH_MAX_OS);

	free(stk_pending);
	stk_pending = new_pending;

	for (i = 0; i < count; i++) {
		int found = 0;
		extract_module_id(paths[i], incoming_id);

		for (j = 0; j < stk_pending_count; j++) {
			extract_module_id(stk_pending[j], existing_id);
			if (strncmp(existing_id, incoming_id,
				    STK_MOD_ID_BUFFER) == 0) {
				found = 1;
				break;
			}
		}

		if (!found) {
			strncpy(stk_pending[stk_pending_count], paths[i],
				STK_PATH_MAX_OS - 1);
			stk_pending[stk_pending_count][STK_PATH_MAX_OS - 1] =
			    '\0';
			stk_pending_count++;
		}
	}
}

void stk_pending_remove(const char *id)
{
	size_t i, write;
	char pending_id[STK_MOD_ID_BUFFER];

	if (!stk_pending_count)
		return;

	write = 0;
	for (i = 0; i < stk_pending_count; i++) {
		extract_module_id(stk_pending[i], pending_id);
		if (strncmp(pending_id, id, STK_MOD_ID_BUFFER) == 0)
			continue;
		if (write != i)
			memcpy(stk_pending[write], stk_pending[i],
			       STK_PATH_MAX_OS);
		write++;
	}
	stk_pending_count = write;
}

size_t stk_pending_retry(void)
{
	size_t i, d, loaded = 0;
	unsigned char deps_satisfied;
	unsigned char result;
	void *handle;
	void *test;
	union {
		void *obj;
		const char *(*meta_func)(void);
	} u;
	const stk_dep_t *deps;
	size_t dep_count;
	int found;
	size_t write;
	char pending_id[STK_MOD_ID_BUFFER];

	if (!stk_pending_count)
		return 0;

	write = 0;
	for (i = 0; i < stk_pending_count; i++) {
		test = platform_load_library(stk_pending[i]);
		if (test) {
			platform_unload_library(test);
			if (write != i)
				memcpy(stk_pending[write], stk_pending[i],
				       STK_PATH_MAX_OS);
			write++;
		}
	}
	stk_pending_count = write;

	if (!stk_pending_count) {
		stk_pending_free();
		return 0;
	}

	if (stk_module_realloc_memory(module_count + stk_pending_count) != 0)
		return 0;

	for (i = 0; i < stk_pending_count; i++) {
		extract_module_id(stk_pending[i], pending_id);
		if (is_mod_loaded(pending_id) >= 0) {
			memcpy(stk_pending[i],
			       stk_pending[stk_pending_count - 1],
			       STK_PATH_MAX_OS);
			stk_pending_count--;
			i--;
			continue;
		}

		handle = platform_load_library(stk_pending[i]);
		if (!handle)
			continue;

		u.obj = platform_get_symbol(handle, stk_mod_deps_sym);
		if (!u.obj) {
			platform_unload_library(handle);
			goto attempt_load;
		}

		deps = (const stk_dep_t *)u.obj;
		dep_count = 0;
		while (deps[dep_count].id[0] != '\0')
			dep_count++;

		deps_satisfied = 1;
		for (d = 0; d < dep_count; d++) {
			found = is_mod_loaded(deps[d].id);
			if (found < 0) {
				deps_satisfied = 0;
				break;
			}
			if (deps[d].version[0] &&
			    !stk_validate_constraint(
				deps[d].version, stk_modules[found].version)) {
				deps_satisfied = 0;
				break;
			}
		}

		platform_unload_library(handle);

		if (!deps_satisfied)
			continue;

	attempt_load:
		result = stk_module_load(stk_pending[i], module_count);
		if (result != STK_MOD_INIT_SUCCESS)
			continue;

		module_count++;
		loaded++;

		memcpy(stk_pending[i], stk_pending[stk_pending_count - 1],
		       STK_PATH_MAX_OS);
		stk_pending_count--;
		i--;
	}

	if (stk_pending_count == 0)
		stk_pending_free();

	if (loaded > 0)
		stk_module_realloc_memory(module_count);

	return loaded;
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
