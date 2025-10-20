#ifndef STK_LOG_H
#define STK_LOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void stk_log(FILE *fp, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* STK_LOG_H */
