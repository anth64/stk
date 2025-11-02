#ifndef STK_H
#define STK_H

#include "stk_version.h"
#include <stdlib.h>

#define MOD_DIR_BUFFER_SIZE 32

#ifdef __cplusplus
extern "C" {
#endif

int stk_init(const char *mod_dir);
int stk_shutdown(void);

size_t stk_module_count(void);
#ifdef __cplusplus
}
#endif

#endif /* STK_H */
