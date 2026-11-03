/* Port of src/app/bootstrap/simulation/types.ts. TS has types only; C adds
 * the value semantics TS gets from the garbage collector for
 * SimulationPresentationEvent: a deep copy (an owned value whose strings,
 * arrays and payloads live in one storage block) and its release. The copy
 * walks a declarative shape of every pointer each payload type holds.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/types.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/content/composition/q1/types.h"
#include "qa/content/composition/q2/types.h"
#include "qa/content/q1/addons/context.h"
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

/* ---- shapes: where each type keeps pointers ---- */
typedef enum shape_field_kind {
  SHAPE_STRING,  /* const char * (fixed consecutive pointers) */
  SHAPE_INLINE,  /* embedded struct */
  SHAPE_POINTER, /* const T * to one value (NULL allowed) */
  SHAPE_ARRAY,   /* const T * + size_t count */
  SHAPE_STRINGS  /* const char *const * + size_t count */
} shape_field_kind;
typedef struct shape shape;
typedef struct shape_field {
  shape_field_kind kind;
  size_t offset, count_offset, size, fixed;
  const shape *shape;
} shape_field;
struct shape { const shape_field *fields; size_t count; };
#define SHAPE(name, ...) \
  static const shape_field name##_fields[] = {__VA_ARGS__}; \
  static const shape name = {name##_fields, sizeof name##_fields / sizeof name##_fields[0]}
#define STR(T, f) {SHAPE_STRING, offsetof(T, f), 0, 0, 1, NULL}
#define STRN(T, f, n) {SHAPE_STRING, offsetof(T, f), 0, 0, n, NULL}
#define IN(T, f, s) {SHAPE_INLINE, offsetof(T, f), 0, 0, 1, &s}
#define PTR(T, f, E, s) {SHAPE_POINTER, offsetof(T, f), 0, sizeof(E), 1, s}
#define ARR(T, f, n, E, s) {SHAPE_ARRAY, offsetof(T, f), offsetof(T, n), sizeof(E), 1, s}
#define STRS(T, f, n) {SHAPE_STRINGS, offsetof(T, f), offsetof(T, n), 0, 1, NULL}

SHAPE(actor_shape, STR(qa_actor_id, session.name));
SHAPE(owner_shape, STR(qa_presentation_owner, provider));
SHAPE(double_view_shape, ARR(qa_double_view, values, count, double, NULL));

/* Q1Event. */
SHAPE(q1_msg_arg_shape, STR(qa_q1_msg_arg, text));
SHAPE(q1_message_part_shape, STR(qa_q1_message_part, text),
      ARR(qa_q1_message_part, args, arg_count, qa_q1_msg_arg, &q1_msg_arg_shape));
SHAPE(q1_attack_shape, STR(qa_q1_character_attack, kind));
SHAPE(q1_event_shape, STR(qa_q1_event, kind), IN(qa_q1_event, actor, actor_shape),
      IN(qa_q1_event, player, actor_shape), STR(qa_q1_event, sound_channel), STR(qa_q1_event, path),
      STR(qa_q1_event, text), STR(qa_q1_event, effect), STR(qa_q1_event, view_model),
      STR(qa_q1_event, pattern), STR(qa_q1_event, map), STR(qa_q1_event, id),
      ARR(qa_q1_event, args, arg_count, qa_q1_msg_arg, &q1_msg_arg_shape),
      ARR(qa_q1_event, parts, part_count, qa_q1_message_part, &q1_message_part_shape),
      STR(qa_q1_event, beam_style), STR(qa_q1_event, weapon), STR(qa_q1_event, powerup),
      IN(qa_q1_event, attack, q1_attack_shape));

/* Q1IntermissionResult / Q1SourceFinale (map and text are inline). */
SHAPE(q1_intermission_shape, STR(qa_q1_intermission_result, kind));

/* Q1CompositionEvent. */
SHAPE(q1ad_event_shape, IN(qa_q1ad_event, actor, actor_shape), IN(qa_q1ad_event, player, actor_shape),
      STR(qa_q1ad_event, text));
SHAPE(comp_userinfo_shape, STR(qa_comp_userinfo_entry, key), STR(qa_comp_userinfo_entry, value));
SHAPE(comp_client_shape, IN(qa_comp_q1_client_snapshot, actor, actor_shape),
      STR(qa_comp_q1_client_snapshot, name),
      ARR(qa_comp_q1_client_snapshot, userinfo, userinfo_count, qa_comp_userinfo_entry, &comp_userinfo_shape));
SHAPE(comp_choice_shape, STR(qa_comp_q1_prompt_choice, label));
SHAPE(comp_q1_event_shape, PTR(qa_comp_q1_event, addon_event, qa_q1ad_event, &q1ad_event_shape),
      IN(qa_comp_q1_event, client, comp_client_shape), IN(qa_comp_q1_event, actor, actor_shape),
      PTR(qa_comp_q1_event, status, qa_q1ad_ctf_status, NULL), STR(qa_comp_q1_event, team),
      STR(qa_comp_q1_event, title),
      ARR(qa_comp_q1_event, choices, choice_count, qa_comp_q1_prompt_choice, &comp_choice_shape),
      STR(qa_comp_q1_event, action), STR(qa_comp_q1_event, text),
      PTR(qa_comp_q1_event, finale, qa_q1_intermission_result, &q1_intermission_shape));

/* Q2PresentationEvent. */
SHAPE(q2_field_pair_shape, STR(qa_q2_field_pair, key), STR(qa_q2_field_pair, value));
SHAPE(q2_spawn_fields_shape, STR(qa_q2_spawn_fields, classname),
      ARR(qa_q2_spawn_fields, values, value_count, qa_q2_field_pair, &q2_field_pair_shape));
SHAPE(q2_event_shape, STR(qa_q2_presentation_event, kind), IN(qa_q2_presentation_event, actor, actor_shape),
      STR(qa_q2_presentation_event, path), STRN(qa_q2_presentation_event, attached_models, 3),
      STR(qa_q2_presentation_event, loop), STR(qa_q2_presentation_event, loop_owner),
      STR(qa_q2_presentation_event, text), STR(qa_q2_presentation_event, level),
      STR(qa_q2_presentation_event, pattern), STR(qa_q2_presentation_event, track),
      STR(qa_q2_presentation_event, effect), IN(qa_q2_presentation_event, player, actor_shape),
      STR(qa_q2_presentation_event, item), STR(qa_q2_presentation_event, icon),
      STR(qa_q2_presentation_event, name), STR(qa_q2_presentation_event, message),
      PTR(qa_q2_presentation_event, fields, qa_q2_spawn_fields, &q2_spawn_fields_shape));

/* Q2WeaponEvent. */
SHAPE(q2w_event_shape, PTR(qa_q2w_event, actor, qa_actor_id, &actor_shape), STR(qa_q2w_event, effect),
      STR(qa_q2w_event, weapon), STR(qa_q2w_event, model), STR(qa_q2w_event, priority));

/* Q2CompositionEvent. */
SHAPE(ctf_score_row_shape, STR(qa_q2_ctf_score_row, name));
SHAPE(ctf_menu_entry_shape, STR(qa_q2_ctf_menu_entry, label), STR(qa_q2_ctf_menu_entry, action));
SHAPE(ctf_event_shape, STR(qa_q2_ctf_event, kind), IN(qa_q2_ctf_event, actor, actor_shape),
      ARR(qa_q2_ctf_event, red, red_count, qa_q2_ctf_score_row, &ctf_score_row_shape),
      ARR(qa_q2_ctf_event, blue, blue_count, qa_q2_ctf_score_row, &ctf_score_row_shape),
      ARR(qa_q2_ctf_event, spectators, spectator_count, qa_q2_ctf_score_row, &ctf_score_row_shape),
      STR(qa_q2_ctf_event, layout), STR(qa_q2_ctf_event, title), STR(qa_q2_ctf_event, text),
      STR(qa_q2_ctf_event, match),
      ARR(qa_q2_ctf_event, entries, entry_count, qa_q2_ctf_menu_entry, &ctf_menu_entry_shape),
      STRN(qa_q2_ctf_event, flag_states, 2), STR(qa_q2_ctf_event, tech),
      IN(qa_q2_ctf_event, id_target, actor_shape));
SHAPE(lmctf_menu_entry_shape, STR(qa_lmctf_menu_entry, label), STR(qa_lmctf_menu_entry, command));
SHAPE(lmctf_score_row_shape, IN(qa_lmctf_score_row, actor, actor_shape), STR(qa_lmctf_score_row, name));
SHAPE(lmctf_event_shape, STR(qa_lmctf_event, kind), IN(qa_lmctf_event, actor, actor_shape),
      STR(qa_lmctf_event, title),
      ARR(qa_lmctf_event, entries, entry_count, qa_lmctf_menu_entry, &lmctf_menu_entry_shape),
      ARR(qa_lmctf_event, rows, row_count, qa_lmctf_score_row, &lmctf_score_row_shape),
      STR(qa_lmctf_event, layout), STR(qa_lmctf_event, rune), IN(qa_lmctf_event, victim, actor_shape),
      STR(qa_lmctf_event, name));
SHAPE(q2mp_player_effect_shape, IN(qa_q2mp_player_effect, actor, actor_shape),
      IN(qa_q2mp_player_effect, sphere, actor_shape));
SHAPE(q2_composition_shape, PTR(qa_q2_composition_event, ctf, qa_q2_ctf_event, &ctf_event_shape),
      PTR(qa_q2_composition_event, lmctf, qa_lmctf_event, &lmctf_event_shape),
      IN(qa_q2_composition_event, actor, actor_shape),
      PTR(qa_q2_composition_event, player_effect, qa_q2mp_player_effect, &q2mp_player_effect_shape),
      PTR(qa_q2_composition_event, entity_event, qa_q2mp_entity_event, NULL));

/* Q2RereleaseEvent. */
SHAPE(world_text_shape, STR(qa_world_text, text), STR(qa_world_text, content));
SHAPE(level_entry_shape, STR(qa_q2_rerelease_level_entry, map), STR(qa_q2_rerelease_level_entry, name));
SHAPE(q2_rerelease_shape, STR(qa_q2_rerelease_event, kind),
      ARR(qa_q2_rerelease_event, lines, line_count, qa_debug_line, NULL),
      IN(qa_q2_rerelease_event, text_value, world_text_shape), IN(qa_q2_rerelease_event, actor, actor_shape),
      STR(qa_q2_rerelease_event, level), STR(qa_q2_rerelease_event, text),
      STRS(qa_q2_rerelease_event, args, arg_count), STR(qa_q2_rerelease_event, primary),
      STR(qa_q2_rerelease_event, secondary), STR(qa_q2_rerelease_event, hand),
      STR(qa_q2_rerelease_event, image),
      ARR(qa_q2_rerelease_event, levels, level_count, qa_q2_rerelease_level_entry, &level_entry_shape),
      STR(qa_q2_rerelease_event, value), STR(qa_q2_rerelease_event, map), STR(qa_q2_rerelease_event, id),
      STR(qa_q2_rerelease_event, sky_name), IN(qa_q2_rerelease_event, target, actor_shape),
      STR(qa_q2_rerelease_event, name), IN(qa_q2_rerelease_event, item, actor_shape));

/* Q2PlayerEvent. */
SHAPE(q2_player_view_shape, STR(qa_q2_base_player_view, selected_item), STR(qa_q2_base_player_view, timer_item));
SHAPE(q2_score_row_shape, STR(qa_q2_score_row, name));
SHAPE(count_policy_shape, STR(qa_inventory_count_policy, kind), STR(qa_inventory_count_policy, arithmetic));
SHAPE(inventory_entry_shape, STR(qa_inventory_entry, item), IN(qa_inventory_entry, count_policy, count_policy_shape));
SHAPE(item_label_shape, STR(qa_q2_player_item_label, item), STR(qa_q2_player_item_label, name));
SHAPE(q2_player_shape, STR(qa_q2_player_event, kind), IN(qa_q2_player_event, target, actor_shape),
      STR(qa_q2_player_event, level), STR(qa_q2_player_event, text), IN(qa_q2_player_event, actor, actor_shape),
      STR(qa_q2_player_event, name), STR(qa_q2_player_event, skin),
      IN(qa_q2_player_event, view, q2_player_view_shape),
      ARR(qa_q2_player_event, rows, row_count, qa_q2_score_row, &q2_score_row_shape),
      IN(qa_q2_player_event, killer, actor_shape),
      ARR(qa_q2_player_event, entries, entry_count, qa_inventory_entry, &inventory_entry_shape),
      STR(qa_q2_player_event, selected),
      ARR(qa_q2_player_event, labels, label_count, qa_q2_player_item_label, &item_label_shape),
      IN(qa_q2_player_event, chase_target, actor_shape));

/* Q3SourceEvent. */
SHAPE(module_identity_shape, STR(qa_module_identity, id), STR(qa_module_identity, artifact_path),
      STR(qa_module_identity, digest), STR(qa_module_identity, revision));
SHAPE(q3_player_state_shape, IN(qa_q3_player_state, stats, double_view_shape),
      IN(qa_q3_player_state, persistent, double_view_shape), IN(qa_q3_player_state, powerups, double_view_shape),
      IN(qa_q3_player_state, ammo, double_view_shape));
SHAPE(q3_entity_event_shape, STR(qa_app_q3_source_entity_event, kind),
      IN(qa_app_q3_source_entity_event, actor, actor_shape));
SHAPE(q3_player_event_shape, STR(qa_app_q3_source_player_event, kind),
      IN(qa_app_q3_source_player_event, actor, actor_shape),
      IN(qa_app_q3_source_player_event, source_module, module_identity_shape),
      STR(qa_app_q3_source_player_event, source_abi_profile),
      IN(qa_app_q3_source_player_event, player_state, q3_player_state_shape));
SHAPE(q3_source_shape, STR(qa_app_q3_source_event, text), STR(qa_app_q3_source_event, reason),
      STR(qa_app_q3_source_event, execution), STR(qa_app_q3_source_event, value),
      IN(qa_app_q3_source_event, actor, actor_shape), STR(qa_app_q3_source_event, path),
      IN(qa_app_q3_source_event, entity, q3_entity_event_shape),
      IN(qa_app_q3_source_event, player, q3_player_event_shape));

/* SourcePresentationEvent members, by kind. */
typedef qa_app_sim_presentation_event E;
SHAPE(owner_event_shape, STR(E, presentation_owner.kind), IN(E, presentation_owner.owner, owner_shape));
SHAPE(debug_graph_shape, STR(E, debug_graph.kind));
SHAPE(q1_kind_shape, PTR(E, q1, qa_q1_event, &q1_event_shape));
SHAPE(q1_sky_shape, STR(E, q1_sky.kind), STR(E, q1_sky.name));
SHAPE(q1_client_shape, STR(E, q1_client.kind), STR(E, q1_client.text));
SHAPE(q1_session_shape, STR(E, q1_session.kind));
SHAPE(q1_fog_shape, STR(E, q1_fog.kind), IN(E, q1_fog.player, actor_shape));
SHAPE(music_shape, STR(E, music.kind));
SHAPE(q1_composition_kind_shape, PTR(E, q1_composition, qa_comp_q1_event, &comp_q1_event_shape));
SHAPE(q1_level_kind_shape, PTR(E, q1_level, qa_q1_intermission_result, &q1_intermission_shape));
SHAPE(q2_kind_shape, PTR(E, q2, qa_q2_presentation_event, &q2_event_shape));
SHAPE(q2_weapon_kind_shape, PTR(E, q2_weapon, qa_q2w_event, &q2w_event_shape));
SHAPE(view_reset_shape, STR(E, view_reset.reason), IN(E, view_reset.actor, actor_shape));
SHAPE(q2_composition_kind_shape, PTR(E, q2_composition, qa_q2_composition_event, &q2_composition_shape));
SHAPE(q2_rerelease_kind_shape, PTR(E, q2_rerelease, qa_q2_rerelease_event, &q2_rerelease_shape));
SHAPE(q2_player_kind_shape, PTR(E, q2_player, qa_q2_player_event, &q2_player_shape));
SHAPE(q3_character_shape, IN(E, q3_character.actor, actor_shape));
SHAPE(q3_source_kind_shape, PTR(E, q3_source, qa_app_q3_source_event, &q3_source_shape));
SHAPE(stamp_content_shape, STR(E, content));
SHAPE(stamp_owner_shape, IN(E, owner, owner_shape));
SHAPE(stamp_recipient_shape, IN(E, recipient, actor_shape));

typedef struct kind_entry { const char *name; const shape *shape; } kind_entry;
static const kind_entry kinds[] = {
  {"presentation-owner", &owner_event_shape}, {"debug-graph", &debug_graph_shape},
  {"q1", &q1_kind_shape}, {"q1-sky", &q1_sky_shape}, {"q1-client", &q1_client_shape},
  {"q1-session", &q1_session_shape}, {"q1-fog", &q1_fog_shape}, {"music", &music_shape},
  {"q1-composition", &q1_composition_kind_shape}, {"q1-level", &q1_level_kind_shape},
  {"q2", &q2_kind_shape}, {"q2-weapon", &q2_weapon_kind_shape}, {"view-reset", &view_reset_shape},
  {"q2-composition", &q2_composition_kind_shape}, {"q2-rerelease", &q2_rerelease_kind_shape},
  {"q2-player", &q2_player_kind_shape}, {"q3-character", &q3_character_shape},
  {"q3-source", &q3_source_kind_shape},
};
static const kind_entry *kind_of(const char *kind) {
  if (kind == NULL) return NULL;
  for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++)
    if (strcmp(kinds[i].name, kind) == 0) return &kinds[i];
  return NULL;
}
bool qa_app_sim_source_presentation_kind_known(const char *kind) { return kind_of(kind) != NULL; }

/* ---- the walk: measure, then copy into one block ---- */
typedef struct arena { char *base; size_t used; } arena; /* base NULL: measuring */
static void *arena_take(arena *a, size_t size) {
  size_t aligned = (a->used + (_Alignof(max_align_t) - 1)) & ~(size_t)(_Alignof(max_align_t) - 1);
  a->used = aligned + size;
  return a->base ? a->base + aligned : NULL;
}
static const void *load_pointer(const char *at) { const void *p; memcpy(&p, at, sizeof p); return p; }
static void store_pointer(char *at, const void *p) { if (at) memcpy(at, &p, sizeof p); }
static const char *copy_string(arena *a, const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = arena_take(a, length);
  if (copy) memcpy(copy, text, length);
  return copy;
}
static void walk(arena *a, const char *source, char *target, const shape *s);
static void walk_field(arena *a, const char *source, char *target, const shape_field *f) {
  const char *from = source + f->offset;
  char *to = target ? target + f->offset : NULL;
  switch (f->kind) {
  case SHAPE_STRING:
    for (size_t i = 0; i < f->fixed; i++) {
      const char *text = load_pointer(from + i * sizeof text);
      if (text) store_pointer(to ? to + i * sizeof text : NULL, copy_string(a, text));
    }
    break;
  case SHAPE_INLINE: walk(a, from, to, f->shape); break;
  case SHAPE_POINTER: {
    const char *value = load_pointer(from);
    if (!value) break;
    char *copy = arena_take(a, f->size);
    if (copy) memcpy(copy, value, f->size);
    if (f->shape) walk(a, value, copy, f->shape);
    store_pointer(to, copy);
    break;
  }
  case SHAPE_ARRAY: {
    const char *values = load_pointer(from);
    if (!values) break;
    size_t count; memcpy(&count, source + f->count_offset, sizeof count);
    /* An empty array keeps a non-NULL pointer: TS [] is not undefined. */
    char *copy = arena_take(a, f->size * count);
    if (copy && count) memcpy(copy, values, f->size * count);
    if (f->shape)
      for (size_t i = 0; i < count; i++) walk(a, values + i * f->size, copy ? copy + i * f->size : NULL, f->shape);
    store_pointer(to, copy);
    break;
  }
  case SHAPE_STRINGS: {
    const char *values = load_pointer(from);
    if (!values) break;
    size_t count; memcpy(&count, source + f->count_offset, sizeof count);
    char *copy = arena_take(a, sizeof(const char *) * count);
    for (size_t i = 0; i < count; i++) {
      const char *text = load_pointer(values + i * sizeof text);
      const char *owned = text ? copy_string(a, text) : NULL;
      store_pointer(copy ? copy + i * sizeof text : NULL, owned);
    }
    store_pointer(to, copy);
    break;
  }
  }
}
static void walk(arena *a, const char *source, char *target, const shape *s) {
  for (size_t i = 0; i < s->count; i++) walk_field(a, source, target, &s->fields[i]);
}
static void walk_event(arena *a, const E *source, E *target, const kind_entry *kind) {
  const char *from = (const char *)source;
  char *to = (char *)target;
  walk(a, from, to, &stamp_content_shape);
  if (source->has_owner) walk(a, from, to, &stamp_owner_shape);
  if (source->has_recipient) walk(a, from, to, &stamp_recipient_shape);
  walk(a, from, to, kind->shape);
}

qa_app_sim_source_presentation_event qa_app_sim_presentation_event_source(const qa_app_sim_presentation_event *event) {
  qa_app_sim_source_presentation_event source;
  memset(&source, 0, sizeof source);
  source.kind = event->kind;
  /* The union members share one layout prefix position: copy the union. */
  memcpy((char *)&source + offsetof(qa_app_sim_source_presentation_event, presentation_owner),
         (const char *)event + offsetof(qa_app_sim_presentation_event, presentation_owner),
         sizeof source - offsetof(qa_app_sim_source_presentation_event, presentation_owner));
  return source;
}
qa_app_sim_presentation_event qa_app_sim_presentation_event_view(const qa_app_sim_source_presentation_event *source) {
  qa_app_sim_presentation_event event;
  memset(&event, 0, sizeof event);
  event.kind = source->kind;
  memcpy((char *)&event + offsetof(qa_app_sim_presentation_event, presentation_owner),
         (const char *)source + offsetof(qa_app_sim_source_presentation_event, presentation_owner),
         sizeof *source - offsetof(qa_app_sim_source_presentation_event, presentation_owner));
  return event;
}
bool qa_app_sim_presentation_event_copy(qa_app_sim_presentation_event *out, const qa_app_sim_presentation_event *source) {
  const kind_entry *kind = kind_of(source->kind);
  if (!kind) qa_range_error("Unknown source presentation event kind %s", source->kind ? source->kind : "(null)");
  arena measure = {NULL, 0};
  walk_event(&measure, source, NULL, kind);
  char *block = malloc(measure.used ? measure.used : 1);
  if (!block) { memset(out, 0, sizeof *out); return false; }
  *out = *source;
  out->kind = kind->name;
  out->storage = block;
  arena fill = {block, 0};
  walk_event(&fill, source, out, kind);
  return true;
}
void qa_app_sim_presentation_event_release(qa_app_sim_presentation_event *event) {
  if (!event->storage) return;
  free(event->storage);
  memset(event, 0, sizeof *event);
}
