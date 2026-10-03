# Makefile for tinyadd (SPEC section 5).
#
#   make         build build/tinyadd
#   make test    build and run all tests
#   make debug   build with -g and AddressSanitizer + UBSan into build/debug/
#   make clean   remove everything under build/

CC ?= cc

BUILD_DIR   ?= build
MODE_CFLAGS ?= -O2
MODE_LDFLAGS ?=

WARN_FLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror
ALL_CFLAGS  = $(WARN_FLAGS) $(MODE_CFLAGS) $(CFLAGS) -Iinclude -MMD -MP
ALL_LDFLAGS = $(MODE_LDFLAGS) $(LDFLAGS)

SRCS      := $(wildcard src/*.c)
LIB_SRCS  := $(filter-out src/main.c,$(SRCS))
TEST_SRCS := $(wildcard tests/*.c)

LIB_OBJS  := $(LIB_SRCS:%.c=$(BUILD_DIR)/obj/%.o)
MAIN_OBJ  := $(BUILD_DIR)/obj/src/main.o
TEST_OBJS := $(TEST_SRCS:%.c=$(BUILD_DIR)/obj/%.o)

BIN      := $(BUILD_DIR)/tinyadd
TEST_BIN := $(BUILD_DIR)/test_runner

SANITIZE := -fsanitize=address,undefined

.PHONY: all test debug clean

all: $(BIN)

$(BIN): $(MAIN_OBJ) $(LIB_OBJS)
	$(CC) -o $@ $^ $(ALL_LDFLAGS)

$(TEST_BIN): $(TEST_OBJS) $(LIB_OBJS)
	$(CC) -o $@ $^ $(ALL_LDFLAGS)

test: $(BIN) $(TEST_BIN)
	./$(TEST_BIN)

debug:
	$(MAKE) BUILD_DIR=build/debug \
	        MODE_CFLAGS="-g -O0 -fno-omit-frame-pointer $(SANITIZE)" \
	        MODE_LDFLAGS="$(SANITIZE)" \
	        build/debug/tinyadd build/debug/test_runner

$(BUILD_DIR)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

clean:
	rm -rf build

-include $(LIB_OBJS:.o=.d) $(MAIN_OBJ:.o=.d) $(TEST_OBJS:.o=.d)
