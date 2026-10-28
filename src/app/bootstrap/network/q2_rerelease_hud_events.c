/* Port of src/app/bootstrap/network/q2-rerelease-hud-events.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q2_rerelease_hud_events.h"
#include <string.h>
#include "qa/foundation/support.h"
bool qa_app_translate_q2_rerelease_hud_record(const qa_q2_server_record *record,
    const qa_app_q2_service_host *host) {
  const qa_q2_server_event *event = &record->event;
  if (event->kind != QA_Q2_SERVER_POI && event->kind != QA_Q2_SERVER_DAMAGE) return false;
  qa_app_q2_service_player player;
  if (!host->player(host->ctx, &player)) return false;
  qa_app_q2_presentation_event base = {0};
  base.kind = "q2-rerelease"; base.sequence = host->next_sequence(host->ctx);
  base.content = host->content(host->ctx); base.seconds = host->seconds;
  base.has_actor = true; base.actor = player.actor;
  if (event->kind == QA_Q2_SERVER_POI) {
    const qa_q2_kex_poi *value = &event->poi;
    base.key = value->key;
    if (value->time == 65535) {
      base.event_kind = QA_APP_SVC_EV_REMOVE_POI;
      host->emit(host->ctx, &base);
    } else {
      const char *image = host->config_string(host->ctx, host->image_config_offset + value->image);
      if (image == NULL) qa_range_error("Q2 POI image %g has no configstring", value->image);
      base.event_kind = QA_APP_SVC_EV_KEYED_POI;
      base.duration = value->time;
      base.position.x = value->pos[0]; base.position.y = value->pos[1]; base.position.z = value->pos[2];
      base.image = image; base.color = value->color; base.flags = value->flags;
      host->emit(host->ctx, &base);
    }
  } else {
    for (size_t i = 0; i < event->indicator_count; i++) {
      const qa_q2_kex_damage_indicator *value = &event->indicators[i];
      qa_app_q2_presentation_event out = base;
      out.sequence = host->next_sequence(host->ctx);
      out.event_kind = QA_APP_SVC_EV_DIRECTIONAL_DAMAGE;
      out.damage = value->damage; out.health = value->health;
      out.armor = value->armor; out.shield = value->shield;
      out.direction.x = value->direction[0]; out.direction.y = value->direction[1];
      out.direction.z = value->direction[2];
      host->emit(host->ctx, &out);
    }
  }
  return true;
}
