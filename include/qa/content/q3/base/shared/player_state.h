/* Port of src/content/q3/base/shared/player-state.ts. GPL-2.0-or-later.
 * MoveFlags/CommandButtons/PlayerAnimation re-exports are the landed
 * qa/movement/q3/constants.h values. TS number formatting in error/debug
 * strings uses integer text for integral values below 1e21, %g otherwise. */
#ifndef QA_CONTENT_Q3_BASE_SHARED_PLAYER_STATE_H
#define QA_CONTENT_Q3_BASE_SHARED_PLAYER_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/contracts/math.h"
#include "qa/movement/q3/constants.h"

enum { QA_Q3_BASE_ENTITYNUM_WORLD = 1022, QA_Q3_BASE_ENTITYNUM_NONE = 1023 };

typedef struct qa_q3_base_user_command {
  double server_time; qa_vec3 angles; double buttons, weapon, forwardmove, rightmove, upmove;
} qa_q3_base_user_command;

/* TS PlayerSlotBinding: read false is TS undefined. index is a TS number. */
typedef struct qa_q3_base_player_slot_binding {
  void *ctx;
  bool (*read)(void *ctx, double index, int32_t *out);
  void (*write)(void *ctx, double index, int32_t value);
} qa_q3_base_player_slot_binding;

/* TS PlayerStateSlots over an Int32Array. Init with source != NULL borrows
 * it (not freed); otherwise owns a zeroed array. */
typedef struct qa_q3_base_player_state_slots {
  double length;
  int32_t *values; bool owns_values;
  bool has_binding; qa_q3_base_player_slot_binding binding;
  void *stored_ctx; void (*stored)(void *ctx, double index, double value);
} qa_q3_base_player_state_slots;
bool qa_q3_base_player_state_slots_init(qa_q3_base_player_state_slots *slots, double length,
    int32_t *source_values, size_t source_length, bool has_source,
    const qa_q3_base_player_slot_binding *binding,
    void *stored_ctx, void (*stored)(void *ctx, double index, double value));
void qa_q3_base_player_state_slots_destroy(qa_q3_base_player_state_slots *slots);
double qa_q3_base_player_state_slots_get(const qa_q3_base_player_state_slots *slots, double index);
void qa_q3_base_player_state_slots_set(qa_q3_base_player_state_slots *slots, double index, double value);
/* TS copy(): caller-owned array of (size_t)length, NULL only on OOM. */
int32_t *qa_q3_base_player_state_slots_copy(const qa_q3_base_player_state_slots *slots);

typedef struct qa_q3_base_predictable_event { double sequence, event, parameter; } qa_q3_base_predictable_event;
/* TS PredictableEventDebug. module_is_game selects "game" vs "cgame";
 * show_events returns a borrowed string. */
typedef struct qa_q3_base_predictable_event_debug {
  void *ctx; bool module_is_game;
  const char *(*show_events)(void *ctx);
  void (*print)(void *ctx, const char *message);
} qa_q3_base_predictable_event_debug;

/* TS PlayerAuthorityBinding. */
typedef struct qa_q3_base_player_authority_binding {
  void *ctx;
  qa_vec3 (*origin)(void *ctx); void (*set_origin)(void *ctx, qa_vec3 value);
  qa_vec3 (*velocity)(void *ctx); void (*set_velocity)(void *ctx, qa_vec3 value);
  qa_q3_base_player_slot_binding stats, ammo;
} qa_q3_base_player_authority_binding;

typedef enum qa_q3_base_copy_stores { QA_Q3_BASE_PRESERVE_AUTHORITY, QA_Q3_BASE_REPLACE_AUTHORITY } qa_q3_base_copy_stores;

/* TS PlayerStateRecord<number,number,number> = SourcePlayerState. Scalar
 * fields are raw TS numbers. origin/velocity/health go through accessors. */
typedef struct qa_q3_base_player_state_record {
  qa_q3_base_predictable_event_debug *event_debug; /* borrowed, nullable */
  double command_time, pm_type, bob_cycle, pm_flags, pm_time;
  qa_vec3 origin_value, velocity_value;
  double weapon_time, gravity, speed;
  qa_vec3 delta_angles;
  double ground_entity_num, legs_timer, legs_anim, torso_timer, torso_anim, movement_dir;
  qa_vec3 grapple_point;
  double e_flags, event_sequence;
  qa_q3_base_player_state_slots events, event_parms;
  double external_event, external_event_parm, external_event_time, client_num;
  double weapon, weapon_state;
  qa_vec3 viewangles;
  double viewheight, damage_event, damage_yaw, damage_pitch, damage_count;
  qa_q3_base_player_state_slots stats, persistant, powerups, ammo;
  double generic1, loop_sound, jumppad_ent, ping, pmove_framecount, jumppad_frame, entity_event_sequence;
  qa_q3_base_product state_product;
  const qa_q3_base_player_authority_binding *authority; /* borrowed, nullable */
} qa_q3_base_player_state_record;
typedef qa_q3_base_player_state_record qa_q3_base_source_player_state;
typedef qa_q3_base_player_state_record qa_q3_base_player_state;

bool qa_q3_base_player_state_record_init(qa_q3_base_player_state_record *ps, qa_q3_base_product product,
    double pm_type, double weapon, double weapon_state, const qa_q3_base_player_authority_binding *authority);
bool qa_q3_base_player_state_init(qa_q3_base_player_state *ps, qa_q3_base_product product,
    const qa_q3_base_player_authority_binding *authority);
void qa_q3_base_player_state_record_destroy(qa_q3_base_player_state_record *ps);
qa_q3_base_product qa_q3_base_player_state_record_product(const qa_q3_base_player_state_record *ps);
qa_vec3 qa_q3_base_player_state_record_origin(const qa_q3_base_player_state_record *ps);
void qa_q3_base_player_state_record_set_origin(qa_q3_base_player_state_record *ps, qa_vec3 value);
qa_vec3 qa_q3_base_player_state_record_velocity(const qa_q3_base_player_state_record *ps);
void qa_q3_base_player_state_record_set_velocity(qa_q3_base_player_state_record *ps, qa_vec3 value);
double qa_q3_base_player_state_record_health(const qa_q3_base_player_state_record *ps);
void qa_q3_base_player_state_record_set_health(qa_q3_base_player_state_record *ps, double value);
bool qa_q3_base_player_state_record_copy(qa_q3_base_player_state_record *out, const qa_q3_base_player_state_record *ps);
bool qa_q3_base_player_state_copy(qa_q3_base_player_state *out, const qa_q3_base_player_state *ps);
void qa_q3_base_player_state_record_copy_from(qa_q3_base_player_state_record *ps,
    const qa_q3_base_player_state_record *source, qa_q3_base_copy_stores stores);
void qa_q3_base_player_state_record_set_event_debug(qa_q3_base_player_state_record *ps,
    qa_q3_base_predictable_event_debug *debug);
/* TS addEvent(event, parameter = 0). */
qa_q3_base_predictable_event qa_q3_base_player_state_record_add_event(qa_q3_base_player_state_record *ps,
    double event, double parameter);
/* TS createPlayerState(product, authority = null). */
bool qa_q3_base_create_player_state(qa_q3_base_player_state *out, qa_q3_base_product product,
    const qa_q3_base_player_authority_binding *authority);
#endif
