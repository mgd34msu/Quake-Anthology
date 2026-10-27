/*
 * Port of src/contracts/world.ts (types only): actor registry, body
 * table, callbacks, scheduling, and client admission shapes.
 * Method interfaces become function tables + ctx; TS callbacks that
 * may be null are nullable function pointers.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_WORLD_H
#define QA_CONTRACTS_WORLD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/time.h"

typedef struct qa_actor_observation {
  qa_actor_id id;
  qa_provider_id owner;
  const char *definition; /* `${string}:${string}` */
} qa_actor_observation;

typedef struct qa_actor_observation_result {
  bool has_value;
  qa_actor_observation value;
} qa_actor_observation_result;

/** TS ActorRegistry. ownedBy() returns pointer + count. */
typedef struct qa_actor_registry {
  void *ctx;
  qa_session_id session;
  qa_owned_actor (*allocate)(void *ctx, qa_provider_id owner, const char *definition);
  void (*release)(void *ctx, qa_owned_actor actor);
  bool (*is_live)(void *ctx, qa_actor_id actor);
  qa_actor_observation_result (*observe)(void *ctx, qa_actor_id actor);
  const qa_owned_actor *(*owned_by)(void *ctx, qa_provider_id owner, size_t *out_count);
} qa_actor_registry;

typedef struct qa_body_state {
  qa_vec3 origin;
  qa_vec3 angles;
  qa_vec3 velocity;
  qa_bounds bounds;
  const qa_actor_id *ground; /* nullable */
} qa_body_state;

/* TS BodyAttachment.follow union on kind:
 * "translation"(offset) | "center" | "bounds-min"(offset). */
typedef enum qa_body_follow_kind {
  QA_BODY_FOLLOW_TRANSLATION, QA_BODY_FOLLOW_CENTER, QA_BODY_FOLLOW_BOUNDS_MIN
} qa_body_follow_kind;
typedef struct qa_body_attachment {
  qa_actor_id anchor;
  qa_body_follow_kind follow_kind;
  qa_vec3 follow_offset;
} qa_body_attachment;

typedef struct qa_linked_body {
  qa_actor_id actor;
  qa_body_state state;
  qa_bounds absolute_bounds;
  double link_count;
} qa_linked_body;

typedef struct qa_body_state_result { bool has_value; qa_body_state value; } qa_body_state_result;
typedef struct qa_body_attachment_result {
  bool has_value;
  qa_body_attachment value;
} qa_body_attachment_result;
typedef struct qa_linked_body_result { bool has_value; qa_linked_body value; } qa_linked_body_result;

/** TS BodyTable. link()'s optional origin is a nullable pointer. */
typedef struct qa_body_table {
  void *ctx;
  qa_body_state_result (*read)(void *ctx, qa_actor_id actor);
  void (*write)(void *ctx, qa_owned_actor actor, qa_body_state state);
  void (*attach)(void *ctx, qa_owned_actor actor, qa_body_attachment attachment);
  void (*detach)(void *ctx, qa_owned_actor actor);
  qa_body_attachment_result (*attachment)(void *ctx, qa_actor_id actor);
  qa_linked_body_result (*linked)(void *ctx, qa_actor_id actor);
  void (*link)(void *ctx, qa_owned_actor actor, const qa_vec3 *origin);
  void (*unlink)(void *ctx, qa_owned_actor actor);
} qa_body_table;

typedef struct qa_touch_surface {
  const char *name;
  double native_flags;
  double native_value;
} qa_touch_surface;

/* TS TouchContact.sourceTrace: {kind:"q2-rerelease", trace(q2), ent, inverted}. */
typedef struct qa_touch_source_trace {
  qa_trace_result trace;
  qa_actor_id ent;
  bool inverted;
} qa_touch_source_trace;

typedef struct qa_touch_contact {
  qa_owned_actor self;
  qa_actor_id other;
  const qa_plane *plane;               /* nullable */
  const qa_touch_surface *surface;     /* nullable */
  const qa_touch_source_trace *source_trace; /* optional */
} qa_touch_contact;

typedef struct qa_pain_reaction {
  const qa_attack_provenance *attack; /* nullable */
  qa_owned_actor self;
  const qa_actor_id *attacker;        /* nullable */
  double kick;
  double damage;
} qa_pain_reaction;

typedef struct qa_death_reaction {
  qa_pain_reaction pain;
  const qa_actor_id *inflictor; /* nullable */
  qa_vec3 point;
} qa_death_reaction;

/** TS ActorCallbacks: each callback is nullable; all are synchronous. */
typedef struct qa_actor_callbacks {
  void *ctx;
  void (*think)(void *ctx, qa_owned_actor self, qa_frame_context frame);
  void (*touch)(void *ctx, qa_touch_contact contact);
  void (*use)(void *ctx, qa_owned_actor self, const qa_actor_id *other,
              const qa_actor_id *activator);
  void (*pain)(void *ctx, qa_pain_reaction reaction);
  void (*die)(void *ctx, qa_death_reaction reaction);
} qa_actor_callbacks;

typedef struct qa_scheduled_think {
  qa_actor_id actor;
  qa_callback_id callback;
  qa_think_timing timing;
} qa_scheduled_think;

typedef struct qa_scheduled_think_result {
  bool has_value;
  qa_scheduled_think value;
} qa_scheduled_think_result;

/** TS ActorSchedule. */
typedef struct qa_actor_schedule {
  void *ctx;
  void (*schedule)(void *ctx, qa_owned_actor actor, qa_callback_id callback,
                   qa_think_timing timing);
  void (*cancel)(void *ctx, qa_owned_actor actor);
  qa_scheduled_think_result (*pending)(void *ctx, qa_actor_id actor);
} qa_actor_schedule;

typedef struct qa_q2_admission_result {
  bool allowed;
  const char *userinfo;
} qa_q2_admission_result;

/* TS ClientAdmission union on kind: "q1" connect(client) |
 * "q2-classic" connect(client,userinfo) | "q2-rerelease"
 * connect(client,userinfo,socialId,isBot) | "q3"
 * connect(client,firstTime,isBot) -> string | null. */
typedef enum qa_client_admission_kind {
  QA_CLIENT_ADMISSION_Q1, QA_CLIENT_ADMISSION_Q2_CLASSIC,
  QA_CLIENT_ADMISSION_Q2_RERELEASE, QA_CLIENT_ADMISSION_Q3
} qa_client_admission_kind;
typedef struct qa_client_admission {
  qa_client_admission_kind kind;
  void *ctx;
  void (*connect_q1)(void *ctx, qa_client_id client);
  qa_q2_admission_result (*connect_q2_classic)(void *ctx, qa_client_id client,
                                               const char *userinfo);
  qa_q2_admission_result (*connect_q2_rerelease)(void *ctx, qa_client_id client,
                                                 const char *userinfo,
                                                 const char *social_id, bool is_bot);
  const char *(*connect_q3)(void *ctx, qa_client_id client, bool first_time,
                            bool is_bot); /* NULL = admitted */
} qa_client_admission;

/* TS ActorLifetimePolicy. reuse: "immediate"|"source-cooldown";
 * clear: "source-edict"|"source-gentity";
 * exhaustion: "fatal"|"qw-last-slot" (exact strings). */
typedef struct qa_actor_lifetime_policy {
  double first_dynamic_slot;
  const char *reuse;
  void *ctx;
  bool (*reusable_after)(void *ctx, qa_source_time freed_at, qa_source_time now);
  const char *clear;
  const char *exhaustion;
} qa_actor_lifetime_policy;

#endif
