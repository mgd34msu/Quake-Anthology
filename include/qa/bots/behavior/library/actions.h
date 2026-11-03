/*
 * Port of src/bots/behavior/library/actions.ts (botlib/be_ea.c).
 * Each client owns a 40-byte bot_input_t in one zeroed hunk allocation of the
 * bot memory, read and written little-endian as TS does through DataView.
 *
 * qa_bot_action_buffer_init is the constructor with maxClients null and the
 * default `new BotMemory()`; bind installs the TS host and shared memory. The
 * struct must not move while it owns its default memory.
 *
 * *_calls variants are the TS generators: lazily started, they validate and
 * format only on their first step. The plain variants run finishCalls over them.
 * Strings are bytes: 32000 is compared against bytes where TS compares UTF-16
 * units (equal for byte-valued text, the only text TS accepts). A TS code unit
 * above 0xff (RangeError from byteCString) cannot be expressed by char input.
 * GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_ACTIONS_H
#define QA_BOTS_BEHAVIOR_LIBRARY_ACTIONS_H
#include "qa/bots/behavior/library/call_steps.h"
#include "qa/bots/behavior/library/memory.h"
#include "qa/contracts/math.h"
#include "qa/persistence/value.h"
#include <stdbool.h>
#include <stdint.h>
#define QA_BOT_INPUT_BYTES 40
typedef enum qa_bot_action_flag {
  QA_BOT_ACTION_ATTACK = 0x1,
  QA_BOT_ACTION_USE = 0x2,
  QA_BOT_ACTION_RESPAWN = 0x8,
  QA_BOT_ACTION_JUMP = 0x10,
  QA_BOT_ACTION_MOVE_UP = 0x20,
  QA_BOT_ACTION_CROUCH = 0x80,
  QA_BOT_ACTION_MOVE_DOWN = 0x100,
  QA_BOT_ACTION_MOVE_FORWARD = 0x200,
  QA_BOT_ACTION_MOVE_BACK = 0x800,
  QA_BOT_ACTION_MOVE_LEFT = 0x1000,
  QA_BOT_ACTION_MOVE_RIGHT = 0x2000,
  QA_BOT_ACTION_DELAYED_JUMP = 0x8000,
  QA_BOT_ACTION_TALK = 0x10000,
  QA_BOT_ACTION_GESTURE = 0x20000,
  QA_BOT_ACTION_WALK = 0x80000,
  QA_BOT_ACTION_AFFIRMATIVE = 0x100000,
  QA_BOT_ACTION_NEGATIVE = 0x200000,
  QA_BOT_ACTION_GET_FLAG = 0x800000,
  QA_BOT_ACTION_GUARD_BASE = 0x1000000,
  QA_BOT_ACTION_PATROL = 0x2000000,
  QA_BOT_ACTION_FOLLOW_ME = (int)0x8000000
} qa_bot_action_flag;
/* TS BotInput: binary32 values widened on read. */
typedef struct qa_bot_input {
  float think_time;
  qa_vec3 direction;
  float speed;
  qa_vec3 view_angles;
  int32_t action_flags;
  int32_t weapon;
} qa_bot_input;
/* TS BotActionHost. `command` stays valid until the returned steps complete. */
typedef struct qa_bot_action_host {
  qa_call_steps (*client_command)(void *ctx, int64_t client, const char *command);
  void *ctx;
} qa_bot_action_host;
typedef struct qa_bot_action_buffer {
  bool has_inputs;
  qa_bot_memory_allocation allocation;
  int64_t client_capacity;
  qa_bot_action_host host;
  qa_bot_memory *memory;
  qa_bot_memory own_memory;
} qa_bot_action_buffer;
/* TS lazy vector argument, evaluated after the client's input resolves. */
typedef qa_vec3 (*qa_bot_action_vec3_fn)(void *ctx);

void qa_bot_action_buffer_init(qa_bot_action_buffer *b);
void qa_bot_action_buffer_destroy(qa_bot_action_buffer *b);
/* TS constructor host/memory arguments; memory NULL keeps the default owner. */
void qa_bot_action_buffer_bind(qa_bot_action_buffer *b, qa_bot_action_host host,
                               qa_bot_memory *memory);
void qa_bot_action_buffer_setup(qa_bot_action_buffer *b, int64_t max_clients);
void qa_bot_action_buffer_shutdown(qa_bot_action_buffer *b);
void qa_bot_action_buffer_dispose_resources(qa_bot_action_buffer *b);
int64_t qa_bot_action_buffer_get_max_clients(const qa_bot_action_buffer *b);
/* Owned tree; NULL only on OOM. */
qa_save_value *qa_bot_action_buffer_checkpoint(const qa_bot_action_buffer *b,
                                               qa_bot_memory_capture *capture);
void qa_bot_action_buffer_restore(qa_bot_action_buffer *b, const qa_save_value *value,
                                  qa_bot_memory_restored *restored);

void qa_bot_action_say(qa_bot_action_buffer *b, int64_t client, const char *text);
qa_call_steps qa_bot_action_say_calls(qa_bot_action_buffer *b, int64_t client, const char *text);
void qa_bot_action_say_team(qa_bot_action_buffer *b, int64_t client, const char *text);
qa_call_steps qa_bot_action_say_team_calls(qa_bot_action_buffer *b, int64_t client,
                                           const char *text);
void qa_bot_action_tell(qa_bot_action_buffer *b, int64_t client, int64_t recipient,
                        const char *text);
qa_call_steps qa_bot_action_tell_calls(qa_bot_action_buffer *b, int64_t client, int64_t recipient,
                                       const char *text);
void qa_bot_action_use_item(qa_bot_action_buffer *b, int64_t client, const char *item);
qa_call_steps qa_bot_action_use_item_calls(qa_bot_action_buffer *b, int64_t client,
                                           const char *item);
void qa_bot_action_drop_item(qa_bot_action_buffer *b, int64_t client, const char *item);
qa_call_steps qa_bot_action_drop_item_calls(qa_bot_action_buffer *b, int64_t client,
                                            const char *item);
void qa_bot_action_use_inventory(qa_bot_action_buffer *b, int64_t client, const char *inv);
qa_call_steps qa_bot_action_use_inventory_calls(qa_bot_action_buffer *b, int64_t client,
                                                const char *inv);
void qa_bot_action_drop_inventory(qa_bot_action_buffer *b, int64_t client, const char *inv);
qa_call_steps qa_bot_action_drop_inventory_calls(qa_bot_action_buffer *b, int64_t client,
                                                 const char *inv);
void qa_bot_action_command(qa_bot_action_buffer *b, int64_t client, const char *command);
qa_call_steps qa_bot_action_command_calls(qa_bot_action_buffer *b, int64_t client,
                                          const char *command);
/* TS private addFlag, kept public for the q3 travel callers. */
void qa_bot_action_add_flag(qa_bot_action_buffer *b, int64_t client, int32_t flag);
void qa_bot_action_gesture(qa_bot_action_buffer *b, int64_t client);
void qa_bot_action_talk(qa_bot_action_buffer *b, int64_t client);
void qa_bot_action_attack(qa_bot_action_buffer *b, int64_t client);
void qa_bot_action_use(qa_bot_action_buffer *b, int64_t client);
void qa_bot_action_respawn(qa_bot_action_buffer *b, int64_t client);
void qa_bot_action_crouch(qa_bot_action_buffer *b, int64_t client);
void qa_bot_action_walk(qa_bot_action_buffer *b, int64_t client);
void qa_bot_action_move_up(qa_bot_action_buffer *b, int64_t client);
void qa_bot_action_move_down(qa_bot_action_buffer *b, int64_t client);
void qa_bot_action_move_forward(qa_bot_action_buffer *b, int64_t client);
void qa_bot_action_move_back(qa_bot_action_buffer *b, int64_t client);
void qa_bot_action_move_left(qa_bot_action_buffer *b, int64_t client);
void qa_bot_action_move_right(qa_bot_action_buffer *b, int64_t client);
void qa_bot_action_action(qa_bot_action_buffer *b, int64_t client, int64_t flags);
void qa_bot_action_select_weapon(qa_bot_action_buffer *b, int64_t client, int64_t weapon);
void qa_bot_action_jump(qa_bot_action_buffer *b, int64_t client);
void qa_bot_action_delayed_jump(qa_bot_action_buffer *b, int64_t client);
void qa_bot_action_move(qa_bot_action_buffer *b, int64_t client, qa_vec3 direction, double speed);
void qa_bot_action_move_lazy(qa_bot_action_buffer *b, int64_t client, qa_bot_action_vec3_fn fn,
                             void *ctx, double speed);
void qa_bot_action_view(qa_bot_action_buffer *b, int64_t client, qa_vec3 angles);
void qa_bot_action_view_lazy(qa_bot_action_buffer *b, int64_t client, qa_bot_action_vec3_fn fn,
                             void *ctx);
/* The active source body is empty (TS keeps it as an explicit no-op). */
void qa_bot_action_end_regular(qa_bot_action_buffer *b, int64_t client, double think_time);
qa_bot_input qa_bot_action_get_input(qa_bot_action_buffer *b, int64_t client, double think_time);
/* EA_GetInput's memcpy source: 40 bytes borrowed from the allocation. */
const uint8_t *qa_bot_action_get_input_bytes(qa_bot_action_buffer *b, int64_t client,
                                             double think_time);
void qa_bot_action_reset_input(qa_bot_action_buffer *b, int64_t client);
#endif
