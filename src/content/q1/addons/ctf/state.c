/* Port of src/content/q1/addons/ctf/state.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/state.h"

#include <stdio.h>
#include <string.h>

#include "qa/content/q1/addons/ctf/grapple.h"
#include "qa/foundation/support.h"

double qa_q1ad_ctf_team_number(const char *team) {
  return team == NULL ? 0 : strcmp(team, "red") == 0 ? 5 : strcmp(team, "blue") == 0 ? 14 : 0;
}
const char *qa_q1ad_ctf_number_team(double value) {
  return value == 5 ? "red" : value == 14 ? "blue" : NULL;
}
const char *qa_q1ad_ctf_opposite(const char *team) {
  return strcmp(team, "red") == 0 ? "blue" : "red";
}
const char *qa_q1ad_ctf_rune_item(const char *rune, char buffer[64]) {
  (void)snprintf(buffer, 64, "q1:ctf/rune/%s", rune);
  return buffer;
}

void qa_q1ad_ctf_state_init(qa_q1ad_ctf_state *state, qa_q1ad_context *context,
                            qa_q1ad_ctf_services services, qa_shared_grapple_control *shared) {
  qa_q1ad_grapple_factory grapple_factory = state->grapple_factory;
  qa_q1ad_weapon_factory weapon_factory = state->weapon_factory;
  void *factory_ctx = state->factory_ctx;
  *state = (qa_q1ad_ctf_state){.context = context, .services = services, .shared_grapple = shared,
      .factory_ctx = factory_ctx, .grapple_factory = grapple_factory, .weapon_factory = weapon_factory};
  bool shared_blocks = shared != NULL && !(shared->selection.is_enabled &&
      shared->selection.binding != NULL && strcmp(shared->selection.binding, "slot") == 0 &&
      shared->selection.mechanic != NULL && strcmp(shared->selection.mechanic, "q1-threewave") == 0 &&
      shared->native_slot != NULL && shared->native_slot(shared->ctx, "q1-threewave"));
  state->grapple = shared_blocks ? (qa_q1ad_threewave_grapple){0} : qa_q1ad_ctf_create_grapple(state);
  context->slice = state;
}
bool qa_q1ad_ctf_native_grapple_enabled(const qa_q1ad_ctf_state *state) { return state->grapple.present; }
void qa_q1ad_ctf_grapple_state(qa_q1ad_ctf_state *state, qa_actor_id actor, qa_q1ad_grapple_pose *out) {
  if (!state->grapple.present)
    qa_range_error("Native grapple animation requires the selected Threewave slot");
  *out = (qa_q1ad_grapple_pose){0};
  if (state->grapple.state != NULL) (void)state->grapple.state(state->grapple.ctx, actor, out);
}
bool qa_q1ad_ctf_grapple_pulling(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  return state->grapple.present && state->grapple.pulling != NULL &&
      state->grapple.pulling(state->grapple.ctx, actor);
}
double qa_q1ad_ctf_teamplay(qa_q1ad_ctf_state *state) { return qa_q1ad_cvar(state->context, "teamplay"); }
bool qa_q1ad_ctf_start_map(qa_q1ad_ctf_state *state) {
  const char *map = state->context->game.map_name != NULL ? state->context->game.map_name : "";
  return strcmp(map, "start") == 0 || strcmp(map, "ctfstart") == 0;
}
qa_q1ad_actor *qa_q1ad_ctf_world(qa_q1ad_ctf_state *state) {
  qa_q1ad_actor *world = qa_q1ad_world(state->context);
  if (world == NULL) qa_range_error("CTF map has no world actor");
  return world;
}
qa_owned_actor qa_q1ad_ctf_owner(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_owned_actor owner;
  if (!qa_q1ad_resolve_owned(state->context, actor, &owner))
    qa_range_error("CTF player is no longer admitted");
  return owner;
}
qa_q1ad_body qa_q1ad_ctf_body(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1ad_body body;
  if (!qa_q1ad_body_by_id(state->context, actor, &body))
    qa_range_error("CTF actor has no shared body");
  return body;
}
void qa_q1ad_ctf_write_body(qa_q1ad_ctf_state *state, qa_actor_id actor, const qa_q1ad_body_patch *patch) {
  qa_q1ad_context *context = state->context;
  qa_q1ad_body body = qa_q1ad_ctf_body(state, actor);
  if (patch->has_origin) body.origin = patch->origin;
  if (patch->has_angles) body.angles = patch->angles;
  if (patch->has_velocity) body.velocity = patch->velocity;
  if (patch->has_bounds) body.bounds = patch->bounds;
  if (patch->has_ground) { body.ground = patch->ground; body.has_ground = !patch->ground_null; }
  qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
  if (context->game.write_body != NULL) context->game.write_body(context->game.ctx, owner, &body);
  if (context->game.link_body != NULL) context->game.link_body(context->game.ctx, owner);
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
const char *qa_q1ad_ctf_team(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  /* Bootstrap canonicalizes shared combat teams to team:red/team:blue for
   * cross-game identity; isolated composition hosts keep the plain forms. */
  const char *value = state->context->game.combat_team != NULL
      ? state->context->game.combat_team(state->context->game.ctx, actor) : NULL;
  if (value == NULL) return NULL;
  if (strcmp(value, "red") == 0 || strcmp(value, "team:red") == 0) return "red";
  if (strcmp(value, "blue") == 0 || strcmp(value, "team:blue") == 0) return "blue";
  return NULL;
}
const char *qa_q1ad_ctf_last_team(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  return qa_q1ad_ctf_number_team(qa_q1ad_ctf_number(state, actor, "lastteam"));
}
qa_q1ad_actor *qa_q1ad_ctf_flag(qa_q1ad_ctf_state *state, const char *team) {
  const char *classname = strcmp(team, "red") == 0 ? "item_flag_team1" : "item_flag_team2";
  qa_q1ad_actor *entities[512];
  size_t count = qa_q1ad_entities(state->context, entities, 512);
  for (size_t i = 0; i < count; i++)
    if (entities[i]->classname != NULL && strcmp(entities[i]->classname, classname) == 0) return entities[i];
  return NULL;
}
const char *qa_q1ad_ctf_flag_team(qa_q1ad_actor *flag) {
  return flag->classname != NULL && strcmp(flag->classname, "item_flag_team1") == 0 ? "red" : "blue";
}
qa_q1ad_actor *qa_q1ad_ctf_carried(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1ad_actor *entities[512];
  size_t count = qa_q1ad_entities(state->context, entities, 512);
  for (size_t i = 0; i < count; i++) {
    const char *classname = entities[i]->classname != NULL ? entities[i]->classname : "";
    if (strncmp(classname, "item_flag_team", 14) == 0 && entities[i]->count == 1 &&
        entities[i]->has_owner && qa_same_actor(entities[i]->owner, actor)) return entities[i];
  }
  return NULL;
}
qa_q1ad_actor *qa_q1ad_ctf_hook(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  return state->grapple.present && state->grapple.hook != NULL
      ? state->grapple.hook(state->grapple.ctx, actor) : NULL;
}
const char *qa_q1ad_ctf_rune(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  char item[64];
  for (int i = 0; i < 4; i++) {
    double count = state->context->game.inventory_count != NULL
        ? state->context->game.inventory_count(state->context->game.ctx, actor,
              qa_q1ad_ctf_rune_item(QA_Q1AD_CTF_RUNES[i], item)) : 0;
    if (count > 0) return QA_Q1AD_CTF_RUNES[i];
  }
  return NULL;
}
void qa_q1ad_ctf_grant(qa_q1ad_ctf_state *state, qa_actor_id actor, qa_item_id item, double count, double capacity) {
  if (state->context->game.inventory_configure != NULL)
    state->context->game.inventory_configure(state->context->game.ctx,
        qa_q1ad_ctf_owner(state, actor), item, count, capacity);
}
void qa_q1ad_ctf_announce(qa_q1ad_ctf_state *state, const char *key, const qa_actor_id *actor, const char *extra) {
  qa_q1ad_msg_arg args[2];
  size_t count = 0;
  if (actor != NULL) {
    args[count++] = (qa_q1ad_msg_arg){.text = state->services.name != NULL
        ? state->services.name(state->services.ctx, *actor) : ""};
  }
  if (extra != NULL && extra[0] != '\0') args[count++] = (qa_q1ad_msg_arg){.text = extra};
  qa_actor_id players[64];
  size_t player_count = qa_q1ad_players(state->context, players, 64);
  for (size_t i = 0; i < player_count; i++)
    qa_q1ad_message(state->context, players[i], key, false, args, count);
}
void qa_q1ad_ctf_update(qa_q1ad_ctf_state *state, const qa_actor_id *player) {
  int64_t flags = 0;
  const char *teams[2] = {"red", "blue"};
  for (int index = 0; index < 2; index++) {
    qa_q1ad_actor *flag = qa_q1ad_ctf_flag(state, teams[index]);
    flags |= (flag == NULL ? 1 : (int64_t)1 << (int)flag->count) << (index * 3);
  }
  qa_actor_id solo[1], many[64];
  const qa_actor_id *actors = many;
  size_t count;
  if (player != NULL) { solo[0] = *player; actors = solo; count = 1; }
  else count = qa_q1ad_players(state->context, many, 64);
  for (size_t i = 0; i < count; i++) {
    const char *rune = qa_q1ad_ctf_rune(state, actors[i]);
    int ordinal = -1;
    if (rune != NULL) for (int j = 0; j < 4; j++) if (QA_Q1AD_CTF_RUNES[j] == rune) ordinal = j;
    if (state->services.status != NULL)
      state->services.status(state->services.ctx, actors[i], (qa_q1ad_ctf_status){
          .red = state->services.captures != NULL ? state->services.captures(state->services.ctx, "red") : 0,
          .blue = state->services.captures != NULL ? state->services.captures(state->services.ctx, "blue") : 0,
          .flags = flags, .rune_items = ordinal < 0 ? 0 : (int64_t)32 << ordinal});
  }
}
