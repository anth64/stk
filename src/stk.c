#include "stk.h"
#include "stk_log.h"

extern char stk_mod_dir[MOD_DIR_BUFFER_SIZE];

int stk_init(const char *mod_dir)
{
	stk_log(stdout, "[stk] stk initialized v%s!", STK_VERSION_STRING);
	return 0;
}

int stk_shutdown(void)
{
	stk_log(stdout, "[stk] stk shutdown");
	return 0;
}
