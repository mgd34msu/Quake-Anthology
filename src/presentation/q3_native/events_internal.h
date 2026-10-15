#ifndef QA_Q3_NATIVE_EVENTS_INTERNAL_H
#define QA_Q3_NATIVE_EVENTS_INTERNAL_H
#include "events.h"
#include "qa/binary.h"
#include "qa/text.h"
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
    qa_application_event_subscription delivery;
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

static inline uint8_t q3ne_byte(float x) { return (uint8_t)(uint32_t)qa_source_to_i32(x); }
static inline qa_vec3 q3ne_array(const float x[3]) { return qa_v3(x[0], x[1], x[2]); }
static inline void q3ne_store(float x[3], qa_vec3 v) { x[0] = v.x; x[1] = v.y; x[2] = v.z; }
static inline bool q3ne_fail(qa_error *e, qa_status status, const char *s) { qa_error_set(e,status,0,"%s",s); return false; }
static inline float q3ne_life(int32_t start, int32_t end) { return (1 / (float)qa_i32_sub(end,start)); }
static inline float q3ne_remaining(const q3n_local_entity *v, int32_t time) { return ((float)qa_i32_sub(v->end_time,time) * v->life_rate); }
static inline void q3ne_identity(qa_vec3 axis[3]) { axis[0]=qa_v3(1,0,0); axis[1]=qa_v3(0,1,0); axis[2]=qa_v3(0,0,1); }
bool q3ne_current(const q3n_frame *, qa_error *);
bool q3ne_sound(const q3n_frame *, int32_t sound, const qa_vec3 *, int32_t entity, int32_t channel, bool local, qa_error *);
void q3ne_local_free(q3n_events *, int32_t index);
void q3ne_local_reset(q3n_events *);
#endif
