/* Port of src/app/bootstrap/network/q2-service-presentation.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q2_service_presentation.h"
#include "qa/network/common/js_number.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/q2_effects.h"
#include "qa/app/bootstrap/network/q2_rerelease_hud_events.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
/* The TS literal's stamp, in its order: { kind, sequence: host.nextSequence(), content:
 * host.content(), seconds: host.seconds, sourceEntity }. A borrowed view; the caller sets the
 * kind's payload member and emits it. */
static qa_app_sim_presentation_event stamped(const qa_app_q2_service_host *host,
    const char *kind, bool has_source, double source_entity) {
  qa_app_sim_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = kind;
  e.sequence = host->next_sequence(host->ctx);
  e.content = host->content(host->ctx);
  e.seconds = host->seconds;
  e.has_source_entity = has_source;
  e.source_entity = source_entity;
  return e;
}
static qa_q2_presentation_event q2_event(const char *kind) {
  qa_q2_presentation_event event;
  memset(&event, 0, sizeof event);
  event.kind = kind;
  return event;
}
static qa_q2_player_event player_event(const char *kind) {
  qa_q2_player_event event;
  memset(&event, 0, sizeof event);
  event.kind = kind;
  return event;
}
static qa_q2_rerelease_event rerelease_event(const char *kind) {
  qa_q2_rerelease_event event;
  memset(&event, 0, sizeof event);
  event.kind = kind;
  return event;
}
bool qa_app_q2_service_player_info(const qa_app_q2_service_host *host, double index, const char *value) {
  if (index < host->player_skin_config_offset || index >= host->player_skin_config_offset + 256)
    return false;
  double slot = index - host->player_skin_config_offset;
  const char *split = strchr(value, '\\');
  qa_actor_id actor;
  if (!host->actor(host->ctx, slot + 1, &actor)) return false;
  qa_app_sim_presentation_event e = stamped(host, "q2-player", true, slot + 1);
  qa_q2_player_event payload = player_event("userinfo");
  payload.actor = actor;
  payload.slot = slot;
  char *name = NULL;
  if (split) {
    size_t length = (size_t)(split - value);
    name = malloc(length + 1);
    if (!name) qa_range_error("out of memory");
    memcpy(name, value, length);
    name[length] = '\0';
    payload.name = name;
    payload.skin = split + 1;
  } else {
    payload.name = value;
    payload.skin = "";
  }
  e.q2_player = &payload;
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, name);
  host->emit(host->ctx, &e);
  qa_cleanup_pop(&guard);
  free(name);
  return true;
}
bool qa_app_q2_service_print(const qa_app_q2_service_host *host, double level, const char *text) {
  qa_app_q2_service_player player;
  if (!host->player(host->ctx, &player)) return false;
  if (level == 4 || level == 5) {
    qa_app_sim_presentation_event e = stamped(host, "q2", true, player.source_entity);
    qa_q2_presentation_event payload = q2_event("centerprint");
    payload.has_actor = true; payload.actor = player.actor; payload.text = text;
    payload.has_instant = true; payload.instant = level == 4;
    e.q2 = &payload;
    host->emit(host->ctx, &e);
  } else {
    qa_app_sim_presentation_event e = stamped(host, "q2-player", true, player.source_entity);
    qa_q2_player_event payload = player_event("print");
    payload.has_target = true; payload.target = player.actor;
    payload.level = level == 3 ? "chat" : level == 2 ? "high" : level == 1 ? "medium" : "low";
    payload.text = text;
    e.q2_player = &payload;
    host->emit(host->ctx, &e);
  }
  return true;
}
typedef struct muzzle_scope { qa_q2_edition edition; double flash; qa_vec3 offset; } muzzle_scope;
static void muzzle_body(void *ctx) {
  muzzle_scope *scope = ctx;
  scope->offset = qa_q2m_muzzle_offset(scope->edition, scope->flash);
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
        qa_q2_presentation_event decoded;
        if (qa_app_q2_effect_from_wire(&event->temp, &decoded)) {
          qa_app_sim_presentation_event e = stamped(host, "q2", false, 0); /* TS sourceEntity: null */
          e.q2 = &decoded;
          host->emit(host->ctx, &e);
        } else {
          qa_q2w_event beam;
          if (qa_app_q2_beam_from_wire(&event->temp, &beam)) {
            qa_app_sim_presentation_event e = stamped(host, "q2-weapon", false, 0);
            e.q2_weapon = &beam;
            host->emit(host->ctx, &e);
          } else remain = true;
        }
        break;
      }
      case QA_Q2_SERVER_SOUND: {
        const char *path = host->config_string(host->ctx,
            host->sound_config_offset + event->sound.index);
        /* cl_parse.c returns when the precache slot is empty; gi.soundindex("") yields index 0. */
        if (path == NULL) break;
        qa_app_q2_service_entity entity; bool has_entity =
            host->entity(host->ctx, event->sound.entity, &entity);
        qa_app_sim_presentation_event e = stamped(host, "q2", true, event->sound.entity);
        qa_q2_presentation_event payload = q2_event("sound");
        if (event->sound.entity != 0)
          payload.has_actor = host->actor(host->ctx, event->sound.entity, &payload.actor);
        payload.origin = event->sound.has_position ? event->sound.position
            : has_entity ? entity.origin : (qa_vec3){0, 0, 0};
        payload.path = path; payload.volume = event->sound.volume;
        payload.attenuation = event->sound.attenuation; payload.channel = event->sound.channel;
        payload.reliable = false; payload.loop = "once";
        e.q2 = &payload;
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
          /* try { muzzleOffset(...) } catch { continue; }: a flash number outside the table is dropped. */
          muzzle_scope muzzle = { host->has_edition && host->edition_rerelease ? QA_Q2_EDITION_RERELEASE : QA_Q2_EDITION_CLASSIC,
              event->flash, {0, 0, 0} };
          qa_caught_error dropped;
          if (!qa_try(muzzle_body, &muzzle, &dropped)) break;
          qa_vec3 offset = muzzle.offset;
          qa_app_sim_presentation_event e = stamped(host, "q2", true, event->flash_entity);
          qa_q2_presentation_event payload = q2_event("monster-muzzleflash");
          payload.has_actor = true; payload.actor = actor; payload.flash = event->flash;
          payload.origin.x = entity.origin.x + axes.forward.x * offset.x + axes.right.x * offset.y;
          payload.origin.y = entity.origin.y + axes.forward.y * offset.x + axes.right.y * offset.y;
          payload.origin.z = entity.origin.z + axes.forward.z * offset.x + axes.right.z * offset.y + offset.z;
          payload.direction = axes.forward;
          e.q2 = &payload;
          host->emit(host->ctx, &e);
        } else {
          qa_actor_id actor;
          if (!host->actor(host->ctx, event->flash_entity, &actor)) { remain = true; break; }
          qa_app_sim_presentation_event e = stamped(host, "q2-weapon", true, event->flash_entity);
          qa_q2w_event payload;
          memset(&payload, 0, sizeof payload);
          payload.kind = QA_Q2W_EVENT_MUZZLEFLASH;
          payload.actor = &actor; payload.flash = event->flash; payload.silenced = event->silenced;
          e.q2_weapon = &payload;
          host->emit(host->ctx, &e);
        }
        break;
      }
      case QA_Q2_SERVER_CENTER_PRINT: {
        qa_app_q2_service_player player;
        if (host->player(host->ctx, &player)) {
          qa_app_sim_presentation_event e = stamped(host, "q2", true, player.source_entity);
          qa_q2_presentation_event payload = q2_event("centerprint");
          payload.has_actor = true; payload.actor = player.actor; payload.text = event->text;
          e.q2 = &payload;
          host->emit(host->ctx, &e);
        } else remain = true;
        break;
      }
      case QA_Q2_SERVER_FOG: {
        qa_app_q2_service_player player;
        if (!host->player(host->ctx, &player)) { remain = true; break; }
        qa_app_sim_presentation_event e = stamped(host, "q2-rerelease", true, player.source_entity);
        qa_q2_rerelease_event payload = rerelease_event("fog");
        payload.has_actor = true; payload.actor = player.actor;
        payload.fog_value = host->fog(host->ctx, event->fog);
        payload.transition_milliseconds = (qa_js_to_int32(event->fog.bits) & QA_Q2_SVC_FOG_BIT_TIME) == 0
            ? 0 : event->fog.time;
        e.q2_rerelease = &payload;
        host->emit(host->ctx, &e);
        break;
      }
      case QA_Q2_SERVER_ACHIEVEMENT: {
        qa_app_sim_presentation_event e = stamped(host, "q2-rerelease", false, 0);
        qa_q2_rerelease_event payload = rerelease_event("achievement");
        payload.id = event->text;
        e.q2_rerelease = &payload;
        host->emit(host->ctx, &e);
        break;
      }
      case QA_Q2_SERVER_HELP_PATH: {
        qa_app_q2_service_player player;
        if (!host->player(host->ctx, &player)) { remain = true; break; }
        qa_app_sim_presentation_event e = stamped(host, "q2-rerelease", true, player.source_entity);
        qa_q2_rerelease_event payload = rerelease_event("help-path");
        payload.has_actor = true; payload.actor = player.actor; payload.first = event->help_path.start;
        payload.position.x = event->help_path.pos[0]; payload.position.y = event->help_path.pos[1];
        payload.position.z = event->help_path.pos[2];
        payload.direction.x = event->help_path.dir[0]; payload.direction.y = event->help_path.dir[1];
        payload.direction.z = event->help_path.dir[2];
        e.q2_rerelease = &payload;
        host->emit(host->ctx, &e);
        break;
      }
      default: remain = true; break;
    }
    if (remain) records[kept++] = *record;
  }
  return kept;
}
