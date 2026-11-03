/* Port of src/app/bootstrap/simulation/native-q1-pusher.ts: native source
 * fields join the same pusher transaction used by raw QC. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_q1_pusher.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/q1/foundation/entity.h"
#include "qa/content/q1/foundation/entity_services.h"
#include "qa/network/common/js_number.h"
#include "qa/world/actors/body.h"

typedef qa_app_sim_native_q1_pusher pusher_t;

static bool np_read(void *ctx, qa_actor_id actor, qa_q1_physics_entity *out) {
  pusher_t *p = ctx;
  qa_q1_physics_entity physical;
  if (!qa_app_sim_shared_physics_read_q1_pusher(p->physics, actor, &physical)) return false;
  qa_q1_actor *source = qa_q1_get_entity(p->game, &actor);
  if (source != NULL) {
    physical.local_time_seconds = qa_q1_actor_number(source, "ltime");
    physical.next_think_seconds = source->next_think;
    physical.state.flags = source->movement_flags;
  }
  *out = physical;
  return true;
}
static void np_write(void *ctx, qa_q1_physics_entity physical) {
  pusher_t *p = ctx;
  qa_app_sim_shared_physics_write_q1_pusher(p->physics, physical);
  qa_q1_actor *source = qa_q1_get_entity(p->game, &physical.actor.id);
  if (source != NULL) {
    char text[40];
    qa_net_js_number(physical.local_time_seconds, text, sizeof text);
    if (!qa_q1_actor_field_set(source, "ltime", text)) abort();
    source->next_think = physical.next_think_seconds;
    source->movement_flags = physical.state.flags;
  }
}
static void np_link(void *ctx, qa_owned_actor actor, bool touch) {
  pusher_t *p = ctx;
  qa_shared_body_table_link(p->game->host.bodies, actor, NULL);
  if (touch) qa_app_sim_shared_physics_touch_triggers(p->physics, actor);
}
static void np_blocked(void *ctx, qa_owned_actor actor, qa_actor_id other) {
  pusher_t *p = ctx;
  qa_q1_actor *entity = qa_q1_get_entity(p->game, &actor.id);
  if (entity != NULL && entity->blocked.fn != NULL) entity->blocked.fn(entity->blocked.ctx, p->game, entity, other);
}
/* services.ctx is &p->env, the first member of p. */
static void np_think(void *ctx, qa_owned_actor actor) {
  pusher_t *p = ctx;
  qa_q1_actor *entity = qa_q1_get_entity(p->game, &actor.id);
  if (entity != NULL && entity->think.fn != NULL) entity->think.fn(entity->think.ctx, p->game, entity);
}

qa_app_sim_native_q1_pusher *qa_app_sim_native_q1_pusher_create(qa_q1_services *game, qa_app_sim_shared_physics *physics) {
  pusher_t *p = calloc(1, sizeof *p);
  if (p == NULL) return NULL;
  p->game = game; p->physics = physics;
  p->env.physics = physics; p->env.projection_ctx = p;
  p->env.read = np_read; p->env.write = np_write; p->env.link = np_link; p->env.blocked = np_blocked;
  return p;
}
void qa_app_sim_native_q1_pusher_destroy(qa_app_sim_native_q1_pusher *p) {
  if (p == NULL) return;
  free(p->env.candidates);
  free(p);
}
qa_q1_pusher_services qa_app_sim_native_q1_pusher_services(qa_app_sim_native_q1_pusher *p) {
  qa_q1_pusher_services services = qa_app_sim_shared_physics_q1_pusher_services(&p->env);
  services.think = np_think;
  return services;
}
