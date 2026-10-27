/* Port of src/content/q2/foundation/weapons/generic-frame.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_WEAPONS_GENERIC_FRAME_H
#define QA_CONTENT_Q2_FOUNDATION_WEAPONS_GENERIC_FRAME_H
#include "qa/content/q2/foundation/weapons/types.h"

/* Q2GenericFrameState / Q2GenericDefinition are Picks of qa_q2w_state /
 * qa_q2w_definition; the full structs are passed. */
typedef struct qa_q2w_classic_frame_input { bool attack, change_requested; } qa_q2w_classic_frame_input;
typedef struct qa_q2w_rerelease_frame_input {
  bool attack, change_requested;
  double now, frame_seconds;
  bool instant_switch, holster, weapon_thunk;
} qa_q2w_rerelease_frame_input;
typedef struct qa_q2w_frame_hooks {
  void *ctx;
  double (*random)(void *ctx);
  double (*ammo)(void *ctx);
  void (*no_ammo)(void *ctx);
  void (*fire)(void *ctx, bool buffered);
  void (*change_weapon)(void *ctx);
  void (*reverse_animation)(void *ctx);
  void (*attack_animation)(void *ctx);
  void (*powerup_sound)(void *ctx);
  double (*animation_time)(void *ctx); /* rerelease only */
  void (*prepare_drop)(void *ctx);     /* rerelease only */
} qa_q2w_frame_hooks;
typedef qa_q2w_frame_hooks qa_q2w_classic_frame_hooks;
typedef qa_q2w_frame_hooks qa_q2w_rerelease_frame_hooks;

double qa_q2w_millisecond_sum(double time, double seconds);
void qa_q2w_step_classic_frame(qa_q2w_state *state, const qa_q2w_definition *d,
                               qa_q2w_classic_frame_input input, const qa_q2w_frame_hooks *hooks);
void qa_q2w_step_rerelease_frame(qa_q2w_state *state, const qa_q2w_definition *d,
                                 qa_q2w_rerelease_frame_input input, const qa_q2w_frame_hooks *hooks);
#endif
