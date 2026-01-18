#ifndef STK_H
#define STK_H

#include "stk_version.h"
#include <stdlib.h>

#define STK_MOD_DIR_BUFFER 256
#define STK_MOD_ID_BUFFER 64
#define STK_PATH_MAX 256
#define STK_PATH_MAX_OS 4096

#if defined(__linux__) || defined(_WIN32)
#define STK_EVENT_BUFFER 4096
#endif

#if defined(_WIN32)
#define STK_MODULE_EXT ".dll"
#elif defined(__APPLE__)
#define STK_MODULE_EXT ".dylib"
#else
#define STK_MODULE_EXT ".so"
#endif

#define STK_MODULE_EXT_LEN (sizeof(STK_MODULE_EXT) - 1)

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	STK_MOD_LOAD,
	STK_MOD_UNLOAD,
	STK_MOD_RELOAD
} stk_module_event_t;

int stk_init(const char *mod_dir, const char *tmp_dir);
void stk_shutdown(void);
size_t stk_module_count(void);
size_t stk_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* STK_H */
