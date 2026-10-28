/* Port of src/app/bootstrap/effects/q3.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/effects/q3.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/event_ring.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
struct qa_app_q3_application_effects {
  qa_app_q3_effects_host host;
  qa_content_id content;
  const char *product;
  double time; /* state.time */
  qa_event_ring sounds; /* EventRing<SourceEffectSound>(8192) */
  bool has_cursor;
  qa_event_cursor cursor;
};
qa_app_q3_application_effects *qa_app_q3_application_effects_create(
    qa_app_q3_effects_host host, qa_content_id content, const char *product) {
  qa_app_q3_application_effects *e = calloc(1, sizeof *e);
  if (e == NULL) qa_range_error("Out of memory");
  e->host = host;
  e->content = content;
  e->product = product;
  if (!qa_event_ring_init(&e->sounds, 8192, sizeof(qa_app_source_effect_sound)))
    qa_range_error("Out of memory");
  return e;
}
void qa_app_q3_application_effects_destroy(qa_app_q3_application_effects *effects) {
  if (effects == NULL) return;
  qa_event_ring_destroy(&effects->sounds);
  free(effects);
}
qa_content_id qa_app_q3_application_effects_content(const qa_app_q3_application_effects *effects) { return effects->content; }
double qa_app_q3_application_effects_time(const qa_app_q3_application_effects *effects) { return effects->time; }
const char *qa_app_q3_application_effects_product(const qa_app_q3_application_effects *effects) { return effects->product; }
void qa_app_q3_application_effects_admit_sound(qa_app_q3_application_effects *effects,
                                               qa_app_source_effect_sound sound) {
  qa_event_ring_admit(&effects->sounds, &sound);
}
static qa_app_source_effect_sound *take_sounds(qa_app_q3_application_effects *e, size_t *out_count) {
  if (!e->has_cursor) {
    qa_event_ring_open_cursor(&e->sounds, &e->cursor, false);
    e->has_cursor = true;
  }
  size_t count = 0;
  void *taken = qa_event_ring_take(&e->sounds, &e->cursor, &count);
  qa_event_ring_retire(&e->sounds);
  *out_count = count;
  if (taken == NULL) {
    taken = malloc(sizeof(qa_app_source_effect_sound));
    if (taken == NULL) qa_range_error("Out of memory");
  }
  return taken;
}
bool qa_app_q3_application_effects_event(qa_app_q3_application_effects *effects,
                                         qa_app_q3_character_presentation_event event, qa_vec3 origin) {
  effects->time = event.time_milliseconds;
  long long kind = (long long)event.event & ~0x300LL;
  switch (kind) {
    case QA_Q3_EV_PLAYER_TELEPORT_IN:
    case QA_Q3_EV_PLAYER_TELEPORT_OUT:
      effects->host.spawn_effect(effects->host.ctx, origin);
      return true;
    case QA_Q3_EV_JUMP_PAD:
      effects->host.smoke_puff(effects->host.ctx, origin, (qa_vec3){0, 0, 1}, 32,
                               (qa_vec4){1, 1, 1, 0.33}, 1000, event.time_milliseconds, 0, 1, "smokePuff");
      return true;
    case QA_Q3_EV_GIB_PLAYER:
      effects->host.gib_player(effects->host.ctx, origin);
      return true;
    case QA_Q3_EV_NONE: case QA_Q3_EV_FOOTSTEP: case QA_Q3_EV_FOOTSTEP_METAL:
    case QA_Q3_EV_FOOTSPLASH: case QA_Q3_EV_FOOTWADE: case QA_Q3_EV_SWIM:
    case QA_Q3_EV_STEP_4: case QA_Q3_EV_STEP_8: case QA_Q3_EV_STEP_12: case QA_Q3_EV_STEP_16:
    case QA_Q3_EV_FALL_SHORT: case QA_Q3_EV_FALL_MEDIUM: case QA_Q3_EV_FALL_FAR:
    case QA_Q3_EV_JUMP: case QA_Q3_EV_WATER_TOUCH: case QA_Q3_EV_WATER_LEAVE:
    case QA_Q3_EV_WATER_UNDER: case QA_Q3_EV_WATER_CLEAR:
    case QA_Q3_EV_NOAMMO: case QA_Q3_EV_CHANGE_WEAPON: case QA_Q3_EV_PAIN:
    case QA_Q3_EV_DEATH1: case QA_Q3_EV_DEATH2: case QA_Q3_EV_DEATH3:
    case QA_Q3_EV_OBITUARY: case QA_Q3_EV_STOPLOOPINGSOUND: case QA_Q3_EV_TAUNT:
      return true;
    default:
      return false;
  }
}
void qa_app_q3_application_effects_prepare(qa_app_q3_application_effects *effects,
                                           double time_milliseconds, double elapsed_milliseconds) {
  effects->time = time_milliseconds;
  effects->host.prepare(effects->host.ctx, time_milliseconds, elapsed_milliseconds);
}
void qa_app_q3_application_effects_frame(qa_app_q3_application_effects *effects,
    qa_scene_camera camera, const void *source_order, bool has_viewer, qa_actor_id viewer,
    bool has_q1_fog, qa_scene_fog q1_fog,
    const void **out_admission, const void **out_operations, size_t *out_operation_count,
    const void **out_lights, size_t *out_light_count) {
  effects->host.frame(effects->host.ctx, camera, source_order, has_viewer, viewer,
                      has_q1_fog, q1_fog, out_admission, out_operations, out_operation_count,
                      out_lights, out_light_count);
}
qa_app_source_effect_sound *qa_app_q3_application_effects_drain_sounds(
    qa_app_q3_application_effects *effects, size_t *out_count) {
  return take_sounds(effects, out_count);
}
void qa_app_q3_application_effects_reset_round(qa_app_q3_application_effects *effects) {
  effects->host.clear_blood_owners(effects->host.ctx);
  effects->host.pool_initialize(effects->host.ctx);
  effects->host.marks_reset(effects->host.ctx);
  qa_event_ring_reset(&effects->sounds);
  effects->has_cursor = false;
}
void qa_app_q3_application_effects_close(qa_app_q3_application_effects *effects) {
  qa_app_q3_application_effects_reset_round(effects);
}
