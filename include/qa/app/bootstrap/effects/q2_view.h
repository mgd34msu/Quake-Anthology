/* Port of src/app/bootstrap/effects/q2-view.ts (Rogue p_view.c /
 * g_sphere.c presentation state). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_EFFECTS_Q2_VIEW_H
#define QA_APP_BOOTSTRAP_EFFECTS_Q2_VIEW_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q2/missionpacks/types.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
/* TS Q2EffectPlayerView. has_blend false is the TS null blend. */
typedef struct qa_app_q2_effect_player_view {
  qa_scene_camera camera;
  bool infrared;
  bool has_blend;
  qa_vec4 blend;
} qa_app_q2_effect_player_view;
/* TS Q2EffectViews. */
typedef struct qa_app_q2_effect_views qa_app_q2_effect_views;
qa_app_q2_effect_views *qa_app_q2_effect_views_create(void);
void qa_app_q2_effect_views_destroy(qa_app_q2_effect_views *views);
/* TS receive (event excludes tracker-pain). */
void qa_app_q2_effect_views_receive(qa_app_q2_effect_views *views, qa_q2mp_player_effect event);
/* TS frame. pose: false = TS undefined. */
qa_app_q2_effect_player_view qa_app_q2_effect_views_frame(qa_app_q2_effect_views *views,
    qa_actor_id actor, qa_scene_camera camera, double seconds,
    bool (*pose)(void *ctx, qa_actor_id actor, qa_vec3 *origin_out), void *pose_ctx);
/* TS retain(actors). */
void qa_app_q2_effect_views_retain(qa_app_q2_effect_views *views, const qa_actor_id *actors, size_t count);
void qa_app_q2_effect_views_clear(qa_app_q2_effect_views *views);
#endif
