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

LDFLAGS_PLAT = -ldl
CFLAGS_PLAT  = -fPIC
CFLAGS_BASE  = -Wall -Wpedantic -I${.CURDIR}/${INC_DIR} -std=c89 ${CFLAGS_PLAT}

.PHONY: all debug release clean test install uninstall

all: debug

OBJS_DEBUG   = ${SRCS:S/^src\//obj\/debug\//:S/.c$/.o/}
OBJS_RELEASE = ${SRCS:S/^src\//obj\/release\//:S/.c$/.o/}

debug: ${BIN_DIR}/debug/${FULL_LIB}
release: ${BIN_DIR}/release/${FULL_LIB}

${BIN_DIR}/debug/${FULL_LIB}: ${OBJS_DEBUG}
	@mkdir -p ${.TARGET:H}
	${CC} -shared -o ${.TARGET} ${.ALLSRC} ${LDFLAGS_PLAT}

${BIN_DIR}/release/${FULL_LIB}: ${OBJS_RELEASE}
	@mkdir -p ${.CURDIR}/${BIN_DIR}/release
	${CC} -shared -s -o ${.CURDIR}/${BIN_DIR}/release/${FULL_LIB} ${.ALLSRC} ${LDFLAGS_PLAT}


.for _src in ${SRCS}
_obj_base = ${_src:S/^src\///:S/.c$/.o/}

obj/debug/${_obj_base}: ${_src}
	@mkdir -p ${.TARGET:H}
	${CC} ${CFLAGS_BASE} -g -O0 -MMD -MP -c ${.ALLSRC} -o ${.TARGET}

obj/release/${_obj_base}: ${_src}
	@mkdir -p ${.TARGET:H}
	${CC} ${CFLAGS_BASE} -O2 -MMD -MP -c ${.ALLSRC} -o ${.TARGET}
.endfor

.-include "obj/debug/*.d"
.-include "obj/release/*.d"

clean:
	rm -rf ${.CURDIR}/${OBJ_DIR} ${.CURDIR}/${BIN_DIR}

test: debug
	@echo "=== Building and running stk tests ==="
	cd ${.CURDIR}/test && ${MAKE} -f bmake.mk

install: release
	install -d ${LIBDIR} ${INCDIR}
	install -m 755 ${.CURDIR}/${BIN_DIR}/release/${FULL_LIB} ${LIBDIR}/
	install -m 644 ${.CURDIR}/${INC_DIR}/stk.h ${INCDIR}/

uninstall:
	rm -f ${LIBDIR}/${FULL_LIB}
	rm -f ${INCDIR}/stk.h
