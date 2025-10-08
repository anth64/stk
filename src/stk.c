#include "stk.h"
#include <stdio.h>

int stk_init(void)
{
	printf("stk initialized v%s\n", STK_VERSION_STRING);
	return 0;
}

int stk_shutdown(void)
{
	printf("stk shutdown\n");
	return 0;
}
