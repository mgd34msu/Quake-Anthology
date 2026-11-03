/* Port of src/content/q3/base/shared/player-state.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

/* Minimal JS String(number): integral below 1e21 as integer text. */
static void num_str(double v, char *buf, size_t cap) {
  qa_net_js_number(v, buf, cap);
}
/* JS (x)|0 and Int32Array store conversion (ToInt32). */
static int32_t to_int32(double v) {
  if (!isfinite(v)) return 0;
  double m = fmod(trunc(v), 4294967296.0);
  if (m >= 2147483648.0) m -= 4294967296.0;
  if (m < -2147483648.0) m += 4294967296.0;
  return qa_js_to_int32(m);
}

bool qa_q3_base_player_state_slots_init(qa_q3_base_player_state_slots *slots, double length,
    int32_t *source_values, size_t source_length, bool has_source,
    const qa_q3_base_player_slot_binding *binding,
    void *stored_ctx, void (*stored)(void *ctx, double index, double value)) {
  memset(slots, 0, sizeof *slots);
  slots->length = length;
  if (has_source && (double)source_length != length) {
    char lb[32], sb[32];
    num_str(length, lb, sizeof lb); num_str((double)source_length, sb, sizeof sb);
    qa_range_error("Player state source slots require %s values, got %s", lb, sb);
  }
  if (has_source) { slots->values = source_values; slots->owns_values = false; }
  else {
    size_t n = length > 0 ? (size_t)length : 0;
    slots->values = calloc(n ? n : 1, sizeof(int32_t));
    if (!slots->values) return false;
    slots->owns_values = true;
  }
  if (binding) { slots->has_binding = true; slots->binding = *binding; }
  slots->stored_ctx = stored_ctx; slots->stored = stored;
  return true;
}
void qa_q3_base_player_state_slots_destroy(qa_q3_base_player_state_slots *slots) {
  if (!slots) return;
  if (slots->owns_values) free(slots->values);
  slots->values = NULL; slots->owns_values = false;
}
static noreturn void slot_outside(const qa_q3_base_player_state_slots *slots, double index) {
  char ib[32], lb[32];
  num_str(index, ib, sizeof ib); num_str(slots->length, lb, sizeof lb);
  qa_range_error("Player state slot %s outside %s", ib, lb);
}
double qa_q3_base_player_state_slots_get(const qa_q3_base_player_state_slots *slots, double index) {
  if (slots->has_binding) {
    int32_t value;
    if (!slots->binding.read(slots->binding.ctx, index, &value)) slot_outside(slots, index);
    return (double)value;
  }
  /* Int32Array load: non-integer or out-of-range index is undefined. */
  if (!(index == trunc(index)) || index < 0 || index >= slots->length) slot_outside(slots, index);
  return (double)slots->values[(size_t)index];
}
void qa_q3_base_player_state_slots_set(qa_q3_base_player_state_slots *slots, double index, double value) {
  if (!(index == trunc(index)) || index < 0 || index >= slots->length) slot_outside(slots, index);
  if (!slots->has_binding) slots->values[(size_t)index] = to_int32(value);
  else slots->binding.write(slots->binding.ctx, index, to_int32(value));
  if (slots->stored) slots->stored(slots->stored_ctx, index, qa_q3_base_player_state_slots_get(slots, index));
}
int32_t *qa_q3_base_player_state_slots_copy(const qa_q3_base_player_state_slots *slots) {
  size_t n = slots->length > 0 ? (size_t)slots->length : 0;
  int32_t *out = malloc((n ? n : 1) * sizeof(int32_t));
  if (!out) return NULL;
  for (size_t i = 0; i < n; i++) out[i] = to_int32(qa_q3_base_player_state_slots_get(slots, (double)i));
  return out;
}

static const char *const EVENT_NAMES[] = {
  "EV_NONE", "EV_FOOTSTEP", "EV_FOOTSTEP_METAL", "EV_FOOTSPLASH", "EV_FOOTWADE", "EV_SWIM",
  "EV_STEP_4", "EV_STEP_8", "EV_STEP_12", "EV_STEP_16", "EV_FALL_SHORT", "EV_FALL_MEDIUM", "EV_FALL_FAR",
  "EV_JUMP_PAD", "EV_JUMP", "EV_WATER_TOUCH", "EV_WATER_LEAVE", "EV_WATER_UNDER", "EV_WATER_CLEAR",
  "EV_ITEM_PICKUP", "EV_GLOBAL_ITEM_PICKUP", "EV_NOAMMO", "EV_CHANGE_WEAPON", "EV_FIRE_WEAPON",
  "EV_USE_ITEM0", "EV_USE_ITEM1", "EV_USE_ITEM2", "EV_USE_ITEM3", "EV_USE_ITEM4", "EV_USE_ITEM5",
  "EV_USE_ITEM6", "EV_USE_ITEM7", "EV_USE_ITEM8", "EV_USE_ITEM9", "EV_USE_ITEM10", "EV_USE_ITEM11",
  "EV_USE_ITEM12", "EV_USE_ITEM13", "EV_USE_ITEM14", "EV_USE_ITEM15", "EV_ITEM_RESPAWN", "EV_ITEM_POP",
  "EV_PLAYER_TELEPORT_IN", "EV_PLAYER_TELEPORT_OUT", "EV_GRENADE_BOUNCE", "EV_GENERAL_SOUND",
  "EV_GLOBAL_SOUND", "EV_GLOBAL_TEAM_SOUND", "EV_BULLET_HIT_FLESH", "EV_BULLET_HIT_WALL", "EV_MISSILE_HIT",
  "EV_MISSILE_MISS", "EV_MISSILE_MISS_METAL", "EV_RAILTRAIL", "EV_SHOTGUN", "EV_BULLET", "EV_PAIN",
  "EV_DEATH1", "EV_DEATH2", "EV_DEATH3", "EV_OBITUARY", "EV_POWERUP_QUAD", "EV_POWERUP_BATTLESUIT",
  "EV_POWERUP_REGEN", "EV_GIB_PLAYER", "EV_SCOREPLUM", "EV_PROXIMITY_MINE_STICK", "EV_PROXIMITY_MINE_TRIGGER",
  "EV_KAMIKAZE", "EV_OBELISKEXPLODE", "EV_INVUL_IMPACT", "EV_JUICED", "EV_LIGHTNINGBOLT", "EV_DEBUG_LINE",
  "EV_STOPLOOPINGSOUND", "EV_TAUNT"
};
enum { EVENT_NAME_COUNT = sizeof EVENT_NAMES / sizeof EVENT_NAMES[0] };

static qa_vec3 vzero(void) { qa_vec3 v = {0, 0, 0}; return v; }
bool qa_q3_base_player_state_record_init(qa_q3_base_player_state_record *ps, qa_q3_base_product product,
    double pm_type, double weapon, double weapon_state, const qa_q3_base_player_authority_binding *authority) {
  memset(ps, 0, sizeof *ps);
  ps->origin_value = qa_math_vec3(0, 0, 0);
  ps->velocity_value = qa_math_vec3(0, 0, 0);
  ps->delta_angles = qa_math_vec3(0, 0, 0);
  ps->grapple_point = qa_math_vec3(0, 0, 0);
  ps->viewangles = qa_math_vec3(0, 0, 0);
  ps->state_product = product;
  ps->authority = authority;
  if (!qa_q3_base_player_state_slots_init(&ps->events, 2, NULL, 0, false, NULL, NULL, NULL)) return false;
  if (!qa_q3_base_player_state_slots_init(&ps->event_parms, 2, NULL, 0, false, NULL, NULL, NULL)) return false;
  if (!qa_q3_base_player_state_slots_init(&ps->stats, 16, NULL, 0, false,
        authority ? &authority->stats : NULL, NULL, NULL)) return false;
  if (!qa_q3_base_player_state_slots_init(&ps->persistant, 16, NULL, 0, false, NULL, NULL, NULL)) return false;
  if (!qa_q3_base_player_state_slots_init(&ps->powerups, 16, NULL, 0, false, NULL, NULL, NULL)) return false;
  if (!qa_q3_base_player_state_slots_init(&ps->ammo, 16, NULL, 0, false,
        authority ? &authority->ammo : NULL, NULL, NULL)) return false;
  ps->pm_type = pm_type; ps->weapon = weapon; ps->weapon_state = weapon_state;
  (void)vzero;
  return true;
}
bool qa_q3_base_player_state_init(qa_q3_base_player_state *ps, qa_q3_base_product product,
    const qa_q3_base_player_authority_binding *authority) {
  return qa_q3_base_player_state_record_init(ps, product, QA_Q3_PM_NORMAL, QA_Q3_WP_NONE, QA_Q3_WEAPON_READY, authority);
}
void qa_q3_base_player_state_record_destroy(qa_q3_base_player_state_record *ps) {
  if (!ps) return;
  qa_q3_base_player_state_slots_destroy(&ps->events);
  qa_q3_base_player_state_slots_destroy(&ps->event_parms);
  qa_q3_base_player_state_slots_destroy(&ps->stats);
  qa_q3_base_player_state_slots_destroy(&ps->persistant);
  qa_q3_base_player_state_slots_destroy(&ps->powerups);
  qa_q3_base_player_state_slots_destroy(&ps->ammo);
}
qa_q3_base_product qa_q3_base_player_state_record_product(const qa_q3_base_player_state_record *ps) {
  return ps->state_product;
}
qa_vec3 qa_q3_base_player_state_record_origin(const qa_q3_base_player_state_record *ps) {
  return ps->authority ? ps->authority->origin(ps->authority->ctx) : ps->origin_value;
}
void qa_q3_base_player_state_record_set_origin(qa_q3_base_player_state_record *ps, qa_vec3 value) {
  if (!ps->authority) ps->origin_value = value; else ps->authority->set_origin(ps->authority->ctx, value);
}
qa_vec3 qa_q3_base_player_state_record_velocity(const qa_q3_base_player_state_record *ps) {
  return ps->authority ? ps->authority->velocity(ps->authority->ctx) : ps->velocity_value;
}
void qa_q3_base_player_state_record_set_velocity(qa_q3_base_player_state_record *ps, qa_vec3 value) {
  if (!ps->authority) ps->velocity_value = value; else ps->authority->set_velocity(ps->authority->ctx, value);
}
double qa_q3_base_player_state_record_health(const qa_q3_base_player_state_record *ps) {
  return qa_q3_base_player_state_slots_get(&ps->stats, qa_q3_base_stat_schema_for(ps->state_product)->health);
}
void qa_q3_base_player_state_record_set_health(qa_q3_base_player_state_record *ps, double value) {
  qa_q3_base_player_state_slots_set(&ps->stats, qa_q3_base_stat_schema_for(ps->state_product)->health, value);
}
static void copy_slots(qa_q3_base_player_state_slots *target, const qa_q3_base_player_state_slots *source) {
  for (double i = 0; i < target->length; i++)
    qa_q3_base_player_state_slots_set(target, i, qa_q3_base_player_state_slots_get(source, i));
}
void qa_q3_base_player_state_record_copy_from(qa_q3_base_player_state_record *ps,
    const qa_q3_base_player_state_record *source, qa_q3_base_copy_stores stores) {
  bool preserve = ps->authority != NULL && stores == QA_Q3_BASE_PRESERVE_AUTHORITY;
  ps->state_product = source->state_product;
  ps->command_time = source->command_time;
  ps->pm_type = source->pm_type;
  ps->bob_cycle = source->bob_cycle;
  ps->pm_flags = source->pm_flags;
  ps->pm_time = source->pm_time;
  if (!preserve) {
    qa_vec3 o = qa_q3_base_player_state_record_origin(source);
    qa_vec3 v = qa_q3_base_player_state_record_velocity(source);
    qa_vec3 oc = {o.x, o.y, o.z}, vc = {v.x, v.y, v.z};
    qa_q3_base_player_state_record_set_origin(ps, oc);
    qa_q3_base_player_state_record_set_velocity(ps, vc);
  }
  ps->weapon_time = source->weapon_time;
  ps->gravity = source->gravity;
  ps->speed = source->speed;
  ps->delta_angles = (qa_vec3){source->delta_angles.x, source->delta_angles.y, source->delta_angles.z};
  ps->ground_entity_num = source->ground_entity_num;
  ps->legs_timer = source->legs_timer;
  ps->legs_anim = source->legs_anim;
  ps->torso_timer = source->torso_timer;
  ps->torso_anim = source->torso_anim;
  ps->movement_dir = source->movement_dir;
  ps->grapple_point = (qa_vec3){source->grapple_point.x, source->grapple_point.y, source->grapple_point.z};
  ps->e_flags = source->e_flags;
  ps->event_sequence = source->event_sequence;
  copy_slots(&ps->events, &source->events);
  copy_slots(&ps->event_parms, &source->event_parms);
  ps->external_event = source->external_event;
  ps->external_event_parm = source->external_event_parm;
  ps->external_event_time = source->external_event_time;
  ps->client_num = source->client_num;
  ps->weapon = source->weapon;
  ps->weapon_state = source->weapon_state;
  ps->viewangles = (qa_vec3){source->viewangles.x, source->viewangles.y, source->viewangles.z};
  ps->viewheight = source->viewheight;
  ps->damage_event = source->damage_event;
  ps->damage_yaw = source->damage_yaw;
  ps->damage_pitch = source->damage_pitch;
  ps->damage_count = source->damage_count;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(ps->state_product);
  for (double index = 0; index < ps->stats.length; index++) {
    if (preserve && (index == schema->health || index == schema->armor || index == schema->weapons)) continue;
    qa_q3_base_player_state_slots_set(&ps->stats, index, qa_q3_base_player_state_slots_get(&source->stats, index));
  }
  copy_slots(&ps->persistant, &source->persistant);
  copy_slots(&ps->powerups, &source->powerups);
  if (!preserve) copy_slots(&ps->ammo, &source->ammo);
  ps->generic1 = source->generic1;
  ps->loop_sound = source->loop_sound;
  ps->jumppad_ent = source->jumppad_ent;
  ps->ping = source->ping;
  ps->pmove_framecount = source->pmove_framecount;
  ps->jumppad_frame = source->jumppad_frame;
  ps->entity_event_sequence = source->entity_event_sequence;
}
bool qa_q3_base_player_state_record_copy(qa_q3_base_player_state_record *out, const qa_q3_base_player_state_record *ps) {
  if (!qa_q3_base_player_state_record_init(out, ps->state_product, ps->pm_type, ps->weapon, ps->weapon_state, NULL))
    return false;
  qa_q3_base_player_state_record_copy_from(out, ps, QA_Q3_BASE_PRESERVE_AUTHORITY);
  return true;
}
bool qa_q3_base_player_state_copy(qa_q3_base_player_state *out, const qa_q3_base_player_state *ps) {
  if (!qa_q3_base_player_state_init(out, ps->state_product, NULL)) return false;
  qa_q3_base_player_state_record_copy_from(out, ps, QA_Q3_BASE_PRESERVE_AUTHORITY);
  return true;
}
void qa_q3_base_player_state_record_set_event_debug(qa_q3_base_player_state_record *ps,
    qa_q3_base_predictable_event_debug *debug) {
  ps->event_debug = debug;
}
static void pad_start(const char *s, size_t width, char *buf, size_t cap) {
  size_t n = strlen(s);
  size_t pad = n >= width ? 0 : width - n;
  size_t o = 0;
  for (size_t i = 0; i < pad && o + 1 < cap; i++) buf[o++] = ' ';
  for (size_t i = 0; i < n && o + 1 < cap; i++) buf[o++] = s[i];
  buf[o] = '\0';
}
qa_q3_base_predictable_event qa_q3_base_player_state_record_add_event(qa_q3_base_player_state_record *ps,
    double event, double parameter) {
  qa_q3_base_predictable_event_debug *debug = ps->event_debug;
  if (debug != NULL) {
    /* TS slice(0,255) counts UTF-16 units; bytes here (ASCII cvar text). */
    char head[256];
    const char *shown = debug->show_events(debug->ctx);
    size_t n = shown ? strlen(shown) : 0;
    if (n > 255) n = 255;
    memcpy(head, shown ? shown : "", n); head[n] = '\0';
    if (qa_native_atof(head) != 0) {
      const char *name = NULL;
      if (event == trunc(event) && event >= 0 && event < EVENT_NAME_COUNT) name = EVENT_NAMES[(size_t)event];
      if (name == NULL) {
        char eb[32]; num_str(event, eb, sizeof eb);
        qa_range_error("bg_misc.c eventnames has no entry for %s", eb);
      }
      const char *label = debug->module_is_game ? " game" : "Cgame";
      char fc[40], seq[40], fcp[64], seqp[64], namep[64], parm[40];
      num_str(ps->pmove_framecount, fc, sizeof fc);
      num_str(ps->event_sequence, seq, sizeof seq);
      num_str(parameter, parm, sizeof parm);
      pad_start(fc, 5, fcp, sizeof fcp);
      pad_start(seq, 5, seqp, sizeof seqp);
      pad_start(name, 20, namep, sizeof namep);
      char msg[512];
      snprintf(msg, sizeof msg, "%s event svt %s -> %s: num = %s parm %s\n", label, fcp, seqp, namep, parm);
      debug->print(debug->ctx, msg);
    }
  }
  double sequence = ps->event_sequence;
  qa_q3_base_player_state_slots_set(&ps->events, (double)(to_int32(sequence) & 1), event);
  qa_q3_base_player_state_slots_set(&ps->event_parms, (double)(to_int32(sequence) & 1), parameter);
  ps->event_sequence = (double)to_int32(sequence + 1);
  qa_q3_base_predictable_event out = {sequence, event, parameter};
  return out;
}
bool qa_q3_base_create_player_state(qa_q3_base_player_state *out, qa_q3_base_product product,
    const qa_q3_base_player_authority_binding *authority) {
  return qa_q3_base_player_state_init(out, product, authority);
}
