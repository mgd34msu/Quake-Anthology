/* Port of src/app/bootstrap/simulation/grapple-checkpoint.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/grapple_checkpoint.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/weapon_slot_checkpoint.h"
#include "qa/foundation/support.h"
#include "qa/persistence/q1_foundation.h"
#include "qa/persistence/q2_foundation.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out)) qa_range_error("Out of memory");
  return out;
}
static double finite_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_finite(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static bool boolean_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  bool value = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_vec3 vector_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  qa_vec3 value = qa_read_vector(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_saved_actor_id actor_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  qa_saved_actor_id value = qa_read_saved_actor(&f);
  qa_save_reader_destroy(&f);
  return value;
}
/* nullable(readSavedActor): false is TS null. */
static bool nullable_actor_of(const qa_save_reader *reader, const char *name, qa_saved_actor_id *out) {
  qa_save_reader f = field(reader, name);
  bool present = !qa_save_reader_is_null(&f);
  if (present) *out = qa_read_saved_actor(&f);
  qa_save_reader_destroy(&f);
  return present;
}
static void *array_of(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

typedef struct read_state { qa_app_sim_grapple_runtime_checkpoint out; } read_state;
static void release_partial(void *arg) { qa_app_sim_grapple_runtime_checkpoint_destroy(&((read_state *)arg)->out); }

qa_app_sim_grapple_runtime_checkpoint qa_app_sim_read_grapple_runtime_checkpoint(const qa_save_reader *reader) {
  read_state state;
  memset(&state, 0, sizeof state);
  qa_app_sim_grapple_runtime_checkpoint *out = &state.out;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, release_partial, &state);
  qa_save_reader random_r = field(reader, "random");
  qa_random_state random = qa_read_random(&random_r);
  if (random.kind != QA_RANDOM_STATE_GLIBC_RANDOM && random.kind != QA_RANDOM_STATE_Q2_RERELEASE_MT19937) {
    free((void *)random.words.values);
    qa_save_reader_fail(&random_r, "Unsupported grapple random stream");
  }
  out->random = qa_app_sim_source_random_state_of(&random);
  free((void *)random.words.values);
  qa_save_reader_destroy(&random_r);
  qa_save_reader version = field(reader, "version");
  out->version = qa_save_reader_literal_number(&version, 2);
  qa_save_reader_destroy(&version);
  qa_save_reader animations = field(reader, "weaponAnimations");
  size_t count = qa_save_reader_list_count(&animations);
  out->weapon_animations = array_of(count, sizeof *out->weapon_animations);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&animations, i);
    qa_app_sim_grapple_saved_animation *saved = &out->weapon_animations[i];
    saved->actor = actor_of(&entry, "actor");
    qa_save_reader s = field(&entry, "state");
    saved->animation.state = qa_app_sim_read_grapple_weapon_state(&s);
    saved->animation.state_read_order = true;
    qa_save_reader_destroy(&s);
    saved->animation.next_frame_at = finite_of(&entry, "nextFrameAt");
    saved->animation.kick_origin = vector_of(&entry, "kickOrigin");
    saved->animation.kick_pitch = finite_of(&entry, "kickPitch");
    out->weapon_animation_count = i + 1;
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&animations);
  qa_save_reader controls = field(reader, "controls");
  count = qa_save_reader_list_count(&controls);
  out->controls = array_of(count, sizeof *out->controls);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&controls, i);
    qa_app_sim_grapple_saved_control *saved = &out->controls[i];
    saved->actor = actor_of(&entry, "actor");
    saved->control.held = boolean_of(&entry, "held");
    saved->control.jump = boolean_of(&entry, "jump");
    qa_save_reader bit = field(&entry, "teleportBit");
    if (!qa_save_reader_is_null(&bit)) {
      static const double bits[] = {0, 4};
      saved->control.has_teleport_bit = true;
      saved->control.teleport_bit = qa_save_reader_choice_number(&bit, bits, 2);
    }
    qa_save_reader_destroy(&bit);
    saved->control.pressed = boolean_of(&entry, "pressed");
    saved->control.released = boolean_of(&entry, "released");
    saved->control.previous_velocity = vector_of(&entry, "previousVelocity");
    saved->control.prediction_suppressed = boolean_of(&entry, "predictionSuppressed");
    saved->control.read_order = true;
    out->control_count = i + 1;
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&controls);
  qa_save_reader source = field(reader, "source"), kind_r = field(&source, "kind");
  static const char *const kinds[] = {"q1-threewave", "q2-ctf", "q2-lmctf", "q3-qvm"};
  const char *kind = qa_save_reader_choice_string(&kind_r, kinds, 4);
  qa_save_reader_destroy(&kind_r);
  qa_app_sim_grapple_source_checkpoint *saved_source = &out->source;
  if (strcmp(kind, "q3-qvm") == 0) {
    qa_save_reader component = field(&source, "component");
    saved_source->component = qa_app_sim_read_qvm_grapple_source_checkpoint(&component);
    saved_source->kind = "q3-qvm";
    qa_save_reader_destroy(&component);
    qa_save_reader holstered = field(&source, "holstered");
    count = qa_save_reader_list_count(&holstered);
    saved_source->holstered = array_of(count, sizeof *saved_source->holstered);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&holstered, i);
      saved_source->holstered[i] = qa_read_saved_actor(&entry);
      saved_source->holstered_count = i + 1;
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&holstered);
  } else if (strcmp(kind, "q1-threewave") == 0) {
    qa_save_reader entities = field(&source, "entities");
    saved_source->q1_saved = qa_read_q1_foundation_checkpoint(&entities);
    saved_source->has_q1_saved = true;
    saved_source->kind = "q1-threewave";
    qa_save_reader_destroy(&entities);
  } else {
    bool ctf = strcmp(kind, "q2-ctf") == 0;
    saved_source->kind = ctf ? "q2-ctf" : "q2-lmctf";
    qa_save_reader entities = field(&source, "entities");
    saved_source->q2_entities = qa_read_q2_foundation_checkpoint(&entities, &saved_source->q2_owner);
    qa_save_reader_destroy(&entities);
    qa_save_reader states = field(&source, "states");
    count = qa_save_reader_list_count(&states);
    if (ctf) saved_source->ctf_states = array_of(count, sizeof *saved_source->ctf_states);
    else saved_source->lmctf_states = array_of(count, sizeof *saved_source->lmctf_states);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&states, i), s = field(&entry, "state");
      qa_saved_actor_id actor = actor_of(&entry, "actor");
      if (ctf) {
        qa_ctf_grapple_checkpoint value;
        memset(&value, 0, sizeof value);
        value.has_grapple = nullable_actor_of(&s, "grapple", &value.grapple);
        static const char *const states_choice[] = {"fly", "pull", "hang"};
        qa_save_reader g = field(&s, "grappleState");
        value.grapple_state = qa_save_reader_choice_string(&g, states_choice, 3);
        qa_save_reader_destroy(&g);
        value.grapple_release_time = finite_of(&s, "grappleReleaseTime");
        qa_save_reader k = field(&s, "grappleNoKnockback");
        if (!qa_save_reader_is_undefined(&k) && !qa_save_reader_is_null(&k)) {
          value.has_no_knockback = true;
          value.grapple_no_knockback = qa_save_reader_boolean(&k);
        }
        qa_save_reader_destroy(&k);
        saved_source->ctf_states[i].actor = actor;
        saved_source->ctf_states[i].state = value;
        saved_source->ctf_state_count = i + 1;
      } else {
        qa_lmctf_grapple_checkpoint value;
        memset(&value, 0, sizeof value);
        value.has_hook = nullable_actor_of(&s, "hook", &value.hook);
        static const double hook_states[] = {0, 1, 2};
        qa_save_reader h = field(&s, "hookState");
        value.hook_state = qa_save_reader_choice_number(&h, hook_states, 3);
        qa_save_reader_destroy(&h);
        value.hook_length = finite_of(&s, "hookLength");
        value.hook_held = boolean_of(&s, "hookHeld");
        saved_source->lmctf_states[i].actor = actor;
        saved_source->lmctf_states[i].state = value;
        saved_source->lmctf_state_count = i + 1;
      }
      qa_save_reader_destroy(&s);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&states);
  }
  qa_save_reader_destroy(&source);
  qa_cleanup_pop(&cleanup);
  return state.out;
}
