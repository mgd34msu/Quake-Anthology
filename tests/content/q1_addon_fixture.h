/*
 * Test fixture for the Q1 addon checks: a real Q1 game over the real world
 * tables (q1_world_fixture.h), its Q1Base, and a Q1AddonContext whose
 * Q1AddonServices and campaign binding check a sentinel ctx. Header-only.
 * GPL-2.0-or-later.
 */
#ifndef QA_TESTS_CONTENT_Q1_ADDON_FIXTURE_H
#define QA_TESTS_CONTENT_Q1_ADDON_FIXTURE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q1_world_fixture.h"
#include "qa/content/q1/addons.h"

/* Every addon host callback verifies the ctx it was registered with. */
static int q1af_sentinel;
static int q1af_ctx_checks, q1af_ctx_errors; /* one check per host callback invocation */
#define Q1AF_CTX(ctx) do { q1af_ctx_checks++; if ((ctx) != &q1af_sentinel) q1af_ctx_errors++; } while (0)

typedef struct q1_addon_fixture {
  q1_world_fixture fx;
  qa_q1_base *base;
  qa_q1ad_context *context;
  qa_q1_campaign_binding campaign;
  double campaign_flags;
  int campaign_skill;
  double skill_cvar;
  double horde_cvar;
  int rune_events, alpha_events, music_events, fog_events;
  qa_q1ad_event last_event;
  char **strings; size_t string_count, string_capacity; /* copies of recorded event strings */
} q1_addon_fixture;

/* One fixture at a time: the sentinel ctx cannot carry the fixture. */
static q1_addon_fixture *q1af_current;

static inline double q1af_read_flags(void *ctx) { Q1AF_CTX(ctx); return q1af_current->campaign_flags; }
static inline void q1af_write_flags(void *ctx, double flags) { Q1AF_CTX(ctx); q1af_current->campaign_flags = flags; }
static inline void q1af_set_skill(void *ctx, int skill) { Q1AF_CTX(ctx); q1af_current->campaign_skill = skill; }

static inline void q1af_emit(void *ctx, const qa_q1ad_event *event) {
  Q1AF_CTX(ctx);
  q1_addon_fixture *af = q1af_current;
  af->last_event = *event;
  if (event->kind == QA_Q1AD_EVENT_RUNE_COLLECTED) af->rune_events++;
  if (event->kind == QA_Q1AD_EVENT_ALPHA) af->alpha_events++;
  if (event->kind == QA_Q1AD_EVENT_MUSIC) af->music_events++;
  if (event->kind == QA_Q1AD_EVENT_FOG) af->fog_events++;
}
static inline bool q1af_is_monster(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); return false; }
static inline double q1af_cvar(void *ctx, const char *name) {
  Q1AF_CTX(ctx);
  if (strcmp(name, "skill") == 0) return q1af_current->skill_cvar;
  if (strcmp(name, "horde") == 0) return q1af_current->horde_cvar;
  return 0;
}
static inline void q1af_set_cvar(void *ctx, const char *name, const char *value) {
  Q1AF_CTX(ctx);
  if (strcmp(name, "skill") == 0) q1af_current->skill_cvar = strtod(value, NULL);
}

static inline qa_q1ad_services q1af_services(void) {
  qa_q1ad_services services = {.ctx = &q1af_sentinel, .cheat_arsenal = NULL, .emit = q1af_emit,
                               .is_monster = q1af_is_monster, .cvar = q1af_cvar, .set_cvar = q1af_set_cvar};
  return services;
}

/* Recorded Q1Events keep their strings: the emitter's strings are borrowed for
 * the call only (a message text may live on the emitter's stack). */
static inline const char *q1af_keep(q1_addon_fixture *af, const char *text) {
  if (text == NULL) return NULL;
  af->strings = q1_wf_grow(af->strings, &af->string_capacity, af->string_count, sizeof *af->strings);
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) abort();
  memcpy(copy, text, length);
  af->strings[af->string_count++] = copy;
  return copy;
}
static inline void q1af_record_emit(void *ctx, qa_q1_event event) {
  q1_addon_fixture *af = q1af_current;
  event.path = q1af_keep(af, event.path);
  event.text = q1af_keep(af, event.text);
  event.pattern = q1af_keep(af, event.pattern);
  event.id = q1af_keep(af, event.id);
  q1_wf_emit(ctx, event);
}

/* Creates the game and base; the context is created by the caller (bare
 * qa_q1ad_context_create, or qa_q1ad_register_q1_campaign_addons). */
static inline void q1af_open_world(q1_addon_fixture *af, const qa_q1_options *options) {
  memset(af, 0, sizeof *af);
  q1af_current = af;
  if (!q1_world_fixture_create(&af->fx, options != NULL ? *options : q1_world_fixture_options())) {
    fprintf(stderr, "q1_addon_fixture: world fixture failed\n");
    abort();
  }
  af->fx.game->host.emit = q1af_record_emit;
  af->campaign = (qa_q1_campaign_binding){.ctx = &q1af_sentinel, .read_flags = q1af_read_flags,
                                          .write_flags = q1af_write_flags, .set_skill = q1af_set_skill};
  qa_q1_base_options base_options;
  memset(&base_options, 0, sizeof base_options);
  base_options.campaign = &af->campaign;
  af->base = qa_q1_register_base(af->fx.game, &base_options);
  if (af->base == NULL) abort();
}
static inline void q1af_open(q1_addon_fixture *af, qa_q1ad_program program) {
  q1af_open_world(af, NULL);
  af->context = qa_q1ad_context_create(af->base, program, q1af_services());
  if (af->context == NULL) abort();
}
static inline void q1af_close(q1_addon_fixture *af) {
  qa_q1ad_context_destroy(af->context);
  qa_q1_base_destroy(af->base);
  q1_world_fixture_destroy(&af->fx);
  for (size_t i = 0; i < af->string_count; i++) free(af->strings[i]);
  free(af->strings);
  q1af_current = NULL;
}

/* game.spawners.has(classname): a spawn handler is registered. */
static inline bool q1af_has_spawner(q1_addon_fixture *af, const char *classname) {
  qa_q1_services *game = af->fx.game;
  for (size_t i = 0; i < game->spawner_count; i++) if (strcmp(game->spawners[i].classname, classname) == 0) return true;
  return false;
}
static inline qa_q1_actor *q1af_create(q1_addon_fixture *af, const char *classname) {
  return qa_q1_create(af->fx.game, classname, NULL, NULL);
}
static inline qa_q1_actor *q1af_spawn(q1_addon_fixture *af, const char *classname) {
  qa_q1_actor *entity = q1af_create(af, classname);
  qa_q1_spawn_entity(af->fx.game, entity);
  return entity;
}
/* TS harness spawn(classname, fields): game.create(classname, source) then spawnEntity. */
static inline qa_q1_actor *q1af_spawn_fields(q1_addon_fixture *af, const char *classname, const char *const *pairs, size_t pair_count) {
  qa_q1_entity_property properties[16];
  size_t count = 0;
  properties[count].key = (char *)"classname";
  properties[count++].value = (char *)classname;
  for (size_t i = 0; i < pair_count && count < 16; i++) {
    properties[count].key = (char *)pairs[2 * i];
    properties[count++].value = (char *)pairs[2 * i + 1];
  }
  qa_q1_entity source = {.properties = properties, .property_count = count};
  qa_q1_actor *entity = qa_q1_create(af->fx.game, classname, &source, NULL);
  qa_q1_spawn_entity(af->fx.game, entity);
  return entity;
}
static inline void q1af_set_body(q1_addon_fixture *af, qa_q1_actor *entity, qa_vec3 origin, qa_bounds bounds) {
  qa_q1_body_patch patch = {.has_origin = true, .origin = origin, .has_bounds = true, .bounds = bounds};
  qa_q1_set_body(af->fx.game, entity, &patch);
}
static inline void q1af_action(q1_addon_fixture *af, qa_q1_actor *entity, const char *name) {
  qa_q1_services *game = af->fx.game;
  qa_q1_call_action(game, entity, qa_q1_callback_registry_action(game->named, entity, name));
}
static inline void q1af_use(q1_addon_fixture *af, qa_q1_actor *entity, const qa_actor_id *activator) {
  qa_q1_call_use(af->fx.game, entity, entity->use, NULL, activator);
}
static inline void q1af_touch(q1_addon_fixture *af, qa_q1_actor *entity, qa_actor_id other) {
  qa_q1_call_touch(af->fx.game, entity, entity->touch, other, NULL, NULL);
}
/* Runs the entity's pending think (game.schedule) and advances game time to it. */
static inline void q1af_think(q1_addon_fixture *af, qa_q1_actor *entity) {
  q1_world_fixture *fx = &af->fx;
  for (size_t i = 0; i < fx->think_count; i++)
    if (qa_same_owned_actor(fx->thinks[i].actor, entity->actor)) {
      double due = fx->thinks[i].due;
      qa_q1_services_set_time(fx->game, due);
      q1_world_fixture_advance(fx, due);
      return;
    }
}
static inline size_t q1af_count_events(q1_addon_fixture *af, const char *kind) {
  size_t count = 0;
  for (size_t i = 0; i < af->fx.event_count; i++) if (strcmp(af->fx.events[i].kind, kind) == 0) count++;
  return count;
}
static inline const qa_q1_event *q1af_first_event(q1_addon_fixture *af, const char *kind) {
  for (size_t i = 0; i < af->fx.event_count; i++) if (strcmp(af->fx.events[i].kind, kind) == 0) return &af->fx.events[i];
  return NULL;
}
static inline const qa_q1_state_extension *q1af_extension(q1_addon_fixture *af) {
  qa_q1_services *game = af->fx.game;
  for (size_t i = 0; i < game->state_extension_count; i++)
    if (strcmp(game->state_extensions[i]->id, af->context->extension_id) == 0) return game->state_extensions[i];
  return NULL;
}

#endif
