.include "config.mk"

FULL_LIB     = lib${LIB_NAME}.so
LDFLAGS_PLAT = -ldl
CFLAGS_PLAT  = -fPIC
CFLAGS_BASE  = -Wall -Wpedantic -I${INC_DIR} -std=c89 ${CFLAGS_PLAT}

.PHONY: all debug release clean

all: debug

OBJS_DEBUG   = ${SRCS:S/^src\//obj\/debug\//:S/.c$/.o/}
OBJS_RELEASE = ${SRCS:S/^src\//obj\/release\//:S/.c$/.o/}

debug: ${BIN_DIR}/debug/${FULL_LIB}
release: ${BIN_DIR}/release/${FULL_LIB}

${BIN_DIR}/debug/${FULL_LIB}: ${OBJS_DEBUG}
	@mkdir -p ${.TARGET:H}
	${CC} -shared -o ${.TARGET} ${.ALLSRC} ${LDFLAGS_PLAT}

${BIN_DIR}/release/${FULL_LIB}: ${OBJS_RELEASE}
	@mkdir -p ${.TARGET:H}
	${CC} -shared -o ${.TARGET} ${.ALLSRC} ${LDFLAGS_PLAT}

.for _src in ${SRCS}
obj/debug/${_src:T:R}.o: ${_src}
	@mkdir -p ${.TARGET:H}
	${CC} ${CFLAGS_BASE} -g -O0 -MMD -MP -c ${_src} -o ${.TARGET}

obj/release/${_src:T:R}.o: ${_src}
	@mkdir -p ${.TARGET:H}
	${CC} ${CFLAGS_BASE} -O2 -MMD -MP -c ${_src} -o ${.TARGET}
.endfor

.dinclude "obj/debug/*.d"
.dinclude "obj/release/*.d"

clean:
	rm -rf ${OBJ_DIR} ${BIN_DIR}
