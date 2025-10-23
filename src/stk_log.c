#include "stk_log.h"
#include <stdarg.h>
#include <stdio.h>

void stk_log(FILE *fp, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	vfprintf(fp, fmt, args);
	fputc('\n', fp);

	va_end(args);
}
