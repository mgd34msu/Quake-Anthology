/* Port of src/app/bootstrap/simulation/runtime.ts, SharedSimulation phase B1
 * "worlds and rules" (docs/runtime_layout.md): foreign-world adjuncts,
 * selected and guest monsters, the native Q2 guest bridge, the shared match
 * rules (tag, LMCTF/CTF, DeathBall, Q3 horde), combat registration and the
 * source accessors of that region. TS throws become qa_raise("Error", ...)
 * with the TS message. GPL-2.0-or-later. */
#include "shared_simulation_internal.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/types.h"
#include "qa/app/bootstrap/simulation/provider_frames.h"
#include "qa/app/bootstrap/simulation/runtime.h"
#include "qa/app/bootstrap/simulation/monster_sources.h"
#include "qa/app/bootstrap/simulation/monster_placement.h"
#include "qa/app/bootstrap/simulation/q3_commands.h"
#include "qa/content/catalog/monsters.h"
#include "qa/content/q1/base/monsters.h"
#include "qa/content/q2/rerelease/players.h"
#include "qa/content/q1/foundation/runtime.h"
#include "qa/content/composition/q1_hipnotic_supply.h"
#include "qa/content/composition/q1_q2_supply.h"
#include "qa/content/composition/q1_q3_supply.h"
#include "qa/content/monsters/definitions.h"
#include "qa/content/q1/base/creatures.h"
#include "qa/content/q1/base/species.h"
#include "qa/content/q1/foundation/pickups.h"
#include "qa/app/bootstrap/simulation/native_mod_host.h"
#include "qa/bots/navigation/rerelease_path.h"
#include "qa/content/catalog/timing.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/network/common/js_number.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/records.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/formats/q3_map/entities.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/world/actors/body.h"
#include "qa/content/q3/base/combat_bridge.h"
#include "qa/content/composition/q2_q3_items.h"
#include "foreign_match_host.h"
/* TS leaves an unregistered Q3 bridge policy to the GC; the simulation frees it with itself. */
static void ss_release_q3_bridge_policy(void *ctx) {
  qa_combat_policy policy;
  memset(&policy, 0, sizeof policy);
  policy.ctx = ctx;
  qa_q3_base_combat_bridge_policy_destroy(policy);
}

static noreturn void wl_fail(const char *format, ...) {
  char message[1024];
  va_list args; va_start(args, format); vsnprintf(message, sizeof message, format, args); va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}
static void *wl_alloc(size_t count, size_t size) {
  void *memory = calloc(count ? count : 1, size);
  if (!memory) qa_range_error("Out of memory");
  return memory;
}
static void *wl_grow(void *ptr, size_t *capacity, size_t need, size_t size) {
  if (need <= *capacity) return ptr;
  size_t next = *capacity ? *capacity * 2 : 8;
  while (next < need) next *= 2;
  void *grown = realloc(ptr, next * size);
  if (!grown) qa_range_error("Out of memory");
  *capacity = next;
  return grown;
}
static qa_shared_body_table *wl_bodies(qa_app_shared_simulation *sim) { return qa_app_sim_shared_physics_bodies(sim->physics); }
static double wl_time(const qa_app_shared_simulation *sim) { return qa_app_shared_sim_time_seconds(sim); }
/* players() copied: the phase A result is storage the next call reuses. */
static qa_actor_id *wl_players(qa_app_shared_simulation *sim, size_t *out_count) {
  size_t count = 0;
  const qa_actor_id *players = qa_app_shared_sim_players(sim, &count);
  qa_actor_id *copy = wl_alloc(count, sizeof *copy);
  if (count) memcpy(copy, players, count * sizeof *copy);
  *out_count = count;
  return copy;
}
static bool wl_includes(const qa_actor_id *actors, size_t count, qa_actor_id actor) {
  for (size_t i = 0; i < count; i++) if (qa_same_actor(actors[i], actor)) return true;
  return false;
}
/* this.combat.read(actor)?.health ?? 0 */
static double wl_health(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_combat_state_result state = qa_gameplay_authority_read(sim->combat, actor);
  return state.has_value ? state.value.health : 0;
}
static bool wl_nullable_equal(const char *a, const char *b) { return a == b || (a && b && strcmp(a, b) == 0); }
static qa_q2_game_services *wl_q2_services(qa_app_shared_simulation *sim) { return sim->source.product->services; }
static qa_q3_game_entity *wl_q3_native(qa_app_shared_simulation *sim, qa_actor_id actor) {
  return qa_q3_base_entity_records_native_by_actor(qa_app_q3_source_runtime_records(sim->source.q3), true, actor);
}

/* ---- Identities (runtime.ts 1183-1215) ---- */
/* get tsPrimary */
bool qa_app_shared_sim_ts_primary(qa_app_shared_simulation *sim) {
  return !sim->options.has_q2_guest && sim->options.q3_guest == NULL && sim->options.prepared_quakec == NULL;
}
/* content.split(":")[index] (NULL = TS undefined); malloc'd. */
static char *wl_colon_part(const char *content, size_t index) {
  const char *start = content;
  for (size_t i = 0; i < index; i++) {
    const char *colon = strchr(start, ':');
    if (!colon) return NULL;
    start = colon + 1;
  }
  const char *end = strchr(start, ':');
  size_t length = end ? (size_t)(end - start) : strlen(start);
  char *part = wl_alloc(length + 1, 1);
  memcpy(part, start, length);
  return part;
}
static const char *wl_map_content(const qa_app_shared_simulation *sim) {
  const qa_resource_provenance *provenance = &sim->recipe.map.geometry.provenance;
  return provenance->kind == QA_CONTENT_MOUNT_ARCHIVE ? provenance->archive_mount->identity.content
    : provenance->loose_mount->identity.content;
}
/* foreignQ2Identity() with its edition and program. */
bool qa_app_shared_sim_foreign_q2_identity_full(qa_app_shared_simulation *sim, qa_provider_reference *reference,
    const char **edition, const char **program) {
  if (!qa_ss_is(sim->world_kind, "q2-bsp") || !qa_app_shared_sim_ts_primary(sim)
      || qa_app_sim_provider_family(sim->recipe.map.entities.provider) == QA_GAME_FAMILY_Q2) return false;
  const char *content = wl_map_content(sim);
  char *edition_part = wl_colon_part(content, 1), *campaign = wl_colon_part(content, 2);
  const char *chosen_edition = qa_ss_is(edition_part, "rerelease") || qa_ss_is(campaign, "mg2") || qa_ss_is(campaign, "n64") ? "rerelease" : "classic";
  const char *chosen_program = qa_ss_is(campaign, "xatrix") ? "xatrix" : qa_ss_is(campaign, "rogue") ? "rogue"
    : qa_ss_is(campaign, "mg2") || qa_ss_is(campaign, "n64") ? "mg2" : "baseq2";
  free(edition_part); free(campaign);
  char provider[128];
  snprintf(provider, sizeof provider, "q2:source/%s/%s", chosen_edition, chosen_program);
  reference->provider = qa_app_shared_sim_intern(sim, provider);
  reference->content = content;
  *edition = chosen_edition; *program = chosen_program;
  return true;
}
bool qa_app_shared_sim_foreign_q2_identity(qa_app_shared_simulation *sim, qa_provider_reference *out) {
  const char *edition, *program;
  return qa_app_shared_sim_foreign_q2_identity_full(sim, out, &edition, &program);
}
bool qa_app_shared_sim_foreign_q1_identity(qa_app_shared_simulation *sim, qa_provider_reference *reference,
    const char **edition, const char **program) {
  if (!qa_ss_is(sim->world_kind, "q1-bsp") || !qa_app_shared_sim_ts_primary(sim)
      || qa_app_sim_provider_family(sim->recipe.map.entities.provider) == QA_GAME_FAMILY_Q1) return false;
  const char *content = wl_map_content(sim);
  char *edition_part = wl_colon_part(content, 1), *campaign = wl_colon_part(content, 2);
  static const char *const programs[] = {"hipnotic", "rogue", "dopa", "mg1", "mg3", "ctf"};
  const char *chosen_program = "id1";
  for (size_t i = 0; i < sizeof programs / sizeof *programs; i++) if (qa_ss_is(campaign, programs[i])) chosen_program = programs[i];
  const char *chosen_edition = qa_ss_is(edition_part, "rerelease") ? "rerelease" : "classic";
  free(edition_part); free(campaign);
  char provider[128];
  snprintf(provider, sizeof provider, "q1:source/%s/%s", chosen_edition, chosen_program);
  reference->provider = qa_app_shared_sim_intern(sim, provider);
  reference->content = content;
  *edition = chosen_edition; *program = chosen_program;
  return true;
}
bool qa_app_shared_sim_foreign_adjunct_identity(qa_app_shared_simulation *sim, qa_provider_reference *reference,
    const char **family, const char **edition) {
  const char *program;
  if (qa_app_shared_sim_foreign_q2_identity_full(sim, reference, edition, &program)) { *family = "q2"; return true; }
  if (qa_app_shared_sim_foreign_q1_identity(sim, reference, edition, &program)) { *family = "q1"; return true; }
  return false;
}

/* ---- canonicalTeam / primaryMatchPlayer (6584-6657) ---- */
/* TS nativePrimary() (B2) is the q2Guest prepared NativePrimaryProfile. */
static const qa_compat_q2_native_primary_profile *wl_native_primary(qa_app_shared_simulation *sim) {
  return qa_app_shared_sim_native_primary(sim);
}
static bool wl_native_match(qa_app_shared_simulation *sim, qa_source_primary_match *out) {
  const qa_compat_q2_native_primary_profile *profile = wl_native_primary(sim);
  if (!profile || !profile->player->has_match) return false;
  *out = profile->player->match;
  return true;
}
static bool wl_qvm_match(qa_app_shared_simulation *sim, qa_source_primary_match *out) {
  return sim->source.qvm_weapons && qa_qvm_primary_weapons_get_match(sim->source.qvm_weapons, out);
}
const char *qa_app_shared_sim_canonical_team(qa_app_shared_simulation *sim, qa_actor_id actor, const char *original) {
  qa_client_id client;
  if (!qa_app_shared_sim_player_client(sim, actor, &client)) return original;
  if (qa_ss_is(sim->options.mode, "coop")) return "team:coop";
  return sim->strategy->canonical_team(sim, original);
}
/* SourceStrategy.canonicalTeam per class. */
static const char *wl_mapped_team(const qa_source_primary_match *profile, bool has, const char *original) {
  if (has) for (size_t i = 0; i < profile->team_count; i++)
    if (wl_nullable_equal(profile->teams[i].alias.source, original)) return profile->teams[i].alias.team;
  return original;
}
const char *qa_ss_q2_native_canonical_team(qa_app_shared_simulation *sim, const char *original) {
  qa_source_primary_match profile; bool has = wl_native_match(sim, &profile);
  return wl_mapped_team(&profile, has, original);
}
const char *qa_ss_q3_qvm_canonical_team(qa_app_shared_simulation *sim, const char *original) {
  qa_source_primary_match profile; bool has = wl_qvm_match(sim, &profile);
  return wl_mapped_team(&profile, has, original);
}
const char *qa_ss_quakec_canonical_team(qa_app_shared_simulation *sim, const char *original) {
  const qa_app_sim_prepared_quakec_source *prepared = sim->options.prepared_quakec;
  if (prepared->has_teams) for (size_t i = 0; i < prepared->team_count; i++)
    if (wl_nullable_equal(prepared->teams[i].source, original)) return prepared->teams[i].team;
  return original;
}
const char *qa_ss_q1_canonical_team(qa_app_shared_simulation *sim, const char *original) {
  if (sim->source.q1_composition->clients.program != QA_COMP_Q1_CTF) return original;
  return qa_ss_is(original, "red") ? "team:red" : qa_ss_is(original, "blue") ? "team:blue" : original;
}
const char *qa_ss_q2_canonical_team(qa_app_shared_simulation *sim, const char *original) {
  if (sim->source.product->match.source_kind != QA_Q2_MATCH_CTF && sim->source.product->match.source_kind != QA_Q2_MATCH_LMCTF) return original;
  return qa_ss_is(original, "RED") ? "team:red" : qa_ss_is(original, "BLUE") ? "team:blue" : original;
}
const char *qa_ss_q3_canonical_team(qa_app_shared_simulation *sim, const char *original) {
  (void)sim;
  return qa_ss_is(original, "q3-team:1") ? "team:red" : qa_ss_is(original, "q3-team:2") ? "team:blue" : original;
}
/* SourceStrategy.declaresMatch (Q2Native, Q3Qvm). */
bool qa_ss_q2_native_declares_match(qa_app_shared_simulation *sim) { qa_source_primary_match profile; return wl_native_match(sim, &profile); }
bool qa_ss_q3_qvm_declares_match(qa_app_shared_simulation *sim) { qa_source_primary_match profile; return wl_qvm_match(sim, &profile); }
/* The TS SourceRuntime object identity: the owner each kind carries. */
static const void *wl_source_identity(const qa_ss_source_runtime *source) {
  switch (source->kind) {
  case QA_SS_SOURCE_QUAKEC: return source->quakec;
  case QA_SS_SOURCE_Q1: return source->q1_composition;
  case QA_SS_SOURCE_Q3: return source->q3;
  case QA_SS_SOURCE_Q3_QVM: return source->qvm;
  case QA_SS_SOURCE_Q2_NATIVE: return source->classic_world ? (const void *)source->classic_world : (const void *)source->rerelease_world;
  case QA_SS_SOURCE_Q2: return source->product;
  case QA_SS_SOURCE_LOADING: break;
  }
  return NULL;
}
/* The closure record of one primaryMatchPlayer result. Records are kept per
 * distinct capture (source, owned, client), so a repeated call for the same
 * capture reuses its record instead of growing without bound. */
typedef struct wl_match_player {
  qa_app_shared_simulation *sim;
  qa_ss_source_kind kind; const void *source;
  qa_actor_id actor; qa_owned_actor owned; qa_client_id client;
} wl_match_player;
static void wl_match_current(wl_match_player *record) {
  qa_app_shared_simulation *sim = record->sim;
  qa_owned_actor owned; qa_client_id client;
  if (sim->closed || sim->source.kind != record->kind || wl_source_identity(&sim->source) != record->source
      || !qa_session_actor_registry_resolve_owned(sim->actors, record->actor, &owned) || !qa_same_owned_actor(owned, record->owned)
      || !qa_app_shared_sim_player_client(sim, record->actor, &client) || !qa_same_client(client, record->client))
    wl_fail("Original match player was retired");
}
static const char *wl_match_team(void *ctx) {
  wl_match_player *record = ctx;
  wl_match_current(record);
  qa_combat_state_result state = qa_gameplay_authority_read(record->sim->combat, record->actor);
  return state.has_value ? state.value.team : NULL;
}
static qa_lmctf_player_state *wl_lmctf_state(qa_app_shared_simulation *sim, qa_actor_id actor) {
  return qa_lmctf_context_get(qa_q2_lmctf_get_context(sim->source.product->match.lmctf), actor);
}
static double wl_match_score(void *ctx) {
  wl_match_player *record = ctx;
  wl_match_current(record);
  return record->sim->strategy->match_score(record->sim, record->actor);
}
static void wl_match_set_score(void *ctx, double value) {
  wl_match_player *record = ctx;
  wl_match_current(record);
  record->sim->strategy->set_match_score(record->sim, record->actor, value);
  wl_match_current(record);
}
/* SourceStrategy.matchScore / setMatchScore per class. */
double qa_ss_q1_match_score(qa_app_shared_simulation *sim, qa_actor_id actor) {
  return qa_comp_q1_source_clients_require(&sim->source.q1_composition->clients, actor)->frags;
}
double qa_ss_quakec_match_score(qa_app_shared_simulation *sim, qa_actor_id actor) { return qa_app_sim_quakec_source_match_score(sim->source.quakec, actor); }
double qa_ss_q2_native_match_score(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_compat_q2_native_primary_player player = qa_app_shared_sim_selected_native_player(sim);
  return qa_compat_q2_native_primary_player_score(&player, actor);
}
double qa_ss_q3_qvm_match_score(qa_app_shared_simulation *sim, qa_actor_id actor) {
  return qa_qvm_primary_weapons_score(qa_app_shared_sim_require_qvm_weapons(sim), actor);
}
double qa_ss_q2_match_score(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q2_base_player_state *state = qa_q2_players_get_state(sim->source.product->players, actor);
  if (!state) wl_fail("Original Q2 score has no player");
  if (sim->source.product->match.source_kind == QA_Q2_MATCH_LMCTF) {
    qa_lmctf_player_state *lmctf = wl_lmctf_state(sim, actor);
    return lmctf ? qa_lmctf_player_state_stat(lmctf, "score", 0) : 0;
  }
  return state->score;
}
double qa_ss_q3_match_score(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q3_game_entity *native = wl_q3_native(sim, actor);
  if (!native || !native->client) wl_fail("Original Q3 score has no player");
  return qa_q3_base_player_state_slots_get(&native->client->ps.persistant, QA_Q3_BASE_PERS_SCORE);
}
void qa_ss_q1_set_match_score(qa_app_shared_simulation *sim, qa_actor_id actor, double value) {
  if (!isfinite(value)) wl_fail("Quake score must be finite");
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&sim->source.q1_composition->clients, actor);
  client->frags = (float)value;
  qa_comp_q1_source_clients_publish(&sim->source.q1_composition->clients, client);
}
void qa_ss_quakec_set_match_score(qa_app_shared_simulation *sim, qa_actor_id actor, double value) {
  qa_app_sim_quakec_source_set_match_score(sim->source.quakec, actor, value);
}
void qa_ss_q2_native_set_match_score(qa_app_shared_simulation *sim, qa_actor_id actor, double value) {
  qa_compat_q2_native_primary_player player = qa_app_shared_sim_selected_native_player(sim);
  qa_compat_q2_native_primary_player_set_score(&player, actor, value);
}
void qa_ss_q3_qvm_set_match_score(qa_app_shared_simulation *sim, qa_actor_id actor, double value) {
  qa_qvm_primary_weapons_set_score(qa_app_shared_sim_require_qvm_weapons(sim), actor, value);
}
void qa_ss_q2_set_match_score(qa_app_shared_simulation *sim, qa_actor_id actor, double value) {
  qa_q2_base_player_state *state = qa_q2_players_get_state(sim->source.product->players, actor);
  if (!state) wl_fail("Original Q2 score has no player");
  state->score = qa_source_score(value);
  if (sim->source.product->match.source_kind == QA_Q2_MATCH_LMCTF) {
    qa_lmctf_player_state *lmctf = wl_lmctf_state(sim, actor);
    if (lmctf) qa_lmctf_player_state_set_stat(lmctf, "score", state->score);
  }
}
void qa_ss_q3_set_match_score(qa_app_shared_simulation *sim, qa_actor_id actor, double value) {
  qa_q3_game_entity *native = wl_q3_native(sim, actor);
  if (!native || !native->client) wl_fail("Original Q3 score has no player");
  qa_q3_base_player_state_slots_set(&native->client->ps.persistant, QA_Q3_BASE_PERS_SCORE, qa_source_score(value));
}
typedef struct wl_native_command { qa_app_shared_simulation *sim; double slot; qa_string_view_list args; const char *rest; } wl_native_command;
static void wl_native_command_run(void *ctx) {
  wl_native_command *command = ctx; qa_app_shared_simulation *sim = command->sim;
  if (sim->source.classic_world) qa_app_sim_classic_guest_world_command(sim->source.classic_world, command->slot, command->args.values, command->args.count, command->rest);
  else qa_app_sim_rerelease_guest_world_command(sim->source.rerelease_world, command->slot, command->args.values, command->args.count, command->rest);
}
/* args.slice(1).join(" "), malloc'd. */
static char *wl_join_rest(qa_string_view_list args) {
  size_t length = 1;
  for (size_t i = 1; i < args.count; i++) length += strlen(args.values[i]) + 1;
  char *text = wl_alloc(length, 1);
  for (size_t i = 1; i < args.count; i++) { if (i > 1) strcat(text, " "); strcat(text, args.values[i]); }
  return text;
}
static void wl_match_set_team(void *ctx, const char *requested) {
  wl_match_player *record = ctx;
  wl_match_current(record);
  if (wl_nullable_equal(wl_match_team(record), requested)) return;
  record->sim->strategy->set_match_team(record->sim, record->actor, requested);
  wl_match_current(record);
}
/* SourceStrategy.setMatchTeam per class. */
void qa_ss_q2_native_set_match_team(qa_app_shared_simulation *sim, qa_actor_id actor, const char *requested) {
  qa_source_primary_match profile;
  if (!wl_native_match(sim, &profile)) wl_fail("Original native team commands have no declaration");
  qa_string_view_list args = qa_source_team_command(profile, requested);
  if (args.count == 0) wl_fail("Original team command is empty");
  qa_client_id client;
  if (!qa_app_shared_sim_player_client(sim, actor, &client)) wl_fail("Original team command has no client");
  char *rest = wl_join_rest(args);
  wl_native_command command = {sim, (double)client.slot + 1, args, rest};
  qa_app_q2_game_callback(wl_native_command_run, &command);
  free(rest);
}
void qa_ss_q3_qvm_set_match_team(qa_app_shared_simulation *sim, qa_actor_id actor, const char *requested) {
  qa_source_primary_match profile; bool has_profile = wl_qvm_match(sim, &profile);
  qa_app_q3_player players[64]; size_t total = qa_app_q3_qvm_server_game_players(sim->source.qvm, players, 64);
  const qa_app_q3_player *player = NULL;
  for (size_t i = 0; i < total && !player; i++) if (qa_same_actor(players[i].actor, actor)) player = &players[i];
  if (!has_profile || !player) wl_fail("Original QVM team commands have no declaration");
  qa_string_view_list args = qa_source_team_command(profile, requested);
  qa_app_q3_qvm_server_game_command_immediate(sim->source.qvm, *player, args.values, args.count);
}
void qa_ss_quakec_set_match_team(qa_app_shared_simulation *sim, qa_actor_id actor, const char *requested) {
  const qa_app_sim_prepared_quakec_source *prepared = sim->options.prepared_quakec;
  const char *team = requested;
  if (prepared->has_teams) for (size_t i = 0; i < prepared->team_count; i++)
    if (wl_nullable_equal(prepared->teams[i].team, requested)) { team = prepared->teams[i].source; break; }
  qa_app_sim_quakec_source_set_match_team(sim->source.quakec, actor, team);
  qa_app_shared_sim_notify_client_event(sim, "userinfo", actor);
}
void qa_ss_q3_set_match_team(qa_app_shared_simulation *sim, qa_actor_id actor, const char *requested) {
  const char *argument = qa_ss_is(requested, "team:red") ? "red" : qa_ss_is(requested, "team:blue") ? "blue" : requested == NULL ? "free" : NULL;
  if (!argument) wl_fail("Team has no original Q3 command");
  const char *args[] = {argument};
  qa_app_q3_source_runtime_player_command(sim->source.q3, actor, "team", args, 1);
}
void qa_ss_q1_set_match_team(qa_app_shared_simulation *sim, qa_actor_id actor, const char *requested) {
  qa_comp_q1_source_clients *clients = &sim->source.q1_composition->clients;
  qa_comp_q1_source_client *entry = qa_comp_q1_source_clients_require(clients, actor);
  double value = clients->program == QA_COMP_Q1_CTF ? qa_ss_is(requested, "team:red") ? 5 : qa_ss_is(requested, "team:blue") ? 14 : NAN
    : requested == NULL ? NAN : qa_js_string_to_number(requested);
  if (!isfinite(value) || trunc(value) != value || value < 1 || value > 14) wl_fail("Team has no original Quake color command");
  qa_comp_q1_source_clients_colors(clients, actor, (double)qa_comp_q1_source_client_shirt(entry), value - 1);
}
void qa_ss_q2_set_match_team(qa_app_shared_simulation *sim, qa_actor_id actor, const char *requested) {
  if (sim->source.product->match.source_kind != QA_Q2_MATCH_CTF && sim->source.product->match.source_kind != QA_Q2_MATCH_LMCTF)
    wl_fail("This original source has no declared team-change command");
  qa_q2_game_services *game = wl_q2_services(sim);
  qa_q2_entity *entity = qa_q2_game_entity_of(game, &actor);
  const char *argument = qa_ss_is(requested, "team:red") ? "red" : qa_ss_is(requested, "team:blue") ? "blue" : NULL;
  if (!entity || !argument) wl_fail("Team has no original Q2 team command");
  const char *args[] = {argument};
  if (sim->source.product->match.source_kind == QA_Q2_MATCH_CTF) qa_q2_ctf_command(sim->source.product->match.ctf, entity, game, "team", args, 1);
  else qa_q2_lmctf_command(sim->source.product->match.lmctf, entity, game, "team", args, 1);
}
qa_source_match_player_result qa_app_shared_sim_primary_match_player(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_source_match_player_result result; memset(&result, 0, sizeof result);
  qa_owned_actor owned; qa_client_id client;
  if (!qa_session_actor_registry_resolve_owned(sim->actors, actor, &owned) || !qa_app_shared_sim_player_client(sim, actor, &client)
      || !sim->strategy->declares_match(sim)) return result;
  wl_match_player *record = NULL;
  const void *identity = wl_source_identity(&sim->source);
  for (size_t i = 0; i < sim->b1_match_player_count && !record; i++) {
    wl_match_player *candidate = sim->b1_match_players[i];
    if (candidate->kind == sim->source.kind && candidate->source == identity && qa_same_actor(candidate->actor, actor)
        && qa_same_owned_actor(candidate->owned, owned) && qa_same_client(candidate->client, client)) record = candidate;
  }
  if (!record) {
    record = qa_app_shared_sim_keep(sim, sizeof *record);
    *record = (wl_match_player){sim, sim->source.kind, identity, actor, owned, client};
    sim->b1_match_players = wl_grow(sim->b1_match_players, &sim->b1_match_player_capacity, sim->b1_match_player_count + 1, sizeof *sim->b1_match_players);
    sim->b1_match_players[sim->b1_match_player_count++] = record;
  }
  result.has_value = true;
  result.value = (qa_source_match_player){record, owned.owner, wl_match_team, wl_match_score, wl_match_set_team, wl_match_set_score};
  return result;
}

/* ---- Source accessors (6795-6856, 7344-7421, 8099-8101) ---- */
const char *qa_app_shared_sim_source_entity_text(qa_app_shared_simulation *sim) { return sim->world_entities; }
qa_q1_services *qa_app_shared_sim_q1_weapon_source(qa_app_shared_simulation *sim) {
  if (sim->has_selected_arsenal)
    return qa_ss_is(sim->selected_arsenal.family, "q1") && sim->has_selected_weapon_source && qa_ss_is(sim->selected_weapon_source.kind, "q1")
      ? sim->selected_weapon_source.q1_game : NULL;
  return (sim->strategy->as_q1(sim) != NULL) ? sim->source.q1_game : NULL;
}
bool qa_app_shared_sim_q2_weapon_source(qa_app_shared_simulation *sim, qa_q2_game_services **out_game, qa_q2w_weapons **out_weapons) {
  if (sim->has_selected_arsenal) {
    if (!qa_ss_is(sim->selected_arsenal.family, "q2") || !sim->has_selected_weapon_source || !qa_ss_is(sim->selected_weapon_source.kind, "q2")) return false;
    *out_game = sim->selected_weapon_source.q2_game; *out_weapons = sim->selected_weapon_source.weapons;
    return true;
  }
  if ((sim->strategy->as_q2(sim) == NULL)) return false;
  *out_game = sim->source.product->services; *out_weapons = sim->source.q2_weapons;
  return true;
}
bool qa_app_shared_sim_q2_item_weapon_source(qa_app_shared_simulation *sim, qa_q2_game_services **out_game, qa_q2w_weapons **out_weapons) {
  if (qa_app_shared_sim_q2_weapon_source(sim, out_game, out_weapons)) return true;
  if ((sim->strategy->as_q2(sim) == NULL)) return false;
  *out_game = sim->source.product->services; *out_weapons = sim->source.q2_weapons;
  return true;
}
qa_app_q3_selected_arsenal *qa_app_shared_sim_selected_q3_weapon_source(qa_app_shared_simulation *sim) {
  return sim->has_selected_arsenal && qa_ss_is(sim->selected_arsenal.family, "q3") ? sim->selected_arsenal.q3 : NULL;
}
qa_cvar_registry *qa_app_shared_sim_q2_server_cvars(qa_app_shared_simulation *sim) { return sim->q2_server_registry; }
bool qa_app_shared_sim_q2_movement_config(qa_app_shared_simulation *sim, qa_app_sim_q2_movement_config *out) {
  if ((sim->strategy->as_q2(sim) == NULL) || sim->q2_server_registry == NULL) return false;
  const qa_q2_game_options *options = &sim->source.product->services->options;
  bool rerelease = options->edition == QA_Q2_EDITION_RERELEASE;
  if (rerelease) {
    qa_cvar_read read;
    if (qa_cvar_registry_find(sim->q2_server_registry, "sv_airaccelerate", &read)) { out->air_accelerate = read.integer_value; qa_cvar_read_free(&read); }
    else out->air_accelerate = 0;
  } else out->air_accelerate = options->mode == QA_Q2_MODE_DEATHMATCH ? qa_cvar_registry_variable_value(sim->q2_server_registry, "sv_airaccelerate") : 0;
  out->n64_physics = rerelease && options->mode != QA_Q2_MODE_DEATHMATCH && strncmp(options->map_name, "q64/", 4) == 0;
  return true;
}
/* serverSettings(): the bound settings over the source's registry. The Q3
 * source reads effective values from its game settings snapshots. */
typedef struct wl_q3_settings_owner { qa_cvar_server_owner base; qa_app_q3_source_runtime *runtime; } wl_q3_settings_owner;
static qa_server_setting_values wl_q3_settings_read(void *ctx, qa_server_setting_target target) {
  wl_q3_settings_owner *owner = ctx;
  qa_server_setting_values values = owner->base.owner.read(owner->base.owner.ctx, target);
  if (target.is_bit) {
    qa_server_setting_values again = owner->base.owner.read(owner->base.owner.ctx, target);
    free(values.effective); values.effective = again.effective; again.effective = NULL;
    qa_server_setting_values_free(&again);
    return values;
  }
  free(values.effective);
  values.effective = qa_ss_strdup(qa_app_q3_source_runtime_setting_string(owner->runtime, target.name));
  return values;
}
static void wl_q3_settings_write(void *ctx, qa_server_setting_target target, const char *value, qa_server_apply_at apply_at) {
  wl_q3_settings_owner *owner = ctx;
  owner->base.owner.write(owner->base.owner.ctx, target, value, apply_at);
}
/* Q3Strategy.serverSettingsOwner(owner): effective values come from the game settings snapshots. `owner` is the base of
 * the wl_q3_settings_owner serverSettings allocates. */
qa_server_settings_owner qa_ss_q3_server_settings_owner(qa_app_shared_simulation *sim, qa_cvar_server_owner *owner) {
  wl_q3_settings_owner *settings = (wl_q3_settings_owner *)owner;
  settings->runtime = sim->source.q3;
  return (qa_server_settings_owner){settings, wl_q3_settings_read, wl_q3_settings_write};
}
qa_app_shared_sim_bound_settings qa_app_shared_sim_server_settings(qa_app_shared_simulation *sim) {
  qa_app_shared_sim_bound_settings result; memset(&result, 0, sizeof result);
  qa_cvar_registry *cvars = sim->strategy->server_cvars(sim);
  if (!cvars) return result;
  wl_q3_settings_owner *owner = wl_alloc(1, sizeof *owner);
  qa_cvar_server_settings_owner_init(&owner->base, cvars, false);
  qa_server_settings_owner bound = sim->strategy->server_settings_owner(sim, &owner->base);
  size_t count = 0;
  const qa_server_setting_definition **definitions = qa_server_definitions_for_recipe(&sim->recipe, &count);
  result.bindings = wl_alloc(count, sizeof *result.bindings);
  for (size_t i = 0; i < count; i++) result.bindings[i] = (qa_bound_server_setting){definitions[i], bound};
  result.count = count; result.storage = owner;
  return result;
}
void qa_app_shared_sim_bound_settings_free(qa_app_shared_sim_bound_settings *settings) {
  free(settings->bindings); free(settings->storage);
  memset(settings, 0, sizeof *settings);
}
void qa_app_shared_sim_server_profile(qa_app_shared_simulation *sim, qa_server_profile *out) {
  qa_app_shared_sim_bound_settings settings = qa_app_shared_sim_server_settings(sim);
  qa_capture_server_profile(settings.bindings, settings.count, out);
  qa_app_shared_sim_bound_settings_free(&settings);
}
qa_q2_product_runtime *qa_app_shared_sim_q2_source(qa_app_shared_simulation *sim) { return (sim->strategy->as_q2(sim) != NULL) ? sim->source.product : NULL; }
qa_app_sim_quakec_source *qa_app_shared_sim_quakec_source(qa_app_shared_simulation *sim) { return (sim->strategy->as_quakec(sim) != NULL) ? sim->source.quakec : NULL; }
const qa_ss_source_runtime *qa_app_shared_sim_q1_source(qa_app_shared_simulation *sim) { return (sim->strategy->as_q1(sim) != NULL) ? &sim->source : NULL; }
/* q2Native(): the classic or rerelease guest world (both NULL = TS null). */
bool qa_app_shared_sim_q2_native(qa_app_shared_simulation *sim, qa_app_sim_classic_guest_world **classic, qa_app_sim_rerelease_guest_world **rerelease) {
  if ((sim->strategy->as_q2_native(sim) == NULL)) return false;
  /* Either out-pointer may be NULL when the caller only asks whether the native world exists. */
  if (classic != NULL) *classic = sim->source.classic_world;
  if (rerelease != NULL) *rerelease = sim->source.rerelease_world;
  return true;
}
size_t qa_app_shared_sim_q2_native_players(qa_app_shared_simulation *sim, const qa_client_id **out_clients, const qa_actor_id **out_actors) {
  static __thread qa_client_id *clients; static __thread qa_actor_id *actors; static __thread size_t capacity;
  if ((sim->strategy->as_q2_native(sim) == NULL)) { *out_clients = NULL; *out_actors = NULL; return 0; }
  size_t count = sim->source.clients.count, client_capacity = capacity;
  clients = wl_grow(clients, &client_capacity, count + 1, sizeof *clients);
  actors = wl_grow(actors, &capacity, count + 1, sizeof *actors);
  for (size_t i = 0; i < count; i++) {
    clients[i] = qa_ss_map_key_at(&sim->source.clients, i)->client;
    actors[i] = ((qa_owned_actor *)qa_ss_map_value_at(&sim->source.clients, i))->id;
  }
  *out_clients = clients; *out_actors = actors;
  return count;
}
qa_app_q3_qvm_server_game *qa_app_shared_sim_q3_guest(qa_app_shared_simulation *sim) { return (sim->strategy->as_q3_qvm(sim) != NULL) ? sim->source.qvm : NULL; }
void qa_app_shared_sim_shutdown_q3_guest(qa_app_shared_simulation *sim) {
  qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(sim);
  if (guest) qa_app_q3_qvm_server_game_shutdown(guest);
}
qa_app_q3_source_runtime *qa_app_shared_sim_q3_source(qa_app_shared_simulation *sim) { return (sim->strategy->as_q3(sim) != NULL) ? sim->source.q3 : NULL; }
bool qa_app_shared_sim_selected_q3_source_state(qa_app_shared_simulation *sim, qa_app_shared_sim_q3_source_state *out) {
  if (!sim->selected_q3_source) return false;
  out->content = sim->weapon_provider.content; out->source = sim->selected_q3_source;
  out->state = qa_app_q3_selected_source_source_state(sim->selected_q3_source);
  return true;
}
const char *qa_app_shared_sim_pending_match_map(qa_app_shared_simulation *sim) {
  if ((sim->strategy->as_q2(sim) == NULL) || sim->source.product->match.source_kind != QA_Q2_MATCH_LMCTF) return NULL;
  const qa_lmctf_map_change *pending = qa_lmctf_match_pending_map(qa_q2_lmctf_get_match(sim->source.product->match.lmctf));
  return pending ? pending->map : NULL;
}
const qa_app_sim_source_objective *qa_app_shared_sim_source_objectives(qa_app_shared_simulation *sim, size_t *out_count) {
  static __thread qa_app_sim_source_objective *storage; static __thread size_t capacity;
  size_t count = 0;
  const qa_source_objective_listing *listings = qa_mod_match_state_objectives(sim->source_match, &count);
  storage = wl_grow(storage, &capacity, count + 1, sizeof *storage);
  for (size_t i = 0; i < count; i++) {
    qa_app_sim_source_objective *objective = &storage[i]; memset(objective, 0, sizeof *objective);
    objective->bot_goal = listings[i].bot_goal; objective->complete = listings[i].state.complete;
    if (listings[i].state.target) { objective->has_target = true; objective->target = *listings[i].state.target; }
    if (listings[i].state.carrier) { objective->has_carrier = true; objective->carrier = *listings[i].state.carrier; }
  }
  *out_count = count;
  return storage;
}

/* ---- Team policy and same-map source restart (7346-7418) ---- */
bool qa_app_shared_sim_team_game(qa_app_shared_simulation *sim) { return sim->strategy->team_game(sim); }
qa_app_sim_network_restart_plan qa_app_shared_sim_source_restart_plan(qa_app_shared_simulation *sim) {
  qa_app_shared_sim_assert_open(sim);
  if ((sim->strategy->as_q3(sim) == NULL)) return (qa_app_sim_network_restart_plan){false, "Selected provider has no same-map source reset"};
  if (sim->has_selected_arsenal || sim->has_selected_weapon_source || sim->monster_sources.count != 0 || sim->grapple != NULL
      || sim->hand_grenades != NULL || qa_app_shared_sim_foreign_tag(sim) || qa_app_shared_sim_foreign_lmctf(sim) || qa_app_shared_sim_foreign_deathball(sim))
    return (qa_app_sim_network_restart_plan){false, "Selected adjunct providers and foreign match modes require full world replacement"};
  const char *compatibility = qa_app_q3_source_runtime_restart_compatibility(sim->source.q3);
  return qa_ss_is(compatibility, "compatible") ? (qa_app_sim_network_restart_plan){true, NULL}
    : (qa_app_sim_network_restart_plan){false, compatibility};
}
typedef struct wl_restart { qa_app_shared_simulation *sim; qa_app_q3_captured_session carry; } wl_restart;
static void wl_restart_body(void *ctx) {
  wl_restart *restart = ctx; qa_app_shared_simulation *sim = restart->sim;
  qa_app_q3_source_runtime *previous = sim->source.q3;
  size_t observation_count = 0;
  const qa_actor_observation *observations = qa_session_actor_registry_observations(sim->actors, &observation_count);
  qa_actor_id *actors = wl_alloc(observation_count, sizeof *actors);
  for (size_t i = 0; i < observation_count; i++) actors[i] = observations[i].id;
  for (size_t i = 0; i < observation_count; i++) {
    qa_owned_actor owned;
    if (qa_session_actor_registry_resolve_owned(sim->actors, actors[i], &owned)) qa_session_actor_registry_release(sim->actors, owned);
  }
  free(actors);
  if (sim->has_dispose_source_combat) qa_gameplay_authority_unregister(sim->combat, sim->dispose_source_combat);
  sim->has_dispose_source_combat = false;
  qa_app_q3_source_runtime_close(previous);
  size_t discarded;
  qa_app_sim_simulation_events_take(sim->events, &discarded);
  qa_app_presentation_state_take_presentation(sim->presentation, &discarded);
  qa_event_ring_reset(&sim->transitions);
  sim->has_level_change = false; free(sim->level_change_map); sim->level_change_map = NULL;
  qa_world_debug_line_store_clear(&sim->debug_line_store); sim->debug_line_snapshot = NULL; sim->debug_line_snapshot_count = 0;
  qa_world_text_store_clear(&sim->world_text_store); sim->world_text_snapshot = NULL; sim->world_text_snapshot_count = 0;
  qa_app_q3_source_options options = *qa_app_q3_source_runtime_options(previous);
  options.session_carry = &restart->carry.carry;
  qa_app_q3_source_host host = *qa_app_q3_source_runtime_host(previous);
  qa_app_q3_source_runtime_mode mode = {false, NULL};
  qa_app_q3_source_runtime *game = qa_app_q3_source_runtime_new(options, host, mode);
  if (!game) wl_fail("Out of memory");
  /* The closed runtime (and the subsystems it owns) is dropped once its
   * replacement exists (TS GC); the new runtime copied the options and host it reads. */
  /* TS passes previous.game.host to the new runtime: the created host carries over. */
  qa_app_q3_created_source_host *created = sim->source.q3_host;
  sim->source.q3_host = NULL;
  qa_app_shared_sim_source_runtime_destroy(sim, &sim->source);
  sim->source.kind = QA_SS_SOURCE_Q3; sim->source.q3 = game; sim->source.q3_owner = qa_app_q3_source_runtime_owner(game);
  sim->source.q3_host = created;
  qa_ss_set_source_strategy(sim); /* this.setSource({ kind: "q3", game }) */
  if (qa_app_sim_provider_family(sim->recipe.combat.provider) == QA_GAME_FAMILY_Q3) {
    qa_combat_policy bridge_policy = qa_q3_base_combat_bridge_policy(qa_app_q3_source_runtime_bridge(game));
    qa_app_shared_sim_keep_owned(sim, bridge_policy.ctx, ss_release_q3_bridge_policy);
    sim->dispose_source_combat = qa_gameplay_authority_register(sim->combat, bridge_policy);
    sim->has_dispose_source_combat = true;
  }
  qa_app_q3_source_runtime_load(game);
  sim->source_round_settlement = (qa_ss_round_settlement){"ready", game, 0};
}
const qa_client_id *qa_app_shared_sim_restart_source_round(qa_app_shared_simulation *sim, size_t *out_count) {
  if (qa_ss_is(sim->source_round_settlement.kind, "active") || qa_ss_is(sim->source_round_settlement.kind, "failed"))
    wl_fail("Complete or retire source round settlement before another restart");
  qa_app_sim_network_restart_plan plan = qa_app_shared_sim_source_restart_plan(sim);
  if (!plan.source_reset) wl_fail("Source restart requires full world replacement: %s", plan.reason);
  if (sim->stepping) wl_fail("Source restart requires a completed simulation frame");
  if ((sim->strategy->as_q3(sim) == NULL)) wl_fail("Source restart owner changed");
  if (qa_app_sim_bot_services_configuration(&sim->bot_services) != NULL) wl_fail("Detach bot round services before source restart");
  size_t count = 0;
  const qa_client_id *identities = qa_app_shared_sim_client_identities(sim, &count);
  static __thread qa_client_id *clients; static __thread size_t capacity;
  clients = wl_grow(clients, &capacity, count + 1, sizeof *clients);
  if (count) memcpy(clients, identities, count * sizeof *clients);
  wl_restart restart = {sim, qa_app_q3_source_runtime_capture_session(sim->source.q3)};
  qa_caught_error caught;
  if (!qa_try(wl_restart_body, &restart, &caught)) {
    qa_app_q3_captured_session_destroy(&restart.carry);
    qa_app_shared_sim_close(sim);
    qa_rethrow(&caught);
  }
  /* The restarted runtime borrows its session carry; the previous carry's
   * runtime was replaced above. */
  if (sim->has_b1_restart_carry) qa_app_q3_captured_session_destroy(&sim->b1_restart_carry);
  sim->b1_restart_carry = restart.carry; sim->has_b1_restart_carry = true;
  *out_count = count;
  return clients;
}
void qa_app_shared_sim_begin_source_round_settlement(qa_app_shared_simulation *sim) {
  qa_app_shared_sim_assert_open(sim);
  qa_ss_round_settlement settlement = sim->source_round_settlement;
  if (sim->stepping || !qa_ss_is(settlement.kind, "ready") || !sim->strategy->settles(sim, settlement.source))
    wl_fail("Source round settlement requires a fresh matching source reset");
  if (!qa_app_shared_sim_source_restart_plan(sim).source_reset || sim->clock.current.time.kind != QA_SOURCE_TIME_MILLISECONDS)
    wl_fail("Source round settlement requires a compatible native Q3 clock");
  sim->source_round_settlement = (qa_ss_round_settlement){"active", settlement.source, 0};
}
void qa_app_shared_sim_complete_source_round_settlement(qa_app_shared_simulation *sim) {
  qa_app_shared_sim_assert_open(sim);
  qa_ss_round_settlement settlement = sim->source_round_settlement;
  if (sim->stepping || !qa_ss_is(settlement.kind, "active") || !sim->strategy->settles(sim, settlement.source) || settlement.completed != 4)
    wl_fail("Source round settlement requires four completed frames before resuming");
  sim->source_round_settlement = (qa_ss_round_settlement){"none", NULL, 0};
}

/* C-only teardown of the B1 owners (called by qa_app_shared_simulation_destroy). */

/* ---- Q1 punch and source view roll (1050-1099) ---- */
/* The punch owner q1Punch reads and writes within one call (q1_punch.c
 * resolve), so the closure record is the calling thread's scratch. */
typedef struct wl_punch_owner {
  qa_app_shared_simulation *sim; qa_actor_id actor;
  bool quakec; qa_q1_player_state *players[2]; size_t player_count;
} wl_punch_owner;
static qa_vec3 wl_punch_read(void *ctx) {
  wl_punch_owner *owner = ctx;
  if (owner->quakec) return qa_app_sim_quakec_source_client_punch_angles(owner->sim->source.quakec, owner->actor);
  return owner->players[0]->punch_angles;
}
static void wl_punch_write(void *ctx, qa_vec3 angles) {
  wl_punch_owner *owner = ctx;
  if (owner->quakec) { qa_app_sim_quakec_source_set_client_punch_angles(owner->sim->source.quakec, owner->actor, angles); return; }
  for (size_t i = 0; i < owner->player_count; i++) owner->players[i]->punch_angles = angles;
}
bool qa_app_shared_sim_q1_punch_owner(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_q1_punch_owner *out) {
  static __thread wl_punch_owner scratch;
  wl_punch_owner owner; memset(&owner, 0, sizeof owner);
  owner.sim = sim; owner.actor = actor;
  if ((sim->strategy->as_quakec(sim) != NULL) && qa_ss_is(qa_app_sim_quakec_source_kind(sim->source.quakec), "netquake")) owner.quakec = true;
  else {
    /* The map game and a selected Q1 arsenal each keep (and checkpoint) their
     * own player record; the unified punch mirrors into every live record. */
    qa_q1_player_state *map = (sim->strategy->as_q1(sim) != NULL) ? qa_q1_player(sim->source.q1_game, actor) : NULL;
    qa_q1_services *weapons = qa_app_shared_sim_q1_weapon_source(sim);
    qa_q1_player_state *selected = weapons ? qa_q1_player(weapons, actor) : NULL;
    if (map) owner.players[owner.player_count++] = map;
    if (selected && selected != map) owner.players[owner.player_count++] = selected;
    if (owner.player_count == 0) return false;
  }
  scratch = owner;
  *out = (qa_app_sim_q1_punch_owner){&scratch, wl_punch_read, wl_punch_write};
  return true;
}
void qa_app_shared_sim_emit_q1_addon(qa_app_shared_simulation *sim, qa_content_id content, const qa_q1ad_event *event, qa_source_time time) {
  if (event->kind == QA_Q1AD_EVENT_PUNCH_ANGLE) qa_app_sim_q1_player_punch_write(sim->q1_punch, event->player, event->angles);
  else if (event->kind == QA_Q1AD_EVENT_VIEW_ROLL) qa_app_shared_sim_set_source_view_roll(sim, event->player, event->roll);
  qa_comp_q1_event composition; memset(&composition, 0, sizeof composition);
  composition.kind = QA_COMP_Q1_EVENT_ADDON; composition.addon_event = event;
  qa_app_sim_source_event_input input; memset(&input, 0, sizeof input);
  input.kind = "q1-composition"; input.q1_composition = &composition;
  qa_app_shared_sim_emit(sim, content, input, &time, NULL);
}
void qa_app_shared_sim_set_source_view_roll(qa_app_shared_simulation *sim, qa_actor_id actor, double roll) {
  if (!isfinite(roll)) qa_range_error("Source view roll must be finite");
  qa_client_id client;
  if (!qa_app_shared_sim_player_client(sim, actor, &client)) wl_fail("Source view requires a live client");
  if ((sim->strategy->as_quakec(sim) != NULL)) { qa_app_sim_quakec_source_set_client_view_roll(sim->source.quakec, actor, roll); return; }
  if ((sim->strategy->as_q2_native(sim) != NULL)) {
    double slot = (double)qa_app_shared_sim_native_q2_client(sim, actor).slot + 1;
    if (sim->source.classic_world) qa_app_sim_classic_guest_services_set_player_view_roll(qa_app_sim_classic_guest_world_services(sim->source.classic_world), slot, actor, roll);
    else qa_app_sim_rerelease_guest_services_set_player_view_roll(qa_app_sim_rerelease_guest_world_services(sim->source.rerelease_world), slot, actor, roll);
    return;
  }
  if ((sim->strategy->as_q3_qvm(sim) != NULL)) { qa_app_q3_guest_records_set_player_view_roll(qa_app_q3_qvm_server_game_records(sim->source.qvm), actor, roll); return; }
  qa_app_sim_movement_player *movement = qa_app_shared_sim_require_player(sim, actor);
  if ((sim->strategy->as_q1(sim) != NULL)) {
    qa_q1_player_state *player = qa_q1_player(sim->source.q1_game, actor);
    if (!player) wl_fail("Q1 source view has no player state");
    player->view_angles.z = (float)roll;
  } else if ((sim->strategy->as_q3(sim) != NULL)) {
    qa_q3_game_entity *record = qa_app_q3_source_runtime_record_by_actor(sim->source.q3, actor);
    if (!record || !record->client) wl_fail("Q3 source view has no player state");
    record->client->ps.viewangles.z = (float)roll;
  }
  qa_app_sim_movement_player_set_source_view_roll(movement, roll);
}
void qa_app_shared_sim_advance_q1_punch(qa_app_shared_simulation *sim) {
  const qa_numeric_operations *numeric = qa_create_numeric_operations(&qa_q1_donor_profile);
  size_t count; qa_actor_id *players = wl_players(sim, &count);
  bool netquake = (sim->strategy->as_quakec(sim) != NULL) && qa_ss_is(qa_app_sim_quakec_source_kind(sim->source.quakec), "netquake");
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = players[i];
    qa_owned_actor owned;
    qa_app_sim_movement_player *player = qa_session_actor_registry_resolve_owned(sim->actors, actor, &owned) ? qa_app_shared_sim_player_state(sim, owned) : NULL;
    bool netquake_state = player && player->state.kind == QA_MOVEMENT_STATE_Q1;
    if (player && (player->has_cutscene || player->intermission || (netquake_state && player->state.as.q1.move_type == 0))) continue;
    /* Native NetQuake ClientThink owns DropPunchAngle and writes its result to QC memory. */
    if (netquake && (netquake_state || !qa_app_sim_quakec_source_client_punch_advances(sim->source.quakec, actor))) continue;
    qa_vec3 punch_angles = qa_app_sim_q1_player_punch_advance(sim->q1_punch, actor, qa_ss_seconds(sim->source_frame.elapsed), *numeric);
    if (netquake_state) player->state.as.q1.punch_angles = punch_angles;
  }
  free(players);
}

/* ---- Adjunct host runtime (ActorHostRuntime over an adjunct clock) ---- */
typedef struct wl_clock { qa_frame_context frame; bool advanced; } wl_clock;
typedef struct wl_runtime {
  qa_app_shared_simulation *sim;
  qa_frame_context *frame;   /* the adjunct clock's frame */
  double frame_seconds;      /* NaN: seconds(clock.frame.elapsed) */
  bool shared_time;          /* now = this.timeSeconds (guest monster adjunct) */
} wl_runtime;
static double wl_runtime_now(void *ctx) {
  wl_runtime *runtime = ctx;
  return runtime->shared_time ? wl_time(runtime->sim) : qa_ss_seconds(runtime->frame->time);
}
static double wl_runtime_frame_seconds(void *ctx) {
  wl_runtime *runtime = ctx;
  return isnan(runtime->frame_seconds) ? qa_ss_seconds(runtime->frame->elapsed) : runtime->frame_seconds;
}
static void wl_runtime_schedule(void *ctx, qa_owned_actor actor, const double *due_seconds) {
  wl_runtime *runtime = ctx;
  if (due_seconds == NULL) qa_frame_scheduler_cancel(runtime->sim->scheduler, actor);
  else qa_app_shared_sim_schedule(runtime->sim, actor, *due_seconds);
}
static qa_app_sim_actor_host_runtime wl_make_runtime(qa_app_shared_simulation *sim, qa_numeric_profile numeric,
    qa_app_sim_source_random *random, qa_frame_context *frame, double frame_seconds, bool shared_time) {
  wl_runtime *record = qa_app_shared_sim_keep(sim, sizeof *record);
  *record = (wl_runtime){sim, frame, frame_seconds, shared_time};
  return (qa_app_sim_actor_host_runtime){numeric, random, record, wl_runtime_now, wl_runtime_frame_seconds, wl_runtime_schedule};
}
/* The initial adjunct frame: providerFrame({ ...sourceFrame, elapsed: 0 s }, world.clock, timing.clock). */
static qa_frame_context wl_initial_frame(qa_app_shared_simulation *sim, qa_clock_profile provider_clock) {
  qa_frame_context frame = sim->source_frame;
  frame.elapsed = (qa_source_time){QA_SOURCE_TIME_SECONDS, 0};
  qa_provider_timing world = qa_app_sim_provider_timing(sim->recipe, sim->recipe.map.entities.provider);
  return qa_app_sim_provider_frame(frame, world.clock, provider_clock);
}
static const char *wl_campaign_provider(const qa_app_shared_simulation *sim) {
  return sim->recipe.campaign.is_campaign ? sim->recipe.campaign.mission.provider : sim->recipe.map.entities.provider;
}
/* TS this.events.message({ kind: "q2-inventory", counts }, actor). */
static void wl_q2_inventory_message(qa_app_shared_simulation *sim, qa_actor_id actor) {
  size_t count = 0; const qa_inventory_entry *entries = qa_shared_inventory_table_entries(sim->inventory, actor, &count);
  double *counts = wl_alloc(count, sizeof *counts);
  for (size_t i = 0; i < count; i++) counts[i] = entries[i].count;
  qa_network_event event; memset(&event, 0, sizeof event);
  event.kind = QA_NETWORK_EVENT_Q2_INVENTORY; event.counts = (qa_double_view){counts, count};
  qa_app_sim_simulation_events_message(sim->events, event, true, actor, false, 0);
  free(counts);
}
static void wl_emit_missionpack_player(qa_app_shared_simulation *sim, qa_content_id content, const qa_q2mp_player_effect *effect,
    qa_source_time time) {
  qa_q2_composition_event composition; memset(&composition, 0, sizeof composition);
  composition.kind = QA_Q2_COMPOSITION_EVENT_MISSIONPACK_PLAYER; composition.player_effect = effect;
  qa_app_sim_source_event_input input; memset(&input, 0, sizeof input);
  input.kind = "q2-composition"; input.q2_composition = &composition;
  qa_app_shared_sim_emit(sim, content, input, &time, NULL);
}
/* this.strategy.foreignPowerups(actor) as the Q2 monster adjuncts read it. */
static qa_q2mpm_powerups wl_foreign_powerups(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_app_source_foreign_powerups own = sim->strategy->foreign_powerups(sim, actor);
  return (qa_q2mpm_powerups){own.quad_until, own.double_until, own.invulnerability_until};
}
/* Q3Strategy.foreignPowerups: the native powerup timers (milliseconds) in seconds. */
qa_app_source_foreign_powerups qa_ss_q3_foreign_powerups(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_app_source_foreign_powerups powers = {0, 0, 0};
  qa_q3_game_entity *native = wl_q3_native(sim, actor);
  if (native && native->client) {
    powers.quad_until = qa_q3_base_player_state_slots_get(&native->client->ps.powerups, QA_Q3_PW_QUAD) / 1000;
    powers.invulnerability_until = qa_q3_base_player_state_slots_get(&native->client->ps.powerups, QA_Q3_PW_INVULNERABILITY) / 1000;
  }
  return powers;
}

/* ---- Foreign Q2 world (1220-1267) ---- */
typedef struct wl_foreign_q2 {
  qa_app_shared_simulation *sim;
  qa_provider_reference reference;
  qa_q2_foundation_host host;
  qa_provider_timing timing;
  qa_q2_game_options options;
} wl_foreign_q2;
static void wl_fq2_emit(void *ctx, qa_q2w_event event) { wl_foreign_q2 *world = ctx; qa_app_shared_sim_weapon_event(world->sim, world->reference.content, &event); }
static void wl_fq2_dodge(void *ctx, qa_actor_id monster, qa_actor_id attacker, double eta, qa_trace_result trace) {
  qa_app_shared_sim_q2_monster_dodge(((wl_foreign_q2 *)ctx)->sim, monster, &attacker, eta, &trace);
}
static void wl_fq2_ammo_changed(void *ctx, qa_actor_id actor, qa_item_id ammo) {
  (void)ammo; /* TS ammoChanged: actor => ... (the ammo argument is unused) */
  wl_q2_inventory_message(((wl_foreign_q2 *)ctx)->sim, actor);
}
static bool wl_can_target(void *ctx, const qa_actor_id *attacker, qa_actor_id target) {
  (void)ctx; return attacker == NULL || !qa_same_actor(*attacker, target);
}
static const qa_monster_mission *wl_mission(void *ctx, qa_actor_id actor) {
  return qa_ss_map_get(&((wl_foreign_q2 *)ctx)->sim->monster_missions, &actor);
}
static double wl_fq2_gravity(void *ctx) { return qa_app_sim_shared_physics_gravity(((wl_foreign_q2 *)ctx)->sim->physics); }
static qa_q2mpm_powerups wl_fq2_powerups(void *ctx, qa_actor_id actor) { return wl_foreign_powerups(((wl_foreign_q2 *)ctx)->sim, actor); }
static void wl_fq2_player_effect(void *ctx, qa_q2mp_player_effect event) {
  wl_foreign_q2 *world = ctx;
  wl_emit_missionpack_player(world->sim, world->reference.content, &event, world->sim->foreign_world.clock.frame.time);
}
static qa_q2m_state *wl_fq2_monster(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((wl_foreign_q2 *)ctx)->sim;
  if (!sim->has_foreign_world || !sim->foreign_world.modules) return NULL;
  qa_q2m_context *context = qa_q2m_monsters_context(&sim->foreign_world.modules->monsters, actor);
  return context ? context->state : NULL;
}
static qa_q2_foundation *wl_fq2_create_game(void *ctx, qa_q2m_monsters *monsters, const qa_q2_spawn_module *modules, size_t module_count) {
  (void)monsters; /* the host reads this world's monsters through wl_fq2_monster once the modules exist */
  wl_foreign_q2 *world = ctx;
  return qa_q2_entity_services_create(&world->host, world->options, modules, module_count);
}
static void wl_fq2_teleport_player(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles) {
  qa_app_shared_simulation *sim = ((wl_foreign_q2 *)ctx)->sim;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_body_state body; bool has_body = qa_app_shared_sim_body(sim, actor, &body);
  qa_q2_player_movement_change change; memset(&change, 0, sizeof change);
  change.kind = "teleport"; change.origin = origin; change.angles = angles; change.velocity = has_body ? body.velocity : qa_ss_zero;
  change.command_angles = player->command_angles; change.hold_milliseconds = 160; change.spectator = false;
  qa_app_shared_sim_set_player_movement(sim, actor, change);
}
static void wl_fq2_player_push(void *ctx, qa_actor_id actor, qa_vec3 velocity) {
  qa_app_shared_simulation *sim = ((wl_foreign_q2 *)ctx)->sim;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_body_state body;
  if (!qa_app_shared_sim_body(sim, actor, &body)) return;
  body.velocity = velocity;
  qa_shared_body_table_write(wl_bodies(sim), player->actor, body);
  player->state = qa_app_sim_movement_player_read_state(player);
}
static void wl_fq2_set_actor_gravity(void *ctx, qa_actor_id actor, double gravity) {
  qa_app_shared_simulation *sim = ((wl_foreign_q2 *)ctx)->sim;
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  if (player) {
    player->gravity_multiplier = gravity;
    double scaled = trunc(qa_app_sim_shared_physics_gravity(sim->physics) * gravity);
    if (player->state.kind == QA_MOVEMENT_STATE_Q2) player->state.as.q2.gravity = scaled;
    else if (player->state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) player->state.as.q2_rerelease.gravity = scaled;
    else if (player->state.kind == QA_MOVEMENT_STATE_Q3) player->state.as.q3.gravity = scaled;
    return;
  }
  if (!sim->has_foreign_world) return;
  qa_q2_game_services *game = qa_q2_entity_services_game(sim->foreign_world.modules->game);
  qa_q2_entity *entity = game->entity(game->ctx, &actor);
  if (entity) entity->gravity = gravity;
}
/* itemHooks: { weaponPicked: () => undefined, silencer: () => undefined, powerArmor: () => undefined } */
static void wl_item_weapon_picked(void *ctx, qa_actor_id player, qa_item_id item, bool first) { (void)ctx; (void)player; (void)item; (void)first; }
static void wl_item_silencer(void *ctx, qa_actor_id player, double charges) { (void)ctx; (void)player; (void)charges; }
static void wl_item_power_armor(void *ctx, qa_actor_id player, const char *kind) { (void)ctx; (void)player; (void)kind; }
/* The adjunct's TS common options. */
static qa_q2_game_options wl_q2_common(qa_app_shared_simulation *sim, qa_provider_id provider, qa_q2_edition edition) {
  qa_q2_game_options options; memset(&options, 0, sizeof options);
  options.provider = provider; options.edition = edition; options.skill = sim->options.skill; options.max_clients = sim->options.max_clients;
  options.campaign = wl_campaign_provider(sim);
  options.combat_provider = sim->recipe.combat.provider; options.inventory_provider = sim->recipe.inventory.provider;
  options.movement_provider = sim->recipe.movement.provider;
  options.mode = qa_ss_is(sim->options.mode, "coop") ? QA_Q2_MODE_COOP : QA_Q2_MODE_SINGLEPLAYER;
  options.map_name = sim->recipe.map.geometry.requested_path; options.deathmatch_flags = 0;
  return options;
}
static void wl_create_foreign_q2_world(qa_app_shared_simulation *sim) {
  if (sim->has_foreign_world) return;
  qa_provider_reference reference; const char *edition, *program;
  if (!qa_app_shared_sim_foreign_q2_identity_full(sim, &reference, &edition, &program)) return;
  bool rerelease = qa_ss_is(edition, "rerelease");
  wl_foreign_q2 *world = qa_app_shared_sim_keep(sim, sizeof *world);
  world->sim = sim; world->reference = reference;
  if (!qa_native_provider_timing(reference, QA_GAME_FAMILY_Q2, rerelease, &world->timing)) qa_range_error("Out of memory");
  qa_app_sim_source_random *random = qa_app_sim_source_random_create(sim->options.seed, rerelease ? "q2-rerelease" : "classic");
  qa_ss_foreign_world *foreign = &sim->foreign_world;
  memset(foreign, 0, sizeof *foreign);
  foreign->reference = reference; foreign->edition = edition; foreign->program = program; foreign->random = random;
  foreign->clock.frame = wl_initial_frame(sim, world->timing.clock); foreign->clock.advanced = false;
  qa_app_sim_actor_host_runtime runtime = wl_make_runtime(sim, world->timing.numeric, random, &foreign->clock.frame, rerelease ? 0.025 : 0.1, false);
  world->host = qa_app_shared_sim_q2_actor_host(sim, reference, runtime, wl_fq2_monster, world, NULL);
  world->options = wl_q2_common(sim, reference.provider, rerelease ? QA_Q2_EDITION_RERELEASE : QA_Q2_EDITION_CLASSIC);
  qa_app_sim_foreign_q2_world_options options; memset(&options, 0, sizeof options);
  options.edition = world->options.edition; options.program = program;
  options.weapons.ctx = world; options.weapons.emit = wl_fq2_emit; options.weapons.dodge = wl_fq2_dodge;
  options.weapons.ammo_changed = wl_fq2_ammo_changed; options.weapons.lag_is_history = false; options.weapons.can_target = wl_can_target;
  options.behavior.ctx = world; options.behavior.mission = wl_mission;
  options.item_hooks.ctx = world; options.item_hooks.weapon_picked = wl_item_weapon_picked; options.item_hooks.silencer = wl_item_silencer;
  options.item_hooks.power_armor = wl_item_power_armor;
  options.ctx = world; options.create_game = wl_fq2_create_game; options.gravity = wl_fq2_gravity; options.powerups = wl_fq2_powerups;
  options.player_effect = wl_fq2_player_effect; options.teleport_player = wl_fq2_teleport_player; options.player_push = wl_fq2_player_push;
  options.set_actor_gravity = wl_fq2_set_actor_gravity;
  sim->has_foreign_world = true; sim->b1_foreign_q2 = world;
  foreign->modules = qa_app_sim_create_foreign_q2_world_modules(options);
}

/* ---- Foreign Q1 world (1272-1325) ---- */
typedef struct wl_foreign_q1 {
  qa_app_shared_simulation *sim;
  qa_content_id content; qa_provider_id campaign;
  qa_provider_timing timing;
  qa_comp_q1_services services;
  qa_q1_options foundation;
} wl_foreign_q1;
static double wl_fq1_cvar(void *ctx, const char *name) { return qa_cvar_registry_variable_value(((wl_foreign_q1 *)ctx)->sim->foreign_q1_world.cvars, name); }
static void wl_fq1_set_cvar(void *ctx, const char *name, const char *value) {
  qa_app_shared_simulation *sim = ((wl_foreign_q1 *)ctx)->sim;
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(sim->foreign_q1_world.cvars, name, value, true, &snapshot)) qa_cvar_read_free(&snapshot);
  if (qa_ss_is(name, "skill")) {
    double skill = qa_cvar_registry_variable_value(sim->foreign_q1_world.cvars, name);
    if (skill != 0 && skill != 1 && skill != 2 && skill != 3) wl_fail("Q1 skill must be 0..3");
    sim->foreign_q1_world.campaign.skill = (int)skill;
  }
}
static void wl_fq1_emit(void *ctx, const qa_comp_q1_event *event) {
  wl_foreign_q1 *world = ctx; qa_app_shared_simulation *sim = world->sim;
  qa_app_sim_source_event_input input; memset(&input, 0, sizeof input);
  if (event->kind == QA_COMP_Q1_EVENT_LEVEL_PRESENTATION) {
    input.kind = "q1-level"; input.q1_level = event->finale;
    qa_app_shared_sim_emit(sim, world->content, input, NULL, NULL);
    return;
  }
  if (event->kind == QA_COMP_Q1_EVENT_ADDON) {
    qa_app_shared_sim_emit_q1_addon(sim, world->content, event->addon_event, sim->foreign_q1_world.clock.frame.time);
    return;
  }
  input.kind = "q1-composition"; input.q1_composition = event;
  qa_app_shared_sim_emit(sim, world->content, input, NULL, NULL);
}
static qa_comp_q1_selected_player wl_fq1_selected_player(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((wl_foreign_q1 *)ctx)->sim;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_q1_character_actor **character = qa_ss_map_get(&sim->q1_characters, &player->actor);
  const char *life = character ? qa_q1_character_actor_life(*character) : NULL;
  qa_comp_q1_selected_player selected; memset(&selected, 0, sizeof selected);
  selected.dead_flag = qa_ss_is(life, "dying") ? 1 : qa_ss_is(life, "dead") ? 2 : qa_ss_is(life, "respawnable") ? 3 : wl_health(sim, actor) > 0 ? 0 : 2;
  selected.is_bot = qa_app_sim_bot_services_is_bot(&sim->bot_services, actor);
  selected.view_angles = player->view_angles;
  qa_q2_base_player_view *view = qa_ss_map_get(&sim->q2_views, &actor);
  selected.view_offset = character ? qa_q1_character_actor_view_offset(*character) : view ? view->offset : (qa_vec3){0, 0, player->view_height};
  selected.frame = qa_ss_is(player->animation.state.kind, "q1") || qa_ss_is(player->animation.state.kind, "q2") ? player->animation.state.frame : 0;
  double water_type = player->water_type;
  selected.water_type = water_type == -3 || water_type == 32 ? "water" : water_type == -4 || water_type == 16 ? "slime"
    : water_type == -5 || water_type == 8 ? "lava" : "empty";
  selected.water_level = player->water_level == 3 ? 3 : player->water_level == 2 ? 2 : player->water_level == 1 ? 1 : 0;
  selected.teleport_until = player->state.kind == QA_MOVEMENT_STATE_Q1 ? player->state.as.q1.teleport_time_seconds : 0;
  return selected;
}
static void wl_fq1_set_observer(void *ctx, qa_actor_id actor, bool enabled) {
  qa_app_shared_simulation *sim = ((wl_foreign_q1 *)ctx)->sim;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_q2_player_movement_change change; memset(&change, 0, sizeof change);
  change.kind = "noclip"; change.enabled = enabled;
  qa_app_shared_sim_set_player_movement(sim, actor, change);
  qa_combat_trait_changes traits; memset(&traits, 0, sizeof traits);
  traits.has_can_take_damage = true; traits.can_take_damage = !enabled;
  qa_gameplay_authority_set_traits(sim->combat, player->actor, traits);
  qa_shared_body_table_link(wl_bodies(sim), player->actor, NULL);
}
static void wl_fq1_place_player(void *ctx, qa_owned_actor actor, qa_q1_actor *spot, const qa_q1_travel_state *travel) {
  qa_app_shared_simulation *sim = ((wl_foreign_q1 *)ctx)->sim;
  qa_app_shared_sim_place_q1_player(sim, qa_app_shared_sim_require_player(sim, actor.id), spot, travel);
}
static void wl_fq1_disconnect(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((wl_foreign_q1 *)ctx)->sim;
  qa_session_actor_registry_release(sim->actors, qa_app_shared_sim_require_player(sim, actor)->actor);
}
static void wl_fq1_teleport(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles, qa_vec3 velocity, double until) {
  qa_app_shared_simulation *sim = ((wl_foreign_q1 *)ctx)->sim;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_q2_player_movement_change change; memset(&change, 0, sizeof change);
  change.kind = "teleport"; change.origin = origin; change.angles = angles; change.velocity = velocity;
  change.command_angles = player->command_angles; change.hold_milliseconds = qa_js_max(0, (until - wl_time(sim)) * 1000);
  qa_app_shared_sim_set_player_movement(sim, actor, change);
}
/* weaponServices: () => modules.game */
static qa_q1_services *wl_fq1_weapon_services(void *ctx, qa_actor_id actor) {
  (void)actor; /* TS weaponServices: () => modules.game */
  return ((wl_foreign_q1 *)ctx)->sim->foreign_q1_world.modules->game;
}
static bool wl_fq1_cheat_arsenal(void *ctx, qa_actor_id actor, const char *category) {
  qa_app_shared_simulation *sim = ((wl_foreign_q1 *)ctx)->sim;
  if (category == NULL) return qa_app_shared_sim_grant_selected_arsenal(sim, actor, "weapons") && qa_app_shared_sim_grant_selected_arsenal(sim, actor, "ammo");
  return qa_app_shared_sim_grant_selected_arsenal(sim, actor, category);
}
static bool wl_fq1_give_selected_item(void *ctx, qa_actor_id actor, const char *const *args, size_t count) {
  return qa_app_shared_sim_give_selected_item(((wl_foreign_q1 *)ctx)->sim, actor, args, count);
}
static qa_item_id wl_fq1_selected_weapon(void *ctx, qa_actor_id actor) { return qa_app_shared_sim_player_ui(((wl_foreign_q1 *)ctx)->sim, actor).active_weapon; }
static qa_item_id wl_fq1_selected_ammo(void *ctx, qa_actor_id actor) {
  qa_app_sim_player_ui ui = qa_app_shared_sim_player_ui(((wl_foreign_q1 *)ctx)->sim, actor);
  return ui.has_ammo ? ui.ammo_item : NULL;
}
static bool wl_fq1_select_weapon(void *ctx, qa_actor_id actor, qa_item_id item) {
  qa_app_shared_simulation *sim = ((wl_foreign_q1 *)ctx)->sim;
  return sim->has_selected_arsenal && qa_app_shared_sim_selected_arsenal_select(sim, actor, item);
}
static void wl_fq1_weapon_changed(void *ctx, qa_actor_id actor, qa_item_id acquired) {
  (void)acquired; /* TS weaponChanged: actor => ... */
  qa_app_shared_simulation *sim = ((wl_foreign_q1 *)ctx)->sim;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  player->arsenal = qa_app_shared_sim_arsenal(sim, player);
}
static bool wl_fq1_prompt_supported(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((wl_foreign_q1 *)ctx)->sim;
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  return player != NULL && sim->options.prompt_supported != NULL && sim->options.prompt_supported(sim->options.prompt_ctx, player->client);
}
static void wl_fq1_restart_session(void *ctx, const char *map, double flags) {
  wl_foreign_q1 *world = ctx; qa_app_shared_simulation *sim = world->sim;
  sim->q1_restart = true; sim->foreign_q1_world.campaign.flags = flags;
  char name[512]; snprintf(name, sizeof name, "q1:%s", map);
  qa_transition_intent intent; memset(&intent, 0, sizeof intent);
  intent.kind = "campaign-level"; intent.campaign = world->campaign; intent.map = qa_app_shared_sim_intern(sim, name);
  intent.spawn_point = ""; intent.gate_count = 0; intent.cause = NULL;
  qa_event_ring_admit(&sim->transitions, &intent);
}
static void wl_fq1_finish_campaign(void *ctx) {
  wl_foreign_q1 *world = ctx;
  qa_transition_intent intent; memset(&intent, 0, sizeof intent);
  intent.kind = "campaign-complete"; intent.campaign = world->campaign; intent.gate_count = 0;
  qa_event_ring_admit(&world->sim->transitions, &intent);
}
static void wl_print_message(void *ctx, const char *text) { qa_app_shared_sim_print(ctx, text, NULL); }
static const char *const wl_q1_cvar_names[] = {"skill", "deathmatch", "coop", "teamplay", "sv_gravity", "sv_maxspeed", "samelevel",
  "timelimit", "fraglimit", "gamecfg", "sv_cheats", "footsteps"};
static void wl_create_foreign_q1_world(qa_app_shared_simulation *sim) {
  if (sim->has_foreign_q1_world) return;
  qa_provider_reference reference; const char *edition, *program;
  if (!qa_app_shared_sim_foreign_q1_identity(sim, &reference, &edition, &program)) return;
  bool rerelease = qa_ss_is(edition, "rerelease");
  wl_foreign_q1 *world = qa_app_shared_sim_keep(sim, sizeof *world);
  world->sim = sim; world->content = reference.content; world->campaign = wl_campaign_provider(sim);
  if (!qa_native_provider_timing(reference, QA_GAME_FAMILY_Q1, rerelease, &world->timing)) qa_range_error("Out of memory");
  qa_ss_foreign_q1_world *foreign = &sim->foreign_q1_world;
  memset(foreign, 0, sizeof *foreign);
  foreign->reference = reference; foreign->edition = edition; foreign->program = program;
  foreign->random = qa_app_sim_source_random_create(sim->options.seed, "classic");
  foreign->clock.frame = wl_initial_frame(sim, world->timing.clock); foreign->clock.advanced = false;
  qa_app_sim_actor_host_runtime runtime = wl_make_runtime(sim, world->timing.numeric, foreign->random, &foreign->clock.frame, NAN, false);
  qa_q1_campaign_state_init(&foreign->campaign, 0, (int)sim->options.skill);
  qa_cvar_registry_options cvar_options; memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q1_NETQUAKE;
  cvar_options.context.session = sim->session; cvar_options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  cvar_options.print = wl_print_message; cvar_options.print_ctx = sim;
  foreign->cvars = qa_cvar_registry_create(&cvar_options);
  if (!foreign->cvars) qa_range_error("Out of memory");
  char skill[32];
  const char *values[] = {qa_ss_number_text(sim->options.skill, skill), qa_fm_deathmatch(sim) ? "1" : "0", qa_ss_is(sim->options.mode, "coop") ? "1" : "0",
    "0", "800", "320", "0", "0", "0", "0", "0", "1"};
  for (size_t i = 0; i < 12; i++) {
    qa_cvar_snapshot snapshot;
    if (qa_cvar_registry_register(foreign->cvars, wl_q1_cvar_names[i], values[i], 0, &snapshot)) qa_cvar_read_free(&snapshot);
  }
  foreign->cvar_names = (const char **)wl_q1_cvar_names; foreign->cvar_name_count = 12;
  qa_comp_q1_services *services = &world->services;
  services->ctx = world; services->shared_grapple = qa_app_shared_sim_shared_grapple(sim);
  services->cvar = wl_fq1_cvar; services->set_cvar = wl_fq1_set_cvar; services->emit = wl_fq1_emit;
  services->selected_player = wl_fq1_selected_player; services->set_observer = wl_fq1_set_observer;
  services->place_player = wl_fq1_place_player; services->disconnect = wl_fq1_disconnect; services->teleport = wl_fq1_teleport;
  services->weapon_services = wl_fq1_weapon_services; services->cheat_arsenal = wl_fq1_cheat_arsenal;
  services->give_selected_item = wl_fq1_give_selected_item; services->selected_weapon = wl_fq1_selected_weapon;
  services->selected_ammo = wl_fq1_selected_ammo; services->select_weapon = wl_fq1_select_weapon;
  services->weapon_changed = wl_fq1_weapon_changed; services->prompt_supported = wl_fq1_prompt_supported;
  services->restart_session = wl_fq1_restart_session; services->finish_campaign = wl_fq1_finish_campaign;
  qa_q1_options *foundation = &world->foundation;
  foundation->provider = reference.provider; foundation->edition = edition; foundation->skill = sim->options.skill;
  foundation->physics_edition = qa_app_shared_sim_q1_physics_edition(sim);
  foundation->deathmatch = qa_fm_deathmatch(sim) ? 1 : 0; foundation->coop = qa_ss_is(sim->options.mode, "coop");
  foundation->max_clients = sim->options.max_clients; foundation->has_max_clients = true;
  foundation->campaign = world->campaign; foundation->combat_provider = sim->recipe.combat.provider;
  foundation->movement_provider = sim->recipe.movement.provider; foundation->inventory_provider = sim->recipe.inventory.provider;
  foundation->gravity = 800;
  static const char *const programs[] = {"id1", "hipnotic", "rogue", "dopa", "mg1", "mg3", "ctf"};
  qa_comp_q1_program index = QA_COMP_Q1_ID1;
  for (int i = 0; i < 7; i++) if (qa_ss_is(program, programs[i])) index = (qa_comp_q1_program)i;
  sim->has_foreign_q1_world = true; sim->b1_foreign_q1 = world;
  foreign->modules = qa_app_sim_create_foreign_q1_world_modules(qa_app_shared_sim_q1_actor_host(sim, reference, runtime, NULL),
      foundation, index, &foreign->campaign.binding, services);
}
void qa_app_shared_sim_create_foreign_world(qa_app_shared_simulation *sim) {
  wl_create_foreign_q2_world(sim);
  wl_create_foreign_q1_world(sim);
}

/* ---- spawnForeignWorld (1338-1360) ---- */
static void wl_report_unsupported(const char *const *classnames, size_t count) {
  size_t length = 1;
  for (size_t i = 0; i < count; i++) length += strlen(classnames[i]) + 2;
  char *joined = wl_alloc(length, 1); size_t written = 0;
  for (size_t i = 0; i < count; i++) {
    bool seen = false;
    for (size_t j = 0; j < i && !seen; j++) seen = qa_ss_is(classnames[j], classnames[i]);
    if (seen) continue;
    if (written++) strcat(joined, ", ");
    strcat(joined, classnames[i]);
  }
  char message[2048];
  snprintf(message, sizeof message, "Unimplemented authored foreign spawns: %s", joined);
  free(joined);
  wl_fail("%s", message);
}
/* ordinals minus the guest lane's selected-monster replacements; malloc'd. */
static double *wl_keep_ordinals(qa_app_shared_simulation *sim, const double *ordinals, size_t ordinal_count, const char *edition, size_t *out_count) {
  const qa_app_sim_guest_monster_plan *plan = (sim->strategy->as_q3(sim) != NULL) ? qa_app_shared_sim_guest_monster_plan(sim, sim->world_entities, edition) : NULL;
  double *keep = wl_alloc(ordinal_count, sizeof *keep); size_t count = 0;
  for (size_t i = 0; i < ordinal_count; i++) {
    bool replaced = false;
    if (plan) for (size_t j = 0; j < plan->replacement_count && !replaced; j++) replaced = plan->replacements[j].ordinal == ordinals[i];
    bool duplicate = false;
    for (size_t j = 0; j < count && !duplicate; j++) duplicate = keep[j] == ordinals[i];
    if (!replaced && !duplicate) keep[count++] = ordinals[i];
  }
  *out_count = count;
  return keep;
}
void qa_app_shared_sim_spawn_foreign_world(qa_app_shared_simulation *sim, const double *ordinals, size_t ordinal_count, const char *entities) {
  if (sim->has_foreign_world) {
    size_t keep_count; double *keep = wl_keep_ordinals(sim, ordinals, ordinal_count, sim->foreign_world.edition, &keep_count);
    if (keep_count == 0) { free(keep); return; }
    qa_q2_spawn_report report = qa_q2_foundation_load(sim->foreign_world.modules->game, entities, keep, keep_count);
    free(keep);
    if (report.unsupported_count != 0) {
      const char **names = wl_alloc(report.unsupported_count, sizeof *names);
      for (size_t i = 0; i < report.unsupported_count; i++) names[i] = report.unsupported[i]->classname;
      wl_report_unsupported(names, report.unsupported_count);
    }
    return;
  }
  if (sim->has_foreign_q1_world) {
    size_t keep_count; double *keep = wl_keep_ordinals(sim, ordinals, ordinal_count, sim->foreign_q1_world.edition, &keep_count);
    if (keep_count == 0) { free(keep); return; }
    size_t entity_count = 0;
    qa_q1_entity *list = qa_q1_parse_entities(entities, &entity_count);
    const qa_q1_world_geometry *geometry = sim->world.as.q1;
    qa_bounds *bounds = wl_alloc(geometry->model_count, sizeof *bounds);
    for (size_t i = 0; i < geometry->model_count; i++) bounds[i] = geometry->models[i].bounds;
    char *map_name = qa_ss_map_name(sim->recipe.map.geometry.requested_path);
    qa_q1_spawn_report report; memset(&report, 0, sizeof report);
    bool ok = qa_comp_q1_source_composition_spawn_entities(&sim->foreign_q1_world.modules->composition, list, entity_count,
        bounds, geometry->model_count, map_name, keep, keep_count, &report);
    free(keep); free(bounds); free(map_name); qa_q1_entities_destroy(list, entity_count);
    if (!ok) qa_range_error("Out of memory");
    if (report.foreign_count != 0) {
      const char **names = wl_alloc(report.foreign_count, sizeof *names);
      for (size_t i = 0; i < report.foreign_count; i++) names[i] = report.foreign[i].classname;
      wl_report_unsupported(names, report.foreign_count);
    }
    qa_q1_spawn_report_free(&report);
  }
}

/* ---- Adjunct frames (1893-1948) ---- */
void qa_app_shared_sim_begin_foreign_q1_world_frame(qa_app_shared_simulation *sim, bool map) {
  if (!sim->has_foreign_q1_world) return;
  qa_ss_foreign_q1_world *foreign = &sim->foreign_q1_world;
  foreign->clock.advanced = false;
  if (!map) return;
  qa_clock_profile world_clock = qa_app_sim_provider_timing(sim->recipe, sim->recipe.map.entities.provider).clock;
  qa_provider_timing timing;
  if (!qa_native_provider_timing(foreign->reference, QA_GAME_FAMILY_Q1, qa_ss_is(foreign->edition, "rerelease"), &timing)) qa_range_error("Out of memory");
  qa_frame_context projected = qa_app_sim_provider_frame(sim->source_frame, world_clock, timing.clock);
  qa_native_provider_timing_free(&timing);
  foreign->clock.frame = projected; foreign->clock.advanced = true;
  qa_q1_begin_frame(foreign->modules->game, qa_ss_seconds(projected.time), qa_ss_seconds(projected.elapsed));
  qa_comp_q1_source_composition_pre_frame(&foreign->modules->composition, qa_ss_seconds(projected.elapsed));
}
/* The Q2 adjunct clock: advance one native frame when milliseconds reaches it. */
static bool wl_advance_q2_clock(wl_clock *clock, bool classic, double milliseconds) {
  double interval = classic ? 100 : 25;
  double next = qa_js_round(qa_ss_seconds(clock->frame.time) * 1000) + interval;
  if (milliseconds != next) return false;
  qa_source_time_kind kind = classic ? QA_SOURCE_TIME_SECONDS : QA_SOURCE_TIME_MILLISECONDS;
  double divisor = classic ? 1000 : 1;
  clock->frame = (qa_frame_context){clock->frame.frame + 1, {kind, next / divisor}, {kind, interval / divisor}, QA_FRAME_PHASE_FRAME_ENTRY};
  clock->advanced = true;
  return true;
}
void qa_app_shared_sim_begin_foreign_world_frame(qa_app_shared_simulation *sim, double milliseconds, bool map) {
  if (!sim->has_foreign_world) return;
  qa_ss_foreign_world *foreign = &sim->foreign_world;
  foreign->clock.advanced = false;
  if (!map) return;
  wl_clock clock = {foreign->clock.frame, foreign->clock.advanced};
  if (!wl_advance_q2_clock(&clock, qa_ss_is(foreign->edition, "classic"), milliseconds)) return;
  foreign->clock.frame = clock.frame; foreign->clock.advanced = clock.advanced;
  qa_q2m_monsters_begin_frame(&foreign->modules->monsters, qa_q2_entity_services_game(foreign->modules->game));
}

/* ---- Guest monsters: plans and consumption (1532-1571, 2439-2455) ---- */
/* Edition semantics of the authored map, for guests whose own edition says
 * nothing about the map they host (QVM, TS Q3). */
const char *qa_app_shared_sim_guest_map_edition(qa_app_shared_simulation *sim) {
  return strstr(sim->recipe.map.entities.content, ":rerelease:") ? "rerelease" : "classic";
}
/* Whether shared actors can be projected into this guest. */
bool qa_app_shared_sim_guest_monster_projection(qa_app_shared_simulation *sim, const char *edition) {
  if (sim->options.q3_guest != NULL)
    return qa_app_prepared_q3_primary_combat(((const qa_app_sim_q3_guest_options *)sim->options.q3_guest)->prepared) != NULL;
  if (!sim->options.has_q2_guest) return true;
  if (qa_app_shared_sim_native_primary(sim) != NULL) return true;
  if (!qa_ss_is(edition, "classic")) return false;
  const qa_app_sim_native_q2_guest_options *guest = qa_app_shared_sim_q2_guest(sim);
  if (!guest->prepared) return false;
  const qa_resolved_execution_module *execution = qa_ss_is(guest->edition, "classic")
    ? &((const qa_app_sim_prepared_classic_guest *)guest->prepared)->execution : &((const qa_app_sim_prepared_rerelease_guest *)guest->prepared)->execution;
  const qa_resolved_resource_reference *artifact = execution->artifact;
  return qa_compat_q2_classic_get_combat_profile(artifact->digest) != NULL;
}
/* guestMonsterPlan(text, edition): cached per (edition, text); NULL = TS null. */
const qa_app_sim_guest_monster_plan *qa_app_shared_sim_guest_monster_plan(qa_app_shared_simulation *sim, const char *text, const char *edition) {
  size_t length = strlen(edition) + 1 + strlen(text) + 1;
  char *key = wl_alloc(length, 1);
  /* TS key: edition + "\0" + text. C keys are NUL-terminated, so the
   * separator is a byte no edition name contains. */
  snprintf(key, length, "%s\x01%s", edition, text);
  qa_app_sim_guest_monster_plan **cached = qa_ss_map_get(&sim->guest_monster_plans, &key);
  if (cached) { free(key); return *cached; }
  /* Only native guest spawn sites call this, some before the source runtime
   * exists; gate on the recipe alone so early calls cannot cache an empty plan. */
  qa_app_sim_guest_monster_plan *plan = NULL;
  if (sim->recipe.enemies.kind == QA_ENEMY_SELECTION_REPLACE) {
    const char *family = qa_ss_is(sim->world_kind, "q1-bsp") ? "q1" : qa_ss_is(sim->world_kind, "q3-bsp") ? "q3" : "q2";
    bool projection = qa_app_shared_sim_guest_monster_projection(sim, edition);
    plan = qa_app_sim_plan_guest_monsters(text, qa_ss_is(edition, "rerelease") ? QA_Q2_EDITION_RERELEASE : QA_Q2_EDITION_CLASSIC,
        family, &sim->recipe.enemies, sim->options.skill, sim->options.mode, projection);
  }
  qa_ss_map_set(&sim->guest_monster_plans, &key, &plan);
  free(key);
  return plan;
}
const char *qa_app_shared_sim_guest_monster_entities(qa_app_shared_simulation *sim, const char *text, const char *edition) {
  const qa_app_sim_guest_monster_plan *plan = qa_app_shared_sim_guest_monster_plan(sim, text, edition);
  return plan ? plan->entities : text;
}
/* `${classname}: ${reason}` entries, de-duplicated in order, joined by "; ". */
static char *wl_kept_reasons(const qa_app_sim_guest_monster_plan *plan) {
  size_t length = 1;
  for (size_t i = 0; i < plan->kept_count; i++) length += strlen(plan->kept[i].classname) + strlen(plan->kept[i].reason) + 4;
  char *joined = wl_alloc(length, 1);
  size_t written = 0;
  for (size_t i = 0; i < plan->kept_count; i++) {
    bool seen = false;
    for (size_t j = 0; j < i && !seen; j++)
      seen = qa_ss_is(plan->kept[j].classname, plan->kept[i].classname) && qa_ss_is(plan->kept[j].reason, plan->kept[i].reason);
    if (seen) continue;
    if (written++) strcat(joined, "; ");
    strcat(joined, plan->kept[i].classname); strcat(joined, ": "); strcat(joined, plan->kept[i].reason);
  }
  return joined;
}
static char *wl_format(const char *format, ...) __attribute__((format(printf, 1, 2), nonnull(1)));
static char *wl_format(const char *format, ...) {
  va_list args; va_start(args, format);
  int length = vsnprintf(NULL, 0, format, args);
  va_end(args);
  char *text = wl_alloc((size_t)length + 1, 1);
  va_start(args, format); vsnprintf(text, (size_t)length + 1, format, args); va_end(args);
  return text;
}
bool qa_app_shared_sim_foreign_native_movement(qa_app_shared_simulation *sim) {
  if ((sim->strategy->as_q2_native(sim) == NULL)) return false;
  qa_clock_profile_kind clock = qa_app_sim_provider_timing(sim->recipe, sim->recipe.movement.provider).clock.kind;
  return clock != (qa_ss_is(sim->source.edition, "classic") ? QA_CLOCK_Q2_CLASSIC : QA_CLOCK_Q2_RERELEASE);
}
bool qa_app_shared_sim_foreign_native_character(qa_app_shared_simulation *sim) {
  return (sim->strategy->as_q2_native(sim) != NULL) && qa_app_sim_provider_family(sim->recipe.character.definition.provider) != QA_GAME_FAMILY_Q2;
}
static const char *wl_clock_name(qa_clock_profile_kind kind) {
  switch (kind) {
  case QA_CLOCK_Q1_NETQUAKE: return "q1-netquake";
  case QA_CLOCK_Q1_QUAKEWORLD: return "q1-quakeworld";
  case QA_CLOCK_Q2_CLASSIC: return "q2-classic";
  case QA_CLOCK_Q2_RERELEASE: return "q2-rerelease";
  case QA_CLOCK_Q3: break;
  }
  return "q3";
}
qa_app_shared_sim_consumption qa_app_shared_sim_selection_consumption(qa_app_shared_simulation *sim) {
  qa_app_shared_sim_consumption result; memset(&result, 0, sizeof result);
  if ((sim->strategy->as_q2_native(sim) == NULL)) return result;
  result.entries = wl_alloc(3, sizeof *result.entries);
  const qa_enemy_selection *enemies = &sim->recipe.enemies;
  const qa_app_sim_guest_monster_plan *plan = sim->guest_monsters_admitted && enemies->kind == QA_ENEMY_SELECTION_REPLACE
    ? qa_app_shared_sim_guest_monster_plan(sim, sim->world_entities, sim->source.edition) : NULL;
  if (plan) {
    char *reasons = wl_kept_reasons(plan);
    qa_app_shared_sim_selection_consumption_entry *entry = &result.entries[result.count++];
    entry->axis = "enemies";
    entry->requested = enemies->default_target.is_map_defined ? "map-defined" : enemies->default_target.reference.source.provider;
    entry->outcome = plan->kept_count == 0 ? "honored" : plan->replacement_count == 0 ? "guest-native" : "partial";
    char replaced[40], kept[40];
    if (reasons[0] == '\0') entry->detail = wl_format("%s replaced; %s guest-native", qa_ss_number_text((double)plan->replacement_count, replaced),
        qa_ss_number_text((double)plan->kept_count, kept));
    else entry->detail = wl_format("%s replaced; %s guest-native (%s)", qa_ss_number_text((double)plan->replacement_count, replaced),
        qa_ss_number_text((double)plan->kept_count, kept), reasons);
    free(reasons);
  }
  const char *movement_clock = wl_clock_name(qa_app_sim_provider_timing(sim->recipe, sim->recipe.movement.provider).clock.kind);
  const char *native_clock = qa_ss_is(sim->source.edition, "classic") ? "q2-classic" : "q2-rerelease";
  if (qa_app_shared_sim_foreign_native_movement(sim)) {
    qa_app_shared_sim_selection_consumption_entry *entry = &result.entries[result.count++];
    entry->axis = "movement"; entry->requested = sim->recipe.movement.provider; entry->outcome = "honored";
    entry->detail = wl_format("%s interposed at the guest Pmove%s", movement_clock, sim->native_mirrored_combat.count == 0 ? ""
      : "; combat mirror follows guest health/armor/invulnerability, team null, shared damage writes discarded");
  } else if (qa_ss_is(movement_clock, native_clock) && !qa_ss_is(sim->recipe.movement.content, sim->recipe.map.entities.content)) {
    qa_app_shared_sim_selection_consumption_entry *entry = &result.entries[result.count++];
    entry->axis = "movement"; entry->requested = sim->recipe.movement.provider; entry->outcome = "guest-native";
    entry->detail = wl_format("shares the guest %s clock but is a different provider than %s; the guest Pmove runs", native_clock,
      sim->recipe.map.entities.content);
  }
  if (qa_app_shared_sim_foreign_native_character(sim)) {
    qa_app_shared_sim_selection_consumption_entry *entry = &result.entries[result.count++];
    entry->axis = "character"; entry->requested = sim->recipe.character.definition.provider; entry->outcome = "honored";
    entry->detail = wl_format("selected character drawn over the guest player edict; corpses and bodies stay guest models");
  }
  return result;
}
void qa_app_shared_sim_selection_consumption_free(qa_app_shared_sim_consumption *consumption) {
  for (size_t i = 0; i < consumption->count; i++) free(consumption->entries[i].detail);
  free(consumption->entries);
  memset(consumption, 0, sizeof *consumption);
}
/* Deferred admission report: lands on the first stepped frame. */
void qa_app_shared_sim_flush_guest_monster_report(qa_app_shared_simulation *sim) {
  if (sim->pending_guest_monster_report == NULL) return;
  char *text = sim->pending_guest_monster_report; sim->pending_guest_monster_report = NULL;
  qa_app_shared_sim_print(sim, text, NULL);
  free(text);
}

/* ---- Combat registration (3885-3962) ---- */
qa_victim_armor_context qa_app_shared_sim_victim_armor_context(qa_app_shared_simulation *sim, qa_damage_request request) {
  qa_app_sim_actor_execution *target = qa_ss_map_get(&sim->actor_executions, &request.target);
  const char *product = target && target->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2
    ? (qa_q2_entity_services_game(target->q2_services)->options.edition == QA_Q2_EDITION_RERELEASE ? "rerelease" : "classic")
    : strstr(sim->recipe.inventory.content, ":rerelease:") ? "rerelease" : "classic";
  qa_body_state body; bool has_body = qa_app_shared_sim_body(sim, request.target, &body);
  qa_vec3 contact = has_body ? qa_ss_subtract(request.point, body.origin) : qa_ss_zero;
  qa_actor_id world;
  bool world_inflictor = request.attack.inflictor && qa_app_shared_sim_world_actor(sim, &world) && qa_same_actor(world, *request.attack.inflictor);
  qa_vec3 direction = request.attack.cause.kind == QA_ATTACK_CAUSE_Q1 && has_body && contact.x == 0 && contact.y == 0 && contact.z == 0
      && request.attack.inflictor != NULL && !world_inflictor
    ? (qa_vec3){-request.direction.x, -request.direction.y, -request.direction.z} : contact;
  double length = qa_js_hypot3(direction.x, direction.y, direction.z), yaw = (has_body ? body.angles.y : 0) * M_PI / 180;
  qa_victim_armor_context context; memset(&context, 0, sizeof context);
  context.arithmetic = "binary32";
  context.has_q2 = true; context.q2_product = product; context.q2_ctf = qa_ss_is(sim->recipe.match.provider, "q2:ctf");
  context.q2_alive = wl_health(sim, request.target) > 0;
  context.screen_facing_dot = length == 0 ? 0 : (direction.x * cos(yaw) + direction.y * sin(yaw)) / length;
  return context;
}
static qa_victim_armor_context wl_armor_context(void *ctx, qa_damage_request request) { return qa_app_shared_sim_victim_armor_context(ctx, request); }
static void wl_tag_damage(void *ctx, const void *input, void *output) {
  qa_app_shared_simulation *sim = ctx; const qa_damage_request *request = input; qa_damage_request *out = output;
  *out = *request;
  if (!sim->has_tag_owner || qa_same_actor(request->target, sim->tag_owner)
      || (request->attack.attacker && qa_same_actor(*request->attack.attacker, sim->tag_owner))) return;
  out->amount = trunc(request->amount * 3 / 4);
}
static void wl_lmctf_damage(void *ctx, const void *input, void *output) {
  qa_app_shared_simulation *sim = ctx; const qa_damage_request *request = input; qa_damage_request *out = output;
  *out = *request;
  double amount = request->amount;
  bool has_attacker = request->attack.attacker != NULL; qa_actor_id attacker = has_attacker ? *request->attack.attacker : request->target;
  /* Native g_runes: Damage multiplies outgoing by 1.75, Resist divides incoming
   * by 1.75, Vampire returns half the health damage dealt. */
  if (has_attacker && qa_ss_is(qa_app_shared_sim_lmctf_rune_held(sim, attacker), "damage")) amount = trunc((float)(amount * 1.75));
  if (qa_ss_is(qa_app_shared_sim_lmctf_rune_held(sim, request->target), "resist")) amount = trunc((float)(amount / 1.75));
  if (has_attacker && !qa_same_actor(attacker, request->target) && amount > 0 && qa_ss_is(qa_app_shared_sim_lmctf_rune_held(sim, attacker), "vampire")) {
    qa_owned_actor owned; qa_combat_state_result health = qa_gameplay_authority_read(sim->combat, attacker);
    if (qa_session_actor_registry_resolve_owned(sim->actors, attacker, &owned) && health.has_value)
      qa_gameplay_authority_set_health(sim->combat, owned, qa_js_min(250, health.value.health + qa_js_shr(amount, 1)));
  }
  if (amount != request->amount) out->amount = amount;
}
static void wl_deathball_damage(void *ctx, const void *input, void *output) {
  qa_app_shared_simulation *sim = ctx; const qa_damage_request *request = input; qa_damage_request *out = output;
  *out = *request;
  /* Native DeathBall: the ball takes 1 damage from every hit. */
  if (sim->has_deathball && qa_same_actor(request->target, sim->deathball.id) && request->amount != 1) out->amount = 1;
}
static void wl_register_transform(qa_app_shared_simulation *sim, const char *provider, const char *id,
    void (*transform)(void *ctx, const void *request, void *out_request)) {
  qa_mod_operation_registration registration; memset(&registration, 0, sizeof registration);
  registration.identity = (qa_mod_registration_identity){provider, id, 0};
  registration.kind = QA_MOD_TRANSFORM; registration.ctx = sim; registration.transform = transform;
  if (qa_mod_operation_register(qa_gameplay_authority_damage_operation(sim->combat), registration) < 0) qa_range_error("Out of memory");
}
/* Q1 policy source effects: the current Q1 combat source's folded effects. */
static bool wl_q1_effects(qa_app_shared_simulation *sim, qa_q1_damage_source_effects *out) {
  qa_q1_services *game = qa_app_shared_sim_q1_combat_source(sim);
  if (!game) return false;
  *out = qa_q1_damage_source_effects_of(game);
  return true;
}
static bool wl_q1_before_quad(void *ctx, qa_damage_request request, double amount, qa_combat_state target, const qa_combat_state *attacker,
    qa_damage_preparation *out) {
  qa_q1_damage_source_effects effects;
  if (wl_q1_effects(ctx, &effects) && effects.before_quad && effects.before_quad(effects.ctx, request, amount, target, attacker, out)) return true;
  *out = (qa_damage_preparation){true, amount};
  return true;
}
static bool wl_q1_after_quad(void *ctx, qa_damage_request request, double amount, qa_combat_state target, const qa_combat_state *attacker,
    qa_damage_preparation *out) {
  qa_q1_damage_source_effects effects;
  if (wl_q1_effects(ctx, &effects) && effects.after_quad && effects.after_quad(effects.ctx, request, amount, target, attacker, out)) return true;
  *out = (qa_damage_preparation){true, amount};
  return true;
}
static bool wl_q1_armor_allowed(void *ctx, qa_damage_request request, double amount, qa_combat_state target, const qa_combat_state *attacker) {
  qa_q1_damage_source_effects effects;
  return wl_q1_effects(ctx, &effects) && effects.armor_allowed ? effects.armor_allowed(effects.ctx, request, amount, target, attacker) : true;
}
static bool wl_q1_protection_applies(void *ctx, qa_damage_request request, qa_combat_state target, const qa_combat_state *attacker) {
  qa_q1_damage_source_effects effects;
  return wl_q1_effects(ctx, &effects) && effects.protection_applies ? effects.protection_applies(effects.ctx, request, target, attacker) : true;
}
static bool wl_q1_before_health(void *ctx, qa_damage_request request, double amount, qa_combat_state target, const qa_combat_state *attacker) {
  qa_q1_damage_source_effects effects;
  return wl_q1_effects(ctx, &effects) && effects.before_health ? effects.before_health(effects.ctx, request, amount, target, attacker) : true;
}
static double wl_q1_after_armor(void *ctx, qa_damage_request request, double take, qa_combat_state target, const qa_combat_state *attacker) {
  qa_q1_damage_source_effects effects;
  return wl_q1_effects(ctx, &effects) && effects.after_armor ? effects.after_armor(effects.ctx, request, take, target, attacker) : take;
}
static qa_q1_combat_context wl_q1_context(void *ctx, qa_damage_request request, qa_combat_state target, const qa_combat_state *attacker) {
  (void)target; (void)attacker; /* TS context: request => ... */
  qa_q1_services *game = qa_app_shared_sim_q1_combat_source(ctx);
  if (game) return qa_q1_combat_context_for(game, request);
  qa_q1_combat_context context; memset(&context, 0, sizeof context);
  context.arithmetic = "binary32"; context.quad = false; context.teamplay = 0; context.walk = false; context.has_momentum_direction = false;
  return context;
}
/* q2MatchDamageEffects(): the Q2 source match's effects (throws without a Q2 source). */
static qa_q2_damage_source_effects wl_q2_match_effects(qa_app_shared_simulation *sim, qa_q2_product_match_effects_binding *storage) {
  return sim->strategy->match_damage_effects(sim, storage);
}
qa_q2_damage_source_effects qa_app_shared_sim_q2_match_damage_effects(qa_app_shared_simulation *sim, qa_q2_product_match_effects_binding *storage) {
  return wl_q2_match_effects(sim, storage);
}
static double wl_q2_before_momentum(void *ctx, qa_damage_request request, double damage, qa_combat_state target, const qa_combat_state *attacker) {
  qa_q2_product_match_effects_binding storage; qa_q2_damage_source_effects effects = wl_q2_match_effects(ctx, &storage);
  return effects.before_momentum ? effects.before_momentum(effects.ctx, request, damage, target, attacker) : damage;
}
static bool wl_q2_power_armor_allowed(void *ctx, qa_damage_request request, qa_combat_state target, const qa_combat_state *attacker) {
  qa_q2_product_match_effects_binding storage; qa_q2_damage_source_effects effects = wl_q2_match_effects(ctx, &storage);
  return effects.power_armor_allowed ? effects.power_armor_allowed(effects.ctx, request, target, attacker) : true;
}
static double wl_q2_after_power_armor(void *ctx, qa_damage_request request, double take, qa_combat_state target, const qa_combat_state *attacker) {
  qa_q2_product_match_effects_binding storage; qa_q2_damage_source_effects effects = wl_q2_match_effects(ctx, &storage);
  return effects.after_power_armor ? effects.after_power_armor(effects.ctx, request, take, target, attacker) : take;
}
static bool wl_q2_armor_allowed(void *ctx, qa_damage_request request, qa_combat_state target, const qa_combat_state *attacker) {
  qa_q2_product_match_effects_binding storage; qa_q2_damage_source_effects effects = wl_q2_match_effects(ctx, &storage);
  return effects.armor_allowed ? effects.armor_allowed(effects.ctx, request, target, attacker) : true;
}
static double wl_q2_after_armor(void *ctx, qa_damage_request request, double take, qa_combat_state target, const qa_combat_state *attacker) {
  qa_q2_product_match_effects_binding storage; qa_q2_damage_source_effects effects = wl_q2_match_effects(ctx, &storage);
  return effects.after_armor ? effects.after_armor(effects.ctx, request, take, target, attacker) : take;
}
static void wl_q2_after_health(void *ctx, qa_damage_decision decision, qa_current_combat_state current) {
  qa_q2_product_match_effects_binding storage; qa_q2_damage_source_effects effects = wl_q2_match_effects(ctx, &storage);
  if (effects.after_health) effects.after_health(effects.ctx, decision, current);
}
static qa_q2_combat_context wl_q2_context(void *ctx, qa_damage_request request, qa_combat_state target, const qa_combat_state *attacker) {
  (void)target; (void)attacker; /* TS context: request => ... */
  qa_app_shared_simulation *sim = ctx;
  qa_q2_combat_context context; memset(&context, 0, sizeof context);
  context.arithmetic = "binary32";
  context.player = qa_app_shared_sim_player(sim, request.target) != NULL;
  context.monster = strncmp(qa_app_shared_sim_classname(sim, request.target), "monster_", 8) == 0;
  context.attacker_player = request.attack.attacker != NULL && qa_app_shared_sim_player(sim, *request.attack.attacker) != NULL;
  context.has_enemy = sim->strategy->has_enemy(sim, request.target);
  context.easy_skill = sim->options.skill == 0; context.deathmatch = qa_fm_deathmatch(sim);
  context.defender_sphere = false;
  context.team_damage_enabled = sim->strategy->team_damage_enabled(sim, sim->recipe.match.provider);
  context.friendly_fire = sim->q2_server_registry == NULL || (qa_js_to_int32(qa_cvar_registry_variable_value(sim->q2_server_registry, "dmflags")) & 256) == 0;
  context.nuke = false; context.no_knockback = false; context.movable = !qa_app_sim_shared_physics_is_brush(sim->physics, request.target);
  context.reject_team_damage = false; context.suppress_pain = false;
  return context;
}
void qa_app_shared_sim_register_combat(qa_app_shared_simulation *sim) {
  qa_provider_id id = sim->recipe.combat.provider;
  qa_victim_armor_policy armor = qa_native_victim_armor(wl_armor_context, sim);
  if (qa_app_shared_sim_foreign_tag(sim)) wl_register_transform(sim, "q2:tag", "q2:tag/damage", wl_tag_damage);
  if (qa_app_shared_sim_foreign_lmctf(sim)) wl_register_transform(sim, "q2:lmctf", "q2:lmctf/runes", wl_lmctf_damage);
  if (qa_app_shared_sim_foreign_deathball(sim)) wl_register_transform(sim, "q2:deathball", "q2:deathball/ball", wl_deathball_damage);
  qa_game_family family = qa_app_sim_provider_family(id);
  if (family == QA_GAME_FAMILY_Q1) {
    qa_q1_combat_policy_options options; memset(&options, 0, sizeof options);
    options.id = id; options.ctx = sim; options.context = wl_q1_context; options.armor = armor; options.has_source_effects = true;
    options.source_effects = (qa_q1_damage_source_effects){sim, wl_q1_before_quad, wl_q1_after_quad, wl_q1_armor_allowed,
      wl_q1_protection_applies, wl_q1_before_health, wl_q1_after_armor, NULL};
    qa_combat_policy policy = qa_create_q1_combat_policy(options);
    qa_app_shared_sim_keep_owned(sim, armor.ctx, free); qa_app_shared_sim_keep_owned(sim, policy.ctx, free); /* TS: GC reclaims both with the simulation. */
    qa_gameplay_authority_register(sim->combat, policy);
  } else if (family == QA_GAME_FAMILY_Q2) {
    qa_q2_combat_policy_options options; memset(&options, 0, sizeof options);
    options.id = id; options.ctx = sim; options.context = wl_q2_context; options.armor = armor;
    if (qa_ss_is(sim->recipe.match.provider, "q2:ctf") || qa_ss_is(sim->recipe.match.provider, "q2:lmctf")) {
      options.has_source_effects = true;
      options.source_effects = (qa_q2_damage_source_effects){sim, wl_q2_before_momentum, wl_q2_power_armor_allowed, wl_q2_after_power_armor,
        wl_q2_armor_allowed, wl_q2_after_armor, wl_q2_after_health};
    }
    qa_combat_policy policy = qa_create_q2_combat_policy(options);
    qa_app_shared_sim_keep_owned(sim, armor.ctx, free); qa_app_shared_sim_keep_owned(sim, policy.ctx, free); /* TS: GC reclaims both with the simulation. */
    qa_gameplay_authority_register(sim->combat, policy);
  } else qa_victim_armor_policy_free(armor); /* TS leaves the unused policy to the GC. */
}

/* ---- Monster targeting (3983-4022, 4062-4076) ---- */
qa_q2_weapon_target_result qa_app_shared_sim_q2_weapon_target(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q2_weapon_target_result result; memset(&result, 0, sizeof result);
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(sim->actors, actor, &owner)) return result;
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor);
  qa_q2_entity *native = entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2 ? entry->q2_entity : NULL;
  qa_app_sim_shared_solid collision; bool has_collision = qa_app_shared_sim_collision(sim, owner, &collision);
  qa_spatial_actor linked; bool has_linked = qa_shared_linked_actor(sim->scene, actor, &linked);
  qa_actor_id world;
  bool is_world = qa_app_shared_sim_world_actor(sim, &world) && qa_same_actor(world, actor);
  bool q3 = entry && (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3 || entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3_SOURCE);
  const char *solid = is_world ? "brush"
    : q3 || (entry == NULL && qa_app_shared_sim_player(sim, actor) == NULL)
      ? !has_linked ? "none" : qa_ss_is(linked.collision.role, "trigger") ? "trigger" : qa_ss_is(linked.collision.shape_kind, "model") ? "brush" : "box"
      : has_collision ? collision.solid : "none";
  result.has_value = true;
  result.value.solid = solid;
  result.value.laser_immune = native ? native->laser_immune : false;
  result.value.damageable_target = native ? native->damageable_target : false;
  result.value.bfg_explobox = entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC
    ? qa_ss_is(qa_app_sim_quakec_source_classname(entry->quakec->ctx, actor), "misc_explobox")
    : entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 ? qa_ss_is(entry->q1_entity->classname, "misc_explobox")
    : entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2 && qa_ss_is(entry->q2_entity->classname, "misc_explobox");
  return result;
}
void qa_app_shared_sim_q2_monster_dodge(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_actor_id *attacker,
    double eta, const qa_trace_result *trace) {
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor);
  if (!entry || entry->kind != QA_APP_SIM_ACTOR_EXECUTION_Q2 || !qa_session_actor_registry_is_live(sim->actors, actor)) return;
  qa_provider_id provider = qa_q2_entity_services_game(entry->q2_services)->options.provider;
  qa_app_sim_selected_monster_source **selected = qa_ss_map_get(&sim->monster_sources, &provider);
  qa_q2m_monsters *monsters = selected && qa_ss_is((*selected)->kind, "q2") && (*selected)->q2->game == entry->q2_services ? &(*selected)->q2->monsters
    : (sim->strategy->as_q2(sim) != NULL) && sim->source.product->game == entry->q2_services ? &sim->source.product->monsters
    : sim->has_foreign_world && sim->foreign_world.modules->game == entry->q2_services ? &sim->foreign_world.modules->monsters : NULL;
  if (!monsters) return;
  qa_q2m_context *context = qa_q2m_monsters_context(monsters, actor);
  if (!context) return;
  qa_q2m_monsters_dodge(monsters, context->entity, context->game, *attacker, eta, trace, false);
}
/* monsterTarget(actor): false = TS null. */
static bool wl_monster_target(qa_app_shared_simulation *sim, qa_actor_id actor, qa_monster_target_observation *out) {
  qa_body_state body;
  if (!qa_session_actor_registry_is_live(sim->actors, actor) || !qa_app_shared_sim_body(sim, actor, &body)) return false;
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor);
  qa_q1_player_state *q1 = entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 ? qa_q1_player(entry->q1_services, actor) : NULL;
  bool native_value = false;
  bool native = sim->strategy->notarget(sim, actor, &native_value) && native_value;
  memset(out, 0, sizeof *out);
  out->view_height = player ? player->view_height : entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2 ? entry->q2_entity->view_height : 25;
  bool entity_notarget = entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 ? (qa_js_to_int32(entry->q1_entity->movement_flags) & 128) != 0
    : entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2
      && (qa_js_to_int32(entry->q2_entity->flags) & (32 | (qa_q2_entity_services_game(entry->q2_services)->options.edition == QA_Q2_EDITION_RERELEASE ? 0x1008000 : 0))) != 0;
  out->notarget = native || entity_notarget;
  out->invisible = (q1 ? qa_q1_player_powerup_get(q1, QA_Q1_POWERUP_IDS[2]) : 0) > wl_time(sim);
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2) { out->has_light_level = true; out->light_level = entry->q2_entity->light_level; }
  if (q1) { out->has_hostile_until = true; out->hostile_until = q1->hostile_until; }
  out->has_cloak_alpha = sim->strategy->cloak_alpha(sim, actor, &out->cloak_alpha);
  return true;
}
bool qa_app_shared_sim_monster_target_notarget(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_monster_target_observation observation;
  return wl_monster_target(sim, actor, &observation) && observation.notarget;
}
qa_q1_monster_target_result qa_app_shared_sim_monster_target_q1(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q1_monster_target_result result; memset(&result, 0, sizeof result);
  result.has_value = wl_monster_target(sim, actor, &result.value);
  return result;
}
bool qa_app_shared_sim_q1_visibility_eye(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q1_client_eye *out) {
  qa_body_state body;
  if (!qa_app_shared_sim_body(sim, actor, &body)) return false;
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor);
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 && qa_q1_actor_field_get(entry->q1_entity, "view_ofs") != NULL) {
    *out = (qa_q1_client_eye){body.origin, qa_q1_actor_vector(entry->q1_entity, "view_ofs")};
    return true;
  }
  qa_client_id client;
  if (qa_app_shared_sim_player_client(sim, actor, &client)) {
    qa_app_sim_player_view view = qa_app_shared_sim_player_view(sim, actor);
    *out = (qa_q1_client_eye){body.origin, {view.origin.x - body.origin.x, view.origin.y - body.origin.y, view.origin.z + view.view_height - body.origin.z}};
    return true;
  }
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 && entry->q1_entity->monster != NULL) {
    qa_q1_base_monster *monster = qa_q1_creatures_monsters_get(qa_q1_creatures_for(entry->q1_services), entry->q1_entity->actor);
    qa_vec3 eye;
    if (monster && qa_q1_base_monster_eye(monster, entry->q1_entity->actor.id, &eye)) { *out = (qa_q1_client_eye){eye, qa_ss_zero}; return true; }
  }
  qa_monster_target_observation target;
  *out = (qa_q1_client_eye){body.origin, {0, 0, wl_monster_target(sim, actor, &target) ? target.view_height : 0}};
  return true;
}

/* ---- Native Q2 guest bridge: clients and characters (6853-7086) ---- */
static bool wl_native_actor(qa_app_shared_simulation *sim, double slot, qa_actor_id *out) {
  return sim->source.classic_world ? qa_app_sim_classic_guest_world_actor(sim->source.classic_world, slot, out)
    : qa_app_sim_rerelease_guest_world_actor(sim->source.rerelease_world, slot, out);
}
static bool wl_native_has_source_inventory(qa_app_shared_simulation *sim) {
  return sim->source.classic_world ? qa_app_sim_classic_guest_services_has_source_inventory(qa_app_sim_classic_guest_world_services(sim->source.classic_world))
    : qa_app_sim_rerelease_guest_services_has_source_inventory(qa_app_sim_rerelease_guest_world_services(sim->source.rerelease_world));
}
static void wl_native_begin(qa_app_shared_simulation *sim, double slot) {
  if (sim->source.classic_world) qa_app_sim_classic_guest_world_begin(sim->source.classic_world, slot);
  else qa_app_sim_rerelease_guest_world_begin(sim->source.rerelease_world, slot);
}
qa_client_id qa_app_shared_sim_native_q2_client(qa_app_shared_simulation *sim, qa_actor_id actor) {
  if ((sim->strategy->as_q2_native(sim) == NULL)) wl_fail("No native Quake II source");
  for (size_t i = 0; i < sim->source.clients.count; i++)
    if (qa_same_actor(((qa_owned_actor *)qa_ss_map_value_at(&sim->source.clients, i))->id, actor)) return qa_ss_map_key_at(&sim->source.clients, i)->client;
  wl_fail("Actor has no native Quake II client");
}
static const qa_app_sim_native_q2_travel_client *wl_retained_native_client(qa_app_shared_simulation *sim, qa_client_id client) {
  const qa_app_sim_native_q2_travel *travel = sim->options.native_q2_travel;
  if (!travel) return NULL;
  const qa_app_sim_native_q2_travel_clients *base = travel->rerelease ? &travel->rerelease_travel.base : &travel->classic.base;
  for (size_t i = 0; i < base->client_count; i++) if (qa_same_client(base->clients[i].client, client)) return &base->clients[i];
  return NULL;
}
typedef struct wl_native_character { qa_app_shared_simulation *sim; qa_actor_id actor; } wl_native_character;
static bool wl_q1_character_pose(void *ctx, qa_q1_character_source_pose *out) {
  wl_native_character *record = ctx;
  *out = qa_app_shared_sim_q1_character_pose(record->sim, record->actor);
  return true;
}
/* attachNativeCharacter(player): selected-character animators for native
 * clients; they derive the drawn model's animation from guest-observable state. */
void qa_app_shared_sim_attach_native_character(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  if (!qa_app_shared_sim_foreign_native_character(sim)) return;
  qa_owned_actor actor = player->actor;
  if (player->character == QA_GAME_FAMILY_Q1 && !qa_ss_map_has(&sim->q1_characters, &actor)) {
    wl_native_character *record = qa_app_shared_sim_keep(sim, sizeof *record);
    record->sim = sim; record->actor = actor.id;
    qa_q1_character_options options; memset(&options, 0, sizeof options);
    options.has_presentation_only = true; options.presentation_only = true;
    options.ctx = record; options.source_pose = wl_q1_character_pose;
    qa_q1_character_actor *character = qa_q1_character_actor_create(qa_app_shared_sim_q1_character_source(sim), actor, &options);
    if (!character) qa_range_error("Out of memory");
    qa_ss_map_set(&sim->q1_characters, &actor, &character);
  }
  if (player->character == QA_GAME_FAMILY_Q3 && !qa_ss_is(player->animation.state.kind, "q3"))
    player->animation = (qa_actor_animation_state){sim->recipe.character.definition.provider, qa_q3_spawn_animation()};
}
qa_owned_actor qa_app_shared_sim_register_q2_native_client_mode(qa_app_shared_simulation *sim, qa_client_id client, bool live) {
  if ((sim->strategy->as_q2_native(sim) == NULL) || !qa_app_shared_sim_owns_client(sim, client) || (double)client.slot >= sim->options.max_clients
      || qa_ss_map_has(&sim->source.clients, &client)) wl_fail("Invalid native client registration");
  qa_actor_id id; qa_owned_actor actor;
  if (!wl_native_actor(sim, (double)client.slot + 1, &id) || !qa_session_actor_registry_resolve_owned(sim->actors, id, &actor))
    wl_fail("Native ClientConnect did not reserve its source actor");
  qa_ss_map_set(&sim->source.clients, &client, &actor);
  /* Restored clients need their host too: without it a loaded save would
   * silently fall back to guest movement and the guest player model. */
  if (qa_app_shared_sim_foreign_native_movement(sim) || qa_app_shared_sim_foreign_native_character(sim)) {
    qa_body_state body; bool has_body = qa_app_shared_sim_body(sim, actor.id, &body);
    qa_arsenal_state arsenal; memset(&arsenal, 0, sizeof arsenal);
    arsenal.provider = sim->weapon_provider.provider; arsenal.active_weapon = NULL; arsenal.ammo = NULL; arsenal.ammo_count = 0;
    arsenal.state.kind = "q2"; arsenal.state.gun_frame = 0; arsenal.state.state = 0; arsenal.state.pending_weapon = NULL;
    arsenal.state.machinegun_shots = 0; arsenal.state.grenade_time = (qa_source_time){QA_SOURCE_TIME_SECONDS, 0}; arsenal.state.grenade_blew_up = false;
    qa_app_sim_movement_player *player = qa_app_shared_sim_create_player(sim, actor, client, has_body ? body.origin : qa_ss_zero,
        has_body ? body.angles : qa_ss_zero, arsenal);
    qa_ss_map_set(&sim->native_movement_players, &actor, &player);
    qa_app_shared_sim_attach_native_character(sim, player);
  }
  const qa_app_sim_native_q2_travel_client *retained = live ? wl_retained_native_client(sim, client) : NULL;
  if (retained && retained->has_native_inventory_selection) {
    qa_ss_optional_item selection = {!retained->native_inventory_selection_null, retained->native_inventory_selection};
    qa_ss_map_set(&sim->restored_native_inventory_selections, &actor.id, &selection);
  }
  if (live && wl_native_has_source_inventory(sim)) qa_app_shared_sim_bind_equipment_inventory(sim, actor, NULL, 0);
  return actor;
}
qa_owned_actor qa_app_shared_sim_register_q2_native_client(qa_app_shared_simulation *sim, qa_client_id client) {
  return qa_app_shared_sim_register_q2_native_client_mode(sim, client, true);
}
/* ---- captureNativeQ2Travel (6193-6235) ----
 * The C record owns everything it holds (the next simulation reads it after
 * this one is gone): the wrapper below keeps the client records, strings,
 * captured levels and carried visited levels. Visited levels carried over
 * from options.nativeQ2Travel stay borrowed from that record. */
typedef struct wl_native_travel {
  qa_app_sim_native_q2_travel travel; /* first member: the public record */
  qa_app_sim_native_q2_travel_client *clients; size_t client_count;
  qa_app_sim_dropped_pickup_level *dropped; size_t dropped_count;
  qa_app_sim_classic_visited_entry *classic_visited; qa_app_sim_rerelease_visited_entry *rerelease_visited; size_t visited_count;
  qa_q2_classic_visited_level *classic_level; qa_app_sim_q2_rerelease_visited_level *rerelease_level; /* the level captured here */
  char **strings; size_t string_count, string_capacity;
} wl_native_travel;
static const char *wl_travel_string(wl_native_travel *travel, const char *text) {
  if (text == NULL) return NULL;
  travel->strings = wl_grow(travel->strings, &travel->string_capacity, travel->string_count + 1, sizeof *travel->strings);
  char *copy = qa_ss_strdup(text);
  if (copy == NULL) qa_range_error("Out of memory");
  travel->strings[travel->string_count++] = copy;
  return copy;
}
void qa_app_shared_sim_native_q2_travel_free(qa_app_sim_native_q2_travel *record) {
  if (record == NULL) return;
  wl_native_travel *travel = (wl_native_travel *)record;
  for (size_t i = 0; i < travel->client_count; i++) {
    qa_app_sim_native_q2_travel_client *client = &travel->clients[i];
    free((void *)client->hand_grenades); free((void *)client->weapon_slot);
    if (client->selected_arsenal != NULL) { qa_ss_selected_arsenal_travel_free((qa_ss_selected_arsenal_travel *)client->selected_arsenal); free((void *)client->selected_arsenal); }
  }
  free(travel->clients);
  qa_app_sim_dropped_pickup_levels_destroy(travel->dropped, travel->dropped_count);
  free(travel->classic_visited); free(travel->rerelease_visited);
  if (travel->classic_level != NULL) {
    free(travel->classic_level->state.configstrings); free(travel->classic_level->state.portals);
    free((void *)travel->classic_level->level.bytes); free(travel->classic_level);
  }
  if (travel->rerelease_level != NULL) {
    free(travel->rerelease_level->state.configstrings); free(travel->rerelease_level->state.portals);
    qa_app_sim_rerelease_source_save_free(&travel->rerelease_level->level); free(travel->rerelease_level->level.native);
    free(travel->rerelease_level);
  }
  for (size_t i = 0; i < travel->string_count; i++) free(travel->strings[i]);
  free(travel->strings);
  free(travel);
}
static void wl_native_travel_cleanup(void *ctx) { qa_app_shared_sim_native_q2_travel_free(&((wl_native_travel *)ctx)->travel); }
typedef struct wl_level_write { qa_app_sim_classic_guest_world *world; qa_app_shared_sim_next_frame_fn next_frame; void *frame_ctx; } wl_level_write;
static void wl_write_travel_level(void *ctx, const char *level_path) {
  wl_level_write *write = ctx;
  qa_app_sim_classic_guest_world_write_travel_level_loading(write->world, level_path, write->next_frame, write->frame_ctx);
}
qa_app_sim_native_q2_travel *qa_app_shared_sim_capture_native_q2_travel(qa_app_shared_simulation *sim, bool new_unit,
    const char *spawn_point, qa_app_shared_sim_next_frame_fn next_frame, void *frame_ctx) {
  qa_app_shared_sim_assert_open(sim);
  if ((sim->strategy->as_q2_native(sim) == NULL) || sim->stepping) wl_fail("Native map travel requires a completed source frame");
  bool rerelease = qa_ss_is(sim->source.edition, "rerelease");
  wl_native_travel *travel = wl_alloc(1, sizeof *travel);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, wl_native_travel_cleanup, travel);
  size_t record_count = 0;
  const qa_app_sim_classic_guest_client *records = rerelease ? qa_app_sim_rerelease_guest_world_clients(sim->source.rerelease_world, &record_count)
      : qa_app_sim_classic_guest_world_clients(sim->source.classic_world, &record_count);
  travel->clients = wl_alloc(record_count, sizeof *travel->clients);
  for (size_t r = 0; r < record_count; r++) {
    const qa_client_id *client = NULL; const qa_owned_actor *actor = NULL;
    for (size_t i = 0; i < sim->source.clients.count && client == NULL; i++)
      if ((double)qa_ss_map_key_at(&sim->source.clients, i)->client.slot + 1 == records[r].slot) {
        client = &qa_ss_map_key_at(&sim->source.clients, i)->client; actor = qa_ss_map_value_at(&sim->source.clients, i);
      }
    if (client == NULL) wl_fail("Native source client has no session identity");
    qa_app_sim_native_q2_travel_client *entry = &travel->clients[travel->client_count++];
    entry->client = *client; entry->phase = qa_ss_is(records[r].phase, "active") ? "active" : "connected";
    qa_app_sim_hand_grenade_travel grenades;
    if (sim->hand_grenades != NULL && qa_app_sim_hand_grenade_runtime_travel(sim->hand_grenades, actor->id, &grenades)) {
      qa_app_sim_hand_grenade_travel *kept = wl_alloc(1, sizeof *kept); *kept = grenades;
      kept->ammo.item = wl_travel_string(travel, grenades.ammo.item);
      entry->hand_grenades = kept;
    }
    if (qa_app_shared_sim_has_weapon_slot(sim, actor->id)) {
      qa_app_sim_weapon_slot_projection projection = qa_app_shared_sim_slot_projection(sim, actor->id, NULL);
      const qa_weapon_reference *slot = projection.pending != NULL ? projection.pending : projection.active;
      if (slot != NULL) {
        qa_weapon_reference *kept = wl_alloc(1, sizeof *kept);
        kept->provider = wl_travel_string(travel, slot->provider); kept->item = wl_travel_string(travel, slot->item);
        entry->weapon_slot = kept;
      }
    }
    if (sim->grapple != NULL) qa_app_sim_grapple_runtime_release(sim->grapple, actor->id);
    qa_ss_selected_arsenal_travel *selected = wl_alloc(1, sizeof *selected);
    if (qa_app_shared_sim_capture_selected_arsenal_travel(sim, actor->id, selected)) { entry->selected_arsenal = selected; entry->selected_arsenal_kind = selected->kind; }
    else free(selected);
    qa_compat_q2_native_inventory_readout readout;
    if (sim->native_primary_inventory != NULL && qa_compat_q2_native_primary_inventory_read(sim->native_primary_inventory, actor->id, &readout)) {
      entry->has_native_inventory_selection = true; entry->native_inventory_selection_null = readout.selected == NULL;
      entry->native_inventory_selection = wl_travel_string(travel, readout.selected);
    }
  }
  qa_app_sim_native_q2_travel_clients *base = rerelease ? &travel->travel.rerelease_travel.base : &travel->travel.classic.base;
  base->clients = travel->clients; base->client_count = travel->client_count; base->spawn_point = wl_travel_string(travel, spawn_point);
  travel->travel.rerelease = rerelease;
  const char *map = sim->recipe.map.geometry.requested_path;
  /* new Map(source.visited): the carried levels in Map order; set(map, ...) replaces in place or appends. */
  size_t carried = new_unit ? 0 : sim->source.visited.count;
  if (rerelease) travel->rerelease_visited = wl_alloc(carried + 1, sizeof *travel->rerelease_visited);
  else travel->classic_visited = wl_alloc(carried + 1, sizeof *travel->classic_visited);
  for (size_t i = 0; i < carried; i++) {
    const char *key = wl_travel_string(travel, qa_ss_map_key_at(&sim->source.visited, i)->string);
    const void *level = *(const void **)qa_ss_map_value_at(&sim->source.visited, i);
    if (rerelease) { travel->rerelease_visited[i].map = key; travel->rerelease_visited[i].level = *(const qa_app_sim_q2_rerelease_visited_level *)level; }
    else { travel->classic_visited[i].map = key; travel->classic_visited[i].level = *(const qa_q2_classic_visited_level *)level; }
  }
  travel->visited_count = carried;
  if (!new_unit) {
    size_t at = carried;
    for (size_t i = 0; i < carried; i++) if (qa_ss_is(rerelease ? travel->rerelease_visited[i].map : travel->classic_visited[i].map, map)) at = i;
    size_t configstring_count = 0;
    const qa_app_sim_configstring *configstrings = NULL;
    if (rerelease) {
      qa_app_sim_q2_rerelease_visited_level *level = wl_alloc(1, sizeof *level);
      travel->rerelease_level = level;
      level->version = 1; level->map = wl_travel_string(travel, map);
      level->level = qa_app_sim_rerelease_guest_world_write_travel_level_loading(sim->source.rerelease_world, next_frame, frame_ctx);
      configstrings = qa_app_sim_rerelease_guest_world_configstrings(sim->source.rerelease_world, &configstring_count);
      level->state.configstrings = wl_alloc(configstring_count, sizeof *level->state.configstrings);
      for (size_t i = 0; i < configstring_count; i++)
        level->state.configstrings[i] = (qa_app_sim_q2_rerelease_configstring){configstrings[i].index, wl_travel_string(travel, configstrings[i].value)};
      level->state.configstring_count = configstring_count;
      level->state.portals = wl_alloc(sim->area_portals.count, sizeof *level->state.portals);
      for (size_t i = 0; i < sim->area_portals.count; i++)
        level->state.portals[i] = (qa_app_sim_q2_rerelease_portal){qa_ss_map_key_at(&sim->area_portals, i)->number, *(bool *)qa_ss_map_value_at(&sim->area_portals, i)};
      level->state.portal_count = sim->area_portals.count;
      travel->rerelease_visited[at].map = level->map; travel->rerelease_visited[at].level = *level;
    } else {
      qa_q2_classic_visited_level *level = wl_alloc(1, sizeof *level);
      travel->classic_level = level;
      wl_level_write write = {sim->source.classic_world, next_frame, frame_ctx};
      uint8_t *bytes = NULL; size_t length = 0;
      if (!qa_classic_original_save_files_capture_travel_level_loading(sim->source.files, wl_write_travel_level, &write, &bytes, &length))
        qa_range_error("Out of memory");
      level->level.bytes = bytes; level->level.length = length;
      level->version = 1; level->map = wl_travel_string(travel, map);
      configstrings = qa_app_sim_classic_guest_world_configstrings(sim->source.classic_world, &configstring_count);
      level->state.configstrings = wl_alloc(configstring_count, sizeof *level->state.configstrings);
      for (size_t i = 0; i < configstring_count; i++)
        level->state.configstrings[i] = (qa_q2_classic_configstring_entry){configstrings[i].index, wl_travel_string(travel, configstrings[i].value)};
      level->state.configstring_count = configstring_count;
      level->state.portals = wl_alloc(sim->area_portals.count, sizeof *level->state.portals);
      for (size_t i = 0; i < sim->area_portals.count; i++)
        level->state.portals[i] = (qa_q2_classic_portal_entry){qa_ss_map_key_at(&sim->area_portals, i)->number, *(bool *)qa_ss_map_value_at(&sim->area_portals, i)};
      level->state.portal_count = sim->area_portals.count;
      travel->classic_visited[at].map = level->map; travel->classic_visited[at].level = *level;
    }
    if (at == carried) travel->visited_count++;
  }
  if (rerelease) {
    travel->travel.rerelease_travel.world = sim->source.rerelease_world;
    travel->travel.rerelease_travel.visited = travel->rerelease_visited; travel->travel.rerelease_travel.visited_count = travel->visited_count;
  } else {
    travel->travel.classic.world = sim->source.classic_world; travel->travel.classic.files = sim->source.files;
    travel->travel.classic.visited = travel->classic_visited; travel->travel.classic.visited_count = travel->visited_count;
  }
  /* release(travel) */
  if (sim->has_dispose_native_pickup_supply) { sim->dispose_native_pickup_supply.dispose(sim->dispose_native_pickup_supply.ctx); sim->has_dispose_native_pickup_supply = false; }
  if (sim->native_primary_drop != NULL) { qa_compat_q2_native_primary_drop_close(sim->native_primary_drop); qa_compat_q2_native_primary_drop_destroy(sim->native_primary_drop); sim->native_primary_drop = NULL; }
  if (sim->native_primary_inventory != NULL) { qa_compat_q2_native_primary_inventory_close(sim->native_primary_inventory); qa_compat_q2_native_primary_inventory_destroy(sim->native_primary_inventory); sim->native_primary_inventory = NULL; }
  if (sim->native_primary_commands != NULL) { qa_compat_q2_native_primary_commands_close(sim->native_primary_commands); qa_compat_q2_native_primary_commands_destroy(sim->native_primary_commands); sim->native_primary_commands = NULL; }
  if (sim->native_primary_weapons != NULL) { qa_compat_q2_native_primary_weapons_close(sim->native_primary_weapons); qa_compat_q2_native_primary_weapons_destroy(sim->native_primary_weapons); sim->native_primary_weapons = NULL; }
  travel->dropped = qa_app_sim_source_pickup_cargo_travel(sim->dropped_pickups, map, new_unit, &travel->dropped_count);
  base->dropped_pickups = travel->dropped; base->dropped_pickup_level_count = travel->dropped_count; base->has_dropped_pickups = true;
  qa_cleanup_pop(&cleanup);
  return &travel->travel;
}
qa_app_sim_player_admission qa_app_shared_sim_admit_q2_native_player(qa_app_shared_simulation *sim, qa_client_id client, const char *userinfo, bool begin) {
  qa_app_shared_sim_assert_open(sim);
  if ((sim->strategy->as_q2_native(sim) == NULL) || !qa_app_shared_sim_owns_client(sim, client) || (double)client.slot >= sim->options.max_clients
      || qa_ss_map_has(&sim->source.clients, &client)) wl_fail("Invalid native client admission");
  double slot = (double)client.slot + 1;
  const qa_app_sim_native_q2_travel_client *retained = wl_retained_native_client(sim, client);
  if (retained) {
    qa_owned_actor actor = qa_app_shared_sim_register_q2_native_client(sim, client);
    if (begin && qa_ss_is(retained->phase, "active")) { wl_native_begin(sim, slot); qa_app_shared_sim_notify_client_event(sim, "admitted", actor.id); }
    return (qa_app_sim_player_admission){actor.id, qa_app_shared_sim_player_view(sim, actor.id).view_height};
  }
  qa_app_sim_classic_guest_connect result = sim->source.classic_world ? qa_app_sim_classic_guest_world_connect(sim->source.classic_world, slot, userinfo)
    : qa_app_sim_rerelease_guest_world_connect(sim->source.rerelease_world, slot, userinfo, "", false);
  if (!result.allowed) wl_fail("Native Quake II rejected the local client");
  qa_owned_actor actor = qa_app_shared_sim_register_q2_native_client(sim, client);
  if (begin) { wl_native_begin(sim, slot); qa_app_shared_sim_notify_client_event(sim, "admitted", actor.id); }
  return (qa_app_sim_player_admission){actor.id, qa_app_shared_sim_player_view(sim, actor.id).view_height};
}
qa_app_sim_movement_player *qa_app_shared_sim_native_character_host(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_owned_actor owned;
  if (!qa_session_actor_registry_resolve_owned(sim->actors, actor, &owned)) return NULL;
  qa_app_sim_movement_player **player = qa_ss_map_get(&sim->native_movement_players, &owned);
  return player ? *player : NULL;
}
/* presentationPlayers(): TS-owned players plus native character hosts.
 * Storage is reused by the next call. */
qa_app_sim_movement_player *const *qa_app_shared_sim_presentation_players(qa_app_shared_simulation *sim, size_t *out_count) {
  static __thread qa_app_sim_movement_player **storage; static __thread size_t capacity;
  size_t count = 0;
  storage = wl_grow(storage, &capacity, sim->player_states.count + sim->native_movement_players.count + 1, sizeof *storage);
  for (size_t i = 0; i < sim->player_states.count; i++) storage[count++] = *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i);
  if ((sim->strategy->as_q2_native(sim) != NULL))
    for (size_t i = 0; i < sim->native_movement_players.count; i++) {
      qa_app_sim_movement_player *player = *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->native_movement_players, i);
      if (!qa_ss_map_has(&sim->player_states, &player->actor)) storage[count++] = player;
    }
  *out_count = count;
  return storage;
}
static bool wl_ui_invulnerable(const qa_app_sim_player_ui *ui) {
  for (size_t i = 0; i < ui->powerup_count; i++) if (qa_ss_is(ui->powerups[i].item, "q2:item_invulnerability")) return true;
  return false;
}
/* Mirror contract: health, armor and invulnerability follow the guest; mass
 * 200, canTakeDamage true and team null are declared constants. A shared
 * write to a mirrored player is discarded and reported once per client. */
void qa_app_shared_sim_sync_native_movement_combat(qa_app_shared_simulation *sim, qa_owned_actor owned) {
  qa_combat_state_result current = qa_gameplay_authority_read(sim->combat, owned.id);
  if (current.has_value && !qa_ss_map_has(&sim->native_mirrored_combat, &owned.id)) return;
  qa_app_sim_player_ui ui = qa_app_shared_sim_native_player_ui(sim, owned.id);
  bool invulnerable = wl_ui_invulnerable(&ui);
  if (!current.has_value) {
    qa_combat_state state; memset(&state, 0, sizeof state);
    state.health = ui.health; state.armor = ui.armor; state.mass = 200; state.can_take_damage = true; state.invulnerable = invulnerable; state.team = NULL;
    qa_gameplay_authority_create_state(sim->combat, owned, state, NULL, NULL);
    qa_ss_map_set(&sim->native_mirrored_combat, &owned.id, NULL);
    qa_ss_map_set(&sim->native_mirrored_health, &owned.id, &ui.health);
    return;
  }
  double *mirrored = qa_ss_map_get(&sim->native_mirrored_health, &owned.id);
  if (mirrored && current.value.health != *mirrored && !qa_ss_map_has(&sim->native_mirror_write_reported, &owned.id)) {
    qa_ss_map_set(&sim->native_mirror_write_reported, &owned.id, NULL);
    char wrote[40], guest[40];
    char *text = wl_format("Shared combat wrote health %s to a guest-owned player (guest has %s); the guest is the damage authority, so the write is discarded\n",
      qa_ss_number_text(current.value.health, wrote), qa_ss_number_text(ui.health, guest));
    qa_app_shared_sim_print(sim, text, NULL);
    free(text);
  }
  qa_ss_map_set(&sim->native_mirrored_health, &owned.id, &ui.health);
  if (current.value.health != ui.health) qa_gameplay_authority_set_health(sim->combat, owned, ui.health);
  const qa_regular_armor_state *a = &current.value.armor.regular, *b = &ui.armor.regular;
  if (a->kind != b->kind || (a->kind != QA_REGULAR_ARMOR_NONE && b->kind != QA_REGULAR_ARMOR_NONE && a->points != b->points)
      || !qa_ss_is(current.value.armor.powered.kind, ui.armor.powered.kind))
    qa_gameplay_authority_set_armor(sim->combat, owned, ui.armor);
  if (current.value.invulnerable != invulnerable) {
    qa_combat_trait_changes changes; memset(&changes, 0, sizeof changes);
    changes.has_invulnerable = true; changes.invulnerable = invulnerable;
    qa_gameplay_authority_set_traits(sim->combat, owned, changes);
  }
}
/* Selected-movement water contents in the guest's Q2 contents dialect. */
double qa_app_shared_sim_native_water_contents(qa_app_shared_simulation *sim, const qa_app_sim_movement_player *player) {
  (void)sim; /* a TS method that reads only the player */
  double value = player->state.kind == QA_MOVEMENT_STATE_Q1 ? player->state.as.q1.water_type : player->water_type;
  if (qa_ss_is(player->profile.kind, "q1-netquake") || qa_ss_is(player->profile.kind, "q1-quakeworld"))
    return value == -3 ? 32 : value == -4 ? 64 : value == -5 ? 128 : 0;
  int32_t bits = qa_js_to_int32(value);
  if (qa_ss_is(player->profile.kind, "q3")) return (bits & 32) != 0 ? 32 : (bits & 16) != 0 ? 64 : (bits & 8) != 0 ? 128 : 0;
  return bits & (32 | 64 | 128);
}
/* ---- bindNativeInput (6424-6603) ---- */
/* TS this.weaponSlots.get(actor)?.primarySelected() ?? true. */
static bool wl_primary_selected(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_app_sim_weapon_slot **slot = qa_ss_map_get(&sim->weapon_slots, &actor);
  return slot == NULL || qa_app_sim_weapon_slot_primary_selected(*slot);
}
static qa_app_sim_q2_native_world wl_q2_native_world(qa_app_shared_simulation *sim) {
  qa_app_sim_q2_native_world world = {qa_ss_is(sim->source.edition, "rerelease"), sim->source.classic_world, sim->source.rerelease_world};
  return world;
}
/* nativePrimaryWeaponHost(source.game, this.actors) for an owner that outlives the call: its context is kept. */
static qa_compat_q2_native_primary_weapon_host wl_kept_native_host(qa_app_shared_simulation *sim) {
  qa_app_sim_native_primary_weapon_context *context = qa_app_shared_sim_keep(sim, sizeof *context);
  return qa_app_sim_native_primary_weapon_host(context, wl_q2_native_world(sim), sim->actors);
}
/* bindNativePrimaryWeapons hooks. */
static bool wl_native_weapon_selected(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ctx;
  return !sim->has_selected_arsenal && wl_primary_selected(sim, actor);
}
static void wl_native_weapon_completed(void *ctx, qa_actor_id actor, bool reached) { qa_app_shared_sim_native_weapon_step(ctx, actor, reached); }
static void wl_native_weapon_spawned(void *ctx, qa_actor_id actor) { qa_app_shared_sim_native_client_spawned(ctx, actor); }
/* NativePrimaryCommands hooks. */
static void wl_native_give(void *ctx, qa_actor_id actor, const char *category) {
  if (qa_app_shared_sim_selected_arsenal_has(ctx, actor)) qa_app_shared_sim_grant_selected_arsenal(ctx, actor, category);
}
static bool wl_native_give_item(void *ctx, qa_actor_id actor, const char *const *args, size_t count) {
  return qa_app_shared_sim_selected_arsenal_has(ctx, actor) && qa_app_shared_sim_give_selected_item(ctx, actor, args, count);
}
static void wl_native_give_ammo(void *ctx, qa_actor_id actor, qa_item_id item, const char *kind, double amount) {
  qa_app_shared_sim_give_native_selected_ammo(ctx, actor, item, kind, amount);
}
static bool wl_native_drop(void *ctx, qa_actor_id actor, qa_item_id *out_item, double *out_ammo) {
  qa_app_shared_simulation *sim = ctx;
  if (!qa_app_shared_sim_selected_arsenal_has(sim, actor)) return false;
  qa_app_sim_player_ui ui = qa_app_shared_sim_selected_arsenal_ui(sim, actor);
  qa_item_id item = NULL;
  if (ui.active_weapon != NULL) {
    const qa_ss_optional_item *found = sim->has_selected_original_weapons ? qa_ss_map_get(&sim->selected_original_weapons, &ui.active_weapon) : NULL;
    if (found == NULL) wl_fail("Selected native death drop has no admitted original supply owner");
    item = found->has ? found->item : NULL;
  }
  *out_item = item; *out_ammo = ui.has_ammo ? ui.ammo_count : 0;
  return true;
}
/* NativePrimaryInventory / NativePrimaryDrop hooks. */
static bool wl_native_rows(void *ctx, qa_actor_id actor, const qa_compat_q2_native_inventory_row **out, size_t *out_count) {
  *out = qa_app_shared_sim_native_inventory_rows(ctx, actor, out_count);
  return *out != NULL;
}
static void wl_native_use(void *ctx, qa_actor_id actor, qa_item_id item) { qa_app_shared_sim_use_selected_inventory_item(ctx, actor, item); }
static void wl_native_print(void *ctx, qa_actor_id actor, const char *text) { qa_app_shared_sim_print(ctx, text, &actor); }
static qa_item_id wl_native_item_at(void *ctx, qa_guest_address address) {
  qa_app_shared_simulation *sim = ctx;
  return sim->native_primary_commands != NULL ? qa_compat_q2_native_primary_commands_item_at(sim->native_primary_commands, address) : NULL;
}
static qa_guest_address wl_native_descriptor(void *ctx, double index) {
  qa_app_shared_simulation *sim = ctx;
  qa_compat_q2_native_primary_commands *commands = sim->native_primary_commands;
  qa_compat_q2_native_source_item descriptor; bool found = false;
  if (commands != NULL) {
    size_t count = 0; const qa_compat_q2_native_inventory_slot *slots = qa_compat_q2_native_primary_commands_inventory_slots(commands, &count);
    for (size_t i = 0; i < count; i++)
      if (slots[i].index == index) { found = qa_compat_q2_native_primary_commands_source_item(commands, slots[i].item, &descriptor); break; }
  }
  if (!found) wl_fail("Original inventory descriptor is unavailable");
  return descriptor.address;
}
static qa_item_id wl_native_drop_selected(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ctx;
  qa_compat_q2_native_inventory_readout readout;
  if (sim->native_primary_inventory == NULL || !qa_compat_q2_native_primary_inventory_read(sim->native_primary_inventory, actor, &readout)) return NULL;
  return readout.selected;
}
static qa_compat_q2_native_drop_projection wl_native_drop_projection(void *ctx, qa_actor_id actor, qa_item_id item) {
  return qa_app_shared_sim_native_drop_projection(ctx, actor, item);
}
typedef struct wl_dropped_call { qa_app_shared_simulation *sim; qa_actor_id actor, pickup; qa_item_id item; double count; } wl_dropped_call;
/* () => this.nativeDroppedItem(...): a non-NULL result is TS true. */
static void *wl_dropped_execute(void *ctx) {
  wl_dropped_call *call = ctx;
  return qa_app_shared_sim_native_dropped_item(call->sim, call->actor, call->pickup, call->item, call->count) ? call : NULL;
}
static bool wl_native_dropped(void *ctx, qa_actor_id actor, qa_actor_id pickup, qa_item_id item, double count) {
  wl_dropped_call call = {ctx, actor, pickup, item, count};
  return qa_app_shared_sim_native_drop_protection(ctx, actor, "debit", wl_dropped_execute, &call) != NULL;
}
typedef struct wl_consume_call { void *execute_ctx; void (*execute)(void *execute_ctx); } wl_consume_call;
static void *wl_consume_execute(void *ctx) { wl_consume_call *call = ctx; call->execute(call->execute_ctx); return NULL; }
static void wl_native_consume(void *ctx, qa_actor_id actor, void *execute_ctx, void (*execute)(void *execute_ctx)) {
  wl_consume_call call = {execute_ctx, execute};
  qa_app_shared_sim_native_drop_protection(ctx, actor, "consume", wl_consume_execute, &call);
}
static qa_compat_q2_native_drop_action wl_native_drop_action(void *ctx, qa_actor_id actor, qa_item_id item) {
  qa_app_shared_simulation *sim = ctx;
  qa_source_item_action_invocation invocation = qa_shared_inventory_table_item_action(sim->inventory, actor, item, QA_SOURCE_ITEM_ACTION_DROP);
  qa_compat_q2_native_drop_action action = {NULL, NULL};
  if (invocation.has_value) { action.ctx = invocation.ctx; action.invoke = invocation.call; }
  return action;
}
/* source.game.source.host.bindPickupSupply({ owns }) and its remover. */
typedef struct wl_pickup_supply { qa_app_shared_simulation *sim; qa_shared_pickup_admission *admission; bool rerelease; void *host; } wl_pickup_supply;
static bool wl_supply_owns(void *ctx, qa_actor_id actor, qa_item_id item, qa_actor_id pickup) {
  wl_pickup_supply *supply = ctx;
  qa_app_sim_dropped_pickup_cargo cargo;
  if (!qa_app_sim_source_pickup_cargo_get(supply->sim->dropped_pickups, pickup, &cargo)) return qa_shared_pickup_admission_owns(supply->admission, actor, item);
  return qa_shared_inventory_table_count(supply->sim->inventory, actor, cargo.item) > 0;
}
static void wl_supply_dispose(void *ctx) {
  wl_pickup_supply *supply = ctx;
  qa_compat_q2_native_pickup_supply owner = {supply, wl_supply_owns};
  if (supply->rerelease) qa_compat_q2_rerelease_q2_guest_host_unbind_pickup_supply(supply->host, owner);
  else qa_compat_q2_classic_guest_host_unbind_pickup_supply(supply->host, owner);
}

/* source.game.bindInput services. */
typedef struct wl_input_services { qa_app_shared_simulation *sim; qa_mod_client_movement_outputs outputs; } wl_input_services;
static bool wl_input_body_bounds(void *ctx, qa_actor_id actor, qa_bounds *out) {
  qa_body_state body;
  if (!qa_app_shared_sim_body(((wl_input_services *)ctx)->sim, actor, &body)) return false;
  *out = body.bounds; return true;
}
static const qa_mod_client_movement_outputs *wl_input_client_outputs(void *ctx, qa_actor_id actor) {
  wl_input_services *services = ctx;
  return qa_app_shared_sim_active_client_outputs(services->sim, actor, &services->outputs) ? &services->outputs : NULL;
}
static bool wl_input_identity(void *ctx, double slot, qa_mod_client_identity *out) {
  qa_app_shared_simulation *sim = ((wl_input_services *)ctx)->sim;
  qa_actor_id actor; qa_client_id client;
  if (!wl_native_actor(sim, slot, &actor) || !qa_app_shared_sim_player_client(sim, actor, &client)) return false;
  out->client = client; out->actor = actor; return true;
}
static bool wl_input_live(void *ctx, qa_mod_client_identity identity) {
  qa_app_shared_simulation *sim = ((wl_input_services *)ctx)->sim;
  qa_client_id client;
  return qa_session_actor_registry_is_live(sim->actors, identity.actor) && qa_app_shared_sim_player_client(sim, identity.actor, &client)
      && qa_same_client(client, identity.client);
}
typedef struct wl_release_listener { qa_compat_q2_native_input_release_listener listener; void *ctx; } wl_release_listener;
static void wl_input_released(void *ctx, qa_owned_actor actor) { wl_release_listener *record = ctx; record->listener(record->ctx, actor.id); }
static uint64_t wl_input_on_release(void *ctx, qa_compat_q2_native_input_release_listener listener, void *listener_ctx) {
  qa_app_shared_simulation *sim = ((wl_input_services *)ctx)->sim;
  wl_release_listener *record = qa_app_shared_sim_keep(sim, sizeof *record);
  record->listener = listener; record->ctx = listener_ctx;
  return (uint64_t)qa_session_actor_registry_on_release(sim->actors, wl_input_released, record);
}
static void wl_input_remove_release(void *ctx, uint64_t handle) {
  qa_session_actor_registry_off_release(((wl_input_services *)ctx)->sim->actors, (int64_t)handle);
}
static void wl_input_retired(void *ctx, qa_mod_client_identity identity) {
  qa_app_shared_simulation *sim = ((wl_input_services *)ctx)->sim;
  const qa_owned_actor *owner = qa_ss_map_get(&sim->source.clients, &identity.client);
  if (owner != NULL && qa_same_actor(owner->id, identity.actor)) qa_ss_map_delete(&sim->source.clients, &identity.client);
}
static const qa_mod_client_command_receipt *wl_input_accepted(void *ctx, qa_actor_id actor) {
  return qa_ss_map_get(&((wl_input_services *)ctx)->sim->mod_client_commands, &actor);
}
static qa_frame_context wl_input_frame(void *ctx) { return ((wl_input_services *)ctx)->sim->source_frame; }
static qa_user_command wl_input_original_command(void *ctx, qa_mod_client_identity identity, qa_user_command command) {
  qa_app_shared_simulation *sim = ((wl_input_services *)ctx)->sim;
  if (!sim->has_selected_arsenal || qa_compat_q2_native_primary_weapons_continuing(qa_app_shared_sim_require_native_weapons(sim), identity.actor)) return command;
  double *buttons = command.kind == QA_USER_COMMAND_Q2 ? &command.as.q2.buttons : &command.as.q2_rerelease.buttons;
  *buttons = (double)(qa_js_to_int32(*buttons) & ~1);
  return command;
}
/* The guest Pmove call the selected foreign movement owns. */
typedef struct wl_input_movement {
  qa_app_shared_simulation *sim; qa_mod_client_identity identity; const qa_compat_q2_native_input_motion *projection;
  qa_user_command command; qa_vec3 aim; qa_owned_actor owned; qa_app_sim_movement_player *player;
  void *run_ctx; qa_guest_call_result (*run)(void *run_ctx);
  qa_guest_call_result result;
  qa_app_sim_native_slice_contact *contacts; size_t contact_count, contact_capacity;
  qa_app_sim_native_move_slices quoted;
} wl_input_movement;
static void wl_input_run(void *ctx) { wl_input_movement *call = ctx; call->result = call->run(call->run_ctx); }
static void wl_with_input_movement(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_compat_q2_native_input_motion *projection,
    void (*run)(void *), void *ctx) {
  if (qa_ss_is(sim->source.edition, "classic"))
    qa_app_sim_classic_guest_services_with_input_movement(qa_app_sim_classic_guest_world_services(sim->source.classic_world), actor, projection, run, ctx);
  else qa_app_sim_rerelease_guest_services_with_input_movement(qa_app_sim_rerelease_guest_world_services(sim->source.rerelease_world), actor, projection, run, ctx);
}
/* TS `this.nativeSliceContacts = []` ... finally { contacts.push(...); this.nativeSliceContacts = null }. */
static void wl_take_slice_contacts(wl_input_movement *call) {
  qa_app_shared_simulation *sim = call->sim;
  for (size_t i = 0; i < sim->native_slice_contact_count; i++) {
    if (call->contact_count == call->contact_capacity)
      call->contacts = wl_grow(call->contacts, &call->contact_capacity, call->contact_count + 1, sizeof *call->contacts);
    call->contacts[call->contact_count++] = *sim->native_slice_contacts[i];
    free(sim->native_slice_contacts[i]);
  }
  free(sim->native_slice_contacts); sim->native_slice_contacts = NULL; sim->native_slice_contact_count = 0;
  sim->has_native_slice_contacts = false;
}
static void wl_movement_cleanup(void *ctx) {
  wl_input_movement *call = ctx;
  wl_take_slice_contacts(call); free(call->contacts); call->contacts = NULL;
  free(call->quoted.slices); call->quoted.slices = NULL;
}
static bool wl_within_quantum(double a, double b) { return fabs(a - b) <= 0.12501; }
typedef struct wl_slot_lookup { qa_app_shared_simulation *sim; } wl_slot_lookup;
static bool wl_source_slot(void *ctx, qa_actor_id actor, double *out) {
  qa_actor_source_result source = qa_session_actor_registry_source_of(((wl_slot_lookup *)ctx)->sim->actors, actor);
  if (!source.has_value) return false;
  *out = (double)source.slot; return true;
}
static void wl_selected_movement(void *ctx) {
  wl_input_movement *call = ctx;
  qa_app_shared_simulation *sim = call->sim;
  qa_actor_id actor = call->identity.actor;
  qa_app_sim_movement_player *player = call->player;
  qa_app_shared_sim_sync_native_movement_combat(sim, call->owned);
  bool classic = qa_ss_is(sim->source.edition, "classic");
  if (classic) {
    qa_body_state current;
    const qa_ss_exact_motion *exact = qa_ss_map_get(&sim->native_exact_motions, &actor);
    if (qa_app_shared_sim_body(sim, actor, &current) && exact != NULL) {
      if (wl_within_quantum(exact->origin.x, current.origin.x) && wl_within_quantum(exact->origin.y, current.origin.y)
          && wl_within_quantum(exact->origin.z, current.origin.z) && wl_within_quantum(exact->velocity.x, current.velocity.x)
          && wl_within_quantum(exact->velocity.y, current.velocity.y) && wl_within_quantum(exact->velocity.z, current.velocity.z)) {
        qa_body_state state = current; state.origin = exact->origin; state.velocity = exact->velocity;
        qa_shared_body_table_write(wl_bodies(sim), call->owned, state);
      } else qa_ss_map_delete(&sim->native_exact_motions, &actor);
    }
  }
  if (qa_app_shared_sim_selected_arsenal_has(sim, actor)) qa_app_shared_sim_store_player_arsenal(sim, player, qa_app_shared_sim_arsenal(sim, player));
  qa_clock_profile clock = qa_app_sim_provider_timing(sim->recipe, sim->recipe.movement.provider).clock;
  const double *carried = qa_ss_map_get(&sim->native_move_carry, &actor);
  double milliseconds = call->command.kind == QA_USER_COMMAND_Q2 ? call->command.as.q2.milliseconds : call->command.as.q2_rerelease.milliseconds;
  call->quoted = qa_app_sim_native_move_slices_quote(clock, milliseconds, carried != NULL ? *carried : 0);
  qa_ss_map_set(&sim->native_move_carry, &actor, &call->quoted.carry);
  qa_movement_state command_clock = qa_app_sim_movement_player_read_state(player);
  double server_time = command_clock.kind == QA_MOVEMENT_STATE_Q3 && command_clock.as.q3.command_time_milliseconds > 0
      ? command_clock.as.q3.command_time_milliseconds : trunc(qa_app_shared_sim_time_seconds(sim) * 1000);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, wl_movement_cleanup, call);
  bool has_result = false; qa_movement_result result; memset(&result, 0, sizeof result);
  qa_arsenal_intent arsenal = {sim->weapon_provider.provider, NULL, false, false, 0};
  for (size_t index = 0; index < call->quoted.count; index++) {
    double slice = call->quoted.slices[index];
    server_time += slice;
    qa_user_command sliced = call->command;
    if (sliced.kind == QA_USER_COMMAND_Q2) sliced.as.q2.milliseconds = slice; else sliced.as.q2_rerelease.milliseconds = slice;
    qa_app_sim_native_player_view view = {player->profile.kind, player->arsenal.state.kind, player->arsenal.state.source_weapon};
    qa_user_command converted = qa_app_sim_selected_native_command(sliced, call->aim, view, server_time);
    qa_actor_command input; memset(&input, 0, sizeof input);
    input.actor = actor; input.sequence = player->last_sequence + 1;
    input.source.kind = QA_COMMAND_SOURCE_BOT; input.source.provider = sim->recipe.map.entities.provider;
    input.command = converted; input.angle_space = "absolute";
    if (sim->has_selected_arsenal && qa_app_shared_sim_selected_arsenal_has(sim, actor)) input.arsenal = &arsenal;
    qa_app_sim_relative_movement_input relative = {input.command, input.angle_space};
    relative = qa_app_sim_relative_movement_command(relative, qa_app_sim_movement_player_read_state(player));
    input.command = relative.command; input.angle_space = relative.angle_space;
    qa_frame_context frame = sim->source_frame;
    frame.phase = QA_FRAME_PHASE_CLIENT_COMMAND; frame.elapsed.kind = QA_SOURCE_TIME_MILLISECONDS; frame.elapsed.value = slice;
    sim->has_native_slice_contacts = true;
    result = qa_app_sim_movement_player_move(player, input, frame, NULL); has_result = true;
    wl_take_slice_contacts(call);
    if (!qa_ss_is(result.status, "active") || !qa_session_actor_registry_is_live(sim->actors, actor)) break;
  }
  call->result.kind = QA_GUEST_VALUE_VOID;
  if (!has_result) { qa_cleanup_pop(&cleanup); wl_movement_cleanup(call); return; }
  bool active = qa_ss_is(result.status, "active");
  qa_body_state exact;
  if (classic && active && qa_app_shared_sim_body(sim, actor, &exact)) {
    qa_ss_exact_motion motion = {exact.origin, exact.velocity};
    qa_ss_map_set(&sim->native_exact_motions, &actor, &motion);
  }
  qa_body_state body;
  if (active && qa_session_actor_registry_is_live(sim->actors, actor) && qa_app_shared_sim_body(sim, actor, &body)) {
    qa_compat_q2_native_input_motion_state written = {body.origin, body.velocity};
    call->projection->write(call->projection->ctx, written);
    bool ducked = player->bounds.max.z - player->bounds.min.z < player->standing_bounds.max.z - player->standing_bounds.min.z;
    bool has_ground = player->ground.kind != QA_TRACE_HIT_NONE;
    double ground_slot = 0;
    if (player->ground.kind == QA_TRACE_HIT_ACTOR) {
      qa_actor_source_result source = qa_session_actor_registry_source_of(sim->actors, player->ground.actor);
      ground_slot = source.has_value ? (double)source.slot : 0;
    }
    /* Pmove also lists solids the player ended against; brush models trace as world hits, so recover them by overlap. */
    qa_bounds abs = {{body.origin.x + body.bounds.min.x - 1, body.origin.y + body.bounds.min.y - 1, body.origin.z + body.bounds.min.z - 1},
      {body.origin.x + body.bounds.max.x + 1, body.origin.y + body.bounds.max.y + 1, body.origin.z + body.bounds.max.z + 1}};
    size_t found = 0;
    const qa_spatial_actor *items = qa_shared_query_actors(sim->scene, abs, "solid", &found);
    qa_app_sim_native_overlap *overlapped = wl_alloc(found + 1, sizeof *overlapped);
    size_t overlapped_count = 0;
    for (size_t i = 0; i < found; i++) {
      if (qa_same_actor(items[i].body.actor, actor)) continue;
      overlapped[overlapped_count].actor = items[i].body.actor;
      overlapped[overlapped_count].plane = qa_app_sim_overlap_plane(body.origin, body.bounds, items[i].body.absolute_bounds);
      overlapped_count++;
    }
    wl_slot_lookup lookup = {sim};
    size_t touch_count = 0;
    qa_app_sim_native_pmove_touch *touches = qa_app_sim_pmove_touch_list(call->contacts, call->contact_count, overlapped, overlapped_count,
        wl_source_slot, &lookup, &touch_count);
    free(overlapped);
    /* A resting player is not re-hit by the sweep each slice, so keep the last standable plane the provider reported
     * while grounded. Before any report (spawned already standing) the plane is derived flat at the feet. */
    qa_plane reported;
    bool has_reported = qa_app_sim_reported_ground_plane(call->contacts, call->contact_count, &reported);
    if (!has_ground) qa_ss_map_delete(&sim->native_ground_planes, &actor);
    else if (has_reported) qa_ss_map_set(&sim->native_ground_planes, &actor, &reported);
    qa_compat_q2_native_input_touch *written_touches = wl_alloc(touch_count + 1, sizeof *written_touches);
    for (size_t i = 0; i < touch_count; i++) { written_touches[i].slot = touches[i].slot; written_touches[i].plane = touches[i].plane; }
    free(touches);
    qa_compat_q2_native_input_motion_outputs outputs; memset(&outputs, 0, sizeof outputs);
    outputs.grounded = has_ground; outputs.has_ground_slot = has_ground; outputs.ground_slot = ground_slot; outputs.ducked = ducked;
    outputs.water_level = player->water_level; outputs.water_type = qa_app_shared_sim_native_water_contents(sim, player);
    outputs.view_height = player->view_height; outputs.view_angles = call->aim; outputs.bounds = player->bounds;
    outputs.touches = written_touches; outputs.touch_count = touch_count;
    if (has_ground) {
      const qa_plane *kept = qa_ss_map_get(&sim->native_ground_planes, &actor);
      qa_plane flat = {{0, 0, 1}, body.origin.z + body.bounds.min.z};
      outputs.has_ground_plane = true; outputs.ground_plane = kept != NULL ? *kept : flat;
    }
    call->projection->write_outputs(call->projection->ctx, &outputs);
    free(written_touches);
  }
  qa_cleanup_pop(&cleanup);
  wl_movement_cleanup(call);
}
static qa_guest_call_result wl_input_movement_fn(void *ctx, qa_mod_client_identity identity, const qa_compat_q2_native_input_motion *projection,
    qa_user_command command, qa_vec3 aim, void *run_ctx, qa_guest_call_result (*run)(void *run_ctx)) {
  qa_app_shared_simulation *sim = ((wl_input_services *)ctx)->sim;
  const qa_vec3 *velocity = qa_ss_map_get(&sim->native_equipment_velocity, &identity.actor);
  if (velocity != NULL) {
    qa_compat_q2_native_input_motion_state state = projection->read(projection->ctx);
    state.velocity = *velocity;
    projection->write(projection->ctx, state);
    qa_ss_map_delete(&sim->native_equipment_velocity, &identity.actor);
  }
  wl_input_movement call; memset(&call, 0, sizeof call);
  call.sim = sim; call.identity = identity; call.projection = projection; call.command = command; call.aim = aim;
  call.run_ctx = run_ctx; call.run = run;
  /* A character-only host must never step movement: only a foreign selected movement provider interposes at this boundary. */
  bool owned = qa_app_shared_sim_resolve_owned(sim, identity.actor, &call.owned);
  qa_app_sim_movement_player **player = !owned || !qa_app_shared_sim_foreign_native_movement(sim) ? NULL
      : qa_ss_map_get(&sim->native_movement_players, &call.owned);
  if (player == NULL) { wl_with_input_movement(sim, identity.actor, projection, wl_input_run, &call); return call.result; }
  /* Selected foreign movement owns this Pmove call: step the unified provider against the shared body and project the
   * result back into the guest pmove struct; the guest ClientThink continues around it. */
  call.player = *player;
  wl_with_input_movement(sim, identity.actor, projection, wl_selected_movement, &call);
  return call.result;
}

void qa_app_shared_sim_bind_native_input(qa_app_shared_simulation *sim) {
  if ((sim->strategy->as_q2_native(sim) == NULL)) return;
  const qa_app_sim_native_q2_travel *travel = sim->options.native_q2_travel;
  const qa_app_sim_native_q2_travel_clients *retained = travel == NULL ? NULL : travel->rerelease ? &travel->rerelease_travel.base : &travel->classic.base;
  if (retained != NULL && retained->has_dropped_pickups)
    qa_app_sim_source_pickup_cargo_revisit(sim->dropped_pickups, retained->dropped_pickups, retained->dropped_pickup_level_count,
        sim->recipe.map.geometry.requested_path);
  if (sim->has_selected_arsenal || sim->hand_grenades != NULL) {
    if (sim->native_primary_weapons != NULL) {
      qa_compat_q2_native_primary_weapons_close(sim->native_primary_weapons);
      qa_compat_q2_native_primary_weapons_destroy(sim->native_primary_weapons); sim->native_primary_weapons = NULL;
    }
    const qa_compat_q2_native_primary_profile *primary = qa_app_shared_sim_native_primary(sim);
    if (primary == NULL) wl_fail("Native primary weapon source is unqualified");
    qa_app_sim_native_primary_weapon_context *context = qa_app_shared_sim_keep(sim, sizeof *context);
    qa_compat_q2_native_primary_weapon_hooks hooks; memset(&hooks, 0, sizeof hooks);
    hooks.ctx = sim; hooks.selected = wl_native_weapon_selected; hooks.completed = wl_native_weapon_completed; hooks.spawned = wl_native_weapon_spawned;
    sim->native_primary_weapons = qa_app_sim_bind_native_primary_weapons(context, wl_q2_native_world(sim), sim->actors, primary->weapons, hooks);
  }
  const qa_compat_q2_native_primary_profile *profile = qa_app_shared_sim_native_primary(sim);
  const qa_compat_q2_native_primary_command_profile *command_profile = profile != NULL ? profile->commands : NULL;
  const qa_compat_q2_native_primary_inventory_profile *inventory_profile = profile != NULL ? profile->inventory : NULL;
  const qa_compat_q2_native_primary_drop_profile *drop_profile = profile != NULL ? profile->drop : NULL;
  if (command_profile != NULL && inventory_profile != NULL && drop_profile != NULL) {
    if (sim->native_primary_commands != NULL) {
      qa_compat_q2_native_primary_commands_close(sim->native_primary_commands);
      qa_compat_q2_native_primary_commands_destroy(sim->native_primary_commands); sim->native_primary_commands = NULL;
    }
    qa_compat_q2_native_primary_command_hooks command_hooks = {sim, wl_native_give, wl_native_give_item, wl_native_give_ammo, wl_native_drop};
    sim->native_primary_commands = qa_compat_q2_native_primary_commands_create(wl_kept_native_host(sim), command_profile, command_hooks);
    if (sim->native_primary_commands == NULL) qa_range_error("Out of memory");
    const qa_item_id prototypes[] = {inventory_profile->proto_weapon, inventory_profile->proto_ammunition, inventory_profile->proto_usable,
      inventory_profile->proto_passive, inventory_profile->proto_droppable, inventory_profile->proto_undroppable};
    for (size_t i = 0; i < sizeof prototypes / sizeof *prototypes; i++) {
      qa_compat_q2_native_source_item item;
      if (!qa_compat_q2_native_primary_commands_source_item(sim->native_primary_commands, prototypes[i], &item))
        wl_fail("Native inventory prototype %s is absent from its declared original item table", prototypes[i]);
    }
    if (sim->hand_grenades != NULL) {
      qa_provider_id provider = qa_app_sim_hand_grenade_runtime_selection(sim->hand_grenades)->source.provider;
      qa_item_id item = qa_source_equipment_item(profile->weapons->equipment_contexts, profile->weapons->equipment_context_count, provider);
      if (item != NULL) {
        size_t count = 0; bool available = false;
        const qa_compat_q2_native_source_weapon *weapons = qa_compat_q2_native_primary_commands_weapons(sim->native_primary_commands, &count);
        for (size_t i = 0; i < count && !available; i++) available = qa_ss_is(weapons[i].item, item);
        if (!available) wl_fail("Equipment %s names an unavailable original cadence item %s", provider, item);
      }
    }
    if (sim->has_selected_arsenal) {
      const qa_pickup_supply_profile *supply = sim->selected_original_supply;
      if (supply == NULL) wl_fail("Native selected weapons require original supply");
      size_t catalog_count = 0, weapon_count = 0, source_count = 0;
      const qa_selected_pickup_weapon *catalog = qa_app_shared_sim_selected_arsenal_catalog(sim, &catalog_count);
      const qa_compat_q2_native_source_weapon *weapons = qa_compat_q2_native_primary_commands_weapons(sim->native_primary_commands, &weapon_count);
      qa_pickup_weapon *original = wl_alloc(weapon_count + 1, sizeof *original);
      for (size_t i = 0; i < weapon_count; i++) { original[i].item = weapons[i].item; original[i].ammo = weapons[i].ammo; }
      qa_weapon_source_entry *sources = qa_selected_weapon_sources(*supply, catalog, catalog_count, original, weapon_count, &source_count);
      free(original);
      qa_ss_map_clear(&sim->selected_original_weapons);
      for (size_t i = 0; i < source_count; i++) {
        qa_ss_optional_item value = {sources[i].source != NULL, sources[i].source != NULL ? qa_app_shared_sim_intern(sim, sources[i].source) : NULL};
        qa_ss_map_set(&sim->selected_original_weapons, &sources[i].item, &value);
      }
      sim->has_selected_original_weapons = true;
      free(sources);
      /* new Set(catalog.flatMap(weapon => weapon.ammo === null ? [] : [weapon.ammo])): first-occurrence order. */
      catalog = qa_app_shared_sim_selected_arsenal_catalog(sim, &catalog_count);
      qa_item_id *ammo = wl_alloc(catalog_count + 1, sizeof *ammo);
      size_t ammo_count = 0;
      for (size_t i = 0; i < catalog_count; i++) {
        if (catalog[i].ammo == NULL) continue;
        bool seen = false;
        for (size_t j = 0; j < ammo_count && !seen; j++) seen = qa_ss_is(ammo[j], catalog[i].ammo);
        if (!seen) ammo[ammo_count++] = catalog[i].ammo;
      }
      for (size_t i = 0; i < ammo_count; i++) qa_app_shared_sim_native_drop_ammo_source(sim, ammo[i]);
      free(ammo);
    }
    if (sim->native_primary_inventory != NULL) {
      qa_compat_q2_native_primary_inventory_close(sim->native_primary_inventory);
      qa_compat_q2_native_primary_inventory_destroy(sim->native_primary_inventory); sim->native_primary_inventory = NULL;
    }
    qa_compat_q2_native_primary_inventory_hooks inventory_hooks = {sim, wl_native_rows, wl_native_use, wl_native_descriptor, wl_native_item_at, wl_native_print};
    sim->native_primary_inventory = qa_compat_q2_native_primary_inventory_create(wl_kept_native_host(sim), inventory_profile, inventory_hooks);
    if (sim->native_primary_inventory == NULL) qa_range_error("Out of memory");
    if (sim->native_primary_drop != NULL) {
      qa_compat_q2_native_primary_drop_close(sim->native_primary_drop);
      qa_compat_q2_native_primary_drop_destroy(sim->native_primary_drop); sim->native_primary_drop = NULL;
    }
    qa_compat_q2_native_primary_drop_hooks drop_hooks = {sim, wl_native_rows, wl_native_drop_selected, wl_native_drop_projection, wl_native_dropped,
      wl_native_consume, wl_native_drop_action, wl_native_print};
    sim->native_primary_drop = qa_compat_q2_native_primary_drop_create(wl_kept_native_host(sim), drop_profile, sim->native_primary_commands, drop_hooks);
    if (sim->native_primary_drop == NULL) qa_range_error("Out of memory");
  } else if (sim->has_selected_arsenal) wl_fail("Native selected inventory requires qualified original command policies");
  if (sim->has_selected_arsenal) {
    if (sim->has_dispose_native_pickup_supply) sim->dispose_native_pickup_supply.dispose(sim->dispose_native_pickup_supply.ctx);
    qa_shared_pickup_admission *admission = sim->selected_supply;
    if (admission == NULL) wl_fail("Native selected weapons have no supply mapping");
    wl_pickup_supply *supply = qa_app_shared_sim_keep(sim, sizeof *supply);
    supply->sim = sim; supply->admission = admission; supply->rerelease = qa_ss_is(sim->source.edition, "rerelease");
    supply->host = supply->rerelease ? (void *)qa_app_sim_rerelease_guest_source_host(qa_app_sim_rerelease_guest_world_source(sim->source.rerelease_world))
        : (void *)qa_app_sim_classic_guest_source_host(qa_app_sim_classic_guest_world_source(sim->source.classic_world));
    qa_compat_q2_native_pickup_supply owner = {supply, wl_supply_owns};
    if (supply->rerelease) qa_compat_q2_rerelease_q2_guest_host_bind_pickup_supply(supply->host, owner);
    else qa_compat_q2_classic_guest_host_bind_pickup_supply(supply->host, owner);
    sim->has_dispose_native_pickup_supply = true; sim->dispose_native_pickup_supply = (qa_ss_dispose){supply, wl_supply_dispose};
    size_t count = 0;
    qa_actor_id *players = wl_players(sim, &count);
    for (size_t i = 0; i < count; i++) {
      qa_owned_actor owner_actor;
      if (qa_app_shared_sim_resolve_owned(sim, players[i], &owner_actor)) qa_app_shared_sim_bind_selected_original_pickups(sim, owner_actor);
    }
    free(players);
  }
  wl_input_services *record = qa_app_shared_sim_keep(sim, sizeof *record);
  record->sim = sim;
  qa_compat_q2_native_input_services services; memset(&services, 0, sizeof services);
  services.applications = sim->mod_client_applications;
  services.numeric = qa_ss_is(sim->source.edition, "classic")
      ? &qa_app_sim_classic_guest_services_options_of(qa_app_sim_classic_guest_world_services(sim->source.classic_world))->numeric
      : &qa_app_sim_rerelease_guest_services_options_of(qa_app_sim_rerelease_guest_world_services(sim->source.rerelease_world))->base.numeric;
  services.ctx = record;
  services.body_bounds = wl_input_body_bounds; services.client_outputs = wl_input_client_outputs; services.identity = wl_input_identity;
  services.live = wl_input_live; services.accepted = wl_input_accepted; services.frame = wl_input_frame; services.on_release = wl_input_on_release;
  services.remove_release = wl_input_remove_release; services.retired = wl_input_retired; services.original_command = wl_input_original_command;
  services.movement = wl_input_movement_fn;
  if (qa_ss_is(sim->source.edition, "classic")) qa_app_sim_classic_guest_world_bind_input(sim->source.classic_world, services);
  else qa_app_sim_rerelease_guest_world_bind_input(sim->source.rerelease_world, services);
}
static int32_t wl_contents(qa_app_shared_simulation *sim, qa_vec3 point) { return qa_js_to_int32(qa_app_shared_sim_contents(sim, point, "q2")); }
void qa_app_shared_sim_tick_native_characters(qa_app_shared_simulation *sim) {
  if ((sim->strategy->as_q2_native(sim) == NULL) || !qa_app_shared_sim_foreign_native_character(sim)) return;
  size_t count = sim->source.clients.count;
  qa_owned_actor *clients = wl_alloc(count, sizeof *clients);
  for (size_t i = 0; i < count; i++) clients[i] = *(qa_owned_actor *)qa_ss_map_value_at(&sim->source.clients, i);
  for (size_t i = 0; i < count; i++) {
    qa_owned_actor actor = clients[i];
    qa_app_sim_movement_player **entry = qa_ss_map_get(&sim->native_movement_players, &actor);
    if (!entry || qa_ss_map_has(&sim->player_states, &actor)) continue;
    qa_app_sim_movement_player *player = *entry;
    qa_app_shared_sim_sync_native_movement_combat(sim, actor);
    double health = qa_app_shared_sim_native_player_ui(sim, actor.id).health;
    double *previous_value = qa_ss_map_get(&sim->native_character_health, &actor.id);
    bool has_previous = previous_value != NULL; double previous = has_previous ? *previous_value : 0;
    qa_ss_map_set(&sim->native_character_health, &actor.id, &health);
    qa_ss_character_command *stored = qa_ss_map_get(&sim->native_character_commands, &actor.id);
    qa_ss_character_command buttons = stored ? *stored : (qa_ss_character_command){false, false};
    qa_body_state body;
    if (!qa_app_shared_sim_body(sim, actor.id, &body)) continue;
    player->view_angles = qa_app_shared_sim_player_view(sim, actor.id).angles;
    if (player->character == QA_GAME_FAMILY_Q1) {
      qa_q1_character_actor **character_entry = qa_ss_map_get(&sim->q1_characters, &actor);
      if (!character_entry) continue;
      qa_q1_character_actor *character = *character_entry;
      if (has_previous && previous > 0 && health <= 0) qa_q1_character_actor_die(character, NULL);
      else if (has_previous && previous <= 0 && health > 0) qa_q1_character_actor_respawn(character, NULL);
      else if (has_previous && health > 0 && health < previous) qa_q1_character_actor_pain(character, NULL, previous - health, false);
      int32_t eye = wl_contents(sim, (qa_vec3){body.origin.x, body.origin.y, body.origin.z + player->view_height});
      int32_t waist = wl_contents(sim, (qa_vec3){body.origin.x, body.origin.y, body.origin.z + (body.bounds.min.z + body.bounds.max.z) / 2});
      int32_t feet = wl_contents(sim, (qa_vec3){body.origin.x, body.origin.y, body.origin.z + body.bounds.min.z + 1});
      int32_t wet = (eye & (32 | 64 | 128)) != 0 ? eye : (waist & (32 | 64 | 128)) != 0 ? waist : feet;
      qa_q1_character_source_pose pose = qa_app_shared_sim_q1_character_pose(sim, actor.id);
      qa_q1_character_input input; memset(&input, 0, sizeof input);
      input.axe_pose = pose.has_axe_pose ? pose.axe_pose : qa_ss_is(qa_app_shared_sim_native_player_ui(sim, actor.id).active_weapon, "q1:weapon/axe");
      input.attack = buttons.attack; input.jump = buttons.jump; input.use = false;
      input.water_level = (eye & (32 | 64 | 128)) != 0 ? 3 : (waist & (32 | 64 | 128)) != 0 ? 2 : (feet & (32 | 64 | 128)) != 0 ? 1 : 0;
      input.water_type = (wet & 32) != 0 ? "water" : (wet & 64) != 0 ? "slime" : (wet & 128) != 0 ? "lava" : "empty";
      input.invisible = false;
      qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor.id);
      input.invulnerable = combat.has_value ? combat.value.invulnerable : false;
      qa_q1_character_presentation frame = qa_q1_character_actor_frame(character, wl_time(sim), input);
      qa_animation_state state; memset(&state, 0, sizeof state);
      state.kind = "q1"; state.frame = frame.frame; state.next_frame_seconds = wl_time(sim) + 0.1;
      player->animation = (qa_actor_animation_state){sim->recipe.character.definition.provider, state};
    } else if (player->character == QA_GAME_FAMILY_Q3) {
      if (has_previous && previous <= 0 && health > 0)
        player->animation = (qa_actor_animation_state){sim->recipe.character.definition.provider, qa_q3_spawn_animation()};
      if (!qa_ss_is(player->animation.state.kind, "q3")) continue;
      double slot = (double)qa_app_shared_sim_native_q2_client(sim, actor.id).slot + 1;
      double flags = sim->source.classic_world ? qa_app_sim_classic_guest_world_player_state(sim->source.classic_world, slot).movement.flags
        : qa_app_sim_rerelease_guest_world_player_state(sim->source.rerelease_world, slot).movement.flags;
      int32_t movement_flags = qa_js_to_int32(flags);
      bool moving = qa_js_hypot2(body.velocity.x, body.velocity.y) != 0;
      int32_t waist = wl_contents(sim, (qa_vec3){body.origin.x, body.origin.y, body.origin.z + (body.bounds.min.z + body.bounds.max.z) / 2});
      const char *locomotion = (waist & (32 | 64 | 128)) != 0 ? "swim" : (movement_flags & 4) == 0 ? "jump"
        : (movement_flags & 1) != 0 ? "crouch" : moving ? "run" : "idle";
      qa_app_sim_player_view view = qa_app_shared_sim_player_view(sim, actor.id);
      double yaw = view.angles.y * M_PI / 180;
      bool backwards = body.velocity.x * cos(yaw) + body.velocity.y * sin(yaw) < 0;
      qa_animation_step_input input = {actor, sim->source_frame, player->animation, locomotion, backwards, false};
      qa_animation_step_result result = qa_q3_step_character_animation(input,
          strstr(sim->recipe.character.definition.content, "missionpack") ? "missionpack" : "baseq3", health <= 0, 0);
      player->animation = result.animation;
      free((void *)result.effects);
    }
  }
  free(clients);
}

/* ---- Rerelease navigation and native mod context (3234-3253) ---- */
void qa_app_shared_sim_install_rerelease_navigation(qa_app_shared_simulation *sim, qa_app_sim_application_bot_navigation *navigation) {
  qa_app_shared_sim_assert_open(sim);
  if (sim->native_navigation != NULL && sim->native_navigation != navigation) wl_fail("Native navigation already belongs to this world");
  sim->native_navigation = navigation;
}
qa_app_sim_application_bot_navigation *qa_app_shared_sim_rerelease_navigation(qa_app_shared_simulation *sim) { return sim->native_navigation; }
static void *wl_nav_runtime(void *ctx) {
  qa_app_shared_simulation *sim = ctx;
  return sim->native_navigation ? qa_app_sim_application_bot_navigation_runtime(sim->native_navigation) : NULL;
}
/* The C compat imports call rereleasePathToGoal through the services
 * (compat/q2/rerelease/navigation.h); the points live until the next call. */
static qa_compat_q2_rerelease_path_result wl_nav_path_to_goal(void *ctx, void *runtime, qa_compat_q2_rerelease_path_request request) {
  static __thread qa_rerelease_path_info last;
  (void)ctx;
  qa_rerelease_path_info_destroy(&last);
  qa_rerelease_path_request native = {request.start, request.goal, (int64_t)request.flags, request.move_distance, request.ignore_node_flags,
    request.min_height, request.max_height, request.radius, request.drop_height, request.jump_height};
  last = qa_rerelease_path_to_goal(runtime, native);
  return (qa_compat_q2_rerelease_path_result){last.points, last.point_count, last.distance_squared, last.first, last.second,
    (double)last.link_type, (double)last.code};
}
static qa_compat_q2_rerelease_goal_status wl_nav_move_to_point(void *ctx, qa_actor_id actor, qa_vec3 point, double tolerance) {
  return qa_app_sim_bot_services_move_to_point(&((qa_app_shared_simulation *)ctx)->bot_services, actor, point, tolerance);
}
static qa_compat_q2_rerelease_goal_status wl_nav_follow_actor(void *ctx, qa_actor_id actor, qa_actor_id target) {
  return qa_app_sim_bot_services_follow_actor(&((qa_app_shared_simulation *)ctx)->bot_services, actor, target);
}
qa_compat_q2_rerelease_navigation_services qa_app_shared_sim_rerelease_navigation_services(qa_app_shared_simulation *sim) {
  return (qa_compat_q2_rerelease_navigation_services){sim, wl_nav_runtime, wl_nav_path_to_goal, wl_nav_move_to_point, wl_nav_follow_actor};
}
static void wl_mod_collision(void *ctx, qa_owned_actor actor, qa_actor_collision collision) {
  qa_app_sim_shared_physics_set_collision(((qa_app_shared_simulation *)ctx)->physics, actor, collision);
}
static qa_q2m_state *wl_no_monster(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; return NULL; } /* () => undefined */
static qa_q2_foundation_host *wl_mod_engine(void *ctx, qa_provider_reference source, const qa_app_sim_actor_host_runtime *runtime) {
  qa_app_shared_simulation *sim = ctx;
  qa_q2_foundation_host *host = qa_app_shared_sim_keep(sim, sizeof *host);
  *host = qa_app_shared_sim_q2_actor_host(sim, source, *runtime, wl_no_monster, NULL, NULL);
  return host;
}
/* nativeModContext(): false = TS undefined (no weapon behavior clock). */
bool qa_app_shared_sim_native_mod_context(qa_app_shared_simulation *sim, void *out_context) {
  const qa_windows_capabilities *clock = sim->options.weapon_behavior_clock;
  if (clock == NULL) return false;
  qa_app_sim_native_mod_host_context *context = out_context;
  memset(context, 0, sizeof *context);
  context->scene = sim->scene; context->map_path = sim->recipe.map.geometry.requested_path; context->max_clients = sim->options.max_clients;
  context->frame_milliseconds = 25; context->skill = sim->options.skill; context->mode = sim->options.mode;
  context->gravity = qa_app_sim_shared_physics_gravity(sim->physics);
  context->clock = *clock; context->navigation = qa_app_shared_sim_rerelease_navigation_services(sim);
  context->ctx = sim; context->engine = wl_mod_engine; context->collision = wl_mod_collision;
  return true;
}

/* ---- createMonsterMovement (1993-2018) ---- */
typedef struct wl_monster_movement { qa_app_shared_simulation *sim; qa_app_sim_source_random *random; qa_numeric_profile numeric; } wl_monster_movement;
static double wl_move_random(void *ctx) { return qa_app_sim_source_random_next_integer(((wl_monster_movement *)ctx)->random); }
static qa_bounds wl_absolute_bounds(qa_app_shared_simulation *sim, qa_actor_id actor, qa_body_state body) {
  qa_linked_body_result linked = qa_shared_body_table_linked(wl_bodies(sim), actor);
  return linked.has_value ? linked.value.absolute_bounds : (qa_bounds){qa_ss_add(body.origin, body.bounds.min), qa_ss_add(body.origin, body.bounds.max)};
}
static bool wl_move_read_target(void *ctx, qa_actor_id actor, qa_q1_monster_move_target *out) {
  qa_app_shared_simulation *sim = ((wl_monster_movement *)ctx)->sim;
  qa_body_state body;
  if (!qa_app_shared_sim_body(sim, actor, &body)) return false;
  *out = (qa_q1_monster_move_target){body.origin, wl_absolute_bounds(sim, actor, body)};
  return true;
}
static qa_q1_actor *wl_q1_execution_entity(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor);
  return entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 ? entry->q1_entity : NULL;
}
static bool wl_move_read(void *ctx, qa_actor_id actor, qa_q1_monster_move_state *out) {
  qa_app_shared_simulation *sim = ((wl_monster_movement *)ctx)->sim;
  qa_body_state body; bool has_body = qa_app_shared_sim_body(sim, actor, &body);
  qa_q1_actor *entity = wl_q1_execution_entity(sim, actor);
  if (!has_body || !entity) return false;
  memset(out, 0, sizeof *out);
  out->origin = body.origin; out->angles = body.angles; out->bounds = body.bounds;
  out->absolute_bounds = wl_absolute_bounds(sim, actor, body);
  out->flags = entity->movement_flags;
  if (body.ground) { out->ground.kind = QA_TRACE_HIT_ACTOR; out->ground.actor = *body.ground; } else out->ground.kind = QA_TRACE_HIT_NONE;
  out->ideal_yaw = entity->ideal_yaw; out->yaw_speed = entity->yaw_speed;
  if (entity->monster && entity->monster->has_enemy) { out->has_enemy = true; out->enemy = entity->monster->enemy; }
  return true;
}
static void wl_move_write(void *ctx, qa_owned_actor actor, qa_q1_monster_move_state state) {
  qa_app_shared_simulation *sim = ((wl_monster_movement *)ctx)->sim;
  qa_body_state body; bool has_body = qa_app_shared_sim_body(sim, actor.id, &body);
  qa_q1_actor *entity = wl_q1_execution_entity(sim, actor.id);
  if (!has_body || !entity) return;
  qa_actor_id ground; bool has_ground = false;
  if (state.ground.kind == QA_TRACE_HIT_ACTOR) { ground = state.ground.actor; has_ground = true; }
  else if (state.ground.kind == QA_TRACE_HIT_WORLD) has_ground = qa_app_shared_sim_world_actor(sim, &ground);
  body.origin = state.origin; body.angles = state.angles; body.ground = has_ground ? &ground : NULL;
  qa_shared_body_table_write(wl_bodies(sim), actor, body);
  entity->movement_flags = state.flags; entity->ideal_yaw = state.ideal_yaw; entity->yaw_speed = state.yaw_speed;
}
static void wl_move_link(void *ctx, qa_owned_actor actor, bool triggers) {
  qa_app_shared_simulation *sim = ((wl_monster_movement *)ctx)->sim;
  qa_shared_body_table_link(wl_bodies(sim), actor, NULL);
  if (triggers) qa_app_sim_shared_physics_touch_triggers(sim->physics, actor);
}
void qa_app_shared_sim_create_monster_movement(qa_app_shared_simulation *sim, qa_numeric_profile numeric,
    qa_app_sim_source_random *random, qa_q1_monster_movement *out) {
  /* The record keeps the profile at a stable address: createNumericOperations
   * caches by profile identity. */
  wl_monster_movement *record = qa_app_shared_sim_keep(sim, sizeof *record);
  record->sim = sim; record->random = random; record->numeric = numeric;
  qa_q1_monster_move_services services; memset(&services, 0, sizeof services);
  services.ctx = record; services.scene = qa_shared_as_scene_queries(sim->scene);
  services.numeric = *qa_create_numeric_operations(&record->numeric);
  services.random = (qa_q1_monster_random){record, wl_move_random};
  services.read = wl_move_read; services.read_target = wl_move_read_target; services.write = wl_move_write; services.link = wl_move_link;
  qa_q1_monster_movement_init(out, services);
}

/* ---- beginMonsterFrames (1925-1948) ---- */
void qa_app_shared_sim_begin_monster_frames(qa_app_shared_simulation *sim, double milliseconds, bool map) {
  qa_clock_profile world_clock = qa_app_sim_provider_timing(sim->recipe, sim->recipe.map.entities.provider).clock;
  for (size_t i = 0; i < sim->monster_sources.count; i++) {
    qa_app_sim_selected_monster_source *source = *(qa_app_sim_selected_monster_source **)qa_ss_map_value_at(&sim->monster_sources, i);
    source->clock.advanced = false;
    if (qa_ss_is(source->kind, "q1")) {
      if (!map) continue;
      qa_frame_context projected = qa_app_sim_provider_frame(sim->source_frame, world_clock,
          qa_app_sim_provider_timing(sim->recipe, source->reference.provider).clock);
      source->clock.frame = projected; source->clock.advanced = true;
      qa_q1_begin_frame(source->game, qa_ss_seconds(projected.time), qa_ss_seconds(projected.elapsed));
      if (source->addon) qa_q1ad_frame(source->addon, qa_ss_seconds(projected.elapsed));
    } else {
      qa_q2_game_services *game = qa_q2_entity_services_game(source->q2->game);
      wl_clock clock = {source->clock.frame, source->clock.advanced};
      if (!wl_advance_q2_clock(&clock, game->options.edition == QA_Q2_EDITION_CLASSIC, milliseconds)) continue;
      source->clock.frame = clock.frame; source->clock.advanced = clock.advanced;
      qa_q2m_monsters_begin_frame(&source->q2->monsters, game);
    }
  }
}

/* ---- selectedCreaturePickups (1950-1991) ---- */
typedef struct wl_creature_pickups {
  qa_app_shared_simulation *sim;
  qa_shared_pickup_admission *admission;
  qa_pickup_admission contract;
  qa_pickup_supply_profile identity;
  qa_pickup_supply_mapping *mappings; /* ammo then weapons, each [source] -> [source] */
} wl_creature_pickups;
typedef struct wl_pickup_count { qa_app_shared_simulation *sim; qa_actor_id actor; const qa_pickup_ammo_receipt *grants; size_t grant_count; } wl_pickup_count;
static double wl_pickup_before(void *ctx, qa_item_id item) {
  wl_pickup_count *count = ctx;
  for (size_t i = 0; i < count->grant_count; i++) if (qa_ss_is(count->grants[i].item, item)) return count->grants[i].before;
  return qa_shared_inventory_table_count(count->sim->inventory, count->actor, item);
}
static void wl_selected_pickup_ammo(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_pickup_ammo_receipt *grants, size_t count, bool auto_switch) {
  if (sim->selected_arsenal.q1) qa_app_q1_selected_arsenal_pickup_ammo(sim->selected_arsenal.q1, actor, grants, count, auto_switch);
  else if (sim->selected_arsenal.q2) qa_app_q2_selected_arsenal_pickup_ammo(sim->selected_arsenal.q2, actor, grants, count, auto_switch);
  else qa_app_q3_selected_arsenal_pickup_ammo(sim->selected_arsenal.q3, actor, grants, count, auto_switch);
}
static void wl_selected_pickup_weapons(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_item_id *weapons, size_t count, qa_pickup_selection selection) {
  if (sim->selected_arsenal.q1) qa_app_q1_selected_arsenal_pickup_weapons(sim->selected_arsenal.q1, actor, weapons, count, selection);
  else if (sim->selected_arsenal.q2) qa_app_q2_selected_arsenal_pickup_weapons(sim->selected_arsenal.q2, actor, weapons, count, selection);
  else qa_app_q3_selected_arsenal_pickup_weapons(sim->selected_arsenal.q3, actor, weapons, count, selection);
}
static void wl_creature_ammo_granted(void *ctx, qa_owned_actor actor, const qa_pickup_ammo_receipt *grants, size_t grant_count, bool auto_switch) {
  qa_app_shared_simulation *sim = ((wl_creature_pickups *)ctx)->sim;
  if (sim->has_selected_arsenal) {
    wl_selected_pickup_ammo(sim, actor, grants, grant_count, auto_switch);
    qa_app_shared_sim_refresh_arsenal_cache(sim, actor.id);
  } else if ((sim->strategy->as_q1(sim) != NULL)) {
    qa_q1_services *game = sim->source.q1_game;
    qa_q1_player_state *player = qa_q1_player(game, actor.id);
    if (!player) wl_fail("Q1 pickup recipient has no admitted player");
    wl_pickup_count count = {sim, actor.id, grants, grant_count};
    qa_q1_weapon before = qa_q1_choose_best_counted(game, actor, wl_pickup_before, &count);
    qa_q1_ammo_pickup_selection(game, player, before, auto_switch && !qa_ss_is(player->auto_switch, "never"));
  }
}
static const char *wl_selection_name(qa_pickup_selection selection) {
  return selection == QA_PICKUP_SELECTION_NEVER ? "never" : selection == QA_PICKUP_SELECTION_ALWAYS ? "always" : "better";
}
static void wl_creature_weapon_granted(void *ctx, qa_owned_actor actor, const qa_item_id *weapons, size_t weapon_count, qa_pickup_selection selection) {
  qa_app_shared_simulation *sim = ((wl_creature_pickups *)ctx)->sim;
  if (sim->has_selected_arsenal) {
    wl_selected_pickup_weapons(sim, actor, weapons, weapon_count, selection);
    qa_app_shared_sim_refresh_arsenal_cache(sim, actor.id);
  } else if ((sim->strategy->as_q1(sim) != NULL)) {
    qa_q1_services *game = sim->source.q1_game;
    qa_q1_player_state *player = qa_q1_player(game, actor.id);
    if (!player) wl_fail("Q1 pickup recipient has no admitted player");
    for (size_t i = 0; i < weapon_count; i++) {
      qa_q1_weapon weapon = NULL;
      for (size_t j = 0; j < 8 && !weapon; j++) if (qa_ss_is(qa_q1_services_weapon_item(game, QA_Q1_WEAPONS[j]), weapons[i])) weapon = QA_Q1_WEAPONS[j];
      for (size_t j = 0; j < game->registered_weapon_count && !weapon; j++)
        if (qa_ss_is(qa_q1_services_weapon_item(game, game->registered_weapons[j].def->id), weapons[i])) weapon = game->registered_weapons[j].def->id;
      if (weapon) qa_q1_weapon_pickup_selection(game, player, weapon, qa_ss_is(player->auto_switch, "never") ? "never" : wl_selection_name(selection));
    }
  } else if ((sim->strategy->as_q2(sim) != NULL) && selection != QA_PICKUP_SELECTION_NEVER) {
    qa_q2_game_services *game = sim->source.product->services;
    qa_q2w_weapons *source_weapons = sim->source.q2_weapons;
    qa_q2_entity *player = qa_q2_game_entity_of(game, &actor.id);
    if (!player) wl_fail("Q2 pickup recipient has no admitted player");
    size_t definition_count = 0;
    const qa_q2w_definition *const *definitions = qa_q2w_weapons_registered_definitions(source_weapons, &definition_count);
    for (size_t i = 0; i < weapon_count; i++) {
      const qa_q2w_definition *definition = NULL;
      for (size_t j = 0; j < definition_count && !definition; j++) if (qa_ss_is(definitions[j]->item, weapons[i])) definition = definitions[j];
      qa_q2w_state *state = qa_q2w_ballistics_state(&source_weapons->base, actor.id);
      qa_item_id current_item = NULL;
      if (state && state->weapon) for (size_t j = 0; j < definition_count && !current_item; j++)
        if (qa_ss_is(definitions[j]->name, state->weapon)) current_item = definitions[j]->item;
      if (definition && qa_composition_q1_q2_pickup_select(current_item, weapons[i], selection))
        qa_q2w_weapons_request_weapon(source_weapons, (qa_q2w_owner){player->actor, player->view_height}, game, definition->name, false);
    }
  }
}
static bool wl_q1_game_registers(qa_q1_services *game, const char *weapon) {
  for (size_t i = 0; i < game->registered_weapon_count; i++) if (qa_ss_is(game->registered_weapons[i].def->id, weapon)) return true;
  return false;
}
/* selectedCreaturePickups(): the admission owner is kept until destroy. */
static wl_creature_pickups *wl_selected_creature_pickups(qa_app_shared_simulation *sim) {
  wl_creature_pickups *record = qa_app_shared_sim_keep(sim, sizeof *record);
  record->sim = sim;
  const qa_pickup_supply_profile *supply = &QA_COMPOSITION_Q1_Q3_SUPPLY_PROFILE;
  record->mappings = qa_app_shared_sim_keep(sim, (supply->ammo_count + supply->weapon_count + 1) * sizeof *record->mappings);
  for (size_t i = 0; i < supply->ammo_count; i++) record->mappings[i] = (qa_pickup_supply_mapping){supply->ammo[i].source, &supply->ammo[i].source, 1};
  for (size_t i = 0; i < supply->weapon_count; i++)
    record->mappings[supply->ammo_count + i] = (qa_pickup_supply_mapping){supply->weapons[i].source, &supply->weapons[i].source, 1};
  record->identity = (qa_pickup_supply_profile){"composition:q1-creature-q1-supply", record->mappings, supply->ammo_count, NULL, 0, NULL, 0,
    record->mappings + supply->ammo_count, supply->weapon_count};
  const char *family = sim->has_selected_arsenal ? sim->selected_arsenal.family : NULL;
  qa_pickup_supply_profile profile = qa_ss_is(family, "q3") ? QA_COMPOSITION_Q1_Q3_SUPPLY_PROFILE
    : qa_ss_is(family, "q2") ? QA_COMPOSITION_Q1_Q2_SUPPLY_PROFILE
    : qa_ss_is(family, "q1") && wl_q1_game_registers(qa_app_shared_sim_selected_q1_game(sim), "hipnotic:laser") ? QA_COMPOSITION_Q1_HIPNOTIC_SUPPLY_PROFILE
    : qa_ss_is(family, "q1") || (sim->strategy->as_q1(sim) != NULL) ? record->identity : QA_COMPOSITION_Q1_Q2_SUPPLY_PROFILE;
  qa_shared_pickup_admission_options options; memset(&options, 0, sizeof options);
  options.inventory = sim->inventory; options.profile = profile; options.ctx = record;
  options.ammo_granted = wl_creature_ammo_granted; options.weapon_granted = wl_creature_weapon_granted;
  record->admission = qa_shared_pickup_admission_create(options);
  if (!record->admission) qa_range_error("Out of memory");
  record->contract = qa_shared_pickup_admission_as_contract(record->admission);
  return record;
}

/* ---- monsterSourceFor (1102-1179) ---- */
typedef struct wl_monster_source {
  qa_app_sim_selected_monster_source base; /* first: the monsterSources map stores &base */
  qa_app_shared_simulation *sim;
  bool q2_rerelease;
  qa_q1_options q1_options;
  qa_app_sim_q1_expansion *expansion;
  qa_q1_creatures *creatures;
  qa_q1_state_extension creatures_extension;
  wl_creature_pickups *pickups;
  qa_q2_foundation_host q2_host;
  qa_q2_game_options q2_options;
} wl_monster_source;
static void wl_ms_emit(void *ctx, const qa_q1ad_event *event) {
  wl_monster_source *source = ctx;
  qa_app_shared_sim_emit_q1_addon(source->sim, source->base.reference.content, event, source->base.clock.frame.time);
}
static bool wl_ms_is_monster(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((wl_monster_source *)ctx)->sim;
  qa_app_sim_actor_execution *execution = qa_ss_map_get(&sim->actor_executions, &actor);
  return execution && execution->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 ? (qa_js_to_int32(execution->q1_entity->movement_flags) & 32) != 0
    : execution && execution->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2 && (qa_js_to_int32(execution->q2_entity->server_flags) & 4) != 0;
}
static double wl_ms_cvar(void *ctx, const char *name) {
  qa_app_shared_simulation *sim = ((wl_monster_source *)ctx)->sim;
  if ((sim->strategy->as_q1(sim) != NULL)) return qa_cvar_registry_variable_value(sim->source.q1_cvars, name);
  return sim->q2_server_registry ? qa_cvar_registry_variable_value(sim->q2_server_registry, name) : 0;
}
static void wl_ms_set_cvar(void *ctx, const char *name, const char *value) {
  qa_app_shared_simulation *sim = ((wl_monster_source *)ctx)->sim;
  qa_cvar_registry *registry = (sim->strategy->as_q1(sim) != NULL) ? sim->source.q1_cvars : sim->q2_server_registry;
  if (registry == NULL) wl_fail("Selected Q1 addon requires the shared server settings registry");
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(registry, name, value, true, &snapshot)) qa_cvar_read_free(&snapshot);
}
static bool wl_creature_kill(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx; wl_fail("Selected creature has no authored mission: %s", monster->entity->classname);
}
static void wl_creature_finale(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx; wl_fail("Selected creature finale is unavailable: %s", monster->entity->classname);
}
static bool wl_creatures_capture(void *ctx, uint8_t **out, size_t *out_length) {
  wl_monster_source *source = ctx;
  qa_save_value *root = qa_save_value_object();
  if (!root || !qa_save_value_set(root, "version", qa_save_value_number(1)) || !qa_q1_creatures_capture_fields(source->creatures, root)) {
    qa_save_value_free(root); return false;
  }
  *out = qa_encode_checkpoint_value(root, out_length);
  qa_save_value_free(root);
  return *out != NULL;
}
static void wl_creatures_restore(void *ctx, const uint8_t *bytes, size_t length) {
  wl_monster_source *source = ctx;
  qa_save_value *value = qa_decode_checkpoint_value(bytes, length);
  qa_save_reader reader, version;
  if (!value || !qa_save_reader_init(&reader, value, "q1:selected-creatures")) qa_range_error("Out of memory");
  if (!qa_save_reader_field(&reader, "version", &version)) qa_range_error("Out of memory");
  qa_save_reader_literal_number(&version, 1);
  qa_q1_creatures_restore_fields(source->creatures, &reader);
  qa_save_reader_destroy(&version); qa_save_reader_destroy(&reader);
  qa_save_value_free(value);
}
static void wl_creatures_clone(void *ctx, qa_q1_actor *original, qa_q1_actor *target) {
  qa_q1_creatures_clone(((wl_monster_source *)ctx)->creatures, original, target);
}
/* Q2 monster source hooks. */
static void wl_ms_q2_emit(void *ctx, qa_q2w_event event) { wl_monster_source *s = ctx; qa_app_shared_sim_weapon_event(s->sim, s->base.reference.content, &event); }
static void wl_ms_q2_dodge(void *ctx, qa_actor_id monster, qa_actor_id attacker, double eta, qa_trace_result trace) {
  qa_app_shared_sim_q2_monster_dodge(((wl_monster_source *)ctx)->sim, monster, &attacker, eta, &trace);
}
static void wl_ms_q2_ammo_changed(void *ctx, qa_actor_id actor, qa_item_id ammo) {
  (void)ammo; /* TS ammoChanged: actor => ... */
  wl_q2_inventory_message(((wl_monster_source *)ctx)->sim, actor);
}
static const qa_monster_mission *wl_ms_mission(void *ctx, qa_actor_id actor) {
  return qa_ss_map_get(&((wl_monster_source *)ctx)->sim->monster_missions, &actor);
}
static double wl_ms_gravity(void *ctx) { return qa_app_sim_shared_physics_gravity(((wl_monster_source *)ctx)->sim->physics); }
static qa_q2mpm_powerups wl_ms_powerups(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((wl_monster_source *)ctx)->sim;
  if ((sim->strategy->as_q2(sim) != NULL)) {
    qa_q2mpm_powerups powers; qa_q2_composition_powerups composed = qa_q2_product_runtime_powerups(sim->source.product, actor);
    powers.quad_until = composed.quad_until; powers.double_until = composed.double_until; powers.invulnerability_until = composed.invulnerability_until;
    return powers;
  }
  /* Guest powerup timers are not public state on guest worlds; selected monsters do not react to them there. */
  return wl_foreign_powerups(sim, actor);
}
static void wl_ms_player_effect(void *ctx, qa_q2mp_player_effect event) {
  wl_monster_source *source = ctx;
  wl_emit_missionpack_player(source->sim, source->base.reference.content, &event, source->base.clock.frame.time);
}
static qa_q2m_state *wl_ms_monster(void *ctx, qa_actor_id actor) {
  wl_monster_source *source = ctx;
  if (!source->base.q2) return NULL;
  qa_q2m_context *context = qa_q2m_monsters_context(&source->base.q2->monsters, actor);
  return context ? context->state : NULL;
}
static qa_q2_entity_services *wl_ms_create_game(void *ctx, qa_q2m_monsters *monsters, const qa_q2_spawn_module *modules, size_t module_count) {
  (void)monsters; /* the actor host reads this source's monsters through wl_ms_monster */
  wl_monster_source *source = ctx;
  return qa_q2_entity_services_create(&source->q2_host, source->q2_options, modules, module_count);
}
qa_app_sim_selected_monster_source *qa_app_shared_sim_monster_source_for(qa_app_shared_simulation *sim, qa_monster_definition_reference definition) {
  qa_app_sim_selected_monster_source **existing = qa_ss_map_get(&sim->monster_sources, &definition.source.provider);
  if (existing) return *existing;
  const qa_monster_source_definition *registered = qa_monster_source(definition);
  qa_provider_reference reference = definition.source;
  qa_provider_timing timing = qa_app_sim_provider_timing(sim->recipe, reference.provider);
  bool q2 = qa_ss_is(registered->family, "q2");
  wl_monster_source *source = qa_app_shared_sim_keep(sim, sizeof *source);
  source->sim = sim; source->q2_rerelease = q2 && qa_ss_is(registered->edition, "rerelease");
  source->base.reference = reference;
  source->base.random = qa_app_sim_source_random_create(sim->options.seed, source->q2_rerelease ? "q2-rerelease" : "classic");
  source->base.clock.frame = wl_initial_frame(sim, timing.clock); source->base.clock.advanced = false;
  qa_app_sim_actor_host_runtime runtime = wl_make_runtime(sim, timing.numeric, source->base.random, &source->base.clock.frame,
      q2 ? (qa_ss_is(registered->edition, "classic") ? 0.1 : 0.025) : NAN, false);
  /* The map stores the source before its modules exist so the hooks resolve it. */
  qa_app_sim_selected_monster_source *stored = &source->base;
  if (!q2) {
    source->base.kind = "q1";
    qa_q1_options *options = &source->q1_options;
    options->provider = reference.provider; options->edition = registered->edition; options->skill = sim->options.skill;
    options->max_clients = sim->options.max_clients; options->has_max_clients = true;
    options->campaign = wl_campaign_provider(sim); options->combat_provider = sim->recipe.combat.provider;
    options->inventory_provider = sim->recipe.inventory.provider; options->movement_provider = sim->recipe.movement.provider;
    options->physics_edition = qa_app_shared_sim_q1_physics_edition(sim); options->deathmatch = 0;
    options->coop = qa_ss_is(sim->options.mode, "coop"); options->gravity = qa_app_sim_shared_physics_gravity(sim->physics);
    options->precache_id1 = true;
    qa_q1_services *game = calloc(1, sizeof *game);
    if (!game || !qa_q1_services_init(game, qa_app_shared_sim_q1_actor_host(sim, reference, runtime, NULL), *options)) qa_range_error("Out of memory");
    source->base.game = game;
    if (qa_ss_is(registered->program, "mg3") && sim->options.restore == NULL) game->world = qa_q1_create(game, "worldspawn", NULL, NULL);
    if (!qa_ss_is(registered->program, "id1")) {
      qa_q1ad_services addon; memset(&addon, 0, sizeof addon);
      addon.ctx = source; addon.emit = wl_ms_emit; addon.is_monster = wl_ms_is_monster; addon.cvar = wl_ms_cvar; addon.set_cvar = wl_ms_set_cvar;
      source->expansion = qa_app_sim_register_selected_q1_expansion(game, registered->program, addon);
      source->base.addon = source->expansion->context;
    } else {
      qa_q1_monster_services services; memset(&services, 0, sizeof services);
      services.ctx = source; services.count_monster_kill = wl_creature_kill; services.finale = wl_creature_finale; services.finish_finale = wl_creature_finale;
      source->creatures = qa_q1_creatures_create(game, services);
      if (!source->creatures) qa_range_error("Out of memory");
      qa_q1_base_monster_species *species = wl_alloc(QA_Q1_BASE_SPECIES_COUNT, sizeof *species); size_t species_count = 0;
      for (size_t i = 0; i < QA_Q1_BASE_SPECIES_COUNT; i++) {
        bool all = true;
        for (size_t j = 0; j < QA_Q1_BASE_SPECIES[i].classname_count && all; j++)
          all = qa_monster_source_creature(registered, QA_Q1_BASE_SPECIES[i].classnames[j]) != NULL;
        if (all) species[species_count++] = QA_Q1_BASE_SPECIES[i];
      }
      qa_q1_creatures_register_species(source->creatures, species, species_count);
      free(species);
      source->creatures_extension = (qa_q1_state_extension){source, "q1:selected-creatures", wl_creatures_capture, wl_creatures_restore, wl_creatures_clone};
      qa_q1_register_state_extension(game, &source->creatures_extension);
    }
    source->pickups = wl_selected_creature_pickups(sim);
    game->pickup_admission = &source->pickups->contract;
  } else {
    source->base.kind = "q2";
    source->q2_host = qa_app_shared_sim_q2_actor_host(sim, reference, runtime, wl_ms_monster, source, NULL);
    source->q2_options = wl_q2_common(sim, reference.provider, source->q2_rerelease ? QA_Q2_EDITION_RERELEASE : QA_Q2_EDITION_CLASSIC);
    source->q2_options.campaign = wl_campaign_provider(sim);
    qa_app_sim_q2_monster_options options; memset(&options, 0, sizeof options);
    options.edition = source->q2_options.edition; options.program = registered->program;
    options.behavior.ctx = source; options.behavior.mission = wl_ms_mission;
    options.weapons.ctx = source; options.weapons.emit = wl_ms_q2_emit; options.weapons.dodge = wl_ms_q2_dodge;
    options.weapons.ammo_changed = wl_ms_q2_ammo_changed; options.weapons.lag_is_history = false; options.weapons.can_target = wl_can_target;
    options.ctx = source; options.create_game = wl_ms_create_game; options.gravity = wl_ms_gravity; options.powerups = wl_ms_powerups;
    options.player_effect = wl_ms_player_effect;
    source->base.q2 = qa_app_sim_create_selected_q2_monster_modules(options);
  }
  qa_ss_map_set(&sim->monster_sources, &reference.provider, &stored);
  return stored;
}

/* ---- attachMonster (1362-1375) ---- */
void qa_app_shared_sim_attach_monster(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_monster_definition_reference definition,
    const qa_monster_mission *mission) {
  qa_app_sim_selected_monster_source *source = qa_app_shared_sim_monster_source_for(sim, definition);
  qa_ss_map_set(&sim->monster_missions, &actor.id, mission);
  if (qa_ss_is(source->kind, "q1")) {
    qa_q1_set_mission(source->game, actor.id, mission);
    qa_q1_actor *entity = qa_q1_attach_existing(source->game, actor, definition.classname, NULL, NULL);
    entity->spawnflags = mission->ambush ? 1 : 0;
    double deathmatch = 0;
    qa_q1_spawn_entity_context(source->game, entity, &deathmatch);
    return;
  }
  qa_q2_spawn_fields fields; memset(&fields, 0, sizeof fields);
  fields.ordinal = -1; fields.classname = definition.classname;
  qa_q2_entity *entity = qa_q2_entity_services_attach(source->q2->game, actor, fields);
  entity->spawnflags = mission->ambush ? 1 : 0;
  qa_q2_entity_services_spawn_entity(source->q2->game, entity);
}


/* ---- prepareSelectedMonsters / prepareGuestMonsters (1377-1681) ---- */
typedef struct wl_selected_behavior {
  qa_app_shared_simulation *sim;
  bool guest;
  qa_app_sim_monster_map map;
  qa_q2m_monsters *authored_monsters;  /* NULL = TS null */
  qa_q2_mover_module *authored_movers; /* NULL = TS null */
  const char *edition;                 /* guest: the plan edition */
  qa_app_sim_selected_q2_monster_modules *modules; /* guest Q2 adjunct; NULL = TS null */
  qa_q1_services *adjunct_q1;          /* guest Q1 adjunct (owned) */
  qa_q2_foundation_host adjunct_host; qa_q2_game_options adjunct_options;
  qa_provider_timing timing;
  qa_q1_options adjunct_q1_options;
  qa_q1_monster_admission q1_admission; qa_q2_monster_admission q2_admission;
  qa_q1_authored_path_follower q1_follower;
} wl_selected_behavior;
/* One placement attempt's closure state. */
typedef struct wl_placement {
  wl_selected_behavior *behavior;
  qa_app_sim_selected_monster_source *source;
  double original_medium;
  qa_actor_id *blockers; size_t blocker_count, blocker_capacity;
  const qa_actor_id *excluded; size_t excluded_count;
} wl_placement;
static double wl_medium(qa_app_sim_selected_monster_source *source, qa_vec3 origin) {
  if (qa_ss_is(source->kind, "q2")) {
    qa_q2_game_services *game = qa_q2_entity_services_game(source->q2->game);
    return qa_js_to_int32(game->host->point_contents(game->host->ctx, origin)) & 56;
  }
  qa_q1_contents contents = source->game->host.contents(source->game->host.ctx, origin);
  return qa_ss_is(contents, "water") ? 32 : qa_ss_is(contents, "slime") ? 16 : qa_ss_is(contents, "lava") ? 8 : 0;
}
static bool wl_same_medium(void *ctx, qa_vec3 origin) { wl_placement *placement = ctx; return wl_medium(placement->source, origin) == placement->original_medium; }
static qa_trace_result wl_placement_trace(void *ctx, qa_trace_query query) {
  wl_placement *placement = ctx;
  return qa_shared_trace_excluding(placement->behavior->sim->scene, query, placement->excluded, placement->excluded_count);
}
static void wl_placement_blocked(void *ctx, qa_actor_id actor) {
  wl_placement *placement = ctx;
  if (wl_includes(placement->blockers, placement->blocker_count, actor)) return;
  placement->blockers = wl_grow(placement->blockers, &placement->blocker_capacity, placement->blocker_count + 1, sizeof *placement->blockers);
  placement->blockers[placement->blocker_count++] = actor;
}
static void wl_blocked_ignored(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; } /* blockedBy: () => undefined */
static qa_trace_policy wl_monster_policy(const qa_app_sim_selected_monster_source *source) {
  qa_trace_policy policy; memset(&policy, 0, sizeof policy);
  if (qa_ss_is(source->kind, "q1")) { policy.kind = "q1"; policy.move = "normal"; policy.has_hull = false; }
  else { policy.kind = "q2"; policy.contents_mask = 1; policy.leaf_contents = "merged"; }
  return policy;
}
static const qa_q1_base_monster_species *wl_species_for(const char *classname) {
  for (size_t i = 0; i < QA_Q1_BASE_SPECIES_COUNT; i++)
    for (size_t j = 0; j < QA_Q1_BASE_SPECIES[i].classname_count; j++)
      if (qa_ss_is(QA_Q1_BASE_SPECIES[i].classnames[j], classname)) return &QA_Q1_BASE_SPECIES[i];
  return NULL;
}
static const char *wl_q1_property(const qa_q1_entity *entity, const char *key) {
  for (size_t i = 0; i < entity->property_count; i++) if (qa_ss_is(entity->properties[i].key, key)) return entity->properties[i].value;
  return NULL;
}
static const char *wl_locomotion_name(const char *movement) {
  return qa_ss_is(movement, "fly") || qa_ss_is(movement, "swim") ? movement : "walk";
}
static noreturn void wl_placement_obstructed(qa_app_shared_simulation *sim, qa_authored_monster *entry, qa_monster_definition_reference definition) {
  char ordinal[40];
  wl_fail("Selected monster placement obstructed in %s: source %s %s -> %s/%s", sim->recipe.map.geometry.requested_path,
    qa_ss_number_text(entry->source_ordinal, ordinal), entry->base.classname, definition.source.provider, definition.classname);
}
static qa_app_sim_nearby_placement wl_search(wl_placement *placement, qa_body_state body, const char *locomotion, const qa_actor_id *world,
    qa_vec3 origin, qa_bounds bounds, const char *native_locomotion, qa_trace_query query, bool record_blockers,
    const qa_actor_id *excluded, size_t excluded_count) {
  placement->excluded = excluded; placement->excluded_count = excluded_count;
  qa_app_sim_nearby_placement_input input; memset(&input, 0, sizeof input);
  input.body = body; input.locomotion = locomotion; input.world_actor = world;
  input.authored.origin = origin; input.authored.bounds = bounds; input.authored.locomotion = native_locomotion;
  input.query = query; input.ctx = placement; input.same_medium = wl_same_medium; input.trace = wl_placement_trace;
  input.blocked_by = record_blockers ? wl_placement_blocked : wl_blocked_ignored;
  return qa_app_sim_nearby_monster_placement(&input);
}
static qa_app_sim_nearby_placement wl_no_placement(void) { qa_app_sim_nearby_placement none; memset(&none, 0, sizeof none); return none; }
static void wl_apply_placement(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_body_state body, qa_app_sim_nearby_placement found) {
  qa_actor_id ground = found.ground;
  body.origin = found.origin; body.ground = found.has_ground ? &ground : NULL;
  qa_shared_body_table_write(wl_bodies(sim), actor, body);
}
static bool wl_validate_placement(void *ctx, qa_authored_monster *entry, qa_monster_definition_reference definition, const qa_vec3 *relocated) {
  wl_selected_behavior *behavior = ctx; qa_app_shared_simulation *sim = behavior->sim;
  qa_owned_actor actor = entry->base.actor;
  qa_body_state body;
  if (!qa_app_shared_sim_body(sim, actor.id, &body)) wl_fail("Started selected monster has no shared body");
  qa_app_sim_selected_monster_source *source = qa_app_shared_sim_monster_source_for(sim, definition);
  qa_trace_query query; memset(&query, 0, sizeof query);
  query.start = body.origin; query.end = body.origin; query.shape = (qa_trace_shape){"box", body.bounds};
  query.target.is_model = false; query.pass_actor = &actor.id;
  query.numeric = qa_app_sim_provider_timing(sim->recipe, definition.source.provider).numeric;
  query.policy = wl_monster_policy(source);
  qa_trace_result trace = qa_shared_geometry_trace(sim->scene, query);
  if (!trace.start_solid && !trace.all_solid) return true;
  qa_actor_id world; bool has_world = qa_app_shared_sim_world_actor(sim, &world);
  wl_placement placement = {behavior, source, wl_medium(source, body.origin), NULL, 0, 0, NULL, 0};
  if (behavior->guest) {
    const qa_q2m_definition *native = behavior->modules ? qa_q2m_monsters_definition(&behavior->modules->monsters, entry->base.classname,
        qa_q2_entity_services_game(behavior->modules->game)) : NULL;
    const qa_q1_base_monster_species *species = behavior->modules ? NULL : wl_species_for(entry->base.classname);
    qa_bounds native_bounds = native ? native->bounds : species ? species->bounds : body.bounds;
    const char *native_movement = native && native->has_locomotion ? qa_q2m_locomotion_name(native->locomotion) : species ? species->movement : NULL;
    qa_q2_spawn_fields *authored = NULL; size_t authored_count = 0;
    if (!qa_q2_parse_entities(sim->world_entities, qa_ss_is(behavior->edition, "rerelease") ? QA_Q2_EDITION_RERELEASE : QA_Q2_EDITION_CLASSIC,
        &authored, &authored_count)) qa_range_error("Out of memory");
    size_t ordinal = (size_t)entry->source_ordinal;
    const char *position = ordinal < authored_count ? qa_q2_spawn_get(&authored[ordinal], "origin") : NULL;
    qa_app_sim_nearby_placement found = wl_no_placement();
    if (position) found = wl_search(&placement, body, "walk", has_world ? &world : NULL, relocated ? *relocated : qa_q1_parse_vector(position),
        native_bounds, wl_locomotion_name(native_movement), query, false, NULL, 0);
    qa_q2_entities_free(authored, authored_count);
    if (!found.found) wl_placement_obstructed(sim, entry, definition);
    wl_apply_placement(sim, actor, body, found);
    qa_shared_body_table_link(wl_bodies(sim), actor, NULL);
    return true;
  }
  qa_app_sim_monster_map map = behavior->map;
  bool map_q1 = qa_ss_is(map.kind, "q1");
  if (map_q1 && qa_app_sim_is_q1_teleport_staging(map.q1, body)) {
    entry->placement.kind = "teleport"; entry->placement.origin = body.origin;
    return true;
  }
  qa_q1_actor *entity = qa_ss_is(source->kind, "q1") ? qa_q1_get_entity(source->game, &actor.id) : NULL;
  qa_q2_game_services *source_game = qa_ss_is(source->kind, "q2") ? qa_q2_entity_services_game(source->q2->game) : NULL;
  qa_q2_entity *q2_entity = source_game ? qa_q2_game_entity_of(source_game, &actor.id) : NULL;
  qa_q2_game_services *map_game = map_q1 ? NULL : qa_q2_entity_services_game(map.q2);
  qa_q2_spawn_fields *authored = NULL; size_t authored_count = 0;
  if (!map_q1 && qa_ss_is(sim->world_kind, "q2-bsp") &&
      !qa_q2_parse_entities(sim->world_entities, map_game->options.edition, &authored, &authored_count)) qa_range_error("Out of memory");
  size_t ordinal = (size_t)entry->source_ordinal;
  const qa_q2_spawn_fields *authored_q2 = ordinal < authored_count ? &authored[ordinal] : NULL;
  const qa_q2m_definition *native = !map_q1 && behavior->authored_monsters ? qa_q2m_monsters_definition(behavior->authored_monsters, entry->base.classname, map_game) : NULL;
  bool preserved_q2 = source_game && !map_q1 && qa_ss_is(sim->world_kind, "q2-bsp") && q2_entity
    && qa_app_sim_preserves_authored_q2_placement((qa_app_sim_q2_placement_input){sim->recipe.map, authored_q2, definition, native, source_game, q2_entity, body});
  const qa_q1_entity *authored_q1 = sim->q1_map && qa_ss_is(sim->world_kind, "q1-bsp") && ordinal < sim->q1_map->entity_count ? &sim->q1_map->entity_list[ordinal] : NULL;
  bool preserved = qa_ss_is(source->kind, "q1") && qa_ss_is(sim->world_kind, "q1-bsp") && entity
    && qa_app_sim_preserves_authored_q1_placement((qa_app_sim_q1_placement_input){sim->recipe.map, authored_q1, definition, source->game, entity, body});
  if (preserved || preserved_q2) { qa_q2_entities_free(authored, authored_count); return true; }
  /* Classes without a species/definition record (army, dog) fall back to the selected hull. */
  const qa_q1_base_monster_species *species = map_q1 ? wl_species_for(entry->base.classname) : NULL;
  qa_bounds native_bounds = !map_q1 ? (native ? native->bounds : body.bounds) : (species ? species->bounds : body.bounds);
  const char *native_movement = !map_q1 ? (native && native->has_locomotion ? qa_q2m_locomotion_name(native->locomotion) : NULL) : species ? species->movement : NULL;
  const char *position = !map_q1 ? (authored_q2 ? qa_q2_spawn_get(authored_q2, "origin") : NULL)
    : qa_ss_is(sim->world_kind, "q1-bsp") && authored_q1 ? wl_q1_property(authored_q1, "origin") : NULL;
  double flags = entity ? entity->movement_flags : q2_entity ? q2_entity->flags : 0;
  int32_t bits = qa_js_to_int32(flags);
  const char *locomotion = (bits & 2) != 0 ? "swim" : (bits & 1) != 0 ? "fly" : "walk";
  qa_vec3 authored_origin = relocated ? *relocated : position ? qa_q1_parse_vector(position) : qa_ss_zero;
  qa_app_sim_nearby_placement found = wl_no_placement();
  if (position) found = wl_search(&placement, body, locomotion, has_world ? &world : NULL, authored_origin, native_bounds,
      wl_locomotion_name(native_movement), query, true, NULL, 0);
  if (!found.found) {
    /* Doors that block the search and will open (locked or moving) make the monster wait. */
    qa_q2_entity **candidates = NULL; size_t candidate_count = 0;
    if (!map_q1 && behavior->authored_movers && entry->base.targetname && entry->base.targetname[0] != '\0') {
      size_t target_count = 0;
      qa_q2_entity *const *targets = map_game->targets(map_game->ctx, entry->base.targetname, &target_count);
      candidates = wl_alloc(target_count, sizeof *candidates);
      for (size_t i = 0; i < target_count; i++) {
        qa_q2_traversal state = qa_q2_mover_module_traversal(behavior->authored_movers, targets[i]);
        if (qa_ss_is(targets[i]->classname, "func_door") && (state.locked || state.has_destination)) candidates[candidate_count++] = targets[i];
      }
    }
    qa_actor_id *doors = wl_alloc(candidate_count, sizeof *doors); size_t door_count = 0;
    for (size_t i = 0; i < candidate_count; i++)
      if (wl_includes(placement.blockers, placement.blocker_count, candidates[i]->actor.id)) doors[door_count++] = candidates[i]->actor.id;
    qa_app_sim_nearby_placement future = wl_no_placement();
    while (door_count > 0) {
      future = position ? wl_search(&placement, body, locomotion, has_world ? &world : NULL, authored_origin, native_bounds,
          wl_locomotion_name(native_movement), query, true, doors, door_count) : wl_no_placement();
      if (future.found) break;
      size_t next_count = 0;
      qa_actor_id *next = wl_alloc(candidate_count, sizeof *next);
      for (size_t i = 0; i < candidate_count; i++)
        if (wl_includes(placement.blockers, placement.blocker_count, candidates[i]->actor.id)) next[next_count++] = candidates[i]->actor.id;
      if (next_count == door_count) { free(next); break; }
      free(doors); doors = next; door_count = next_count;
    }
    if (!map_q1 && future.found) {
      qa_authored_monster_barrier *barriers = wl_alloc(door_count, sizeof *barriers);
      for (size_t i = 0; i < door_count; i++) {
        qa_owned_actor door; qa_session_actor_registry_resolve_owned(sim->actors, doors[i], &door);
        barriers[i] = (qa_authored_monster_barrier){doors[i], map_game->body(map_game->ctx, door).origin};
      }
      qa_app_sim_authored_monster_set_waiting(entry, barriers, door_count);
      free(barriers); free(doors); free(candidates); free(placement.blockers); qa_q2_entities_free(authored, authored_count);
      qa_combat_trait_changes traits; memset(&traits, 0, sizeof traits);
      traits.has_can_take_damage = true; traits.can_take_damage = false;
      qa_gameplay_authority_set_traits(sim->combat, actor, traits);
      qa_shared_body_table_link(wl_bodies(sim), actor, NULL);
      return false;
    }
    free(doors); free(candidates); free(placement.blockers); qa_q2_entities_free(authored, authored_count);
    wl_placement_obstructed(sim, entry, definition);
  }
  free(placement.blockers); qa_q2_entities_free(authored, authored_count);
  wl_apply_placement(sim, actor, body, found);
  if (entity && qa_ss_is(locomotion, "walk")) entity->movement_flags = qa_js_bit_or(entity->movement_flags, 512);
  qa_shared_body_table_link(wl_bodies(sim), actor, NULL);
  return true;
}
static bool wl_placement_ready(void *ctx, qa_authored_monster *entry) {
  wl_selected_behavior *behavior = ctx; qa_app_shared_simulation *sim = behavior->sim;
  if (behavior->guest) return false;
  if (qa_ss_is(entry->placement.kind, "teleport")) {
    qa_body_state body;
    return qa_ss_is(behavior->map.kind, "q1") && qa_app_shared_sim_body(sim, entry->base.actor.id, &body) && !qa_app_sim_is_q1_teleport_staging(behavior->map.q1, body);
  }
  if (!qa_ss_is(entry->placement.kind, "waiting") || !qa_ss_is(behavior->map.kind, "q2") || behavior->authored_movers == NULL) return false;
  qa_q2_game_services *game = qa_q2_entity_services_game(behavior->map.q2);
  for (size_t i = 0; i < entry->placement.barrier_count; i++) {
    const qa_authored_monster_barrier *barrier = &entry->placement.barriers[i];
    qa_q2_entity *door = qa_q2_game_entity_of(game, &barrier->actor);
    if (!door) continue;
    qa_q2_traversal traversal = qa_q2_mover_module_traversal(behavior->authored_movers, door);
    qa_vec3 origin = game->body(game->ctx, door->actor).origin;
    if (!(!traversal.locked && !traversal.has_destination && (origin.x != barrier->origin.x || origin.y != barrier->origin.y || origin.z != barrier->origin.z)))
      return false;
  }
  return true;
}
static void wl_resume(void *ctx, qa_actor_id actor, const qa_actor_id *activator) {
  qa_app_shared_simulation *sim = ((wl_selected_behavior *)ctx)->sim;
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor);
  if (!entry || entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3 || entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3_SOURCE || entry->kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC)
    wl_fail("Activated monster has no creature source continuation");
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1) {
    qa_q1_actor *entity = entry->q1_entity; qa_q1_services *services = entry->q1_services;
    qa_q1_bound_action start = entity->think;
    qa_q1_cancel(services, entity);
    if (start.fn) start.fn(start.ctx, services, entity);
    if (qa_session_actor_registry_is_live(sim->actors, actor) && activator && entity->use.fn) entity->use.fn(entity->use.ctx, services, entity, NULL, activator);
  } else {
    qa_q2_entity *entity = entry->q2_entity; qa_q2_game_services *services = qa_q2_entity_services_game(entry->q2_services);
    qa_q2_think start = entity->think;
    services->cancel(services->ctx, entity);
    if (start.fn) start.fn(start.ctx, entity, services);
    if (qa_session_actor_registry_is_live(sim->actors, actor) && activator && entity->use.fn) entity->use.fn(entity->use.ctx, entity, services, NULL, activator);
  }
}
static bool wl_enemy(void *ctx, qa_actor_id actor, qa_actor_id *out) {
  qa_app_shared_simulation *sim = ((wl_selected_behavior *)ctx)->sim;
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor);
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1) {
    if (!entry->q1_entity->monster || !entry->q1_entity->monster->has_enemy) return false;
    *out = entry->q1_entity->monster->enemy; return true;
  }
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2 && entry->q2_entity->has_enemy) { *out = entry->q2_entity->enemy; return true; }
  return false;
}
static bool wl_old_enemy(void *ctx, qa_actor_id actor, qa_actor_id *out) {
  qa_app_shared_simulation *sim = ((wl_selected_behavior *)ctx)->sim;
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor);
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1) {
    if (!entry->q1_entity->monster || !entry->q1_entity->monster->has_old_enemy) return false;
    *out = entry->q1_entity->monster->old_enemy; return true;
  }
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2 && entry->read_monster) {
    qa_q2m_state *state = entry->read_monster(entry->ctx);
    if (state && state->has_old_enemy) { *out = state->old_enemy; return true; }
  }
  return false;
}
static void wl_set_route(void *ctx, qa_actor_id actor, const qa_actor_id *goal, double pause_until) {
  qa_app_shared_simulation *sim = ((wl_selected_behavior *)ctx)->sim;
  qa_monster_definition_reference definition;
  if (!sim->selected_monsters || !qa_app_sim_selected_monsters_definition(sim->selected_monsters, actor, &definition))
    wl_fail("Selected route actor has no creature definition");
  qa_app_sim_selected_monster_source *source = qa_app_shared_sim_monster_source_for(sim, definition);
  if (qa_ss_is(source->kind, "q2")) { qa_q2m_monsters_set_route(&source->q2->monsters, actor, goal, pause_until); return; }
  qa_q1_actor *entity = qa_q1_get_entity(source->game, &actor);
  if (!entity) wl_fail("Selected Q1 route actor has no source continuation");
  qa_q1_set_monster_route(source->game, entity, goal, pause_until);
}
static void wl_attach(void *ctx, qa_owned_actor actor, qa_monster_definition_reference definition, const qa_monster_mission *mission) {
  qa_app_shared_sim_attach_monster(((wl_selected_behavior *)ctx)->sim, actor, definition, mission);
}
static qa_app_sim_selected_monster_behavior wl_behavior(wl_selected_behavior *record) {
  return (qa_app_sim_selected_monster_behavior){record, wl_attach, wl_validate_placement, wl_placement_ready, wl_enemy, wl_old_enemy,
    wl_set_route, wl_resume};
}
static void wl_prepare_definitions(qa_app_shared_simulation *sim) {
  size_t count = 0;
  qa_monster_definition_reference *definitions = qa_selected_monster_definitions(sim->recipe.enemies, &count);
  for (size_t i = 0; i < count; i++) qa_app_shared_sim_monster_source_for(sim, definitions[i]);
  free(definitions);
}
/* Q1 map admission and path-follower closures. */
static bool wl_q1_resolve(void *ctx, const char *classname, const qa_q1_entity *source, const char **provider, const char **out_classname) {
  qa_app_shared_simulation *sim = ((wl_selected_behavior *)ctx)->sim;
  qa_monster_definition_reference definition;
  qa_app_sim_monster_fields fields = {source, NULL};
  if (!qa_app_sim_selected_monsters_resolve(sim->selected_monsters, classname, fields, &definition)) return false;
  *provider = definition.source.provider; *out_classname = definition.classname;
  return true;
}
static void wl_q1_spawn(void *ctx, qa_owned_actor actor, const qa_q1_entity *source, double ordinal, const char *provider, const char *classname) {
  qa_app_shared_simulation *sim = ((wl_selected_behavior *)ctx)->sim;
  qa_monster_definition_reference definition;
  qa_app_sim_monster_fields fields = {source, NULL};
  if (!qa_app_sim_selected_monsters_resolve(sim->selected_monsters, classname, fields, &definition) || !qa_ss_is(definition.source.provider, provider)) {
    /* The admission hands back what resolve returned: rebuild its reference. */
    memset(&definition, 0, sizeof definition);
    definition.classname = classname; definition.source.provider = provider;
  }
  qa_app_sim_selected_monsters_admit_q1(sim->selected_monsters, actor, source, ordinal, definition);
}
static bool wl_q1_path_follower(void *ctx, qa_actor_id mover, qa_q1_authored_path_follower *out) {
  qa_app_shared_simulation *sim = ((wl_selected_behavior *)ctx)->sim;
  qa_app_sim_q1_path_follower follower;
  if (!qa_app_sim_selected_monsters_q1_path_follower(sim->selected_monsters, mover, &follower)) return false;
  *out = (qa_q1_authored_path_follower){follower.targetname, follower.has_enemy, follower.enemy, follower.ctx, follower.advance};
  return true;
}
static bool wl_q2_resolve(void *ctx, const char *classname, const qa_q2_spawn_fields *source, qa_monster_definition_reference *out) {
  qa_app_shared_simulation *sim = ((wl_selected_behavior *)ctx)->sim;
  qa_app_sim_monster_fields fields = {NULL, source};
  return qa_app_sim_selected_monsters_resolve(sim->selected_monsters, classname, fields, out);
}
static void wl_q2_spawn(void *ctx, qa_owned_actor actor, const qa_q2_spawn_fields *source, qa_monster_definition_reference definition) {
  qa_app_sim_selected_monsters_admit_q2(((wl_selected_behavior *)ctx)->sim->selected_monsters, actor, source, definition);
}
static const qa_q2m_path_follower *wl_q2_path(void *ctx, qa_actor_id actor) {
  return qa_app_sim_selected_monsters_q2_path_follower(((wl_selected_behavior *)ctx)->sim->selected_monsters, actor);
}
static const qa_q2m_combat_follower *wl_q2_combat(void *ctx, qa_actor_id actor) {
  return qa_app_sim_selected_monsters_q2_combat_follower(((wl_selected_behavior *)ctx)->sim->selected_monsters, actor);
}
void qa_app_shared_sim_prepare_selected_monsters(qa_app_shared_simulation *sim) {
  const qa_enemy_selection *selection = &sim->recipe.enemies;
  if (selection->kind != QA_ENEMY_SELECTION_REPLACE) return;
  if (sim->selected_monsters != NULL) wl_fail("Selected monster admission is already prepared");
  if ((sim->strategy->as_q1(sim) == NULL) && (sim->strategy->as_q2(sim) == NULL))
    wl_fail("Selected monster map admission currently requires a Q1 or Q2 authored map");
  /* On a foreign map the authored-family adjunct foundation is the map program. */
  wl_selected_behavior *behavior = qa_app_shared_sim_keep(sim, sizeof *behavior);
  behavior->sim = sim; behavior->guest = false;
  qa_app_sim_monster_map map; memset(&map, 0, sizeof map);
  if (sim->has_foreign_q1_world) { map.kind = "q1"; map.q1 = sim->foreign_q1_world.modules->game; }
  else if (sim->has_foreign_world) { map.kind = "q2"; map.q2 = sim->foreign_world.modules->game; map.items = sim->foreign_world.modules->items; }
  else if ((sim->strategy->as_q1(sim) != NULL)) { map.kind = "q1"; map.q1 = sim->source.q1_game; }
  else { map.kind = "q2"; map.q2 = sim->source.product->game; map.items = sim->source.product->items; }
  behavior->map = map;
  behavior->authored_monsters = sim->has_foreign_world ? &sim->foreign_world.modules->monsters : (sim->strategy->as_q2(sim) != NULL) ? &sim->source.product->monsters : NULL;
  behavior->authored_movers = sim->has_foreign_world ? sim->foreign_world.modules->movers : (sim->strategy->as_q2(sim) != NULL) ? sim->source.product->movers : NULL;
  qa_app_sim_selected_monsters *selected = qa_app_sim_selected_monsters_create(selection, map, wl_behavior(behavior));
  if (!selected) qa_range_error("Out of memory");
  sim->selected_monsters = selected; /* the closures below resolve it through the simulation */
  wl_prepare_definitions(sim);
  if (qa_ss_is(map.kind, "q1")) {
    behavior->q1_admission = (qa_q1_monster_admission){behavior, wl_q1_resolve, wl_q1_spawn};
    qa_q1_foundation_set_monster_admission(map.q1, &behavior->q1_admission);
    map.q1->authored_path_follower.ctx = behavior; map.q1->authored_path_follower.fn = wl_q1_path_follower;
  } else {
    behavior->q2_admission = (qa_q2_monster_admission){behavior, wl_q2_resolve, wl_q2_spawn};
    qa_q2_foundation_set_monster_admission(map.q2, &behavior->q2_admission);
    if (behavior->authored_monsters == NULL) wl_fail("Selected monster admission lost its authored Q2 monster modules");
    behavior->authored_monsters->external_path = (qa_q2m_follower_lookup){behavior, wl_q2_path, NULL};
    behavior->authored_monsters->external_combat = (qa_q2m_follower_lookup){behavior, NULL, wl_q2_combat};
  }
}
/* Guest Q2 adjunct hooks. */
static void wl_gm_emit(void *ctx, qa_q2w_event event) { qa_app_shared_simulation *sim = ((wl_selected_behavior *)ctx)->sim; qa_app_shared_sim_weapon_event(sim, sim->recipe.map.entities.content, &event); }
static void wl_gm_dodge(void *ctx, qa_actor_id monster, qa_actor_id attacker, double eta, qa_trace_result trace) {
  qa_app_shared_sim_q2_monster_dodge(((wl_selected_behavior *)ctx)->sim, monster, &attacker, eta, &trace);
}
static void wl_gm_ammo_changed(void *ctx, qa_actor_id actor, qa_item_id ammo) {
  (void)ammo; /* TS ammoChanged: actor => ... */
  wl_q2_inventory_message(((wl_selected_behavior *)ctx)->sim, actor);
}
static const qa_monster_mission *wl_gm_mission(void *ctx, qa_actor_id actor) { return qa_ss_map_get(&((wl_selected_behavior *)ctx)->sim->monster_missions, &actor); }
static double wl_gm_gravity(void *ctx) { return qa_app_sim_shared_physics_gravity(((wl_selected_behavior *)ctx)->sim->physics); }
static qa_q2mpm_powerups wl_gm_powerups(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; return (qa_q2mpm_powerups){0, 0, 0}; } /* () => none */
static void wl_gm_player_effect(void *ctx, qa_q2mp_player_effect event) {
  qa_app_shared_simulation *sim = ((wl_selected_behavior *)ctx)->sim;
  wl_emit_missionpack_player(sim, sim->recipe.map.entities.content, &event, sim->source_frame.time);
}
static qa_q2m_state *wl_gm_monster(void *ctx, qa_actor_id actor) {
  wl_selected_behavior *behavior = ctx;
  if (!behavior->modules) return NULL;
  qa_q2m_context *context = qa_q2m_monsters_context(&behavior->modules->monsters, actor);
  return context ? context->state : NULL;
}
static qa_q2_entity_services *wl_gm_create_game(void *ctx, qa_q2m_monsters *monsters, const qa_q2_spawn_module *modules, size_t module_count) {
  (void)monsters; /* read through wl_gm_monster */
  wl_selected_behavior *behavior = ctx;
  return qa_q2_entity_services_create(&behavior->adjunct_host, behavior->adjunct_options, modules, module_count);
}
void qa_app_shared_sim_prepare_guest_monsters(qa_app_shared_simulation *sim) {
  const qa_enemy_selection *selection = &sim->recipe.enemies;
  if (selection->kind != QA_ENEMY_SELECTION_REPLACE || !sim->strategy->admits_guest_monsters(sim)) return;
  if (sim->selected_monsters != NULL) wl_fail("Selected monster admission is already prepared");
  qa_provider_reference reference = sim->recipe.map.entities;
  const char *edition = sim->strategy->guest_edition(sim) != NULL ? sim->strategy->guest_edition(sim) : qa_app_shared_sim_guest_map_edition(sim);
  /* The adjunct family follows the map's authored family; Q3 maps use the selection's family. */
  const qa_monster_selection_target *first = selection->default_target.is_map_defined ? NULL : &selection->default_target;
  bool all_map_defined = selection->default_target.is_map_defined;
  for (size_t i = 0; i < selection->by_classname_count; i++) {
    if (!first && !selection->by_classname[i].target.is_map_defined) first = &selection->by_classname[i].target;
    if (!selection->by_classname[i].target.is_map_defined) all_map_defined = false;
  }
  const char *family = qa_ss_is(sim->world_kind, "q1-bsp") ? "q1" : qa_ss_is(sim->world_kind, "q2-bsp") ? "q2"
    : qa_app_sim_provider_family(first ? first->reference.source.provider : reference.provider) == QA_GAME_FAMILY_Q1 ? "q1" : "q2";
  if (qa_ss_is(sim->world_kind, "q3-bsp") && all_map_defined) return;
  bool rerelease = qa_ss_is(edition, "rerelease");
  wl_selected_behavior *behavior = qa_app_shared_sim_keep(sim, sizeof *behavior);
  behavior->sim = sim; behavior->guest = true; behavior->edition = edition;
  behavior->timing = qa_app_sim_provider_timing(sim->recipe, reference.provider);
  qa_app_sim_source_random *random = qa_app_sim_source_random_create(sim->options.seed, rerelease ? "q2-rerelease" : "classic");
  qa_app_sim_actor_host_runtime runtime = wl_make_runtime(sim, behavior->timing.numeric, random, &sim->source_frame, rerelease ? 0.025 : 0.1, true);
  qa_app_sim_monster_map map; memset(&map, 0, sizeof map);
  if (qa_ss_is(family, "q1")) {
    qa_q1_options *options = &behavior->adjunct_q1_options;
    options->provider = reference.provider; options->edition = edition; options->skill = sim->options.skill;
    options->max_clients = sim->options.max_clients; options->has_max_clients = true; options->campaign = wl_campaign_provider(sim);
    options->combat_provider = sim->recipe.combat.provider; options->inventory_provider = sim->recipe.inventory.provider;
    options->movement_provider = sim->recipe.movement.provider; options->physics_edition = qa_app_shared_sim_q1_physics_edition(sim);
    options->deathmatch = 0; options->coop = qa_ss_is(sim->options.mode, "coop"); options->gravity = qa_app_sim_shared_physics_gravity(sim->physics);
    options->precache_id1 = true;
    behavior->adjunct_q1 = calloc(1, sizeof *behavior->adjunct_q1);
    if (!behavior->adjunct_q1 || !qa_q1_services_init(behavior->adjunct_q1, qa_app_shared_sim_q1_actor_host(sim, reference, runtime, NULL), *options))
      qa_range_error("Out of memory");
    map.kind = "q1"; map.q1 = behavior->adjunct_q1;
  } else {
    behavior->adjunct_host = qa_app_shared_sim_q2_actor_host(sim, reference, runtime, wl_gm_monster, behavior, NULL);
    behavior->adjunct_options = wl_q2_common(sim, reference.provider, rerelease ? QA_Q2_EDITION_RERELEASE : QA_Q2_EDITION_CLASSIC);
    qa_app_sim_q2_monster_options options; memset(&options, 0, sizeof options);
    options.edition = behavior->adjunct_options.edition; options.program = "baseq2";
    options.behavior.ctx = behavior; options.behavior.mission = wl_gm_mission;
    options.weapons.ctx = behavior; options.weapons.emit = wl_gm_emit; options.weapons.dodge = wl_gm_dodge;
    options.weapons.ammo_changed = wl_gm_ammo_changed; options.weapons.lag_is_history = false; options.weapons.can_target = wl_can_target;
    options.ctx = behavior; options.create_game = wl_gm_create_game; options.gravity = wl_gm_gravity; options.powerups = wl_gm_powerups;
    options.player_effect = wl_gm_player_effect;
    behavior->modules = qa_app_sim_create_selected_q2_monster_modules(options);
    if (behavior->modules->game == NULL) wl_fail("Native monster adjunct game was not created");
    qa_q2_item_hooks hooks; memset(&hooks, 0, sizeof hooks);
    hooks.ctx = behavior; hooks.weapon_picked = wl_item_weapon_picked; hooks.silencer = wl_item_silencer; hooks.power_armor = wl_item_power_armor;
    hooks.foreign_items = qa_composition_q3_foreign_item_definitions;
    map.kind = "q2"; map.q2 = behavior->modules->game; map.items = qa_q2_create_item_module(hooks);
    if (!map.items) qa_range_error("Out of memory");
  }
  behavior->map = map;
  qa_app_sim_selected_monsters *selected = qa_app_sim_selected_monsters_create(selection, map, wl_behavior(behavior));
  if (!selected) qa_range_error("Out of memory");
  sim->selected_monsters = selected;
  wl_prepare_definitions(sim);
  sim->b1_guest_monsters = behavior; sim->b1_guest_random = random;
}

/* ---- admitGuestMonsters / runGuestMonsters / syncGuestMonsterProxies (1686-1777) ---- */
static bool wl_guest_source(qa_app_shared_simulation *sim) { return sim->strategy->admits_guest_monsters(sim); }
typedef struct wl_admit_replacement {
  qa_app_shared_simulation *sim; qa_owned_actor actor; const qa_app_source_monster_proxy *proxy; bool has_proxy_slot; double proxy_slot;
  const qa_app_sim_guest_monster_replacement *replacement;
} wl_admit_replacement;
static void wl_admit_replacement_body(void *ctx) {
  wl_admit_replacement *admit = ctx; qa_app_shared_simulation *sim = admit->sim;
  if (admit->has_proxy_slot && admit->proxy != NULL) admit->proxy->bind(sim, admit->proxy_slot, admit->actor);
  qa_app_sim_selected_monsters_admit_q2(sim->selected_monsters, admit->actor, admit->replacement->fields, admit->replacement->definition);
}
void qa_app_shared_sim_admit_guest_monsters(qa_app_shared_simulation *sim) {
  if (!wl_guest_source(sim) || sim->selected_monsters == NULL || sim->guest_monsters_admitted) return;
  sim->guest_monsters_admitted = true;
  qa_app_source_monster_proxy proxy;
  bool has_proxy = sim->strategy->monster_proxy(sim, &proxy);
  const char *edition = sim->strategy->guest_edition(sim) != NULL ? sim->strategy->guest_edition(sim) : qa_app_shared_sim_guest_map_edition(sim);
  const qa_app_sim_guest_monster_plan *plan = qa_app_shared_sim_guest_monster_plan(sim, sim->world_entities, edition);
  if (!plan) return;
  if (plan->kept_count != 0) {
    /* Admission runs during world load, before presentation cursors exist;
     * the report lands on the first stepped frame. */
    char *reasons = wl_kept_reasons(plan), replaced[40], kept[40];
    free(sim->pending_guest_monster_report);
    sim->pending_guest_monster_report = wl_format("Selected enemies: %s replaced; %s guest-native (%s)\n",
      qa_ss_number_text((double)plan->replacement_count, replaced), qa_ss_number_text((double)plan->kept_count, kept), reasons);
    free(reasons);
  }
  for (size_t i = 0; i < plan->replacement_count; i++) {
    const qa_app_sim_guest_monster_replacement *replacement = &plan->replacements[i];
    qa_monster_definition_reference definition = replacement->definition;
    /* Guests address trace hits by their own entity slots: a classic proxy edict or a QVM proxy entity. */
    double proxy_slot = 0;
    bool has_proxy_slot = has_proxy && proxy.allocate(sim, &proxy_slot);
    if (has_proxy && !has_proxy_slot) wl_fail("Selected monster projection lost its declared combat profile");
    char name[512];
    snprintf(name, sizeof name, "%s/%s", definition.source.provider, definition.classname);
    qa_owned_actor actor = !has_proxy_slot ? qa_session_actor_registry_allocate(sim->actors, sim->recipe.map.entities.provider, name)
      : qa_session_actor_registry_allocate_at_source(sim->actors, sim->recipe.map.entities.provider, (int64_t)proxy_slot, name);
    const char *origin_text = qa_q2_spawn_get(replacement->fields, "origin");
    qa_vec3 origin = qa_q1_parse_vector(origin_text ? origin_text : "0 0 0");
    const char *angle_text = qa_q2_spawn_get(replacement->fields, "angle");
    double angle = angle_text ? qa_js_string_to_number(angle_text) : 0;
    qa_body_state body = {origin, {0, angle, 0}, qa_ss_zero, {qa_ss_zero, qa_ss_zero}, NULL};
    qa_shared_body_table_create_body(wl_bodies(sim), actor, body);
    qa_combat_state combat; memset(&combat, 0, sizeof combat);
    combat.health = 0; combat.armor.regular.kind = QA_REGULAR_ARMOR_NONE; combat.armor.powered.kind = "none";
    combat.mass = 100; combat.can_take_damage = false; combat.invulnerable = false; combat.team = NULL;
    qa_gameplay_authority_create_state(sim->combat, actor, combat, NULL, NULL);
    wl_admit_replacement admit = {sim, actor, has_proxy ? &proxy : NULL, has_proxy_slot, proxy_slot, replacement};
    qa_caught_error caught;
    if (!qa_try(wl_admit_replacement_body, &admit, &caught)) { qa_session_actor_registry_release(sim->actors, actor); qa_rethrow(&caught); }
  }
}
/* syncGuestMonsterProxies(): TS monster bodies mirror into QVM proxy entity fields. */
void qa_app_shared_sim_sync_guest_monster_proxies(qa_app_shared_simulation *sim) {
  if ((sim->strategy->as_q3_qvm(sim) == NULL) || sim->source.qvm_combat == NULL || sim->selected_monsters == NULL) return;
  size_t count = qa_app_sim_selected_monsters_count(sim->selected_monsters);
  for (size_t i = 0; i < count; i++) {
    qa_authored_monster *entry = qa_app_sim_selected_monsters_at(sim->selected_monsters, i);
    double slot; qa_body_state body;
    if (qa_app_q3_guest_records_slot(qa_app_q3_qvm_server_game_records(sim->source.qvm), entry->base.actor.id, &slot)
        && qa_app_shared_sim_body(sim, entry->base.actor.id, &body))
      qa_qvm_combat_bindings_sync_proxy_entity(sim->source.qvm_combat, slot, body);
  }
}
static void wl_turn_position(void *ctx, qa_owned_actor actor, double out[2]) { qa_app_shared_sim_source_position(ctx, actor.id, out); }
static void wl_execute(qa_app_shared_simulation *sim, const qa_app_sim_actor_execution *execution, qa_frame_context frame, qa_app_sim_actor_visited *visited) {
  qa_app_sim_actor_execution entry = *execution;
  qa_app_sim_actor_execution_frame context = {sim->actors, wl_bodies(sim), sim->physics, sim->scheduler, frame,
    qa_ss_seconds(frame.time), qa_ss_seconds(frame.elapsed), visited};
  qa_app_sim_execute_actor(&entry, &context);
  qa_app_sim_shared_physics_commit_attachments(sim->physics);
}
/* One guest frame of selected-monster source time on a native world. */
void qa_app_shared_sim_run_guest_monsters(qa_app_shared_simulation *sim) {
  qa_app_shared_sim_flush_guest_monster_report(sim);
  if (sim->selected_monsters == NULL || !sim->strategy->has_guest_ui(sim)) return;
  /* Selected monsters read player health through shared combat; keep the guest mirror current. */
  if ((sim->strategy->as_q2_native(sim) != NULL)) {
    size_t count = sim->source.clients.count;
    qa_owned_actor *clients = wl_alloc(count, sizeof *clients);
    for (size_t i = 0; i < count; i++) clients[i] = *(qa_owned_actor *)qa_ss_map_value_at(&sim->source.clients, i);
    for (size_t i = 0; i < count; i++) qa_app_shared_sim_sync_native_movement_combat(sim, clients[i]);
    free(clients);
  }
  qa_app_shared_sim_begin_monster_frames(sim, qa_js_round(wl_time(sim) * 1000), true);
  qa_app_sim_actor_visited visited; memset(&visited, 0, sizeof visited);
  qa_app_sim_ordered_actor_turns *turns = qa_app_sim_ordered_actor_turns_create(sim->actors, wl_turn_position, sim);
  qa_owned_actor actor;
  while (qa_app_sim_ordered_actor_turns_next(turns, &visited, &actor)) {
    qa_app_sim_actor_execution *execution = qa_ss_map_get(&sim->actor_executions, &actor.id);
    if (!execution || execution->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3 || execution->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3_SOURCE
        || execution->kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC) continue;
    if (execution->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2) {
      qa_provider_id provider = qa_q2_entity_services_game(execution->q2_services)->options.provider;
      qa_app_sim_selected_monster_source **found = qa_ss_map_get(&sim->monster_sources, &provider);
      qa_app_sim_selected_monster_source *source = found ? *found : NULL;
      if (!source || !qa_ss_is(source->kind, "q2") || execution->q2_services != source->q2->game) continue;
      if (!qa_app_sim_selected_monsters_before_turn(sim->selected_monsters, actor.id)) continue;
      if (source->clock.advanced && qa_session_actor_registry_is_live(sim->actors, actor.id)) wl_execute(sim, execution, source->clock.frame, &visited);
      continue;
    }
    qa_provider_id provider = qa_q1_services_provider(execution->q1_services);
    qa_app_sim_selected_monster_source **found = qa_ss_map_get(&sim->monster_sources, &provider);
    qa_app_sim_selected_monster_source *source = found ? *found : NULL;
    if (!source || source->game != execution->q1_services) continue;
    if (!qa_app_sim_selected_monsters_before_turn(sim->selected_monsters, actor.id) || !qa_session_actor_registry_is_live(sim->actors, actor.id)) continue;
    wl_execute(sim, execution, source->clock.frame, &visited);
  }
  qa_app_sim_ordered_actor_turns_destroy(turns);
  free(visited.actors);
  for (size_t i = 0; i < sim->monster_sources.count; i++) {
    qa_app_sim_selected_monster_source *source = *(qa_app_sim_selected_monster_source **)qa_ss_map_value_at(&sim->monster_sources, i);
    if (qa_ss_is(source->kind, "q2") && source->clock.advanced) qa_q2m_monsters_end_frame(&source->q2->monsters, qa_q2_entity_services_game(source->q2->game));
  }
  qa_app_shared_sim_sync_guest_monster_proxies(sim);
}

/* ---- Foreign Q2 world checkpoint (1835-1867) ---- */
/* SourceRandom.checkpoint() as the contract RandomState; words malloc'd. */
static qa_random_state wl_random_checkpoint(qa_app_sim_source_random *random) {
  qa_app_sim_source_random_state state = qa_app_sim_source_random_checkpoint(random);
  qa_random_state out; memset(&out, 0, sizeof out);
  out.draws = (double)state.draws;
  if (qa_ss_is(state.kind, "q2-rerelease-mt19937")) {
    out.kind = QA_RANDOM_STATE_Q2_RERELEASE_MT19937; out.module = "msvc-2022-17.6";
    double *words = wl_alloc(QA_Q2_RERELEASE_WORDS, sizeof *words);
    for (size_t i = 0; i < QA_Q2_RERELEASE_WORDS; i++) words[i] = state.rerelease.words[i];
    out.words = (qa_double_view){words, QA_Q2_RERELEASE_WORDS};
    out.index = (double)state.rerelease.index; out.draws = (double)state.rerelease.draws;
  } else {
    out.kind = QA_RANDOM_STATE_GLIBC_RANDOM;
    double *words = wl_alloc(31, sizeof *words);
    for (size_t i = 0; i < 31; i++) words[i] = state.words[i];
    out.words = (qa_double_view){words, 31};
    out.front = state.front; out.rear = state.rear;
  }
  return out;
}
/* SourceRandom.restore(state) from the contract RandomState. */
static void wl_random_restore(qa_app_sim_source_random *random, qa_random_state state) {
  qa_app_sim_source_random_state restored; memset(&restored, 0, sizeof restored);
  if (state.kind == QA_RANDOM_STATE_Q2_RERELEASE_MT19937) {
    restored.kind = "q2-rerelease-mt19937";
    for (size_t i = 0; i < state.words.count && i < QA_Q2_RERELEASE_WORDS; i++) restored.rerelease.words[i] = qa_js_to_uint32(state.words.values[i]);
    restored.rerelease.index = (int64_t)state.index; restored.rerelease.draws = (int64_t)state.draws;
  } else {
    restored.kind = "glibc-random";
    for (size_t i = 0; i < state.words.count && i < 31; i++) restored.words[i] = qa_js_to_int32(state.words.values[i]);
    restored.front = (int)state.front; restored.rear = (int)state.rear; restored.draws = (int64_t)state.draws;
  }
  qa_app_sim_source_random_restore(random, restored);
}
/* captureForeignWorld(): false = TS null. The checkpoint's arrays are
 * released with qa_app_shared_sim_foreign_world_checkpoint_free. */
bool qa_app_shared_sim_capture_foreign_world(qa_app_shared_simulation *sim, qa_app_sim_foreign_q2_world_checkpoint *out) {
  if (!sim->has_foreign_world) return false;
  qa_ss_foreign_world *foreign = &sim->foreign_world;
  qa_app_sim_foreign_q2_world_modules *modules = foreign->modules;
  qa_q2_game_services *game = qa_q2_entity_services_game(modules->game);
  memset(out, 0, sizeof *out);
  out->version = 1; out->reference = foreign->reference; out->edition = foreign->edition; out->program = foreign->program;
  out->frame = foreign->clock.frame; out->random = wl_random_checkpoint(foreign->random);
  out->entities = qa_q2_entity_services_capture(modules->game);
  if (!qa_q2m_monsters_capture(&modules->monsters, &out->monsters)) qa_range_error("Out of memory");
  qa_q2w_cause_entry *causes = NULL; size_t cause_count = 0;
  if (!qa_q2w_ballistics_capture_projectiles(&modules->ballistics, &causes, &cause_count)) qa_range_error("Out of memory");
  out->ballistics = (qa_app_sim_ballistics_checkpoint){causes, cause_count};
  out->movers = qa_q2_mover_module_capture(modules->movers, game);
  out->items = qa_q2_item_module_capture(modules->items, game);
  out->base_entities = qa_q2_base_entity_module_capture(modules->base_entities, game);
  out->packs = wl_alloc(modules->pack_count, sizeof *out->packs); out->pack_count = modules->pack_count;
  for (size_t i = 0; i < modules->pack_count; i++) {
    out->packs[i].pack = modules->packs[i].pack;
    if (!qa_q2mpm_monsters_capture(&modules->packs[i].monsters, game, &out->packs[i].monsters)) qa_range_error("Out of memory");
    out->packs[i].steam_id = qa_q2mp_entities_capture(&modules->packs[i].entities).steam_id;
  }
  return true;
}
void qa_app_shared_sim_foreign_world_checkpoint_free(qa_app_sim_foreign_q2_world_checkpoint *checkpoint) {
  free((void *)checkpoint->random.words.values);
  qa_q2m_monsters_checkpoint_free(&checkpoint->monsters);
  qa_q2w_free_cause_entries((qa_q2w_cause_entry *)checkpoint->ballistics.blaster_causes);
  qa_q2_base_entities_module_checkpoint_free(&checkpoint->base_entities);
  for (size_t i = 0; i < checkpoint->pack_count; i++) qa_q2mpm_monsters_checkpoint_free(&checkpoint->packs[i].monsters);
  free(checkpoint->packs);
  memset(checkpoint, 0, sizeof *checkpoint);
}
void qa_app_shared_sim_restore_foreign_world(qa_app_shared_simulation *sim, const qa_app_sim_foreign_q2_world_checkpoint *saved) {
  if (!sim->has_foreign_world) wl_fail("Saved foreign world has no adjunct");
  qa_ss_foreign_world *foreign = &sim->foreign_world;
  if (!qa_ss_is(foreign->reference.provider, saved->reference.provider) || !qa_ss_is(foreign->reference.content, saved->reference.content)
      || !qa_ss_is(foreign->edition, saved->edition) || !qa_ss_is(foreign->program, saved->program))
    wl_fail("Saved foreign world differs from its adjunct");
  foreign->clock.frame = saved->frame; foreign->clock.advanced = false;
  wl_random_restore(foreign->random, saved->random);
  qa_app_sim_foreign_q2_world_modules *modules = foreign->modules;
  qa_q2_game_services *game = qa_q2_entity_services_game(modules->game);
  qa_q2_entity_services_restore(modules->game, saved->entities);
  qa_q2m_monsters_restore(&modules->monsters, game, &saved->monsters);
  qa_q2w_ballistics_restore_projectiles(&modules->ballistics, game, saved->ballistics.blaster_causes, saved->ballistics.blaster_cause_count);
  qa_q2_mover_module_restore(modules->movers, game, saved->movers);
  qa_q2_item_module_restore(modules->items, game, saved->items);
  qa_q2_base_entity_module_restore(modules->base_entities, game, saved->base_entities);
  for (size_t i = 0; i < saved->pack_count; i++) {
    qa_app_sim_foreign_q2_world_pack *pack = NULL;
    for (size_t j = 0; j < modules->pack_count && !pack; j++) if (qa_ss_is(modules->packs[j].pack, saved->packs[i].pack)) pack = &modules->packs[j];
    if (!pack) wl_fail("Saved foreign mission pack is not registered");
    qa_q2mpm_monsters_restore(&pack->monsters, game, &saved->packs[i].monsters);
    qa_q2mp_entities_restore(&pack->entities, (qa_q2mp_rogue_entities_checkpoint){saved->packs[i].steam_id});
  }
}

/* C-only teardown of the B1 owners (qa_app_shared_simulation_destroy calls it
 * after the source runtime, while the actor registry is still alive). */
void qa_app_shared_sim_worlds_destroy(qa_app_shared_simulation *sim) {
  /* bindNativeInput's owners (closed by close(); destroy only frees them). */
  qa_compat_q2_native_primary_drop_destroy(sim->native_primary_drop); sim->native_primary_drop = NULL;
  if (sim->native_primary_inventory != NULL) { qa_compat_q2_native_primary_inventory_destroy(sim->native_primary_inventory); sim->native_primary_inventory = NULL; }
  qa_compat_q2_native_primary_commands_destroy(sim->native_primary_commands); sim->native_primary_commands = NULL;
  qa_compat_q2_native_primary_weapons_destroy(sim->native_primary_weapons); sim->native_primary_weapons = NULL;
  if (sim->selected_monsters) { qa_app_sim_selected_monsters_destroy(sim->selected_monsters); sim->selected_monsters = NULL; }
  for (size_t i = 0; i < sim->monster_sources.count; i++) {
    wl_monster_source *source = *(wl_monster_source **)qa_ss_map_value_at(&sim->monster_sources, i);
    if (qa_ss_is(source->base.kind, "q1")) {
      if (source->creatures) qa_q1_creatures_destroy(source->creatures);
      if (source->base.game) { qa_q1_services_free(source->base.game); free(source->base.game); }
      if (source->expansion) qa_app_sim_q1_expansion_destroy(source->expansion);
      if (source->pickups) qa_shared_pickup_admission_destroy(source->pickups->admission);
    } else if (source->base.q2) {
      qa_q2_entity_services_destroy(source->base.q2->game);
      qa_app_sim_selected_q2_monster_modules_destroy(source->base.q2);
    }
    qa_app_sim_source_random_destroy(source->base.random);
  }
  qa_ss_map_clear(&sim->monster_sources);
  if (sim->b1_guest_monsters) {
    wl_selected_behavior *behavior = sim->b1_guest_monsters;
    if (behavior->adjunct_q1) { qa_q1_services_free(behavior->adjunct_q1); free(behavior->adjunct_q1); }
    if (behavior->modules) {
      qa_q2_entity_services_destroy(behavior->modules->game);
      qa_app_sim_selected_q2_monster_modules_destroy(behavior->modules);
      qa_q2_item_module_destroy(behavior->map.items);
    }
    qa_app_sim_source_random_destroy(sim->b1_guest_random);
    sim->b1_guest_monsters = NULL;
  }
  if (sim->has_foreign_world) {
    if (sim->foreign_world.modules) {
      qa_q2_entity_services_destroy(sim->foreign_world.modules->game);
      qa_app_sim_foreign_q2_world_modules_destroy(sim->foreign_world.modules);
    }
    qa_app_sim_source_random_destroy(sim->foreign_world.random);
    if (sim->b1_foreign_q2) qa_native_provider_timing_free(&((wl_foreign_q2 *)sim->b1_foreign_q2)->timing);
    sim->has_foreign_world = false;
  }
  if (sim->has_foreign_q1_world) {
    qa_app_sim_foreign_q1_world_modules_destroy(sim->foreign_q1_world.modules);
    qa_app_sim_source_random_destroy(sim->foreign_q1_world.random);
    if (sim->foreign_q1_world.cvars) qa_cvar_registry_destroy(sim->foreign_q1_world.cvars);
    if (sim->b1_foreign_q1) qa_native_provider_timing_free(&((wl_foreign_q1 *)sim->b1_foreign_q1)->timing);
    sim->has_foreign_q1_world = false;
  }
  for (size_t i = 0; i < sim->guest_monster_plans.count; i++)
    qa_app_sim_guest_monster_plan_destroy(*(qa_app_sim_guest_monster_plan **)qa_ss_map_value_at(&sim->guest_monster_plans, i));
  qa_ss_map_clear(&sim->guest_monster_plans);
  free(sim->b1_match_players); sim->b1_match_players = NULL; sim->b1_match_player_count = sim->b1_match_player_capacity = 0;
  if (sim->has_b1_restart_carry) qa_app_q3_captured_session_destroy(&sim->b1_restart_carry);
  sim->has_b1_restart_carry = false;
}
