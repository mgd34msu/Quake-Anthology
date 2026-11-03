/* Port of src/content/q1/addons/ctf/state.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/ctf/grapple.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

static bool same_text(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

double qa_q1ad_ctf_team_number(const char *team) {
  return same_text(team, "red") ? 5 : same_text(team, "blue") ? 14 : 0;
}
const char *qa_q1ad_ctf_number_team(double value) {
  return value == 5 ? "red" : value == 14 ? "blue" : NULL;
}
const char *qa_q1ad_ctf_opposite(const char *team) {
  return strcmp(team, "red") == 0 ? "blue" : "red";
}
qa_item_id qa_q1ad_ctf_rune_item(const char *rune) {
  static const char *const ITEMS[4] = {"q1:ctf/rune/resistance", "q1:ctf/rune/strength",
                                       "q1:ctf/rune/haste", "q1:ctf/rune/regeneration"};
  for (int i = 0; i < 4; i++)
    if (strcmp(QA_Q1AD_CTF_RUNES[i], rune) == 0) return ITEMS[i];
  qa_range_error("Unknown CTF rune: %s", rune);
}

bool qa_q1ad_ctf_state_init(qa_q1ad_ctf_state *state, qa_q1ad_context *context,
                            qa_q1ad_ctf_services services, const qa_shared_grapple_control *shared) {
  *state = (qa_q1ad_ctf_state){.context = context, .services = services, .shared_grapple = shared};
  bool shared_blocks = shared != NULL && !(shared->selection.is_enabled &&
      same_text(shared->selection.binding, "slot") && same_text(shared->selection.mechanic, "q1-threewave") &&
      shared->native_slot(shared->ctx, "q1-threewave"));
  if (shared_blocks) return true;
  state->grapple = qa_q1ad_ctf_create_grapple(state);
  return state->grapple != NULL;
}

void qa_q1ad_ctf_state_free(qa_q1ad_ctf_state *state) {
  if (state->grapple_weapon != NULL) free(state->grapple_weapon);
  if (state->grapple != NULL) {
    qa_session_actor_registry_off_release(state->context->game->host.actors, state->grapple_release);
    qa_q1_threewave_grapple_free(state->grapple);
    free(state->grapple);
  }
  state->grapple = NULL;
  state->grapple_weapon = NULL;
}

bool qa_q1ad_ctf_native_grapple_enabled(const qa_q1ad_ctf_state *state) { return state->grapple != NULL; }

qa_q1_threewave_state *qa_q1ad_ctf_grapple_state(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  if (state->grapple == NULL) qa_range_error("Native grapple animation requires the selected Threewave slot");
  return qa_q1_threewave_state_for(state->grapple, actor);
}
bool qa_q1ad_ctf_grapple_pulling(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  return state->grapple != NULL && qa_q1_threewave_pulling(state->grapple, actor);
}
double qa_q1ad_ctf_teamplay(qa_q1ad_ctf_state *state) {
  return state->context->services.cvar(state->context->services.ctx, "teamplay");
}
/* The vote lobby is "start" in the rerelease tree and "ctfstart" in the
 * classic ThreeWave tree; both carry info_vote_destination spawns. */
bool qa_q1ad_ctf_start_map(qa_q1ad_ctf_state *state) {
  const char *map = state->context->game->map_name;
  return same_text(map, "start") || same_text(map, "ctfstart");
}
qa_q1_actor *qa_q1ad_ctf_world(qa_q1ad_ctf_state *state) {
  qa_q1_actor *world = state->context->game->world;
  if (world == NULL) qa_range_error("CTF map has no world actor");
  return world;
}
qa_owned_actor qa_q1ad_ctf_owner(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(state->context->game->host.actors, actor, &owner))
    qa_range_error("CTF player is no longer admitted");
  return owner;
}
qa_q1_body qa_q1ad_ctf_body(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_body_state_result body = qa_shared_body_table_read(state->context->game->host.bodies, actor);
  if (!body.has_value) qa_range_error("CTF actor has no shared body");
  return qa_q1_body_from_state(body.value);
}
void qa_q1ad_ctf_write_body(qa_q1ad_ctf_state *state, qa_actor_id actor, const qa_q1_body_patch *patch) {
  qa_q1_services *game = state->context->game;
  qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
  qa_q1_body body = qa_q1ad_ctf_body(state, actor);
  if (patch->has_origin) body.origin = patch->origin;
  if (patch->has_angles) body.angles = patch->angles;
  if (patch->has_velocity) body.velocity = patch->velocity;
  if (patch->has_bounds) body.bounds = patch->bounds;
  if (patch->has_ground) {
    body.has_ground = patch->ground != NULL;
    if (patch->ground != NULL) body.ground = *patch->ground;
  }
  qa_shared_body_table_write(game->host.bodies, owner, qa_q1_body_to_state(&body));
  qa_shared_body_table_link(game->host.bodies, qa_q1ad_ctf_owner(state, actor), NULL);
}
double qa_q1ad_ctf_number(qa_q1ad_ctf_state *state, qa_actor_id actor, const char *name) {
  char field[96];
  (void)snprintf(field, sizeof field, "ctf.%s", name);
  return qa_q1ad_player_number(state->context, actor, field);
}
void qa_q1ad_ctf_set(qa_q1ad_ctf_state *state, qa_actor_id actor, const char *name, double value) {
  char field[96];
  (void)snprintf(field, sizeof field, "ctf.%s", name);
  qa_q1ad_set_player_number(state->context, actor, field, value);
}
/* Bootstrap canonicalizes shared combat teams to team:red/team:blue for
 * cross-game identity; isolated composition hosts keep the plain forms. */
const char *qa_q1ad_ctf_team(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_combat_state_result combat = qa_gameplay_authority_read(state->context->game->host.combat, actor);
  const char *value = combat.has_value ? combat.value.team : NULL;
  if (same_text(value, "red") || same_text(value, "team:red")) return "red";
  if (same_text(value, "blue") || same_text(value, "team:blue")) return "blue";
  return NULL;
}
const char *qa_q1ad_ctf_last_team(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  return qa_q1ad_ctf_number_team(qa_q1ad_ctf_number(state, actor, "lastteam"));
}
qa_q1_actor *qa_q1ad_ctf_flag(qa_q1ad_ctf_state *state, const char *team) {
  qa_q1_services *game = state->context->game;
  const char *classname = strcmp(team, "red") == 0 ? "item_flag_team1" : "item_flag_team2";
  for (size_t i = 0; i < game->entity_count; i++)
    if (strcmp(game->entities[i]->classname, classname) == 0) return game->entities[i];
  return NULL;
}
const char *qa_q1ad_ctf_flag_team(const qa_q1_actor *flag) {
  return strcmp(flag->classname, "item_flag_team1") == 0 ? "red" : "blue";
}
qa_q1_actor *qa_q1ad_ctf_carried(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1_services *game = state->context->game;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (strncmp(entity->classname, "item_flag_team", 14) == 0 && entity->count == 1 &&
        entity->has_owner && qa_same_actor(entity->owner, actor)) return entity;
  }
  return NULL;
}
qa_q1_actor *qa_q1ad_ctf_hook(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  return state->grapple != NULL ? qa_q1_threewave_hook(state->grapple, actor) : NULL;
}
const char *qa_q1ad_ctf_rune(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  for (int i = 0; i < 4; i++)
    if (qa_shared_inventory_table_count(state->context->game->host.inventory, actor,
                                        qa_q1ad_ctf_rune_item(QA_Q1AD_CTF_RUNES[i])) > 0)
      return QA_Q1AD_CTF_RUNES[i];
  return NULL;
}
void qa_q1ad_ctf_grant(qa_q1ad_ctf_state *state, qa_actor_id actor, qa_item_id item, double count, double capacity) {
  qa_inventory_entry entry = {.item = item, .count = count, .capacity = capacity};
  qa_shared_inventory_table_configure(state->context->game->host.inventory, qa_q1ad_ctf_owner(state, actor),
                                      entry, NULL, NULL);
}
void qa_q1ad_ctf_announce(qa_q1ad_ctf_state *state, const char *key, const qa_actor_id *actor, const char *extra) {
  qa_q1_services *game = state->context->game;
  qa_q1_msg_arg args[2];
  size_t count = 0;
  if (actor != NULL) args[count++] = (qa_q1_msg_arg){.text = state->services.name(state->services.ctx, *actor)};
  if (extra != NULL && extra[0] != '\0') args[count++] = (qa_q1_msg_arg){.text = extra};
  size_t player_count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &player_count);
  for (size_t i = 0; i < player_count; i++) qa_q1_message(game, &players[i], key, false, args, count);
}
void qa_q1ad_ctf_update(qa_q1ad_ctf_state *state, const qa_actor_id *player) {
  qa_q1_services *game = state->context->game;
  double flags = 0;
  static const char *const TEAMS[2] = {"red", "blue"};
  for (int index = 0; index < 2; index++) {
    qa_q1_actor *flag = qa_q1ad_ctf_flag(state, TEAMS[index]);
    flags = qa_js_bit_or(flags, qa_js_shl(flag == NULL ? 1 : qa_js_shl(1, flag->count), index * 3));
  }
  size_t count = 1;
  const qa_actor_id *actors = player;
  if (player == NULL) actors = game->host.players(game->host.ctx, &count);
  for (size_t i = 0; i < count; i++) {
    const char *rune = qa_q1ad_ctf_rune(state, actors[i]);
    int ordinal = -1;
    if (rune != NULL)
      for (int j = 0; j < 4; j++) if (QA_Q1AD_CTF_RUNES[j] == rune) ordinal = j;
    /* Object literal order: captures("red") before captures("blue"). */
    qa_q1ad_ctf_status status = {0};
    status.red = state->services.captures(state->services.ctx, "red");
    status.blue = state->services.captures(state->services.ctx, "blue");
    status.flags = (int64_t)flags;
    status.rune_items = ordinal < 0 ? 0 : (int64_t)qa_js_shl(32, ordinal);
    state->services.status(state->services.ctx, actors[i], status);
  }
}
