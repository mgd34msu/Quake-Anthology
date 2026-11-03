/* Port of src/persistence/q2-weapons.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_weapons.h"

#include <stdlib.h>
#include <string.h>

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
/* TS readSavedActor (save-image.ts, not yet ported as a module):
 * {slot: integer(0), generation: integer(0)}. Local copy only. */
static qa_saved_actor_id read_actor(const qa_save_reader *reader) {
  qa_saved_actor_id actor;
  qa_save_reader slot = field(reader, "slot");
  actor.slot = qa_save_reader_integer_min(&slot, 0);
  qa_save_reader_destroy(&slot);
  qa_save_reader generation = field(reader, "generation");
  actor.generation = qa_save_reader_integer_min(&generation, 0);
  qa_save_reader_destroy(&generation);
  return actor;
}
static bool set_own(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_set(object, key, child)) { qa_save_value_free(child); return false; }
  return true;
}
static bool push_own(qa_save_value *array, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_push(array, child)) { qa_save_value_free(child); return false; }
  return true;
}
__attribute__((unused)) static qa_save_value *str_value(const char *text) {
  return qa_save_value_string(text != NULL ? text : "");
}
__attribute__((unused)) static qa_save_value *vec_value(const qa_vec3 *vector) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "x", qa_save_value_number(vector->x)) ||
      !set_own(obj, "y", qa_save_value_number(vector->y)) ||
      !set_own(obj, "z", qa_save_value_number(vector->z))) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
static qa_save_value *actor_value(const qa_saved_actor_id *actor) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "slot", qa_save_value_number(actor->slot)) ||
      !set_own(obj, "generation", qa_save_value_number(actor->generation))) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
__attribute__((unused)) static qa_save_value *time_value(const qa_source_time *time) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "kind", qa_save_value_string(
          time->kind == QA_SOURCE_TIME_SECONDS ? "seconds" : "milliseconds")) ||
      !set_own(obj, "value", qa_save_value_number(time->value))) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
/* Moves every entry of `child` into `object` (TS object spread). */
__attribute__((unused)) static bool merge_into(qa_save_value *object, qa_save_value *child) {
  for (size_t i = 0; i < child->entry_count; i++) {
    qa_save_value *value = child->entries[i].value;
    child->entries[i].value = NULL;
    if (!qa_save_value_set(object, child->entries[i].key, value)) {
      child->entries[i].value = value;
      return false;
    }
  }
  return true;
}


static double number_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_number(&f);
  qa_save_reader_destroy(&f);
  return value;
}

static const char *hand_name(qa_q2w_hand hand) {
  return hand == QA_Q2W_HAND_LEFT ? "left" : hand == QA_Q2W_HAND_CENTER ? "center" : "right";
}
static const char *handoff_name(qa_q2w_handoff handoff) {
  return handoff == QA_Q2W_HANDOFF_HOLSTERING ? "holstering" : handoff == QA_Q2W_HANDOFF_HOLSTERED ? "holstered" : "active";
}
static const char *reservation_name(qa_q2w_hand_reservation reservation) {
  return reservation == QA_Q2W_RES_FINITE ? "finite" : reservation == QA_Q2W_RES_INFINITE ? "infinite" : "none";
}

static void read_q2_weapon_input(const qa_save_reader *reader, qa_q2w_input *out);
static qa_save_value *build_q2_weapon_input(const qa_q2w_input *in);
static void read_q2_weapon_state(const qa_save_reader *reader, qa_q2w_state *out);
static qa_save_value *build_q2_weapon_state(const qa_q2w_state *in);
static void read_q2_silencer_charge(const qa_save_reader *reader, qa_q2w_charge_entry *out);
static qa_save_value *build_q2_silencer_charge(const qa_q2w_charge_entry *in);
static void read_q2_weapon_state_entry(const qa_save_reader *reader, qa_q2w_state_entry *out);
static qa_save_value *build_q2_weapon_state_entry(const qa_q2w_state_entry *in);
static void read_q2_weapon_input_entry(const qa_save_reader *reader, qa_q2w_input_entry *out);
static qa_save_value *build_q2_weapon_input_entry(const qa_q2w_input_entry *in);
static void read_q2_blaster_cause(const qa_save_reader *reader, qa_q2w_cause_entry *out);
static qa_save_value *build_q2_blaster_cause(const qa_q2w_cause_entry *in);
static void read_q2_weapons_checkpoint(const qa_save_reader *reader, qa_q2w_weapons_checkpoint *out, qa_persist_decoded *owner);
static qa_save_value *build_q2_weapons_checkpoint(const qa_q2w_weapons_checkpoint *in);

static void read_q2_weapon_input(const qa_save_reader *reader, qa_q2w_input *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "attack");
    (*out).attack = qa_save_reader_boolean(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "latchedAttack");
    (*out).latched_attack = qa_save_reader_boolean(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "holster");
    (*out).holster = qa_save_reader_boolean(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "angles");
    (*out).angles = qa_read_vector(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "ducked");
    (*out).ducked = qa_save_reader_boolean(&f4);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "spectator");
    (*out).spectator = qa_save_reader_boolean(&f5);
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "notarget");
    (*out).notarget = qa_save_reader_boolean(&f6);
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "hand");
    static const char *const ch1[] = {"right", "left", "center"};
    { const char *hand = qa_save_reader_choice_string(&f7, ch1, 3);
      (*out).hand = strcmp(hand, "left") == 0 ? QA_Q2W_HAND_LEFT : strcmp(hand, "center") == 0 ? QA_Q2W_HAND_CENTER : QA_Q2W_HAND_RIGHT; }
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "animatePlayer");
    (*out).animate_player = qa_save_reader_boolean(&f8);
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "quadUntil");
    (*out).quad_until = qa_save_reader_number(&f9);
    qa_save_reader_destroy(&f9); }
  { qa_save_reader f10 = field(reader, "doubleUntil");
    (*out).double_until = qa_save_reader_number(&f10);
    qa_save_reader_destroy(&f10); }
  { qa_save_reader f11 = field(reader, "quadFireUntil");
    (*out).quad_fire_until = qa_save_reader_number(&f11);
    qa_save_reader_destroy(&f11); }
  { qa_save_reader f12 = field(reader, "haste");
    (*out).haste = qa_save_reader_boolean(&f12);
    qa_save_reader_destroy(&f12); }
  { qa_save_reader f13 = field(reader, "noStackDouble");
    (*out).no_stack_double = qa_save_reader_boolean(&f13);
    qa_save_reader_destroy(&f13); }
  { qa_save_reader f14 = field(reader, "instantSwitch");
    (*out).instant_switch = qa_save_reader_boolean(&f14);
    qa_save_reader_destroy(&f14); }
  { qa_save_reader f15 = field(reader, "quickSwitch");
    (*out).quick_switch = qa_save_reader_boolean(&f15);
    qa_save_reader_destroy(&f15); }
  { qa_save_reader f16 = field(reader, "infiniteAmmo");
    (*out).infinite_ammo = qa_save_reader_boolean(&f16);
    qa_save_reader_destroy(&f16); }
  { qa_save_reader f17 = field(reader, "playersCollide");
    (*out).players_collide = qa_save_reader_boolean(&f17);
    qa_save_reader_destroy(&f17); }
  { qa_save_reader f18 = field(reader, "gravity");
    (*out).gravity = qa_save_reader_number(&f18);
    qa_save_reader_destroy(&f18); }
  { qa_save_reader f19 = field(reader, "weaponThunk");
    (*out).weapon_thunk = qa_save_reader_boolean(&f19);
    qa_save_reader_destroy(&f19); }
}
static qa_save_value *build_q2_weapon_input(const qa_q2w_input *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "attack", qa_save_value_boolean((*in).attack))) goto fail;
  if (!set_own(obj, "latchedAttack", qa_save_value_boolean((*in).latched_attack))) goto fail;
  if (!set_own(obj, "holster", qa_save_value_boolean((*in).holster))) goto fail;
  if (!set_own(obj, "angles", vec_value(&(*in).angles))) goto fail;
  if (!set_own(obj, "ducked", qa_save_value_boolean((*in).ducked))) goto fail;
  if (!set_own(obj, "spectator", qa_save_value_boolean((*in).spectator))) goto fail;
  if (!set_own(obj, "notarget", qa_save_value_boolean((*in).notarget))) goto fail;
  if (!set_own(obj, "hand", qa_save_value_string(hand_name((*in).hand)))) goto fail;
  if (!set_own(obj, "animatePlayer", qa_save_value_boolean((*in).animate_player))) goto fail;
  if (!set_own(obj, "quadUntil", qa_save_value_number((*in).quad_until))) goto fail;
  if (!set_own(obj, "doubleUntil", qa_save_value_number((*in).double_until))) goto fail;
  if (!set_own(obj, "quadFireUntil", qa_save_value_number((*in).quad_fire_until))) goto fail;
  if (!set_own(obj, "haste", qa_save_value_boolean((*in).haste))) goto fail;
  if (!set_own(obj, "noStackDouble", qa_save_value_boolean((*in).no_stack_double))) goto fail;
  if (!set_own(obj, "instantSwitch", qa_save_value_boolean((*in).instant_switch))) goto fail;
  if (!set_own(obj, "quickSwitch", qa_save_value_boolean((*in).quick_switch))) goto fail;
  if (!set_own(obj, "infiniteAmmo", qa_save_value_boolean((*in).infinite_ammo))) goto fail;
  if (!set_own(obj, "playersCollide", qa_save_value_boolean((*in).players_collide))) goto fail;
  if (!set_own(obj, "gravity", qa_save_value_number((*in).gravity))) goto fail;
  if (!set_own(obj, "weaponThunk", qa_save_value_boolean((*in).weapon_thunk))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_weapon_state(const qa_save_reader *reader, qa_q2w_state *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "primaryHandoff");
    static const char *const ch2[] = {"active", "holstering", "holstered"};
    { const char *handoff = qa_save_reader_is_undefined(&f0) ? "active" : qa_save_reader_choice_string(&f0, ch2, 3);
      (*out).primary_handoff = strcmp(handoff, "holstering") == 0 ? QA_Q2W_HANDOFF_HOLSTERING : strcmp(handoff, "holstered") == 0 ? QA_Q2W_HANDOFF_HOLSTERED : QA_Q2W_HANDOFF_ACTIVE; }
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "handReservation");
    { qa_save_reader w = field(reader, "weapon");
      const char *weapon = qa_save_reader_is_null(&w) ? NULL : qa_save_reader_string(&w);
      qa_save_reader_destroy(&w);
      qa_save_reader ph = field(reader, "phase");
      static const char *const phases[] = {"activating", "ready", "firing", "dropping"};
      const char *phase = qa_save_reader_choice_string(&ph, phases, 4);
      qa_save_reader_destroy(&ph);
      bool grenade_firing = weapon != NULL && strcmp(weapon, "grenades") == 0 && strcmp(phase, "firing") == 0;
      if (qa_save_reader_is_undefined(&f1)) {
        if (grenade_firing) qa_save_reader_fail(&f1, "legacy active hand grenade has no ammunition reservation");
        (*out).hand_reservation = QA_Q2W_RES_NONE;
      } else {
        qa_save_reader k = field(&f1, "kind");
        static const char *const kinds[] = {"none", "finite", "infinite"};
        const char *kind = qa_save_reader_choice_string(&k, kinds, 3);
        qa_save_reader_destroy(&k);
        if (strcmp(kind, "none") != 0 && !grenade_firing)
          qa_save_reader_fail(&f1, "hand reservation requires a firing hand grenade weapon");
        (*out).hand_reservation = strcmp(kind, "finite") == 0 ? QA_Q2W_RES_FINITE : strcmp(kind, "infinite") == 0 ? QA_Q2W_RES_INFINITE : QA_Q2W_RES_NONE;
      } }
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "sourceFiring");
    (*out).source_firing = qa_save_reader_boolean(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "weapon");
    if (qa_save_reader_is_null(&f3)) (*out).weapon = NULL; else {
      (*out).weapon = qa_save_reader_string(&f3);
    }
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "lastWeapon");
    if (qa_save_reader_is_null(&f4)) (*out).last_weapon = NULL; else {
      (*out).last_weapon = qa_save_reader_string(&f4);
    }
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "pending");
    if (qa_save_reader_is_null(&f5)) (*out).pending = NULL; else {
      (*out).pending = qa_save_reader_string(&f5);
    }
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "phase");
    static const char *const ch6[] = {"activating", "ready", "firing", "dropping"};
    { const char *phase = qa_save_reader_choice_string(&f6, ch6, 4);
      (*out).phase = strcmp(phase, "ready") == 0 ? QA_Q2W_READY : strcmp(phase, "firing") == 0 ? QA_Q2W_FIRING : strcmp(phase, "dropping") == 0 ? QA_Q2W_DROPPING : QA_Q2W_ACTIVATING; }
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "frame");
    (*out).frame = qa_save_reader_number(&f7);
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "thinkTime");
    (*out).think_time = qa_save_reader_number(&f8);
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "fireFinished");
    (*out).fire_finished = qa_save_reader_number(&f9);
    qa_save_reader_destroy(&f9); }
  { qa_save_reader f10 = field(reader, "fireBuffered");
    (*out).fire_buffered = qa_save_reader_boolean(&f10);
    qa_save_reader_destroy(&f10); }
  { qa_save_reader f11 = field(reader, "latchedAttack");
    (*out).latched_attack = qa_save_reader_boolean(&f11);
    qa_save_reader_destroy(&f11); }
  { qa_save_reader f12 = field(reader, "machinegunShots");
    (*out).machinegun_shots = qa_save_reader_number(&f12);
    qa_save_reader_destroy(&f12); }
  { qa_save_reader f13 = field(reader, "emptySoundTime");
    (*out).empty_sound_time = qa_save_reader_number(&f13);
    qa_save_reader_destroy(&f13); }
  { qa_save_reader f14 = field(reader, "grenadeTime");
    (*out).grenade_time = qa_save_reader_number(&f14);
    qa_save_reader_destroy(&f14); }
  { qa_save_reader f15 = field(reader, "grenadeFinished");
    (*out).grenade_finished = qa_save_reader_number(&f15);
    qa_save_reader_destroy(&f15); }
  { qa_save_reader f16 = field(reader, "grenadeBlewUp");
    (*out).grenade_blew_up = qa_save_reader_boolean(&f16);
    qa_save_reader_destroy(&f16); }
  { qa_save_reader f17 = field(reader, "kickOrigin");
    (*out).kick_origin = qa_read_vector(&f17);
    qa_save_reader_destroy(&f17); }
  { qa_save_reader f18 = field(reader, "kickAngles");
    (*out).kick_angles = qa_read_vector(&f18);
    qa_save_reader_destroy(&f18); }
  { qa_save_reader f19 = field(reader, "kickTime");
    (*out).kick_time = qa_save_reader_is_undefined(&f19) ? number_field(reader, "kickUntil") - number_field(reader, "kickDuration") : qa_save_reader_number(&f19);
    qa_save_reader_destroy(&f19); }
  { qa_save_reader f20 = field(reader, "kickUntil");
    (*out).kick_until = qa_save_reader_number(&f20);
    qa_save_reader_destroy(&f20); }
  { qa_save_reader f21 = field(reader, "kickDuration");
    (*out).kick_duration = qa_save_reader_number(&f21);
    qa_save_reader_destroy(&f21); }
  { qa_save_reader f22 = field(reader, "loopSound");
    (*out).loop_sound = qa_save_reader_string(&f22);
    qa_save_reader_destroy(&f22); }
  { qa_save_reader f23 = field(reader, "viewModel");
    if (qa_save_reader_is_null(&f23)) (*out).view_model = NULL; else {
      (*out).view_model = qa_save_reader_string(&f23);
    }
    qa_save_reader_destroy(&f23); }
  { qa_save_reader f24 = field(reader, "viewSkin");
    (*out).view_skin = qa_save_reader_number(&f24);
    qa_save_reader_destroy(&f24); }
  { qa_save_reader f25 = field(reader, "lastFiringTime");
    (*out).last_firing_time = qa_save_reader_number(&f25);
    qa_save_reader_destroy(&f25); }
  { qa_save_reader f26 = field(reader, "gunRate");
    (*out).gun_rate = qa_save_reader_number(&f26);
    qa_save_reader_destroy(&f26); }
}
static qa_save_value *build_q2_weapon_state(const qa_q2w_state *in) {
  /* Keys in TS Q2WeaponState class field order (capture spreads the state). */
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "primaryHandoff", qa_save_value_string(handoff_name((*in).primary_handoff)))) goto fail;
  if (!set_own(obj, "weapon", (*in).weapon != NULL ? str_value((*in).weapon) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "lastWeapon", (*in).last_weapon != NULL ? str_value((*in).last_weapon) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "pending", (*in).pending != NULL ? str_value((*in).pending) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "phase", qa_save_value_string(qa_q2w_phase_name((*in).phase)))) goto fail;
  if (!set_own(obj, "frame", qa_save_value_number((*in).frame))) goto fail;
  if (!set_own(obj, "thinkTime", qa_save_value_number((*in).think_time))) goto fail;
  if (!set_own(obj, "fireFinished", qa_save_value_number((*in).fire_finished))) goto fail;
  if (!set_own(obj, "fireBuffered", qa_save_value_boolean((*in).fire_buffered))) goto fail;
  if (!set_own(obj, "latchedAttack", qa_save_value_boolean((*in).latched_attack))) goto fail;
  if (!set_own(obj, "machinegunShots", qa_save_value_number((*in).machinegun_shots))) goto fail;
  if (!set_own(obj, "emptySoundTime", qa_save_value_number((*in).empty_sound_time))) goto fail;
  { qa_save_value *res = qa_save_value_object();
    if (res == NULL || !set_own(res, "kind", qa_save_value_string(reservation_name((*in).hand_reservation))) || !set_own(obj, "handReservation", res)) { qa_save_value_free(res); goto fail; } }
  if (!set_own(obj, "grenadeTime", qa_save_value_number((*in).grenade_time))) goto fail;
  if (!set_own(obj, "grenadeFinished", qa_save_value_number((*in).grenade_finished))) goto fail;
  if (!set_own(obj, "grenadeBlewUp", qa_save_value_boolean((*in).grenade_blew_up))) goto fail;
  if (!set_own(obj, "kickOrigin", vec_value(&(*in).kick_origin))) goto fail;
  if (!set_own(obj, "kickAngles", vec_value(&(*in).kick_angles))) goto fail;
  if (!set_own(obj, "kickTime", qa_save_value_number((*in).kick_time))) goto fail;
  if (!set_own(obj, "kickUntil", qa_save_value_number((*in).kick_until))) goto fail;
  if (!set_own(obj, "kickDuration", qa_save_value_number((*in).kick_duration))) goto fail;
  if (!set_own(obj, "loopSound", str_value((*in).loop_sound))) goto fail;
  if (!set_own(obj, "viewModel", (*in).view_model != NULL ? str_value((*in).view_model) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "viewSkin", qa_save_value_number((*in).view_skin))) goto fail;
  if (!set_own(obj, "lastFiringTime", qa_save_value_number((*in).last_firing_time))) goto fail;
  if (!set_own(obj, "sourceFiring", qa_save_value_boolean((*in).source_firing))) goto fail;
  if (!set_own(obj, "gunRate", qa_save_value_number((*in).gun_rate))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_silencer_charge(const qa_save_reader *reader, qa_q2w_charge_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "charges");
    (*out).charges = qa_save_reader_integer(&f1);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_silencer_charge(const qa_q2w_charge_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "charges", qa_save_value_number((*in).charges))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_weapon_state_entry(const qa_save_reader *reader, qa_q2w_state_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "state");
    read_q2_weapon_state(&f1, &(*out).state);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_weapon_state_entry(const qa_q2w_state_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "state", build_q2_weapon_state(&(*in).state))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_weapon_input_entry(const qa_save_reader *reader, qa_q2w_input_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "input");
    read_q2_weapon_input(&f1, &(*out).input);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_weapon_input_entry(const qa_q2w_input_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "input", build_q2_weapon_input(&(*in).input))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_blaster_cause(const qa_save_reader *reader, qa_q2w_cause_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "meansOfDeath");
    (*out).means_of_death = qa_save_reader_integer(&f1);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_blaster_cause(const qa_q2w_cause_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "meansOfDeath", qa_save_value_number((*in).means_of_death))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_weapons_checkpoint(const qa_save_reader *reader, qa_q2w_weapons_checkpoint *out, qa_persist_decoded *owner) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "formatVersion");
    { double version = qa_save_reader_integer(&f0); if (version != 2 && version != 3) qa_save_reader_fail(reader, "unsupported Q2 weapon checkpoint format"); (*out).format_version = 3; }
    /* Version 2 also carried per-actor noise records and the two global sound entities, which nothing read; they are ignored. */
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "silencerCharges");
    (*out).silencer_charge_count = qa_save_reader_list_count(&f1);
    qa_q2w_charge_entry *silencer_charges_v = qa_persist_decoded_array(owner, (*out).silencer_charge_count, sizeof *silencer_charges_v);
    (*out).silencer_charges = silencer_charges_v;
    for (size_t i8 = 0; i8 < (*out).silencer_charge_count; i8++) {
      qa_save_reader it9 = item(&f1, i8);
      read_q2_silencer_charge(&it9, &silencer_charges_v[i8]);
      qa_save_reader_destroy(&it9);
    }
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "sourceRules");
    static const char *const ch10[] = {"base", "ctf", "lmctf"};
    (*out).source_rules = qa_save_reader_choice_string(&f2, ch10, 3);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "registered");
    (*out).registered_count = qa_save_reader_list_count(&f3);
    const char * *registered_v = qa_persist_decoded_array(owner, (*out).registered_count, sizeof *registered_v);
    (*out).registered = registered_v;
    for (size_t i11 = 0; i11 < (*out).registered_count; i11++) {
      qa_save_reader it12 = item(&f3, i11);
      registered_v[i11] = qa_save_reader_string(&it12);
      qa_save_reader_destroy(&it12);
    }
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "fallbackOrder");
    if (!qa_save_reader_is_null(&f4)) { (*out).has_fallback_order = true;
      (*out).fallback_order_count = qa_save_reader_list_count(&f4);
      const char * *fallback_order_v = qa_persist_decoded_array(owner, (*out).fallback_order_count, sizeof *fallback_order_v);
      (*out).fallback_order = fallback_order_v;
      for (size_t i13 = 0; i13 < (*out).fallback_order_count; i13++) {
        qa_save_reader it14 = item(&f4, i13);
        fallback_order_v[i13] = qa_save_reader_string(&it14);
        qa_save_reader_destroy(&it14);
      }
    }
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "states");
    (*out).state_count = qa_save_reader_list_count(&f5);
    qa_q2w_state_entry *states_v = qa_persist_decoded_array(owner, (*out).state_count, sizeof *states_v);
    (*out).states = states_v;
    for (size_t i15 = 0; i15 < (*out).state_count; i15++) {
      qa_save_reader it16 = item(&f5, i15);
      read_q2_weapon_state_entry(&it16, &states_v[i15]);
      qa_save_reader_destroy(&it16);
    }
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "inputs");
    (*out).input_count = qa_save_reader_list_count(&f6);
    qa_q2w_input_entry *inputs_v = qa_persist_decoded_array(owner, (*out).input_count, sizeof *inputs_v);
    (*out).inputs = inputs_v;
    for (size_t i17 = 0; i17 < (*out).input_count; i17++) {
      qa_save_reader it18 = item(&f6, i17);
      read_q2_weapon_input_entry(&it18, &inputs_v[i17]);
      qa_save_reader_destroy(&it18);
    }
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f10 = field(reader, "blasterCauses");
    (*out).blaster_cause_count = qa_save_reader_list_count(&f10);
    qa_q2w_cause_entry *blaster_causes_v = qa_persist_decoded_array(owner, (*out).blaster_cause_count, sizeof *blaster_causes_v);
    (*out).blaster_causes = blaster_causes_v;
    for (size_t i21 = 0; i21 < (*out).blaster_cause_count; i21++) {
      qa_save_reader it22 = item(&f10, i21);
      read_q2_blaster_cause(&it22, &blaster_causes_v[i21]);
      qa_save_reader_destroy(&it22);
    }
    qa_save_reader_destroy(&f10); }
}
static qa_save_value *build_q2_weapons_checkpoint(const qa_q2w_weapons_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "formatVersion", qa_save_value_number((*in).format_version))) goto fail;
  { qa_save_value *arr23 = qa_save_value_array();
    if (arr23 == NULL) goto fail;
    for (size_t i24 = 0; i24 < (*in).silencer_charge_count; i24++) {
      if (!push_own(arr23, build_q2_silencer_charge(&(*in).silencer_charges[i24]))) { qa_save_value_free(arr23); goto fail; }
    }
    if (!set_own(obj, "silencerCharges", arr23)) goto fail; }
  if (!set_own(obj, "sourceRules", str_value((*in).source_rules))) goto fail;
  { qa_save_value *arr25 = qa_save_value_array();
    if (arr25 == NULL) goto fail;
    for (size_t i26 = 0; i26 < (*in).registered_count; i26++) {
      if (!push_own(arr25, str_value((*in).registered[i26]))) { qa_save_value_free(arr25); goto fail; }
    }
    if (!set_own(obj, "registered", arr25)) goto fail; }
  if (!(*in).has_fallback_order) { if (!set_own(obj, "fallbackOrder", qa_save_value_null())) goto fail; } else {
    { qa_save_value *arr27 = qa_save_value_array();
      if (arr27 == NULL) goto fail;
      for (size_t i28 = 0; i28 < (*in).fallback_order_count; i28++) {
        if (!push_own(arr27, str_value((*in).fallback_order[i28]))) { qa_save_value_free(arr27); goto fail; }
      }
      if (!set_own(obj, "fallbackOrder", arr27)) goto fail; }
  }
  { qa_save_value *arr29 = qa_save_value_array();
    if (arr29 == NULL) goto fail;
    for (size_t i30 = 0; i30 < (*in).state_count; i30++) {
      if (!push_own(arr29, build_q2_weapon_state_entry(&(*in).states[i30]))) { qa_save_value_free(arr29); goto fail; }
    }
    if (!set_own(obj, "states", arr29)) goto fail; }
  { qa_save_value *arr31 = qa_save_value_array();
    if (arr31 == NULL) goto fail;
    for (size_t i32 = 0; i32 < (*in).input_count; i32++) {
      if (!push_own(arr31, build_q2_weapon_input_entry(&(*in).inputs[i32]))) { qa_save_value_free(arr31); goto fail; }
    }
    if (!set_own(obj, "inputs", arr31)) goto fail; }
  { qa_save_value *arr35 = qa_save_value_array();
    if (arr35 == NULL) goto fail;
    for (size_t i36 = 0; i36 < (*in).blaster_cause_count; i36++) {
      if (!push_own(arr35, build_q2_blaster_cause(&(*in).blaster_causes[i36]))) { qa_save_value_free(arr35); goto fail; }
    }
    if (!set_own(obj, "blasterCauses", arr35)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

qa_q2w_input qa_read_q2_weapon_input(const qa_save_reader *reader) {
  qa_q2w_input out;
  read_q2_weapon_input(reader, &out);
  return out;
}
qa_q2w_state qa_read_q2_weapon_state(const qa_save_reader *reader) {
  qa_q2w_state out;
  read_q2_weapon_state(reader, &out);
  return out;
}
qa_q2w_weapons_checkpoint qa_read_q2_weapons_checkpoint(const qa_save_reader *reader, qa_persist_decoded *owner) {
  qa_q2w_weapons_checkpoint out;
  read_q2_weapons_checkpoint(reader, &out, owner);
  return out;
}
uint8_t *qa_encode_q2_weapons_checkpoint(const qa_q2w_weapons_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *value = build_q2_weapons_checkpoint(checkpoint);
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}
bool qa_decode_q2_weapons_checkpoint(const uint8_t *bytes, size_t length, qa_q2w_weapons_checkpoint *out,
                                     qa_persist_decoded *owner) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q2-weapons")) { qa_save_value_free(root); return false; }
  owner->root = root;
  read_q2_weapons_checkpoint(&reader, out, owner);
  qa_save_reader_destroy(&reader);
  return true;
}
