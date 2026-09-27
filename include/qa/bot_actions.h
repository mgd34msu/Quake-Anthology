#ifndef QA_BOT_ACTIONS_H
#define QA_BOT_ACTIONS_H

#include "qa/common.h"
#include "qa/movement.h"

typedef enum qa_bot_action_flag {
    QA_BOT_ATTACK = 0x0000001, QA_BOT_USE = 0x0000002, QA_BOT_RESPAWN = 0x0000008,
    QA_BOT_JUMP = 0x0000010, QA_BOT_MOVE_UP = 0x0000020, QA_BOT_CROUCH = 0x0000080,
    QA_BOT_MOVE_DOWN = 0x0000100, QA_BOT_MOVE_FORWARD = 0x0000200,
    QA_BOT_MOVE_BACK = 0x0000800, QA_BOT_MOVE_LEFT = 0x0001000, QA_BOT_MOVE_RIGHT = 0x0002000,
    QA_BOT_DELAYED_JUMP = 0x0008000, QA_BOT_TALK = 0x0010000, QA_BOT_GESTURE = 0x0020000,
    QA_BOT_WALK = 0x0080000, QA_BOT_AFFIRMATIVE = 0x0100000, QA_BOT_NEGATIVE = 0x0200000,
    QA_BOT_GET_FLAG = 0x0800000, QA_BOT_GUARD_BASE = 0x1000000, QA_BOT_PATROL = 0x2000000,
    QA_BOT_FOLLOW_ME = 0x8000000
} qa_bot_action_flag;
typedef struct qa_bot_input {
    float think_time;
    qa_vec3 direction;
    float speed;
    qa_vec3 view_angles;
    uint32_t action_flags;
    int32_t weapon;
} qa_bot_input;
typedef struct qa_bot_action_services {
    void *context;
    /* Text is borrowed for this synchronous call. Source client IDs are local
     * to this instance; the host resolves the canonical actor/connection. */
    bool (*command)(void *, int32_t client, const char *, qa_error *);
} qa_bot_action_services;
typedef struct qa_bot_actions qa_bot_actions;
bool qa_bot_actions_create(uint32_t clients, const qa_bot_action_services *, qa_bot_actions **,
                           qa_error *);
void qa_bot_actions_destroy(qa_bot_actions *);
uint32_t qa_bot_actions_capacity(const qa_bot_actions *);
/* Setup replaces and zeroes records only on successful allocation. Shutdown
 * releases records, preserving services for a subsequent setup. */
bool qa_bot_actions_setup(qa_bot_actions *, uint32_t clients, qa_error *);
void qa_bot_actions_shutdown(qa_bot_actions *);
bool qa_bot_actions_add(qa_bot_actions *, uint32_t client, uint32_t flags, qa_error *);
bool qa_bot_actions_weapon(qa_bot_actions *, uint32_t client, int32_t weapon, qa_error *);
bool qa_bot_actions_jump(qa_bot_actions *, uint32_t client, bool delayed, qa_error *);
bool qa_bot_actions_move(qa_bot_actions *, uint32_t client, qa_vec3 direction, float speed,
                         qa_error *);
bool qa_bot_actions_view(qa_bot_actions *, uint32_t client, qa_vec3 angles, qa_error *);
bool qa_bot_actions_input(qa_bot_actions *, uint32_t client, float think_time, qa_bot_input *,
                          qa_error *);
bool qa_bot_actions_read(const qa_bot_actions *, uint32_t client, qa_bot_input *, qa_error *);
bool qa_bot_actions_restore(qa_bot_actions *, uint32_t client, const qa_bot_input *, qa_error *);
bool qa_bot_actions_reset(qa_bot_actions *, uint32_t client, qa_error *);
/* The donor's EA_EndRegular body has no effects. */
void qa_bot_actions_end_regular(qa_bot_actions *, int32_t client, float think_time);
typedef enum qa_bot_text_action {
    QA_BOT_COMMAND, QA_BOT_SAY, QA_BOT_SAY_TEAM, QA_BOT_TELL,
    QA_BOT_USE_ITEM, QA_BOT_DROP_ITEM, QA_BOT_USE_INVENTORY, QA_BOT_DROP_INVENTORY
} qa_bot_text_action;
bool qa_bot_actions_text(qa_bot_actions *, int32_t client, qa_bot_text_action,
                         int32_t recipient, const char *text, qa_error *);

typedef struct qa_bot_view_state {
    qa_vec3 angles, ideal, velocity;
} qa_bot_view_state;
float qa_bot_angle_difference(float angle, float ideal);
float qa_bot_change_angle(float angle, float ideal, float speed);
void qa_bot_change_view(qa_bot_view_state *, float factor, float maximum_degrees_per_second,
                        float elapsed, bool challenge);
void qa_bot_view_delta(qa_bot_view_state *, const int32_t delta[3], bool add);
/* Produces the source Q3 user-command values in the common movement command.
 * The native controller adapts these values to its selected movement provider. */
bool qa_bot_input_q3_command(const qa_bot_input *, const int32_t delta_angles[3],
                             int32_t server_time_ms, qa_movement_command *, qa_error *);

#endif
