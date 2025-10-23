#ifndef STK_H
#define STK_H

#include "stk_version.h"
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

int stk_init(void);
int stk_shutdown(void);

size_t stk_module_count(void);
#ifdef __cplusplus
}
#endif

#endif /* STK_H */
