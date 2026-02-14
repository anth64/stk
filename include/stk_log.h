#ifndef STK_LOG_H
#define STK_LOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	STK_LOG_DEBUG,
	STK_LOG_INFO,
	STK_LOG_WARN,
	STK_LOG_ERROR
} stk_log_level_t;

void stk_set_log_output(FILE *fp);
void stk_set_log_prefix(const char *prefix);
void stk_set_log_level(stk_log_level_t min_level);

void stk_log(stk_log_level_t level, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* STK_LOG_H */
