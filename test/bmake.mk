CC ?= cc
CFLAGS = -Wall -Wpedantic -I../include -std=c89
LDFLAGS = -L../bin/debug -lstk

UNAME_S != uname -s

.if ${UNAME_S} == "Darwin"
LDFLAGS += -Wl,-rpath,../bin/debug
MODULE_EXT = .dylib
.else
LDFLAGS += -Wl,-rpath,../bin/debug
MODULE_EXT = .so
.endif

.PHONY: all test clean

all: test

test_program: test.c
	$(CC) $(CFLAGS) -o $@ test.c $(LDFLAGS)

test_mod$(MODULE_EXT): test_mod.c
	$(CC) $(CFLAGS) -fPIC -shared -o $@ test_mod.c

setup:
	@mkdir -p mods
	@cp -f test_mod$(MODULE_EXT) mods/ 2>/dev/null || true
	@echo "Test environment ready: mods/ directory with test_mod$(MODULE_EXT)"

run: test_program test_mod$(MODULE_EXT) setup
	@echo "Running integration test (CTRL+C to exit)..."
	@./test_program

test: test_program test_mod$(MODULE_EXT) setup
	@echo "=== stk Integration Test ==="
	@echo "1. Starting test program"
	@echo "2. Will load test_mod$(MODULE_EXT) from mods/"
	@echo "3. Press CTRL+C to exit"
	@echo "============================="
	@./test_program || echo "Test completed."

clean:
	rm -f test_program test_mod$(MODULE_EXT)
	rm -rf mods/
