/* Port of src/bots/behavior/library/actions.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_ACTIONS_H
#define QA_BOTS_BEHAVIOR_LIBRARY_ACTIONS_H
#include "qa/contracts/math.h"
#include <stdint.h>
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
typedef struct qa_bot_input {
  float think_time;
  qa_vec3 direction;
  float speed;
  qa_vec3 view_angles;
  int32_t action_flags;
  int32_t weapon;
} qa_bot_input;
typedef struct qa_bot_action_buffer {
  qa_bot_input *inputs;
  int64_t client_capacity;
  int has_inputs;
  void (*client_command)(void *ctx, int64_t client, const char *command);
  void *host_ctx;
} qa_bot_action_buffer;
void qa_bot_action_buffer_init(qa_bot_action_buffer *b);
void qa_bot_action_buffer_destroy(qa_bot_action_buffer *b);
void qa_bot_action_buffer_setup(qa_bot_action_buffer *b, int64_t max_clients);
void qa_bot_action_buffer_shutdown(qa_bot_action_buffer *b);
void qa_bot_action_buffer_dispose_resources(qa_bot_action_buffer *b);
int64_t qa_bot_action_buffer_get_max_clients(const qa_bot_action_buffer *b);
void qa_bot_action_say(qa_bot_action_buffer *b, int64_t client, const char *text);
void qa_bot_action_say_team(qa_bot_action_buffer *b, int64_t client, const char *text);
void qa_bot_action_tell(qa_bot_action_buffer *b, int64_t client, int64_t recipient,
                        const char *text);
void qa_bot_action_use_item(qa_bot_action_buffer *b, int64_t client, const char *item);
void qa_bot_action_drop_item(qa_bot_action_buffer *b, int64_t client, const char *item);
void qa_bot_action_use_inventory(qa_bot_action_buffer *b, int64_t client, const char *inv);
void qa_bot_action_drop_inventory(qa_bot_action_buffer *b, int64_t client, const char *inv);
void qa_bot_action_command(qa_bot_action_buffer *b, int64_t client, const char *command);
void qa_bot_action_add_flag(qa_bot_action_buffer *b, int64_t client, int32_t flag);
void qa_bot_action_action(qa_bot_action_buffer *b, int64_t client, int64_t flags);
void qa_bot_action_select_weapon(qa_bot_action_buffer *b, int64_t client, int64_t weapon);
void qa_bot_action_jump(qa_bot_action_buffer *b, int64_t client);
void qa_bot_action_delayed_jump(qa_bot_action_buffer *b, int64_t client);
void qa_bot_action_move(qa_bot_action_buffer *b, int64_t client, qa_vec3 direction, double speed);
void qa_bot_action_view(qa_bot_action_buffer *b, int64_t client, qa_vec3 angles);
void qa_bot_action_end_regular(qa_bot_action_buffer *b, int64_t client, double think_time);
qa_bot_input qa_bot_action_get_input(qa_bot_action_buffer *b, int64_t client, double think_time);
void qa_bot_action_reset_input(qa_bot_action_buffer *b, int64_t client);
#endif
