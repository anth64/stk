#ifndef STK_PLATFORM_H
#define STK_PLATFORM_H

#ifdef _WIN32
#define STK_PATH_SEP '\\'
#define STK_PATH_SEP_STR "\\"
#else
#define STK_PATH_SEP '/'
#define STK_PATH_SEP_STR "/"
#endif

#endif /* STK_PLATFORM_H */
