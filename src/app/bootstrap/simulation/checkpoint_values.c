/* Checkpoint-value trees of the live TS records checkpointFromSource
 * (runtime.ts) encodes; see checkpoint_values.h. Key order follows the
 * TS object each record is built from, which the comment on each
 * builder names. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/checkpoint_values.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/checkpoint.h"
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/foundation/support.h"
#include "qa/persistence/q1_foundation.h"
#include "qa/persistence/q2_base_entities.h"
#include "qa/persistence/q2_foundation.h"
#include "qa/persistence/q2_hand_grenades.h"
#include "qa/persistence/q2_items.h"
#include "qa/persistence/q2_missionpacks.h"
#include "qa/persistence/q2_monsters.h"
#include "qa/persistence/q2_movers.h"
#include "qa/persistence/save_image.h"

static void cv_oom(const void *p) { if (p == NULL) qa_range_error("Out of memory"); }
static qa_save_value *cv_obj(void) { qa_save_value *v = qa_save_value_object(); cv_oom(v); return v; }
static qa_save_value *cv_arr(void) { qa_save_value *v = qa_save_value_array(); cv_oom(v); return v; }
static void cv_set(qa_save_value *o, const char *key, qa_save_value *child) {
  cv_oom(child);
  if (!qa_save_value_set(o, key, child)) { qa_save_value_free(child); cv_oom(NULL); }
}
static void cv_push(qa_save_value *a, qa_save_value *child) {
  cv_oom(child);
  if (!qa_save_value_push(a, child)) { qa_save_value_free(child); cv_oom(NULL); }
}
static qa_save_value *cv_num(double v) { return qa_save_value_number(v); }
static qa_save_value *cv_str(const char *v) { return qa_save_value_string(v); }
static qa_save_value *cv_bool(bool v) { return qa_save_value_boolean(v); }
static qa_save_value *cv_null(void) { return qa_save_value_null(); }
static qa_save_value *cv_nullable_str(const char *v) { return v == NULL ? cv_null() : cv_str(v); }
static qa_save_value *cv_saved(qa_saved_actor_id actor) { return qa_save_actor_value(actor); }
static qa_save_value *cv_nullable_saved(bool has, qa_saved_actor_id actor) { return has ? cv_saved(actor) : cv_null(); }
static qa_save_value *cv_vector(qa_vec3 v) { return qa_save_vector_value(v); }
/* encodeCheckpointValue bytes of an owner codec, as the tree TS embeds.
 * length is read through a pointer: the encoder call that sets it is an
 * argument of the same call, and C leaves argument order unspecified. */
static qa_save_value *cv_tree(uint8_t *bytes, const size_t *length) {
  cv_oom(bytes);
  qa_save_value *value = qa_decode_checkpoint_value(bytes, *length);
  free(bytes);
  cv_oom(value);
  return value;
}

/* SourceRandom.checkpoint(): glibc { kind, words, front, rear, draws } or
 * MT19937 { kind, distribution, words, index, draws }, via the shared
 * RandomState writer. */
static qa_save_value *cv_random(qa_app_sim_source_random_state state) {
  double words[QA_Q2_RERELEASE_WORDS];
  qa_random_state out; memset(&out, 0, sizeof out);
  if (state.kind != NULL && strcmp(state.kind, "q2-rerelease-mt19937") == 0) {
    out.kind = QA_RANDOM_STATE_Q2_RERELEASE_MT19937;
    for (size_t i = 0; i < QA_Q2_RERELEASE_WORDS; i++) words[i] = state.rerelease.words[i];
    out.words = (qa_double_view){words, QA_Q2_RERELEASE_WORDS};
    out.index = (double)state.rerelease.index; out.draws = (double)state.rerelease.draws;
  } else {
    out.kind = QA_RANDOM_STATE_GLIBC_RANDOM;
    for (size_t i = 0; i < 31; i++) words[i] = state.words[i];
    out.words = (qa_double_view){words, 31};
    out.front = state.front; out.rear = state.rear; out.draws = (double)state.draws;
  }
  return qa_save_random_value(out);
}

static const char *cv_external_slot(qa_q3_external_weapon_slot slot) {
  switch (slot) {
    case QA_Q3_WEAPON_SLOT_ACTIVE: return "active";
    case QA_Q3_WEAPON_SLOT_HOLSTER_REQUESTED: return "holster-requested";
    case QA_Q3_WEAPON_SLOT_DROPPING: return "dropping";
    case QA_Q3_WEAPON_SLOT_HOLSTERED: return "holstered";
    case QA_Q3_WEAPON_SLOT_RESUME_REQUESTED: return "resume-requested";
  }
  return "active";
}
/* q3SpawnArsenalRuntime literal; stepQ3Arsenal, readQ3ArsenalRuntime and
 * the request helpers spread it, so the order never changes. */
qa_save_value *qa_app_q3_arsenal_runtime_state_value(const qa_q3_arsenal_runtime_state *state) {
  qa_save_value *o = cv_obj();
  cv_set(o, "product", cv_str(state->product));
  cv_set(o, "maxHealth", cv_num(state->max_health));
  cv_set(o, "spectator", cv_bool(state->spectator));
  cv_set(o, "persistentPowerupTag", cv_num(state->persistent_powerup_tag));
  cv_set(o, "holdableItem", cv_num(state->holdable_item));
  cv_set(o, "holdableTag", cv_num(state->holdable_tag));
  cv_set(o, "respawned", cv_bool(state->respawned));
  cv_set(o, "useItemHeld", cv_bool(state->use_item_held));
  cv_set(o, "eventSequence", cv_num(state->event_sequence));
  cv_set(o, "fractionalMilliseconds", cv_num(state->fractional_milliseconds));
  cv_set(o, "externalSlot", cv_str(cv_external_slot(state->external_slot)));
  cv_set(o, "requestedWeapon", state->has_requested_weapon ? cv_num(state->requested_weapon) : cv_null());
  return o;
}

/* player.sourceMovement: the moveClient options literal that produced it
 * (ClientThink, the spectator policy) or the restored record. */
qa_save_value *qa_app_sim_movement_player_source_movement_value(const qa_app_sim_movement_player *player) {
  if (!player->has_source_movement) return cv_null();
  const qa_app_sim_client_movement_options *m = &player->source_movement;
  qa_save_value *o = cv_obj();
  qa_save_value *fixed = m->has_fixed_msec ? cv_num(m->fixed_msec) : cv_null();
  switch (m->order) {
    case QA_APP_SIM_MOVEMENT_ORDER_THINK:
      cv_set(o, "debugLevel", cv_num(m->debug_level));
      cv_set(o, "traceMask", cv_num(m->trace_mask));
      cv_set(o, "fixedMsec", fixed);
      cv_set(o, "gauntletHit", cv_bool(m->gauntlet_hit));
      cv_set(o, "noFootsteps", cv_bool(m->no_footsteps));
      cv_set(o, "ladders", cv_bool(m->ladders)); /* ClientThink: settings.ladders !== false */
      break;
    case QA_APP_SIM_MOVEMENT_ORDER_SPECTATOR:
      cv_set(o, "debugLevel", cv_num(m->debug_level));
      cv_set(o, "traceMask", cv_num(m->trace_mask));
      cv_set(o, "fixedMsec", fixed);
      cv_set(o, "noFootsteps", cv_bool(m->no_footsteps));
      cv_set(o, "gauntletHit", cv_bool(m->gauntlet_hit));
      break;
    case QA_APP_SIM_MOVEMENT_ORDER_SAVED:
      cv_set(o, "traceMask", cv_num(m->trace_mask));
      cv_set(o, "fixedMsec", fixed);
      cv_set(o, "noFootsteps", cv_bool(m->no_footsteps));
      cv_set(o, "gauntletHit", cv_bool(m->gauntlet_hit));
      cv_set(o, "debugLevel", cv_num(m->debug_level));
      break;
  }
  return o;
}

qa_save_value *qa_app_sim_save_q2_attack_value(const qa_attack_provenance *attack) {
  qa_q2_attack_checkpoint saved = qa_q2_save_attack(*attack);
  qa_save_value *value = qa_q2_attack_checkpoint_value(&saved);
  cv_oom(value);
  return value;
}

/* HandGrenadeRuntime.capture(): { version, controller, controls, source }. */
qa_save_value *qa_app_sim_hand_grenade_runtime_checkpoint_value(const qa_app_sim_hand_grenade_runtime_checkpoint *checkpoint) {
  qa_save_value *o = cv_obj();
  cv_set(o, "version", cv_num(checkpoint->version));
  /* Q2HandGrenadeEquipment.capture() through the persistence codec. */
  const qa_hand_grenades_checkpoint *controller = &checkpoint->controller;
  qa_hand_grenades_actor *actors = calloc(controller->actor_count != 0 ? controller->actor_count : 1, sizeof *actors);
  cv_oom(actors);
  for (size_t i = 0; i < controller->actor_count; i++) {
    const qa_hand_grenade_checkpoint_entry *from = &controller->actors[i];
    actors[i].actor = from->actor;
    actors[i].state.config.enabled = from->state.config.enabled;
    actors[i].state.config.initial_ammo = from->state.config.initial_ammo;
    actors[i].state.config.capacity = from->state.config.capacity;
    actors[i].state.config.infinite_ammo = from->state.config.infinite_ammo;
    qa_hand_action *action = &actors[i].state.action;
    action->kind = (qa_hand_action_kind)from->state.action.kind;
    action->frame = from->state.action.frame;
    action->release_queued = from->state.action.release_queued;
    action->next_at = from->state.action.next_at;
    action->expires_at = from->state.action.expires_at;
    action->throw_at = from->state.action.throw_at;
    action->ready_at = from->state.action.ready_at;
    action->require_release = from->state.action.require_release;
  }
  qa_persist_hand_grenades_checkpoint persisted = {controller->version,
      controller->edition == QA_Q2_EDITION_RERELEASE ? "rerelease" : "classic", actors, controller->actor_count, NULL};
  size_t length = 0;
  uint8_t *bytes = qa_encode_hand_grenades_checkpoint(&persisted, &length);
  free(actors);
  cv_set(o, "controller", cv_tree(bytes, &length));
  qa_save_value *controls = cv_arr();
  for (size_t i = 0; i < checkpoint->control_count; i++) {
    const qa_app_sim_hand_grenade_control *control = &checkpoint->controls[i];
    qa_save_value *entry = cv_obj();
    cv_set(entry, "actor", cv_saved(control->actor));
    cv_set(entry, "held", cv_bool(control->held));
    cv_set(entry, "pressed", cv_bool(control->pressed));
    cv_set(entry, "released", cv_bool(control->released));
    cv_push(controls, entry);
  }
  cv_set(o, "controls", controls);
  qa_save_value *source = cv_obj();
  cv_set(source, "entities", cv_tree(qa_encode_q2_foundation_checkpoint(&checkpoint->entities, &length), &length));
  cv_set(source, "random", cv_random(checkpoint->random));
  cv_set(o, "source", source);
  return o;
}

/* createGrappleWeaponState() literal: { animation, handoff }, or readGrappleWeaponState's { handoff, animation }
 * (read_order); the state and its animation are mutated in place, so each keeps the order it was built with. */
static qa_save_value *cv_grapple_weapon_state(const qa_grapple_weapon_state *state, bool read_order) {
  static const char *const handoffs[] = {"active", "holstering", "holstered"};
  qa_save_value *o = cv_obj(), *animation = cv_obj();
  cv_set(animation, "phase", cv_str(qa_q2w_phase_name(state->animation.phase)));
  cv_set(animation, "frame", cv_num(state->animation.frame));
  cv_set(animation, "latchedAttack", cv_bool(state->animation.latched_attack));
  cv_set(animation, "sourceFiring", cv_bool(state->animation.source_firing));
  cv_set(animation, "thinkTime", cv_num(state->animation.think_time));
  cv_set(animation, "fireFinished", cv_num(state->animation.fire_finished));
  cv_set(animation, "fireBuffered", cv_bool(state->animation.fire_buffered));
  cv_set(animation, "lastFiringTime", cv_num(state->animation.last_firing_time));
  if (read_order) cv_set(o, "handoff", cv_str(handoffs[state->handoff]));
  cv_set(o, "animation", animation);
  if (!read_order) cv_set(o, "handoff", cv_str(handoffs[state->handoff]));
  return o;
}

/* QvmGrappleSource.capture(): { version, grapple: core.capture(), bindings, tethers }. */
qa_save_value *qa_app_sim_qvm_grapple_source_checkpoint_value(const qa_app_sim_qvm_grapple_source_checkpoint *checkpoint) {
  qa_save_value *o = cv_obj();
  cv_set(o, "version", cv_num(checkpoint->version));
  const qa_qvm_grapple_checkpoint *grapple = &checkpoint->grapple;
  qa_save_value *core = cv_obj();
  cv_set(core, "version", cv_num(grapple->version));
  cv_set(core, "profile", cv_str(grapple->profile));
  cv_set(core, "module", qa_save_guest_checkpoint_value(&grapple->module));
  qa_save_value *owners = cv_arr();
  for (size_t i = 0; i < grapple->owner_count; i++) cv_push(owners, cv_saved(grapple->owners[i]));
  cv_set(core, "owners", owners);
  cv_set(o, "grapple", core);
  /* { ...binding, actor } keeps the binding literal { actor, pointer, client, origin }. */
  qa_save_value *bindings = cv_arr();
  for (size_t i = 0; i < checkpoint->binding_count; i++) {
    const qa_app_sim_qvm_grapple_binding *binding = &checkpoint->bindings[i];
    qa_save_value *entry = cv_obj();
    cv_set(entry, "actor", cv_saved(binding->actor));
    cv_set(entry, "pointer", cv_num(binding->pointer));
    cv_set(entry, "client", cv_bool(binding->client));
    cv_set(entry, "origin", cv_vector(binding->origin));
    cv_push(bindings, entry);
  }
  cv_set(o, "bindings", bindings);
  qa_save_value *tethers = cv_arr();
  for (size_t i = 0; i < checkpoint->tether_count; i++) {
    qa_save_value *entry = cv_obj();
    cv_set(entry, "owner", cv_saved(checkpoint->tethers[i].owner));
    cv_set(entry, "actor", cv_saved(checkpoint->tethers[i].actor));
    cv_push(tethers, entry);
  }
  cv_set(o, "tethers", tethers);
  return o;
}

static qa_save_value *cv_q1_content_value(const qa_q1_foundation_checkpoint *content) {
  qa_persist_q1_foundation_checkpoint view;
  qa_persist_q1_foundation_from_content(content, &view);
  qa_save_value *value = qa_q1_foundation_checkpoint_value(&view);
  qa_q1_foundation_checkpoint_free(&view);
  cv_oom(value);
  return value;
}

/* GrappleRuntime.capture(): { ...common, source } with common
 * { version, weaponAnimations, controls, random }. Animations are
 * { actor, ...GrappleWeaponAnimation } and controls { actor, ...GrappleControl }
 * in the admit() literal order. */
qa_save_value *qa_app_sim_grapple_runtime_checkpoint_value(const qa_app_sim_grapple_runtime_checkpoint *checkpoint) {
  qa_save_value *o = cv_obj();
  cv_set(o, "version", cv_num(checkpoint->version));
  qa_save_value *animations = cv_arr();
  for (size_t i = 0; i < checkpoint->weapon_animation_count; i++) {
    const qa_app_sim_grapple_saved_animation *saved = &checkpoint->weapon_animations[i];
    qa_save_value *entry = cv_obj();
    cv_set(entry, "actor", cv_saved(saved->actor));
    cv_set(entry, "state", cv_grapple_weapon_state(&saved->animation.state, saved->animation.state_read_order));
    cv_set(entry, "nextFrameAt", cv_num(saved->animation.next_frame_at));
    cv_set(entry, "kickOrigin", cv_vector(saved->animation.kick_origin));
    cv_set(entry, "kickPitch", cv_num(saved->animation.kick_pitch));
    cv_push(animations, entry);
  }
  cv_set(o, "weaponAnimations", animations);
  qa_save_value *controls = cv_arr();
  for (size_t i = 0; i < checkpoint->control_count; i++) {
    const qa_app_sim_grapple_saved_control *saved = &checkpoint->controls[i];
    const qa_app_sim_grapple_control *c = &saved->control;
    qa_save_value *entry = cv_obj();
    cv_set(entry, "actor", cv_saved(saved->actor));
    if (c->read_order) {
      cv_set(entry, "held", cv_bool(c->held));
      cv_set(entry, "jump", cv_bool(c->jump));
      cv_set(entry, "teleportBit", c->has_teleport_bit ? cv_num(c->teleport_bit) : cv_null());
    } else {
      cv_set(entry, "teleportBit", c->has_teleport_bit ? cv_num(c->teleport_bit) : cv_null());
      cv_set(entry, "jump", cv_bool(c->jump));
      cv_set(entry, "held", cv_bool(c->held));
    }
    cv_set(entry, "pressed", cv_bool(c->pressed));
    cv_set(entry, "released", cv_bool(c->released));
    cv_set(entry, "previousVelocity", cv_vector(c->previous_velocity));
    cv_set(entry, "predictionSuppressed", cv_bool(c->prediction_suppressed));
    cv_push(controls, entry);
  }
  cv_set(o, "controls", controls);
  cv_set(o, "random", cv_random(checkpoint->random));
  const qa_app_sim_grapple_source_checkpoint *saved = &checkpoint->source;
  qa_save_value *source = cv_obj();
  cv_set(source, "kind", cv_str(saved->kind));
  if (strcmp(saved->kind, "q1-threewave") == 0) {
    if (saved->has_q1_saved) {
      qa_save_value *value = qa_q1_foundation_checkpoint_value(&saved->q1_saved);
      cv_oom(value);
      cv_set(source, "entities", value);
    } else cv_set(source, "entities", cv_q1_content_value(&saved->q1_entities));
  } else if (strcmp(saved->kind, "q2-ctf") == 0 || strcmp(saved->kind, "q2-lmctf") == 0) {
    size_t length = 0;
    cv_set(source, "entities", cv_tree(qa_encode_q2_foundation_checkpoint(&saved->q2_entities, &length), &length));
    qa_save_value *states = cv_arr();
    if (strcmp(saved->kind, "q2-ctf") == 0) {
      for (size_t i = 0; i < saved->ctf_state_count; i++) {
        const qa_ctf_grapple_checkpoint *s = &saved->ctf_states[i].state;
        qa_save_value *entry = cv_obj(), *state = cv_obj();
        cv_set(entry, "actor", cv_saved(saved->ctf_states[i].actor));
        /* captureCtfGrapple literal. */
        cv_set(state, "grapple", cv_nullable_saved(s->has_grapple, s->grapple));
        cv_set(state, "grappleState", cv_str(s->grapple_state));
        cv_set(state, "grappleReleaseTime", cv_num(s->grapple_release_time));
        cv_set(state, "grappleNoKnockback", s->has_no_knockback ? cv_bool(s->grapple_no_knockback) : cv_null());
        cv_set(entry, "state", state);
        cv_push(states, entry);
      }
    } else {
      for (size_t i = 0; i < saved->lmctf_state_count; i++) {
        const qa_lmctf_grapple_checkpoint *s = &saved->lmctf_states[i].state;
        qa_save_value *entry = cv_obj(), *state = cv_obj();
        cv_set(entry, "actor", cv_saved(saved->lmctf_states[i].actor));
        /* captureLmctfGrapple literal. */
        cv_set(state, "hook", cv_nullable_saved(s->has_hook, s->hook));
        cv_set(state, "hookState", cv_num(s->hook_state));
        cv_set(state, "hookLength", cv_num(s->hook_length));
        cv_set(state, "hookHeld", cv_bool(s->hook_held));
        cv_set(entry, "state", state);
        cv_push(states, entry);
      }
    }
    cv_set(source, "states", states);
  } else {
    cv_set(source, "component", qa_app_sim_qvm_grapple_source_checkpoint_value(&saved->component));
    qa_save_value *holstered = cv_arr();
    for (size_t i = 0; i < saved->holstered_count; i++) cv_push(holstered, cv_saved(saved->holstered[i]));
    cv_set(source, "holstered", holstered);
  }
  cv_set(o, "source", source);
  return o;
}

static qa_save_value *cv_weapon_reference(qa_weapon_reference reference) {
  qa_save_value *o = cv_obj();
  cv_set(o, "provider", cv_str(reference.provider));
  cv_set(o, "item", cv_str(reference.item));
  return o;
}
/* WeaponSlotState literals of weapon-slot.ts. */
qa_save_value *qa_app_sim_weapon_slot_state_value(qa_app_sim_weapon_slot_state state) {
  qa_save_value *o = cv_obj();
  cv_set(o, "kind", cv_str(state.kind));
  if (strcmp(state.kind, "active") == 0) cv_set(o, "provider", cv_str(state.provider));
  else if (strcmp(state.kind, "switching") == 0 || strcmp(state.kind, "activating") == 0) {
    cv_set(o, "from", cv_str(state.from));
    cv_set(o, "next", cv_weapon_reference(state.next));
    if (strcmp(state.kind, "activating") == 0) cv_set(o, "request", cv_num(state.request));
  } else if (strcmp(state.kind, "holstering-primary") == 0 || strcmp(state.kind, "holstering-equipment") == 0)
    cv_set(o, "next", cv_weapon_reference(state.next));
  return o;
}

/* captureForeignWorld() literal. */
qa_save_value *qa_app_sim_foreign_q2_world_checkpoint_value(const qa_app_sim_foreign_q2_world_checkpoint *checkpoint) {
  qa_save_value *o = cv_obj();
  size_t length = 0;
  cv_set(o, "version", cv_num(checkpoint->version));
  cv_set(o, "reference", qa_save_provider_value(checkpoint->reference));
  cv_set(o, "edition", cv_str(checkpoint->edition));
  cv_set(o, "program", cv_str(checkpoint->program));
  cv_set(o, "frame", qa_save_frame_value(checkpoint->frame));
  cv_set(o, "random", qa_save_random_value(checkpoint->random));
  cv_set(o, "entities", cv_tree(qa_encode_q2_foundation_checkpoint(&checkpoint->entities, &length), &length));
  cv_set(o, "monsters", cv_tree(qa_encode_q2_monsters_checkpoint(&checkpoint->monsters, &length), &length));
  qa_save_value *ballistics = cv_obj(), *causes = cv_arr();
  for (size_t i = 0; i < checkpoint->ballistics.blaster_cause_count; i++) {
    qa_save_value *entry = cv_obj();
    cv_set(entry, "actor", cv_saved(checkpoint->ballistics.blaster_causes[i].actor));
    cv_set(entry, "meansOfDeath", cv_num(checkpoint->ballistics.blaster_causes[i].means_of_death));
    cv_push(causes, entry);
  }
  cv_set(ballistics, "blasterCauses", causes);
  cv_set(o, "ballistics", ballistics);
  cv_set(o, "movers", cv_tree(qa_encode_q2_movers_checkpoint(&checkpoint->movers, &length), &length));
  cv_set(o, "items", cv_tree(qa_encode_q2_items_checkpoint(&checkpoint->items, &length), &length));
  cv_set(o, "baseEntities", cv_tree(qa_encode_q2_base_entities_checkpoint(&checkpoint->base_entities, &length), &length));
  qa_save_value *packs = cv_arr();
  for (size_t i = 0; i < checkpoint->pack_count; i++) {
    qa_save_value *entry = cv_obj();
    cv_set(entry, "pack", cv_str(checkpoint->packs[i].pack));
    cv_set(entry, "monsters", cv_tree(qa_encode_q2_mission_pack_monsters_checkpoint(&checkpoint->packs[i].monsters, &length), &length));
    cv_set(entry, "steamId", cv_num(checkpoint->packs[i].steam_id));
    cv_push(packs, entry);
  }
  cv_set(o, "packs", packs);
  return o;
}

/* The Q3 weapon state literal { kind, sourceWeapon, state, timeMilliseconds }
 * (weaponSnapshot / q3SpawnLoadout); Q1/Q2 states in their content literals. */
static qa_save_value *cv_weapon_state(const qa_weapon_state *state) {
  qa_save_value *o = cv_obj();
  cv_set(o, "kind", cv_str(state->kind));
  if (strcmp(state->kind, "q3") == 0) {
    cv_set(o, "sourceWeapon", cv_num(state->source_weapon));
    cv_set(o, "state", cv_num(state->state));
    cv_set(o, "timeMilliseconds", cv_num(state->time_milliseconds));
  } else if (strcmp(state->kind, "q1") == 0) {
    if (state->source_weapon_first) cv_set(o, "sourceWeapon", cv_num(state->source_weapon));
    cv_set(o, "frame", cv_num(state->frame));
    cv_set(o, "attackFinishedSeconds", cv_num(state->attack_finished_seconds));
    if (!state->source_weapon_first) cv_set(o, "sourceWeapon", cv_num(state->source_weapon));
  } else {
    cv_set(o, "gunFrame", cv_num(state->gun_frame));
    cv_set(o, "state", cv_num(state->state));
    cv_set(o, "pendingWeapon", cv_nullable_str(state->pending_weapon));
    cv_set(o, "machinegunShots", cv_num(state->machinegun_shots));
    cv_set(o, "grenadeTime", qa_save_time_value(state->grenade_time));
    cv_set(o, "grenadeBlewUp", cv_bool(state->grenade_blew_up));
  }
  return o;
}
/* Q3: the q3SpawnLoadout literal { provider, activeWeapon, state, ammo },
 * which Q3SelectedArsenal.read and stepQ3Arsenal spread. Q1/Q2: the
 * arsenal literals of arsenal/q1.ts, arsenal/q2.ts and admitPlayer,
 * { provider, activeWeapon, ammo, state }. */
static qa_save_value *cv_arsenal(const qa_arsenal_state *arsenal, bool state_first) {
  qa_save_value *o = cv_obj();
  cv_set(o, "provider", cv_str(arsenal->provider));
  cv_set(o, "activeWeapon", cv_nullable_str(arsenal->active_weapon));
  if (state_first) cv_set(o, "state", cv_weapon_state(&arsenal->state));
  qa_save_value *ammo = cv_arr();
  for (size_t i = 0; i < arsenal->ammo_count; i++) cv_push(ammo, qa_save_inventory_entry_value(arsenal->ammo[i]));
  cv_set(o, "ammo", ammo);
  if (!state_first) cv_set(o, "state", cv_weapon_state(&arsenal->state));
  return o;
}
qa_save_value *qa_app_sim_arsenal_state_value(const qa_arsenal_state *arsenal) {
  return cv_arsenal(arsenal, arsenal->state.kind != NULL && strcmp(arsenal->state.kind, "q3") == 0);
}

/* Q3SelectedArsenal.capture(actor) literal. */
qa_save_value *qa_app_q3_selected_arsenal_checkpoint_value(const qa_app_q3_selected_arsenal_checkpoint *checkpoint) {
  qa_save_value *o = cv_obj();
  cv_set(o, "supplyProfile", cv_nullable_str(checkpoint->supply_profile));
  cv_set(o, "arsenal", cv_arsenal(&checkpoint->arsenal, !checkpoint->arsenal_read_order));
  cv_set(o, "runtime", qa_app_q3_arsenal_runtime_state_value(&checkpoint->runtime));
  cv_set(o, "torsoAnimation", cv_num(checkpoint->torso_animation));
  cv_set(o, "lastFireMilliseconds", checkpoint->has_last_fire_milliseconds ? cv_num(checkpoint->last_fire_milliseconds) : cv_null());
  cv_set(o, "pendingUse", cv_nullable_str(checkpoint->pending_use));
  return o;
}

/* Q3CharacterActor.capture() literal; animation is the q3SpawnAnimation shape. */
qa_save_value *qa_q3_character_checkpoint_value(const qa_q3_character_checkpoint *checkpoint) {
  qa_save_value *o = cv_obj(), *animation = cv_obj();
  cv_set(o, "version", cv_num(checkpoint->version));
  cv_set(o, "product", cv_str(checkpoint->product));
  cv_set(animation, "kind", cv_str("q3"));
  cv_set(animation, "legs", cv_num(checkpoint->animation.legs));
  cv_set(animation, "torso", cv_num(checkpoint->animation.torso));
  cv_set(animation, "legsTimerMilliseconds", cv_num(checkpoint->animation.legs_timer_milliseconds));
  cv_set(animation, "torsoTimerMilliseconds", cv_num(checkpoint->animation.torso_timer_milliseconds));
  cv_set(o, "animation", animation);
  cv_set(o, "flags", cv_num(checkpoint->flags));
  cv_set(o, "eventSequence", cv_num(checkpoint->event_sequence));
  cv_set(o, "respawnTime", cv_num(checkpoint->respawn_time));
  cv_set(o, "spawnCount", cv_num(checkpoint->spawn_count));
  cv_set(o, "dead", cv_bool(checkpoint->dead));
  cv_set(o, "gibbed", cv_bool(checkpoint->gibbed));
  cv_set(o, "initialized", cv_bool(checkpoint->initialized));
  return o;
}

/* newQ1Travel / captureQ1Travel literal { health, maxHealth, armor, inventory, weapon, extensions }. */
qa_save_value *qa_q1_travel_state_value(const qa_q1_travel_state *state) {
  qa_save_value *o = cv_obj();
  cv_set(o, "health", cv_num(state->health));
  cv_set(o, "maxHealth", cv_num(state->max_health));
  cv_set(o, "armor", qa_save_armor_value(state->armor));
  qa_save_value *inventory = cv_arr();
  for (size_t i = 0; i < state->inventory_count; i++) cv_push(inventory, qa_save_inventory_entry_value(state->inventory[i]));
  cv_set(o, "inventory", inventory);
  cv_set(o, "weapon", cv_str(state->weapon));
  qa_save_value *extensions = cv_arr();
  for (size_t i = 0; i < state->extension_count; i++) {
    qa_save_value *entry = cv_obj();
    cv_set(entry, "id", cv_str(state->extensions[i].id));
    cv_set(entry, "bytes", qa_save_value_bytes(state->extensions[i].bytes, state->extensions[i].length));
    cv_push(extensions, entry);
  }
  cv_set(o, "extensions", extensions);
  return o;
}

/* The "model" event literal of Q2EntityServices / Q2 player characters:
 * { kind, actor, path, attachedModels, frame, oldFrame, scale, alpha,
 * skin, effects, renderFlags }; the capture spread replaces actor in place. */
qa_save_value *qa_q2_presentation_model_value(const qa_q2_presentation_event *model) {
  return qa_q2_presentation_model_value_ordered(model, false);
}
/* read_order: the record the sourceModels reader rebuilt, { kind, actor, path, attachedModels, frame, oldFrame, scale,
 * skin, effects, renderFlags, alpha }. */
qa_save_value *qa_q2_presentation_model_value_ordered(const qa_q2_presentation_event *model, bool read_order) {
  qa_save_value *o = cv_obj(), *attached = cv_arr();
  cv_set(o, "kind", cv_str("model"));
  cv_set(o, "actor", cv_saved(qa_saved_actor_id_of(model->actor)));
  cv_set(o, "path", cv_str(model->path));
  for (size_t i = 0; i < 3; i++) cv_push(attached, cv_str(model->attached_models[i] == NULL ? "" : model->attached_models[i]));
  cv_set(o, "attachedModels", attached);
  cv_set(o, "frame", cv_num(model->frame));
  cv_set(o, "oldFrame", cv_num(model->old_frame));
  cv_set(o, "scale", cv_num(model->scale));
  if (!read_order) cv_set(o, "alpha", cv_num(model->alpha));
  cv_set(o, "skin", cv_num(model->skin));
  cv_set(o, "effects", cv_num(model->effects));
  cv_set(o, "renderFlags", cv_num(model->render_flags));
  if (read_order) cv_set(o, "alpha", cv_num(model->alpha));
  return o;
}

/* q2BuildView literal; Q2Players.buildView appends helpIcon: true. */
qa_save_value *qa_q2_player_view_value(const qa_q2_base_player_view *view) {
  qa_save_value *o = cv_obj();
  cv_set(o, "angles", cv_vector(view->angles));
  cv_set(o, "offset", cv_vector(view->offset));
  cv_set(o, "kickAngles", cv_vector(view->kick_angles));
  cv_set(o, "gunAngles", cv_vector(view->gun_angles));
  cv_set(o, "gunOffset", cv_vector(view->gun_offset));
  qa_save_value *blend = cv_obj();
  cv_set(blend, "x", cv_num(view->blend.x));
  cv_set(blend, "y", cv_num(view->blend.y));
  cv_set(blend, "z", cv_num(view->blend.z));
  cv_set(blend, "w", cv_num(view->blend.w));
  cv_set(o, "blend", blend);
  cv_set(o, "fov", cv_num(view->fov));
  cv_set(o, "underwater", cv_bool(view->underwater));
  cv_set(o, "flashes", cv_num(view->flashes));
  cv_set(o, "health", cv_num(view->health));
  cv_set(o, "armor", cv_num(view->armor));
  cv_set(o, "ammo", cv_num(view->ammo));
  cv_set(o, "score", cv_num(view->score));
  cv_set(o, "selectedItem", cv_nullable_str(view->selected_item));
  if (view->has_timer) {
    qa_save_value *timer = cv_obj();
    cv_set(timer, "item", cv_str(view->timer_item));
    cv_set(timer, "seconds", cv_num(view->timer_seconds));
    cv_set(o, "timer", timer);
  } else cv_set(o, "timer", cv_null());
  cv_set(o, "spectator", cv_bool(view->spectator));
  cv_set(o, "layouts", cv_num(view->layouts));
  if (view->help_icon) cv_set(o, "helpIcon", cv_bool(true));
  return o;
}
