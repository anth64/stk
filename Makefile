# Directories
SRC_DIR := src
INC_DIR := include
OBJ_DIR := obj
BIN_DIR := bin

# Source files
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS_DEBUG := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/debug/%.o,$(SRCS))
OBJS_RELEASE := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/release/%.o,$(SRCS))

# Compiler and flags
CC := gcc
CFLAGS_DEBUG := -g -O0 -Wall -Wpedantic -I$(INC_DIR) -std=c89 -fPIC
CFLAGS_RELEASE := -O2 -Wall -Wpedantic -I$(INC_DIR) -std=c89 -fPIC

# Library name
LIB_NAME := libstk.so

# Default build
all: debug

# Debug / Release builds
debug: $(BIN_DIR)/debug/$(LIB_NAME)
release: $(BIN_DIR)/release/$(LIB_NAME)

# Build shared library
$(BIN_DIR)/debug/$(LIB_NAME): $(OBJS_DEBUG)
	@mkdir -p $(BIN_DIR)/debug
	$(CC) -shared -o $@ $^

$(BIN_DIR)/release/$(LIB_NAME): $(OBJS_RELEASE)
	@mkdir -p $(BIN_DIR)/release
	$(CC) -shared -o $@ $^

# Compile object files with header dependency tracking
$(OBJ_DIR)/debug/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)/debug
	$(CC) $(CFLAGS_DEBUG) -MMD -MP -c $< -o $@

$(OBJ_DIR)/release/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)/release
	$(CC) $(CFLAGS_RELEASE) -MMD -MP -c $< -o $@

# Include generated dependency files
-include $(OBJS_DEBUG:.o=.d)
-include $(OBJS_RELEASE:.o=.d)

# Clean
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all debug release clean

