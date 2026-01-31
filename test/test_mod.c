#include <stdio.h>

int stk_mod_init(void)
{
	printf("test mod initialized!\n");
	return 0;
}

void stk_mod_shutdown(void) { printf("test mod shut down.\n"); }
