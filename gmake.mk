include config.mk 

ifeq ($(OS),Windows_NT)
    SHELL := cmd.exe
    FULL_LIB := $(LIB_NAME).dll
    STATIC_LIB := lib$(LIB_NAME).a
    LDFLAGS_PLAT :=
    CFLAGS_PLAT :=
    CFLAGS_STATIC :=
    MKDIR = if not exist $(subst /,\,$(1)) mkdir $(subst /,\,$(1))
    RMDIR = if exist $(subst /,\,$(1)) rd /s /q $(subst /,\,$(1))
else
    FULL_LIB := lib$(LIB_NAME).so
    STATIC_LIB := lib$(LIB_NAME).a
    LDFLAGS_PLAT := -ldl
    CFLAGS_PLAT := -fPIC
    CFLAGS_STATIC :=
    MKDIR = mkdir -p $(1)
    RMDIR = rm -rf $(1)
endif

RELEASE_LDFLAGS := -s
CFLAGS_BASE := -Wall -Wpedantic -I$(INC_DIR) -std=c89 $(CFLAGS_PLAT) 

PREFIX ?= /usr
LIBDIR ?= $(PREFIX)/lib
INCDIR ?= $(PREFIX)/include

.PHONY: all debug release clean test install uninstall

all: debug

debug: $(BIN_DIR)/debug/$(FULL_LIB) $(BIN_DIR)/debug/$(STATIC_LIB)
release: $(BIN_DIR)/release/$(FULL_LIB) $(BIN_DIR)/release/$(STATIC_LIB)

# Debug Rules
$(BIN_DIR)/debug/$(FULL_LIB): $(SRCS:src/%.c=obj/debug/shared/%.o)
	@$(call MKDIR,$(@D))
	$(CC) -shared -o $@ $^ $(LDFLAGS_PLAT)

$(BIN_DIR)/debug/$(STATIC_LIB): $(SRCS:src/%.c=obj/debug/static/%.o)
	@$(call MKDIR,$(@D))
	ar rcs $@ $^

obj/debug/shared/%.o: src/%.c
	@$(call MKDIR,$(@D))
	$(CC) $(CFLAGS_BASE) -g -O0 -MMD -MP -c $< -o $@

obj/debug/static/%.o: src/%.c
	@$(call MKDIR,$(@D))
	$(CC) $(CFLAGS_BASE) $(CFLAGS_STATIC) -g -O0 -MMD -MP -c $< -o $@

# Release Rules
$(BIN_DIR)/release/$(FULL_LIB): $(SRCS:src/%.c=obj/release/shared/%.o)
	@$(call MKDIR,$(@D))
	$(CC) -shared $(RELEASE_LDFLAGS) -o $@ $^ $(LDFLAGS_PLAT)

$(BIN_DIR)/release/$(STATIC_LIB): $(SRCS:src/%.c=obj/release/static/%.o)
	@$(call MKDIR,$(@D))
	ar rcs $@ $^

obj/release/shared/%.o: src/%.c
	@$(call MKDIR,$(@D))
	$(CC) $(CFLAGS_BASE) -O2 -MMD -MP -c $< -o $@

obj/release/static/%.o: src/%.c
	@$(call MKDIR,$(@D))
	$(CC) $(CFLAGS_BASE) $(CFLAGS_STATIC) -O2 -MMD -MP -c $< -o $@

-include $(wildcard obj/debug/shared/*.d)
-include $(wildcard obj/debug/static/*.d)
-include $(wildcard obj/release/shared/*.d)
-include $(wildcard obj/release/static/*.d) 

clean: 
	@$(call RMDIR,$(OBJ_DIR))
	@$(call RMDIR,$(BIN_DIR))

test: debug
	@echo "=== Building and running stk tests ==="
	@$(MAKE) -C test -f gmake.mk

ifneq ($(OS),Windows_NT)
install:
	@test -f $(BIN_DIR)/release/$(FULL_LIB) || { echo "Run 'make -f gmake.mk release' before installing."; exit 1; }
	install -d $(LIBDIR) $(INCDIR)/stk
	install -m 755 $(BIN_DIR)/release/$(FULL_LIB) $(LIBDIR)/
	install -m 644 $(BIN_DIR)/release/$(STATIC_LIB) $(LIBDIR)/
	install -m 644 $(INC_DIR)/stk.h $(INCDIR)/stk/
	install -m 644 $(INC_DIR)/stk_version.h $(INCDIR)/stk/
	install -m 644 $(INC_DIR)/stk_log.h $(INCDIR)/stk/

uninstall:
	rm -f $(LIBDIR)/$(FULL_LIB)
	rm -f $(LIBDIR)/$(STATIC_LIB)
	rm -rf $(INCDIR)/stk
else
install:
	@echo "make install is not supported on Windows."
	@echo "Copy include/ directory contents to your_project/include/stk/"
	@echo "Copy bin/release/stk.dll to your project's lib directory."
	@echo "Copy bin/release/stk.lib to your project's lib directory."

uninstall:
	@echo "make uninstall is not supported on Windows."
endif
