.include "config.mk"

PREFIX      ?= /usr/local
LIBDIR      ?= ${PREFIX}/lib
INCDIR      ?= ${PREFIX}/include

UNAME_S != uname -s

.if ${UNAME_S} == "Darwin"
FULL_LIB     = lib${LIB_NAME}.dylib
.else
FULL_LIB     = lib${LIB_NAME}.so
.endif

STATIC_LIB   = lib${LIB_NAME}.a

LDFLAGS_PLAT = -ldl
CFLAGS_PLAT  = -fPIC
CFLAGS_BASE  = -Wall -Wpedantic -I${.CURDIR}/${INC_DIR} -std=c89 ${CFLAGS_PLAT}
CFLAGS_STATIC =

.PHONY: all debug release clean test install uninstall

all: debug

OBJS_DEBUG_SHARED   = ${SRCS:S|^src/|${.CURDIR}/obj/debug/shared/|:S/.c$/.o/}
OBJS_DEBUG_STATIC   = ${SRCS:S|^src/|${.CURDIR}/obj/debug/static/|:S/.c$/.o/}
OBJS_RELEASE_SHARED = ${SRCS:S|^src/|${.CURDIR}/obj/release/shared/|:S/.c$/.o/}
OBJS_RELEASE_STATIC = ${SRCS:S|^src/|${.CURDIR}/obj/release/static/|:S/.c$/.o/}

debug: ${.CURDIR}/${BIN_DIR}/debug/${FULL_LIB} ${.CURDIR}/${BIN_DIR}/debug/${STATIC_LIB}
release: ${.CURDIR}/${BIN_DIR}/release/${FULL_LIB} ${.CURDIR}/${BIN_DIR}/release/${STATIC_LIB}

${.CURDIR}/${BIN_DIR}/debug/${FULL_LIB}: ${OBJS_DEBUG_SHARED}
	@mkdir -p ${.TARGET:H}
	${CC} -shared -o ${.TARGET} ${.ALLSRC} ${LDFLAGS_PLAT}

${.CURDIR}/${BIN_DIR}/debug/${STATIC_LIB}: ${OBJS_DEBUG_STATIC}
	@mkdir -p ${.TARGET:H}
	ar rcs ${.TARGET} ${.ALLSRC}

${.CURDIR}/${BIN_DIR}/release/${FULL_LIB}: ${OBJS_RELEASE_SHARED}
	@mkdir -p ${.TARGET:H}
	${CC} -shared -s -o ${.TARGET} ${.ALLSRC} ${LDFLAGS_PLAT}

${.CURDIR}/${BIN_DIR}/release/${STATIC_LIB}: ${OBJS_RELEASE_STATIC}
	@mkdir -p ${.TARGET:H}
	ar rcs ${.TARGET} ${.ALLSRC}

.for _src in ${SRCS}
${.CURDIR}/obj/debug/shared/${_src:S|^src/||:S/.c$/.o/}: ${.CURDIR}/${_src}
	@mkdir -p ${.TARGET:H}
	${CC} ${CFLAGS_BASE} -g -O0 -MMD -MP -MT ${.TARGET} -MF ${.TARGET:S/.o$/.d/} -c ${.ALLSRC} -o ${.TARGET}

${.CURDIR}/obj/debug/static/${_src:S|^src/||:S/.c$/.o/}: ${.CURDIR}/${_src}
	@mkdir -p ${.TARGET:H}
	${CC} ${CFLAGS_BASE} ${CFLAGS_STATIC} -g -O0 -MMD -MP -MT ${.TARGET} -MF ${.TARGET:S/.o$/.d/} -c ${.ALLSRC} -o ${.TARGET}

${.CURDIR}/obj/release/shared/${_src:S|^src/||:S/.c$/.o/}: ${.CURDIR}/${_src}
	@mkdir -p ${.TARGET:H}
	${CC} ${CFLAGS_BASE} -O2 -MMD -MP -MT ${.TARGET} -MF ${.TARGET:S/.o$/.d/} -c ${.ALLSRC} -o ${.TARGET}

${.CURDIR}/obj/release/static/${_src:S|^src/||:S/.c$/.o/}: ${.CURDIR}/${_src}
	@mkdir -p ${.TARGET:H}
	${CC} ${CFLAGS_BASE} ${CFLAGS_STATIC} -O2 -MMD -MP -MT ${.TARGET} -MF ${.TARGET:S/.o$/.d/} -c ${.ALLSRC} -o ${.TARGET}
.endfor

clean:
	rm -rf ${.CURDIR}/${OBJ_DIR} ${.CURDIR}/${BIN_DIR}

test: debug
	@echo "=== Building and running stk tests ==="
	cd ${.CURDIR}/test && ${MAKE} -f bmake.mk

install:
	@test -f ${.CURDIR}/${BIN_DIR}/release/${FULL_LIB} || { echo "Run 'make -f bmake.mk release' before installing."; exit 1; }
	install -d ${LIBDIR} ${INCDIR}/stk
	install -m 755 ${.CURDIR}/${BIN_DIR}/release/${FULL_LIB} ${LIBDIR}/
	install -m 644 ${.CURDIR}/${BIN_DIR}/release/${STATIC_LIB} ${LIBDIR}/
	install -m 644 ${.CURDIR}/${INC_DIR}/stk.h ${INCDIR}/stk/
	install -m 644 ${.CURDIR}/${INC_DIR}/stk_version.h ${INCDIR}/stk/
	install -m 644 ${.CURDIR}/${INC_DIR}/stk_log.h ${INCDIR}/stk/

uninstall:
	rm -f ${LIBDIR}/${FULL_LIB}
	rm -f ${LIBDIR}/${STATIC_LIB}
	rm -rf ${INCDIR}/stk
