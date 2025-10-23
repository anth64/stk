#include "stk.h"
#include "stk_log.h"

int stk_init(void)
{
	stk_log(stdout, "[stk] stk initialized v%s!", STK_VERSION_STRING);
	return 0;
}

int stk_shutdown(void)
{
	stk_log(stdout, "[stk] stk shutdown");
	return 0;
}
