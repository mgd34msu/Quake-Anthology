/* Port of src/app/bootstrap/mod-presentations.ts. Coordinates one
 * ApplicationModPresentation per prepared gameplay mod; per-entry
 * host construction rides the factory below. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_MOD_PRESENTATIONS_H
#define QA_APP_BOOTSTRAP_MOD_PRESENTATIONS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/mod_presentation.h"
#include "qa/contracts/common.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/presentation.h"
/* TS ComponentClientCommandTarget. instance is an opaque identity. */
typedef struct qa_app_component_client_command_target {
  qa_presentation_owner owner;
  const void *instance;
  qa_seat_id seat;
  qa_actor_id viewer;
} qa_app_component_client_command_target;
/* TS ComponentClientCommandRequest. */
typedef struct qa_app_component_client_command_request {
  const char *target; /* "component-client" */
  qa_app_component_client_command_target consumer;
  const char *mode;   /* "console"|"reliable" */
  const char *name;
  const char *const *arguments; size_t argument_count;
  qa_seat_id seat;
  qa_command_context source;
} qa_app_component_client_command_request;
/* TS ApplicationModPresentationsOptions factory surface. */
typedef struct qa_app_mod_presentations_factory {
  void *ctx;
  qa_app_application_mod_presentation *(*create_presentation)(void *ctx, qa_provider_id provider, qa_seat_id seat);
  void (*queue_command)(void *ctx, qa_app_component_client_command_request request); /* may be NULL */
} qa_app_mod_presentations_factory;
typedef struct qa_app_application_mod_presentations qa_app_application_mod_presentations;
qa_app_application_mod_presentations *qa_app_application_mod_presentations_create(
    qa_app_mod_presentations_factory factory);
const char *qa_app_application_mod_presentations_add(qa_app_application_mod_presentations *presentations,
    qa_provider_id provider, qa_seat_id seat, qa_actor_id viewer, const void *instance);
qa_app_application_mod_presentation *qa_app_application_mod_presentations_find(
    qa_app_application_mod_presentations *presentations, qa_provider_id provider, qa_seat_id seat);
const char *qa_app_application_mod_presentations_frame(qa_app_application_mod_presentations *presentations, double sequence);
const char *qa_app_application_mod_presentations_receive(qa_app_application_mod_presentations *presentations,
    const void *events, size_t count);
void qa_app_application_mod_presentations_retire(qa_app_application_mod_presentations *presentations, qa_provider_id provider);
void qa_app_application_mod_presentations_close(qa_app_application_mod_presentations *presentations);
#endif
