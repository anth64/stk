#include <stdlib.h>

static void **stk_handles = NULL;
static void (**stk_inits)(void) = NULL;
static void (**stk_shutdowns)(void) = NULL;
static const char **stk_ids = NULL;

static size_t stk_module_count = 0;
static size_t stk_module_capacity = 0;
