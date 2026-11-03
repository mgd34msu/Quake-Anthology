/* Port of src/app/bootstrap/network/q2-service-presentation.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q2_SERVICE_PRESENTATION_H
#define QA_APP_BOOTSTRAP_NETWORK_Q2_SERVICE_PRESENTATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/network/q2/fog.h"
#include "qa/network/q2/server_messages.h"
typedef struct qa_app_q2_service_player {
  qa_actor_id actor; double source_entity;
} qa_app_q2_service_player;
typedef struct qa_app_q2_service_entity { qa_vec3 origin, angles; } qa_app_q2_service_entity;
/* TS Q2ServicePresentationHost. player/actor/entity return false for TS null. */
typedef struct qa_app_q2_service_host {
  void *ctx;
  bool has_edition; bool edition_rerelease;
  double seconds;
  double sound_config_offset, image_config_offset, player_skin_config_offset;
  qa_content_id (*content)(void *ctx);
  double (*next_sequence)(void *ctx);
  bool (*player)(void *ctx, qa_app_q2_service_player *out);
  bool (*actor)(void *ctx, double source_entity, qa_actor_id *out);
  bool (*entity)(void *ctx, double source_entity, qa_app_q2_service_entity *out);
  qa_q2_fog_state (*fog)(void *ctx, qa_q2_svc_fog_data value);
  const char *(*config_string)(void *ctx, double index); /* NULL = undefined */
  void (*set_config_string)(void *ctx, double index, const char *value);
  void (*set_inventory)(void *ctx, const double *counts, size_t count);
  void (*set_layout)(void *ctx, const char *text);
  /* event is a borrowed view (storage NULL) valid for the call; a host that
   * keeps it stores qa_app_sim_presentation_event_copy. */
  void (*emit)(void *ctx, const qa_app_sim_presentation_event *event);
} qa_app_q2_service_host;
bool qa_app_q2_service_player_info(const qa_app_q2_service_host *host, double index, const char *value);
/* Remaining (untranslated) records are compacted to the front of
 * `records`; returns their count. */
size_t qa_app_translate_q2_service_records(qa_q2_server_record *records, size_t count,
    const qa_app_q2_service_host *host);
bool qa_app_q2_service_print(const qa_app_q2_service_host *host, double level, const char *text);
#endif
