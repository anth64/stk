#ifndef STK_H
#define STK_H

#include "stk_version.h"
#include <stdlib.h>

#define MOD_DIR_BUFFER_SIZE 32
#define PATH_BUFFER_SIZE 1024

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { STK_MOD_LOAD, STK_MOD_UNLOAD } stk_module_event_t;

int stk_init(const char *mod_dir);
void stk_shutdown(void);
size_t stk_module_count(void);
size_t stk_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* STK_H */
