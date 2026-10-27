/*
 * Port of src/world/session/mod-clients.ts (types only): destination
 * client identities, ordered input applications, and the client
 * services interface (fn table + ctx). Optional TS members are
 * nullable fn ptrs / has_* flags. The application struct is the
 * mutable working form of the TS getter-backed object; its command,
 * aim, arsenal, and impulse fields are updated by input outputs.
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SESSION_MOD_CLIENTS_H
#define QA_WORLD_SESSION_MOD_CLIENTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/mod_callbacks.h"
#include "qa/contracts/mod_client_outputs.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/session.h"
#include "qa/contracts/time.h"
#include "qa/world/session/mod_client_outputs.h"

typedef struct qa_mod_client_identity {
  qa_client_id client;
  qa_actor_id actor;
} qa_mod_client_identity;

/* kind: "admitted" | "userinfo" | "disconnecting" (exact strings). */
typedef struct qa_mod_client_event {
  const char *kind;
  qa_mod_client_identity identity;
} qa_mod_client_event;

/** TS ModClientCommand (an accepted receipt). */
typedef struct qa_mod_client_command_receipt {
  qa_actor_command input;
  qa_source_time time;
} qa_mod_client_command_receipt;

/** TS ModClientApplication. accepted is borrowed (nullable);
 * arsenal is an owned copy when has_arsenal. scope: "client-command"
 * | "movement-slice"; angle_space: "absolute" | "source-relative". */
typedef struct qa_mod_client_application {
  qa_mod_client_identity identity;
  int64_t invocation;
  bool has_parent_invocation;
  int64_t parent_invocation;
  const char *scope;
  qa_user_command command;
  const char *angle_space;
  qa_vec3 absolute_aim;
  qa_frame_context frame;
  const qa_mod_client_command_receipt *accepted; /* nullable */
  bool has_arsenal;
  qa_arsenal_intent arsenal;
  double controls_impulse;
} qa_mod_client_application;

/* Input to ModClientApplications.begin: the application minus its
 * assigned invocation. has_arsenal false + arsenal NULL is the TS
 * undefined; has_arsenal true + arsenal NULL is the TS null.
 * has_parent_invocation false is the TS undefined/null parent. */
typedef struct qa_mod_client_application_input {
  qa_mod_client_identity identity;
  bool has_parent_invocation;
  int64_t parent_invocation;
  const char *scope;
  qa_user_command command;
  const char *angle_space;
  qa_vec3 absolute_aim;
  qa_frame_context frame;
  const qa_mod_client_command_receipt *accepted; /* nullable */
  bool has_arsenal;
  const qa_arsenal_intent *arsenal; /* nullable */
  bool has_controls;
  double controls_impulse;
} qa_mod_client_application_input;

/** TS ModClientApplicationEvent. For phase "before", output/output_ctx
 * form the output capability (valid only inside the live callback);
 * outcome is NULL. For "after", outcome is "completed" |
 * "actor-removed" | "failed". */
typedef struct qa_mod_client_application_event {
  bool is_before;
  qa_mod_client_application *application;
  const char *outcome; /* after only */
  void (*output)(void *output_ctx, qa_mod_client_input_output value);
  void *output_ctx;
} qa_mod_client_application_event;

typedef void (*qa_mod_client_listener_fn)(void *ctx,
                                          qa_mod_client_event event);
typedef void (*qa_mod_client_application_listener_fn)(
    void *ctx, qa_mod_client_application_event *event);

/* A TS `() => undefined` unsubscribe closure. */
typedef struct qa_mod_client_unsubscribe {
  void *ctx;
  void (*invoke)(void *ctx);
} qa_mod_client_unsubscribe;

typedef struct qa_mod_client_player_view {
  qa_vec3 view_offset;
  bool crouched;
} qa_mod_client_player_view;

/** TS ModClientServices. claim_outputs / grounded / player_view may
 * be NULL (TS optional). clients writes a borrowed array. */
typedef struct qa_mod_client_services {
  void *ctx;
  double maximum;
  qa_mod_client_output_lease (*claim_outputs)(
      void *ctx, qa_provider_id owner,
      const qa_mod_client_output_channel *channels, size_t channel_count);
  const qa_mod_client_identity *(*clients)(void *ctx, size_t *out_count);
  bool (*for_actor)(void *ctx, qa_actor_id actor, qa_client_id *out);
  bool (*actor)(void *ctx, qa_client_id client, qa_actor_id *out);
  const char *(*userinfo)(void *ctx, qa_client_id client);
  void (*set_userinfo)(void *ctx, qa_client_id client, const char *value);
  const qa_mod_client_command_receipt *(*command)(void *ctx,
                                                  qa_client_id client);
  bool (*grounded)(void *ctx, qa_client_id client);
  qa_mod_client_player_view (*player_view)(void *ctx, qa_client_id client);
  void (*drop)(void *ctx, qa_client_id client, const char *reason,
               qa_content_id content);
  qa_mod_client_unsubscribe (*subscribe)(
      void *ctx, qa_mod_client_listener_fn listener, void *listener_ctx);
  qa_mod_client_unsubscribe (*subscribe_application)(
      void *ctx, qa_mod_client_application_listener_fn listener,
      void *listener_ctx);
} qa_mod_client_services;

#endif
