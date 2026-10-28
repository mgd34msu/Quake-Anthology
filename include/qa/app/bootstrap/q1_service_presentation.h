/* Port of src/app/bootstrap/q1-service-presentation.ts. The
 * unlanded SimulationPresentationEvent variants this file reads
 * ride the view below. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q1_SERVICE_PRESENTATION_H
#define QA_APP_BOOTSTRAP_Q1_SERVICE_PRESENTATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/presentation.h"
#include "qa/contracts/render.h"
#include "qa/render/scene/q2_sky.h"
/* TS Q1ClientRow. Optional fields use has_ flags; strings borrowed
 * from the owning presentation until reset/close. */
typedef struct qa_app_q1_client_row {
  double slot;
  const char *name;
  double colors, frags;
  bool has_ping; double ping;
  const char *social;      /* NULL = absent */
  const char *player_info; /* NULL = absent */
} qa_app_q1_client_row;
/* TS Q1ClientMetadataEvent union on kind. */
typedef struct qa_app_q1_client_metadata_event {
  const char *kind; /* name|colors|frags|ping|social|player-info */
  double slot;
  const char *text_value;  /* name/social/player-info */
  double number_value;     /* colors/frags/ping */
} qa_app_q1_client_metadata_event;
/* Minimal SimulationPresentationEvent view. */
typedef struct qa_app_q1_service_event {
  const char *kind;       /* presentation-owner|q1-sky|q1-client|other */
  const char *event_kind; /* refreshed|retired|... */
  bool has_owner;
  qa_presentation_owner owner;
  bool has_recipient;
  qa_actor_id recipient;
  qa_content_id content;
  const char *sky_name;   /* q1-sky */
  qa_app_q1_client_metadata_event client; /* q1-client */
  double sequence;
} qa_app_q1_service_event;
/* Mutable row table for updateQ1ClientMetadata (owned). */
typedef struct qa_app_q1_client_table qa_app_q1_client_table;
qa_app_q1_client_table *qa_app_q1_client_table_create(void);
void qa_app_q1_client_table_destroy(qa_app_q1_client_table *table);
void qa_app_update_q1_client_metadata(qa_app_q1_client_table *table, qa_app_q1_client_metadata_event event);
size_t qa_app_q1_client_table_rows(const qa_app_q1_client_table *table, const qa_app_q1_client_row **out);
/* TS Q1ServiceAssets: texture loads per content. */
typedef struct qa_app_q1_service_assets {
  void *ctx;
  /* textures.load(path,{family:"q1",wrap:"clamp",mipmap:false,usage:"sky"}):
   * false = TS null. */
  bool (*load_texture)(void *ctx, qa_content_id content, const char *path, qa_renderer_image *image_out);
  qa_renderer_image (*missing_image)(void *ctx, qa_content_id content);
} qa_app_q1_service_assets;
typedef struct qa_app_q1_service_presentation qa_app_q1_service_presentation;
qa_app_q1_service_presentation *qa_app_q1_service_presentation_create(void);
void qa_app_q1_service_presentation_destroy(qa_app_q1_service_presentation *presentation);
/* Aborts when closed (TS Error). */
void qa_app_q1_service_presentation_receive(qa_app_q1_service_presentation *presentation,
    const qa_app_q1_service_event *events, size_t event_count);
void qa_app_q1_service_presentation_prepare(qa_app_q1_service_presentation *presentation,
    const qa_app_q1_service_assets *assets);
typedef struct qa_app_q1_sky_refresh qa_app_q1_sky_refresh;
qa_app_q1_sky_refresh *qa_app_q1_service_presentation_prepare_image_refresh(
    qa_app_q1_service_presentation *presentation, const qa_app_q1_service_assets *assets);
void qa_app_q1_sky_refresh_apply(qa_app_q1_sky_refresh *refresh);
void qa_app_q1_sky_refresh_destroy(qa_app_q1_sky_refresh *refresh);
/* Borrowed source sky or NULL. */
const qa_q2_sky_view *qa_app_q1_service_presentation_view(qa_app_q1_service_presentation *presentation,
    qa_actor_id actor);
/* Sorted borrowed rows written into caller storage; returns count. */
size_t qa_app_q1_service_presentation_clients(qa_app_q1_service_presentation *presentation,
    qa_content_id content, qa_actor_id actor, qa_app_q1_client_row *out, size_t capacity);
void qa_app_q1_service_presentation_retire(qa_app_q1_service_presentation *presentation, qa_actor_id actor);
void qa_app_q1_service_presentation_reset(qa_app_q1_service_presentation *presentation);
void qa_app_q1_service_presentation_close(qa_app_q1_service_presentation *presentation);
#endif
