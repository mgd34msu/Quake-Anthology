/* SimulationPresentationEvent <-> unified event tree (unified_event_values.h).
 *
 * TS has no such step: unified-event-codec.ts writes the typed event object a source
 * emitted and reads back that object. Each pair of functions here (w_* builds the TS
 * object of a C payload, r_* builds the C payload of that object) follows the codec's
 * writer for the same TS type (writeEventQ1Event, writeEventQ2PresentationEvent, ...):
 * the same variants, the same fields under the same names, nullable members as null and
 * optional members left out when undefined. C representations of TS values:
 *  - nullable ActorId: has_* flag (or a NULL pointer where the struct holds one);
 *  - optional members: has_* flags, NULL arrays/strings, or the C default the producer
 *    uses for "absent" where TS writes the member only when it is `true` (q2 sound
 *    `local`, q2 view `helpIcon`);
 *  - Q1 sound channels "-1" | "5" | "6" | "7" are the numbers TS sends as numbers;
 *  - enum kinds map to the TS strings through the tables below.
 * The tree a reader returns is the codec's validated output, so every required field is
 * present with its TS type. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_event_values.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/unified_event_codec.h"
#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/content/composition/q1/types.h"
#include "qa/content/composition/q2/types.h"
#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/addons/ctf/types.h"
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/weapons/types.h"
#include "qa/content/q2/missionpacks/entities/types.h"
#include "qa/content/q2/missionpacks/types.h"
#include "qa/content/q2/multiplayer/ctf/types.h"
#include "qa/content/q2/multiplayer/lmctf/types.h"
#include "qa/content/q2/rerelease/campaign.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/foundation/support.h"

static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static noreturn void unknown(const char *what, const char *kind) {
  qa_range_error("Unknown %s presentation event variant %s", what, kind != NULL ? kind : "(null)");
}

/* ---- tree construction -------------------------------------------------------------- */
static qa_save_value *ck(qa_save_value *value) {
  if (value == NULL) qa_range_error("Out of memory");
  return value;
}
static void put(qa_save_value *object, const char *key, qa_save_value *value) {
  if (!qa_save_value_set(object, key, ck(value))) { qa_save_value_free(value); qa_range_error("Out of memory"); }
}
static void push(qa_save_value *array, qa_save_value *value) {
  if (!qa_save_value_push(array, ck(value))) { qa_save_value_free(value); qa_range_error("Out of memory"); }
}
static qa_save_value *obj(void) { return ck(qa_save_value_object()); }
static qa_save_value *arr(void) { return ck(qa_save_value_array()); }
static qa_save_value *num(double value) { return qa_save_value_number(value); }
static qa_save_value *str(const char *value) { return qa_save_value_string(value != NULL ? value : ""); }
static qa_save_value *boolean(bool value) { return qa_save_value_boolean(value); }
static qa_save_value *null_value(void) { return qa_save_value_null(); }
static qa_save_value *str_or_null(const char *value) { return value != NULL ? qa_save_value_string(value) : qa_save_value_null(); }
static qa_save_value *kind_object(const char *kind) {
  qa_save_value *o = obj();
  put(o, "kind", str(kind));
  return o;
}
static qa_save_value *vec3(qa_vec3 v) {
  qa_save_value *o = obj();
  put(o, "x", num(v.x)); put(o, "y", num(v.y)); put(o, "z", num(v.z));
  return o;
}
static qa_save_value *vec4(qa_vec4 v) {
  qa_save_value *o = obj();
  put(o, "x", num(v.x)); put(o, "y", num(v.y)); put(o, "z", num(v.z)); put(o, "w", num(v.w));
  return o;
}
/* writeActorId: {slot, generation}, with the live identity attached. */
static qa_save_value *actor(qa_actor_id id) {
  qa_save_value *o = obj();
  put(o, "slot", num((double)id.slot)); put(o, "generation", num((double)id.generation));
  if (!qa_save_value_attach(o, &id, sizeof id)) { qa_save_value_free(o); qa_range_error("Out of memory"); }
  return o;
}
/* writeEventQ1EventEffectActor: ActorId | null. */
static qa_save_value *actor_or_null(bool has, qa_actor_id id) { return has ? actor(id) : null_value(); }
static qa_save_value *strings(const char *const *values, size_t count) {
  qa_save_value *a = arr();
  for (size_t i = 0; i < count; i++) push(a, str(values[i]));
  return a;
}

/* ---- tree reading (codec-validated trees) -------------------------------------------- */
static const qa_save_value *get(const qa_save_value *t, const char *key) { return t != NULL ? qa_save_value_get(t, key) : NULL; }
static bool present(const qa_save_value *t, const char *key) {
  const qa_save_value *v = get(t, key);
  return v != NULL && v->kind != QA_SAVE_UNDEFINED;
}
static bool null_at(const qa_save_value *t, const char *key) {
  const qa_save_value *v = get(t, key);
  return v == NULL || v->kind == QA_SAVE_NULL || v->kind == QA_SAVE_UNDEFINED;
}
static double rnum(const qa_save_value *t, const char *key) {
  const qa_save_value *v = get(t, key);
  return v != NULL && v->kind == QA_SAVE_NUMBER ? v->number : NAN;
}
static const char *rstr(const qa_save_value *t, const char *key) {
  const qa_save_value *v = get(t, key);
  return v != NULL && v->kind == QA_SAVE_STRING ? v->text : NULL;
}
static bool rbool(const qa_save_value *t, const char *key) {
  const qa_save_value *v = get(t, key);
  return v != NULL && v->kind == QA_SAVE_BOOLEAN && v->boolean;
}
static qa_vec3 vec3_of(const qa_save_value *v) { return (qa_vec3){rnum(v, "x"), rnum(v, "y"), rnum(v, "z")}; }
static qa_vec3 rvec3(const qa_save_value *t, const char *key) { return vec3_of(get(t, key)); }
static qa_vec4 rvec4(const qa_save_value *t, const char *key) {
  const qa_save_value *v = get(t, key);
  return (qa_vec4){rnum(v, "x"), rnum(v, "y"), rnum(v, "z"), rnum(v, "w")};
}
static qa_actor_id actor_of(const qa_save_value *node) {
  qa_actor_id id;
  if (node != NULL && qa_app_unified_event_actor_id(node, &id)) return id;
  memset(&id, 0, sizeof id);
  id.slot = (int64_t)rnum(node, "slot");
  id.generation = (int64_t)rnum(node, "generation");
  return id;
}
static qa_actor_id ractor(const qa_save_value *t, const char *key) { return actor_of(get(t, key)); }
/* Reads a nullable actor: false for null. */
static bool ractor_or_null(const qa_save_value *t, const char *key, qa_actor_id *out) {
  if (null_at(t, key)) { memset(out, 0, sizeof *out); return false; }
  *out = ractor(t, key);
  return true;
}
static size_t count_of(const qa_save_value *v) { return v != NULL && v->kind == QA_SAVE_ARRAY ? v->item_count : 0; }
static const qa_save_value *item_of(const qa_save_value *v, size_t i) { return v->items[i]; }

/* Scratch blocks a typed view points into until it is deep-copied. */
typedef struct scratch { void **blocks; size_t count, capacity; } scratch;
static void *take(scratch *s, size_t count, size_t size) {
  void *block = calloc(count ? count : 1, size ? size : 1);
  if (block == NULL) qa_range_error("Out of memory");
  if (s->count == s->capacity) {
    size_t capacity = s->capacity ? s->capacity * 2 : 16;
    void **grown = realloc(s->blocks, capacity * sizeof *grown);
    if (grown == NULL) { free(block); qa_range_error("Out of memory"); }
    s->blocks = grown; s->capacity = capacity;
  }
  s->blocks[s->count++] = block;
  return block;
}
static void scratch_free(void *arg) {
  scratch *s = arg;
  for (size_t i = 0; i < s->count; i++) free(s->blocks[i]);
  free(s->blocks);
  memset(s, 0, sizeof *s);
}
static const char *const *rstrings(scratch *s, const qa_save_value *v, size_t *out_count) {
  size_t count = count_of(v);
  const char **values = take(s, count, sizeof *values);
  for (size_t i = 0; i < count; i++) values[i] = item_of(v, i)->kind == QA_SAVE_STRING ? item_of(v, i)->text : "";
  *out_count = count;
  return values;
}

/* ---- enum names ------------------------------------------------------------------------- */
static const char *const Q2W_KINDS[] = {"muzzleflash", "beam", "view-weapon", "player-animation", "invisibility-reveal"};
static const char *const Q1AD_KINDS[] = {"music", "sell-screen", "alpha", "rune-collected", "cutscene", "fog",
    "punch-angle", "view-roll", "lightning", "colored-explosion", "monster-count", "developer-message",
    "actor-effects", "debug-bounds"};
static const char *const COMP_Q1_KINDS[] = {"addon", "client", "client-left", "ctf-status", "ctf-capture", "prompt",
    "clear-prompt", "source-log", "developer-message", "level-presentation"};
static const char *const Q2_COMPOSITION_KINDS[] = {"ctf", "lmctf", "grapple-prediction", "kick", "missionpack-player",
    "missionpack-entity"};
static const char *const Q3_SOURCE_KINDS[] = {"print", "log", "server-command", "console-command", "drop-client",
    "configstring", "sound", "entity-event", "player-event"};
static const char *const Q2MP_EFFECT_KINDS[] = {"tracker-pain", "nuke-blind", "ir", "sphere-camera"};
static const char *const Q2MP_ENTITY_KINDS[] = {"steam", "force-wall"};
static const char *const Q1AD_PROGRAMS[] = {"dopa", "mg1", "mg3", "ctf"};
static const char *const COOP_STATES[] = {"none", "in-combat", "bad-area", "blocked", "waiting", "no-lives"};
#define COUNT(table) (sizeof table / sizeof table[0])
static const char *name_of(const char *const *table, size_t count, int value, const char *what) {
  if (value < 0 || (size_t)value >= count) qa_range_error("Unknown %s presentation event kind %d", what, value);
  return table[value];
}
static int index_of(const char *const *table, size_t count, const char *text, const char *what) {
  for (size_t i = 0; i < count; i++) if (is(table[i], text)) return (int)i;
  unknown(what, text);
}

/* ==== Q1Event (writeEventQ1Event) ==================================================== */
static const char *const Q1_NUMBER_CHANNELS[] = {"-1", "5", "6", "7"};
static qa_save_value *sound_channel(const char *channel) {
  for (size_t i = 0; i < COUNT(Q1_NUMBER_CHANNELS); i++)
    if (is(channel, Q1_NUMBER_CHANNELS[i])) return num(strtod(channel, NULL));
  return str(channel);
}
static const char *read_sound_channel(const qa_save_value *v) {
  if (v != NULL && v->kind == QA_SAVE_NUMBER) {
    for (size_t i = 0; i < COUNT(Q1_NUMBER_CHANNELS); i++)
      if (strtod(Q1_NUMBER_CHANNELS[i], NULL) == v->number) return Q1_NUMBER_CHANNELS[i];
    qa_range_error("Unknown Q1 sound channel %g", v->number);
  }
  return v != NULL && v->kind == QA_SAVE_STRING ? v->text : NULL;
}
static qa_save_value *message_args(const qa_q1_msg_arg *args, size_t count) {
  qa_save_value *a = arr();
  for (size_t i = 0; i < count; i++) push(a, args[i].is_number ? num(args[i].number) : str(args[i].text));
  return a;
}
static const qa_q1_msg_arg *read_message_args(scratch *s, const qa_save_value *v, size_t *out_count) {
  size_t count = count_of(v);
  qa_q1_msg_arg *args = take(s, count, sizeof *args);
  for (size_t i = 0; i < count; i++) {
    const qa_save_value *item = item_of(v, i);
    if (item->kind == QA_SAVE_NUMBER) { args[i].is_number = true; args[i].number = item->number; }
    else args[i].text = item->kind == QA_SAVE_STRING ? item->text : "";
  }
  *out_count = count;
  return args;
}
static qa_save_value *w_q1(const qa_q1_event *e) {
  const char *k = e->kind;
  qa_save_value *o = kind_object(k);
  if (is(k, "stop-sound")) {
    put(o, "actor", actor(e->actor)); put(o, "channel", num(e->channel));
  } else if (is(k, "sound")) {
    if (e->has_origin) put(o, "origin", vec3(e->origin));
    put(o, "actor", actor(e->actor)); put(o, "path", str(e->path)); put(o, "channel", sound_channel(e->sound_channel));
    put(o, "attenuation", num(e->attenuation)); put(o, "volume", num(e->volume));
  } else if (is(k, "ambient")) {
    put(o, "origin", vec3(e->origin)); put(o, "path", str(e->path)); put(o, "volume", num(e->volume));
    put(o, "attenuation", num(e->attenuation));
  } else if (is(k, "message")) {
    put(o, "player", actor(e->player)); put(o, "text", str(e->text)); put(o, "center", boolean(e->center));
    if (e->args != NULL) put(o, "args", message_args(e->args, e->arg_count));
    if (e->parts != NULL) {
      qa_save_value *parts = arr();
      put(o, "parts", parts);
      for (size_t i = 0; i < e->part_count; i++) {
        qa_save_value *part = obj();
        push(parts, part);
        put(part, "text", str(e->parts[i].text));
        if (e->parts[i].args != NULL) put(part, "args", message_args(e->parts[i].args, e->parts[i].arg_count));
      }
    }
  } else if (is(k, "effect")) {
    put(o, "effect", str(e->effect)); put(o, "actor", actor_or_null(e->has_actor, e->actor));
    put(o, "origin", vec3(e->origin)); put(o, "amount", num(e->amount));
    if (e->has_muzzle) {
      qa_save_value *muzzle = obj();
      put(o, "muzzle", muzzle);
      put(muzzle, "origin", vec3(e->muzzle_origin)); put(muzzle, "angles", vec3(e->muzzle_angles));
    }
  } else if (is(k, "colored-explosion")) {
    put(o, "origin", vec3(e->origin)); put(o, "colorStart", num(e->color_start)); put(o, "colorLength", num(e->color_length));
  } else if (is(k, "static-model")) {
    put(o, "path", str(e->path)); put(o, "frame", num(e->frame)); put(o, "colorMap", num(e->color_map));
    put(o, "skin", num(e->skin)); put(o, "origin", vec3(e->origin)); put(o, "angles", vec3(e->angles));
  } else if (is(k, "particles")) {
    put(o, "origin", vec3(e->origin)); put(o, "direction", vec3(e->direction)); put(o, "color", num(e->color));
    put(o, "count", num(e->count));
  } else if (is(k, "server-command")) {
    put(o, "text", str(e->text));
  } else if (is(k, "camera")) {
    put(o, "player", actor(e->player)); put(o, "origin", vec3(e->origin)); put(o, "angles", vec3(e->angles));
    if (e->has_view_offset) put(o, "viewOffset", vec3(e->view_offset));
  } else if (is(k, "beam")) {
    put(o, "style", str(e->beam_style)); put(o, "actor", actor(e->actor)); put(o, "start", vec3(e->start));
    put(o, "end", vec3(e->end));
  } else if (is(k, "lightstyle")) {
    put(o, "style", num(e->style)); put(o, "pattern", str(e->pattern));
  } else if (is(k, "monster-total")) {
    put(o, "total", num(e->total));
  } else if (is(k, "secret") || is(k, "monster-killed")) {
    put(o, "actor", actor(e->actor)); put(o, "total", num(e->total)); put(o, "found", num(e->found));
  } else if (is(k, "weapon")) {
    put(o, "player", actor(e->player)); put(o, "weapon", str(e->weapon)); put(o, "viewModel", str(e->view_model));
    put(o, "frame", num(e->frame)); put(o, "punch", num(e->punch));
    if (e->attack.has) {
      qa_save_value *attack = kind_object(e->attack.kind);
      put(o, "attack", attack);
      if (is(e->attack.kind, "axe")) put(attack, "variant", num(e->attack.variant));
    }
  } else if (is(k, "teleport-player")) {
    put(o, "player", actor(e->player)); put(o, "angles", vec3(e->angles)); put(o, "lockUntil", num(e->lock_until));
  } else if (is(k, "powerup")) {
    put(o, "player", actor(e->player)); put(o, "powerup", str(e->powerup)); put(o, "expires", num(e->expires));
  } else if (is(k, "intermission")) {
    put(o, "origin", vec3(e->origin)); put(o, "angles", vec3(e->angles)); put(o, "map", str(e->map));
    put(o, "exitAfter", num(e->exit_after)); put(o, "track", num(e->track));
  } else if (is(k, "finale")) {
    put(o, "text", str(e->text)); put(o, "stage", num(e->stage));
  } else if (is(k, "achievement")) {
    put(o, "player", actor_or_null(e->has_player, e->player)); put(o, "id", str(e->id));
  } else unknown("q1", k);
  return o;
}
static const qa_q1_event *r_q1(scratch *s, const qa_save_value *t) {
  qa_q1_event *e = take(s, 1, sizeof *e);
  const char *k = rstr(t, "kind");
  e->kind = k;
  if (is(k, "stop-sound")) {
    e->has_actor = true; e->actor = ractor(t, "actor"); e->channel = rnum(t, "channel");
  } else if (is(k, "sound")) {
    e->has_origin = present(t, "origin");
    if (e->has_origin) e->origin = rvec3(t, "origin");
    e->has_actor = true; e->actor = ractor(t, "actor"); e->path = rstr(t, "path");
    e->sound_channel = read_sound_channel(get(t, "channel"));
    e->attenuation = rnum(t, "attenuation"); e->volume = rnum(t, "volume");
  } else if (is(k, "ambient")) {
    e->origin = rvec3(t, "origin"); e->path = rstr(t, "path"); e->volume = rnum(t, "volume");
    e->attenuation = rnum(t, "attenuation");
  } else if (is(k, "message")) {
    e->has_player = true; e->player = ractor(t, "player"); e->text = rstr(t, "text"); e->center = rbool(t, "center");
    if (present(t, "args")) e->args = read_message_args(s, get(t, "args"), &e->arg_count);
    if (present(t, "parts")) {
      const qa_save_value *parts = get(t, "parts");
      size_t count = count_of(parts);
      qa_q1_message_part *values = take(s, count, sizeof *values);
      for (size_t i = 0; i < count; i++) {
        const qa_save_value *part = item_of(parts, i);
        values[i].text = rstr(part, "text");
        if (present(part, "args")) values[i].args = read_message_args(s, get(part, "args"), &values[i].arg_count);
      }
      e->parts = values; e->part_count = count;
    }
  } else if (is(k, "effect")) {
    e->effect = rstr(t, "effect"); e->has_actor = ractor_or_null(t, "actor", &e->actor);
    e->origin = rvec3(t, "origin"); e->amount = rnum(t, "amount");
    if (present(t, "muzzle")) {
      e->has_muzzle = true;
      e->muzzle_origin = rvec3(get(t, "muzzle"), "origin"); e->muzzle_angles = rvec3(get(t, "muzzle"), "angles");
    }
  } else if (is(k, "colored-explosion")) {
    e->origin = rvec3(t, "origin"); e->color_start = rnum(t, "colorStart"); e->color_length = rnum(t, "colorLength");
  } else if (is(k, "static-model")) {
    e->path = rstr(t, "path"); e->frame = rnum(t, "frame"); e->color_map = rnum(t, "colorMap");
    e->skin = rnum(t, "skin"); e->origin = rvec3(t, "origin"); e->angles = rvec3(t, "angles");
  } else if (is(k, "particles")) {
    e->origin = rvec3(t, "origin"); e->direction = rvec3(t, "direction"); e->color = rnum(t, "color");
    e->count = rnum(t, "count");
  } else if (is(k, "server-command")) {
    e->text = rstr(t, "text");
  } else if (is(k, "camera")) {
    e->has_player = true; e->player = ractor(t, "player"); e->origin = rvec3(t, "origin"); e->angles = rvec3(t, "angles");
    e->has_view_offset = present(t, "viewOffset");
    if (e->has_view_offset) e->view_offset = rvec3(t, "viewOffset");
  } else if (is(k, "beam")) {
    e->beam_style = rstr(t, "style"); e->has_actor = true; e->actor = ractor(t, "actor");
    e->start = rvec3(t, "start"); e->end = rvec3(t, "end");
  } else if (is(k, "lightstyle")) {
    e->style = rnum(t, "style"); e->pattern = rstr(t, "pattern");
  } else if (is(k, "monster-total")) {
    e->total = rnum(t, "total");
  } else if (is(k, "secret") || is(k, "monster-killed")) {
    e->has_actor = true; e->actor = ractor(t, "actor"); e->total = rnum(t, "total"); e->found = rnum(t, "found");
  } else if (is(k, "weapon")) {
    e->has_player = true; e->player = ractor(t, "player"); e->weapon = rstr(t, "weapon");
    e->view_model = rstr(t, "viewModel"); e->frame = rnum(t, "frame"); e->punch = rnum(t, "punch");
    if (present(t, "attack")) {
      const qa_save_value *attack = get(t, "attack");
      e->attack.has = true; e->attack.kind = rstr(attack, "kind");
      if (is(e->attack.kind, "axe")) e->attack.variant = rnum(attack, "variant");
    }
  } else if (is(k, "teleport-player")) {
    e->has_player = true; e->player = ractor(t, "player"); e->angles = rvec3(t, "angles");
    e->lock_until = rnum(t, "lockUntil");
  } else if (is(k, "powerup")) {
    e->has_player = true; e->player = ractor(t, "player"); e->powerup = rstr(t, "powerup");
    e->expires = rnum(t, "expires");
  } else if (is(k, "intermission")) {
    e->origin = rvec3(t, "origin"); e->angles = rvec3(t, "angles"); e->map = rstr(t, "map");
    e->exit_after = rnum(t, "exitAfter"); e->track = rnum(t, "track");
  } else if (is(k, "finale")) {
    e->text = rstr(t, "text"); e->stage = rnum(t, "stage");
  } else if (is(k, "achievement")) {
    e->has_player = ractor_or_null(t, "player", &e->player); e->id = rstr(t, "id");
  } else unknown("q1", k);
  return e;
}

/* The inline char arrays of qa_q1_intermission_result (map[64], text[4096]). */
static void copy_into(char *target, size_t capacity, const char *text) {
  size_t length = text != NULL ? strlen(text) : 0;
  if (length >= capacity) length = capacity - 1;
  if (length) memcpy(target, text, length);
  target[length] = '\0';
}
/* ==== Q1IntermissionResult / Q1SourceFinale (writeEventQ1IntermissionResult) ========== */
static qa_save_value *w_intermission(const qa_q1_intermission_result *e) {
  qa_save_value *o = kind_object(e->kind);
  if (is(e->kind, "travel")) put(o, "map", str(e->map));
  else if (is(e->kind, "finale")) { put(o, "text", str(e->text)); put(o, "track", num(e->track)); }
  else if (!is(e->kind, "waiting") && !is(e->kind, "sell-screen")) unknown("q1-level", e->kind);
  return o;
}
static const qa_q1_intermission_result *r_intermission(scratch *s, const qa_save_value *t) {
  qa_q1_intermission_result *e = take(s, 1, sizeof *e);
  e->kind = rstr(t, "kind");
  if (is(e->kind, "travel")) copy_into(e->map, sizeof e->map, rstr(t, "map"));
  else if (is(e->kind, "finale")) { copy_into(e->text, sizeof e->text, rstr(t, "text")); e->track = rnum(t, "track"); }
  else if (!is(e->kind, "waiting") && !is(e->kind, "sell-screen")) unknown("q1-level", e->kind);
  return e;
}

/* ==== Q1AddonEvent (writeEventQ1AddonEvent) =========================================== */
static qa_save_value *w_q1_addon(const qa_q1ad_event *e) {
  const char *k = name_of(Q1AD_KINDS, COUNT(Q1AD_KINDS), (int)e->kind, "q1 addon");
  qa_save_value *o = kind_object(k);
  switch (e->kind) {
    case QA_Q1AD_EVENT_MUSIC: put(o, "track", num(e->track)); put(o, "loopTrack", num(e->loop_track)); break;
    case QA_Q1AD_EVENT_SELL_SCREEN: break;
    case QA_Q1AD_EVENT_ALPHA: put(o, "actor", actor(e->actor)); put(o, "alpha", num(e->alpha)); break;
    case QA_Q1AD_EVENT_RUNE_COLLECTED:
      put(o, "player", actor(e->player)); put(o, "bits", num(e->bits));
      put(o, "program", str(name_of(Q1AD_PROGRAMS, COUNT(Q1AD_PROGRAMS), (int)e->program, "q1 addon program")));
      break;
    case QA_Q1AD_EVENT_CUTSCENE: put(o, "camera", vec3(e->camera)); put(o, "angles", vec3(e->angles)); break;
    case QA_Q1AD_EVENT_FOG:
      put(o, "player", actor_or_null(e->has_player, e->player)); put(o, "density", num(e->density));
      put(o, "color", vec3(e->color_vec)); put(o, "skyFactor", num(e->sky_factor)); put(o, "duration", num(e->duration));
      break;
    case QA_Q1AD_EVENT_PUNCH_ANGLE: put(o, "player", actor(e->player)); put(o, "angles", vec3(e->angles)); break;
    case QA_Q1AD_EVENT_VIEW_ROLL: put(o, "player", actor(e->player)); put(o, "roll", num(e->roll)); break;
    case QA_Q1AD_EVENT_LIGHTNING:
      put(o, "actor", actor(e->actor)); put(o, "style", num(e->style)); put(o, "start", vec3(e->start));
      put(o, "end", vec3(e->end));
      break;
    case QA_Q1AD_EVENT_COLORED_EXPLOSION:
      put(o, "origin", vec3(e->origin)); put(o, "colorStart", num(e->color_start));
      put(o, "colorLength", num(e->color_length));
      break;
    case QA_Q1AD_EVENT_MONSTER_COUNT: put(o, "count", num(e->count)); break;
    case QA_Q1AD_EVENT_DEVELOPER_MESSAGE: put(o, "text", str(e->text)); break;
    case QA_Q1AD_EVENT_ACTOR_EFFECTS: put(o, "actor", actor(e->actor)); put(o, "effects", num(e->effects)); break;
    case QA_Q1AD_EVENT_DEBUG_BOUNDS:
      put(o, "min", vec3(e->min)); put(o, "max", vec3(e->max)); put(o, "color", num(e->color));
      put(o, "lifetime", num(e->lifetime)); put(o, "depthTest", boolean(e->depth_test));
      break;
  }
  return o;
}
static const qa_q1ad_event *r_q1_addon(scratch *s, const qa_save_value *t) {
  qa_q1ad_event *e = take(s, 1, sizeof *e);
  e->kind = (qa_q1ad_event_kind)index_of(Q1AD_KINDS, COUNT(Q1AD_KINDS), rstr(t, "kind"), "q1 addon");
  switch (e->kind) {
    case QA_Q1AD_EVENT_MUSIC: e->track = rnum(t, "track"); e->loop_track = rnum(t, "loopTrack"); break;
    case QA_Q1AD_EVENT_SELL_SCREEN: break;
    case QA_Q1AD_EVENT_ALPHA: e->has_actor = true; e->actor = ractor(t, "actor"); e->alpha = rnum(t, "alpha"); break;
    case QA_Q1AD_EVENT_RUNE_COLLECTED:
      e->has_player = true; e->player = ractor(t, "player"); e->bits = rnum(t, "bits");
      e->program = (qa_q1ad_program)index_of(Q1AD_PROGRAMS, COUNT(Q1AD_PROGRAMS), rstr(t, "program"), "q1 addon program");
      break;
    case QA_Q1AD_EVENT_CUTSCENE: e->camera = rvec3(t, "camera"); e->angles = rvec3(t, "angles"); break;
    case QA_Q1AD_EVENT_FOG:
      e->has_player = ractor_or_null(t, "player", &e->player); e->density = rnum(t, "density");
      e->color_vec = rvec3(t, "color"); e->sky_factor = rnum(t, "skyFactor"); e->duration = rnum(t, "duration");
      break;
    case QA_Q1AD_EVENT_PUNCH_ANGLE: e->has_player = true; e->player = ractor(t, "player"); e->angles = rvec3(t, "angles"); break;
    case QA_Q1AD_EVENT_VIEW_ROLL: e->has_player = true; e->player = ractor(t, "player"); e->roll = rnum(t, "roll"); break;
    case QA_Q1AD_EVENT_LIGHTNING:
      e->has_actor = true; e->actor = ractor(t, "actor"); e->style = (int)rnum(t, "style");
      e->start = rvec3(t, "start"); e->end = rvec3(t, "end");
      break;
    case QA_Q1AD_EVENT_COLORED_EXPLOSION:
      e->origin = rvec3(t, "origin"); e->color_start = rnum(t, "colorStart"); e->color_length = rnum(t, "colorLength");
      break;
    case QA_Q1AD_EVENT_MONSTER_COUNT: e->count = rnum(t, "count"); break;
    case QA_Q1AD_EVENT_DEVELOPER_MESSAGE: e->text = rstr(t, "text"); break;
    case QA_Q1AD_EVENT_ACTOR_EFFECTS: e->has_actor = true; e->actor = ractor(t, "actor"); e->effects = rnum(t, "effects"); break;
    case QA_Q1AD_EVENT_DEBUG_BOUNDS:
      e->min = rvec3(t, "min"); e->max = rvec3(t, "max"); e->color = rnum(t, "color");
      e->lifetime = rnum(t, "lifetime"); e->depth_test = rbool(t, "depthTest");
      break;
  }
  return e;
}

/* ==== Q1CompositionEvent (writeEventQ1CompositionEvent) =============================== */
static qa_save_value *w_q1_composition(const qa_comp_q1_event *e) {
  const char *k = name_of(COMP_Q1_KINDS, COUNT(COMP_Q1_KINDS), (int)e->kind, "q1-composition");
  qa_save_value *o = kind_object(k);
  switch (e->kind) {
    case QA_COMP_Q1_EVENT_ADDON:
      if (e->addon_event == NULL) qa_range_error("q1-composition addon event has no payload");
      put(o, "event", w_q1_addon(e->addon_event));
      break;
    case QA_COMP_Q1_EVENT_CLIENT: {
      const qa_comp_q1_client_snapshot *c = &e->client;
      qa_save_value *client = obj();
      put(o, "client", client);
      put(client, "actor", actor(c->actor)); put(client, "slot", num((double)c->slot)); put(client, "name", str(c->name));
      put(client, "frags", num(c->frags)); put(client, "shirt", num((double)c->shirt)); put(client, "pants", num((double)c->pants));
      put(client, "team", num(c->team)); put(client, "observer", boolean(c->observer)); put(client, "noTarget", boolean(c->no_target));
      qa_save_value *userinfo = arr();
      put(client, "userinfo", userinfo);
      for (size_t i = 0; i < c->userinfo_count; i++) {
        qa_save_value *entry = obj();
        push(userinfo, entry);
        put(entry, "key", str(c->userinfo[i].key)); put(entry, "value", str(c->userinfo[i].value));
      }
      break;
    }
    case QA_COMP_Q1_EVENT_CLIENT_LEFT: put(o, "actor", actor(e->actor)); put(o, "slot", num((double)e->slot)); break;
    case QA_COMP_Q1_EVENT_CTF_STATUS: {
      if (e->status == NULL) qa_range_error("q1-composition ctf-status has no status");
      put(o, "actor", actor(e->actor));
      qa_save_value *status = obj();
      put(o, "status", status);
      put(status, "red", num(e->status->red)); put(status, "blue", num(e->status->blue));
      put(status, "flags", num((double)e->status->flags)); put(status, "runeItems", num((double)e->status->rune_items));
      break;
    }
    case QA_COMP_Q1_EVENT_CTF_CAPTURE: put(o, "team", str(e->team)); put(o, "total", num(e->total)); break;
    case QA_COMP_Q1_EVENT_PROMPT: {
      put(o, "actor", actor(e->actor)); put(o, "title", str(e->title));
      qa_save_value *choices = arr();
      put(o, "choices", choices);
      for (size_t i = 0; i < e->choice_count; i++) {
        qa_save_value *choice = obj();
        push(choices, choice);
        put(choice, "label", str(e->choices[i].label)); put(choice, "impulse", num(e->choices[i].impulse));
      }
      break;
    }
    case QA_COMP_Q1_EVENT_CLEAR_PROMPT: put(o, "actor", actor(e->actor)); break;
    case QA_COMP_Q1_EVENT_SOURCE_LOG: put(o, "actor", actor(e->actor)); put(o, "action", str(e->action)); break;
    case QA_COMP_Q1_EVENT_DEVELOPER_MESSAGE: put(o, "text", str(e->text)); break;
    case QA_COMP_Q1_EVENT_LEVEL_PRESENTATION:
      if (e->finale == NULL || !(is(e->finale->kind, "finale") || is(e->finale->kind, "sell-screen")))
        unknown("q1 level presentation", e->finale != NULL ? e->finale->kind : NULL);
      put(o, "event", w_intermission(e->finale));
      break;
  }
  return o;
}
static const qa_comp_q1_event *r_q1_composition(scratch *s, const qa_save_value *t) {
  qa_comp_q1_event *e = take(s, 1, sizeof *e);
  e->kind = (qa_comp_q1_event_kind)index_of(COMP_Q1_KINDS, COUNT(COMP_Q1_KINDS), rstr(t, "kind"), "q1-composition");
  switch (e->kind) {
    case QA_COMP_Q1_EVENT_ADDON: e->addon_event = r_q1_addon(s, get(t, "event")); break;
    case QA_COMP_Q1_EVENT_CLIENT: {
      const qa_save_value *c = get(t, "client");
      qa_comp_q1_client_snapshot *client = &e->client;
      client->actor = ractor(c, "actor"); client->slot = (int64_t)rnum(c, "slot"); client->name = rstr(c, "name");
      client->frags = rnum(c, "frags"); client->shirt = (int64_t)rnum(c, "shirt"); client->pants = (int64_t)rnum(c, "pants");
      client->team = rnum(c, "team"); client->observer = rbool(c, "observer"); client->no_target = rbool(c, "noTarget");
      const qa_save_value *userinfo = get(c, "userinfo");
      size_t count = count_of(userinfo);
      qa_comp_userinfo_entry *entries = take(s, count, sizeof *entries);
      for (size_t i = 0; i < count; i++) {
        entries[i].key = rstr(item_of(userinfo, i), "key"); entries[i].value = rstr(item_of(userinfo, i), "value");
      }
      client->userinfo = entries; client->userinfo_count = count;
      break;
    }
    case QA_COMP_Q1_EVENT_CLIENT_LEFT: e->actor = ractor(t, "actor"); e->slot = (int64_t)rnum(t, "slot"); break;
    case QA_COMP_Q1_EVENT_CTF_STATUS: {
      e->actor = ractor(t, "actor");
      const qa_save_value *v = get(t, "status");
      qa_q1ad_ctf_status *status = take(s, 1, sizeof *status);
      status->red = rnum(v, "red"); status->blue = rnum(v, "blue");
      status->flags = (int64_t)rnum(v, "flags"); status->rune_items = (int64_t)rnum(v, "runeItems");
      e->status = status;
      break;
    }
    case QA_COMP_Q1_EVENT_CTF_CAPTURE: e->team = rstr(t, "team"); e->total = rnum(t, "total"); break;
    case QA_COMP_Q1_EVENT_PROMPT: {
      e->actor = ractor(t, "actor"); e->title = rstr(t, "title");
      const qa_save_value *choices = get(t, "choices");
      size_t count = count_of(choices);
      qa_comp_q1_prompt_choice *values = take(s, count, sizeof *values);
      for (size_t i = 0; i < count; i++) {
        values[i].label = rstr(item_of(choices, i), "label"); values[i].impulse = rnum(item_of(choices, i), "impulse");
      }
      e->choices = values; e->choice_count = count;
      break;
    }
    case QA_COMP_Q1_EVENT_CLEAR_PROMPT: e->actor = ractor(t, "actor"); break;
    case QA_COMP_Q1_EVENT_SOURCE_LOG: e->actor = ractor(t, "actor"); e->action = rstr(t, "action"); break;
    case QA_COMP_Q1_EVENT_DEVELOPER_MESSAGE: e->text = rstr(t, "text"); break;
    case QA_COMP_Q1_EVENT_LEVEL_PRESENTATION: e->finale = r_intermission(s, get(t, "event")); break;
  }
  return e;
}

/* ==== Q2PresentationEvent (writeEventQ2PresentationEvent) ============================== */
static qa_save_value *w_q2(const qa_q2_presentation_event *e) {
  const char *k = e->kind;
  qa_save_value *o = kind_object(k);
  if (is(k, "model")) {
    put(o, "actor", actor(e->actor)); put(o, "path", str(e->path));
    put(o, "attachedModels", strings(e->attached_models, 3));
    put(o, "frame", num(e->frame)); put(o, "oldFrame", num(e->old_frame)); put(o, "scale", num(e->scale));
    put(o, "alpha", num(e->alpha)); put(o, "skin", num(e->skin)); put(o, "effects", num(e->effects));
    put(o, "renderFlags", num(e->render_flags));
  } else if (is(k, "visibility")) {
    put(o, "actor", actor(e->actor)); put(o, "visible", boolean(e->visible));
  } else if (is(k, "sound")) {
    put(o, "actor", actor_or_null(e->has_actor, e->actor)); put(o, "origin", vec3(e->origin)); put(o, "path", str(e->path));
    put(o, "channel", num(e->channel)); put(o, "volume", num(e->volume)); put(o, "attenuation", num(e->attenuation));
    put(o, "reliable", boolean(e->reliable)); put(o, "loop", str(e->loop));
    if (e->has_loop_owner) put(o, "loopOwner", str(e->loop_owner));
    if (e->local) put(o, "local", boolean(true));
  } else if (is(k, "centerprint")) {
    put(o, "actor", actor(e->actor)); put(o, "text", str(e->text));
    if (e->has_instant) put(o, "instant", boolean(e->instant));
    if (e->has_duration_seconds) put(o, "durationSeconds", num(e->duration_seconds));
  } else if (is(k, "print")) {
    put(o, "actor", actor_or_null(e->has_actor, e->actor)); put(o, "level", str(e->level)); put(o, "text", str(e->text));
  } else if (is(k, "help")) {
    put(o, "slot", num(e->slot)); put(o, "text", str(e->text));
  } else if (is(k, "lightstyle")) {
    put(o, "style", num(e->style)); put(o, "pattern", str(e->pattern));
  } else if (is(k, "music")) {
    put(o, "track", str(e->track));
  } else if (is(k, "effect")) {
    put(o, "effect", str(e->effect)); put(o, "origin", vec3(e->origin)); put(o, "direction", vec3(e->direction));
    put(o, "count", num(e->count)); put(o, "color", num(e->color));
  } else if (is(k, "damage-indicator")) {
    put(o, "actor", actor(e->actor)); put(o, "origin", vec3(e->origin)); put(o, "amount", num(e->amount));
  } else if (is(k, "pickup")) {
    put(o, "player", actor(e->player)); put(o, "item", str(e->item)); put(o, "icon", str(e->icon)); put(o, "name", str(e->name));
  } else if (is(k, "poi")) {
    put(o, "origin", vec3(e->origin)); put(o, "message", str(e->message));
    /* The Map's tree form is an ordered object (the codec writes it as [{key, value}]). */
    qa_save_value *fields = obj();
    put(o, "fields", fields);
    if (e->fields != NULL)
      for (size_t i = 0; i < e->fields->value_count; i++) put(fields, e->fields->values[i].key, str(e->fields->values[i].value));
  } else if (is(k, "dynamic-light")) {
    put(o, "actor", actor(e->actor)); put(o, "origin", vec3(e->origin)); put(o, "color", vec3(e->light_color));
    put(o, "visible", boolean(e->visible)); put(o, "radius", num(e->radius)); put(o, "intensity", num(e->intensity));
    put(o, "resolution", num(e->resolution)); put(o, "fadeStart", num(e->fade_start)); put(o, "fadeEnd", num(e->fade_end));
    put(o, "lightstyle", num(e->lightstyle));
    if (e->has_cone) {
      qa_save_value *cone = obj();
      put(o, "cone", cone);
      put(cone, "direction", vec3(e->cone_direction)); put(cone, "cosHalfAngle", num(e->cone_cos_half_angle));
    } else put(o, "cone", null_value());
  } else if (is(k, "beam")) {
    put(o, "actor", actor(e->actor)); put(o, "start", vec3(e->start)); put(o, "end", vec3(e->end));
    put(o, "width", num(e->width)); put(o, "color", num(e->color)); put(o, "visible", boolean(e->visible));
  } else if (is(k, "monster-beam")) {
    put(o, "effect", str(e->effect)); put(o, "actor", actor(e->actor)); put(o, "start", vec3(e->start)); put(o, "end", vec3(e->end));
  } else if (is(k, "monster-muzzleflash")) {
    put(o, "actor", actor(e->actor)); put(o, "flash", num(e->flash)); put(o, "origin", vec3(e->origin));
    put(o, "direction", vec3(e->direction));
  } else if (is(k, "entity-event")) {
    put(o, "actor", actor(e->actor)); put(o, "event", num(e->event));
  } else unknown("q2", k);
  return o;
}
static const qa_q2_presentation_event *r_q2(scratch *s, const qa_save_value *t) {
  qa_q2_presentation_event *e = take(s, 1, sizeof *e);
  const char *k = rstr(t, "kind");
  e->kind = k;
  if (is(k, "model")) {
    e->has_actor = true; e->actor = ractor(t, "actor"); e->path = rstr(t, "path");
    const qa_save_value *models = get(t, "attachedModels");
    for (size_t i = 0; i < 3 && i < count_of(models); i++)
      e->attached_models[i] = item_of(models, i)->kind == QA_SAVE_STRING ? item_of(models, i)->text : "";
    e->frame = rnum(t, "frame"); e->old_frame = rnum(t, "oldFrame"); e->scale = rnum(t, "scale");
    e->alpha = rnum(t, "alpha"); e->skin = rnum(t, "skin"); e->effects = rnum(t, "effects");
    e->render_flags = rnum(t, "renderFlags");
  } else if (is(k, "visibility")) {
    e->has_actor = true; e->actor = ractor(t, "actor"); e->visible = rbool(t, "visible");
  } else if (is(k, "sound")) {
    e->has_actor = ractor_or_null(t, "actor", &e->actor); e->origin = rvec3(t, "origin"); e->path = rstr(t, "path");
    e->channel = rnum(t, "channel"); e->volume = rnum(t, "volume"); e->attenuation = rnum(t, "attenuation");
    e->reliable = rbool(t, "reliable"); e->loop = rstr(t, "loop");
    e->has_loop_owner = present(t, "loopOwner");
    if (e->has_loop_owner) e->loop_owner = rstr(t, "loopOwner");
    e->local = present(t, "local") && rbool(t, "local");
  } else if (is(k, "centerprint")) {
    e->has_actor = true; e->actor = ractor(t, "actor"); e->text = rstr(t, "text");
    e->has_instant = present(t, "instant");
    if (e->has_instant) e->instant = rbool(t, "instant");
    e->has_duration_seconds = present(t, "durationSeconds");
    if (e->has_duration_seconds) e->duration_seconds = rnum(t, "durationSeconds");
  } else if (is(k, "print")) {
    e->has_actor = ractor_or_null(t, "actor", &e->actor); e->level = rstr(t, "level"); e->text = rstr(t, "text");
  } else if (is(k, "help")) {
    e->slot = rnum(t, "slot"); e->text = rstr(t, "text");
  } else if (is(k, "lightstyle")) {
    e->style = rnum(t, "style"); e->pattern = rstr(t, "pattern");
  } else if (is(k, "music")) {
    e->track = rstr(t, "track");
  } else if (is(k, "effect")) {
    e->effect = rstr(t, "effect"); e->origin = rvec3(t, "origin"); e->direction = rvec3(t, "direction");
    e->count = rnum(t, "count"); e->color = rnum(t, "color");
  } else if (is(k, "damage-indicator")) {
    e->has_actor = true; e->actor = ractor(t, "actor"); e->origin = rvec3(t, "origin"); e->amount = rnum(t, "amount");
  } else if (is(k, "pickup")) {
    e->player = ractor(t, "player"); e->item = rstr(t, "item"); e->icon = rstr(t, "icon"); e->name = rstr(t, "name");
  } else if (is(k, "poi")) {
    e->origin = rvec3(t, "origin"); e->message = rstr(t, "message");
    const qa_save_value *fields = get(t, "fields");
    size_t count = fields != NULL && fields->kind == QA_SAVE_OBJECT ? fields->entry_count : 0;
    qa_q2_spawn_fields *spawn = take(s, 1, sizeof *spawn);
    qa_q2_field_pair *pairs = take(s, count, sizeof *pairs);
    for (size_t i = 0; i < count; i++) {
      pairs[i].key = fields->entries[i].key;
      pairs[i].value = fields->entries[i].value->kind == QA_SAVE_STRING ? fields->entries[i].value->text : (char *)"";
      if (is(pairs[i].key, "classname")) spawn->classname = pairs[i].value;
    }
    spawn->values = pairs; spawn->value_count = count; spawn->value_capacity = count;
    e->fields = spawn;
  } else if (is(k, "dynamic-light")) {
    e->has_actor = true; e->actor = ractor(t, "actor"); e->origin = rvec3(t, "origin"); e->light_color = rvec3(t, "color");
    e->visible = rbool(t, "visible"); e->radius = rnum(t, "radius"); e->intensity = rnum(t, "intensity");
    e->resolution = rnum(t, "resolution"); e->fade_start = rnum(t, "fadeStart"); e->fade_end = rnum(t, "fadeEnd");
    e->lightstyle = rnum(t, "lightstyle");
    e->has_cone = !null_at(t, "cone");
    if (e->has_cone) {
      e->cone_direction = rvec3(get(t, "cone"), "direction"); e->cone_cos_half_angle = rnum(get(t, "cone"), "cosHalfAngle");
    }
  } else if (is(k, "beam")) {
    e->has_actor = true; e->actor = ractor(t, "actor"); e->start = rvec3(t, "start"); e->end = rvec3(t, "end");
    e->width = rnum(t, "width"); e->color = rnum(t, "color"); e->visible = rbool(t, "visible");
  } else if (is(k, "monster-beam")) {
    e->effect = rstr(t, "effect"); e->has_actor = true; e->actor = ractor(t, "actor");
    e->start = rvec3(t, "start"); e->end = rvec3(t, "end");
  } else if (is(k, "monster-muzzleflash")) {
    e->has_actor = true; e->actor = ractor(t, "actor"); e->flash = rnum(t, "flash"); e->origin = rvec3(t, "origin");
    e->direction = rvec3(t, "direction");
  } else if (is(k, "entity-event")) {
    e->has_actor = true; e->actor = ractor(t, "actor"); e->event = rnum(t, "event");
  } else unknown("q2", k);
  return e;
}

/* ==== Q2WeaponEvent (writeEventQ2WeaponEvent) ========================================== */
static qa_actor_id weapon_actor(const qa_q2w_event *e) {
  if (e->actor == NULL) qa_range_error("q2-weapon %s event has no actor", Q2W_KINDS[e->kind]);
  return *e->actor;
}
static qa_save_value *w_q2_weapon(const qa_q2w_event *e) {
  qa_save_value *o = kind_object(name_of(Q2W_KINDS, COUNT(Q2W_KINDS), (int)e->kind, "q2-weapon"));
  switch (e->kind) {
    case QA_Q2W_EVENT_MUZZLEFLASH:
      put(o, "actor", actor(weapon_actor(e))); put(o, "flash", num(e->flash)); put(o, "silenced", boolean(e->silenced));
      break;
    case QA_Q2W_EVENT_BEAM:
      put(o, "effect", str(e->effect)); put(o, "actor", e->actor != NULL ? actor(*e->actor) : null_value());
      put(o, "start", vec3(e->start)); put(o, "end", vec3(e->end)); put(o, "duration", num(e->duration));
      break;
    case QA_Q2W_EVENT_VIEW_WEAPON:
      put(o, "actor", actor(weapon_actor(e))); put(o, "weapon", str_or_null(e->weapon)); put(o, "model", str(e->model));
      put(o, "playerModel", num(e->player_model)); put(o, "frame", num(e->frame)); put(o, "skin", num(e->skin));
      put(o, "rate", num(e->rate)); put(o, "kickOrigin", vec3(e->kick_origin)); put(o, "kickAngles", vec3(e->kick_angles));
      break;
    case QA_Q2W_EVENT_PLAYER_ANIMATION:
      put(o, "actor", actor(weapon_actor(e))); put(o, "priority", str(e->priority)); put(o, "first", num(e->first));
      put(o, "last", num(e->last)); put(o, "resetTime", boolean(e->reset_time));
      break;
    case QA_Q2W_EVENT_INVISIBILITY_REVEAL:
      put(o, "actor", actor(weapon_actor(e))); put(o, "until", num(e->until));
      break;
  }
  return o;
}
static const qa_q2w_event *r_q2_weapon(scratch *s, const qa_save_value *t) {
  qa_q2w_event *e = take(s, 1, sizeof *e);
  e->kind = (qa_q2w_event_kind)index_of(Q2W_KINDS, COUNT(Q2W_KINDS), rstr(t, "kind"), "q2-weapon");
  if (!null_at(t, "actor")) {
    qa_actor_id *id = take(s, 1, sizeof *id);
    *id = ractor(t, "actor");
    e->actor = id;
  }
  switch (e->kind) {
    case QA_Q2W_EVENT_MUZZLEFLASH: e->flash = rnum(t, "flash"); e->silenced = rbool(t, "silenced"); break;
    case QA_Q2W_EVENT_BEAM:
      e->effect = rstr(t, "effect"); e->start = rvec3(t, "start"); e->end = rvec3(t, "end"); e->duration = rnum(t, "duration");
      break;
    case QA_Q2W_EVENT_VIEW_WEAPON:
      e->weapon = rstr(t, "weapon"); e->model = rstr(t, "model"); e->player_model = rnum(t, "playerModel");
      e->frame = rnum(t, "frame"); e->skin = rnum(t, "skin"); e->rate = rnum(t, "rate");
      e->kick_origin = rvec3(t, "kickOrigin"); e->kick_angles = rvec3(t, "kickAngles");
      break;
    case QA_Q2W_EVENT_PLAYER_ANIMATION:
      e->priority = rstr(t, "priority"); e->first = rnum(t, "first"); e->last = rnum(t, "last");
      e->reset_time = rbool(t, "resetTime");
      break;
    case QA_Q2W_EVENT_INVISIBILITY_REVEAL: e->until = rnum(t, "until"); break;
  }
  return e;
}

/* ==== Q2CompositionEvent (writeEventQ2CompositionEvent) ================================ */
static qa_save_value *playing_team(bool has, double team) { return has ? num(team) : null_value(); }
static qa_save_value *pair(const double values[2]) {
  qa_save_value *a = arr();
  push(a, num(values[0])); push(a, num(values[1]));
  return a;
}
static void read_pair(const qa_save_value *v, double out[2]) {
  out[0] = count_of(v) > 0 && item_of(v, 0)->kind == QA_SAVE_NUMBER ? item_of(v, 0)->number : NAN;
  out[1] = count_of(v) > 1 && item_of(v, 1)->kind == QA_SAVE_NUMBER ? item_of(v, 1)->number : NAN;
}
static qa_save_value *ctf_rows(const qa_q2_ctf_score_row *rows, size_t count) {
  qa_save_value *a = arr();
  for (size_t i = 0; i < count; i++) {
    qa_save_value *row = obj();
    push(a, row);
    put(row, "slot", num(rows[i].slot)); put(row, "name", str(rows[i].name)); put(row, "score", num(rows[i].score));
    put(row, "ping", num(rows[i].ping)); put(row, "carriedFlag", playing_team(rows[i].has_carried_flag, rows[i].carried_flag));
  }
  return a;
}
static const qa_q2_ctf_score_row *read_ctf_rows(scratch *s, const qa_save_value *v, size_t *out_count) {
  size_t count = count_of(v);
  qa_q2_ctf_score_row *rows = take(s, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    const qa_save_value *row = item_of(v, i);
    rows[i].slot = rnum(row, "slot"); rows[i].name = rstr(row, "name"); rows[i].score = rnum(row, "score");
    rows[i].ping = rnum(row, "ping");
    rows[i].has_carried_flag = !null_at(row, "carriedFlag");
    if (rows[i].has_carried_flag) rows[i].carried_flag = rnum(row, "carriedFlag");
  }
  *out_count = count;
  return rows;
}
static void w_grapple_cable(qa_save_value *o, qa_actor_id id, qa_vec3 start, qa_vec3 end, qa_vec3 offset) {
  put(o, "actor", actor(id)); put(o, "start", vec3(start)); put(o, "end", vec3(end)); put(o, "offset", vec3(offset));
}
static qa_save_value *w_ctf(const qa_q2_ctf_event *e) {
  const char *k = e->kind;
  qa_save_value *o = kind_object(k);
  if (is(k, "scoreboard")) {
    put(o, "actor", actor(e->actor)); put(o, "red", ctf_rows(e->red, e->red_count)); put(o, "blue", ctf_rows(e->blue, e->blue_count));
    put(o, "spectators", ctf_rows(e->spectators, e->spectator_count)); put(o, "captures", pair(e->captures));
    put(o, "totals", pair(e->totals)); put(o, "layout", str(e->layout));
  } else if (is(k, "hud")) {
    put(o, "actor", actor(e->actor)); put(o, "captures", pair(e->captures));
    qa_save_value *flags = arr();
    put(o, "flagStates", flags);
    push(flags, str(e->flag_states[0])); push(flags, str(e->flag_states[1]));
    put(o, "team", num(e->team)); put(o, "carriedFlag", playing_team(e->has_carried_flag, e->carried_flag));
    put(o, "tech", str_or_null(e->tech)); put(o, "idTarget", actor_or_null(e->has_id_target, e->id_target));
    put(o, "blinkTeam", playing_team(e->has_blink_team, e->blink_team)); put(o, "match", str(e->match));
  } else if (is(k, "menu")) {
    put(o, "actor", actor(e->actor)); put(o, "title", str(e->title));
    qa_save_value *entries = arr();
    put(o, "entries", entries);
    for (size_t i = 0; i < e->entry_count; i++) {
      qa_save_value *entry = obj();
      push(entries, entry);
      put(entry, "label", str(e->entries[i].label)); put(entry, "action", str_or_null(e->entries[i].action));
    }
  } else if (is(k, "match-status")) {
    put(o, "text", str(e->text));
  } else if (is(k, "admin-settings")) {
    put(o, "actor", actor(e->actor));
    const qa_q2_ctf_admin_settings *v = &e->settings;
    qa_save_value *settings = obj();
    put(o, "settings", settings);
    put(settings, "matchMinutes", num(v->match_minutes)); put(settings, "setupMinutes", num(v->setup_minutes));
    put(settings, "startSeconds", num(v->start_seconds)); put(settings, "weaponsStay", boolean(v->weapons_stay));
    put(settings, "instantItems", boolean(v->instant_items)); put(settings, "quadDrop", boolean(v->quad_drop));
    put(settings, "instantWeapons", boolean(v->instant_weapons)); put(settings, "matchLock", boolean(v->match_lock));
  } else if (is(k, "grapple-cable")) {
    w_grapple_cable(o, e->actor, e->start, e->end, e->offset);
  } else unknown("q2 ctf", k);
  return o;
}
static const qa_q2_ctf_event *r_ctf(scratch *s, const qa_save_value *t) {
  qa_q2_ctf_event *e = take(s, 1, sizeof *e);
  const char *k = rstr(t, "kind");
  e->kind = k;
  if (is(k, "scoreboard")) {
    e->actor = ractor(t, "actor");
    e->red = read_ctf_rows(s, get(t, "red"), &e->red_count);
    e->blue = read_ctf_rows(s, get(t, "blue"), &e->blue_count);
    e->spectators = read_ctf_rows(s, get(t, "spectators"), &e->spectator_count);
    read_pair(get(t, "captures"), e->captures); read_pair(get(t, "totals"), e->totals);
    e->layout = rstr(t, "layout");
  } else if (is(k, "hud")) {
    e->actor = ractor(t, "actor"); read_pair(get(t, "captures"), e->captures);
    const qa_save_value *flags = get(t, "flagStates");
    for (size_t i = 0; i < 2 && i < count_of(flags); i++)
      e->flag_states[i] = item_of(flags, i)->kind == QA_SAVE_STRING ? item_of(flags, i)->text : NULL;
    e->team = rnum(t, "team");
    e->has_carried_flag = !null_at(t, "carriedFlag");
    if (e->has_carried_flag) e->carried_flag = rnum(t, "carriedFlag");
    e->tech = null_at(t, "tech") ? NULL : rstr(t, "tech");
    e->has_id_target = ractor_or_null(t, "idTarget", &e->id_target);
    e->has_blink_team = !null_at(t, "blinkTeam");
    if (e->has_blink_team) e->blink_team = rnum(t, "blinkTeam");
    e->match = rstr(t, "match");
  } else if (is(k, "menu")) {
    e->actor = ractor(t, "actor"); e->title = rstr(t, "title");
    const qa_save_value *entries = get(t, "entries");
    size_t count = count_of(entries);
    qa_q2_ctf_menu_entry *values = take(s, count, sizeof *values);
    for (size_t i = 0; i < count; i++) {
      values[i].label = (char *)rstr(item_of(entries, i), "label");
      values[i].action = null_at(item_of(entries, i), "action") ? NULL : rstr(item_of(entries, i), "action");
    }
    e->entries = values; e->entry_count = count;
  } else if (is(k, "match-status")) {
    e->text = rstr(t, "text");
  } else if (is(k, "admin-settings")) {
    e->actor = ractor(t, "actor");
    const qa_save_value *v = get(t, "settings");
    e->settings.match_minutes = rnum(v, "matchMinutes"); e->settings.setup_minutes = rnum(v, "setupMinutes");
    e->settings.start_seconds = rnum(v, "startSeconds"); e->settings.weapons_stay = rbool(v, "weaponsStay");
    e->settings.instant_items = rbool(v, "instantItems"); e->settings.quad_drop = rbool(v, "quadDrop");
    e->settings.instant_weapons = rbool(v, "instantWeapons"); e->settings.match_lock = rbool(v, "matchLock");
  } else if (is(k, "grapple-cable")) {
    e->actor = ractor(t, "actor"); e->start = rvec3(t, "start"); e->end = rvec3(t, "end"); e->offset = rvec3(t, "offset");
  } else unknown("q2 ctf", k);
  return e;
}
static qa_save_value *w_lmctf(const qa_lmctf_event *e) {
  const char *k = e->kind;
  qa_save_value *o = kind_object(k);
  if (is(k, "grapple-cable")) {
    w_grapple_cable(o, e->actor, e->start, e->end, e->offset);
  } else if (is(k, "menu")) {
    put(o, "actor", actor(e->actor)); put(o, "title", str(e->title));
    qa_save_value *entries = arr();
    put(o, "entries", entries);
    for (size_t i = 0; i < e->entry_count; i++) {
      qa_save_value *entry = obj();
      push(entries, entry);
      put(entry, "label", str(e->entries[i].label)); put(entry, "command", str_or_null(e->entries[i].command));
    }
  } else if (is(k, "scoreboard")) {
    put(o, "actor", actor(e->actor));
    qa_save_value *rows = arr();
    put(o, "rows", rows);
    for (size_t i = 0; i < e->row_count; i++) {
      const qa_lmctf_score_row *r = &e->rows[i];
      qa_save_value *row = obj();
      push(rows, row);
      put(row, "actor", actor(r->actor)); put(row, "slot", num(r->slot)); put(row, "name", str(r->name));
      put(row, "team", num(r->team)); put(row, "score", num(r->score)); put(row, "ping", num(r->ping));
    }
    put(o, "layout", str(e->layout));
  } else if (is(k, "hud")) {
    put(o, "actor", actor(e->actor)); put(o, "team", num(e->team)); put(o, "carriedFlag", boolean(e->carried_flag));
    put(o, "rune", str_or_null(e->rune)); put(o, "layout", str(e->layout));
  } else if (is(k, "score-log")) {
    put(o, "actor", actor(e->actor)); put(o, "victim", actor_or_null(e->has_victim, e->victim)); put(o, "name", str(e->name));
    put(o, "amount", num(e->amount)); put(o, "seconds", num(e->seconds));
  } else unknown("lmctf", k);
  return o;
}
static const qa_lmctf_event *r_lmctf(scratch *s, const qa_save_value *t) {
  qa_lmctf_event *e = take(s, 1, sizeof *e);
  const char *k = rstr(t, "kind");
  e->kind = k;
  e->actor = ractor(t, "actor");
  if (is(k, "grapple-cable")) {
    e->start = rvec3(t, "start"); e->end = rvec3(t, "end"); e->offset = rvec3(t, "offset");
  } else if (is(k, "menu")) {
    e->title = rstr(t, "title");
    const qa_save_value *entries = get(t, "entries");
    size_t count = count_of(entries);
    qa_lmctf_menu_entry *values = take(s, count, sizeof *values);
    for (size_t i = 0; i < count; i++) {
      values[i].label = rstr(item_of(entries, i), "label");
      values[i].command = null_at(item_of(entries, i), "command") ? NULL : rstr(item_of(entries, i), "command");
    }
    e->entries = values; e->entry_count = count;
  } else if (is(k, "scoreboard")) {
    const qa_save_value *rows = get(t, "rows");
    size_t count = count_of(rows);
    qa_lmctf_score_row *values = take(s, count, sizeof *values);
    for (size_t i = 0; i < count; i++) {
      const qa_save_value *row = item_of(rows, i);
      values[i].actor = ractor(row, "actor"); values[i].slot = rnum(row, "slot"); values[i].name = rstr(row, "name");
      values[i].team = rnum(row, "team"); values[i].score = rnum(row, "score"); values[i].ping = rnum(row, "ping");
    }
    e->rows = values; e->row_count = count;
    e->layout = rstr(t, "layout");
  } else if (is(k, "hud")) {
    e->team = rnum(t, "team"); e->carried_flag = rbool(t, "carriedFlag");
    e->rune = null_at(t, "rune") ? NULL : rstr(t, "rune"); e->layout = rstr(t, "layout");
  } else if (is(k, "score-log")) {
    e->has_victim = ractor_or_null(t, "victim", &e->victim); e->name = rstr(t, "name");
    e->amount = rnum(t, "amount"); e->seconds = rnum(t, "seconds");
  } else unknown("lmctf", k);
  return e;
}
static qa_save_value *w_q2_composition(const qa_q2_composition_event *e) {
  qa_save_value *o = kind_object(name_of(Q2_COMPOSITION_KINDS, COUNT(Q2_COMPOSITION_KINDS), (int)e->kind, "q2-composition"));
  switch (e->kind) {
    case QA_Q2_COMPOSITION_EVENT_CTF:
      if (e->ctf == NULL) qa_range_error("q2-composition ctf event has no payload");
      put(o, "event", w_ctf(e->ctf));
      break;
    case QA_Q2_COMPOSITION_EVENT_LMCTF:
      if (e->lmctf == NULL) qa_range_error("q2-composition lmctf event has no payload");
      put(o, "event", w_lmctf(e->lmctf));
      break;
    case QA_Q2_COMPOSITION_EVENT_GRAPPLE_PREDICTION:
      put(o, "actor", actor(e->actor)); put(o, "suppressed", boolean(e->suppressed));
      break;
    case QA_Q2_COMPOSITION_EVENT_KICK: put(o, "actor", actor(e->actor)); break;
    case QA_Q2_COMPOSITION_EVENT_MISSIONPACK_PLAYER: {
      const qa_q2mp_player_effect *p = e->player_effect;
      if (p == NULL) qa_range_error("q2-composition missionpack-player event has no payload");
      qa_save_value *event = kind_object(name_of(Q2MP_EFFECT_KINDS, COUNT(Q2MP_EFFECT_KINDS), (int)p->kind, "missionpack player"));
      put(o, "event", event);
      put(event, "actor", actor(p->actor));
      if (p->kind == QA_Q2MP_EFFECT_SPHERE_CAMERA) {
        put(event, "sphere", actor_or_null(p->has_sphere, p->sphere)); put(event, "origin", vec3(p->origin));
        put(event, "angles", vec3(p->angles));
      } else put(event, "until", num(p->until));
      break;
    }
    case QA_Q2_COMPOSITION_EVENT_MISSIONPACK_ENTITY: {
      const qa_q2mp_entity_event *p = e->entity_event;
      if (p == NULL) qa_range_error("q2-composition missionpack-entity event has no payload");
      qa_save_value *event = kind_object(name_of(Q2MP_ENTITY_KINDS, COUNT(Q2MP_ENTITY_KINDS), (int)p->kind, "missionpack entity"));
      put(o, "event", event);
      if (p->kind == QA_Q2MP_ENTITY_EVENT_STEAM) {
        put(event, "id", num(p->id)); put(event, "origin", vec3(p->origin)); put(event, "direction", vec3(p->direction));
        put(event, "count", num(p->count)); put(event, "color", num(p->color)); put(event, "speed", num(p->speed));
        put(event, "milliseconds", num(p->milliseconds));
      } else {
        put(event, "start", vec3(p->start)); put(event, "end", vec3(p->end)); put(event, "color", num(p->color));
      }
      break;
    }
  }
  return o;
}
static const qa_q2_composition_event *r_q2_composition(scratch *s, const qa_save_value *t) {
  qa_q2_composition_event *e = take(s, 1, sizeof *e);
  e->kind = (qa_q2_composition_event_kind)index_of(Q2_COMPOSITION_KINDS, COUNT(Q2_COMPOSITION_KINDS), rstr(t, "kind"),
      "q2-composition");
  switch (e->kind) {
    case QA_Q2_COMPOSITION_EVENT_CTF: e->ctf = r_ctf(s, get(t, "event")); break;
    case QA_Q2_COMPOSITION_EVENT_LMCTF: e->lmctf = r_lmctf(s, get(t, "event")); break;
    case QA_Q2_COMPOSITION_EVENT_GRAPPLE_PREDICTION: e->actor = ractor(t, "actor"); e->suppressed = rbool(t, "suppressed"); break;
    case QA_Q2_COMPOSITION_EVENT_KICK: e->actor = ractor(t, "actor"); break;
    case QA_Q2_COMPOSITION_EVENT_MISSIONPACK_PLAYER: {
      const qa_save_value *v = get(t, "event");
      qa_q2mp_player_effect *p = take(s, 1, sizeof *p);
      p->kind = (qa_q2mp_player_effect_kind)index_of(Q2MP_EFFECT_KINDS, COUNT(Q2MP_EFFECT_KINDS), rstr(v, "kind"),
          "missionpack player");
      p->actor = ractor(v, "actor");
      if (p->kind == QA_Q2MP_EFFECT_SPHERE_CAMERA) {
        p->has_sphere = ractor_or_null(v, "sphere", &p->sphere); p->origin = rvec3(v, "origin"); p->angles = rvec3(v, "angles");
      } else p->until = rnum(v, "until");
      e->player_effect = p;
      break;
    }
    case QA_Q2_COMPOSITION_EVENT_MISSIONPACK_ENTITY: {
      const qa_save_value *v = get(t, "event");
      qa_q2mp_entity_event *p = take(s, 1, sizeof *p);
      p->kind = (qa_q2mp_entity_event_kind)index_of(Q2MP_ENTITY_KINDS, COUNT(Q2MP_ENTITY_KINDS), rstr(v, "kind"),
          "missionpack entity");
      if (p->kind == QA_Q2MP_ENTITY_EVENT_STEAM) {
        p->id = rnum(v, "id"); p->origin = rvec3(v, "origin"); p->direction = rvec3(v, "direction");
        p->count = rnum(v, "count"); p->color = rnum(v, "color"); p->speed = rnum(v, "speed");
        p->milliseconds = rnum(v, "milliseconds");
      } else {
        p->start = rvec3(v, "start"); p->end = rvec3(v, "end"); p->color = rnum(v, "color");
      }
      e->entity_event = p;
      break;
    }
  }
  return e;
}

/* ==== Q2RereleaseEvent (writeEventQ2RereleaseEvent) ===================================== */
static qa_save_value *w_world_text(const qa_world_text *v) {
  qa_save_value *o = obj();
  put(o, "text", str(v->text)); put(o, "origin", vec3(v->origin)); put(o, "color", vec4(v->color));
  put(o, "cellSize", num(v->cell_size));
  if (v->has_distance_cull_factor) put(o, "distanceCullFactor", num(v->distance_cull_factor));
  qa_save_value *orientation = kind_object(v->orientation_fixed ? "fixed" : "billboard");
  put(o, "orientation", orientation);
  if (v->orientation_fixed) put(orientation, "angles", vec3(v->angles));
  put(o, "depthTest", boolean(v->depth_test)); put(o, "font", str(v->font_selected ? "selected" : "classic"));
  return o;
}
static void r_world_text(const qa_save_value *t, qa_world_text *v) {
  v->text = rstr(t, "text"); v->origin = rvec3(t, "origin"); v->color = rvec4(t, "color"); v->cell_size = rnum(t, "cellSize");
  v->has_distance_cull_factor = present(t, "distanceCullFactor");
  if (v->has_distance_cull_factor) v->distance_cull_factor = rnum(t, "distanceCullFactor");
  v->orientation_fixed = is(rstr(get(t, "orientation"), "kind"), "fixed");
  if (v->orientation_fixed) v->angles = rvec3(get(t, "orientation"), "angles");
  v->depth_test = rbool(t, "depthTest"); v->font_selected = is(rstr(t, "font"), "selected");
}
static qa_save_value *w_q2_rerelease(const qa_q2_rerelease_event *e) {
  const char *k = e->kind;
  qa_save_value *o = kind_object(k);
  if (is(k, "debug-shapes")) {
    qa_save_value *lines = arr();
    put(o, "lines", lines);
    for (size_t i = 0; i < e->line_count; i++) {
      qa_save_value *line = obj();
      push(lines, line);
      put(line, "start", vec3(e->lines[i].start)); put(line, "end", vec3(e->lines[i].end));
      put(line, "color", vec4(e->lines[i].color)); put(line, "depthTest", boolean(e->lines[i].depth_test));
    }
    put(o, "lifetimeMilliseconds", num(e->lifetime_milliseconds));
  } else if (is(k, "world-text")) {
    put(o, "text", w_world_text(&e->text_value)); put(o, "lifetime", num(e->lifetime));
  } else if (is(k, "localized-print")) {
    put(o, "actor", actor_or_null(e->has_actor, e->actor)); put(o, "level", str(e->level)); put(o, "text", str(e->text));
    put(o, "args", strings(e->args, e->arg_count));
  } else if (is(k, "mission-objective")) {
    put(o, "actor", actor(e->actor)); put(o, "text", str(e->text)); put(o, "args", strings(e->args, e->arg_count));
    put(o, "talkSound", boolean(e->talk_sound));
  } else if (is(k, "mission-status")) {
    put(o, "actor", actor(e->actor)); put(o, "iconVisible", boolean(e->icon_visible));
  } else if (is(k, "screen-blend")) {
    put(o, "actor", actor(e->actor)); put(o, "blend", vec4(e->blend));
  } else if (is(k, "help-computer")) {
    put(o, "actor", actor(e->actor)); put(o, "visible", boolean(e->visible)); put(o, "primary", str(e->primary));
    put(o, "secondary", str(e->secondary)); put(o, "slowTime", boolean(e->slow_time));
  } else if (is(k, "fog")) {
    put(o, "actor", actor(e->actor));
    const qa_q2_fog_state *f = &e->fog_value;
    qa_save_value *value = obj(), *fog = obj(), *height = obj();
    put(o, "value", value);
    put(value, "fog", fog);
    put(fog, "density", num(f->fog.density)); put(fog, "color", vec3(f->fog.color)); put(fog, "skyFactor", num(f->fog.sky_factor));
    put(value, "heightFog", height);
    put(height, "startColor", vec3(f->height_fog.start_color)); put(height, "startDistance", num(f->height_fog.start_distance));
    put(height, "endColor", vec3(f->height_fog.end_color)); put(height, "endDistance", num(f->height_fog.end_distance));
    put(height, "falloff", num(f->height_fog.falloff)); put(height, "density", num(f->height_fog.density));
    put(o, "transitionMilliseconds", num(e->transition_milliseconds));
  } else if (is(k, "flashlight")) {
    put(o, "actor", actor(e->actor)); put(o, "enabled", boolean(e->enabled)); put(o, "hand", str(e->hand));
  } else if (is(k, "poi")) {
    put(o, "actor", actor(e->actor)); put(o, "position", vec3(e->position)); put(o, "image", str(e->image));
    put(o, "duration", num(e->duration)); put(o, "color", num(e->color));
  } else if (is(k, "remove-poi")) {
    put(o, "actor", actor(e->actor)); put(o, "key", num(e->key));
  } else if (is(k, "keyed-poi")) {
    put(o, "actor", actor(e->actor)); put(o, "key", num(e->key)); put(o, "position", vec3(e->position));
    put(o, "image", str(e->image)); put(o, "duration", num(e->duration)); put(o, "color", num(e->color));
    put(o, "flags", num(e->flags));
  } else if (is(k, "directional-damage")) {
    put(o, "actor", actor(e->actor)); put(o, "direction", vec3(e->direction)); put(o, "damage", num(e->damage));
    put(o, "health", boolean(e->health)); put(o, "armor", boolean(e->armor)); put(o, "shield", boolean(e->shield));
  } else if (is(k, "help-path")) {
    put(o, "actor", actor(e->actor)); put(o, "first", boolean(e->first)); put(o, "position", vec3(e->position));
    put(o, "direction", vec3(e->direction));
  } else if (is(k, "coop-respawn")) {
    put(o, "actor", actor(e->actor));
    put(o, "state", str(name_of(COOP_STATES, COUNT(COOP_STATES), (int)e->coop_state, "coop respawn state")));
    put(o, "lives", num(e->lives));
  } else if (is(k, "autosave")) {
  } else if (is(k, "alpha")) {
    put(o, "actor", actor(e->actor)); put(o, "alpha", num(e->alpha));
  } else if (is(k, "end-of-unit")) {
    qa_save_value *levels = arr();
    put(o, "levels", levels);
    for (size_t i = 0; i < e->level_count; i++) {
      const qa_q2_rerelease_level_entry *v = &e->levels[i];
      qa_save_value *level = obj();
      push(levels, level);
      put(level, "map", str(v->map)); put(level, "name", str(v->name)); put(level, "visitOrder", num(v->visit_order));
      put(level, "totalSecrets", num(v->total_secrets)); put(level, "foundSecrets", num(v->found_secrets));
      put(level, "totalMonsters", num(v->total_monsters)); put(level, "killedMonsters", num(v->killed_monsters));
      put(level, "time", num(v->time));
    }
    put(o, "buttonTime", num(e->button_time));
  } else if (is(k, "player-dogtag")) {
    put(o, "actor", actor(e->actor)); put(o, "value", str(e->value));
  } else if (is(k, "dynamic-light")) {
    put(o, "actor", actor(e->actor)); put(o, "origin", vec3(e->origin)); put(o, "radius", num(e->radius));
    put(o, "color", vec3(e->light_color)); put(o, "visible", boolean(e->visible));
  } else if (is(k, "restart-level")) {
    put(o, "map", str(e->map));
  } else if (is(k, "story")) {
    put(o, "text", str(e->text));
  } else if (is(k, "achievement")) {
    put(o, "id", str(e->id));
  } else if (is(k, "sky")) {
    put(o, "name", str(e->sky_name)); put(o, "rotation", num(e->rotation)); put(o, "autoRotate", boolean(e->auto_rotate));
    put(o, "axis", vec3(e->axis));
  } else if (is(k, "healthbar")) {
    put(o, "actor", actor(e->actor)); put(o, "slot", num(e->slot)); put(o, "target", actor(e->target));
    put(o, "name", str(e->name)); put(o, "fraction", num(e->fraction)); put(o, "visible", boolean(e->visible));
  } else if (is(k, "item-visibility")) {
    put(o, "actor", actor(e->actor)); put(o, "item", actor(e->item)); put(o, "visible", boolean(e->visible));
  } else unknown("q2-rerelease", k);
  return o;
}
/* Every rerelease variant but localized-print, debug-shapes, world-text, autosave, end-of-unit, restart-level,
 * story, achievement and sky carries a non-null actor. */
static bool rerelease_actor_kind(const char *k) {
  return !(is(k, "localized-print") || is(k, "debug-shapes") || is(k, "world-text") || is(k, "autosave")
      || is(k, "end-of-unit") || is(k, "restart-level") || is(k, "story") || is(k, "achievement") || is(k, "sky"));
}
static const qa_q2_rerelease_event *r_q2_rerelease(scratch *s, const qa_save_value *t) {
  qa_q2_rerelease_event *e = take(s, 1, sizeof *e);
  const char *k = rstr(t, "kind");
  e->kind = k;
  if (rerelease_actor_kind(k)) { e->has_actor = true; e->actor = ractor(t, "actor"); }
  if (is(k, "debug-shapes")) {
    const qa_save_value *lines = get(t, "lines");
    size_t count = count_of(lines);
    qa_debug_line *values = take(s, count, sizeof *values);
    for (size_t i = 0; i < count; i++) {
      const qa_save_value *line = item_of(lines, i);
      values[i].start = rvec3(line, "start"); values[i].end = rvec3(line, "end"); values[i].color = rvec4(line, "color");
      values[i].depth_test = rbool(line, "depthTest");
    }
    e->lines = values; e->line_count = count;
    e->lifetime_milliseconds = rnum(t, "lifetimeMilliseconds");
  } else if (is(k, "world-text")) {
    r_world_text(get(t, "text"), &e->text_value); e->lifetime = rnum(t, "lifetime");
  } else if (is(k, "localized-print")) {
    e->has_actor = ractor_or_null(t, "actor", &e->actor); e->level = rstr(t, "level"); e->text = rstr(t, "text");
    e->args = rstrings(s, get(t, "args"), &e->arg_count);
  } else if (is(k, "mission-objective")) {
    e->text = rstr(t, "text"); e->args = rstrings(s, get(t, "args"), &e->arg_count); e->talk_sound = rbool(t, "talkSound");
  } else if (is(k, "mission-status")) {
    e->icon_visible = rbool(t, "iconVisible");
  } else if (is(k, "screen-blend")) {
    e->blend = rvec4(t, "blend");
  } else if (is(k, "help-computer")) {
    e->visible = rbool(t, "visible"); e->primary = rstr(t, "primary"); e->secondary = rstr(t, "secondary");
    e->slow_time = rbool(t, "slowTime");
  } else if (is(k, "fog")) {
    const qa_save_value *value = get(t, "value"), *fog = get(value, "fog"), *height = get(value, "heightFog");
    qa_q2_fog_state *f = &e->fog_value;
    f->fog.density = rnum(fog, "density"); f->fog.color = rvec3(fog, "color"); f->fog.sky_factor = rnum(fog, "skyFactor");
    f->height_fog.start_color = rvec3(height, "startColor"); f->height_fog.start_distance = rnum(height, "startDistance");
    f->height_fog.end_color = rvec3(height, "endColor"); f->height_fog.end_distance = rnum(height, "endDistance");
    f->height_fog.falloff = rnum(height, "falloff"); f->height_fog.density = rnum(height, "density");
    e->transition_milliseconds = rnum(t, "transitionMilliseconds");
  } else if (is(k, "flashlight")) {
    e->enabled = rbool(t, "enabled"); e->hand = rstr(t, "hand");
  } else if (is(k, "poi")) {
    e->position = rvec3(t, "position"); e->image = rstr(t, "image"); e->duration = rnum(t, "duration");
    e->color = rnum(t, "color");
  } else if (is(k, "remove-poi")) {
    e->key = rnum(t, "key");
  } else if (is(k, "keyed-poi")) {
    e->key = rnum(t, "key"); e->position = rvec3(t, "position"); e->image = rstr(t, "image");
    e->duration = rnum(t, "duration"); e->color = rnum(t, "color"); e->flags = rnum(t, "flags");
  } else if (is(k, "directional-damage")) {
    e->direction = rvec3(t, "direction"); e->damage = rnum(t, "damage"); e->health = rbool(t, "health");
    e->armor = rbool(t, "armor"); e->shield = rbool(t, "shield");
  } else if (is(k, "help-path")) {
    e->first = rbool(t, "first"); e->position = rvec3(t, "position"); e->direction = rvec3(t, "direction");
  } else if (is(k, "coop-respawn")) {
    e->coop_state = (qa_q2_coop_respawn_state)index_of(COOP_STATES, COUNT(COOP_STATES), rstr(t, "state"), "coop respawn state");
    e->lives = rnum(t, "lives");
  } else if (is(k, "autosave")) {
  } else if (is(k, "alpha")) {
    e->alpha = rnum(t, "alpha");
  } else if (is(k, "end-of-unit")) {
    const qa_save_value *levels = get(t, "levels");
    size_t count = count_of(levels);
    qa_q2_rerelease_level_entry *values = take(s, count, sizeof *values);
    for (size_t i = 0; i < count; i++) {
      const qa_save_value *v = item_of(levels, i);
      values[i].map = (char *)rstr(v, "map"); values[i].name = (char *)rstr(v, "name");
      values[i].visit_order = rnum(v, "visitOrder"); values[i].total_secrets = rnum(v, "totalSecrets");
      values[i].found_secrets = rnum(v, "foundSecrets"); values[i].total_monsters = rnum(v, "totalMonsters");
      values[i].killed_monsters = rnum(v, "killedMonsters"); values[i].time = rnum(v, "time");
    }
    e->levels = values; e->level_count = count;
    e->button_time = rnum(t, "buttonTime");
  } else if (is(k, "player-dogtag")) {
    e->value = rstr(t, "value");
  } else if (is(k, "dynamic-light")) {
    e->origin = rvec3(t, "origin"); e->radius = rnum(t, "radius"); e->light_color = rvec3(t, "color");
    e->visible = rbool(t, "visible");
  } else if (is(k, "restart-level")) {
    e->map = rstr(t, "map");
  } else if (is(k, "story")) {
    e->text = rstr(t, "text");
  } else if (is(k, "achievement")) {
    e->id = rstr(t, "id");
  } else if (is(k, "sky")) {
    e->sky_name = rstr(t, "name"); e->rotation = rnum(t, "rotation"); e->auto_rotate = rbool(t, "autoRotate");
    e->axis = rvec3(t, "axis");
  } else if (is(k, "healthbar")) {
    e->slot = rnum(t, "slot"); e->has_target = true; e->target = ractor(t, "target"); e->name = rstr(t, "name");
    e->fraction = rnum(t, "fraction"); e->visible = rbool(t, "visible");
  } else if (is(k, "item-visibility")) {
    e->has_item = true; e->item = ractor(t, "item"); e->visible = rbool(t, "visible");
  } else unknown("q2-rerelease", k);
  return e;
}

/* ==== Q2PlayerEvent (writeEventQ2PlayerEvent) ============================================ */
static qa_save_value *w_player_view(const qa_q2_base_player_view *v) {
  qa_save_value *o = obj();
  put(o, "angles", vec3(v->angles)); put(o, "offset", vec3(v->offset)); put(o, "kickAngles", vec3(v->kick_angles));
  put(o, "gunAngles", vec3(v->gun_angles)); put(o, "gunOffset", vec3(v->gun_offset)); put(o, "blend", vec4(v->blend));
  put(o, "fov", num(v->fov)); put(o, "underwater", boolean(v->underwater)); put(o, "flashes", num(v->flashes));
  put(o, "health", num(v->health)); put(o, "armor", num(v->armor)); put(o, "ammo", num(v->ammo)); put(o, "score", num(v->score));
  put(o, "selectedItem", str_or_null(v->selected_item));
  if (v->has_timer) {
    qa_save_value *timer = obj();
    put(o, "timer", timer);
    put(timer, "item", str(v->timer_item)); put(timer, "seconds", num(v->timer_seconds));
  } else put(o, "timer", null_value());
  put(o, "spectator", boolean(v->spectator)); put(o, "layouts", num(v->layouts));
  if (v->help_icon) put(o, "helpIcon", boolean(true));
  return o;
}
static void r_player_view(const qa_save_value *t, qa_q2_base_player_view *v) {
  v->angles = rvec3(t, "angles"); v->offset = rvec3(t, "offset"); v->kick_angles = rvec3(t, "kickAngles");
  v->gun_angles = rvec3(t, "gunAngles"); v->gun_offset = rvec3(t, "gunOffset"); v->blend = rvec4(t, "blend");
  v->fov = rnum(t, "fov"); v->underwater = rbool(t, "underwater"); v->flashes = rnum(t, "flashes");
  v->health = rnum(t, "health"); v->armor = rnum(t, "armor"); v->ammo = rnum(t, "ammo"); v->score = rnum(t, "score");
  v->selected_item = null_at(t, "selectedItem") ? NULL : rstr(t, "selectedItem");
  v->has_timer = !null_at(t, "timer");
  if (v->has_timer) { v->timer_item = rstr(get(t, "timer"), "item"); v->timer_seconds = rnum(get(t, "timer"), "seconds"); }
  v->spectator = rbool(t, "spectator"); v->layouts = rnum(t, "layouts");
  v->help_icon = present(t, "helpIcon") && rbool(t, "helpIcon");
}
static qa_save_value *w_q2_player(const qa_q2_player_event *e) {
  const char *k = e->kind;
  qa_save_value *o = kind_object(k);
  if (is(k, "print")) {
    put(o, "target", actor_or_null(e->has_target, e->target)); put(o, "level", str(e->level)); put(o, "text", str(e->text));
    return o;
  }
  put(o, "actor", actor(e->actor));
  if (is(k, "userinfo")) {
    put(o, "slot", num(e->slot)); put(o, "name", str(e->name)); put(o, "skin", str(e->skin));
  } else if (is(k, "stufftext")) {
    put(o, "text", str(e->text));
  } else if (is(k, "view")) {
    put(o, "view", w_player_view(&e->view));
  } else if (is(k, "scoreboard")) {
    qa_save_value *rows = arr();
    put(o, "rows", rows);
    for (size_t i = 0; i < e->row_count; i++) {
      const qa_q2_score_row *r = &e->rows[i];
      qa_save_value *row = obj();
      push(rows, row);
      put(row, "slot", num(r->slot)); put(row, "name", str(r->name)); put(row, "score", num(r->score));
      put(row, "ping", num(r->ping)); put(row, "minutes", num(r->minutes)); put(row, "spectator", boolean(r->spectator));
    }
    put(o, "killer", actor_or_null(e->has_killer, e->killer)); put(o, "reliable", boolean(e->reliable));
  } else if (is(k, "inventory")) {
    qa_save_value *entries = arr();
    put(o, "entries", entries);
    for (size_t i = 0; i < e->entry_count; i++) {
      const qa_inventory_entry *v = &e->entries[i];
      qa_save_value *entry = obj();
      push(entries, entry);
      put(entry, "item", str(v->item)); put(entry, "count", num(v->count)); put(entry, "capacity", num(v->capacity));
      if (v->has_count_policy) {
        qa_save_value *policy = kind_object(v->count_policy.kind);
        put(entry, "countPolicy", policy);
        if (is(v->count_policy.kind, "source-counter")) put(policy, "arithmetic", str(v->count_policy.arithmetic));
      }
    }
    if (e->has_visible) put(o, "visible", boolean(e->visible));
    if (e->has_selected) put(o, "selected", str_or_null(e->selected));
    if (e->labels != NULL) {
      qa_save_value *labels = arr();
      put(o, "labels", labels);
      for (size_t i = 0; i < e->label_count; i++) {
        qa_save_value *label = obj();
        push(labels, label);
        put(label, "item", str(e->labels[i].item)); put(label, "name", str(e->labels[i].name));
      }
    }
  } else if (is(k, "help")) {
    put(o, "visible", boolean(e->visible));
  } else if (is(k, "load-menu")) {
  } else if (is(k, "trail")) {
    put(o, "origin", vec3(e->origin)); put(o, "time", num(e->time));
  } else if (is(k, "chase")) {
    put(o, "target", actor_or_null(e->has_chase_target, e->chase_target));
  } else unknown("q2-player", k);
  return o;
}
static const qa_q2_player_event *r_q2_player(scratch *s, const qa_save_value *t) {
  qa_q2_player_event *e = take(s, 1, sizeof *e);
  const char *k = rstr(t, "kind");
  e->kind = k;
  if (is(k, "print")) {
    e->has_target = ractor_or_null(t, "target", &e->target); e->level = rstr(t, "level"); e->text = rstr(t, "text");
    return e;
  }
  e->actor = ractor(t, "actor");
  if (is(k, "userinfo")) {
    e->slot = rnum(t, "slot"); e->name = rstr(t, "name"); e->skin = rstr(t, "skin");
  } else if (is(k, "stufftext")) {
    e->text = rstr(t, "text");
  } else if (is(k, "view")) {
    r_player_view(get(t, "view"), &e->view);
  } else if (is(k, "scoreboard")) {
    const qa_save_value *rows = get(t, "rows");
    size_t count = count_of(rows);
    qa_q2_score_row *values = take(s, count, sizeof *values);
    for (size_t i = 0; i < count; i++) {
      const qa_save_value *row = item_of(rows, i);
      values[i].slot = rnum(row, "slot"); values[i].name = rstr(row, "name"); values[i].score = rnum(row, "score");
      values[i].ping = rnum(row, "ping"); values[i].minutes = rnum(row, "minutes"); values[i].spectator = rbool(row, "spectator");
    }
    e->rows = values; e->row_count = count;
    e->has_killer = ractor_or_null(t, "killer", &e->killer); e->reliable = rbool(t, "reliable");
  } else if (is(k, "inventory")) {
    const qa_save_value *entries = get(t, "entries");
    size_t count = count_of(entries);
    qa_inventory_entry *values = take(s, count, sizeof *values);
    for (size_t i = 0; i < count; i++) {
      const qa_save_value *v = item_of(entries, i);
      values[i].item = rstr(v, "item"); values[i].count = rnum(v, "count"); values[i].capacity = rnum(v, "capacity");
      values[i].has_count_policy = present(v, "countPolicy");
      if (values[i].has_count_policy) {
        values[i].count_policy.kind = rstr(get(v, "countPolicy"), "kind");
        if (is(values[i].count_policy.kind, "source-counter"))
          values[i].count_policy.arithmetic = rstr(get(v, "countPolicy"), "arithmetic");
      }
    }
    e->entries = values; e->entry_count = count;
    e->has_visible = present(t, "visible");
    if (e->has_visible) e->visible = rbool(t, "visible");
    e->has_selected = present(t, "selected");
    if (e->has_selected) e->selected = null_at(t, "selected") ? NULL : rstr(t, "selected");
    if (present(t, "labels")) {
      const qa_save_value *labels = get(t, "labels");
      size_t label_count = count_of(labels);
      qa_q2_player_item_label *values_labels = take(s, label_count, sizeof *values_labels);
      for (size_t i = 0; i < label_count; i++) {
        values_labels[i].item = rstr(item_of(labels, i), "item"); values_labels[i].name = rstr(item_of(labels, i), "name");
      }
      e->labels = values_labels; e->label_count = label_count;
    }
  } else if (is(k, "help")) {
    e->has_visible = true; e->visible = rbool(t, "visible");
  } else if (is(k, "load-menu")) {
  } else if (is(k, "trail")) {
    e->origin = rvec3(t, "origin"); e->time = rnum(t, "time");
  } else if (is(k, "chase")) {
    e->has_chase_target = ractor_or_null(t, "target", &e->chase_target);
  } else unknown("q2-player", k);
  return e;
}

/* ==== Q3SourceEvent (writeEventQ3SourceEvent) ============================================ */
static qa_save_value *numbers(const double *values, size_t count) {
  qa_save_value *a = arr();
  for (size_t i = 0; i < count; i++) push(a, num(values[i]));
  return a;
}
static void read_numbers(scratch *s, const qa_save_value *v, qa_double_view *out) {
  size_t count = count_of(v);
  double *values = take(s, count, sizeof *values);
  for (size_t i = 0; i < count; i++) values[i] = item_of(v, i)->kind == QA_SAVE_NUMBER ? item_of(v, i)->number : NAN;
  out->values = values; out->count = count;
}
/* Q3PlayerState in its TS object shape (unified_event_codec.c's player-state tree). */
static qa_save_value *w_player_state(const qa_q3_player_state *p) {
  qa_save_value *o = obj();
  put(o, "commandTimeMilliseconds", num(p->command_time_milliseconds)); put(o, "movementType", num(p->movement_type));
  put(o, "bobCycle", num(p->bob_cycle)); put(o, "movementFlags", num(p->movement_flags));
  put(o, "movementTimeMilliseconds", num(p->movement_time_milliseconds)); put(o, "origin", vec3(p->origin));
  put(o, "velocity", vec3(p->velocity)); put(o, "weaponTimeMilliseconds", num(p->weapon_time_milliseconds));
  put(o, "gravity", num(p->gravity)); put(o, "speed", num(p->speed));
  put(o, "deltaAngleWords", numbers(p->delta_angle_words.values, 3));
  put(o, "groundEntityNumber", num(p->ground_entity_number)); put(o, "legsTimerMilliseconds", num(p->legs_timer_milliseconds));
  put(o, "legsAnimation", num(p->legs_animation)); put(o, "torsoTimerMilliseconds", num(p->torso_timer_milliseconds));
  put(o, "torsoAnimation", num(p->torso_animation)); put(o, "movementDirection", num(p->movement_direction));
  put(o, "grapplePoint", vec3(p->grapple_point)); put(o, "flags", num(p->flags)); put(o, "eventSequence", num(p->event_sequence));
  put(o, "events", numbers(p->events, 2)); put(o, "eventParameters", numbers(p->event_parameters, 2));
  put(o, "externalEvent", num(p->external_event)); put(o, "externalEventParameter", num(p->external_event_parameter));
  put(o, "externalEventTimeMilliseconds", num(p->external_event_time_milliseconds));
  put(o, "clientNumber", num(p->client_number)); put(o, "weapon", num(p->weapon)); put(o, "weaponState", num(p->weapon_state));
  put(o, "viewAngles", vec3(p->view_angles)); put(o, "viewHeight", num(p->view_height));
  put(o, "damageEvent", num(p->damage_event)); put(o, "damageYaw", num(p->damage_yaw)); put(o, "damagePitch", num(p->damage_pitch));
  put(o, "damageCount", num(p->damage_count));
  put(o, "stats", numbers(p->stats.values, p->stats.count)); put(o, "persistent", numbers(p->persistent.values, p->persistent.count));
  put(o, "powerups", numbers(p->powerups.values, p->powerups.count)); put(o, "ammo", numbers(p->ammo.values, p->ammo.count));
  put(o, "generic1", num(p->generic1)); put(o, "loopSound", num(p->loop_sound)); put(o, "jumpPadEntity", num(p->jump_pad_entity));
  put(o, "pingMilliseconds", num(p->ping_milliseconds)); put(o, "movementFrameCount", num(p->movement_frame_count));
  put(o, "jumpPadFrame", num(p->jump_pad_frame)); put(o, "entityEventSequence", num(p->entity_event_sequence));
  return o;
}
static void r_player_state(scratch *s, const qa_save_value *t, qa_q3_player_state *p) {
  p->command_time_milliseconds = rnum(t, "commandTimeMilliseconds"); p->movement_type = rnum(t, "movementType");
  p->bob_cycle = rnum(t, "bobCycle"); p->movement_flags = rnum(t, "movementFlags");
  p->movement_time_milliseconds = rnum(t, "movementTimeMilliseconds"); p->origin = rvec3(t, "origin");
  p->velocity = rvec3(t, "velocity"); p->weapon_time_milliseconds = rnum(t, "weaponTimeMilliseconds");
  p->gravity = rnum(t, "gravity"); p->speed = rnum(t, "speed");
  const qa_save_value *words = get(t, "deltaAngleWords");
  for (size_t i = 0; i < 3; i++)
    p->delta_angle_words.values[i] = i < count_of(words) && item_of(words, i)->kind == QA_SAVE_NUMBER ? item_of(words, i)->number : NAN;
  p->ground_entity_number = rnum(t, "groundEntityNumber"); p->legs_timer_milliseconds = rnum(t, "legsTimerMilliseconds");
  p->legs_animation = rnum(t, "legsAnimation"); p->torso_timer_milliseconds = rnum(t, "torsoTimerMilliseconds");
  p->torso_animation = rnum(t, "torsoAnimation"); p->movement_direction = rnum(t, "movementDirection");
  p->grapple_point = rvec3(t, "grapplePoint"); p->flags = rnum(t, "flags"); p->event_sequence = rnum(t, "eventSequence");
  const qa_save_value *events = get(t, "events"), *parameters = get(t, "eventParameters");
  for (size_t i = 0; i < 2; i++) {
    p->events[i] = i < count_of(events) && item_of(events, i)->kind == QA_SAVE_NUMBER ? item_of(events, i)->number : NAN;
    p->event_parameters[i] = i < count_of(parameters) && item_of(parameters, i)->kind == QA_SAVE_NUMBER
        ? item_of(parameters, i)->number : NAN;
  }
  p->external_event = rnum(t, "externalEvent"); p->external_event_parameter = rnum(t, "externalEventParameter");
  p->external_event_time_milliseconds = rnum(t, "externalEventTimeMilliseconds");
  p->client_number = rnum(t, "clientNumber"); p->weapon = rnum(t, "weapon"); p->weapon_state = rnum(t, "weaponState");
  p->view_angles = rvec3(t, "viewAngles"); p->view_height = rnum(t, "viewHeight");
  p->damage_event = rnum(t, "damageEvent"); p->damage_yaw = rnum(t, "damageYaw"); p->damage_pitch = rnum(t, "damagePitch");
  p->damage_count = rnum(t, "damageCount");
  read_numbers(s, get(t, "stats"), &p->stats); read_numbers(s, get(t, "persistent"), &p->persistent);
  read_numbers(s, get(t, "powerups"), &p->powerups); read_numbers(s, get(t, "ammo"), &p->ammo);
  p->generic1 = rnum(t, "generic1"); p->loop_sound = rnum(t, "loopSound"); p->jump_pad_entity = rnum(t, "jumpPadEntity");
  p->ping_milliseconds = rnum(t, "pingMilliseconds"); p->movement_frame_count = rnum(t, "movementFrameCount");
  p->jump_pad_frame = rnum(t, "jumpPadFrame"); p->entity_event_sequence = rnum(t, "entityEventSequence");
}
static qa_save_value *w_trajectory(const qa_q3_wire_trajectory *v) {
  qa_save_value *o = obj();
  put(o, "type", num(v->type)); put(o, "time", num(v->time)); put(o, "duration", num(v->duration));
  put(o, "base", vec3(v->base)); put(o, "delta", vec3(v->delta));
  return o;
}
static void r_trajectory(const qa_save_value *t, qa_q3_wire_trajectory *v) {
  v->type = rnum(t, "type"); v->time = rnum(t, "time"); v->duration = rnum(t, "duration");
  v->base = rvec3(t, "base"); v->delta = rvec3(t, "delta");
}
/* writeEntityState's fields. */
static qa_save_value *w_entity_state(const qa_q3_entity_state_record *v) {
  qa_save_value *o = obj();
  put(o, "number", num(v->number)); put(o, "eType", num(v->e_type)); put(o, "eFlags", num(v->e_flags));
  put(o, "pos", w_trajectory(&v->pos)); put(o, "apos", w_trajectory(&v->apos));
  put(o, "time", num(v->time)); put(o, "time2", num(v->time2));
  put(o, "origin", vec3(v->origin)); put(o, "origin2", vec3(v->origin2)); put(o, "angles", vec3(v->angles));
  put(o, "angles2", vec3(v->angles2));
  put(o, "otherEntityNum", num(v->other_entity_num)); put(o, "otherEntityNum2", num(v->other_entity_num2));
  put(o, "groundEntityNum", num(v->ground_entity_num)); put(o, "constantLight", num(v->constant_light));
  put(o, "loopSound", num(v->loop_sound)); put(o, "modelindex", num(v->modelindex)); put(o, "modelindex2", num(v->modelindex2));
  put(o, "clientNum", num(v->client_num)); put(o, "frame", num(v->frame)); put(o, "solid", num(v->solid));
  put(o, "event", num(v->event)); put(o, "eventParm", num(v->event_parm)); put(o, "powerups", num(v->powerups));
  put(o, "weapon", num(v->weapon)); put(o, "legsAnim", num(v->legs_anim)); put(o, "torsoAnim", num(v->torso_anim));
  put(o, "generic1", num(v->generic1));
  return o;
}
static void r_entity_state(const qa_save_value *t, qa_q3_entity_state_record *v) {
  v->number = rnum(t, "number"); v->e_type = rnum(t, "eType"); v->e_flags = rnum(t, "eFlags");
  r_trajectory(get(t, "pos"), &v->pos); r_trajectory(get(t, "apos"), &v->apos);
  v->time = rnum(t, "time"); v->time2 = rnum(t, "time2");
  v->origin = rvec3(t, "origin"); v->origin2 = rvec3(t, "origin2"); v->angles = rvec3(t, "angles"); v->angles2 = rvec3(t, "angles2");
  v->other_entity_num = rnum(t, "otherEntityNum"); v->other_entity_num2 = rnum(t, "otherEntityNum2");
  v->ground_entity_num = rnum(t, "groundEntityNum"); v->constant_light = rnum(t, "constantLight");
  v->loop_sound = rnum(t, "loopSound"); v->modelindex = rnum(t, "modelindex"); v->modelindex2 = rnum(t, "modelindex2");
  v->client_num = rnum(t, "clientNum"); v->frame = rnum(t, "frame"); v->solid = rnum(t, "solid");
  v->event = rnum(t, "event"); v->event_parm = rnum(t, "eventParm"); v->powerups = rnum(t, "powerups");
  v->weapon = rnum(t, "weapon"); v->legs_anim = rnum(t, "legsAnim"); v->torso_anim = rnum(t, "torsoAnim");
  v->generic1 = rnum(t, "generic1");
}
static qa_save_value *w_q3_source(const qa_app_q3_source_event *e) {
  qa_save_value *o = kind_object(name_of(Q3_SOURCE_KINDS, COUNT(Q3_SOURCE_KINDS), (int)e->kind, "q3-source"));
  switch (e->kind) {
    case QA_APP_Q3_EVENT_PRINT: case QA_APP_Q3_EVENT_LOG: put(o, "text", str(e->text)); break;
    case QA_APP_Q3_EVENT_SOUND:
      put(o, "actor", actor(e->actor)); put(o, "origin", vec3(e->origin)); put(o, "velocity", vec3(e->velocity));
      put(o, "path", str(e->path)); put(o, "channel", num(e->channel)); put(o, "volume", num(e->volume));
      put(o, "loop", boolean(e->loop));
      break;
    case QA_APP_Q3_EVENT_SERVER_COMMAND: put(o, "client", num(e->client)); put(o, "text", str(e->text)); break;
    case QA_APP_Q3_EVENT_CONSOLE_COMMAND: put(o, "execution", str(e->execution)); put(o, "text", str(e->text)); break;
    case QA_APP_Q3_EVENT_DROP_CLIENT: put(o, "client", num(e->client)); put(o, "reason", str(e->reason)); break;
    case QA_APP_Q3_EVENT_CONFIGSTRING: put(o, "index", num(e->index)); put(o, "value", str(e->value)); break;
    case QA_APP_Q3_EVENT_ENTITY: {
      const qa_app_q3_source_entity_event *v = &e->entity;
      put(o, "actor", actor(v->actor)); put(o, "state", w_entity_state(&v->state)); put(o, "origin", vec3(v->origin));
      put(o, "time", num(v->time));
      break;
    }
    case QA_APP_Q3_EVENT_PLAYER: {
      const qa_app_q3_source_player_event *v = &e->player;
      put(o, "actor", actor(v->actor));
      qa_save_value *source = obj(), *module = obj();
      put(o, "source", source);
      put(source, "module", module);
      put(module, "id", str(v->source_module.id)); put(module, "artifactPath", str(v->source_module.artifact_path));
      put(module, "digest", str(v->source_module.digest)); put(module, "revision", str(v->source_module.revision));
      put(source, "abiProfile", str(v->source_abi_profile));
      put(o, "playerState", w_player_state(&v->player_state)); put(o, "event", num(v->event));
      put(o, "parameter", num(v->parameter));
      qa_save_value *sequence = kind_object(v->sequence_is_predictable ? "predictable" : "external");
      put(o, "sequence", sequence);
      if (v->sequence_is_predictable) put(sequence, "sequence", num(v->sequence_number));
      else put(sequence, "time", num(v->sequence_time));
      put(o, "origin", vec3(v->origin)); put(o, "time", num(v->time));
      break;
    }
  }
  return o;
}
static const qa_app_q3_source_event *r_q3_source(scratch *s, const qa_save_value *t) {
  qa_app_q3_source_event *e = take(s, 1, sizeof *e);
  e->kind = (qa_app_q3_source_event_kind)index_of(Q3_SOURCE_KINDS, COUNT(Q3_SOURCE_KINDS), rstr(t, "kind"), "q3-source");
  switch (e->kind) {
    case QA_APP_Q3_EVENT_PRINT: case QA_APP_Q3_EVENT_LOG: e->text = rstr(t, "text"); break;
    case QA_APP_Q3_EVENT_SOUND:
      e->actor = ractor(t, "actor"); e->origin = rvec3(t, "origin"); e->velocity = rvec3(t, "velocity");
      e->path = rstr(t, "path"); e->channel = rnum(t, "channel"); e->volume = rnum(t, "volume"); e->loop = rbool(t, "loop");
      break;
    case QA_APP_Q3_EVENT_SERVER_COMMAND: e->client = rnum(t, "client"); e->text = rstr(t, "text"); break;
    case QA_APP_Q3_EVENT_CONSOLE_COMMAND: e->execution = rstr(t, "execution"); e->text = rstr(t, "text"); break;
    case QA_APP_Q3_EVENT_DROP_CLIENT: e->client = rnum(t, "client"); e->reason = rstr(t, "reason"); break;
    case QA_APP_Q3_EVENT_CONFIGSTRING: e->index = rnum(t, "index"); e->value = rstr(t, "value"); break;
    case QA_APP_Q3_EVENT_ENTITY: {
      qa_app_q3_source_entity_event *v = &e->entity;
      v->kind = "entity-event"; v->actor = ractor(t, "actor"); r_entity_state(get(t, "state"), &v->state);
      v->origin = rvec3(t, "origin"); v->time = rnum(t, "time");
      break;
    }
    case QA_APP_Q3_EVENT_PLAYER: {
      qa_app_q3_source_player_event *v = &e->player;
      v->kind = "player-event"; v->actor = ractor(t, "actor");
      const qa_save_value *source = get(t, "source"), *module = get(source, "module");
      v->source_module.id = rstr(module, "id"); v->source_module.artifact_path = rstr(module, "artifactPath");
      v->source_module.digest = rstr(module, "digest"); v->source_module.revision = rstr(module, "revision");
      v->source_abi_profile = rstr(source, "abiProfile");
      r_player_state(s, get(t, "playerState"), &v->player_state);
      v->event = rnum(t, "event"); v->parameter = rnum(t, "parameter");
      const qa_save_value *sequence = get(t, "sequence");
      v->sequence_is_predictable = is(rstr(sequence, "kind"), "predictable");
      if (v->sequence_is_predictable) v->sequence_number = rnum(sequence, "sequence");
      else v->sequence_time = rnum(sequence, "time");
      v->origin = rvec3(t, "origin"); v->time = rnum(t, "time");
      break;
    }
  }
  return e;
}

/* ==== SimulationPresentationEvent (writeSourcePresentationEvent) ========================= */
static qa_save_value *owner_tree(qa_presentation_owner owner) {
  qa_save_value *o = obj();
  put(o, "provider", str(owner.provider)); put(o, "generation", num(owner.generation));
  return o;
}
static qa_presentation_owner owner_of(const qa_save_value *t) {
  return (qa_presentation_owner){rstr(t, "provider"), rnum(t, "generation")};
}
static qa_save_value *payload_tree(const qa_app_sim_presentation_event *e) {
  const char *k = e->kind;
  if (is(k, "presentation-owner")) {
    qa_save_value *o = kind_object(e->presentation_owner.kind);
    put(o, "owner", owner_tree(e->presentation_owner.owner));
    return o;
  }
  if (is(k, "debug-graph")) {
    qa_save_value *o = kind_object(e->debug_graph.kind);
    put(o, "value", num(e->debug_graph.value)); put(o, "color", num(e->debug_graph.color));
    return o;
  }
  if (is(k, "q1")) return w_q1(e->q1);
  if (is(k, "q1-fog")) {
    const qa_app_sim_q1_fog_event *f = &e->q1_fog;
    qa_save_value *o = kind_object(f->kind), *transition = obj(), *previous = obj(), *target = obj();
    put(o, "player", actor_or_null(f->has_player, f->player));
    put(o, "transition", transition);
    put(transition, "previous", previous);
    put(previous, "density", num(f->transition.previous.density)); put(previous, "color", vec3(f->transition.previous.color));
    put(transition, "target", target);
    put(target, "density", num(f->transition.target.density)); put(target, "color", vec3(f->transition.target.color));
    put(transition, "start", num(f->transition.start)); put(transition, "duration", num(f->transition.duration));
    put(o, "skyFactor", num(f->sky_factor));
    return o;
  }
  if (is(k, "q1-sky")) {
    qa_save_value *o = kind_object(e->q1_sky.kind);
    put(o, "name", str(e->q1_sky.name));
    return o;
  }
  if (is(k, "q1-client")) {
    const qa_app_sim_q1_client_metadata_event *c = &e->q1_client;
    qa_save_value *o = kind_object(c->kind);
    put(o, "slot", num(c->slot));
    bool text = is(c->kind, "name") || is(c->kind, "social") || is(c->kind, "player-info");
    put(o, "value", text ? str(c->text) : num(c->value));
    return o;
  }
  if (is(k, "q1-session")) return kind_object(e->q1_session.kind);
  if (is(k, "music")) {
    qa_save_value *o = kind_object(e->music.kind);
    if (is(e->music.kind, "cd-track")) put(o, "track", num(e->music.track));
    else put(o, "paused", boolean(e->music.paused));
    return o;
  }
  if (is(k, "q1-composition")) return w_q1_composition(e->q1_composition);
  if (is(k, "q1-level")) return w_intermission(e->q1_level);
  if (is(k, "q2")) return w_q2(e->q2);
  if (is(k, "q2-weapon")) return w_q2_weapon(e->q2_weapon);
  if (is(k, "q2-composition")) return w_q2_composition(e->q2_composition);
  if (is(k, "q2-rerelease")) return w_q2_rerelease(e->q2_rerelease);
  if (is(k, "q2-player")) return w_q2_player(e->q2_player);
  if (is(k, "q3-character")) {
    const qa_app_sim_q3_character_presentation_event *c = &e->q3_character;
    qa_save_value *o = obj();
    put(o, "sequence", num(c->sequence)); put(o, "timeMilliseconds", num(c->time_milliseconds));
    put(o, "event", num(c->event)); put(o, "parameter", num(c->parameter)); put(o, "actor", actor(c->actor));
    return o;
  }
  if (is(k, "q3-source")) return w_q3_source(e->q3_source);
  unknown("source", k);
}
static void cleanup_tree(void *arg) { qa_save_value_free(arg); }
qa_save_value *qa_app_unified_presentation_event_tree(const qa_app_sim_presentation_event *e) {
  if (!qa_app_sim_source_presentation_kind_known(e->kind)) unknown("source", e->kind);
  qa_save_value *o = obj();
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_tree, o);
  put(o, "kind", str(e->kind));
  if (is(e->kind, "view-reset")) {
    /* TS view-reset carries reason/actor/angles beside kind. */
    put(o, "reason", str(e->view_reset.reason)); put(o, "actor", actor(e->view_reset.actor));
    put(o, "angles", vec3(e->view_reset.angles));
  } else put(o, "event", payload_tree(e));
  put(o, "sequence", num(e->sequence)); put(o, "content", str(e->content)); put(o, "seconds", num(e->seconds));
  /* presentation-owner and debug-graph have no sourceEntity; debug-graph keeps its recipient. */
  bool plain = is(e->kind, "presentation-owner") || is(e->kind, "debug-graph");
  if (!plain || is(e->kind, "debug-graph")) {
    if (!plain) put(o, "sourceEntity", e->has_source_entity ? num(e->source_entity) : null_value());
    if (e->has_recipient) put(o, "recipient", actor(e->recipient));
  }
  if (e->has_owner) put(o, "owner", owner_tree(e->owner));
  qa_cleanup_pop(&guard);
  return o;
}

typedef struct from_scope { const qa_save_value *tree; qa_app_sim_presentation_event view; scratch scratch; } from_scope;
static void from_body(from_scope *scope) {
  const qa_save_value *t = scope->tree;
  scratch *s = &scope->scratch;
  qa_app_sim_presentation_event *e = &scope->view;
  const char *k = rstr(t, "kind");
  if (!qa_app_sim_source_presentation_kind_known(k)) unknown("source", k);
  e->kind = k;
  const qa_save_value *v = get(t, "event");
  if (is(k, "presentation-owner")) {
    e->presentation_owner.kind = rstr(v, "kind"); e->presentation_owner.owner = owner_of(get(v, "owner"));
  } else if (is(k, "debug-graph")) {
    e->debug_graph.kind = rstr(v, "kind"); e->debug_graph.value = rnum(v, "value"); e->debug_graph.color = rnum(v, "color");
  } else if (is(k, "q1")) e->q1 = r_q1(s, v);
  else if (is(k, "q1-fog")) {
    qa_app_sim_q1_fog_event *f = &e->q1_fog;
    const qa_save_value *transition = get(v, "transition");
    f->kind = rstr(v, "kind"); f->has_player = ractor_or_null(v, "player", &f->player);
    f->transition.previous.density = rnum(get(transition, "previous"), "density");
    f->transition.previous.color = rvec3(get(transition, "previous"), "color");
    f->transition.target.density = rnum(get(transition, "target"), "density");
    f->transition.target.color = rvec3(get(transition, "target"), "color");
    f->transition.start = rnum(transition, "start"); f->transition.duration = rnum(transition, "duration");
    f->sky_factor = rnum(v, "skyFactor");
  } else if (is(k, "q1-sky")) {
    e->q1_sky.kind = rstr(v, "kind"); e->q1_sky.name = rstr(v, "name");
  } else if (is(k, "q1-client")) {
    qa_app_sim_q1_client_metadata_event *c = &e->q1_client;
    c->kind = rstr(v, "kind"); c->slot = rnum(v, "slot");
    if (is(c->kind, "name") || is(c->kind, "social") || is(c->kind, "player-info")) c->text = rstr(v, "value");
    else c->value = rnum(v, "value");
  } else if (is(k, "q1-session")) e->q1_session.kind = rstr(v, "kind");
  else if (is(k, "music")) {
    e->music.kind = rstr(v, "kind");
    if (is(e->music.kind, "cd-track")) e->music.track = rnum(v, "track");
    else e->music.paused = rbool(v, "paused");
  } else if (is(k, "q1-composition")) e->q1_composition = r_q1_composition(s, v);
  else if (is(k, "q1-level")) e->q1_level = r_intermission(s, v);
  else if (is(k, "q2")) e->q2 = r_q2(s, v);
  else if (is(k, "q2-weapon")) e->q2_weapon = r_q2_weapon(s, v);
  else if (is(k, "view-reset")) {
    e->view_reset.reason = rstr(t, "reason"); e->view_reset.actor = ractor(t, "actor"); e->view_reset.angles = rvec3(t, "angles");
  } else if (is(k, "q2-composition")) e->q2_composition = r_q2_composition(s, v);
  else if (is(k, "q2-rerelease")) e->q2_rerelease = r_q2_rerelease(s, v);
  else if (is(k, "q2-player")) e->q2_player = r_q2_player(s, v);
  else if (is(k, "q3-character")) {
    qa_app_sim_q3_character_presentation_event *c = &e->q3_character;
    c->sequence = rnum(v, "sequence"); c->time_milliseconds = rnum(v, "timeMilliseconds"); c->event = rnum(v, "event");
    c->parameter = rnum(v, "parameter"); c->actor = ractor(v, "actor");
  } else if (is(k, "q3-source")) e->q3_source = r_q3_source(s, v);
  e->sequence = rnum(t, "sequence"); e->content = rstr(t, "content"); e->seconds = rnum(t, "seconds");
  e->has_source_entity = !null_at(t, "sourceEntity");
  if (e->has_source_entity) e->source_entity = rnum(t, "sourceEntity");
  e->has_recipient = present(t, "recipient");
  if (e->has_recipient) e->recipient = ractor(t, "recipient");
  e->has_owner = present(t, "owner");
  if (e->has_owner) e->owner = owner_of(get(t, "owner"));
}
void qa_app_unified_presentation_event_from_tree(const qa_save_value *tree, qa_app_sim_presentation_event *out) {
  from_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.tree = tree;
  qa_cleanup guard;
  qa_cleanup_push(&guard, scratch_free, &scope.scratch);
  from_body(&scope);
  bool copied = qa_app_sim_presentation_event_copy(out, &scope.view);
  qa_cleanup_pop(&guard);
  scratch_free(&scope.scratch);
  if (!copied) qa_range_error("Out of memory");
}

/* ==== SimulationEvent (readEventSimulationEvent's object) ================================ */
static const char *const NETWORK_EVENT_KINDS[] = {"print", "center-print", "command-text", "config-string", "sound",
    "q1-damage", "q1-particle", "q2-layout", "q2-inventory", "q2-muzzle-flash", "q3-server-command", "disconnect"};
void qa_app_unified_simulation_event_view(const qa_save_value *t, qa_simulation_event *out,
    qa_app_unified_simulation_event_storage *storage) {
  memset(out, 0, sizeof *out);
  memset(storage, 0, sizeof *storage);
  out->sequence = rnum(t, "sequence");
  const qa_save_value *time = get(t, "time");
  out->time.kind = is(rstr(time, "kind"), "milliseconds") ? QA_SOURCE_TIME_MILLISECONDS : QA_SOURCE_TIME_SECONDS;
  out->time.value = rnum(time, "value");
  const qa_save_value *audience = get(t, "audience");
  out->audience.kind = rstr(audience, "kind");
  if (is(out->audience.kind, "seat")) {
    const qa_save_value *node = get(audience, "seat");
    qa_seat_id seat;
    if (!qa_app_unified_event_seat_id(node, &seat)) { memset(&seat, 0, sizeof seat); seat.index = (int64_t)rnum(node, "index"); }
    out->audience.seat = seat;
  } else if (is(out->audience.kind, "client")) {
    const qa_save_value *node = get(audience, "client");
    qa_client_id client;
    if (!qa_app_unified_event_client_id(node, &client)) {
      memset(&client, 0, sizeof client);
      client.slot = (int64_t)rnum(node, "slot"); client.generation = (int64_t)rnum(node, "generation");
    }
    out->audience.client = client;
  }
  const qa_save_value *payload = get(t, "payload");
  const char *kind = rstr(payload, "kind");
  qa_simulation_event_payload *p = &out->payload;
  if (is(kind, "sound")) {
    p->kind = QA_SIM_EVENT_SOUND;
    p->resource = rstr(payload, "resource");
    if (!null_at(payload, "actor")) { storage->actor = ractor(payload, "actor"); p->actor = &storage->actor; }
    p->origin = rvec3(payload, "origin");
    p->channel = rnum(payload, "channel"); p->volume = rnum(payload, "volume"); p->attenuation = rnum(payload, "attenuation");
    return;
  }
  if (!is(kind, "message")) qa_range_error("Unified frames carry no %s simulation events", kind != NULL ? kind : "(null)");
  p->kind = QA_SIM_EVENT_MESSAGE;
  p->has_source_presentation_sequence = present(payload, "sourcePresentationSequence");
  if (p->has_source_presentation_sequence) p->source_presentation_sequence = rnum(payload, "sourcePresentationSequence");
  const qa_save_value *v = get(payload, "event");
  qa_network_event *e = &p->event;
  e->kind = (qa_network_event_kind)index_of(NETWORK_EVENT_KINDS, COUNT(NETWORK_EVENT_KINDS), rstr(v, "kind"), "network");
  switch (e->kind) {
    case QA_NETWORK_EVENT_PRINT: e->level = rnum(v, "level"); e->text = rstr(v, "text"); break;
    case QA_NETWORK_EVENT_CENTER_PRINT: case QA_NETWORK_EVENT_COMMAND_TEXT: e->text = rstr(v, "text"); break;
    case QA_NETWORK_EVENT_CONFIG_STRING: e->index = rnum(v, "index"); e->value = rstr(v, "value"); break;
    case QA_NETWORK_EVENT_SOUND:
      e->entity_number = rnum(v, "entityNumber"); e->channel = rnum(v, "channel"); e->sound_index = rnum(v, "soundIndex");
      if (!null_at(v, "origin")) { storage->origin = rvec3(v, "origin"); e->origin = &storage->origin; }
      e->volume = rnum(v, "volume"); e->attenuation = rnum(v, "attenuation"); e->delay_seconds = rnum(v, "delaySeconds");
      break;
    case QA_NETWORK_EVENT_Q1_DAMAGE: e->armor = rnum(v, "armor"); e->blood = rnum(v, "blood"); e->source = rvec3(v, "source"); break;
    case QA_NETWORK_EVENT_Q1_PARTICLE:
      e->origin_value = rvec3(v, "origin"); e->direction = rvec3(v, "direction"); e->count = rnum(v, "count");
      e->color = rnum(v, "color");
      break;
    case QA_NETWORK_EVENT_Q2_LAYOUT: e->program = rstr(v, "program"); break;
    case QA_NETWORK_EVENT_Q2_INVENTORY: {
      const qa_save_value *counts = get(v, "counts");
      size_t count = count_of(counts);
      storage->counts = calloc(count ? count : 1, sizeof *storage->counts);
      if (storage->counts == NULL) qa_range_error("Out of memory");
      for (size_t i = 0; i < count; i++) storage->counts[i] = item_of(counts, i)->kind == QA_SAVE_NUMBER ? item_of(counts, i)->number : NAN;
      e->counts = (qa_double_view){storage->counts, count};
      break;
    }
    case QA_NETWORK_EVENT_Q2_MUZZLE_FLASH:
      e->entity_number = rnum(v, "entityNumber"); e->flash = rnum(v, "flash"); e->monster = rbool(v, "monster");
      break;
    case QA_NETWORK_EVENT_Q3_SERVER_COMMAND: e->sequence = rnum(v, "sequence"); e->text = rstr(v, "text"); break;
    case QA_NETWORK_EVENT_DISCONNECT: e->reason = rstr(v, "reason"); break;
  }
}
void qa_app_unified_simulation_event_storage_release(qa_app_unified_simulation_event_storage *storage) {
  free(storage->counts);
  storage->counts = NULL;
}
