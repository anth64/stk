#ifndef STK_VERSION_H
#define STK_VERSION_H

#define STK_VERSION_MAJOR 0
#define STK_VERSION_MINOR 1
#define STK_VERSION_PATCH 0

#define STK_STRINGIFY_HELPER(x) #x
#define STK_STRINGIFY(x) STK_STRINGIFY_HELPER(x)

#define STK_VERSION_STRING                                                     \
	STK_STRINGIFY(STK_VERSION_MAJOR)                                       \
	"." STK_STRINGIFY(STK_VERSION_MINOR) "." STK_STRINGIFY(                \
	    STK_VERSION_PATCH)

#endif /* STK_VERSION_H */
