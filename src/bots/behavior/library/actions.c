/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/actions.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_USER_MOVE 400
#define VA_BUFFER_SIZE 32000
#define JUMPED_LAST_FRAME 0x80
#define OFF_THINK_TIME 0
#define OFF_DIRECTION 4
#define OFF_SPEED 16
#define OFF_VIEW_ANGLES 20
#define OFF_ACTION_FLAGS 32
#define OFF_WEAPON 36

static uint32_t load_u32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void store_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}
static float load_f32(const uint8_t *p) {
  uint32_t bits = load_u32(p);
  float value;
  memcpy(&value, &bits, sizeof value);
  return value;
}
static void store_f32(uint8_t *p, double value) {
  float narrowed = (float)value;
  uint32_t bits;
  memcpy(&bits, &narrowed, sizeof bits);
  store_u32(p, bits);
}
static qa_vec3 load_vec3(const uint8_t *p) {
  qa_vec3 v = {load_f32(p), load_f32(p + 4), load_f32(p + 8)};
  return v;
}
static void store_vec3(uint8_t *p, qa_vec3 v) {
  store_f32(p, v.x);
  store_f32(p + 4, v.y);
  store_f32(p + 8, v.z);
}
static int32_t load_flags(const uint8_t *p) { return (int32_t)load_u32(p + OFF_ACTION_FLAGS); }
static void store_flags(uint8_t *p, int32_t value) {
  store_u32(p + OFF_ACTION_FLAGS, (uint32_t)value);
}

/* TS input(): resolving bytes first is the source pointer read that throws
 * once the allocation has been reset, ahead of any lazy vector argument. */
static uint8_t *input_bytes(qa_bot_action_buffer *b, int64_t client) {
  if (!b->has_inputs)
    qa_range_error("bot action input allocation has been shut down");
  if (client < 0 || client >= b->client_capacity)
    qa_range_error("bot action client %lld outside 0..%lld", (long long)client,
                   (long long)(b->client_capacity - 1));
  return qa_bot_memory_allocation_bytes(&b->allocation) + client * QA_BOT_INPUT_BYTES;
}

static int32_t source_int(int64_t value, const char *name) {
  if (value < INT32_MIN || value > INT32_MAX)
    qa_range_error("%s must be a signed 32-bit integer", name);
  return (int32_t)value;
}

static char *copy_text(const char *text) {
  size_t size = strlen(text) + 1;
  char *copy = malloc(size);
  if (copy == NULL)
    qa_range_error("out of memory");
  memcpy(copy, text, size);
  return copy;
}

void qa_bot_action_buffer_init(qa_bot_action_buffer *b) {
  memset(b, 0, sizeof *b);
  qa_bot_memory_init(&b->own_memory);
  b->memory = &b->own_memory;
}
void qa_bot_action_buffer_destroy(qa_bot_action_buffer *b) {
  b->has_inputs = false;
  /* A bound memory belongs to its owner and may already be gone. */
  qa_bot_memory_destroy(&b->own_memory);
}
void qa_bot_action_buffer_bind(qa_bot_action_buffer *b, qa_bot_action_host host,
                               qa_bot_memory *memory) {
  b->host = host;
  if (memory != NULL)
    b->memory = memory;
}
void qa_bot_action_buffer_setup(qa_bot_action_buffer *b, int64_t max_clients) {
  if (max_clients < 0 || max_clients * QA_BOT_INPUT_BYTES > 0x7fffffffLL)
    qa_range_error("bot action maxClients %lld exceeds the source signed allocation range",
                   (long long)max_clients);
  if (!qa_bot_memory_allocate(b->memory, max_clients * QA_BOT_INPUT_BYTES, QA_BOT_MEMORY_HUNK, true,
                              &b->allocation))
    qa_range_error("out of memory");
  b->has_inputs = true;
  b->client_capacity = max_clients;
}
void qa_bot_action_buffer_shutdown(qa_bot_action_buffer *b) {
  if (b->has_inputs) {
    qa_bot_memory_allocation allocation = b->allocation;
    qa_bot_memory_free(b->memory, &allocation);
  }
  b->has_inputs = false;
}
void qa_bot_action_buffer_dispose_resources(qa_bot_action_buffer *b) { b->has_inputs = false; }
int64_t qa_bot_action_buffer_get_max_clients(const qa_bot_action_buffer *b) {
  return b->client_capacity;
}

static qa_save_value *checked(qa_save_value *value) {
  if (value == NULL)
    qa_range_error("out of memory");
  return value;
}
qa_save_value *qa_bot_action_buffer_checkpoint(const qa_bot_action_buffer *b,
                                               qa_bot_memory_capture *capture) {
  qa_save_value *image = checked(qa_save_value_object());
  qa_bot_memory_allocation allocation = b->allocation;
  qa_save_value *reference =
      checked(b->has_inputs
                  ? qa_save_value_number((double)qa_bot_memory_capture_reference(capture, &allocation))
                  : qa_save_value_null());
  if (!qa_save_value_set(image, "capacity", checked(qa_save_value_number((double)b->client_capacity))) ||
      !qa_save_value_set(image, "allocation", reference))
    qa_range_error("out of memory");
  return image;
}
void qa_bot_action_buffer_restore(qa_bot_action_buffer *b, const qa_save_value *value,
                                  qa_bot_memory_restored *restored) {
  qa_save_reader reader, field;
  if (!qa_save_reader_init(&reader, value, "bot.actions") ||
      !qa_save_reader_field(&reader, "capacity", &field))
    qa_range_error("out of memory");
  double capacity = qa_save_reader_integer_min(&field, 0);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(&reader, "allocation", &field))
    qa_range_error("out of memory");
  bool has_allocation = !qa_save_reader_is_null(&field);
  double reference = has_allocation ? qa_save_reader_integer_min(&field, 0) : 0;
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&reader);
  if (b->has_inputs || capacity < 0 || capacity > (double)QA_MAX_SAFE_INTEGER)
    qa_range_error("Invalid bot action restoration");
  qa_bot_memory_allocation allocation = {NULL, 0, NULL};
  if (has_allocation) {
    allocation = qa_bot_memory_restored_allocation(restored, (int64_t)reference);
    if ((double)allocation.length != capacity * QA_BOT_INPUT_BYTES)
      qa_range_error("Saved bot action allocation size mismatch");
  }
  b->client_capacity = (int64_t)capacity;
  b->allocation = allocation;
  b->has_inputs = has_allocation;
}

/* One generator body: the TS body runs on the first step, then yield*s the host. */
typedef struct command_steps {
  qa_bot_action_buffer *buffer;
  int64_t client;
  const char *prefix;
  char *value;
  const char *name; /* NULL: the raw command, without the va buffer check */
  bool tell;
  int64_t recipient;
  bool started;
  char *command;
  qa_call_steps inner;
} command_steps;

static void command_steps_release(void *ctx) {
  command_steps *state = ctx;
  free(state->value);
  free(state->command);
  free(state);
}

static char *build_command(const command_steps *state) {
  char tell[64];
  const char *prefix = state->prefix;
  if (state->name == NULL)
    return copy_text(state->value);
  if (state->tell) {
    snprintf(tell, sizeof tell, "tell %d, ", source_int(state->recipient, "bot tell recipient"));
    prefix = tell;
  }
  size_t prefix_length = strlen(prefix), value_length = strlen(state->value);
  if (prefix_length + value_length >= VA_BUFFER_SIZE)
    qa_range_error("%s exceeds the 32000-byte botlib va buffer", state->name);
  char *command = malloc(prefix_length + value_length + 1);
  if (command == NULL)
    qa_range_error("out of memory");
  memcpy(command, prefix, prefix_length);
  memcpy(command + prefix_length, state->value, value_length + 1);
  return command;
}

static qa_call_step_result command_steps_next(void *ctx, const char *thrown) {
  command_steps *state = ctx;
  if (!state->started) {
    if (thrown != NULL) {
      qa_call_step_result step = {false, NULL, {NULL, NULL}, thrown};
      return step;
    }
    state->started = true;
    state->command = build_command(state);
    if (state->buffer->host.client_command == NULL)
      qa_range_error("bot action buffer has no client command host");
    state->inner = state->buffer->host.client_command(state->buffer->host.ctx, state->client,
                                                      state->command);
  }
  qa_call_step_result step = qa_call_steps_forward(&state->inner, thrown);
  if (step.done)
    step.value = NULL; /* CallSteps<undefined>: the body returns nothing. */
  return step;
}

static qa_call_steps command_calls(qa_bot_action_buffer *b, int64_t client, const char *prefix,
                                   const char *value, const char *name, bool tell,
                                   int64_t recipient) {
  command_steps *state = calloc(1, sizeof *state);
  if (state == NULL)
    qa_range_error("out of memory");
  state->buffer = b;
  state->client = client;
  state->prefix = prefix;
  state->value = copy_text(value);
  state->name = name;
  state->tell = tell;
  state->recipient = recipient;
  qa_call_steps steps = {command_steps_next, state, command_steps_release, NULL};
  return steps;
}

#define COMMAND_PAIR(fn, prefix, label)                                                            \
  qa_call_steps qa_bot_action_##fn##_calls(qa_bot_action_buffer *b, int64_t c, const char *t) {    \
    return command_calls(b, c, prefix, t, label, false, 0);                                        \
  }                                                                                                \
  void qa_bot_action_##fn(qa_bot_action_buffer *b, int64_t c, const char *t) {                     \
    qa_finish_calls(qa_bot_action_##fn##_calls(b, c, t));                                          \
  }
COMMAND_PAIR(say, "say ", "bot say text")
COMMAND_PAIR(say_team, "say_team ", "bot team say text")
COMMAND_PAIR(use_item, "use ", "bot item name")
COMMAND_PAIR(drop_item, "drop ", "bot item name")
COMMAND_PAIR(use_inventory, "invuse ", "bot inventory name")
COMMAND_PAIR(drop_inventory, "invdrop ", "bot inventory name")
COMMAND_PAIR(command, "", NULL)

qa_call_steps qa_bot_action_tell_calls(qa_bot_action_buffer *b, int64_t c, int64_t r,
                                       const char *t) {
  return command_calls(b, c, "", t, "bot tell text", true, r);
}
void qa_bot_action_tell(qa_bot_action_buffer *b, int64_t c, int64_t r, const char *t) {
  qa_finish_calls(qa_bot_action_tell_calls(b, c, r, t));
}

void qa_bot_action_add_flag(qa_bot_action_buffer *b, int64_t c, int32_t flag) {
  uint8_t *input = input_bytes(b, c);
  store_flags(input, load_flags(input) | flag);
}
#define FLAG_ACTION(fn, flag)                                                                      \
  void qa_bot_action_##fn(qa_bot_action_buffer *b, int64_t c) {                                    \
    qa_bot_action_add_flag(b, c, flag);                                                            \
  }
FLAG_ACTION(gesture, QA_BOT_ACTION_GESTURE)
FLAG_ACTION(talk, QA_BOT_ACTION_TALK)
FLAG_ACTION(attack, QA_BOT_ACTION_ATTACK)
FLAG_ACTION(use, QA_BOT_ACTION_USE)
FLAG_ACTION(respawn, QA_BOT_ACTION_RESPAWN)
FLAG_ACTION(crouch, QA_BOT_ACTION_CROUCH)
FLAG_ACTION(walk, QA_BOT_ACTION_WALK)
FLAG_ACTION(move_up, QA_BOT_ACTION_MOVE_UP)
FLAG_ACTION(move_down, QA_BOT_ACTION_MOVE_DOWN)
FLAG_ACTION(move_forward, QA_BOT_ACTION_MOVE_FORWARD)
FLAG_ACTION(move_back, QA_BOT_ACTION_MOVE_BACK)
FLAG_ACTION(move_left, QA_BOT_ACTION_MOVE_LEFT)
FLAG_ACTION(move_right, QA_BOT_ACTION_MOVE_RIGHT)

void qa_bot_action_action(qa_bot_action_buffer *b, int64_t c, int64_t flags) {
  uint8_t *input = input_bytes(b, c);
  store_flags(input, load_flags(input) | source_int(flags, "bot action flags"));
}
void qa_bot_action_select_weapon(qa_bot_action_buffer *b, int64_t c, int64_t weapon) {
  uint8_t *input = input_bytes(b, c);
  store_u32(input + OFF_WEAPON, (uint32_t)source_int(weapon, "bot weapon"));
}
static void edge_flag(qa_bot_action_buffer *b, int64_t c, int32_t flag) {
  uint8_t *input = input_bytes(b, c);
  int32_t flags = load_flags(input);
  store_flags(input, (flags & JUMPED_LAST_FRAME) != 0 ? flags & ~flag : flags | flag);
}
void qa_bot_action_jump(qa_bot_action_buffer *b, int64_t c) {
  edge_flag(b, c, QA_BOT_ACTION_JUMP);
}
void qa_bot_action_delayed_jump(qa_bot_action_buffer *b, int64_t c) {
  edge_flag(b, c, QA_BOT_ACTION_DELAYED_JUMP);
}
static void store_move(uint8_t *input, qa_vec3 direction, double speed) {
  store_vec3(input + OFF_DIRECTION, direction);
  float stored = (float)speed;
  store_f32(input + OFF_SPEED, stored > MAX_USER_MOVE    ? MAX_USER_MOVE
                               : stored < -MAX_USER_MOVE ? -MAX_USER_MOVE
                                                         : stored);
}
void qa_bot_action_move(qa_bot_action_buffer *b, int64_t c, qa_vec3 direction, double speed) {
  store_move(input_bytes(b, c), direction, speed);
}
void qa_bot_action_move_lazy(qa_bot_action_buffer *b, int64_t c, qa_bot_action_vec3_fn fn,
                             void *ctx, double speed) {
  uint8_t *input = input_bytes(b, c);
  store_move(input, fn(ctx), speed);
}
void qa_bot_action_view(qa_bot_action_buffer *b, int64_t c, qa_vec3 angles) {
  store_vec3(input_bytes(b, c) + OFF_VIEW_ANGLES, angles);
}
void qa_bot_action_view_lazy(qa_bot_action_buffer *b, int64_t c, qa_bot_action_vec3_fn fn,
                             void *ctx) {
  uint8_t *input = input_bytes(b, c);
  store_vec3(input + OFF_VIEW_ANGLES, fn(ctx));
}
void qa_bot_action_end_regular(qa_bot_action_buffer *b, int64_t c, double think_time) {
  (void)b;
  (void)c;
  (void)think_time;
}
qa_bot_input qa_bot_action_get_input(qa_bot_action_buffer *b, int64_t c, double think_time) {
  uint8_t *input = input_bytes(b, c);
  store_f32(input + OFF_THINK_TIME, think_time);
  qa_bot_input out = {.think_time = load_f32(input + OFF_THINK_TIME),
                      .direction = load_vec3(input + OFF_DIRECTION),
                      .speed = load_f32(input + OFF_SPEED),
                      .view_angles = load_vec3(input + OFF_VIEW_ANGLES),
                      .action_flags = load_flags(input),
                      .weapon = (int32_t)load_u32(input + OFF_WEAPON)};
  return out;
}
const uint8_t *qa_bot_action_get_input_bytes(qa_bot_action_buffer *b, int64_t c,
                                             double think_time) {
  uint8_t *input = input_bytes(b, c);
  store_f32(input + OFF_THINK_TIME, think_time);
  return input;
}
void qa_bot_action_reset_input(qa_bot_action_buffer *b, int64_t c) {
  uint8_t *input = input_bytes(b, c);
  store_flags(input, load_flags(input) & ~JUMPED_LAST_FRAME);
  store_f32(input + OFF_THINK_TIME, 0);
  qa_vec3 zero = {0, 0, 0};
  store_vec3(input + OFF_DIRECTION, zero);
  store_f32(input + OFF_SPEED, 0);
  bool jumped = (load_flags(input) & QA_BOT_ACTION_JUMP) != 0;
  store_flags(input, jumped ? JUMPED_LAST_FRAME : 0);
}
