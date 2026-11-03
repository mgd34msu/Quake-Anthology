/* Port of src/app/bootstrap/network/q2-rerelease-hud-events.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q2_rerelease_hud_events.h"
#include <string.h>
#include "qa/content/q2/rerelease/types.h"
bool qa_app_translate_q2_rerelease_hud_record(const qa_q2_server_record *record,
    const qa_app_q2_service_host *host) {
  const qa_q2_server_event *event = &record->event;
  if (event->kind != QA_Q2_SERVER_POI && event->kind != QA_Q2_SERVER_DAMAGE) return false;
  qa_app_q2_service_player player;
  if (!host->player(host->ctx, &player)) return false;
  /* const stamp = { kind: 'q2-rerelease', sequence: host.nextSequence(), content: host.content(),
   * seconds: host.seconds }: evaluated before either branch. */
  qa_app_sim_presentation_event stamp;
  memset(&stamp, 0, sizeof stamp);
  stamp.kind = "q2-rerelease";
  stamp.sequence = host->next_sequence(host->ctx);
  stamp.content = host->content(host->ctx);
  stamp.seconds = host->seconds;
  qa_q2_rerelease_event payload;
  memset(&payload, 0, sizeof payload);
  payload.has_actor = true;
  payload.actor = player.actor;
  if (event->kind == QA_Q2_SERVER_POI) {
    const qa_q2_kex_poi *value = &event->poi;
    payload.key = value->key;
    if (value->time == 65535) {
      payload.kind = "remove-poi";
      stamp.q2_rerelease = &payload;
      host->emit(host->ctx, &stamp);
    } else {
      const char *image = host->config_string(host->ctx, host->image_config_offset + value->image);
      if (image == NULL) return true;
      payload.kind = "keyed-poi";
      payload.duration = value->time;
      payload.position.x = value->pos[0]; payload.position.y = value->pos[1]; payload.position.z = value->pos[2];
      payload.image = image; payload.color = value->color; payload.flags = value->flags;
      stamp.q2_rerelease = &payload;
      host->emit(host->ctx, &stamp);
    }
  } else {
    for (size_t i = 0; i < event->indicator_count; i++) {
      const qa_q2_kex_damage_indicator *value = &event->indicators[i];
      /* { ...stamp, sequence: host.nextSequence(), event: {...} } */
      qa_app_sim_presentation_event out = stamp;
      out.sequence = host->next_sequence(host->ctx);
      qa_q2_rerelease_event damage = payload;
      damage.kind = "directional-damage";
      damage.damage = value->damage; damage.health = value->health;
      damage.armor = value->armor; damage.shield = value->shield;
      damage.direction.x = value->direction[0]; damage.direction.y = value->direction[1];
      damage.direction.z = value->direction[2];
      out.q2_rerelease = &damage;
      host->emit(host->ctx, &out);
    }
  }
  return true;
}
