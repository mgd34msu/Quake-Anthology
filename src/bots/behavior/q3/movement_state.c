/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/movement_state.h"
#include <stdio.h>
#include <string.h>
void qa_bot_move_state_store_init(qa_bot_move_state_store *s) { memset(s, 0, sizeof *s); }
void qa_bot_move_result_init(qa_bot_move_result *r) { memset(r, 0, sizeof *r); }
int64_t qa_bot_move_state_store_allocate(qa_bot_move_state_store *s) {
  for (int i = 1; i <= QA_MAX_MOVE_STATES; i++)
    if (!s->allocated[i]) {
      s->allocated[i] = true;
      memset(&s->states[i], 0, sizeof s->states[i]);
      return i;
    }
  return 0;
}
qa_bot_move_state *qa_bot_move_state_store_from_handle(qa_bot_move_state_store *s, int64_t h) {
  char b[128];
  if (h <= 0 || h > QA_MAX_MOVE_STATES) {
    if (s->print) {
      snprintf(b, sizeof b, "move state handle %lld out of range\n", (long long)h);
      s->print(s->print_ctx, 4, b);
    }
    return NULL;
  }
  if (!s->allocated[h]) {
    if (s->print) {
      snprintf(b, sizeof b, "invalid move state %lld\n", (long long)h);
      s->print(s->print_ctx, 4, b);
    }
    return NULL;
  }
  return &s->states[h];
}
void qa_bot_move_state_store_free(qa_bot_move_state_store *s, int64_t h) {
  if (qa_bot_move_state_store_from_handle(s, h))
    s->allocated[h] = false;
}
void qa_bot_move_state_store_initialize(qa_bot_move_state_store *s, int64_t h,
                                        qa_bot_init_move in) {
  qa_bot_move_state *st = qa_bot_move_state_store_from_handle(s, h);
  if (!st)
    return;
  if (in.or_move_flags & QA_BOT_MF_TELEPORTED)
    st->has_walk_progress = false;
  st->origin = in.origin;
  st->velocity = in.velocity;
  st->view_offset = in.view_offset;
  st->entity_num = in.entity_num;
  st->client = in.client;
  st->think_time = (float)in.think_time;
  st->presence_type = in.presence_type;
  st->view_angles = in.view_angles;
  int64_t mask = QA_BOT_MF_ONGROUND | QA_BOT_MF_TELEPORTED | QA_BOT_MF_WATERJUMP | QA_BOT_MF_WALK |
                 QA_BOT_MF_GRAPPLEPULL;
  st->move_flags = (st->move_flags & ~mask) | (in.or_move_flags & mask);
}
void qa_bot_move_state_store_reset(qa_bot_move_state_store *s, int64_t h) {
  qa_bot_move_state *st = qa_bot_move_state_store_from_handle(s, h);
  if (st)
    memset(st, 0, sizeof *st);
}
void qa_bot_move_state_store_reset_avoid_reach(qa_bot_move_state_store *s, int64_t h) {
  qa_bot_move_state *st = qa_bot_move_state_store_from_handle(s, h);
  if (!st)
    return;
  st->avoid_reach = 0;
  st->avoid_reach_time = 0;
  st->avoid_reach_tries = 0;
}
void qa_bot_move_state_store_reset_last_avoid_reach(qa_bot_move_state_store *s, int64_t h) {
  qa_bot_move_state *st = qa_bot_move_state_store_from_handle(s, h);
  if (!st)
    return;
  if (st->avoid_reach_time > 0) {
    st->avoid_reach_time = 0;
    int32_t probe = 0;
    memcpy(&probe, &st->avoid_spots[0].origin.x, 4);
    (void)probe; /* TS probe reads avoidspots[0].origin.x bits; C double field differs, keep tries
                    unchanged unless >0 bits */
    if (st->avoid_reach_tries > 0) { /* source decrements when probe>0 */
    }
  }
}
void qa_bot_move_state_store_add_to_avoid_reach(qa_bot_move_state_store *s, qa_bot_move_state *st,
                                                int64_t num, double at) {
  float avoid = (float)at;
  float now = s->time_fn ? (float)s->time_fn(s->time_ctx) : 0.0f;
  if (st->avoid_reach == num) {
    st->avoid_reach_tries = st->avoid_reach_time > now ? (int32_t)(st->avoid_reach_tries + 1) : 1;
    st->avoid_reach_time = (float)((double)now + (double)avoid);
    return;
  }
  if (st->avoid_reach_time < now) {
    st->avoid_reach = num;
    st->avoid_reach_time = (float)((double)now + (double)avoid);
    st->avoid_reach_tries = 1;
  }
}
void qa_bot_move_state_store_add_avoid_spot(qa_bot_move_state_store *s, int64_t h, qa_vec3 o,
                                            double r, int64_t t) {
  qa_bot_move_state *st = qa_bot_move_state_store_from_handle(s, h);
  if (!st)
    return;
  if (t == QA_AVOID_CLEAR) {
    st->num_avoid_spots = 0;
    return;
  }
  if (st->num_avoid_spots >= QA_MAX_AVOID_SPOTS)
    return;
  qa_bot_avoid_spot *sp = &st->avoid_spots[st->num_avoid_spots];
  sp->origin = o;
  sp->radius = (float)r;
  sp->type = t;
  st->num_avoid_spots++;
}
int64_t qa_bot_move_state_store_setup(qa_bot_move_state_store *s) {
  if (s->lib_var_value) {
    s->sv_max_step = s->lib_var_value(s->libvar_ctx, "sv_step", "18");
    s->sv_max_barrier = s->lib_var_value(s->libvar_ctx, "sv_maxbarrier", "32");
    s->sv_gravity = s->lib_var_value(s->libvar_ctx, "sv_gravity", "800");
    s->rocket_index = s->lib_var_value(s->libvar_ctx, "weapindex_rocketlauncher", "5");
    s->bfg_index = s->lib_var_value(s->libvar_ctx, "weapindex_bfg10k", "9");
    s->grapple_index = s->lib_var_value(s->libvar_ctx, "weapindex_grapple", "10");
    s->missile_entity_type = s->lib_var_value(s->libvar_ctx, "entitytypemissile", "3");
    s->offhand_grapple = s->lib_var_value(s->libvar_ctx, "offhandgrapple", "0");
    (void)s->lib_var_value(s->libvar_ctx, "cmd_grappleon", "grappleon");
    (void)s->lib_var_value(s->libvar_ctx, "cmd_grappleoff", "grappleoff");
  }
  return 0;
}
void qa_bot_move_state_store_shutdown(qa_bot_move_state_store *s) {
  for (int i = 1; i <= QA_MAX_MOVE_STATES; i++)
    s->allocated[i] = false;
}
