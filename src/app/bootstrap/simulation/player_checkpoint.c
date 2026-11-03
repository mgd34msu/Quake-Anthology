/* Port of src/app/bootstrap/simulation/player-checkpoint.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/player_checkpoint.h"
#include "qa/app/bootstrap/simulation/checkpoint_values.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/q1/foundation/types.h"
#include "qa/foundation/support.h"
#include "qa/persistence/recipe.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static qa_save_reader fld(const qa_save_reader *r, const char *n) {
  qa_save_reader o; if (!qa_save_reader_field(r, n, &o)) qa_save_reader_fail(r, "out of memory"); return o;
}
static double fnum(const qa_save_reader *r, const char *n) {
  qa_save_reader f = fld(r, n); double v = qa_save_reader_number(&f); qa_save_reader_destroy(&f); return v;
}
static bool fbool(const qa_save_reader *r, const char *n) {
  qa_save_reader f = fld(r, n); bool v = qa_save_reader_boolean(&f); qa_save_reader_destroy(&f); return v;
}
static qa_vec3 fvec(const qa_save_reader *r, const char *n) {
  qa_save_reader f = fld(r, n); qa_vec3 v = qa_read_vector(&f); qa_save_reader_destroy(&f); return v;
}
static qa_integer_triple ftriple(const qa_save_reader *r, const char *n) {
  qa_save_reader f = fld(r, n);
  size_t count = qa_save_reader_list_count(&f);
  if (count != 3) qa_save_reader_fail(&f, "expected three numbers");
  qa_integer_triple out;
  for (size_t i = 0; i < 3; i++) {
    qa_save_reader item; qa_save_reader_item(&f, i, &item);
    out.values[i] = qa_save_reader_number(&item); qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&f); return out;
}
static const char *fchoice(const qa_save_reader *r, const char *n, const char *const *c, size_t k) {
  qa_save_reader f = fld(r, n); const char *v = qa_save_reader_choice_string(&f, c, k); qa_save_reader_destroy(&f); return v;
}
static qa_save_value *saved_actor_value(qa_actor_id actor) {
  qa_saved_actor_id saved = qa_saved_actor_id_of(actor);
  qa_save_value *o = qa_save_value_object();
  if (!o) qa_range_error("Out of memory");
  qa_save_value_set(o, "slot", qa_save_value_number(saved.slot));
  qa_save_value_set(o, "generation", qa_save_value_number(saved.generation));
  return o;
}
qa_save_value *qa_app_sim_save_hit(qa_trace_hit hit) {
  qa_save_value *o = qa_save_value_object();
  if (!o) qa_range_error("Out of memory");
  if (hit.kind == QA_TRACE_HIT_ACTOR) {
    qa_save_value_set(o, "kind", qa_save_value_string("actor"));
    qa_save_value_set(o, "actor", saved_actor_value(hit.actor));
  } else if (hit.kind == QA_TRACE_HIT_WORLD) {
    qa_save_value_set(o, "kind", qa_save_value_string("world"));
    qa_save_value_set(o, "model", qa_save_value_number(hit.model));
  } else qa_save_value_set(o, "kind", qa_save_value_string("none"));
  return o;
}
qa_trace_hit qa_app_sim_read_hit(const qa_save_reader *reader, qa_app_sim_actor_reference_fn reference, void *reference_ctx) {
  static const char *const kinds[] = {"none", "world", "actor"};
  const char *kind = fchoice(reader, "kind", kinds, 3);
  qa_trace_hit out; memset(&out, 0, sizeof out);
  if (is(kind, "none")) { out.kind = QA_TRACE_HIT_NONE; return out; }
  if (is(kind, "world")) {
    out.kind = QA_TRACE_HIT_WORLD;
    qa_save_reader f = fld(reader, "model"); out.model = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
    return out;
  }
  out.kind = QA_TRACE_HIT_ACTOR;
  qa_save_reader f = fld(reader, "actor");
  out.actor = reference(reference_ctx, qa_read_saved_actor(&f));
  qa_save_reader_destroy(&f);
  return out;
}
static qa_movement_state read_movement(const qa_save_reader *reader, qa_app_sim_actor_reference_fn ref, void *ctx) {
  static const char *const kinds[] = {"q1-netquake", "q1-quakeworld", "q2-classic", "q2-rerelease", "q3"};
  const char *kind = fchoice(reader, "kind", kinds, 5);
  qa_movement_state out; memset(&out, 0, sizeof out);
  if (is(kind, "q1-netquake")) {
    out.kind = QA_MOVEMENT_STATE_Q1; qa_q1_movement_state *s = &out.as.q1;
    s->origin = fvec(reader, "origin"); s->velocity = fvec(reader, "velocity"); s->angles = fvec(reader, "angles");
    s->old_origin = fvec(reader, "oldOrigin"); s->angular_velocity = fvec(reader, "angularVelocity");
    s->view_angles = fvec(reader, "viewAngles"); s->punch_angles = fvec(reader, "punchAngles");
    s->move_type = fnum(reader, "moveType"); s->flags = fnum(reader, "flags");
    qa_save_reader g = fld(reader, "ground"); s->ground = qa_app_sim_read_hit(&g, ref, ctx); qa_save_reader_destroy(&g);
    s->water_level = fnum(reader, "waterLevel"); s->water_type = fnum(reader, "waterType");
    s->teleport_time_seconds = fnum(reader, "teleportTimeSeconds"); s->water_jump_direction = fvec(reader, "waterJumpDirection");
    s->ideal_pitch = fnum(reader, "idealPitch"); s->fix_angle = fbool(reader, "fixAngle"); s->health = fnum(reader, "health");
  } else if (is(kind, "q1-quakeworld")) {
    out.kind = QA_MOVEMENT_STATE_QW; qa_qw_movement_state *s = &out.as.qw;
    s->origin = fvec(reader, "origin"); s->velocity = fvec(reader, "velocity"); s->angles = fvec(reader, "angles");
    s->old_buttons = fnum(reader, "oldButtons"); s->water_jump_time_seconds = fnum(reader, "waterJumpTimeSeconds");
    s->dead = fbool(reader, "dead"); s->spectator = fnum(reader, "spectator");
    qa_save_reader g = fld(reader, "ground"); s->ground = qa_app_sim_read_hit(&g, ref, ctx); qa_save_reader_destroy(&g);
  } else if (is(kind, "q2-classic")) {
    out.kind = QA_MOVEMENT_STATE_Q2; qa_q2_movement_state *s = &out.as.q2;
    s->type = fnum(reader, "type"); s->origin_eighths = ftriple(reader, "originEighths");
    s->velocity_eighths = ftriple(reader, "velocityEighths"); s->flags = fnum(reader, "flags");
    s->time_eight_milliseconds = fnum(reader, "timeEightMilliseconds"); s->gravity = fnum(reader, "gravity");
    s->delta_angle_shorts = ftriple(reader, "deltaAngleShorts");
  } else if (is(kind, "q2-rerelease")) {
    out.kind = QA_MOVEMENT_STATE_Q2_RERELEASE; qa_q2_rerelease_movement_state *s = &out.as.q2_rerelease;
    s->type = fnum(reader, "type"); s->origin = fvec(reader, "origin"); s->velocity = fvec(reader, "velocity");
    s->flags = fnum(reader, "flags"); s->time_milliseconds = fnum(reader, "timeMilliseconds"); s->gravity = fnum(reader, "gravity");
    s->delta_angles = fvec(reader, "deltaAngles"); s->view_height = fnum(reader, "viewHeight");
  } else {
    out.kind = QA_MOVEMENT_STATE_Q3; qa_q3_movement_state *s = &out.as.q3;
    s->command_time_milliseconds = fnum(reader, "commandTimeMilliseconds"); s->movement_type = fnum(reader, "movementType");
    s->bob_cycle = fnum(reader, "bobCycle"); s->movement_flags = fnum(reader, "movementFlags");
    s->movement_time_milliseconds = fnum(reader, "movementTimeMilliseconds");
    s->origin = fvec(reader, "origin"); s->velocity = fvec(reader, "velocity"); s->gravity = fnum(reader, "gravity");
    s->speed = fnum(reader, "speed"); s->delta_angle_words = ftriple(reader, "deltaAngleWords");
    s->movement_direction = fnum(reader, "movementDirection"); s->grapple_point = fvec(reader, "grapplePoint");
    s->flags = fnum(reader, "flags"); s->view_angles = fvec(reader, "viewAngles"); s->view_height = fnum(reader, "viewHeight");
    qa_save_reader g = fld(reader, "ground"); s->ground = qa_app_sim_read_hit(&g, ref, ctx); qa_save_reader_destroy(&g);
    s->predictable_event_sequence = fnum(reader, "predictableEventSequence");
    qa_save_reader jp = fld(reader, "jumpPad");
    if (!qa_save_reader_is_null(&jp)) {
      static qa_actor_id pad; pad = ref(ctx, qa_read_saved_actor(&jp)); s->jump_pad = &pad;
    }
    qa_save_reader_destroy(&jp);
    s->movement_frame = fnum(reader, "movementFrame"); s->jump_pad_frame = fnum(reader, "jumpPadFrame");
  }
  return out;
}
static const char *fnamespaced_nullable(const qa_save_reader *r, const char *n) {
  qa_save_reader f = fld(r, n);
  const char *v = qa_save_reader_is_null(&f) ? NULL : qa_namespaced(&f);
  qa_save_reader_destroy(&f); return v;
}
static qa_weapon_state read_weapon(const qa_save_reader *reader) {
  static const char *const kinds[] = {"q1", "q2", "q3"};
  const char *kind = fchoice(reader, "kind", kinds, 3);
  qa_weapon_state out; memset(&out, 0, sizeof out); out.kind = kind;
  if (is(kind, "q1")) {
    out.frame = fnum(reader, "frame"); out.attack_finished_seconds = fnum(reader, "attackFinishedSeconds");
    out.source_weapon = fnum(reader, "sourceWeapon");
  } else if (is(kind, "q2")) {
    out.gun_frame = fnum(reader, "gunFrame"); out.state = fnum(reader, "state");
    out.pending_weapon = fnamespaced_nullable(reader, "pendingWeapon");
    out.machinegun_shots = fnum(reader, "machinegunShots");
    qa_save_reader f = fld(reader, "grenadeTime"); out.grenade_time = qa_read_time(&f); qa_save_reader_destroy(&f);
    out.grenade_blew_up = fbool(reader, "grenadeBlewUp");
  } else {
    out.source_weapon = fnum(reader, "sourceWeapon"); out.state = fnum(reader, "state");
    out.time_milliseconds = fnum(reader, "timeMilliseconds");
  }
  return out;
}
qa_animation_state qa_app_sim_read_animation(const qa_save_reader *reader) {
  static const char *const kinds[] = {"q1", "q2", "q3"};
  const char *kind = fchoice(reader, "kind", kinds, 3);
  qa_animation_state out; memset(&out, 0, sizeof out); out.kind = kind;
  if (is(kind, "q1")) { out.frame = fnum(reader, "frame"); out.next_frame_seconds = fnum(reader, "nextFrameSeconds"); }
  else if (is(kind, "q2")) {
    out.frame = fnum(reader, "frame"); out.end_frame = fnum(reader, "endFrame"); out.priority = fnum(reader, "priority");
    out.duck = fbool(reader, "duck"); out.run = fbool(reader, "run");
  } else {
    out.legs = fnum(reader, "legs"); out.torso = fnum(reader, "torso");
    out.legs_timer_milliseconds = fnum(reader, "legsTimerMilliseconds");
    out.torso_timer_milliseconds = fnum(reader, "torsoTimerMilliseconds");
  }
  return out;
}
static qa_save_value *vec_value(qa_vec3 v) {
  qa_save_value *o = qa_save_value_object(); if (!o) qa_range_error("Out of memory");
  qa_save_value_set(o, "x", qa_save_value_number(v.x));
  qa_save_value_set(o, "y", qa_save_value_number(v.y));
  qa_save_value_set(o, "z", qa_save_value_number(v.z));
  return o;
}
static qa_save_value *hit_value_plain(qa_trace_hit hit) { return qa_app_sim_save_hit(hit); }
static qa_save_value *movement_value(qa_movement_state state) {
  /* TS saveMovement spreads the state; the tree mirrors readMovement. */
  qa_save_value *o = qa_save_value_object(); if (!o) qa_range_error("Out of memory");
  switch (state.kind) {
    case QA_MOVEMENT_STATE_Q1: {
      qa_q1_movement_state s = state.as.q1;
      qa_save_value_set(o, "kind", qa_save_value_string("q1-netquake"));
      qa_save_value_set(o, "origin", vec_value(s.origin)); qa_save_value_set(o, "velocity", vec_value(s.velocity));
      qa_save_value_set(o, "angles", vec_value(s.angles)); qa_save_value_set(o, "oldOrigin", vec_value(s.old_origin));
      qa_save_value_set(o, "angularVelocity", vec_value(s.angular_velocity));
      qa_save_value_set(o, "viewAngles", vec_value(s.view_angles)); qa_save_value_set(o, "punchAngles", vec_value(s.punch_angles));
      qa_save_value_set(o, "moveType", qa_save_value_number(s.move_type)); qa_save_value_set(o, "flags", qa_save_value_number(s.flags));
      qa_save_value_set(o, "ground", hit_value_plain(s.ground));
      qa_save_value_set(o, "waterLevel", qa_save_value_number(s.water_level)); qa_save_value_set(o, "waterType", qa_save_value_number(s.water_type));
      qa_save_value_set(o, "teleportTimeSeconds", qa_save_value_number(s.teleport_time_seconds));
      qa_save_value_set(o, "waterJumpDirection", vec_value(s.water_jump_direction));
      qa_save_value_set(o, "idealPitch", qa_save_value_number(s.ideal_pitch));
      qa_save_value_set(o, "fixAngle", qa_save_value_boolean(s.fix_angle));
      qa_save_value_set(o, "health", qa_save_value_number(s.health)); break;
    }
    case QA_MOVEMENT_STATE_QW: {
      qa_qw_movement_state s = state.as.qw;
      qa_save_value_set(o, "kind", qa_save_value_string("q1-quakeworld"));
      qa_save_value_set(o, "origin", vec_value(s.origin)); qa_save_value_set(o, "velocity", vec_value(s.velocity));
      qa_save_value_set(o, "angles", vec_value(s.angles));
      qa_save_value_set(o, "oldButtons", qa_save_value_number(s.old_buttons));
      qa_save_value_set(o, "waterJumpTimeSeconds", qa_save_value_number(s.water_jump_time_seconds));
      qa_save_value_set(o, "dead", qa_save_value_boolean(s.dead));
      qa_save_value_set(o, "spectator", qa_save_value_number(s.spectator));
      qa_save_value_set(o, "ground", hit_value_plain(s.ground)); break;
    }
    case QA_MOVEMENT_STATE_Q2: case QA_MOVEMENT_STATE_Q2_RERELEASE: case QA_MOVEMENT_STATE_Q3:
      /* Full spread serialization for these kinds is produced by the
       * generic checkpoint writer in TS (state returned as-is); the
       * C capture writes kind + numeric fields read back above. */
      qa_save_value_set(o, "kind", qa_save_value_string(state.kind == QA_MOVEMENT_STATE_Q2 ? "q2-classic" : state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE ? "q2-rerelease" : "q3"));
      if (state.kind == QA_MOVEMENT_STATE_Q2) {
        qa_q2_movement_state s = state.as.q2;
        qa_save_value *a = qa_save_value_array(), *b = qa_save_value_array(), *d = qa_save_value_array();
        for (int i = 0; i < 3; i++) { qa_save_value_push(a, qa_save_value_number(s.origin_eighths.values[i])); qa_save_value_push(b, qa_save_value_number(s.velocity_eighths.values[i])); qa_save_value_push(d, qa_save_value_number(s.delta_angle_shorts.values[i])); }
        qa_save_value_set(o, "type", qa_save_value_number(s.type));
        qa_save_value_set(o, "originEighths", a); qa_save_value_set(o, "velocityEighths", b);
        qa_save_value_set(o, "flags", qa_save_value_number(s.flags));
        qa_save_value_set(o, "timeEightMilliseconds", qa_save_value_number(s.time_eight_milliseconds));
        qa_save_value_set(o, "gravity", qa_save_value_number(s.gravity));
        qa_save_value_set(o, "deltaAngleShorts", d);
      } else if (state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) {
        qa_q2_rerelease_movement_state s = state.as.q2_rerelease;
        qa_save_value_set(o, "type", qa_save_value_number(s.type));
        qa_save_value_set(o, "origin", vec_value(s.origin)); qa_save_value_set(o, "velocity", vec_value(s.velocity));
        qa_save_value_set(o, "flags", qa_save_value_number(s.flags));
        qa_save_value_set(o, "timeMilliseconds", qa_save_value_number(s.time_milliseconds));
        qa_save_value_set(o, "gravity", qa_save_value_number(s.gravity));
        qa_save_value_set(o, "deltaAngles", vec_value(s.delta_angles));
        qa_save_value_set(o, "viewHeight", qa_save_value_number(s.view_height));
      } else {
        qa_q3_movement_state s = state.as.q3;
        qa_save_value *d = qa_save_value_array();
        for (int i = 0; i < 3; i++) qa_save_value_push(d, qa_save_value_number(s.delta_angle_words.values[i]));
        qa_save_value_set(o, "commandTimeMilliseconds", qa_save_value_number(s.command_time_milliseconds));
        qa_save_value_set(o, "movementType", qa_save_value_number(s.movement_type));
        qa_save_value_set(o, "bobCycle", qa_save_value_number(s.bob_cycle));
        qa_save_value_set(o, "movementFlags", qa_save_value_number(s.movement_flags));
        qa_save_value_set(o, "movementTimeMilliseconds", qa_save_value_number(s.movement_time_milliseconds));
        qa_save_value_set(o, "origin", vec_value(s.origin)); qa_save_value_set(o, "velocity", vec_value(s.velocity));
        qa_save_value_set(o, "gravity", qa_save_value_number(s.gravity)); qa_save_value_set(o, "speed", qa_save_value_number(s.speed));
        qa_save_value_set(o, "deltaAngleWords", d);
        if (s.ground_after_delta_angles) qa_save_value_set(o, "ground", hit_value_plain(s.ground));
        qa_save_value_set(o, "movementDirection", qa_save_value_number(s.movement_direction));
        qa_save_value_set(o, "grapplePoint", vec_value(s.grapple_point));
        qa_save_value_set(o, "flags", qa_save_value_number(s.flags));
        qa_save_value_set(o, "viewAngles", vec_value(s.view_angles));
        qa_save_value_set(o, "viewHeight", qa_save_value_number(s.view_height));
        if (!s.ground_after_delta_angles) qa_save_value_set(o, "ground", hit_value_plain(s.ground));
        qa_save_value_set(o, "predictableEventSequence", qa_save_value_number(s.predictable_event_sequence));
        qa_save_value_set(o, "jumpPad", s.jump_pad ? saved_actor_value(*s.jump_pad) : qa_save_value_null());
        qa_save_value_set(o, "movementFrame", qa_save_value_number(s.movement_frame));
        qa_save_value_set(o, "jumpPadFrame", qa_save_value_number(s.jump_pad_frame));
      }
      break;
  }
  return o;
}
qa_save_value *qa_app_sim_capture_movement_player(const qa_app_sim_movement_player *player) {
  qa_save_value *o = qa_save_value_object(); if (!o) qa_range_error("Out of memory");
  qa_save_value_set(o, "version", qa_save_value_number(1));
  /* characterDefinition/Appearance as provider references. */
  qa_save_value *def = qa_save_value_object(), *app = qa_save_value_object();
  qa_save_value_set(def, "provider", qa_save_value_string(player->character_definition.provider));
  qa_save_value_set(def, "content", qa_save_value_string(player->character_definition.content));
  qa_save_value_set(app, "provider", qa_save_value_string(player->character_appearance.provider));
  qa_save_value_set(app, "content", qa_save_value_string(player->character_appearance.content));
  qa_save_value_set(o, "characterDefinition", def); qa_save_value_set(o, "characterAppearance", app);
  if (player->has_body_shape_base) {
    qa_save_value *b = qa_save_value_object();
    qa_save_value_set(b, "min", vec_value(player->body_shape_base.min));
    qa_save_value_set(b, "max", vec_value(player->body_shape_base.max));
    qa_save_value_set(o, "bodyShapeBase", b);
  } else qa_save_value_set(o, "bodyShapeBase", qa_save_value_null());
  qa_save_value_set(o, "fixedPoseActive", qa_save_value_boolean(player->fixed_pose_active));
  if (!player->has_arsenal_intent || player->arsenal_intent.impulse == 0) qa_save_value_set(o, "arsenalIntent", qa_save_value_null());
  else {
    qa_save_value *intent = qa_save_value_object();
    qa_save_value_set(intent, "provider", qa_save_value_string(player->arsenal_intent.provider));
    qa_save_value_set(intent, "weapon", qa_save_value_null());
    qa_save_value_set(intent, "useHoldable", qa_save_value_boolean(false));
    qa_save_value_set(intent, "impulse", qa_save_value_number(player->arsenal_intent.impulse));
    qa_save_value_set(o, "arsenalIntent", intent);
  }
  if (!player->has_netquake_command) qa_save_value_set(o, "netQuakeCommand", qa_save_value_null());
  else {
    qa_save_value *c = qa_save_value_object(); qa_q1_user_command cmd = player->netquake_command;
    qa_save_value_set(c, "kind", qa_save_value_string("q1-netquake"));
    qa_save_value_set(c, "acknowledgedServerTimeSeconds", qa_save_value_number(cmd.acknowledged_server_time_seconds));
    qa_save_value_set(c, "viewAngles", vec_value(cmd.view_angles));
    qa_save_value_set(c, "forwardMove", qa_save_value_number(cmd.forward_move));
    qa_save_value_set(c, "sideMove", qa_save_value_number(cmd.side_move));
    qa_save_value_set(c, "upMove", qa_save_value_number(cmd.up_move));
    qa_save_value_set(c, "buttons", qa_save_value_number(cmd.buttons));
    qa_save_value_set(c, "impulse", qa_save_value_number(cmd.impulse));
    qa_save_value_set(o, "netQuakeCommand", c);
  }
  qa_save_value_set(o, "actor", saved_actor_value(player->actor.id));
  qa_save_value_set(o, "clientSlot", qa_save_value_number((double)player->client.slot));
  qa_save_value_set(o, "state", movement_value(player->state));
  /* arsenal: player.arsenal (the arsenal literal its owner built, in that key order). */
  qa_save_value_set(o, "arsenal", qa_app_sim_arsenal_state_value(&player->arsenal));
  /* animation spread: read back by readActorAnimation. */
  qa_save_value *anim = qa_save_value_object();
  qa_save_value_set(anim, "provider", qa_save_value_string(player->animation.provider));
  qa_save_value *astate = qa_save_value_object();
  /* TS AnimationState is a union on kind: each kind carries only its own members. */
  const char *animation_kind = player->animation.state.kind ? player->animation.state.kind : "q1";
  qa_save_value_set(astate, "kind", qa_save_value_string(animation_kind));
  if (is(animation_kind, "q1")) {
    qa_save_value_set(astate, "frame", qa_save_value_number(player->animation.state.frame));
    qa_save_value_set(astate, "nextFrameSeconds", qa_save_value_number(player->animation.state.next_frame_seconds));
  } else if (is(animation_kind, "q2")) {
    qa_save_value_set(astate, "frame", qa_save_value_number(player->animation.state.frame));
    qa_save_value_set(astate, "endFrame", qa_save_value_number(player->animation.state.end_frame));
    qa_save_value_set(astate, "priority", qa_save_value_number(player->animation.state.priority));
    qa_save_value_set(astate, "duck", qa_save_value_boolean(player->animation.state.duck));
    qa_save_value_set(astate, "run", qa_save_value_boolean(player->animation.state.run));
  } else {
    qa_save_value_set(astate, "legs", qa_save_value_number(player->animation.state.legs));
    qa_save_value_set(astate, "torso", qa_save_value_number(player->animation.state.torso));
    qa_save_value_set(astate, "legsTimerMilliseconds", qa_save_value_number(player->animation.state.legs_timer_milliseconds));
    qa_save_value_set(astate, "torsoTimerMilliseconds", qa_save_value_number(player->animation.state.torso_timer_milliseconds));
  }
  qa_save_value_set(anim, "state", astate);
  qa_save_value_set(o, "animation", anim);
  qa_save_value_set(o, "viewAngles", vec_value(player->view_angles));
  qa_save_value_set(o, "commandAngles", vec_value(player->command_angles));
  qa_save_value_set(o, "viewHeight", qa_save_value_number(player->view_height));
  qa_save_value *bounds = qa_save_value_object();
  qa_save_value_set(bounds, "min", vec_value(player->bounds.min));
  qa_save_value_set(bounds, "max", vec_value(player->bounds.max));
  qa_save_value_set(o, "bounds", bounds);
  qa_save_value_set(o, "ground", hit_value_plain(player->ground));
  qa_save_value_set(o, "waterLevel", qa_save_value_number(player->water_level));
  qa_save_value_set(o, "waterType", qa_save_value_number(player->water_type));
  qa_save_value_set(o, "intermission", qa_save_value_boolean(player->intermission));
  if (!player->has_cutscene) qa_save_value_set(o, "cutscene", qa_save_value_null());
  else {
    qa_save_value *c = qa_save_value_object();
    qa_save_value_set(c, "origin", vec_value(player->cutscene_origin));
    qa_save_value_set(c, "angles", vec_value(player->cutscene_angles));
    qa_save_value_set(c, "viewOffset", vec_value(player->cutscene_view_offset));
    qa_save_value_set(o, "cutscene", c);
  }
  qa_save_value_set(o, "flight", qa_save_value_boolean(player->flight));
  qa_save_value_set(o, "gravityMultiplier", qa_save_value_number(player->gravity_multiplier));
  qa_save_value_set(o, "worldGravity", qa_save_value_number(player->world_gravity));
  qa_save_value_set(o, "buttons", qa_save_value_number(player->buttons));
  qa_save_value_set(o, "previousButtons", qa_save_value_number(player->previous_buttons));
  qa_save_value_set(o, "lastSequence", qa_save_value_number(player->last_sequence));
  qa_save_value_set(o, "lastWeaponSeconds", qa_save_value_number(player->last_weapon_seconds));
  return o;
}
qa_app_sim_saved_movement_player qa_app_sim_read_movement_player(const qa_save_reader *reader, qa_app_sim_actor_reference_fn ref, void *ctx) {
  qa_save_reader v = fld(reader, "version"); qa_save_reader_literal_number(&v, 1); qa_save_reader_destroy(&v);
  qa_app_sim_saved_movement_player out; memset(&out, 0, sizeof out);
  qa_save_reader saved_intent = fld(reader, "arsenalIntent");
  if (!qa_save_reader_is_undefined(&saved_intent) && !qa_save_reader_is_null(&saved_intent)) {
    out.has_arsenal_intent = true;
    qa_save_reader imp = fld(&saved_intent, "impulse");
    bool has_imp = !qa_save_reader_is_undefined(&imp);
    double number = has_imp ? qa_save_reader_integer_min(&imp, 0) : 0;
    if (has_imp && number > 255) qa_save_reader_fail(&imp, "source impulse exceeds one byte");
    qa_save_reader_destroy(&imp);
    qa_save_reader pf = fld(&saved_intent, "provider"); out.arsenal_intent.provider = qa_namespaced(&pf); qa_save_reader_destroy(&pf);
    out.arsenal_intent.weapon = fnamespaced_nullable(&saved_intent, "weapon");
    out.arsenal_intent.use_holdable = fbool(&saved_intent, "useHoldable");
    out.arsenal_intent.has_impulse = has_imp; out.arsenal_intent.impulse = number;
  }
  qa_save_reader_destroy(&saved_intent);
  qa_save_reader nqc = fld(reader, "netQuakeCommand");
  if (!qa_save_reader_is_null(&nqc)) {
    out.has_netquake_command = true;
    qa_save_reader k = fld(&nqc, "kind"); qa_save_reader_literal_string(&k, "q1-netquake"); qa_save_reader_destroy(&k);
    qa_save_reader f;
    f = fld(&nqc, "acknowledgedServerTimeSeconds"); out.netquake_command.acknowledged_server_time_seconds = qa_save_reader_finite(&f); qa_save_reader_destroy(&f);
    out.netquake_command.view_angles = fvec(&nqc, "viewAngles");
    f = fld(&nqc, "forwardMove"); out.netquake_command.forward_move = qa_save_reader_finite(&f); qa_save_reader_destroy(&f);
    f = fld(&nqc, "sideMove"); out.netquake_command.side_move = qa_save_reader_finite(&f); qa_save_reader_destroy(&f);
    f = fld(&nqc, "upMove"); out.netquake_command.up_move = qa_save_reader_finite(&f); qa_save_reader_destroy(&f);
    f = fld(&nqc, "buttons"); out.netquake_command.buttons = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
    f = fld(&nqc, "impulse"); out.netquake_command.impulse = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
  }
  qa_save_reader_destroy(&nqc);
  qa_save_reader cd = fld(reader, "characterDefinition");
  if (!qa_save_reader_is_undefined(&cd)) {
    out.has_character = true; out.character_definition = qa_read_provider(&cd);
    qa_save_reader ca = fld(reader, "characterAppearance");
    if (!qa_save_reader_is_undefined(&ca)) { out.has_character_appearance = true; out.character_appearance = qa_read_provider(&ca); }
    qa_save_reader_destroy(&ca);
  }
  qa_save_reader_destroy(&cd);
  qa_save_reader bsb = fld(reader, "bodyShapeBase");
  if (qa_save_reader_is_undefined(&bsb)) out.has_body_shape_base = false;
  else if (qa_save_reader_is_null(&bsb)) out.has_body_shape_base = false;
  else { out.has_body_shape_base = true; out.body_shape_base = qa_read_bounds(&bsb); }
  qa_save_reader_destroy(&bsb);
  qa_save_reader fp = fld(reader, "fixedPoseActive");
  out.fixed_pose_active = qa_save_reader_is_undefined(&fp) ? false : qa_save_reader_boolean(&fp);
  qa_save_reader_destroy(&fp);
  qa_save_reader fl = fld(reader, "flight");
  out.flight = qa_save_reader_is_undefined(&fl) ? false : qa_save_reader_boolean(&fl);
  qa_save_reader_destroy(&fl);
  qa_save_reader af = fld(reader, "actor"); out.actor = qa_read_saved_actor(&af); qa_save_reader_destroy(&af);
  qa_save_reader cs = fld(reader, "clientSlot"); out.client_slot = qa_save_reader_integer_min(&cs, 0); qa_save_reader_destroy(&cs);
  qa_save_reader st = fld(reader, "state"); out.state = read_movement(&st, ref, ctx); qa_save_reader_destroy(&st);
  qa_save_reader ar = fld(reader, "arsenal");
  qa_save_reader pf2 = fld(&ar, "provider"); out.arsenal.provider = qa_namespaced(&pf2); qa_save_reader_destroy(&pf2);
  out.arsenal.active_weapon = fnamespaced_nullable(&ar, "activeWeapon");
  qa_save_reader ws = fld(&ar, "state"); out.arsenal.state = read_weapon(&ws); qa_save_reader_destroy(&ws);
  qa_save_reader ammo = fld(&ar, "ammo");
  size_t ammo_count = qa_save_reader_list_count(&ammo);
  out.ammo = ammo_count ? malloc(ammo_count * sizeof *out.ammo) : NULL;
  for (size_t i = 0; i < ammo_count; i++) {
    qa_save_reader e; qa_save_reader_item(&ammo, i, &e);
    out.ammo[i] = qa_read_inventory_entry(&e); qa_save_reader_destroy(&e);
  }
  out.arsenal.ammo = out.ammo; out.arsenal.ammo_count = ammo_count;
  qa_save_reader_destroy(&ammo); qa_save_reader_destroy(&ar);
  qa_save_reader an = fld(reader, "animation");
  qa_save_reader ap = fld(&an, "provider"); out.animation.provider = qa_namespaced(&ap); qa_save_reader_destroy(&ap);
  qa_save_reader as = fld(&an, "state"); out.animation.state = qa_app_sim_read_animation(&as); qa_save_reader_destroy(&as);
  qa_save_reader_destroy(&an);
  out.view_angles = fvec(reader, "viewAngles"); out.command_angles = fvec(reader, "commandAngles");
  out.view_height = fnum(reader, "viewHeight");
  qa_save_reader bd = fld(reader, "bounds"); out.bounds = qa_read_bounds(&bd); qa_save_reader_destroy(&bd);
  qa_save_reader gr = fld(reader, "ground"); out.ground = qa_app_sim_read_hit(&gr, ref, ctx); qa_save_reader_destroy(&gr);
  out.water_level = fnum(reader, "waterLevel"); out.water_type = fnum(reader, "waterType");
  out.intermission = fbool(reader, "intermission");
  out.gravity_multiplier = fnum(reader, "gravityMultiplier"); out.world_gravity = fnum(reader, "worldGravity");
  out.buttons = fnum(reader, "buttons"); out.previous_buttons = fnum(reader, "previousButtons");
  qa_save_reader ls = fld(reader, "lastSequence"); out.last_sequence = qa_save_reader_integer_min(&ls, -1); qa_save_reader_destroy(&ls);
  out.last_weapon_seconds = fnum(reader, "lastWeaponSeconds");
  qa_save_reader cut = fld(reader, "cutscene");
  if (!qa_save_reader_is_null(&cut)) {
    out.has_cutscene = true;
    out.cutscene_origin = fvec(&cut, "origin"); out.cutscene_angles = fvec(&cut, "angles"); out.cutscene_view_offset = fvec(&cut, "viewOffset");
  }
  qa_save_reader_destroy(&cut);
  return out;
}
qa_q3_character_checkpoint qa_app_sim_read_q3_character(const qa_save_reader *reader) {
  qa_save_reader an = fld(reader, "animation");
  qa_animation_state animation = qa_app_sim_read_animation(&an);
  qa_save_reader_destroy(&an);
  if (!is(animation.kind, "q3")) qa_save_reader_fail(reader, "Q3 character needs Q3 animation state");
  static const char *const products[] = {"baseq3", "missionpack"};
  qa_q3_character_checkpoint out; memset(&out, 0, sizeof out);
  qa_save_reader v = fld(reader, "version"); out.version = qa_save_reader_literal_number(&v, 1); qa_save_reader_destroy(&v);
  out.product = fchoice(reader, "product", products, 2);
  out.animation = animation;
  out.flags = fnum(reader, "flags");
  qa_save_reader f;
  f = fld(reader, "eventSequence"); out.event_sequence = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
  out.respawn_time = fnum(reader, "respawnTime");
  f = fld(reader, "spawnCount"); out.spawn_count = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
  out.dead = fbool(reader, "dead"); out.gibbed = fbool(reader, "gibbed"); out.initialized = fbool(reader, "initialized");
  return out;
}
qa_q1_travel_state qa_app_sim_read_q1_travel(const qa_save_reader *reader) {
  qa_save_reader wf = fld(reader, "weapon");
  const char *weapon = qa_save_reader_string(&wf); qa_save_reader_destroy(&wf);
  const char *selected = NULL;
  for (int i = 0; i < 20; i++) if (is(QA_Q1_WEAPON_IDS[i], weapon)) selected = QA_Q1_WEAPON_IDS[i];
  if (selected == NULL) qa_save_reader_fail(reader, "Unknown Q1 saved weapon %s", weapon);
  qa_q1_travel_state out; memset(&out, 0, sizeof out);
  out.health = fnum(reader, "health"); out.max_health = fnum(reader, "maxHealth");
  qa_save_reader af = fld(reader, "armor"); out.armor = qa_read_armor(&af); qa_save_reader_destroy(&af);
  qa_save_reader inv = fld(reader, "inventory");
  out.inventory_count = qa_save_reader_list_count(&inv);
  out.inventory = out.inventory_count ? malloc(out.inventory_count * sizeof *out.inventory) : NULL;
  out.inventory_items = out.inventory_count ? malloc(out.inventory_count * sizeof *out.inventory_items) : NULL;
  for (size_t i = 0; i < out.inventory_count; i++) {
    qa_save_reader e; qa_save_reader_item(&inv, i, &e);
    out.inventory[i] = qa_read_inventory_entry(&e);
    /* owned parallel item strings */
    size_t n = strlen(out.inventory[i].item); char *copy = malloc(n + 1); memcpy(copy, out.inventory[i].item, n + 1);
    out.inventory_items[i] = copy; out.inventory[i].item = copy;
    qa_save_reader_destroy(&e);
  }
  qa_save_reader_destroy(&inv);
  out.weapon = selected;
  qa_save_reader ext = fld(reader, "extensions");
  out.extension_count = qa_save_reader_list_count(&ext);
  out.extensions = out.extension_count ? calloc(out.extension_count, sizeof *out.extensions) : NULL;
  for (size_t i = 0; i < out.extension_count; i++) {
    qa_save_reader e, f;
    qa_save_reader_item(&ext, i, &e);
    f = fld(&e, "id"); const char *id = qa_save_reader_string(&f); qa_save_reader_destroy(&f);
    size_t idn = strlen(id); out.extensions[i].id = malloc(idn + 1); memcpy(out.extensions[i].id, id, idn + 1);
    f = fld(&e, "bytes"); size_t len = 0; const uint8_t *bytes = qa_save_reader_bytes(&f, &len); qa_save_reader_destroy(&f);
    out.extensions[i].bytes = len ? malloc(len) : NULL; if (len) memcpy(out.extensions[i].bytes, bytes, len);
    out.extensions[i].length = len;
    qa_save_reader_destroy(&e);
  }
  qa_save_reader_destroy(&ext);
  return out;
}
qa_q2_base_player_view qa_app_sim_read_q2_view(const qa_save_reader *reader) {
  qa_q2_base_player_view out; memset(&out, 0, sizeof out);
  out.angles = fvec(reader, "angles"); out.offset = fvec(reader, "offset");
  out.kick_angles = fvec(reader, "kickAngles"); out.gun_angles = fvec(reader, "gunAngles"); out.gun_offset = fvec(reader, "gunOffset");
  qa_save_reader blend = fld(reader, "blend");
  qa_vec3 b = qa_read_vector(&blend);
  out.blend = (qa_vec4){b.x, b.y, b.z, fnum(&blend, "w")};
  qa_save_reader_destroy(&blend);
  out.fov = fnum(reader, "fov"); out.underwater = fbool(reader, "underwater");
  out.flashes = fnum(reader, "flashes"); out.health = fnum(reader, "health"); out.armor = fnum(reader, "armor");
  out.ammo = fnum(reader, "ammo"); out.score = fnum(reader, "score");
  out.selected_item = fnamespaced_nullable(reader, "selectedItem");
  qa_save_reader timer = fld(reader, "timer");
  if (!qa_save_reader_is_null(&timer)) {
    out.has_timer = true;
    qa_save_reader f = fld(&timer, "item"); out.timer_item = qa_namespaced(&f); qa_save_reader_destroy(&f);
    out.timer_seconds = fnum(&timer, "seconds");
  }
  qa_save_reader_destroy(&timer);
  out.spectator = fbool(reader, "spectator"); out.layouts = fnum(reader, "layouts");
  return out;
}
