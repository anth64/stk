#include "stk_log.h"
#include "stk.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define STK_LOG_TIMESTAMP_BUFFER 32

extern unsigned char stk_flags;
void platform_get_timestamp(char *buffer, size_t size);

static FILE *log_output = NULL;
static char log_prefix[STK_LOG_PREFIX_BUFFER] = "stk";
static stk_log_level_t min_log_level = STK_LOG_INFO;

static const char *get_level_string(stk_log_level_t level)
{
	char *level_str = "";
	switch (level) {
	case STK_LOG_ERROR:
		level_str = "ERROR";
		break;
	case STK_LOG_WARN:
		level_str = "WARN";
		break;
	case STK_LOG_INFO:
		level_str = "INFO";
		break;
	case STK_LOG_DEBUG:
		level_str = "DEBUG";
		break;
	}

	return level_str;
}

void stk_set_log_output(FILE *fp)
{
	if (fp == NULL)
		stk_flags &= ~STK_FLAG_LOGGING_ENABLED;

	log_output = fp;
}

void stk_set_log_prefix(const char *prefix)
{
	if (!prefix) {
		log_prefix[0] = '\0';
		return;
	}

	strncpy(log_prefix, prefix, STK_LOG_PREFIX_BUFFER - 1);
	log_prefix[STK_LOG_PREFIX_BUFFER - 1] = '\0';
}

void stk_set_log_level(stk_log_level_t level) { min_log_level = level; }

void stk_log(stk_log_level_t level, const char *fmt, ...)
{
	FILE *output;
	const char *level_str;
	char timestamp[STK_LOG_TIMESTAMP_BUFFER];
	va_list args;

	if (!(stk_flags & STK_FLAG_LOGGING_ENABLED))
		return;

	if (level < min_log_level)
		return;

	output = log_output ? log_output : stdout;
	level_str = get_level_string(level);
	platform_get_timestamp(timestamp, sizeof(timestamp));

	if (log_prefix[0] != '\0')
		fprintf(output, "%s [%s] [%s] ", timestamp, log_prefix,
			level_str);
	else
		fprintf(output, "%s [%s] ", timestamp, level_str);

	va_start(args, fmt);
	vfprintf(output, fmt, args);
	va_end(args);

	fputc('\n', output);
}
