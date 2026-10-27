/*
 * Port of src/content/composition/q1/clients.ts: native client globals on
 * the same source actors as the selected character.
 * Mirrored surface from the unported Q1Foundation (TS owner:
 * src/content/q1/foundation/runtime.ts): qa_comp_q1_client_game carries
 * only what clients.ts reads (options, the actor registry hooks,
 * combat.setTraits team, and player autoSwitch assignment).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q1_CLIENTS_H
#define QA_CONTENT_COMPOSITION_Q1_CLIENTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/composition/q1/types.h"

typedef void (*qa_comp_q1_actor_release_fn)(void *ctx, qa_owned_actor actor);

typedef struct qa_comp_q1_client_game {
  void *ctx;
  const char *edition; /* "classic" | "rerelease" */
  bool coop;
  bool has_max_clients;
  double max_clients; /* TS options.maxClients ?? 1 */
  bool (*resolve_owned)(void *ctx, qa_actor_id actor, qa_owned_actor *out);
  void (*assert_owned)(void *ctx, qa_owned_actor actor);
  bool (*resolve_saved)(void *ctx, int64_t slot, int64_t generation, qa_owned_actor *out);
  /* Constructor-time actors.onRelease registration. */
  void (*on_release)(void *ctx, qa_comp_q1_actor_release_fn callback, void *callback_ctx);
  void (*set_traits_team)(void *ctx, qa_owned_actor actor, const char *team);
  /* TS player(actor)?.autoSwitch = value, only when the player exists. */
  void (*set_auto_switch)(void *ctx, qa_actor_id actor, const char *value);
} qa_comp_q1_client_game;

/* TS Q1SourceClient. userinfo strings are owned copies. */
typedef struct qa_comp_q1_source_client {
  qa_owned_actor actor;
  int64_t slot;
  qa_comp_userinfo_entry *userinfo;
  size_t userinfo_count;
  size_t userinfo_capacity;
  double frags;
  double team;
  bool observer;
  bool no_target;
  bool god_mode;
  double impulse;
  bool use;
  bool death_recorded;
  double respawn_requested_at;
} qa_comp_q1_source_client;

const char *qa_comp_q1_source_client_name(const qa_comp_q1_source_client *client);
int64_t qa_comp_q1_source_client_shirt(const qa_comp_q1_source_client *client);
int64_t qa_comp_q1_source_client_pants(const qa_comp_q1_source_client *client);
/** TS snapshot getter; borrows the client's live userinfo storage. */
qa_comp_q1_client_snapshot qa_comp_q1_source_client_snapshot(const qa_comp_q1_source_client *client);

/* TS Q1SourceClients. */
typedef struct qa_comp_q1_source_clients {
  qa_comp_q1_client_game game;
  qa_comp_q1_program program;
  const qa_comp_q1_services *services;
  qa_comp_q1_source_client *records;
  size_t record_count;
  size_t record_capacity;
  double red_captures;
  double blue_captures;
} qa_comp_q1_source_clients;

void qa_comp_q1_source_clients_init(qa_comp_q1_source_clients *clients, qa_comp_q1_client_game game,
                                    qa_comp_q1_program program, const qa_comp_q1_services *services);
void qa_comp_q1_source_clients_destroy(qa_comp_q1_source_clients *clients);
qa_comp_q1_source_client *qa_comp_q1_source_clients_get(qa_comp_q1_source_clients *clients, qa_actor_id actor);
qa_comp_q1_source_client *qa_comp_q1_source_clients_require(qa_comp_q1_source_clients *clients, qa_actor_id actor);
/** NULL is OOM only; admission errors abort like the TS throws. */
qa_comp_q1_source_client *qa_comp_q1_source_clients_attach(qa_comp_q1_source_clients *clients,
                                                           qa_owned_actor actor,
                                                           const qa_comp_q1_client_admission *admission);
bool qa_comp_q1_source_clients_update(qa_comp_q1_source_clients *clients, qa_actor_id actor,
                                      const qa_comp_userinfo_entry *userinfo, size_t userinfo_count);
bool qa_comp_q1_source_clients_colors(qa_comp_q1_source_clients *clients, qa_actor_id actor,
                                      double shirt, double pants);
double qa_comp_q1_source_clients_team_color(qa_comp_q1_source_clients *clients, qa_actor_id actor);
void qa_comp_q1_source_clients_spawned(qa_comp_q1_source_clients *clients, qa_actor_id actor);
void qa_comp_q1_source_clients_add_score(qa_comp_q1_source_clients *clients, qa_actor_id actor, double delta);
void qa_comp_q1_source_clients_set_observer(qa_comp_q1_source_clients *clients, qa_actor_id actor, bool observer);
void qa_comp_q1_source_clients_publish(qa_comp_q1_source_clients *clients, const qa_comp_q1_source_client *client);
/** Malloc'd checkpoint bytes (caller frees); NULL is OOM. */
uint8_t *qa_comp_q1_source_clients_capture(qa_comp_q1_source_clients *clients, size_t *out_length);
bool qa_comp_q1_source_clients_restore(qa_comp_q1_source_clients *clients, const uint8_t *bytes, size_t length);

#endif
