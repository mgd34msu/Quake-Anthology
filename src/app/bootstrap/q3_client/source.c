/* Port of src/app/bootstrap/q3-client/source.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/q3_client/source.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
typedef struct snapshot_entry { double number; qa_q3_wire_snapshot snapshot; } snapshot_entry;
typedef struct command_entry { double sequence; char **argv; size_t argc; } command_entry;
typedef struct actor_entry { double number; qa_actor_id actor; } actor_entry;
struct qa_app_application_q3_source {
  qa_actor_id current_actor;
  qa_app_q3_source_callbacks callbacks;
  qa_q3_pres_client_command_history commands;
  snapshot_entry *snapshots; size_t snapshot_count, snapshot_capacity;
  command_entry *server_commands; size_t server_command_count, server_command_capacity;
  actor_entry *actors; size_t actor_count, actor_capacity;
  char *strings[1024];
  char *applied_strings[1024];
  double number, reliable, sequence, command_sequence, level_shot_sequence;
  double snapshot_server_bit; /* 0 | 4 */
  bool has_pending_round_time;
  double pending_round_time;
  qa_q3_base_product product;
  double time;
  double client_number;
};
static char *xstrdup(const char *s) { char *p = strdup(s != NULL ? s : ""); if (p == NULL) qa_range_error("Out of memory"); return p; }
static void set_string(char **slot, const char *value) { free(*slot); *slot = xstrdup(value); }
static void actor_set(qa_app_application_q3_source *s, double number, qa_actor_id actor) {
  for (size_t i = 0; i < s->actor_count; i++)
    if (s->actors[i].number == number) { s->actors[i].actor = actor; return; }
  if (s->actor_count == s->actor_capacity) {
    size_t cap = s->actor_capacity == 0 ? 16 : s->actor_capacity * 2;
    actor_entry *next = realloc(s->actors, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    s->actors = next; s->actor_capacity = cap;
  }
  s->actors[s->actor_count].number = number;
  s->actors[s->actor_count].actor = actor;
  s->actor_count++;
}
static bool actor_get(qa_app_application_q3_source *s, double number, qa_actor_id *out) {
  for (size_t i = 0; i < s->actor_count; i++)
    if (s->actors[i].number == number) { *out = s->actors[i].actor; return true; }
  return false;
}
static void command_free(command_entry *entry) {
  for (size_t i = 0; i < entry->argc; i++) free(entry->argv[i]);
  free(entry->argv);
}
static void push_command(qa_app_application_q3_source *s, const char *const *argv, size_t argc) {
  s->reliable += 1;
  if (s->server_command_count == s->server_command_capacity) {
    size_t cap = s->server_command_capacity == 0 ? 16 : s->server_command_capacity * 2;
    command_entry *next = realloc(s->server_commands, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    s->server_commands = next; s->server_command_capacity = cap;
  }
  command_entry *entry = &s->server_commands[s->server_command_count++];
  entry->sequence = s->reliable;
  entry->argc = argc;
  entry->argv = malloc((argc == 0 ? 1 : argc) * sizeof *entry->argv);
  if (entry->argv == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < argc; i++) entry->argv[i] = xstrdup(argv[i]);
  for (size_t i = 0; i < s->server_command_count; i++) {
    if (s->server_commands[i].sequence == s->reliable - 64) {
      command_free(&s->server_commands[i]);
      memmove(&s->server_commands[i], &s->server_commands[i + 1],
              (s->server_command_count - i - 1) * sizeof *s->server_commands);
      s->server_command_count--;
      break;
    }
  }
}
static void apply_configstrings(qa_app_application_q3_source *s, const qa_app_q3_source_presentation_state *state) {
  char *fresh[1024];
  for (size_t i = 0; i < 1024; i++) fresh[i] = xstrdup("");
  for (size_t i = 0; i < state->configstring_count; i++) {
    double index = state->configstrings[i].index;
    if (index >= 0 && index < 1024) set_string(&fresh[(size_t)index], state->configstrings[i].value);
  }
  for (size_t index = 0; index < 1024; index++) {
    if (strcmp(s->strings[index], fresh[index]) == 0) continue;
    set_string(&s->strings[index], fresh[index]);
    char index_text[32];
    snprintf(index_text, sizeof index_text, "%zu", index);
    const char *argv[3] = {"cs", index_text, fresh[index]};
    push_command(s, argv, 3);
  }
  for (size_t i = 0; i < 1024; i++) free(fresh[i]);
}
static void transport_player(const qa_q3_base_player_state_record *source, qa_q3_player_state_record *out) {
  qa_q3_product product = source->state_product == QA_Q3_BASE_PRODUCT_MISSIONPACK
    ? QA_Q3_PRODUCT_MISSIONPACK : QA_Q3_PRODUCT_BASEQ3;
  qa_q3_player_state_record_init(out, product, (int32_t)source->pm_type,
                                 (int32_t)source->weapon, (int32_t)source->weapon_state);
  out->command_time = (int32_t)source->command_time;
  out->pm_type = (int32_t)source->pm_type;
  out->bob_cycle = (int32_t)source->bob_cycle;
  out->pm_flags = (int32_t)source->pm_flags;
  out->pm_time = (int32_t)source->pm_time;
  out->origin = qa_q3_base_player_state_record_origin(source);
  out->velocity = qa_q3_base_player_state_record_velocity(source);
  out->weapon_time = (int32_t)source->weapon_time;
  out->gravity = (int32_t)source->gravity;
  out->speed = (int32_t)source->speed;
  out->delta_angles = source->delta_angles;
  out->ground_entity_num = (int32_t)source->ground_entity_num;
  out->legs_timer = (int32_t)source->legs_timer;
  out->legs_anim = (int32_t)source->legs_anim;
  out->torso_timer = (int32_t)source->torso_timer;
  out->torso_anim = (int32_t)source->torso_anim;
  out->movement_dir = (int32_t)source->movement_dir;
  out->grapple_point = source->grapple_point;
  out->e_flags = (int32_t)source->e_flags;
  out->event_sequence = (int32_t)source->event_sequence;
  out->external_event = (int32_t)source->external_event;
  out->external_event_parm = (int32_t)source->external_event_parm;
  out->external_event_time = (int32_t)source->external_event_time;
  out->client_num = (int32_t)source->client_num;
  out->weapon = (int32_t)source->weapon;
  out->weapon_state = (int32_t)source->weapon_state;
  out->viewangles = source->viewangles;
  out->viewheight = (int32_t)source->viewheight;
  out->damage_event = (int32_t)source->damage_event;
  out->damage_yaw = (int32_t)source->damage_yaw;
  out->damage_pitch = (int32_t)source->damage_pitch;
  out->damage_count = (int32_t)source->damage_count;
  out->generic1 = (int32_t)source->generic1;
  out->loop_sound = (int32_t)source->loop_sound;
  out->jumppad_ent = (int32_t)source->jumppad_ent;
  out->ping = (int32_t)source->ping;
  out->pmove_framecount = (int32_t)source->pmove_framecount;
  out->jumppad_frame = (int32_t)source->jumppad_frame;
  out->entity_event_sequence = (int32_t)source->entity_event_sequence;
  struct { const qa_q3_base_player_state_slots *in; int32_t *out; size_t count; } slots[6] = {
    {&source->events, out->events, 2}, {&source->event_parms, out->event_parms, 2},
    {&source->stats, out->stats, 16}, {&source->persistant, out->persistant, 16},
    {&source->powerups, out->powerups, 16}, {&source->ammo, out->ammo, 16}};
  for (size_t k = 0; k < 6; k++) {
    int32_t *copy = qa_q3_base_player_state_slots_copy(slots[k].in);
    size_t take = slots[k].count;
    if ((double)take > slots[k].in->length) take = (size_t)slots[k].in->length;
    for (size_t i = 0; i < take; i++) slots[k].out[i] = copy[i];
    free(copy);
  }
}
qa_app_application_q3_source *qa_app_application_q3_source_create(
    qa_actor_id current_actor, const qa_app_q3_source_presentation_state *initial,
    qa_app_q3_source_callbacks callbacks) {
  qa_app_application_q3_source *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  s->current_actor = current_actor;
  s->callbacks = callbacks;
  qa_q3_pres_client_command_history_init(&s->commands);
  s->sequence = -1;
  s->command_sequence = -1;
  s->level_shot_sequence = -1;
  for (size_t i = 0; i < 1024; i++) { s->strings[i] = xstrdup(""); s->applied_strings[i] = xstrdup(""); }
  const qa_app_q3_source_client_row *client = NULL;
  for (size_t i = 0; i < initial->client_count; i++)
    if (qa_same_actor(initial->clients[i].actor, current_actor)) client = &initial->clients[i];
  if (client == NULL) qa_range_error("Q3 presentation seat has no source player");
  s->client_number = client->slot;
  s->product = initial->product;
  for (size_t i = 0; i < initial->configstring_count; i++) {
    double index = initial->configstrings[i].index;
    if (index >= 0 && index < 1024) {
      set_string(&s->strings[(size_t)index], initial->configstrings[i].value);
      set_string(&s->applied_strings[(size_t)index], initial->configstrings[i].value);
    }
  }
  qa_app_application_q3_source_receive(s, initial, NULL, 0, NULL, 0);
  return s;
}
void qa_app_application_q3_source_destroy(qa_app_application_q3_source *source) {
  if (source == NULL) return;
  for (size_t i = 0; i < source->snapshot_count; i++) qa_q3_wire_snapshot_destroy(&source->snapshots[i].snapshot);
  free(source->snapshots);
  for (size_t i = 0; i < source->server_command_count; i++) command_free(&source->server_commands[i]);
  free(source->server_commands);
  free(source->actors);
  for (size_t i = 0; i < 1024; i++) { free(source->strings[i]); free(source->applied_strings[i]); }
  free(source);
}
const char *qa_app_application_q3_source_mode(const qa_app_application_q3_source *source) { (void)source; return "live"; }
qa_actor_id qa_app_application_q3_source_actor(const qa_app_application_q3_source *source) { return source->current_actor; }
double qa_app_application_q3_source_client_number(const qa_app_application_q3_source *source) { return source->client_number; }
double qa_app_application_q3_source_time(const qa_app_application_q3_source *source) { return source->time; }
qa_q3_pres_client_command_history *qa_app_application_q3_source_commands(qa_app_application_q3_source *source) { return &source->commands; }
void qa_app_application_q3_source_assert_can_restart_round(qa_app_application_q3_source *source) {
  if (source->has_pending_round_time) qa_range_error("Local Q3 round restart is already pending");
}
void qa_app_application_q3_source_begin_round_restart(qa_app_application_q3_source *source, double bit,
    const qa_app_q3_source_presentation_state *state) {
  qa_app_application_q3_source_assert_can_restart_round(source);
  if (bit == source->snapshot_server_bit || state->product != source->product || state->time < source->time)
    qa_range_error("Invalid local Q3 restart epoch");
  apply_configstrings(source, state);
  const char *argv[1] = {"map_restart"};
  push_command(source, argv, 1);
  source->snapshot_server_bit = bit;
  source->has_pending_round_time = true;
  source->pending_round_time = state->time;
  source->actor_count = 0;
}
void qa_app_application_q3_source_validate_round_actor(qa_app_application_q3_source *source, qa_actor_id actor,
    const qa_app_q3_source_presentation_state *state) {
  bool listed = false;
  for (size_t i = 0; i < state->client_count; i++)
    if (state->clients[i].slot == source->client_number && qa_same_actor(state->clients[i].actor, actor)) listed = true;
  if (!source->has_pending_round_time
      || actor.session.token != source->current_actor.session.token
      || qa_same_actor(actor, source->current_actor)
      || state->product != source->product || state->time < source->pending_round_time || !listed)
    qa_range_error("Invalid local Q3 round actor binding");
}
void qa_app_application_q3_source_rebind_round(qa_app_application_q3_source *source, qa_actor_id actor,
    const qa_app_q3_source_presentation_state *state) {
  qa_app_application_q3_source_validate_round_actor(source, actor, state);
  source->current_actor = actor;
  source->has_pending_round_time = false;
}
void qa_app_application_q3_source_current(const qa_app_application_q3_source *source,
                                          double *out_number, double *out_server_time) {
  *out_number = source->number;
  *out_server_time = source->time;
}
bool qa_app_application_q3_source_read(qa_app_application_q3_source *source, double number,
                                       const qa_q3_wire_snapshot **out) {
  for (size_t i = 0; i < source->snapshot_count; i++)
    if (source->snapshots[i].number == number) { *out = &source->snapshots[i].snapshot; return true; }
  return false;
}
const char *const *qa_app_application_q3_source_game_state(qa_app_application_q3_source *source, size_t *out_count) {
  *out_count = 1024;
  return (const char *const *)source->applied_strings;
}
bool qa_app_application_q3_source_get_server_command(qa_app_application_q3_source *source, double sequence,
                                                     const char *const **argv_out, size_t *argc_out) {
  command_entry *command = NULL;
  for (size_t i = 0; i < source->server_command_count; i++)
    if (source->server_commands[i].sequence == sequence) command = &source->server_commands[i];
  if (command == NULL) return false;
  if (command->argc > 0 && strcmp(command->argv[0], "clientLevelShot") == 0 && sequence > source->level_shot_sequence) {
    source->level_shot_sequence = sequence;
    if (source->callbacks.level_shot != NULL) source->callbacks.level_shot(source->callbacks.ctx);
  }
  if (command->argc > 0 && strcmp(command->argv[0], "cs") == 0) {
    double index = command->argc > 1 ? strtod(command->argv[1], NULL) : NAN;
    const char *value = command->argc > 2 ? command->argv[2] : NULL;
    if (index != trunc(index) || index < 0 || index >= 1024 || value == NULL)
      qa_range_error("Invalid local source configstring command");
    set_string(&source->applied_strings[(size_t)index], value);
  }
  *argv_out = (const char *const *)command->argv;
  *argc_out = command->argc;
  return true;
}
qa_actor_id qa_app_application_q3_source_actor_at(qa_app_application_q3_source *source, double number) {
  if (source->has_pending_round_time) qa_range_error("Local Q3 actor binding is suspended for restart");
  qa_actor_id actor;
  if (actor_get(source, number, &actor)) return actor;
  if (source->callbacks.source_actor(source->callbacks.ctx, number, &actor)) return actor;
  qa_range_error("Q3 sound entity %g has no source actor", number);
}
bool qa_app_application_q3_source_sound_actor(qa_app_application_q3_source *source, double number, qa_actor_id *out) {
  if (source->has_pending_round_time) return false;
  if (actor_get(source, number, out)) return true;
  return source->callbacks.source_actor(source->callbacks.ctx, number, out);
}
void qa_app_application_q3_source_receive_events(qa_app_application_q3_source *source,
    const qa_app_q3_source_presentation_event *events, size_t event_count) {
  for (size_t i = 0; i < event_count; i++) {
    const qa_app_q3_source_presentation_event *event = &events[i];
    if (event->sequence <= source->sequence) continue;
    source->sequence = event->sequence;
    if (event->kind == NULL || strcmp(event->kind, "q3-source") != 0) continue;
    if (event->event_kind != NULL && strcmp(event->event_kind, "server-command") == 0
        && (event->client < 0 || event->client == source->client_number)) {
      qa_command_tokens tokens;
      if (qa_tokenize_command(event->text, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &tokens)) {
        push_command(source, (const char *const *)tokens.argv, tokens.argc);
        qa_command_tokens_destroy(&tokens);
      }
    }
  }
}
void qa_app_application_q3_source_receive(qa_app_application_q3_source *source,
    const qa_app_q3_source_presentation_state *state,
    const qa_app_q3_source_presentation_event *events, size_t event_count,
    const qa_actor_command *commands, size_t command_count) {
  if (source->has_pending_round_time)
    qa_range_error("Local Q3 restart requires actor rebinding before publication");
  if (state->time < source->time) qa_range_error("Q3 presentation world time rewound");
  const qa_app_q3_source_client_row *player = NULL;
  for (size_t i = 0; i < state->client_count; i++)
    if (qa_same_actor(state->clients[i].actor, source->current_actor)) player = &state->clients[i];
  if (player == NULL || player->slot != source->client_number)
    qa_range_error("Q3 presentation seat changed source player");
  for (size_t i = 0; i < state->entity_count; i++)
    actor_set(source, state->entities[i].state->number, state->entities[i].actor);
  for (size_t i = 0; i < state->client_count; i++)
    actor_set(source, state->clients[i].slot, state->clients[i].actor);
  apply_configstrings(source, state);
  qa_app_application_q3_source_receive_events(source, events, event_count);
  for (size_t i = 0; i < command_count; i++) {
    const qa_actor_command *input = &commands[i];
    if (!qa_same_actor(input->actor, source->current_actor) || input->sequence <= source->command_sequence) continue;
    source->command_sequence = input->sequence;
    if (source->callbacks.prediction_command != NULL) {
      qa_q3_base_user_command translated = source->callbacks.prediction_command(source->callbacks.ctx, *input, state->time);
      qa_q3_pres_client_command_history_append(&source->commands, translated);
    } else if (input->command.kind == QA_USER_COMMAND_Q3) {
      const qa_q3_user_command *command = &input->command.as.q3;
      qa_q3_base_user_command appended = {command->server_time_milliseconds,
        {command->angle_words.values[0], command->angle_words.values[1], command->angle_words.values[2]},
        command->buttons, command->weapon, command->forward_move, command->right_move, command->up_move};
      qa_q3_pres_client_command_history_append(&source->commands, appended);
    } else {
      qa_range_error("Selected foreign movement requires its private cgame prediction command adapter");
    }
  }
  if (source->number > 0 && state->time == source->time) return;
  source->time = state->time;
  qa_q3_player_state_record player_state;
  transport_player(player->state, &player_state);
  qa_q3_visible_entities visible;
  memset(&visible, 0, sizeof visible);
  source->callbacks.select(source->callbacks.ctx, &player_state, state, &visible);
  double previous = source->number;
  source->number += 1;
  if (source->snapshot_count == source->snapshot_capacity) {
    size_t cap = source->snapshot_capacity == 0 ? 16 : source->snapshot_capacity * 2;
    snapshot_entry *next = realloc(source->snapshots, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    source->snapshots = next; source->snapshot_capacity = cap;
  }
  snapshot_entry *entry = &source->snapshots[source->snapshot_count++];
  entry->number = source->number;
  qa_q3_wire_snapshot *snapshot = &entry->snapshot;
  memset(snapshot, 0, sizeof *snapshot);
  snapshot->message_number = (int64_t)source->number;
  snapshot->server_time = (int64_t)state->time;
  snapshot->delta_number = previous == 0 ? -1 : (int64_t)previous;
  snapshot->flags = (int64_t)source->snapshot_server_bit;
  snapshot->server_command_number = (int64_t)source->reliable;
  snapshot->parse_entities_number = 0;
  size_t mask = visible.area_mask_len < 32 ? visible.area_mask_len : 32;
  if (visible.area_mask != NULL && mask > 0) memcpy(snapshot->area_mask, visible.area_mask, mask);
  snapshot->area_mask_len = 32;
  snapshot->player_state = player_state;
  snapshot->entity_count = visible.entity_count;
  if (visible.entity_count > 0) {
    snapshot->entities = malloc(visible.entity_count * sizeof *snapshot->entities);
    if (snapshot->entities == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < visible.entity_count; i++) snapshot->entities[i] = *visible.entities[i];
  }
  for (size_t i = 0; i < source->snapshot_count; i++) {
    if (source->snapshots[i].number == source->number - 32) {
      qa_q3_wire_snapshot_destroy(&source->snapshots[i].snapshot);
      memmove(&source->snapshots[i], &source->snapshots[i + 1],
              (source->snapshot_count - i - 1) * sizeof *source->snapshots);
      source->snapshot_count--;
      break;
    }
  }
}
