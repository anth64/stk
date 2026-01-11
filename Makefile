SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin

CC = cc
CFLAGS_DEBUG = -g -O0 -Wall -Wpedantic -I$(INC_DIR) -std=c89 -fPIC
CFLAGS_RELEASE = -O2 -Wall -Wpedantic -I$(INC_DIR) -std=c89 -fPIC
LDFLAGS = -ldl

LIB_NAME = libstk.so

OBJS_DEBUG = \
	$(OBJ_DIR)/debug/module.o \
	$(OBJ_DIR)/debug/platform.o \
	$(OBJ_DIR)/debug/stk.o \
	$(OBJ_DIR)/debug/stk_log.o

OBJS_RELEASE = \
	$(OBJ_DIR)/release/module.o \
	$(OBJ_DIR)/release/platform.o \
	$(OBJ_DIR)/release/stk.o \
	$(OBJ_DIR)/release/stk_log.o

.PHONY: all debug release clean

all: debug

debug: $(BIN_DIR)/debug/$(LIB_NAME)

release: $(BIN_DIR)/release/$(LIB_NAME)

$(BIN_DIR)/debug/$(LIB_NAME): $(OBJS_DEBUG)
	@mkdir -p $(BIN_DIR)/debug
	$(CC) -shared -o $@ $(OBJS_DEBUG) $(LDFLAGS)

$(BIN_DIR)/release/$(LIB_NAME): $(OBJS_RELEASE)
	@mkdir -p $(BIN_DIR)/release
	$(CC) -shared -o $@ $(OBJS_RELEASE) $(LDFLAGS)

$(OBJ_DIR)/debug/module.o: $(SRC_DIR)/module.c
	@mkdir -p $(OBJ_DIR)/debug
	$(CC) $(CFLAGS_DEBUG) -c $(SRC_DIR)/module.c -o $@

$(OBJ_DIR)/debug/platform.o: $(SRC_DIR)/platform.c
	@mkdir -p $(OBJ_DIR)/debug
	$(CC) $(CFLAGS_DEBUG) -c $(SRC_DIR)/platform.c -o $@

$(OBJ_DIR)/debug/stk.o: $(SRC_DIR)/stk.c
	@mkdir -p $(OBJ_DIR)/debug
	$(CC) $(CFLAGS_DEBUG) -c $(SRC_DIR)/stk.c -o $@

$(OBJ_DIR)/debug/stk_log.o: $(SRC_DIR)/stk_log.c
	@mkdir -p $(OBJ_DIR)/debug
	$(CC) $(CFLAGS_DEBUG) -c $(SRC_DIR)/stk_log.c -o $@

$(OBJ_DIR)/release/module.o: $(SRC_DIR)/module.c
	@mkdir -p $(OBJ_DIR)/release
	$(CC) $(CFLAGS_RELEASE) -c $(SRC_DIR)/module.c -o $@

$(OBJ_DIR)/release/platform.o: $(SRC_DIR)/platform.c
	@mkdir -p $(OBJ_DIR)/release
	$(CC) $(CFLAGS_RELEASE) -c $(SRC_DIR)/platform.c -o $@

$(OBJ_DIR)/release/stk.o: $(SRC_DIR)/stk.c
	@mkdir -p $(OBJ_DIR)/release
	$(CC) $(CFLAGS_RELEASE) -c $(SRC_DIR)/stk.c -o $@

$(OBJ_DIR)/release/stk_log.o: $(SRC_DIR)/stk_log.c
	@mkdir -p $(OBJ_DIR)/release
	$(CC) $(CFLAGS_RELEASE) -c $(SRC_DIR)/stk_log.c -o $@

clean:
	@rm -rf $(OBJ_DIR) $(BIN_DIR)
