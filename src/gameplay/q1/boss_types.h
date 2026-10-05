#ifndef QA_Q1_BOSS_TYPES_H
#define QA_Q1_BOSS_TYPES_H

#include "reference.h"

typedef enum q1_boss_child_kind {
    Q1_CHILD_SPHERE,
    Q1_CHILD_SPHERE_RING,
    Q1_CHILD_SPHERE_CHUNK,
    Q1_CHILD_SPAMMER,
    Q1_CHILD_SWIPER,
    Q1_CHILD_EYE,
    Q1_CHILD_BLASTER,
    Q1_CHILD_VORTEX,
    Q1_CHILD_SPAM,
    Q1_CHILD_SPAM_BEAM,
    Q1_CHILD_SPAM_EXPLODE,
    Q1_CHILD_CLEANUP,
    Q1_CHILD_ZOMBIE_CLEANUP,
    Q1_CHILD_TELEDEATH,
    Q1_CHILD_FINAL_SPIRAL,
    Q1_CHILD_FINAL_CIRCLE,
    Q1_CHILD_FINAL_END
} q1_boss_child_kind;
typedef struct q1_boss_child {
    q1_boss_child_kind kind;
    q1_ref enemy;
    float sign;
    int32_t maximum;
    double sound_after;
    bool reactive, counted_death;
} q1_boss_child;

#endif
