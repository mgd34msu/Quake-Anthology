# Quake Anthology (C port of quake-anthology-ts)
# GNU Make only; no CMake/Meson required. GPL-2.0-or-later.
CC ?= cc
AR ?= ar

CPPFLAGS += -Iinclude $(shell pkg-config --cflags sdl2 freetype2 vorbisfile theoradec)
# -ffp-contract=off preserves the staged binary32 rounding of the TypeScript
# Math.fround call sites bit for bit; do not add -ffast-math.
CFLAGS += -std=c17 -Wall -Wextra -Wpedantic -O2 -g -ffp-contract=off
LDLIBS += -lm -lz -lcurl -lcrypto $(shell pkg-config --libs sdl2 freetype2 vorbisfile theoradec gl)

BUILD := build

# Every ported source joins the archive automatically; compile breakage in a
# freshly ported module fails the build immediately.
LIB_SRCS := $(shell find src -name '*.c' ! -name 'main.c' | sort)
LIB_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(LIB_SRCS))
APP_SRCS := src/main.c
APP_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(APP_SRCS))

TEST_SRCS := $(shell find tests -name 'test_*.c' | sort)
TEST_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(TEST_SRCS))
TEST_BINS := $(patsubst tests/%.c,$(BUILD)/test-%,$(TEST_SRCS))
TEST_RUN := $(patsubst %,run-%,$(TEST_BINS))

BIN := $(BUILD)/quake-anthology
LIB := $(BUILD)/libquake-anthology.a

.PHONY: all test clean $(TEST_RUN)
all: $(BIN)

$(BIN): $(APP_OBJS) $(LIB)
	$(CC) $(CFLAGS) $(APP_OBJS) $(LIB) -o $@ $(LDLIBS)

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

# tests/<module>/test_<name>.c -> build/test-<module>/test_<name>
$(BUILD)/test-%: $(BUILD)/tests/%.o $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(LIB) -o $@ $(LDLIBS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

run-$(BUILD)/test-%: $(BUILD)/test-%
	./$(BUILD)/test-$*

test: $(TEST_BINS) $(BIN)
	@ulimit -c 0; for t in $(TEST_BINS); do echo "== $$t"; ./$$t || exit 1; done
	./$(BIN) --version
	./$(BIN) --help > /dev/null

clean:
	rm -rf $(BUILD)

-include $(LIB_OBJS:.o=.d) $(APP_OBJS:.o=.d) $(TEST_OBJS:.o=.d)
