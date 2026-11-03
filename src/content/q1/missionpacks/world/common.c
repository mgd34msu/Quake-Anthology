/* Port of src/content/q1/missionpacks/world/common.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/common.h"
#include "../missionpacks_internal.h"

void qa_q1mp_world_number(qa_q1_actor *entity, const char *key, double value) {
  qa_q1mp_field_number(entity, key, qa_q1mp_fround(value));
}
void qa_q1mp_world_vector(qa_q1_actor *entity, const char *key, qa_vec3 value) {
  char buf[128];
  /* TS template interpolates Math.fround(x) as JS numbers. */
  (void)snprintf(buf, sizeof buf, "%s %s %s", QA_JS_NUMBER(qa_q1mp_fround(value.x)),
                 QA_JS_NUMBER(qa_q1mp_fround(value.y)), QA_JS_NUMBER(qa_q1mp_fround(value.z)));
  qa_q1mp_field_text(entity, key, buf);
}
void qa_q1mp_world_later(qa_q1_services *game, qa_q1_actor *entity, double delay, const char *name) {
  qa_q1_schedule(game, entity, delay, qa_q1_callback_registry_action(game->named, entity, name));
}
static void zero_angles(qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1_body_patch patch = {0};
  patch.has_angles = true; patch.angles = QA_Q1_ZERO;
  qa_q1_set_body(game, entity, &patch);
}
void qa_q1mp_world_trigger(qa_q1_services *game, qa_q1_actor *entity) {
  entity->movedir = qa_q1_move_direction(qa_q1_get_body(game, entity).angles, game);
  entity->solid = "trigger"; qa_q1mp_set_string(&entity->model, "");
  zero_angles(game, entity);
}
void qa_q1mp_world_brush(qa_q1_services *game, qa_q1_actor *entity) {
  entity->mangle = qa_q1_get_body(game, entity).angles;
  entity->solid = "bsp"; entity->movement = "push";
  zero_angles(game, entity);
}
/* message NULL = TS default "". The prior strings are restored as TS finally does. */
void qa_q1mp_world_target_event(qa_q1_services *game, qa_q1_actor *entity, const char *target, const char *message) {
  char *prior = entity->target, *prior_message = entity->message;
  entity->target = NULL; entity->message = NULL;
  qa_q1mp_set_string(&entity->target, target);
  qa_q1mp_set_string(&entity->message, message != NULL ? message : "");
  const qa_actor_id activator = entity->activator;
  qa_q1_use_targets(game, entity, entity->has_activator ? &activator : NULL);
  free(entity->target); free(entity->message);
  entity->target = prior; entity->message = prior_message;
}
