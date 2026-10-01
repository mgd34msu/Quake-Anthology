#ifndef QA_Q3_NATIVE_LOCAL_ENTITIES_H
#define QA_Q3_NATIVE_LOCAL_ENTITIES_H

#include "frame.h"

typedef enum q3n_local_type {
    Q3N_LE_MARK, Q3N_LE_SHOW, Q3N_LE_FRAGMENT, Q3N_LE_MOVE_SCALE_FADE,
    Q3N_LE_FALL_SCALE_FADE, Q3N_LE_SCALE_FADE, Q3N_LE_FADE_RGB,
    Q3N_LE_EXPLOSION, Q3N_LE_SPRITE_EXPLOSION, Q3N_LE_SCORE_PLUM,
    Q3N_LE_KAMIKAZE, Q3N_LE_INVUL_IMPACT, Q3N_LE_INVUL_JUICED
} q3n_local_type;
enum { Q3N_LE_DONT_SCALE = 1, Q3N_LE_TUMBLE = 2, Q3N_LE_SOUND1 = 4, Q3N_LE_SOUND2 = 8 };
typedef enum q3n_local_mark { Q3N_MARK_NONE, Q3N_MARK_BURN, Q3N_MARK_BLOOD } q3n_local_mark;
typedef enum q3n_local_sound { Q3N_BOUNCE_NONE, Q3N_BOUNCE_BLOOD, Q3N_BOUNCE_BRASS } q3n_local_sound;
typedef struct q3n_local_entity {
    q3n_local_type type;
    int32_t flags, start_time, end_time, fade_in_time;
    float life_rate;
    qa_q3_trajectory pos, angles;
    float bounce_factor, color[4], radius, light;
    qa_vec3 light_color;
    q3n_local_mark mark;
    q3n_local_sound bounce_sound;
    qa_q3_ref_entity ref;
} q3n_local_entity;
/* The record borrows its real slot until free, reset, or oldest eviction.
 * A full pool recycles the oldest record, including during its traversal. */
q3n_local_entity *q3n_local_allocate(q3n_events *, q3n_local_type, qa_q3_ref_kind);
bool q3n_local_submit(const q3n_frame *, qa_error *);

#endif
