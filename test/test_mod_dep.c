#include <stdio.h>

typedef struct {
	char id[64];
	char version[32];
} dep_t;

int stk_mod_init(void)
{
	printf("test_mod_dep initialized!\n");
	return 0;
}

void stk_mod_shutdown(void) { printf("test_mod_dep shut down.\n"); }

const char *stk_mod_name(void) { return "Dependent Test Module"; }
const char *stk_mod_version(void) { return "1.0.0"; }

dep_t stk_mod_deps[] = {{"test_mod", ">=1.0.0"}, {"", ""}};
