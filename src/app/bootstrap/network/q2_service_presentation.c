/* Port of src/app/bootstrap/network/q2-service-presentation.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q2_service_presentation.h"
#include <string.h>
#include "qa/app/bootstrap/network/q2_rerelease_hud_events.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
static qa_app_q2_presentation_event stamped(const qa_app_q2_service_host *host,
    const char *kind, bool has_source, double source_entity) {
  qa_app_q2_presentation_event e = {0};
  e.kind = kind; e.sequence = host->next_sequence(host->ctx);
  e.content = host->content(host->ctx); e.seconds = host->seconds;
  e.has_source_entity = has_source; e.source_entity = source_entity;
  return e;
}
bool qa_app_q2_service_player_info(const qa_app_q2_service_host *host, double index, const char *value) {
  if (index < host->player_skin_config_offset || index >= host->player_skin_config_offset + 256)
    return false;
  double slot = index - host->player_skin_config_offset;
  const char *split = strchr(value, '\\');
  qa_actor_id actor;
  if (!host->actor(host->ctx, slot + 1, &actor)) return false;
  qa_app_q2_presentation_event e = stamped(host, "q2-player", true, slot + 1);
  e.event_kind = QA_APP_SVC_EV_USERINFO;
  e.has_actor = true; e.actor = actor; e.slot = slot;
  e.name = value;
  e.name_length = split ? (size_t)(split - value) : strlen(value);
  e.skin_offset = split ? (size_t)(split - value) + 1 : strlen(value);
  e.skin = value + e.skin_offset;
  host->emit(host->ctx, &e);
  return true;
}
bool qa_app_q2_service_print(const qa_app_q2_service_host *host, double level, const char *text) {
  qa_app_q2_service_player player;
  if (!host->player(host->ctx, &player)) return false;
  if (level == 4 || level == 5) {
    qa_app_q2_presentation_event e = stamped(host, "q2", true, player.source_entity);
    e.event_kind = QA_APP_SVC_EV_CENTERPRINT;
    e.has_actor = true; e.actor = player.actor; e.text = text; e.instant = level == 4;
    host->emit(host->ctx, &e);
  } else {
    qa_app_q2_presentation_event e = stamped(host, "q2-player", true, player.source_entity);
    e.event_kind = QA_APP_SVC_EV_PRINT;
    e.has_actor = true; e.actor = player.actor;
    e.level = level == 3 ? "chat" : level == 2 ? "high" : level == 1 ? "medium" : "low";
    e.text = text;
    host->emit(host->ctx, &e);
  }
  return true;
}
size_t qa_app_translate_q2_service_records(qa_q2_server_record *records, size_t count,
    const qa_app_q2_service_host *host) {
  size_t kept = 0;
  for (size_t i = 0; i < count; i++) {
    qa_q2_server_record *record = &records[i];
    if (qa_app_translate_q2_rerelease_hud_record(record, host)) continue;
    const qa_q2_server_event *event = &record->event;
    bool remain = false;
    switch (event->kind) {
      case QA_Q2_SERVER_CONFIG_STRING: {
        host->set_config_string(host->ctx, event->index, event->text);
        qa_app_q2_service_player player;
        if (host->player(host->ctx, &player))
          qa_app_q2_service_player_info(host, event->index, event->text);
        break;
      }
      case QA_Q2_SERVER_PRINT:
        if (!qa_app_q2_service_print(host, event->level, event->text)) remain = true;
        break;
      case QA_Q2_SERVER_INVENTORY:
        host->set_inventory(host->ctx, event->counts, event->count_count);
        break;
      case QA_Q2_SERVER_LAYOUT:
        host->set_layout(host->ctx, event->text);
        break;
      case QA_Q2_SERVER_TEMPORARY_ENTITY: {
        qa_app_q2_effect_event decoded;
        if (qa_app_q2_effect_from_wire(&event->temp, &decoded)) {
          qa_app_q2_presentation_event e = stamped(host, "q2", true, 0);
          e.has_source_entity = false; /* TS sourceEntity: null */
          e.event_kind = QA_APP_SVC_EV_EFFECT; e.effect = decoded;
          host->emit(host->ctx, &e);
        } else {
          qa_app_q2_beam_event beam;
          if (qa_app_q2_beam_from_wire(&event->temp, &beam)) {
            qa_app_q2_presentation_event e = stamped(host, "q2-weapon", false, 0);
            e.event_kind = QA_APP_SVC_EV_BEAM; e.beam = beam;
            host->emit(host->ctx, &e);
          } else remain = true;
        }
        break;
      }
      case QA_Q2_SERVER_SOUND: {
        const char *path = host->config_string(host->ctx,
            host->sound_config_offset + event->sound.index);
        if (path == NULL)
          qa_range_error("Q2 sound %g has no configstring", event->sound.index);
        qa_app_q2_service_entity entity; bool has_entity =
            host->entity(host->ctx, event->sound.entity, &entity);
        qa_app_q2_presentation_event e = stamped(host, "q2", true, event->sound.entity);
        e.event_kind = QA_APP_SVC_EV_SOUND;
        if (event->sound.entity != 0)
          e.has_actor = host->actor(host->ctx, event->sound.entity, &e.actor);
        e.origin = event->sound.has_position ? event->sound.position
            : has_entity ? entity.origin : (qa_vec3){0, 0, 0};
        e.path = path; e.volume = event->sound.volume;
        e.attenuation = event->sound.attenuation; e.channel = event->sound.channel;
        host->emit(host->ctx, &e);
        break;
      }
      case QA_Q2_SERVER_MUZZLE_FLASH: {
        if (event->monster) {
          qa_app_q2_service_entity entity;
          qa_actor_id actor;
          if (!host->entity(host->ctx, event->flash_entity, &entity)
              || !host->actor(host->ctx, event->flash_entity, &actor)) { remain = true; break; }
          qa_angle_vectors axes = qa_math_angle_vectors_rule(entity.angles, QA_ANGLE_RULE_NATIVE);
          qa_vec3 offset = qa_q2m_muzzle_offset(
              host->has_edition && host->edition_rerelease ? QA_Q2F_RERELEASE : QA_Q2F_CLASSIC,
              event->flash);
          qa_app_q2_presentation_event e = stamped(host, "q2", true, event->flash_entity);
          e.event_kind = QA_APP_SVC_EV_MONSTER_MUZZLEFLASH;
          e.has_actor = true; e.actor = actor; e.flash = event->flash;
          e.origin.x = entity.origin.x + axes.forward.x * offset.x + axes.right.x * offset.y;
          e.origin.y = entity.origin.y + axes.forward.y * offset.x + axes.right.y * offset.y;
          e.origin.z = entity.origin.z + axes.forward.z * offset.x + axes.right.z * offset.y + offset.z;
          e.direction = axes.forward;
          host->emit(host->ctx, &e);
        } else {
          qa_actor_id actor;
          if (!host->actor(host->ctx, event->flash_entity, &actor)) { remain = true; break; }
          qa_app_q2_presentation_event e = stamped(host, "q2-weapon", true, event->flash_entity);
          e.event_kind = QA_APP_SVC_EV_MUZZLEFLASH;
          e.has_actor = true; e.actor = actor; e.flash = event->flash;
          e.silenced = event->silenced;
          host->emit(host->ctx, &e);
        }
        break;
      }
      case QA_Q2_SERVER_CENTER_PRINT: {
        qa_app_q2_service_player player;
        if (host->player(host->ctx, &player)) {
          qa_app_q2_presentation_event e = stamped(host, "q2", true, player.source_entity);
          e.event_kind = QA_APP_SVC_EV_CENTERPRINT;
          e.has_actor = true; e.actor = player.actor; e.text = event->text;
          host->emit(host->ctx, &e);
        } else remain = true;
        break;
      }
      case QA_Q2_SERVER_FOG: {
        qa_app_q2_service_player player;
        if (!host->player(host->ctx, &player)) { remain = true; break; }
        qa_app_q2_presentation_event e = stamped(host, "q2-rerelease", true, player.source_entity);
        e.event_kind = QA_APP_SVC_EV_FOG;
        e.has_actor = true; e.actor = player.actor;
        e.fog = host->fog(host->ctx, event->fog);
        e.transition_milliseconds = ((int64_t)event->fog.bits & QA_Q2_SVC_FOG_BIT_TIME) == 0
            ? 0 : event->fog.time;
        host->emit(host->ctx, &e);
        break;
      }
      case QA_Q2_SERVER_ACHIEVEMENT: {
        qa_app_q2_presentation_event e = stamped(host, "q2-rerelease", false, 0);
        e.event_kind = QA_APP_SVC_EV_ACHIEVEMENT; e.text = event->text;
        host->emit(host->ctx, &e);
        break;
      }
      case QA_Q2_SERVER_HELP_PATH: {
        qa_app_q2_service_player player;
        if (!host->player(host->ctx, &player)) { remain = true; break; }
        qa_app_q2_presentation_event e = stamped(host, "q2-rerelease", true, player.source_entity);
        e.event_kind = QA_APP_SVC_EV_HELP_PATH;
        e.has_actor = true; e.actor = player.actor; e.first = event->help_path.start;
        e.position.x = event->help_path.pos[0]; e.position.y = event->help_path.pos[1];
        e.position.z = event->help_path.pos[2];
        e.direction.x = event->help_path.dir[0]; e.direction.y = event->help_path.dir[1];
        e.direction.z = event->help_path.dir[2];
        host->emit(host->ctx, &e);
        break;
      }
      default: remain = true; break;
    }
    if (remain) records[kept++] = *record;
  }
  return kept;
}
