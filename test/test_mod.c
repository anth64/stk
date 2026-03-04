#include <stdio.h>

int stk_mod_init(void)
{
	printf("test_mod initialized!\n");
	return 0;
}

void stk_mod_shutdown(void) { printf("test_mod shut down.\n"); }

const char *stk_mod_name(void) { return "Test Module"; }
const char *stk_mod_version(void) { return "1.0.0"; }
