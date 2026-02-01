CC := cc
CFLAGS = -Wall -Wpedantic -I../include -std=c89
LDFLAGS = -L../bin/debug -lstk

ifeq ($(OS),Windows_NT)
    MODULE_EXT = .dll
    EXE_EXT = .exe
else
    MODULE_EXT = .so
    EXE_EXT =
    LDFLAGS += -Wl,-rpath,../bin/debug
endif

.PHONY: all test clean

all: test

test_program$(EXE_EXT): test.c
	$(CC) $(CFLAGS) -o $@ test.c $(LDFLAGS)

test_mod$(MODULE_EXT): test_mod.c
	$(CC) $(CFLAGS) -fPIC -shared -o $@ test_mod.c

setup:
ifeq ($(OS),Windows_NT)
	@if not exist mods mkdir mods
	@if exist test_mod.dll copy /Y test_mod.dll mods\ >nul 2>&1
else
	@mkdir -p mods
	@cp -f test_mod.so mods/ 2>/dev/null || true
endif

test: test_program$(EXE_EXT) test_mod$(MODULE_EXT) setup
ifeq ($(OS),Windows_NT)
	@set PATH=../bin/debug;%PATH% && cmd /C "test_program.exe"
else
	@./test_program
endif

clean:
ifeq ($(OS),Windows_NT)
	@del /Q test_program.exe test_mod.dll 2>nul || true
	@rmdir /S /Q mods 2>nul || true
else
	@rm -f test_program test_mod.so
	@rm -rf mods
endif
