include config.mk 

ifeq ($(OS),Windows_NT)
    # Force the shell to cmd.exe to avoid bash/sh interference
    SHELL := cmd.exe
    FULL_LIB := $(LIB_NAME).dll 
    LDFLAGS_PLAT := 
    CFLAGS_PLAT := 
    # Windows-safe directory creation: check existence, then create
    # Use 2>NUL to silence "directory already exists" warnings if any
    MKDIR = if not exist $(subst /,\,$(1)) mkdir $(subst /,\,$(1))
    RMDIR = if exist $(subst /,\,$(1)) rd /s /q $(subst /,\,$(1))
else
    FULL_LIB := lib$(LIB_NAME).so 
    LDFLAGS_PLAT := -ldl 
    CFLAGS_PLAT := -fPIC 
    MKDIR = mkdir -p $(1)
    RMDIR = rm -rf $(1)
endif

CFLAGS_BASE := -Wall -Wpedantic -I$(INC_DIR) -std=c89 $(CFLAGS_PLAT) 

.PHONY: all debug release clean 

all: debug 

debug: $(BIN_DIR)/debug/$(FULL_LIB) 
release: $(BIN_DIR)/release/$(FULL_LIB) 

# Debug Rules
$(BIN_DIR)/debug/$(FULL_LIB): $(SRCS:src/%.c=obj/debug/%.o) 
	@$(call MKDIR,$(@D))
	$(CC) -shared -o $@ $^ $(LDFLAGS_PLAT) 

obj/debug/%.o: src/%.c 
	@$(call MKDIR,$(@D))
	$(CC) $(CFLAGS_BASE) -g -O0 -MMD -MP -c $< -o $@ 

# Release Rules
$(BIN_DIR)/release/$(FULL_LIB): $(SRCS:src/%.c=obj/release/%.o) 
	@$(call MKDIR,$(@D))
	$(CC) -shared -o $@ $^ $(LDFLAGS_PLAT) 

obj/release/%.o: src/%.c 
	@$(call MKDIR,$(@D))
	$(CC) $(CFLAGS_BASE) -O2 -MMD -MP -c $< -o $@ 

-include $(wildcard obj/debug/*.d) 
-include $(wildcard obj/release/*.d) 

clean: 
	@$(call RMDIR,$(OBJ_DIR))
	@$(call RMDIR,$(BIN_DIR))