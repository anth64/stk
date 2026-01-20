include config.mk

ifeq ($(OS),Windows_NT)
    FULL_LIB := $(LIB_NAME).dll
else
    FULL_LIB := lib$(LIB_NAME).so
    LDFLAGS_PLAT := -ldl
    CFLAGS_PLAT := -fPIC
endif

CFLAGS_BASE := -Wall -Wpedantic -I$(INC_DIR) -std=c89 $(CFLAGS_PLAT)

.PHONY: all debug release clean

all: debug

debug: $(BIN_DIR)/debug/$(FULL_LIB)
release: $(BIN_DIR)/release/$(FULL_LIB)

# Debug Rules
$(BIN_DIR)/debug/$(FULL_LIB): $(SRCS:src/%.c=obj/debug/%.o)
	@mkdir -p $(@D)
	$(CC) -shared -o $@ $^ $(LDFLAGS_PLAT)

obj/debug/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -g -O0 -MMD -MP -c $< -o $@

# Release Rules
$(BIN_DIR)/release/$(FULL_LIB): $(SRCS:src/%.c=obj/release/%.o)
	@mkdir -p $(@D)
	$(CC) -shared -o $@ $^ $(LDFLAGS_PLAT)

obj/release/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -O2 -MMD -MP -c $< -o $@

-include $(wildcard obj/debug/*.d)
-include $(wildcard obj/release/*.d)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
