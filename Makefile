# Quake Anthology (C port of quake-anthology-ts)
# GNU Make only; no CMake/Meson required. GPL-2.0-or-later.
CC ?= cc
AR ?= ar

CPPFLAGS += -Iinclude
# -ffp-contract=off preserves the staged binary32 rounding of the TypeScript
# Math.fround call sites bit for bit; do not add -ffast-math.
CFLAGS += -std=c17 -Wall -Wextra -Wpedantic -O2 -g -ffp-contract=off
LDLIBS += -lm

BUILD := build

FOUNDATION_SRCS := \
	src/foundation/arena.c \
	src/foundation/binary.c \
	src/foundation/bounded_queue.c \
	src/foundation/cleanup.c \
	src/foundation/event_ring.c \
	src/foundation/handles.c \
	src/foundation/id_index.c \
	src/foundation/pool.c \
	src/foundation/stamp.c \
	src/foundation/strings.c \
	src/foundation/support.c \
	src/foundation/sys_queue.c \
	src/foundation/vec.c

APP_SRCS := src/version.c src/main.c
LIB_SRCS := $(FOUNDATION_SRCS) src/version.c
LIB_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(LIB_SRCS))
APP_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(APP_SRCS))

TEST_FOUNDATION_SRCS := tests/foundation/test_foundation.c
TEST_FOUNDATION_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(TEST_FOUNDATION_SRCS))

BIN := $(BUILD)/quake-anthology
LIB := $(BUILD)/libquake-anthology.a
TEST_FOUNDATION := $(BUILD)/test-foundation

.PHONY: all test clean
all: $(BIN)

$(BIN): $(APP_OBJS) $(LIB)
	$(CC) $(CFLAGS) $(APP_OBJS) $(LIB) -o $@ $(LDLIBS)

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

$(TEST_FOUNDATION): $(TEST_FOUNDATION_OBJS) $(LIB)
	$(CC) $(CFLAGS) $(TEST_FOUNDATION_OBJS) $(LIB) -o $@ $(LDLIBS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

test: $(TEST_FOUNDATION) $(BIN)
	./$(TEST_FOUNDATION)
	./$(BIN) --version
	./$(BIN) --help > /dev/null

clean:
	rm -rf $(BUILD)

-include $(LIB_OBJS:.o=.d) $(APP_OBJS:.o=.d) $(TEST_FOUNDATION_OBJS:.o=.d)
