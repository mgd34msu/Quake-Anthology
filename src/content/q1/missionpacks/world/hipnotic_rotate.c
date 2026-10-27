/* Port of src/content/q1/missionpacks/world/hipnotic-rotate.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/hipnotic_rotate.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/content/q1/missionpacks/world/rotate_targets.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>

typedef struct ro_ctx { qa_q1mp_game *game; } ro_ctx;
static void set_angles(ro_ctx *c, qa_q1mp_actor *e, qa_vec3 a) { qa_q1mp_set_angles_b(c->game, e, a); }
static void continuous_think(ro_ctx *c, qa_q1b_game *g, qa_q1b_actor *e) {
  double elapsed = g->time - qa_q1mp_number(e, "ltime");
  qa_q1mp_world_number(e, "ltime", g->time);
  double state = qa_q1mp_number(e, "rotate_state");
  if (state == 2) { e->count = fmin(1, e->count + qa_q1mp_number(e,"cnt") * elapsed); elapsed *= e->count; }
  else if (state == 3) {
    e->count -= qa_q1mp_number(e,"cnt") * elapsed;
    if (e->count < 0) { qa_q1mp_rotate_targets_final(c->game, e); qa_q1mp_world_number(e, "rotate_state", 1); g->cancel(g, e); return; }
    elapsed *= e->count;
  }
  set_angles(c, e, qa_q1mp_normalize_angles(qa_q1mp_vadd(g->body(g, e).angles, qa_q1mp_vscale(qa_q1mp_vector_of(e,"rotate"), elapsed))));
  qa_q1mp_rotate_targets(c->game, e);
  qa_q1mp_world_later(c->game, e, 0.02, "hip:rotate_entity");
}
static void continuous_action(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { continuous_think(vctx, g, e); }
static void continuous_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) {
  (void)o; (void)a; ro_ctx *c = vctx;
  e->frame = 1 - e->frame; double state = qa_q1mp_number(e, "rotate_state");
  if (state == 0) {
    if ((e->spawnflags & 1) != 0) {
      if (e->speed != 0) { e->count = 1; qa_q1mp_world_number(e, "rotate_state", 3); }
      else { qa_q1mp_world_number(e, "rotate_state", 1); g->cancel(g, e); }
    }
  } else if (state == 1) {
    qa_q1mp_world_number(e, "ltime", g->time); e->count = 0;
    qa_q1mp_world_number(e, "rotate_state", e->speed != 0 ? 2 : 0);
    qa_q1mp_world_later(c->game, e, 0.02, "hip:rotate_entity");
  } else if (state == 2) { if ((e->spawnflags & 1) != 0) qa_q1mp_world_number(e, "rotate_state", 3); }
  else qa_q1mp_world_number(e, "rotate_state", 2);
}
static void reverse_door(ro_ctx *c, qa_q1b_game *g, qa_q1b_actor *e) {
  e->frame = 1 - e->frame; bool closing = qa_q1mp_number(e, "rotate_state") == 7;
  qa_vec3 start = closing ? e->dest1 : e->dest2, destination = closing ? e->dest2 : e->dest1;
  qa_q1mp_world_vector(e, "dest", destination);
  qa_q1mp_world_number(e, "rotate_state", closing ? 6 : 7);
  qa_q1mp_sound(c->game, e, qa_q1mp_text(e, "noise2"));
  qa_q1mp_world_vector(e, "rotate", qa_q1mp_vscale(qa_q1mp_vsub(destination, start), 1 / e->speed));
  qa_q1mp_world_number(e, "endtime", g->time + e->speed - (qa_q1mp_number(e,"endtime") - g->time));
  qa_q1mp_world_number(e, "ltime", g->time);
  qa_q1mp_world_later(c->game, e, 0.02, "hip:rotate_door");
}
static void reverse_group(ro_ctx *c, qa_q1b_game *g, qa_q1b_actor *e) {
  const char *group = qa_q1mp_text(e, "group");
  if (!group[0]) { reverse_door(c, g, e); return; }
  qa_q1mp_actor *ents[512]; size_t n = g->entities(g, ents, 512);
  for (size_t i = 0; i < n; i++) if (qa_q1mp_streq(qa_q1mp_text(ents[i], "group"), group) && g->live(g, ents[i])) reverse_door(c, g, ents[i]);
}
static void train_stop(ro_ctx *c, qa_q1b_game *g, qa_q1b_actor *e, bool wait) {
  qa_q1mp_opt_actor goal_ref = qa_q1b_references_get(e, "goalentity");
  qa_q1mp_actor *goal = goal_ref.has ? g->entity(g, goal_ref) : NULL;
  if (!goal) qa_range_error("rotate_train: missing goal");
  qa_q1mp_world_number(e, "rotate_state", wait ? 0 : 2);
  const char *noise = qa_q1mp_text(goal, "noise");
  qa_q1mp_sound(c->game, e, noise[0] ? noise : qa_q1mp_text(e, "noise"));
  if ((goal->spawnflags & 2) != 0) { qa_q1mp_world_vector(e, "rotate", QA_Q1MP_ZERO); set_angles(c, e, qa_q1mp_vector_of(e, "finalangle")); }
  if ((goal->spawnflags & 8) != 0) qa_q1mp_world_vector(e, "rotate", QA_Q1MP_ZERO);
  if (wait) qa_q1mp_world_number(e, "endtime", qa_q1mp_number(e,"ltime") + goal->wait);
  else e->damage = 0;
  qa_q1b_fields_set(e, "think1", "hip:rotate_train_next");
}
static void train_next_impl(ro_ctx *c, qa_q1b_game *g, qa_q1b_actor *e) {
  qa_q1mp_world_number(e, "rotate_state", 4);
  qa_q1mp_opt_actor cur_ref = qa_q1b_references_get(e, "goalentity");
  qa_q1mp_actor *current = cur_ref.has ? g->entity(g, cur_ref) : NULL;
  qa_q1mp_actor *target = qa_q1mp_find0(c->game, qa_q1mp_text(e, "path"));
  if (!current || !target || !qa_q1mp_streq(target->classname, "path_rotate")) qa_range_error("rotate_train_next: next target is not path_rotate");
  if (qa_q1mp_text(current, "noise1")[0]) qa_q1b_fields_set(e, "noise1", qa_q1mp_text(current, "noise1"));
  qa_q1mp_sound(c->game, e, qa_q1mp_text(e, "noise1"));
  qa_q1b_references_set(e, "goalentity", QA_Q1MP_SOME(target->actor.id));
  qa_q1b_fields_set(e, "path", target->target);
  if (target->target[0] == '\0') qa_range_error("rotate_train_next: no next target");
  qa_q1b_fields_set(e, "think1", (target->spawnflags & 4) != 0 ? "hip:rotate_train_stop" : target->wait != 0 ? "hip:rotate_train_wait" : "hip:rotate_train_next");
  if (qa_q1mp_text(current, "event")[0]) qa_q1mp_world_target_event(c->game, e, qa_q1mp_text(current, "event"), current->message);
  if ((current->spawnflags & 2) != 0) { qa_q1mp_world_vector(e, "rotate", QA_Q1MP_ZERO); set_angles(c, e, qa_q1mp_vector_of(e, "finalangle")); }
  if ((current->spawnflags & 1) != 0) qa_q1mp_world_vector(e, "rotate", qa_q1mp_vector_of(current, "rotate"));
  if ((current->spawnflags & 16) != 0) e->damage = current->damage;
  if ((current->spawnflags & 64) != 0) qa_q1mp_damage_on_targets(c->game, e, current->damage);
  qa_q1b_body target_body = g->body(g, target), body = g->body(g, e);
  if (current->speed == -1) {
    g->set_origin(g, e, target_body.origin);
    qa_q1mp_world_number(e, "endtime", qa_q1mp_number(e,"ltime") + 0.01);
    qa_q1mp_set_target_origin(c->game, e);
    if ((target->spawnflags & 2) != 0) set_angles(c, e, target_body.angles);
    qa_q1mp_world_number(e, "duration", 1); qa_q1mp_world_number(e, "cnt", g->time);
    e->dest2 = QA_Q1MP_ZERO; e->dest1 = target_body.origin;
    qa_q1mp_world_vector(e, "finaldest", target_body.origin); return;
  }
  qa_q1mp_world_number(e, "rotate_state", 1);
  qa_q1mp_world_vector(e, "finaldest", target_body.origin);
  qa_vec3 delta = qa_q1mp_vsub(target_body.origin, body.origin); double distance = qa_q1mp_length(delta);
  if (distance == 0) {
    qa_q1mp_set_velocity(c->game, e, QA_Q1MP_ZERO);
    qa_q1mp_world_number(e, "endtime", qa_q1mp_number(e,"ltime") + 0.1);
    qa_q1mp_world_number(e, "duration", 1); qa_q1mp_world_number(e, "cnt", g->time);
    e->dest2 = QA_Q1MP_ZERO; e->dest1 = body.origin; return;
  }
  if ((current->spawnflags & 32) == 0 && current->speed > 0) e->speed = current->speed;
  if ((current->spawnflags & 32) == 0 && e->speed == 0) qa_range_error("rotate_train: no speed defined");
  double travel = (current->spawnflags & 32) != 0 ? current->speed : distance / e->speed;
  if (travel < 0.1) {
    qa_q1b_body_patch p = {0}; p.has_velocity = true; p.velocity = QA_Q1MP_ZERO;
    if ((target->spawnflags & 2) != 0) { p.has_angles = true; p.angles = target_body.angles; }
    qa_q1mp_patch(c->game, e, &p);
    qa_q1mp_world_number(e, "endtime", qa_q1mp_number(e,"ltime") + 0.1); return;
  }
  double inverse = 1 / travel;
  if ((target->spawnflags & 2) != 0) {
    qa_q1mp_world_vector(e, "finalangle", qa_q1mp_normalize_angles(target_body.angles));
    qa_q1mp_world_vector(e, "rotate", qa_q1mp_vscale(qa_q1mp_vsub(target_body.angles, g->body(g, e).angles), inverse));
  }
  qa_q1mp_world_number(e, "endtime", qa_q1mp_number(e,"ltime") + travel);
  qa_q1mp_set_velocity(c->game, e, qa_q1mp_vscale(delta, inverse));
  qa_q1mp_world_number(e, "duration", inverse); qa_q1mp_world_number(e, "cnt", g->time);
  e->dest2 = delta; e->dest1 = body.origin;
}
static void train_next_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { train_next_impl(vctx, g, e); }
static void train_wait_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { train_stop(vctx, g, e, true); }
static void train_stop_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { train_stop(vctx, g, e, false); }
static void call_action(qa_q1b_game *g, qa_q1b_actor *e, const char *name) {
  qa_q1b_action action = g->named.action(g->named.ctx, e, name);
  qa_q1b_action_call(&action);
}
static void rotate_first_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  ro_ctx *c = vctx; qa_q1mp_link_rotate_targets(c->game, e);
  e->use = g->named.use(g->named.ctx, e, "hip:rotate_entity");
  qa_q1mp_world_number(e, "rotate_state", (e->spawnflags & 2) != 0 ? 0 : 1);
  if ((e->spawnflags & 2) != 0) { qa_q1mp_world_number(e, "ltime", g->time); qa_q1mp_world_later(c->game, e, 0.02, "hip:rotate_entity"); }
}
static void info_rotate_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { (void)g; qa_q1mp_world_later(((ro_ctx *)vctx)->game, e, 2, "SUB_Remove"); }
static void path_rotate_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { (void)vctx; (void)g; (void)e; }
static void rotate_object_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { (void)vctx; (void)g; e->solid = "none"; e->movement = "none"; }
static void rotate_entity_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  ro_ctx *c = vctx; e->solid = "none"; e->movement = "none";
  if (e->speed != 0) qa_q1mp_world_number(e, "cnt", 1 / e->speed);
  qa_q1mp_world_number(e, "ltime", g->time);
  qa_q1mp_world_later(c->game, e, 0.1, "hip:rotate_first");
}
static void door_action(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  ro_ctx *c = vctx; double elapsed = g->time - qa_q1mp_number(e, "ltime");
  qa_q1mp_world_number(e, "ltime", g->time);
  bool moving = g->time < qa_q1mp_number(e, "endtime");
  set_angles(c, e, moving ? qa_q1mp_vadd(g->body(g, e).angles, qa_q1mp_vscale(qa_q1mp_vector_of(e,"rotate"), elapsed)) : qa_q1mp_vector_of(e, "dest"));
  qa_q1mp_rotate_targets(c->game, e);
  qa_q1mp_world_later(c->game, e, 0.01, moving ? "hip:rotate_door" : "hip:rotate_door_done");
}
static void door_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) {
  (void)o; (void)a; ro_ctx *c = vctx; double state = qa_q1mp_number(e, "rotate_state");
  if (state != 4 && state != 5) return;
  if (qa_q1mp_number(e, "cnt") == 0) { qa_q1mp_world_number(e, "cnt", 1); qa_q1mp_link_rotate_targets(c->game, e); }
  e->frame = 1 - e->frame;
  qa_vec3 destination = state == 4 ? e->dest2 : e->dest1, start = state == 4 ? e->dest1 : e->dest2;
  qa_q1mp_world_vector(e, "dest", destination);
  qa_q1mp_world_number(e, "rotate_state", state == 4 ? 6 : 7);
  qa_q1mp_world_vector(e, "rotate", qa_q1mp_vscale(qa_q1mp_vsub(destination, start), 1 / e->speed));
  qa_q1mp_sound(c->game, e, qa_q1mp_text(e, "noise2"));
  qa_q1mp_world_number(e, "endtime", g->time + e->speed);
  qa_q1mp_world_number(e, "ltime", g->time);
  qa_q1mp_world_later(c->game, e, 0.01, "hip:rotate_door");
}
static void door_done_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  ro_ctx *c = vctx; qa_q1mp_world_number(e, "ltime", g->time); e->frame = 1 - e->frame;
  set_angles(c, e, qa_q1mp_vector_of(e, "dest"));
  if (qa_q1mp_number(e, "rotate_state") == 6) qa_q1mp_world_number(e, "rotate_state", 5);
  else if ((e->spawnflags & 1) != 0) { reverse_group(c, g, e); return; }
  else qa_q1mp_world_number(e, "rotate_state", 4);
  qa_q1mp_sound(c->game, e, qa_q1mp_text(e, "noise3"));
  qa_q1mp_rotate_targets_final(c->game, e);
}
static void rotate_door_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)vctx;
  if (e->target[0] == '\0') qa_range_error("rotate_door without target");
  e->dest1 = QA_Q1MP_ZERO; e->dest2 = g->body(g, e).angles;
  qa_q1b_body_patch p = {0}; p.has_angles = true; p.angles = QA_Q1MP_ZERO; g->set_body(g, e, &p);
  if (e->speed == 0) e->speed = 2;
  e->damage = e->damage == 0 ? 2 : fmax(0, e->damage);
  qa_q1mp_world_number(e, "cnt", 0); qa_q1mp_world_number(e, "rotate_state", 4);
  if (e->sounds == 0) e->sounds = 1;
  qa_q1b_fields_set(e, "noise2", e->sounds == 2 ? "doors/airdoor1.wav" : e->sounds == 3 ? "doors/basesec1.wav" : "doors/winch2.wav");
  qa_q1b_fields_set(e, "noise3", e->sounds == 2 ? "doors/airdoor2.wav" : e->sounds == 3 ? "doors/basesec2.wav" : "doors/drclos4.wav");
  e->solid = "none"; e->movement = "none"; e->use = g->named.use(g->named.ctx, e, "hip:rotate_door");
}
static void movewall_action(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  ro_ctx *c = vctx; qa_q1mp_world_number(e, "ltime", g->time);
  qa_q1mp_world_later(c->game, e, 0.02, "hip:movewall");
}
static void movewall_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; (void)vctx;
  qa_q1mp_actor *owner = g->entity(g, e->owner);
  if (!owner || g->time < owner->attack_finished) return;
  double damage = e->damage != 0 ? e->damage : owner->damage;
  if (damage != 0) { qa_q1b_game_damage(g, other, e->actor.id, QA_Q1MP_SOME(owner->actor.id), damage); owner->attack_finished = g->time + 0.5; }
}
static void movewall_blocked(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other) {
  ro_ctx *c = vctx; qa_q1mp_actor *owner = g->entity(g, e->owner);
  if (!owner || g->time < owner->attack_finished) return;
  owner->attack_finished = g->time + 0.5;
  if (qa_q1mp_streq(owner->classname, "func_rotate_door")) reverse_group(c, g, owner);
  double damage = e->damage != 0 ? e->damage : owner->damage;
  if (damage != 0) qa_q1b_game_damage(g, other, e->actor.id, QA_Q1MP_SOME(owner->actor.id), damage);
}
static void movewall_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  ro_ctx *c = vctx;
  e->movement = "push"; e->solid = (e->spawnflags & 4) != 0 ? "none" : "bsp";
  qa_q1b_body_patch p = {0}; p.has_angles = true; p.angles = QA_Q1MP_ZERO; g->set_body(g, e, &p);
  if (qa_q1mp_streq(e->solid, "bsp")) e->blocked = g->named.blocked(g->named.ctx, e, "hip:movewall");
  if ((e->spawnflags & 2) != 0) e->touch = g->named.touch(g->named.ctx, e, "hip:movewall");
  if ((e->spawnflags & 1) == 0) e->model[0] = '\0';
  qa_q1mp_world_number(e, "ltime", g->time);
  qa_q1mp_world_later(c->game, e, 0.02, "hip:movewall");
}
static void train_find_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  ro_ctx *c = vctx; qa_q1mp_world_number(e, "rotate_state", 3);
  qa_q1mp_link_rotate_targets(c->game, e);
  qa_q1mp_actor *target = qa_q1mp_find0(c->game, qa_q1mp_text(e, "path"));
  if (!target || !qa_q1mp_streq(target->classname, "path_rotate")) qa_range_error("rotate_train_find: next target is not path_rotate");
  qa_q1b_references_set(e, "goalentity", QA_Q1MP_SOME(target->actor.id));
  if ((target->spawnflags & 2) != 0) { set_angles(c, e, g->body(g, target).angles); qa_q1mp_world_vector(e, "finalangle", qa_q1mp_normalize_angles(g->body(g, target).angles)); }
  qa_q1b_fields_set(e, "path", target->target);
  g->set_origin(g, e, g->body(g, target).origin);
  qa_q1mp_set_target_origin(c->game, e); qa_q1mp_rotate_targets_final(c->game, e);
  qa_q1b_fields_set(e, "think1", "hip:rotate_train_next");
  qa_q1mp_world_number(e, "endtime", e->targetname[0] == '\0' ? qa_q1mp_number(e,"ltime") + 0.1 : 0);
  qa_q1mp_world_number(e, "duration", 1); qa_q1mp_world_number(e, "cnt", g->time);
  e->dest2 = QA_Q1MP_ZERO; e->dest1 = g->body(g, e).origin;
}
static void rotate_train_action(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  ro_ctx *c = vctx; double elapsed = g->time - qa_q1mp_number(e, "ltime");
  qa_q1mp_world_number(e, "ltime", g->time);
  if (qa_q1mp_number(e, "endtime") != 0 && g->time >= qa_q1mp_number(e, "endtime")) {
    qa_q1mp_world_number(e, "endtime", 0);
    if (qa_q1mp_number(e, "rotate_state") == 1) {
      qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = qa_q1mp_vector_of(e,"finaldest");
      p.has_velocity = true; p.velocity = QA_Q1MP_ZERO; qa_q1mp_patch(c->game, e, &p);
    }
    if (qa_q1mp_text(e, "think1")[0]) call_action(g, e, qa_q1mp_text(e, "think1"));
  } else {
    double t = (g->time - qa_q1mp_number(e,"cnt")) * qa_q1mp_number(e,"duration");
    if (t > 1) t = 1;
    g->set_origin(g, e, qa_q1mp_vadd(e->dest1, qa_q1mp_vscale(e->dest2, t)));
  }
  set_angles(c, e, qa_q1mp_normalize_angles(qa_q1mp_vadd(g->body(g, e).angles, qa_q1mp_vscale(qa_q1mp_vector_of(e,"rotate"), elapsed))));
  qa_q1mp_rotate_targets(c->game, e);
  qa_q1mp_world_later(c->game, e, 0.02, "hip:rotate_train");
}
static void rotate_train_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) {
  (void)vctx; (void)o; (void)a; qa_vec3 v = g->body(g, e).velocity;
  if (qa_q1mp_streq(qa_q1mp_text(e,"think1"), "hip:rotate_train_find") || v.x != 0 || v.y != 0 || v.z != 0) return;
  if (qa_q1mp_text(e, "think1")[0]) call_action(g, e, qa_q1mp_text(e, "think1"));
}
static void rotate_train_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  ro_ctx *c = vctx;
  if (e->target[0] == '\0') qa_range_error("rotate_train without target");
  if (e->speed == 0) e->speed = 100;
  e->solid = "none"; e->movement = "step";
  if (!qa_q1mp_text(e, "noise")[0]) qa_q1b_fields_set(e, "noise", e->sounds == 1 ? "plats/train2.wav" : "misc/null.wav");
  if (!qa_q1mp_text(e, "noise1")[0]) qa_q1b_fields_set(e, "noise1", e->sounds == 1 ? "plats/train1.wav" : "misc/null.wav");
  e->use = g->named.use(g->named.ctx, e, "hip:rotate_train");
  qa_q1b_fields_set(e, "think1", "hip:rotate_train_find");
  qa_q1mp_world_number(e, "rotate_state", 3);
  qa_q1mp_world_number(e, "ltime", g->time); qa_q1mp_world_number(e, "endtime", g->time + 0.1);
  qa_q1mp_world_number(e, "duration", 1); qa_q1mp_world_number(e, "cnt", 0.1);
  e->dest2 = QA_Q1MP_ZERO; e->dest1 = g->body(g, e).origin;
  qa_q1mp_world_later(c->game, e, 0.1, "hip:rotate_train");
}
static void clock_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  ro_ctx *c = vctx;
  double pos = (g->time + qa_q1mp_number(e,"cnt")) / e->count, angle = 360 * (pos - floor(pos));
  if (qa_q1mp_text(e, "event")[0] && qa_q1mp_number(e,"ltime") > angle) qa_q1mp_world_target_event(c->game, e, qa_q1mp_text(e, "event"), "");
  set_angles(c, e, qa_q1mp_vscale(e->movedir, angle));
  qa_q1mp_rotate_targets_final(c->game, e);
  qa_q1mp_world_number(e, "ltime", angle);
  qa_q1mp_world_later(c->game, e, 1, "hip:clock");
}
static void clock_first_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  ro_ctx *c = vctx; qa_q1mp_link_rotate_targets(c->game, e); call_action(g, e, "hip:clock");
}
static void clock_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  ro_ctx *c = vctx;
  qa_vec3 direction = qa_q1b_move_direction(g, g->body(g, e).angles);
  e->movedir = QA_Q1MP_V3(-direction.y, -direction.z, -direction.x);
  qa_q1b_body_patch p = {0}; p.has_angles = true; p.angles = QA_Q1MP_ZERO; g->set_body(g, e, &p);
  if (e->count == 0) e->count = 60;
  qa_q1mp_world_number(e, "cnt", qa_q1mp_number(e,"cnt") * e->count / 12);
  qa_q1mp_world_number(e, "ltime", g->time);
  qa_q1mp_world_later(c->game, e, 0.1, "hip:clock_first");
}
void qa_q1mp_register_hipnotic_rotation(qa_q1mp_game *game) {
  ro_ctx *c = malloc(sizeof *c); c->game = game; qa_q1b_game *g = game->base;
  g->register_spawn(g, "info_rotate", info_rotate_spawn, c);
  g->register_spawn(g, "path_rotate", path_rotate_spawn, c);
  g->register_spawn(g, "rotate_object", rotate_object_spawn, c);
  qa_q1b_handlers h = {0}; h.ctx = c; h.action = continuous_action; h.use = continuous_use;
  g->named.register_named(g->named.ctx, "hip:rotate_entity", &h);
  qa_q1b_handlers hf = {0}; hf.ctx = c; hf.action = rotate_first_cb;
  g->named.register_named(g->named.ctx, "hip:rotate_first", &hf);
  g->register_spawn(g, "func_rotate_entity", rotate_entity_spawn, c);
  qa_q1b_handlers hd = {0}; hd.ctx = c; hd.action = door_action; hd.use = door_use;
  g->named.register_named(g->named.ctx, "hip:rotate_door", &hd);
  qa_q1b_handlers hdd = {0}; hdd.ctx = c; hdd.action = door_done_cb;
  g->named.register_named(g->named.ctx, "hip:rotate_door_done", &hdd);
  g->register_spawn(g, "func_rotate_door", rotate_door_spawn, c);
  qa_q1b_handlers hm = {0}; hm.ctx = c; hm.action = movewall_action; hm.touch = movewall_touch; hm.blocked = movewall_blocked;
  g->named.register_named(g->named.ctx, "hip:movewall", &hm);
  g->register_spawn(g, "func_movewall", movewall_spawn, c);
  qa_q1b_handlers htn = {0}; htn.ctx = c; htn.action = train_next_cb;
  g->named.register_named(g->named.ctx, "hip:rotate_train_next", &htn);
  qa_q1b_handlers htw = {0}; htw.ctx = c; htw.action = train_wait_cb;
  g->named.register_named(g->named.ctx, "hip:rotate_train_wait", &htw);
  qa_q1b_handlers hts = {0}; hts.ctx = c; hts.action = train_stop_cb;
  g->named.register_named(g->named.ctx, "hip:rotate_train_stop", &hts);
  qa_q1b_handlers htf = {0}; htf.ctx = c; htf.action = train_find_cb;
  g->named.register_named(g->named.ctx, "hip:rotate_train_find", &htf);
  qa_q1b_handlers htr = {0}; htr.ctx = c; htr.action = rotate_train_action; htr.use = rotate_train_use;
  g->named.register_named(g->named.ctx, "hip:rotate_train", &htr);
  g->register_spawn(g, "func_rotate_train", rotate_train_spawn, c);
  qa_q1b_handlers hc = {0}; hc.ctx = c; hc.action = clock_cb;
  g->named.register_named(g->named.ctx, "hip:clock", &hc);
  qa_q1b_handlers hcf = {0}; hcf.ctx = c; hcf.action = clock_first_cb;
  g->named.register_named(g->named.ctx, "hip:clock_first", &hcf);
  g->register_spawn(g, "func_clock", clock_spawn, c);
}
