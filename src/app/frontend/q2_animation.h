#ifndef QA_FRONTEND_Q2_ANIMATION_H
#define QA_FRONTEND_Q2_ANIMATION_H
#include "qa/common.h"
#include <math.h>

typedef struct frontend_q2_animation {
    uint32_t frame, old_frame;
    double server_milliseconds;
    bool ready;
} frontend_q2_animation;
typedef struct frontend_q2_animation_sample {
    uint32_t frame, old_frame;
    float back_lerp;
} frontend_q2_animation_sample;
static inline void frontend_q2_animation_commit(frontend_q2_animation *animation,
    uint32_t frame,uint32_t previous_frame,uint32_t render_flags,
    double server_milliseconds,bool continuous)
{
    if (!animation->ready || !continuous || server_milliseconds<animation->server_milliseconds) {
        *animation=(frontend_q2_animation){.frame=frame,.old_frame=frame,
            .server_milliseconds=server_milliseconds,.ready=true};
    } else if (frame!=animation->frame ||
        ((render_flags&(UINT32_C(1)<<22)) && previous_frame!=animation->old_frame)) {
        animation->frame=frame;animation->old_frame=previous_frame;
        animation->server_milliseconds=server_milliseconds;
    }
}
static inline frontend_q2_animation_sample frontend_q2_animation_lerp(
    const frontend_q2_animation *animation,bool rerelease,bool weapon,
    uint32_t frame,uint32_t old_frame,uint32_t render_flags,
    double client_milliseconds,double tick_milliseconds,double animation_milliseconds,
    float tick_back_lerp)
{
    frontend_q2_animation_sample sample={frame,old_frame,tick_back_lerp};
    bool authored=(render_flags&(UINT32_C(1)<<22))!=0;
    if (rerelease && animation->ready &&
        (weapon || animation->frame!=animation->old_frame || authored)) {
        double back=1-(client_milliseconds-(animation->server_milliseconds-tick_milliseconds))/
            animation_milliseconds;
        sample.frame=animation->frame;
        sample.old_frame=animation->old_frame;
        sample.back_lerp=(float)fmax(0,fmin(1,back));
    }
    return sample;
}
#endif
