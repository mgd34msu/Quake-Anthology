/* Port of src/content/q2/rerelease/q64/index.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_Q64_INDEX_H
#define QA_CONTENT_Q2_RERELEASE_Q64_INDEX_H
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/contracts/session.h"

typedef struct qa_q2rr_eye_state { qa_vec3 neutral_angles, eye_position; double vision_cone; } qa_q2rr_eye_state;
typedef struct qa_q2rr_camera_state { double remaining, distance, speed; qa_vec3 angles; } qa_q2rr_camera_state;
typedef struct qa_q2rr_dummy_state { double fade_remaining, fade_duration; bool fading; } qa_q2rr_dummy_state;
typedef struct qa_q2rr_eye_entry { qa_saved_actor_id actor; qa_q2rr_eye_state state; } qa_q2rr_eye_entry;
typedef struct qa_q2rr_camera_entry { qa_saved_actor_id actor; qa_q2rr_camera_state state; } qa_q2rr_camera_entry;
typedef struct qa_q2rr_dummy_entry { qa_saved_actor_id actor; qa_q2rr_dummy_state state; } qa_q2rr_dummy_entry;
typedef struct qa_q2_rerelease_q64_checkpoint {
  const qa_q2rr_eye_entry *eyes; size_t eye_count;
  const qa_q2rr_camera_entry *cameras; size_t camera_count;
  const qa_q2rr_dummy_entry *dummies; size_t dummy_count;
} qa_q2_rerelease_q64_checkpoint;

struct qa_q2_rerelease_players;
typedef struct qa_q2rr_eye_map { qa_actor_id actor; qa_q2rr_eye_state state; } qa_q2rr_eye_map;
typedef struct qa_q2rr_cam_map { qa_actor_id actor; qa_q2rr_camera_state state; } qa_q2rr_cam_map;
typedef struct qa_q2rr_dum_map { qa_actor_id actor; qa_q2rr_dummy_state state; } qa_q2rr_dum_map;
typedef struct qa_q2_rerelease_q64 {
  struct qa_q2_rerelease_players *players;
  qa_q2_rerelease_hooks hooks;
  void *end_ctx;
  void (*end_of_unit)(void *ctx, qa_q2_game_services *g);
  qa_q2rr_eye_map *eyes; size_t eye_count, eye_cap;
  qa_q2rr_cam_map *cameras; size_t cam_count, cam_cap;
  qa_q2rr_dum_map *dummies; size_t dum_count, dum_cap;
  qa_q2_think spinning_think, eye_setup, eye_think, camera_think, dummy_think;
  qa_q2_use camera_use;
  /* capture scratch (valid until next capture/destroy) */
  qa_q2rr_eye_entry *cp_eyes; qa_q2rr_camera_entry *cp_cams; qa_q2rr_dummy_entry *cp_dums;
} qa_q2_rerelease_q64;
void qa_q2_rerelease_q64_init(qa_q2_rerelease_q64 *q, struct qa_q2_rerelease_players *p,
                              qa_q2_rerelease_hooks h, void *end_ctx,
                              void (*end_of_unit)(void *ctx, qa_q2_game_services *g));
void qa_q2_rerelease_q64_destroy(qa_q2_rerelease_q64 *q);
void qa_q2_rerelease_q64_release(qa_q2_rerelease_q64 *q, qa_actor_id a);
qa_q2_rerelease_q64_checkpoint qa_q2_rerelease_q64_capture(qa_q2_rerelease_q64 *q);
void qa_q2_rerelease_q64_restore(qa_q2_rerelease_q64 *q, qa_q2_game_services *g, qa_q2_rerelease_q64_checkpoint cp);
bool qa_q2_rerelease_q64_spawn(qa_q2_rerelease_q64 *q, qa_q2_entity *e, qa_q2_game_services *g);
qa_q2_callback_definitions qa_q2_rerelease_q64_callbacks(qa_q2_rerelease_q64 *q);
qa_q2_spawn_module qa_q2_rerelease_q64_as_spawn(qa_q2_rerelease_q64 *q);
#endif
