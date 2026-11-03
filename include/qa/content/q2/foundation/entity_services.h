/* Port of src/content/q2/foundation/entity-services.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_ENTITY_SERVICES_H
#define QA_CONTENT_Q2_FOUNDATION_ENTITY_SERVICES_H
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/host.h"
/* NULL only on OOM. modules borrowed. */
qa_q2_entity_services *qa_q2_entity_services_create(qa_q2_foundation_host *host, qa_q2_game_options options, const qa_q2_spawn_module *modules, size_t module_count);
void qa_q2_entity_services_destroy(qa_q2_entity_services *s);
qa_q2_game_services *qa_q2_entity_services_game(qa_q2_entity_services *s);
/* The Q2EntityServices a qa_q2_entity_services_game() result belongs to
 * (TS holds the one object under both types). */
qa_q2_entity_services *qa_q2_entity_services_of_game(qa_q2_game_services *game);
/* Q2EntityServices.capture() through its Q2GameServices view. */
qa_q2_foundation_checkpoint qa_q2_game_services_capture(qa_q2_game_services *game);
qa_q2_entity *const *qa_q2_entity_services_entities(qa_q2_entity_services *s, size_t *n);
qa_authored_target *const *qa_q2_entity_services_authored_targets(qa_q2_entity_services *s, size_t *n);
void qa_q2_entity_services_add_authored_target(qa_q2_entity_services *s, qa_authored_target *t);
/* TS authoredTargets.set: replaces the entry for t->actor in place, else appends. */
void qa_q2_entity_services_set_authored_target(qa_q2_entity_services *s, qa_authored_target *t);
qa_q2_source_callbacks *qa_q2_entity_services_callbacks(qa_q2_entity_services *s);
qa_q2_counters *qa_q2_entity_services_counters(qa_q2_entity_services *s);
/* capture storage owned by services until next capture/destroy. */
qa_q2_foundation_checkpoint qa_q2_entity_services_capture(qa_q2_entity_services *s);
void qa_q2_entity_services_restore(qa_q2_entity_services *s, qa_q2_foundation_checkpoint cp);
qa_q2_entity *qa_q2_entity_services_spawn(qa_q2_entity_services *s, qa_q2_spawn_fields f);
qa_q2_entity *qa_q2_entity_services_spawn_entity(qa_q2_entity_services *s, qa_q2_entity *e);
qa_q2_entity *qa_q2_entity_services_create_entity(qa_q2_entity_services *s, const char *classname, const qa_q2_field_pair *v, size_t n);
qa_q2_entity *qa_q2_entity_services_attach_player(qa_q2_entity_services *s, qa_owned_actor actor);
qa_owned_actor qa_q2_entity_services_allocate_actor(qa_q2_entity_services *s, const qa_q2_spawn_fields *f, const char *definition);
qa_q2_entity *qa_q2_entity_services_attach(qa_q2_entity_services *s, qa_owned_actor actor, qa_q2_spawn_fields f);
void qa_q2_entity_services_run_actor(qa_q2_entity_services *s, qa_actor_id actor, void (*cb)(void *ctx), void *ctx);
void qa_q2_entity_services_apply_projectile_behavior(qa_q2_entity_services *s, qa_actor_id actor, double seconds);
void qa_q2_entity_services_pre_physics(qa_q2_entity_services *s, qa_actor_id actor);
void qa_q2_entity_services_post_physics(qa_q2_entity_services *s, qa_actor_id actor);
double qa_q2_entity_services_source_slot(qa_q2_entity_services *s, qa_actor_id actor, bool *has);
qa_q2_entity *const *qa_q2_entity_services_unsupported(qa_q2_entity_services *s, size_t *n);
#endif
