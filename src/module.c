#include "stk.h"
#include <stdlib.h>

typedef void (*stk_module_func)(void);

static void **stk_handles = NULL;
static stk_module_func *stk_inits = NULL;
static stk_module_func *stk_shutdowns = NULL;

static size_t module_count = 0;

size_t stk_module_count(void) { return module_count; }
