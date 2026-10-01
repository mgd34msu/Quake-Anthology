#ifndef QA_BOT_AI_SOURCE_PLAYER_H
#define QA_BOT_AI_SOURCE_PLAYER_H
#include "internal.h"

/* Offsets are relative to the actual embedded 468-byte playerState_t. */
typedef enum bot_source_player_word {
    BOT_PS_COMMAND_TIME=0,BOT_PS_MOVE_TYPE=4,BOT_PS_BOB_CYCLE=8,
    BOT_PS_MOVE_FLAGS=12,BOT_PS_MOVE_TIME=16,BOT_PS_WEAPON_TIME=44,
    BOT_PS_GRAVITY=48,BOT_PS_SPEED=52,BOT_PS_GROUND_ENTITY=68,
    BOT_PS_LEGS_TIME=72,BOT_PS_LEGS_ANIMATION=76,BOT_PS_TORSO_TIME=80,
    BOT_PS_TORSO_ANIMATION=84,BOT_PS_MOVE_DIRECTION=88,BOT_PS_ENTITY_FLAGS=104,
    BOT_PS_EVENT_SEQUENCE=108,BOT_PS_EXTERNAL_EVENT=128,
    BOT_PS_EXTERNAL_EVENT_PARAMETER=132,BOT_PS_EXTERNAL_EVENT_TIME=136,
    BOT_PS_CLIENT=140,BOT_PS_WEAPON=144,BOT_PS_WEAPON_STATE=148,
    BOT_PS_VIEW_HEIGHT=164,BOT_PS_DAMAGE_EVENT=168,BOT_PS_DAMAGE_YAW=172,
    BOT_PS_DAMAGE_PITCH=176,BOT_PS_DAMAGE_COUNT=180,BOT_PS_GENERIC=440,
    BOT_PS_LOOP_SOUND=444,BOT_PS_JUMP_PAD=448,BOT_PS_PING=452,
    BOT_PS_MOVE_FRAME=456,BOT_PS_JUMP_PAD_FRAME=460,BOT_PS_ENTITY_EVENT_SEQUENCE=464
} bot_source_player_word;
typedef enum bot_source_player_vector {
    BOT_PS_ORIGIN=20,BOT_PS_VELOCITY=32,BOT_PS_GRAPPLE_POINT=92,BOT_PS_VIEW_ANGLES=152
} bot_source_player_vector;
typedef enum bot_source_player_array {
    BOT_PS_DELTA_ANGLES=56,BOT_PS_EVENTS=112,BOT_PS_EVENT_PARAMETERS=120,
    BOT_PS_STATS=184,BOT_PS_PERSISTENT=248,BOT_PS_POWERUPS=312,BOT_PS_AMMO=376
} bot_source_player_array;

/* Reads observe the same retained GAME bytes at every call. Enum qualification
 * happens only when that enum field is read, as in the source view getters. */
bool bot_ai_source_player_word(qa_bots *,bot_ai_state *,bot_source_player_word,int32_t *,qa_error *);
bool bot_ai_source_player_vector(qa_bots *,bot_ai_state *,bot_source_player_vector,qa_vec3 *,qa_error *);
bool bot_ai_source_player_slot(qa_bots *,bot_ai_state *,bot_source_player_array,int32_t,int32_t *,qa_error *);
/* Used at the actual BotAI player-copy stage; product identity stays typed. */
bool bot_ai_source_player_copy(qa_bots *,bot_ai_state *,const qa_q3_player *,qa_error *);
bool bot_ai_source_player_view(qa_bots *,bot_ai_state *,qa_bot_player_state_view *,qa_error *);
#endif
