/* Port of src/content/q2/foundation/targets.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/targets.h"
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/network/common/js_number.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/shadow_lights.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"

#define THINK(name) static void name(void *c, qa_q2_entity *e, qa_q2_game_services *g)
#define USE(name) static void name(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a)
#define TOUCH(name) static void name(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact)
#define TCB(name) ((qa_q2_think){NULL, name})
#define UCB(name) ((qa_q2_use){NULL, name})
#define XCB(name) ((qa_q2_touch){NULL, name})

static qa_q2_presentation_event ev_new(const char *kind) {
  qa_q2_presentation_event ev = {0};
  ev.kind = kind;
  return ev;
}
static void emit_sound(qa_q2_game_services *g, qa_actor_id actor, qa_vec3 origin, const char *path, double ch, double vol, double att, bool reliable, const char *loop) {
  qa_q2_presentation_event ev = ev_new("sound");
  ev.has_actor = true; ev.actor = actor; ev.origin = origin; ev.path = path;
  ev.channel = ch; ev.volume = vol; ev.attenuation = att; ev.reliable = reliable; ev.loop = loop;
  g->host->emit(g->host->ctx, ev);
}
static void use_targets(qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *a) {
  qa_q2_game_use_targets_entity(g, e, a, false);
}
static bool live(qa_q2_game_services *g, qa_actor_id id) { return qa_session_actor_registry_is_live(g->host->actors, id); }

THINK(multi_wait) { (void)c; g->cancel(g->ctx, e); }

static void multi(qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *activator) {
  if (e->has_next_think) return;
  e->has_activator = activator != NULL;
  if (activator) e->activator = *activator;
  use_targets(e, g, activator);
  if (!live(g, e->actor.id)) return;
  if (e->wait > 0) g->schedule(g->ctx, e, e->wait, TCB(multi_wait));
  else { e->touch = (qa_q2_touch){0}; g->schedule(g->ctx, e, g->host->frame_seconds(g->host->ctx), QA_Q2_FREE_ENTITY); }
}

TOUCH(touch_multi);
USE(use_multi);
USE(use_target_speaker);
THINK(func_timer_think);
USE(func_timer_use);
USE(use_target_changelevel);
USE(light_use);
USE(trigger_relay_use);
USE(trigger_counter_use);
USE(trigger_key_use);
USE(use_target_help);
USE(use_target_secret_or_goal);
USE(use_target_explosion);
THINK(explode_think);
USE(use_target_splash);
USE(use_target_poi);
USE(use_areaportal);
TOUCH(teleport_touch);
THINK(target_delay_fire);
USE(target_delay_use);
USE(func_static_use);

static void trigger_multiple(qa_q2_entity *e, qa_q2_game_services *g) {
  if (strcmp(e->classname, "trigger_once") == 0) {
    if ((qa_js_to_int32(e->spawnflags) & 1) != 0) e->spawnflags = (double)((qa_js_to_int32(e->spawnflags) & ~1) | 4);
    e->wait = -1;
  } else if (e->wait == 0) e->wait = 0.2;
  qa_vec3 angles = qa_q2_game_body(g, e).angles;
  if (angles.x != 0 || angles.y != 0 || angles.z != 0) e->movedir = qa_q2_movedir(angles);
  e->visible = false;
  e->touch = XCB(touch_multi);
  e->use = UCB(use_multi);
  g->solid(g->ctx, e, (qa_js_to_int32(e->spawnflags) & 4) != 0 ? "none" : "trigger");
}

static double speaker_loop_attenuation(qa_q2_entity *e, qa_q2_game_services *g) {
  if (g->options.edition == QA_Q2_EDITION_CLASSIC) return 1;
  if (e->attenuation == -1) return 0;
  return e->attenuation > 0 && e->attenuation != 3 ? e->attenuation / 5 : 1;
}
static void emit_speaker(qa_q2_entity *e, qa_q2_game_services *g, const char *op) {
  qa_actor_id actor2 = e->actor.id;
  qa_vec3 origin2 = qa_q2_game_body(g, e).origin;
  const char * path = e->noise;
  double vol = strcmp(op, "once") == 0 ? e->volume : 1;
  double att = strcmp(op, "once") == 0 ? e->attenuation : speaker_loop_attenuation(e, g);
  bool reliable = (qa_js_to_int32(e->spawnflags) & 4) != 0;
  emit_sound(g, actor2, origin2, path, 2,
      vol,
      att,
      reliable, op);
}

static void speaker(qa_q2_entity *e, qa_q2_game_services *g) {
  const char *noise = qa_q2_spawn_get(&e->spawn, "noise");
  if (!noise) { g->host->diagnostic(g->host->ctx, "target_speaker has no noise"); return; }
  /* noise with .wav appended needs retained storage */
  static _Thread_local char buf[512];
  const char *n = noise;
  if (!strstr(noise, ".wav")) { snprintf(buf, sizeof buf, "%s.wav", noise); n = buf; }
  e->noise = qa_q2_intern(n); /* the noise path outlives the spawn values */
  e->volume = qa_q2_number_field(&e->spawn, "volume", 0) != 0 ? qa_q2_number_field(&e->spawn, "volume", 0) : 1;
  double authored = qa_q2_number_field(&e->spawn, "attenuation", 0);
  bool rerelease_loop = g->options.edition == QA_Q2_EDITION_RERELEASE && (qa_js_to_int32(e->spawnflags) & 3) != 0;
  e->attenuation = authored == -1 ? (rerelease_loop ? -1 : 0) : authored != 0 ? authored : (rerelease_loop ? 3 : 1);
  e->sound = (qa_js_to_int32(e->spawnflags) & 1) != 0 ? e->noise : "";
  if (e->sound[0]) emit_speaker(e, g, "start");
  e->use = UCB(use_target_speaker);
  g->link(g->ctx, e);
}

static void timer_spawn(qa_q2_entity *e, qa_q2_game_services *g) {
  if (e->wait == 0) e->wait = 1;
  double a = qa_q2_number_field(&e->spawn, "random", 0);
  double b = e->wait - g->host->frame_seconds(g->host->ctx);
  e->random = qa_js_min(a, b);
  e->use = UCB(func_timer_use);
  if ((qa_js_to_int32(e->spawnflags) & 1) != 0) {
    double left;
    double right;
    e->has_activator = true; e->activator = e->actor.id;
    g->schedule(g->ctx, e, (left = 1 + qa_q2_number_field(&e->spawn, "pausetime", 0) + e->delay + e->wait, right = (g->host->random(g->host->ctx) * 2 - 1) * e->random, left + right), TCB(func_timer_think));
  }
}

static bool ieq(const char *a, const char *b) {
  while (*a && *b) { if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false; a++; b++; }
  return *a == *b;
}
static void change_level(qa_q2_entity *e, qa_q2_game_services *g) {
  if (e->map[0] == '\0') { g->host->diagnostic(g->host->ctx, "target_changelevel has no map"); g->remove(g->ctx, e); return; }
  if (ieq(g->options.map_name, "fact1") && ieq(e->map, "fact3")) e->map = "fact3$secret1";
  e->use = UCB(use_target_changelevel);
}

qa_vec3 qa_q2_unrotate_landmark(qa_vec3 v, qa_vec3 angles) {
  double pitch = -angles.x * 3.141592653589793 / 180, roll = -angles.z * 3.141592653589793 / 180, yaw = -angles.y * 3.141592653589793 / 180;
  qa_vec3 x = {v.x, v.y * cos(pitch) - v.z * sin(pitch), v.y * sin(pitch) + v.z * cos(pitch)};
  qa_vec3 y = {x.x * cos(roll) + x.z * sin(roll), x.y, -x.x * sin(roll) + x.z * cos(roll)};
  return (qa_vec3){y.x * cos(yaw) - y.y * sin(yaw), y.x * sin(yaw) + y.y * cos(yaw), y.z};
}

static bool spawn_target(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)c;
  const char *cn = e->classname;
  if (strcmp(cn, "worldspawn") == 0) {
    e->model = "*0";
    g->solid(g->ctx, e, "brush");
    for (int i = 0; i < 8; i++) {
      qa_q2_entity *body = g->create(g->ctx, "bodyque", NULL, 0);
      body->visible = false; body->server_flags = 1;
    }
    static const char *styles[] = {"m", "mmnmmommommnonmmonqnmmo", "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba", "mmmmmaaaaammmmmaaaaaabcdefgabcdefg", "mamamamamama", "jklmnopqrstuvwxyzyxwvutsrqponmlkj", "nmonqnmomnmomomno", "mmmaaaabcdefgmmmmaaaammmaamm", "mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa", "aaaaaaaazzzzzzzz", "mmamammmmammamamaaamammma", "abcdefghijklmnopqrrqponmlkjihgfedcba"};
    for (size_t i = 0; i < 12; i++) {
      qa_q2_presentation_event ev = ev_new("lightstyle");
      ev.style = (double)i; ev.pattern = styles[i];
      g->host->emit(g->host->ctx, ev);
    }
    qa_q2_presentation_event ev = ev_new("music");
    const char *sounds = qa_q2_spawn_get(&e->spawn, "sounds");
    ev.track = sounds ? sounds : "0";
    g->host->emit(g->host->ctx, ev);
    return true;
  }
  static const char *inert[] = {"info_player_start", "info_player_coop", "info_player_deathmatch", "info_player_intermission", "info_notnull", "info_landmark", "func_group", "target_location", "target_position", "info_player_team1", "info_player_team2", "info_player_single", "info_spectator", "info_camp", "info_podium", "misc_portal_surface", "misc_portal_camera"};
  for (size_t i = 0; i < sizeof inert / sizeof inert[0]; i++) if (strcmp(cn, inert[i]) == 0) return true;
  if (strcmp(cn, "info_null") == 0) { g->remove(g->ctx, e); return true; }
  if (strcmp(cn, "light") == 0) {
    double style = qa_q2_integer_field(&e->spawn, "style", 0);
    if (e->targetname[0] == '\0' || g->options.mode == QA_Q2_MODE_DEATHMATCH) { g->remove(g->ctx, e); return true; }
    if (style >= 32) {
      e->use = UCB(light_use);
      qa_q2_presentation_event ev = ev_new("lightstyle");
      ev.style = style; ev.pattern = (qa_js_to_int32(e->spawnflags) & 1) != 0 ? "a" : "m";
      g->host->emit(g->host->ctx, ev);
    }
    return true;
  }
  if (strcmp(cn, "dynamic_light") == 0) { qa_q2_spawn_shadow_light(e, g); return true; }
  if (strcmp(cn, "target_speaker") == 0) { speaker(e, g); return true; }
  if (strcmp(cn, "func_timer") == 0) { timer_spawn(e, g); return true; }
  if (strcmp(cn, "trigger_once") == 0 || strcmp(cn, "trigger_multiple") == 0) { trigger_multiple(e, g); return true; }
  if (strcmp(cn, "trigger_relay") == 0 || strcmp(cn, "target_relay") == 0) { e->use = UCB(trigger_relay_use); return true; }
  if (strcmp(cn, "trigger_teleport") == 0) { e->visible = false; e->touch = XCB(teleport_touch); g->solid(g->ctx, e, "trigger"); return true; }
  if (strcmp(cn, "target_delay") == 0) { e->use = UCB(target_delay_use); return true; }
  if (strcmp(cn, "func_static") == 0) { g->solid(g->ctx, e, "brush"); e->use = UCB(func_static_use); return true; }
  if (strcmp(cn, "trigger_always") == 0) {
    e->delay = qa_js_max(e->delay, 0.2);
    qa_actor_id self = e->actor.id;
    use_targets(e, g, &self);
    return true;
  }
  if (strcmp(cn, "trigger_counter") == 0) { e->wait = -1; if (e->count == 0) e->count = 2; e->use = UCB(trigger_counter_use); return true; }
  if (strcmp(cn, "trigger_key") == 0) {
    const char *key = qa_q2_spawn_get(&e->spawn, "item");
    if (!key) { g->host->diagnostic(g->host->ctx, "trigger_key has no item"); return true; }
    const char *pickup = g->item_name(g->ctx, key);
    if (!pickup || e->target[0] == '\0') {
      static _Thread_local char buf[512];
      snprintf(buf, sizeof buf, "Q2 trigger_key has unknown item or no target: %s", key);
      g->host->diagnostic(g->host->ctx, buf);
      return true;
    }
    e->use = UCB(trigger_key_use);
    return true;
  }
  if (strcmp(cn, "target_help") == 0) {
    if (g->options.mode == QA_Q2_MODE_DEATHMATCH || e->message[0] == '\0') g->remove(g->ctx, e);
    else e->use = UCB(use_target_help);
    return true;
  }
  if (strcmp(cn, "target_secret") == 0 || strcmp(cn, "target_goal") == 0) {
    if (g->options.mode == QA_Q2_MODE_DEATHMATCH) { g->remove(g->ctx, e); return true; }
    bool secret = strcmp(cn, "target_secret") == 0;
    if (secret) g->counters->total_secrets++; else g->counters->total_goals++;
    qa_vec3 origin = qa_q2_game_body(g, e).origin;
    if (secret && qa_js_latin1_equals_ignore_case(g->options.map_name, "mine3") && origin.x == 280 && origin.y == -2048 && origin.z == -624)
      e->message = "You have found a secret area.";
    e->use = UCB(use_target_secret_or_goal);
    return true;
  }
  if (strcmp(cn, "target_changelevel") == 0) { change_level(e, g); return true; }
  if (strcmp(cn, "target_explosion") == 0) { e->use = UCB(use_target_explosion); return true; }
  if (strcmp(cn, "target_splash") == 0) {
    if (e->count == 0) e->count = 32;
    e->movedir = qa_q2_movedir(qa_q2_game_body(g, e).angles);
    e->use = UCB(use_target_splash);
    return true;
  }
  if (strcmp(cn, "target_poi") == 0) {
    if (g->options.edition != QA_Q2_EDITION_RERELEASE) return false;
    e->use = UCB(use_target_poi);
    return true;
  }
  if (strcmp(cn, "func_areaportal") == 0) { e->use = UCB(use_areaportal); return true; }
  return false;
}

TOUCH(touch_multi) {
  (void)c;
  if (strcmp(e->solid, "trigger") != 0) return;
  if (g->host->is_player(g->host->ctx, contact.other)) { if ((qa_js_to_int32(e->spawnflags) & 2) != 0) return; }
  else if (g->host->is_monster(g->host->ctx, contact.other)) { if ((qa_js_to_int32(e->spawnflags) & 1) == 0) return; }
  else return;
  qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, contact.other);
  if (body.has_value && qa_q2_dot(qa_q2_movedir(body.value.angles), e->movedir) < 0) return;
  multi(e, g, &contact.other);
}
USE(use_multi) {
  (void)c; (void)o;
  if (strcmp(e->solid, "none") == 0) { g->solid(g->ctx, e, "trigger"); return; }
  multi(e, g, a);
}
USE(use_target_speaker) {
  (void)c; (void)o; (void)a;
  if ((qa_js_to_int32(e->spawnflags) & 3) != 0) {
    e->sound = e->sound[0] == '\0' ? e->noise : "";
    emit_speaker(e, g, e->sound[0] ? "start" : "stop");
    return;
  }
  emit_speaker(e, g, "once");
}
THINK(func_timer_think) {
  (void)c;
  qa_actor_id act = e->activator;
  use_targets(e, g, e->has_activator ? &act : NULL);
  if (!live(g, e->actor.id)) return;
  g->schedule(g->ctx, e, e->wait + (g->host->random(g->host->ctx) * 2 - 1) * e->random, TCB(func_timer_think));
}
USE(func_timer_use) {
  (void)c; (void)o;
  e->has_activator = a != NULL;
  if (a) e->activator = *a;
  if (e->has_next_think) { g->cancel(g->ctx, e); return; }
  if (e->delay != 0) g->schedule(g->ctx, e, e->delay, TCB(func_timer_think));
  else func_timer_think(NULL, e, g);
}
USE(use_target_changelevel) {
  (void)c;
  /* A started transition replaces the world within moments; a latch this old means travel failed, so the exit may fire again. */
  if (e->transition_started && g->host->now(g->host->ctx) - e->timestamp < 5) return;
  const char *map = e->map;
  if (g->options.mode == QA_Q2_MODE_SINGLEPLAYER) {
    size_t n = 0;
    const qa_actor_id *players = g->host->players(g->host->ctx, &n);
    if (n > 0) {
      qa_combat_state_result cs = qa_gameplay_authority_read(g->host->combat, players[0]);
      if ((cs.has_value ? cs.value.health : 0) <= 0) return;
    }
  }
  if (g->options.mode == QA_Q2_MODE_DEATHMATCH && (qa_js_to_int32(qa_q2_options_deathmatch_flags(&g->options)) & 4096) == 0 && o != NULL) {
    qa_q2_entity *other_ent = g->entity(g->ctx, o);
    if (!(other_ent && strcmp(other_ent->classname, "worldspawn") == 0)) {
      qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, *o);
      qa_combat_state_result health = qa_gameplay_authority_read(g->host->combat, *o);
      if (body.has_value && health.has_value) {
        qa_actor_id self = e->actor.id;
        double maxh = other_ent ? other_ent->max_health : 0;
        g->damage(g->ctx, *o, e, e->actor.id, &self, 10 * (maxh != 0 ? maxh : 100), 1000, QA_Q2_ZERO, body.value.origin, QA_Q2_ZERO, 28, 0, NULL);
      }
      return;
    }
  }
  if (g->options.mode == QA_Q2_MODE_DEATHMATCH && a != NULL && g->options.player_name != NULL) {
    const char *name = g->options.player_name(g->options.player_name_ctx, *a);
    if (name != NULL) {
      char text[600];
      snprintf(text, sizeof text, "%s exited the level.\n", name);
      qa_q2_presentation_event ev = {0};
      ev.kind = "print"; ev.level = "medium"; ev.text = text;
      g->host->emit(g->host->ctx, ev);
    }
  }
  if (strchr(map, '*')) g->counters->server_flags = (double)(qa_js_to_int32(g->counters->server_flags) & ~255);
  const char *sep = strchr(map, '$');
  static _Thread_local char dest[512], full[600];
  const char *spawn_point = "";
  if (sep) { snprintf(dest, sizeof dest, "%.*s", (int)(sep - map), map); spawn_point = sep + 1; }
  else snprintf(dest, sizeof dest, "%s", map);
  qa_q2_entity *landmark = (a == NULL || g->options.mode == QA_Q2_MODE_DEATHMATCH) ? NULL : g->pick_target(g->ctx, e->target);
  qa_body_state_result player_body = a ? qa_shared_body_table_read(g->host->bodies, *a) : (qa_body_state_result){0};
  qa_q2_player_view_result view = a ? g->host->player_view_state(g->host->ctx, *a) : (qa_q2_player_view_result){0};
  if (a && landmark && player_body.has_value && view.has_value) {
    qa_body_state ref = qa_q2_game_body(g, landmark);
    qa_q2_landmark_carry carry = {.player = *a, .name = landmark->targetname,
      .relative_origin = qa_q2_unrotate_landmark(qa_q2_subtract(player_body.value.origin, ref.origin), ref.angles),
      .relative_velocity = qa_q2_unrotate_landmark(view.value.old_velocity, ref.angles),
      .relative_view_angles = qa_q2_subtract(view.value.view_angles, ref.angles)};
    g->host->prepare_level_change(g->host->ctx, map, &carry, g->counters->server_flags);
  } else g->host->prepare_level_change(g->host->ctx, map, NULL, g->counters->server_flags);
  e->transition_started = true;
  e->timestamp = g->host->now(g->host->ctx);
  snprintf(full, sizeof full, "q2:%s", dest);
  qa_transition_intent intent = {0};
  intent.kind = "campaign-level"; intent.campaign = g->options.campaign; intent.map = full;
  intent.spawn_point = spawn_point; intent.cause = a;
  g->host->transition(g->host->ctx, intent);
}
USE(light_use) {
  (void)c; (void)o; (void)a;
  e->spawnflags = (double)(qa_js_to_int32(e->spawnflags) ^ 1);
  qa_q2_presentation_event ev = ev_new("lightstyle");
  ev.style = e->style; ev.pattern = (qa_js_to_int32(e->spawnflags) & 1) != 0 ? "a" : "m";
  g->host->emit(g->host->ctx, ev);
}
USE(trigger_relay_use) { (void)c; (void)o; use_targets(e, g, a); }
USE(trigger_counter_use) {
  (void)c; (void)o;
  if (e->count == 0) return;
  e->count--;
  if ((qa_js_to_int32(e->spawnflags) & 1) == 0 && a) {
    static _Thread_local char buf[128];
    if (e->count != 0) snprintf(buf, sizeof buf, "%s more to go...", QA_JS_NUMBER(e->count));
    qa_q2_presentation_event ev = ev_new("centerprint");
    ev.has_actor = true; ev.actor = *a; ev.text = e->count != 0 ? buf : "Sequence completed!";
    g->host->emit(g->host->ctx, ev);
    /* TS services.sound(services.entity(activator) ?? self, ...). */
    qa_q2_entity *heard = g->entity(g->ctx, a);
    g->sound(g->ctx, heard != NULL ? heard->actor : e->actor, "misc/talk1.wav", 0, 1, 1);
  }
  if (e->count == 0) multi(e, g, a);
}
USE(trigger_key_use) {
  (void)c; (void)o;
  const char *key_name = qa_q2_spawn_get(&e->spawn, "item");
  if (!key_name) key_name = "";
  static _Thread_local char key_buf[512], text_buf[512];
  snprintf(key_buf, sizeof key_buf, "q2:%s", key_name);
  qa_item_id key = key_buf;
  const char *pickup = g->item_name(g->ctx, key_name);
  if (!pickup) qa_range_error("Q2 saved key references missing item %s", key_name);
  if (!a || !g->host->is_player(g->host->ctx, *a)) return;
  if (qa_shared_inventory_table_count(g->host->inventory, *a, key) == 0) {
    if (g->host->now(g->host->ctx) < e->timestamp) return;
    e->timestamp = g->host->now(g->host->ctx) + 5;
    snprintf(text_buf, sizeof text_buf, "You need the %s", pickup);
    qa_q2_presentation_event ev = ev_new("centerprint");
    ev.has_actor = true; ev.actor = *a; ev.text = text_buf;
    g->host->emit(g->host->ctx, ev);
    qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, *a);
    if (body.has_value) emit_sound(g, *a, body.value.origin, "misc/keytry.wav", 0, 1, 1, false, "once");
    return;
  }
  qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, *a);
  if (body.has_value) emit_sound(g, *a, body.value.origin, "misc/keyuse.wav", 0, 1, 1, false, "once");
  bool cube_key = strcmp(key_name, "key_power_cube") == 0 || (g->options.edition == QA_Q2_EDITION_RERELEASE && strcmp(key_name, "key_explosive_charges") == 0);
  int cube = 0;
  if (g->options.mode == QA_Q2_MODE_COOP && cube_key) {
    qa_q2_entity *ae = g->entity(g->ctx, a);
    int32_t bits = ae ? qa_js_to_int32(ae->power_cubes) : 0; /* TS powerCubes & (1 << cube) */
    for (; cube < 8 && (bits & (1 << cube)) == 0; cube++) {}
  }
  size_t pn = 0;
  const qa_actor_id *players = NULL;
  qa_actor_id single = *a;
  if (g->options.mode == QA_Q2_MODE_COOP) players = g->host->players(g->host->ctx, &pn);
  else { players = &single; pn = 1; }
  qa_actor_id *copy = malloc((pn ? pn : 1) * sizeof *copy);
  if (!copy) qa_range_error("out of memory");
  memcpy(copy, players, pn * sizeof *copy);
  for (size_t i = 0; i < pn; i++) {
    qa_owned_actor owner;
    if (!qa_session_actor_registry_resolve_owned(g->host->actors, copy[i], &owner)) continue;
    if (g->options.mode == QA_Q2_MODE_COOP && cube_key) {
      qa_q2_entity *ent = g->entity(g->ctx, &copy[i]);
      if (!ent || (qa_js_to_int32(ent->power_cubes) & (1 << cube)) == 0) continue;
      ent->power_cubes = (double)(qa_js_to_int32(ent->power_cubes) & ~(1 << cube));
      qa_shared_inventory_table_consume(g->host->inventory, owner, key, 1);
    } else {
      double amount = g->options.mode == QA_Q2_MODE_COOP ? qa_shared_inventory_table_count(g->host->inventory, copy[i], key) : 1;
      qa_shared_inventory_table_consume(g->host->inventory, owner, key, amount);
    }
    g->host->key_consumed(g->host->ctx, copy[i]);
  }
  free(copy);
  use_targets(e, g, a);
  e->use = (qa_q2_use){0};
}
USE(use_target_help) {
  (void)c; (void)o; (void)a;
  g->help_changed++;
  qa_q2_presentation_event ev = ev_new("help");
  ev.slot = (qa_js_to_int32(e->spawnflags) & 1) != 0 ? 1 : 2; ev.text = e->message;
  g->host->emit(g->host->ctx, ev);
}
USE(use_target_secret_or_goal) {
  (void)c; (void)o;
  bool secret = strcmp(e->classname, "target_secret") == 0;
  const char *noise = qa_q2_spawn_get(&e->spawn, "noise");
  g->sound(g->ctx, e->actor, noise ? noise : "misc/secret.wav", 2, 1, 1);
  if (secret) g->counters->found_secrets++; else g->counters->found_goals++;
  if (!secret && g->counters->total_goals == g->counters->found_goals) {
    qa_q2_presentation_event ev = ev_new("music");
    ev.track = "0";
    g->host->emit(g->host->ctx, ev);
  }
  use_targets(e, g, a);
  g->remove(g->ctx, e);
}
THINK(explode_think) {
  (void)c;
  qa_q2_presentation_event ev = ev_new("effect");
  ev.effect = "q2:explosion1"; ev.origin = qa_q2_game_body(g, e).origin; ev.direction = QA_Q2_ZERO; ev.count = 1;
  g->host->emit(g->host->ctx, ev);
  qa_actor_id act = e->activator;
  g->radius_damage(g->ctx, e, e->has_activator ? &act : NULL, e->damage, NULL, e->damage + 40, 25, 0, NULL);
  qa_q2_game_use_targets_entity(g, e, e->has_activator ? &act : NULL, true);
}
USE(use_target_explosion) {
  (void)c; (void)o;
  e->has_activator = a != NULL;
  if (a) e->activator = *a;
  if (e->delay == 0) explode_think(NULL, e, g);
  else g->schedule(g->ctx, e, e->delay, TCB(explode_think));
}
USE(use_target_splash) {
  (void)c; (void)o;
  qa_q2_presentation_event ev = ev_new("effect");
  ev.effect = "q2:splash"; ev.origin = qa_q2_game_body(g, e).origin; ev.direction = e->movedir;
  ev.count = e->count; ev.color = qa_q2_integer_field(&e->spawn, "sounds", 0);
  g->host->emit(g->host->ctx, ev);
  if (e->damage != 0) g->radius_damage(g->ctx, e, a, e->damage, NULL, e->damage + 40, 29, 0, NULL);
}
USE(use_target_poi) {
  (void)c; (void)o; (void)a;
  qa_q2_presentation_event ev = ev_new("poi");
  ev.origin = qa_q2_game_body(g, e).origin; ev.message = e->message; ev.fields = &e->spawn;
  g->host->emit(g->host->ctx, ev);
}
USE(use_areaportal) {
  (void)c; (void)o; (void)a;
  e->count = (double)(qa_js_to_int32(e->count) ^ 1);
  g->host->set_area_portal(g->host->ctx, e->style, e->count != 0);
}
TOUCH(teleport_touch) {
  (void)c;
  if ((qa_js_to_int32(e->spawnflags) & 1) != 0 && !g->host->is_player(g->host->ctx, contact.other)) return;
  qa_combat_state_result cs = qa_gameplay_authority_read(g->host->combat, contact.other);
  if ((cs.has_value ? cs.value.health : 0) < 1) return;
  size_t n = 0;
  qa_q2_entity *const *dests = g->targets(g->ctx, e->target, &n);
  qa_owned_actor owned;
  qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, contact.other);
  if (n == 0 || !qa_session_actor_registry_resolve_owned(g->host->actors, contact.other, &owned) || !body.has_value) return;
  qa_body_state target = qa_q2_game_body(g, dests[0]);
  qa_vec3 old_origin = body.value.origin;
  qa_shared_body_table_unlink(g->host->bodies, owned);
  qa_body_state nb = body.value;
  nb.origin = target.origin; nb.velocity = QA_Q2_ZERO; nb.angles = target.angles; nb.ground = NULL;
  qa_shared_body_table_write(g->host->bodies, owned, nb);
  qa_shared_body_table_link(g->host->bodies, owned, NULL);
  for (int i = 0; i < 2; i++) {
    qa_q2_presentation_event ev = ev_new("effect");
    ev.effect = "q2:player-teleport"; ev.origin = i == 0 ? old_origin : target.origin; ev.direction = QA_Q2_ZERO; ev.count = 1;
    g->host->emit(g->host->ctx, ev);
  }
  use_targets(e, g, &contact.other);
}
THINK(target_delay_fire) {
  (void)c;
  qa_actor_id act = e->activator;
  use_targets(e, g, e->has_activator ? &act : NULL);
}
USE(target_delay_use) {
  (void)c; (void)o;
  e->has_activator = a != NULL;
  if (a) e->activator = *a;
  g->schedule(g->ctx, e, qa_js_max(0, e->wait), TCB(target_delay_fire));
}
USE(func_static_use) {
  (void)c; (void)o; (void)a;
  if (strcmp(e->solid, "brush") == 0) { e->visible = false; g->solid(g->ctx, e, "none"); return; }
  e->visible = true;
  g->solid(g->ctx, e, "brush");
}

static const qa_q2_named_think THINKS[] = {
  {"multi_wait", {NULL, multi_wait}}, {"func_timer_think", {NULL, func_timer_think}},
  {"target_explosion_explode", {NULL, explode_think}}, {"target_delay_fire", {NULL, target_delay_fire}}};
static const qa_q2_named_use USES[] = {
  {"dynamic_light_use", {NULL, qa_q2_dynamic_light_use_fn}}, {"Use_Multi", {NULL, use_multi}},
  {"Use_Target_Speaker", {NULL, use_target_speaker}}, {"func_timer_use", {NULL, func_timer_use}},
  {"use_target_changelevel", {NULL, use_target_changelevel}}, {"light_use", {NULL, light_use}},
  {"trigger_relay_use", {NULL, trigger_relay_use}}, {"trigger_counter_use", {NULL, trigger_counter_use}},
  {"trigger_key_use", {NULL, trigger_key_use}}, {"use_target_help", {NULL, use_target_help}},
  {"use_target_secret_or_goal", {NULL, use_target_secret_or_goal}}, {"use_target_explosion", {NULL, use_target_explosion}},
  {"use_target_splash", {NULL, use_target_splash}}, {"use_target_poi", {NULL, use_target_poi}},
  {"Use_Areaportal", {NULL, use_areaportal}}, {"target_delay_use", {NULL, target_delay_use}},
  {"func_static_use", {NULL, func_static_use}}};
static const qa_q2_named_touch TOUCHES[] = {
  {"Touch_Multi", {NULL, touch_multi}}, {"teleport_touch", {NULL, teleport_touch}}};
static const qa_q2_callback_definitions DEFS = {
  .think = THINKS, .think_count = 4, .use = USES, .use_count = 17, .touch = TOUCHES, .touch_count = 2};

qa_q2_spawn_module qa_q2_create_target_module(void) {
  return (qa_q2_spawn_module){.callbacks = &DEFS, .spawn = spawn_target};
}
