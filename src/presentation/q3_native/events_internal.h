#ifndef QA_Q3_NATIVE_EVENTS_INTERNAL_H
#define QA_Q3_NATIVE_EVENTS_INTERNAL_H
#include "events.h"
#include "marks.h"
#include "trajectory.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum { Q3N_LOCAL_CAPACITY = 512, Q3N_MARK_CAPACITY = 256, Q3N_MARK_VERTICES = 10 };
typedef struct q3n_local_slot {
    int32_t prev, next;
    bool active, present;
    q3n_local_entity value;
} q3n_local_slot;
typedef struct q3n_stored_mark {
    int32_t time, shader;
    bool alpha_fade;
    float color[4];
    uint32_t count;
    qa_q3_poly_vertex vertices[Q3N_MARK_VERTICES];
} q3n_stored_mark;
struct q3n_events {
    q3n_event_options options;
    q3n_event_state state;
    uint32_t seed, smoke_seed;
    qa_vec3 last_score_position;
    q3n_local_slot locals[Q3N_LOCAL_CAPACITY];
    int32_t local_head, local_tail, local_free;
    uint32_t local_count, mark_count;
    q3n_stored_mark marks[Q3N_MARK_CAPACITY];
    int32_t sound_buffer[20], sound_in, sound_out, sound_time;
    bool standalone_effects, remote_source, busy;
};
enum q3n_source_event {
    Q3N_EV_NONE, Q3N_EV_FOOTSTEP, Q3N_EV_FOOTSTEP_METAL, Q3N_EV_FOOTSPLASH,
    Q3N_EV_FOOTWADE, Q3N_EV_SWIM, Q3N_EV_STEP4, Q3N_EV_STEP8, Q3N_EV_STEP12, Q3N_EV_STEP16,
    Q3N_EV_FALL_SHORT, Q3N_EV_FALL_MEDIUM, Q3N_EV_FALL_FAR, Q3N_EV_JUMP_PAD,
    Q3N_EV_JUMP, Q3N_EV_WATER_TOUCH, Q3N_EV_WATER_LEAVE, Q3N_EV_WATER_UNDER, Q3N_EV_WATER_CLEAR,
    Q3N_EV_ITEM_PICKUP, Q3N_EV_GLOBAL_ITEM_PICKUP, Q3N_EV_NOAMMO, Q3N_EV_CHANGE_WEAPON,
    Q3N_EV_FIRE_WEAPON, Q3N_EV_USE_ITEM0, Q3N_EV_USE_ITEM1, Q3N_EV_USE_ITEM2, Q3N_EV_USE_ITEM3,
    Q3N_EV_USE_ITEM4, Q3N_EV_USE_ITEM5, Q3N_EV_USE_ITEM6, Q3N_EV_USE_ITEM7, Q3N_EV_USE_ITEM8,
    Q3N_EV_USE_ITEM9, Q3N_EV_USE_ITEM10, Q3N_EV_USE_ITEM11, Q3N_EV_USE_ITEM12,
    Q3N_EV_USE_ITEM13, Q3N_EV_USE_ITEM14, Q3N_EV_USE_ITEM15, Q3N_EV_ITEM_RESPAWN, Q3N_EV_ITEM_POP,
    Q3N_EV_TELEPORT_IN, Q3N_EV_TELEPORT_OUT, Q3N_EV_GRENADE_BOUNCE, Q3N_EV_GENERAL_SOUND,
    Q3N_EV_GLOBAL_SOUND, Q3N_EV_TEAM_SOUND, Q3N_EV_BULLET_FLESH, Q3N_EV_BULLET_WALL,
    Q3N_EV_MISSILE_HIT, Q3N_EV_MISSILE_MISS, Q3N_EV_MISSILE_METAL, Q3N_EV_RAIL,
    Q3N_EV_SHOTGUN, Q3N_EV_BULLET, Q3N_EV_PAIN, Q3N_EV_DEATH1, Q3N_EV_DEATH2, Q3N_EV_DEATH3,
    Q3N_EV_OBITUARY, Q3N_EV_QUAD, Q3N_EV_BATTLESUIT, Q3N_EV_REGEN, Q3N_EV_GIB, Q3N_EV_SCORE,
    Q3N_EV_PROX_STICK, Q3N_EV_PROX_TRIGGER, Q3N_EV_KAMIKAZE, Q3N_EV_OBELISK_EXPLODE,
    Q3N_EV_OBELISK_PAIN, Q3N_EV_INVUL_IMPACT, Q3N_EV_JUICED, Q3N_EV_LIGHTNING,
    Q3N_EV_DEBUG_LINE, Q3N_EV_STOP_LOOP, Q3N_EV_TAUNT, Q3N_EV_TAUNT_YES, Q3N_EV_TAUNT_NO,
    Q3N_EV_TAUNT_FOLLOW, Q3N_EV_TAUNT_FLAG, Q3N_EV_TAUNT_BASE, Q3N_EV_TAUNT_PATROL
};
static inline int32_t q3ne_word(uint32_t x) { int32_t v; memcpy(&v, &x, 4); return v; }
static inline int32_t q3ne_sub(int32_t x, int32_t y) { return q3ne_word((uint32_t)x - (uint32_t)y); }
static inline int32_t q3ne_plus(int32_t x, int32_t y) { return q3ne_word((uint32_t)x + (uint32_t)y); }
static inline int32_t q3ne_int(float x) { return !isfinite(x) || x < -2147483648.0f || x >= 2147483648.0f ? INT32_MIN : (int32_t)x; }
static inline uint8_t q3ne_byte(float x) { return (uint8_t)(uint32_t)q3ne_int(x); }
static inline qa_vec3 q3ne_array(const float x[3]) { return qa_v3(x[0], x[1], x[2]); }
static inline void q3ne_store(float x[3], qa_vec3 v) { x[0] = v.x; x[1] = v.y; x[2] = v.z; }
static inline qa_vec3 q3ne_scale(qa_vec3 v, float s) { return qa_v3((v.x * s),(v.y * s),(v.z * s)); }
static inline qa_vec3 q3ne_sum(qa_vec3 a, qa_vec3 b) { return qa_v3((a.x + b.x),(a.y + b.y),(a.z + b.z)); }
static inline qa_vec3 q3ne_difference(qa_vec3 a, qa_vec3 b) { return q3ne_sum(a,q3ne_scale(b,-1)); }
static inline float q3ne_dot(qa_vec3 a, qa_vec3 b) { return (((a.x * b.x) + (a.y * b.y)) + (a.z * b.z)); }
static inline qa_vec3 q3ne_cross(qa_vec3 a, qa_vec3 b) { return qa_v3(((a.y * b.z) + -(a.z * b.y)),((a.z * b.x) + -(a.x * b.z)),((a.x * b.y) + -(a.y * b.x))); }
static inline float q3ne_length(qa_vec3 v) { return (sqrtf(q3ne_dot(v,v))); }
static inline qa_vec3 q3ne_normalize(qa_vec3 v) { float n=q3ne_length(v); return n!=0.0f? q3ne_scale(v,(1 / n)):v; }
static inline bool q3ne_fail(qa_error *e, qa_status status, const char *s) { qa_error_set(e,status,0,"%s",s); return false; }
static inline float q3ne_life(int32_t start, int32_t end) { return (1 / (float)q3ne_sub(end,start)); }
static inline float q3ne_remaining(const q3n_local_entity *v, int32_t time) { return ((float)q3ne_sub(v->end_time,time) * v->life_rate); }
static inline void q3ne_identity(qa_vec3 axis[3]) { axis[0]=qa_v3(1,0,0); axis[1]=qa_v3(0,1,0); axis[2]=qa_v3(0,0,1); }
qa_vec3 q3ne_perpendicular(qa_vec3);
qa_vec3 q3ne_rotate(qa_vec3 axis, qa_vec3 point, float degrees);
bool q3ne_current(const q3n_frame *, qa_error *);
bool q3ne_sound(const q3n_frame *, int32_t sound, const qa_vec3 *, int32_t entity, int32_t channel, bool local, qa_error *);
void q3ne_local_free(q3n_events *, int32_t index);
void q3ne_local_reset(q3n_events *);
#endif
