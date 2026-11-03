/* Port of src/content/q1/missionpacks/world/rogue-teams.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_teams.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/foundation/spawns.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/content/q1/missionpacks/world/index.h"

static const qa_bounds FLAG_BOUNDS = {{-16, -16, 0}, {16, 16, 74}};
static int32_t bits(double value) { return qa_js_to_int32(value); }
static double num(const qa_q1_actor *entity, const char *key) { return qa_q1_actor_number(entity, key); }
static const char *team_name(double team) {
  return team == 5 ? "Red" : team == 14 ? "Blue" : team == 1 ? "Grey" : "UNKNOWN";
}
static double mode_of(const qa_q1mp_rogue_teams *t) { return qa_q1mp_teamplay(t->game); }
static bool ctf_of(const qa_q1mp_rogue_teams *t) { const double mode = mode_of(t); return mode == 4 || mode == 5 || mode == 6; }
static double gamecfg_of(const qa_q1mp_rogue_teams *t) {
  return t->hooks != NULL && t->hooks->gamecfg != NULL ? t->hooks->gamecfg(t->hooks->ctx) : 0;
}
/* host.players(), copied: the callbacks below may change the listing. */
static qa_actor_id *players_of(qa_q1_services *game, size_t *out_count) {
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  qa_actor_id *copy = malloc((count != 0 ? count : 1) * sizeof *copy);
  if (copy == NULL) qa_range_error("Out of memory");
  if (count != 0) memcpy(copy, players, count * sizeof *copy);
  *out_count = count;
  return copy;
}

static double color(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  if (t->hooks == NULL || t->hooks->team_color == NULL) {
    if (mode_of(t) <= 0) return 0;
    qa_range_error("Rogue teamplay requires shared player colors");
  }
  return t->hooks->team_color(t->hooks->ctx, actor);
}
static void set_state_entry(qa_q1mp_rogue_teams *t, qa_owned_actor owner, qa_q1_actor *state) {
  for (size_t i = 0; i < t->state_count; i++)
    if (qa_same_owned_actor(t->states[i].actor, owner)) { t->states[i].state = state; return; }
  if (t->state_count == t->state_capacity) {
    const size_t next = t->state_capacity != 0 ? t->state_capacity * 2 : 8;
    qa_q1mp_team_state_entry *grown = realloc(t->states, next * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    t->states = grown; t->state_capacity = next;
  }
  t->states[t->state_count].actor = owner; t->states[t->state_count].state = state; t->state_count++;
}
static qa_q1_actor *state_of(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  qa_q1_services *game = t->game;
  qa_owned_actor owner;
  const bool resolved = qa_session_actor_registry_resolve_owned(game->host.actors, actor, &owner);
  qa_q1_actor *cached = NULL;
  if (resolved) for (size_t i = 0; i < t->state_count; i++) if (qa_same_owned_actor(t->states[i].actor, owner)) cached = t->states[i].state;
  if (cached != NULL && qa_q1_live(game, cached)) return cached;
  /* A restored world rebuilds its entities, so a cache miss still scans once for a saved state entity. */
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (qa_q1mp_streq(entity->classname, "rogue_team_state") && entity->has_owner && qa_same_actor(entity->owner, actor)) {
      if (resolved) set_state_entry(t, owner, entity);
      return entity;
    }
  }
  qa_q1_actor *state = qa_q1_create(game, "rogue_team_state", NULL, NULL);
  state->has_owner = true; state->owner = actor;
  qa_q1mp_world_number(state, "steam", ctf_of(t) && (bits(gamecfg_of(t)) & 8) == 0 ? -1 : color(t, actor));
  if (resolved) set_state_entry(t, owner, state);
  return state;
}
/* Rogue ClientKill refuses the fifth suicide of a connection and counts every accepted one. */
bool qa_q1mp_teams_suicide(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  qa_q1_actor *state = state_of(t, actor);
  if (num(state, "suicide_count") > 3) {
    qa_q1_event event = {0};
    event.kind = "message"; event.has_player = true; event.player = actor; event.text = "$qc_suicided_too_much"; event.center = false;
    qa_q1_emit(t->game, event);
    return false;
  }
  qa_q1mp_world_number(state, "suicide_count", num(state, "suicide_count") + 1);
  return true;
}
double qa_q1mp_teams_team(qa_q1mp_rogue_teams *t, const qa_actor_id *actor) {
  return actor == NULL || !qa_q1_is_player(t->game, actor) ? 0 : num(state_of(t, *actor), "steam");
}
static double team_of(qa_q1mp_rogue_teams *t, qa_actor_id actor) { return qa_q1mp_teams_team(t, &actor); }
static void set_color(qa_q1mp_rogue_teams *t, qa_actor_id actor, double team) {
  if (t->hooks == NULL || t->hooks->set_team_color == NULL) qa_range_error("Rogue CTF requires shared color mutation");
  t->hooks->set_team_color(t->hooks->ctx, actor, team);
}
static void score(qa_q1mp_rogue_teams *t, qa_actor_id actor, double delta) {
  if (t->hooks == NULL || t->hooks->add_frags == NULL) qa_range_error("Rogue CTF requires shared score authority");
  t->hooks->add_frags(t->hooks->ctx, actor, delta);
}
static const char *name_of(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  if (t->hooks == NULL || t->hooks->player_name == NULL) qa_range_error("Rogue CTF requires player names");
  return t->hooks->player_name(t->hooks->ctx, actor);
}
static void message_args(qa_q1mp_rogue_teams *t, qa_actor_id actor, const char *text, bool center,
                         const qa_q1_msg_arg *args, size_t arg_count) {
  qa_q1_event event = {0};
  event.kind = "message"; event.has_player = true; event.player = actor; event.text = text; event.center = center;
  event.args = args; event.arg_count = arg_count;
  qa_q1_emit(t->game, event);
}
static void message(qa_q1mp_rogue_teams *t, qa_actor_id actor, const char *text, bool center) {
  message_args(t, actor, text, center, NULL, 0);
}
static void broadcast(qa_q1mp_rogue_teams *t, const char *text) {
  size_t count;
  qa_actor_id *players = players_of(t->game, &count);
  for (size_t i = 0; i < count; i++) message(t, players[i], text, false);
  free(players);
}
static void sound(qa_q1mp_rogue_teams *t, qa_actor_id actor, const char *path, bool global) {
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(t->game->host.actors, actor, &owner)) return;
  qa_q1_sound(t->game, owner.id, path, global ? "voice" : "item", global ? 0 : 1, 1);
}
static bool legal(const qa_q1mp_rogue_teams *t, double team) {
  const double mode = mode_of(t);
  return mode < 4 ? team > 0 : ctf_of(t) ? team == 5 || team == 14 || (mode == 6 && team == 1) : true;
}
/* ClientConnect's team assignment, run ahead of spawn selection so a first spawn uses the team base. */
static void assign(qa_q1mp_rogue_teams *t, qa_actor_id actor, qa_q1_actor *state) {
  double red = 0, blue = 0, grey = 0;
  size_t count_players;
  qa_actor_id *players = players_of(t->game, &count_players);
  for (size_t i = 0; i < count_players; i++) if (!qa_same_actor(actor, players[i])) {
    const double team = team_of(t, players[i]);
    if (team == 5) red++; else if (team == 14) blue++; else if (team == 1) grey++;
  }
  free(players);
  double team = 5, count = red;
  if (blue < count || (blue == count && t->game->host.random(t->game->host.ctx) < 0.5)) { team = 14; count = blue; }
  if (mode_of(t) == 6 && grey * 2 < count) team = 1;
  qa_q1mp_world_number(state, "steam", team);
  qa_q1mp_world_number(state, "ctf_flags", qa_js_bit_or(num(state, "ctf_flags"), 4));
  char text[96];
  (void)snprintf(text, sizeof text, "You have been assigned to the %s team.\n", team_name(team));
  message(t, actor, text, false);
  set_color(t, actor, team);
}
void qa_q1mp_teams_player_spawned(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  qa_q1_actor *state = state_of(t, actor), *player = qa_q1_get_entity(t->game, &actor);
  /* TeamSetSkin: the CTF player model uses its second skin in deathmatch. */
  if (player != NULL) player->skin = t->game->options.deathmatch != 0 && mode_of(t) >= 4 ? 1 : 0;
  static const char *const TIMERS[] = {"ctf_lasthurtcarrier", "ctf_lastreturnedflag", "ctf_lastfraggedcarrier", "ctf_flagsince"};
  for (size_t i = 0; i < 4; i++) qa_q1mp_world_number(state, TIMERS[i], -10);
  if (num(state, "steam") >= 0 || mode_of(t) < 4) {
    const double team = color(t, actor);
    if (legal(t, team)) { qa_q1mp_world_number(state, "steam", team); return; }
  }
  assign(t, actor, state);
}
typedef struct team_score { double team, score; } team_score;
static int by_score(const void *a, const void *b, void *ctx) {
  (void)ctx;
  const double delta = ((const team_score *)b)->score - ((const team_score *)a)->score;
  return delta > 0 ? 1 : delta < 0 ? -1 : 0; /* NaN compares as +0 */
}
static void check_update(qa_q1mp_rogue_teams *t) {
  qa_q1_services *game = t->game;
  qa_q1_actor *world = game->world;
  if (world == NULL || num(world, "rogue:nextteamupdtime") > game->time || mode_of(t) < 1 || game->options.deathmatch == 0) return;
  qa_q1mp_world_number(world, "rogue:nextteamupdtime", game->time + 120);
  if (!ctf_of(t)) return;
  if (t->hooks == NULL || t->hooks->frags == NULL) qa_range_error("Rogue score update requires shared frags");
  double red = 0, blue = 0, grey = 0;
  size_t count;
  qa_actor_id *players = players_of(game, &count);
  for (size_t i = 0; i < count; i++) {
    const double team = team_of(t, players[i]), frags = t->hooks->frags(t->hooks->ctx, players[i]);
    if (team == 5) red += frags; else if (team == 14) blue += frags; else if (team == 1) grey += frags;
  }
  free(players);
  team_score scores[3] = {{5, red}, {14, blue}, {1, grey}};
  const size_t score_count = mode_of(t) == 6 ? 3 : 2;
  qa_js_stable_sort(scores, score_count, sizeof scores[0], by_score, NULL);
  char text[160];
  if (scores[0].score > scores[1].score) {
    (void)snprintf(text, sizeof text, "%s team is leading by %s points!\n", team_name(scores[0].team), QA_JS_NUMBER(scores[0].score - scores[1].score));
    broadcast(t, text);
    return;
  }
  const double tied0 = red == blue ? 5 : grey == blue ? 14 : 5, tied1 = red == blue ? 14 : grey == blue ? 1 : 1;
  (void)snprintf(text, sizeof text, "%s and %s teams are tied with %s points!\n", team_name(tied0), team_name(tied1), QA_JS_NUMBER(scores[0].score));
  broadcast(t, text);
}
void qa_q1mp_teams_frame(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  qa_q1_services *game = t->game;
  qa_q1_actor *state = state_of(t, actor);
  const double current = color(t, actor);
  check_update(t);
  if (game->options.deathmatch == 0 || mode_of(t) < 4) { qa_q1mp_world_number(state, "steam", current); return; }
  if ((bits(num(state, "ctf_flags")) & 4) != 0) {
    qa_q1mp_world_number(state, "ctf_flags", bits(num(state, "ctf_flags")) & ~4);
    set_color(t, actor, num(state, "steam"));
    return;
  }
  if (!legal(t, current) && current == num(state, "steam")) qa_q1mp_world_number(state, "steam", -1);
  if (current == num(state, "steam")) return;
  const double previous = num(state, "steam");
  const bool changes = (bits(gamecfg_of(t)) & 16) != 0;
  if (previous >= 0 && legal(t, previous) && !changes) {
    if (num(state, "suicide_count") > 3) {
      message(t, actor, "$qc_color_games", false);
      if (t->hooks == NULL || t->hooks->disconnect == NULL) qa_range_error("Rogue team enforcement requires shared disconnect");
      t->hooks->disconnect(t->hooks->ctx, actor);
      char text[160];
      (void)snprintf(text, sizeof text, "%s has bad color sense\n", name_of(t, actor));
      broadcast(t, text);
      return;
    }
    if (num(state, "ctf_killed") != 1) qa_q1mp_world_number(state, "ctf_killed", 2);
    (void)qa_q1_damage(game, actor, actor, &actor, 1000, NULL, NULL, NULL, NULL);
    qa_q1mp_world_number(state, "suicide_count", num(state, "suicide_count") + 1);
    message(t, actor, "$qc_cannot_change_teams", false);
    set_color(t, actor, previous);
    return;
  }
  if (previous >= 0 && !legal(t, previous)) qa_q1mp_world_number(state, "steam", -50);
  if (num(state, "steam") > 0) {
    if (num(state, "ctf_killed") != 1) qa_q1mp_world_number(state, "ctf_killed", 2);
    (void)qa_q1_damage(game, actor, actor, &actor, 1000, NULL, NULL, NULL, NULL);
  }
  if (t->hooks == NULL || t->hooks->frags == NULL) qa_range_error("Rogue team changes require shared scores");
  score(t, actor, -t->hooks->frags(t->hooks->ctx, actor));
  qa_q1mp_teams_player_spawned(t, actor);
}
qa_q1_actor *qa_q1mp_teams_select_spawn(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  qa_q1_services *game = t->game;
  qa_q1_actor *world = game->world;
  if (game->options.coop || game->options.deathmatch == 0 || !ctf_of(t) || world == NULL) return NULL;
  size_t count;
  qa_q1_actor **entities = qa_q1mp_entities(game, &count);
  for (size_t i = 0; i < count; i++) if (qa_q1mp_streq(entities[i]->classname, "testplayerstart")) {
    qa_q1_actor *test = entities[i];
    free(entities);
    return test;
  }
  if (mode_of(t) >= 4 && num(state_of(t, actor), "steam") < 0) assign(t, actor, state_of(t, actor));
  const double team = num(state_of(t, actor), "ctf_killed") == 0 ? team_of(t, actor) : 0;
  const char *key = team == 5 ? "rogue:team1_lastspawn" : team == 14 ? "rogue:team2_lastspawn" : "rogue:lastspawn";
  const char *classname = team == 5 ? "info_player_team1" : team == 14 ? "info_player_team2" : "info_player_deathmatch";
  qa_actor_id last_id;
  qa_q1_actor *last = qa_q1_actor_reference_get(world, key, &last_id) ? qa_q1_get_entity(game, &last_id) : NULL;
  size_t points = 0;
  for (size_t i = 0; i < count; i++) if (qa_q1mp_streq(entities[i]->classname, classname)) entities[points++] = entities[i];
  size_t start_index = points; /* TS indexOf -1 */
  for (size_t i = 0; i < points && start_index == points; i++) if (entities[i] == last) start_index = i;
  qa_q1_actor *selected = NULL;
  for (size_t step = 1; step <= points && selected == NULL; step++) {
    qa_q1_actor *point = entities[start_index == points ? step - 1 : (start_index + step) % points];
    bool occupied = false;
    if (point != last) {
      size_t player_count;
      qa_actor_id *players = players_of(game, &player_count);
      for (size_t p = 0; p < player_count && !occupied; p++) {
        const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, players[p]);
        occupied = body.has_value &&
                   qa_q1_length(qa_q1_vsub(qa_q1_vadd(body.value.origin, qa_q1_vscale(qa_q1_vadd(body.value.bounds.min, body.value.bounds.max), 0.5)),
                                           qa_q1_get_body(game, point).origin)) <= 32;
      }
      free(players);
    }
    if (point == last || !occupied) {
      if (!qa_q1_actor_reference_set(world, key, &point->actor.id)) qa_range_error("Out of memory");
      selected = point;
    }
  }
  free(entities);
  return selected;
}
static qa_q1_actor **flags_of(qa_q1_services *game, size_t *out_count) {
  size_t count;
  qa_q1_actor **entities = qa_q1mp_entities(game, &count);
  size_t flags = 0;
  for (size_t i = 0; i < count; i++) {
    const char *classname = entities[i]->classname;
    if (qa_q1mp_streq(classname, "item_flag_team1") || qa_q1mp_streq(classname, "item_flag_team2") || qa_q1mp_streq(classname, "item_flag"))
      entities[flags++] = entities[i];
  }
  *out_count = flags;
  return entities;
}
static qa_q1_actor *find_flag(qa_q1_actor *const *flags, size_t count, const char *classname) {
  for (size_t i = 0; i < count; i++) if (qa_q1mp_streq(flags[i]->classname, classname)) return flags[i];
  return NULL;
}
bool qa_q1mp_teams_impulse(qa_q1mp_rogue_teams *t, qa_actor_id actor, double impulse) {
  if (impulse != 23) return false;
  qa_q1_services *game = t->game;
  if (game->options.deathmatch == 0) return true;
  if (!ctf_of(t)) { message(t, actor, "$qc_ctf_disabled", false); return true; }
  size_t count;
  qa_q1_actor **flags = flags_of(game, &count);
  char text[256];
  if (mode_of(t) == 5) {
    qa_q1_actor *flag = find_flag(flags, count, "item_flag");
    const char *line;
    if (flag == NULL) line = "$qc_flag_missing";
    else if (num(flag, "cnt") == 0) line = "$qc_flag_at_base";
    else if (num(flag, "cnt") == 2) line = "$qc_flag_lying_about";
    else if (num(flag, "cnt") == 1 && flag->has_owner) {
      if (qa_same_actor(flag->owner, actor)) line = "$qc_you_have_flag";
      else {
        const char * value3 = name_of(t, flag->owner);
        const char * value4 = team_name(team_of(t, flag->owner));
        (void)snprintf(text, sizeof text, "%s of the %s team has the flag!\n", value3, value4);
        line = text;
      }
    } else line = "$qc_flag_screwed_up";
    message(t, actor, line, false);
    free(flags);
    return true;
  }
  qa_q1_actor *red = find_flag(flags, count, "item_flag_team1"), *blue = find_flag(flags, count, "item_flag_team2");
  free(flags);
  const bool swapped = mode_of(t) == 4 && color(t, actor) != 5;
  qa_q1_actor *ordered[2] = {swapped ? blue : red, swapped ? red : blue};
  for (int index = 0; index < 2; index++) {
    qa_q1_actor *flag = ordered[index];
    /* TS compares both indices against red?.number("team"). */
    bool own = index == 0;
    if (mode_of(t) != 4) { const double mine = team_of(t, actor); own = red != NULL && mine == num(red, "team"); }
    char label[32];
    if (own) (void)snprintf(label, sizeof label, "Your flag");
    else if (mode_of(t) == 4) (void)snprintf(label, sizeof label, "The enemy flag");
    else (void)snprintf(label, sizeof label, "%s flag", index == 0 ? "Red" : "Blue");
    if (flag != NULL && num(flag, "cnt") == 1 && flag->has_owner) {
      const qa_actor_id owner = flag->owner;
      const char *line;
      if (qa_same_actor(owner, actor)) {
        if (mode_of(t) == 4) line = "$qc_you_have_enemy_flag";
        else { (void)snprintf(text, sizeof text, "You have the %s flag!\n", index == 0 ? "Red" : "Blue"); line = text; }
      } else if (mode_of(t) == 4) {
        (void)snprintf(text, sizeof text, "%s has %s flag.\n", name_of(t, owner), own ? "your" : "the enemy"); line = text;
      } else {
        const char * value = name_of(t, owner);
        const char * value2 = team_name(team_of(t, owner));
        (void)snprintf(text, sizeof text, "%s of the %s team has the %s flag.\n", value, value2, index == 0 ? "Red" : "Blue");
        line = text;
      }
      message(t, actor, line, false);
    } else {
      const char *where = flag == NULL ? "missing!" : num(flag, "cnt") == 0 ? (mode_of(t) == 6 ? "at base." : own ? "in your base." : "in their base.")
                          : num(flag, "cnt") == 2 ? "lying about." : " corrupt.";
      (void)snprintf(text, sizeof text, "%s is %s\n", label, where);
      message(t, actor, text, false);
    }
  }
  return true;
}
static bool drop_floor(qa_q1_services *game, qa_q1_actor *entity) {
  const qa_q1_body body = qa_q1_get_body(game, entity);
  const qa_vec3 origin = qa_q1_vadd(body.origin, QA_Q1MP_V3(0, 0, 6));
  qa_q1_trace_request request = {0};
  request.start = origin; request.end = qa_q1_vadd(origin, QA_Q1MP_V3(0, 0, -256)); request.bounds = body.bounds;
  request.has_ignore = true; request.ignore = entity->actor.id; request.monsters = true;
  const qa_q1_trace trace = game->host.trace(game->host.ctx, request);
  if (trace.fraction == 1 || trace.all_solid) return false;
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = trace.end;
  patch.has_velocity = true; patch.velocity = QA_Q1_ZERO;
  patch.has_ground = true; patch.ground = trace.has_actor ? &trace.actor : NULL;
  qa_q1_set_body(game, entity, &patch);
  qa_q1_link(game, entity);
  return true;
}
static void flag_base(qa_q1mp_rogue_teams *t, qa_q1_actor *flag, const char *classname) {
  qa_q1_services *game = t->game;
  qa_q1_actor *base = qa_q1_create(game, classname, NULL, NULL);
  qa_q1mp_set_string(&base->model, "progs/ctfbase.mdl");
  base->skin = flag->skin;
  qa_q1mp_world_number(base, "team", num(flag, "team"));
  base->movement_flags = 256; base->movement = "toss";
  base->solid = mode_of(t) == 5 || mode_of(t) == 6 ? "trigger" : "none";
  if (qa_q1mp_streq(base->solid, "trigger")) base->touch = qa_q1_callback_registry_touch(game->named, base, "rogue:flagbase_touch");
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = qa_q1_get_body(game, flag).origin;
  patch.has_angles = true; patch.angles = qa_q1_get_body(game, flag).angles;
  patch.has_bounds = true; patch.bounds = (qa_bounds){{-8, -8, 0}, {8, 8, 8}};
  qa_q1_set_body(game, base, &patch);
  if (!drop_floor(game, base)) qa_q1_remove(game, base);
}
static void regenerate(qa_q1mp_rogue_teams *t, qa_q1_actor *flag) {
  qa_q1_services *game = t->game;
  flag->movement = "toss"; flag->solid = "trigger";
  qa_q1_sound(game, flag->actor.id, "items/itembk2.wav", "voice", 1, 1);
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = qa_q1_actor_vector(flag, "oldorigin");
  patch.has_angles = true; patch.angles = flag->mangle;
  qa_q1_set_body(game, flag, &patch);
  qa_q1mp_world_number(flag, "cnt", 0);
  flag->has_owner = false;
  qa_q1_link(game, flag);
}
static void return_flag(qa_q1mp_rogue_teams *t, qa_q1_actor *flag) {
  regenerate(t, flag);
  size_t count;
  qa_actor_id *players = players_of(t->game, &count);
  for (size_t i = 0; i < count; i++) {
    double left;
    double right;
    char text[96];
    const char *line;
    if (mode_of(t) == 5) line = "$qc_flag_returned";
    else if (mode_of(t) == 6) { (void)snprintf(text, sizeof text, "%s flag has been returned to base!\n", team_name(num(flag, "team"))); line = text; }
    else line = (left = team_of(t, players[i]), right = num(flag, "team"), left == right) ? "$qc_your_flag_returned_base" : "$qc_enemy_flag_returned_base";
    message(t, players[i], line, true);
  }
  free(players);
}
static void drop_flag(qa_q1mp_rogue_teams *t, qa_q1_actor *flag) {
  qa_q1_services *game = t->game;
  const bool has_actor = flag->has_owner;
  const qa_actor_id actor = flag->owner;
  const qa_body_state_result body = has_actor ? qa_shared_body_table_read(game->host.bodies, actor) : (qa_body_state_result){0};
  if (!body.has_value) { return_flag(t, flag); return; }
  if (has_actor) {
    const char * value2;
    const char * value3;
    char text[160];
    if (mode_of(t) == 5) (void)snprintf(text, sizeof text, "%s lost the flag!\n", name_of(t, actor));
    else (void)(value2 = name_of(t, actor), value3 = team_name(num(flag, "team")), snprintf(text, sizeof text, "%s lost the %s flag!\n", value2, value3));
    broadcast(t, text);
  }
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = qa_q1_vadd(body.value.origin, QA_Q1MP_V3(0, 0, -24));
  patch.has_velocity = true; patch.velocity = QA_Q1MP_V3(0, 0, 300);
  patch.has_bounds = true; patch.bounds = FLAG_BOUNDS;
  qa_q1_set_body(game, flag, &patch);
  qa_q1mp_world_number(flag, "cnt", 2);
  flag->movement_flags = 256 | 131072; flag->solid = "trigger"; flag->movement = "toss";
  qa_q1mp_world_number(flag, "super_time", game->time + 40);
  qa_q1_link(game, flag);
}
static void flag_think(void *ctx, qa_q1_services *game, qa_q1_actor *flag) {
  qa_q1mp_rogue_teams *t = ctx;
  qa_q1mp_world_later(game, flag, 0.1, "rogue:flag_think");
  const double status = num(flag, "cnt");
  if (status == 0) return;
  if (status == 2) { if (game->time - num(flag, "super_time") > 40) return_flag(t, flag); return; }
  if (status != 1) qa_range_error("Flag in invalid state");
  const qa_actor_id actor = flag->owner;
  const qa_body_state_result body = flag->has_owner ? qa_shared_body_table_read(game->host.bodies, actor) : (qa_body_state_result){0};
  if (!flag->has_owner || !body.has_value || !qa_q1_is_player(game, &actor) || qa_q1_health(game, actor) <= 0) { drop_flag(t, flag); return; }
  const int32_t carried = bits(num(state_of(t, actor), "ctf_flags"));
  const double team = num(flag, "team");
  if ((mode_of(t) == 5 && (carried & 1) == 0) || (team == 5 && (carried & 1) == 0) || (team == 14 && (carried & 2) == 0)) { drop_flag(t, flag); return; }
  if (t->hooks == NULL || t->hooks->player_frame == NULL) qa_range_error("Rogue carried flags require character source frames");
  const double frame = t->hooks->player_frame(t->hooks->ctx, actor);
  static const double OFFSETS[] = {2, 8, 12, 11, 10, 4, 2, 10, 10, 8, 4, 2};
  /* TS offsets[frame - 29] ?? 0: only an exact index selects an entry. */
  const double offset = frame >= 29 && frame <= 40 ? (floor(frame) == frame ? OFFSETS[(size_t)(frame - 29)] : 0)
                        : frame >= 103 && frame <= 106 ? 6 : frame >= 107 && frame <= 118 ? 7 : 0;
  const double distance = 14 + offset;
  const qa_q1_basis basis = qa_q1_make_vectors(game, body.value.angles);
  const qa_vec3 forward = QA_Q1MP_V3(basis.forward.x, basis.forward.y, -basis.forward.z);
  qa_q1_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_q1_vadd(qa_q1_vsub(qa_q1_vadd(body.value.origin, QA_Q1MP_V3(0, 0, -16)), qa_q1_vscale(forward, distance)), qa_q1_vscale(basis.right, 22));
  patch.has_angles = true; patch.angles = qa_q1_vadd(body.value.angles, QA_Q1MP_V3(0, 0, -45));
  qa_q1_set_body(game, flag, &patch);
  qa_q1_link(game, flag);
  qa_q1mp_world_later(game, flag, 0.01, "rogue:flag_think");
}
static void clear_keys(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  qa_q1_services *game = t->game;
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(game->host.actors, actor, &owner)) return;
  static const char *const KEYS[] = {"q1:key/silver", "q1:key/gold"};
  for (size_t i = 0; i < 2; i++)
    (void)qa_shared_inventory_table_consume(game->host.inventory, owner, KEYS[i], qa_shared_inventory_table_count(game->host.inventory, actor, KEYS[i]));
}
static void capture_flag(qa_q1mp_rogue_teams *t, qa_actor_id actor, bool alternate) {
  qa_q1_services *game = t->game;
  qa_q1_actor *state = state_of(t, actor);
  const double team = team_of(t, actor);
  const int32_t carried = bits(num(state, "ctf_flags"));
  char text[160];
  (void)snprintf(text, sizeof text, "%s captured the flag!\n", name_of(t, actor));
  broadcast(t, text);
  clear_keys(t, actor);
  sound(t, actor, "misc/flagcap.wav", true);
  score(t, actor, alternate ? 8 : 15);
  size_t count;
  qa_actor_id *players = players_of(game, &count);
  for (size_t i = 0; i < count; i++) {
    const qa_actor_id player = players[i];
    qa_q1_actor *other = state_of(t, player);
    if (color(t, player) == team) {
      if (!qa_same_actor(actor, player)) score(t, player, alternate ? 4 : 10);
      if (!alternate) {
        if (mode_of(t) != 5 && num(other, "ctf_lastreturnedflag") + 4 > game->time) {
          score(t, player, 1);
          (void)snprintf(text, sizeof text, "%s gets an assist for returning the %s flag!\n", name_of(t, player), team_name(team));
          broadcast(t, text);
        }
        if (num(other, "ctf_lastfraggedcarrier") + 6 > game->time) {
          score(t, player, 2);
          (void)snprintf(text, sizeof text, "%s gets an assist for fragging the flag carrier!\n", name_of(t, player));
          broadcast(t, text);
        }
      }
      if (mode_of(t) != 5) message(t, player, "$qc_your_team_captured", true);
    } else {
      qa_q1mp_world_number(other, "ctf_lasthurtcarrier", -5);
      if (mode_of(t) != 5) message(t, player, "$qc_your_flag_captured", true);
    }
    if (!alternate) qa_q1mp_world_number(other, "ctf_flags", bits(num(other, "ctf_flags")) & ~3);
  }
  free(players);
  size_t flag_count;
  qa_q1_actor **flags = flags_of(game, &flag_count);
  for (size_t i = 0; i < flag_count; i++) {
    qa_q1_actor *flag = flags[i];
    const bool match = alternate ? num(flag, "team") == ((carried & 1) != 0 ? 5 : 14)
                       : mode_of(t) == 5 ? qa_q1mp_streq(flag->classname, "item_flag") : !qa_q1mp_streq(flag->classname, "item_flag");
    if (match) regenerate(t, flag);
  }
  free(flags);
  if (alternate) qa_q1mp_world_number(state, "ctf_flags", carried & ~3);
}
static void flag_touch(void *ctx, qa_q1_services *game, qa_q1_actor *flag, qa_actor_id actor, const qa_vec3 *normal,
                       const qa_touch_surface *surface) {
  double left;
  double right;
  (void)normal; (void)surface;
  qa_q1mp_rogue_teams *t = ctx;
  if (!qa_q1_is_player(game, &actor) || qa_q1_health(game, actor) <= 0 || (left = color(t, actor), right = team_of(t, actor), left != right) || num(flag, "cnt") == 1) return;
  qa_q1_actor *state = state_of(t, actor);
  const int32_t carried = bits(num(state, "ctf_flags"));
  const double team = num(flag, "team");
  if (mode_of(t) != 5) {
    if (mode_of(t) != 4 && mode_of(t) != 6) return;
    if (team == team_of(t, actor)) {
      if (num(flag, "cnt") == 0) {
        if ((team == 5 && (carried & 2) != 0) || (team == 14 && (carried & 1) != 0)) capture_flag(t, actor, false);
        return;
      }
      score(t, actor, 1);
      qa_q1mp_world_number(state, "ctf_lastreturnedflag", game->time);
      char returned[160];
      (void)snprintf(returned, sizeof returned, "%s returned the %s flag!\n", name_of(t, actor), team_name(team));
      broadcast(t, returned);
      sound(t, actor, qa_q1_actor_text(flag, "noise1"), false);
      return_flag(t, flag);
      return;
    }
    if ((carried & 3) != 0) return;
  }
  char text[160];
  if (mode_of(t) == 5) (void)snprintf(text, sizeof text, "%s got the flag!\n", name_of(t, actor));
  else (void)snprintf(text, sizeof text, "%s got the %s flag!\n", name_of(t, actor), team_name(team));
  broadcast(t, text);
  sound(t, actor, qa_q1_actor_text(flag, "noise"), false);
  qa_q1mp_world_number(state, "ctf_flags", carried | (team == 14 ? 2 : 1));
  qa_q1mp_world_number(state, "ctf_flagsince", game->time);
  qa_owned_actor owner;
  if (qa_session_actor_registry_resolve_owned(game->host.actors, actor, &owner)) {
    if (team == 0 || team == 14) (void)qa_shared_inventory_table_give(game->host.inventory, owner, "q1:key/silver", 1);
    if (team == 0 || team == 5) (void)qa_shared_inventory_table_give(game->host.inventory, owner, "q1:key/gold", 1);
  }
  flag->has_owner = true; flag->owner = actor; flag->movement = "noclip"; flag->solid = "none";
  qa_q1mp_world_number(flag, "cnt", 1);
  qa_q1_link(game, flag);
  message(t, actor, mode_of(t) == 5 ? "YOU GOT THE FLAG\n\nTAKE IT TO THEIR BASE\n" : "YOU GOT THE ENEMY FLAG\n\nRETURN TO BASE\n", true);
  size_t count;
  qa_actor_id *players = players_of(game, &count);
  for (size_t i = 0; i < count; i++) if (!qa_same_actor(actor, players[i])) {
    const char *line;
    if (mode_of(t) == 5) line = "$qc_flag_taken";
    else if (team_of(t, players[i]) == team) line = "$qc_your_flag_taken";
    else { (void)snprintf(text, sizeof text, "%s team has the %s flag!\n", team_name(team_of(t, actor)), team_name(team)); line = text; }
    message(t, players[i], line, true);
  }
  free(players);
}
static void base_touch(void *ctx, qa_q1_services *game, qa_q1_actor *base, qa_actor_id actor, const qa_vec3 *normal,
                       const qa_touch_surface *surface) {
  double left;
  double right;
  (void)normal; (void)surface;
  qa_q1mp_rogue_teams *t = ctx;
  if (!qa_q1_is_player(game, &actor) || qa_q1_health(game, actor) <= 0 || (left = color(t, actor), right = team_of(t, actor), left != right)) return;
  const int32_t carried = bits(num(state_of(t, actor), "ctf_flags"));
  const double team = num(base, "team");
  if (mode_of(t) == 5) {
    if (((team == 5 && team_of(t, actor) == 14) || (team == 14 && team_of(t, actor) == 5)) && (carried & 1) != 0) capture_flag(t, actor, false);
    return;
  }
  if (mode_of(t) == 6 && team_of(t, actor) == 1 && (((carried & 1) != 0 && team == 14) || ((carried & 2) != 0 && team == 5))) capture_flag(t, actor, true);
}
void qa_q1mp_teams_confirmed_damage(qa_q1mp_rogue_teams *t, qa_actor_id target, const qa_actor_id *attacker) {
  double left;
  double right;
  if (ctf_of(t) && attacker != NULL && qa_q1_is_player(t->game, &target) && qa_q1_is_player(t->game, attacker) &&
      (bits(num(state_of(t, target), "ctf_flags")) & 3) != 0 && (left = team_of(t, target), right = team_of(t, *attacker), left != right))
    qa_q1mp_world_number(state_of(t, *attacker), "ctf_lasthurtcarrier", t->game->time);
}
static void assists(qa_q1mp_rogue_teams *t, qa_actor_id target, qa_actor_id attacker) {
  qa_q1_services *game = t->game;
  qa_q1_actor *victim = state_of(t, target), *killer = state_of(t, attacker);
  const double team = team_of(t, attacker);
  if ((bits(num(victim, "ctf_flags")) & 3) != 0 && team_of(t, target) != team) {
    qa_q1mp_world_number(killer, "ctf_lastfraggedcarrier", game->time);
    if (num(victim, "ctf_flagsince") + 2 <= game->time) {
      score(t, attacker, 2);
      const qa_q1_msg_arg bonus = {.is_number = true, .number = 2};
      message_args(t, attacker, "$qc_enemy_killed_bonus", false, &bonus, 1);
    } else message(t, attacker, "$qc_enemy_killed_no_bonus", false);
  }
  bool flag_bonus = false, carrier_bonus = false;
  char text[192];
  if (num(victim, "ctf_lasthurtcarrier") + 4 > game->time && (bits(num(killer, "ctf_flags")) & 3) == 0) {
    score(t, attacker, 2);
    carrier_bonus = true;
    (void)snprintf(text, sizeof text, "%s defends %s's flag carrier against an agressive enemy\n", name_of(t, attacker), team_name(team));
    broadcast(t, text);
  }
  const qa_actor_id origins[2] = {attacker, target};
  for (size_t o = 0; o < 2; o++) {
    const qa_actor_id origin_actor = origins[o];
    const qa_body_state_result origin_body = qa_shared_body_table_read(game->host.bodies, origin_actor);
    if (!origin_body.has_value) continue;
    const qa_vec3 origin = origin_body.value.origin;
    size_t count = 0;
    const qa_actor_observation *live = qa_session_actor_registry_observations(game->host.actors, &count);
    qa_actor_observation *observations = malloc((count != 0 ? count : 1) * sizeof *observations);
    if (observations == NULL) qa_range_error("Out of memory");
    if (count != 0) memcpy(observations, live, count * sizeof *observations);
    for (size_t i = count; i-- > 0;) {
      const qa_actor_id actor = observations[i].id;
      const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor);
      qa_q1_actor *entity = qa_q1_get_entity(game, &actor);
      if (!body.has_value || (entity != NULL && qa_q1mp_streq(entity->solid, "none")) ||
          qa_q1_length(qa_q1_vsub(qa_q1_vadd(body.value.origin, qa_q1_vscale(qa_q1_vadd(body.value.bounds.min, body.value.bounds.max), 0.5)), origin)) > 400)
        continue;
      if (qa_q1_is_player(game, &actor) && team_of(t, actor) == team && (bits(num(state_of(t, actor), "ctf_flags")) & 3) != 0 &&
          !qa_same_actor(actor, attacker) && !carrier_bonus) {
        score(t, attacker, 1);
        carrier_bonus = true;
        (void)snprintf(text, sizeof text, "%s defends %s's flag carrier\n", name_of(t, attacker), team_name(team));
        broadcast(t, text);
      }
      const char *classname = game->host.classname(game->host.ctx, actor);
      if ((team == 5 && qa_q1mp_streq(classname, "item_flag_team1")) || (team == 14 && qa_q1mp_streq(classname, "item_flag_team2")) ||
          (qa_q1mp_streq(classname, "item_flag") && (!qa_same_actor(origin_actor, target) || !flag_bonus))) {
        score(t, attacker, 1);
        flag_bonus = true;
        if (mode_of(t) == 5) (void)snprintf(text, sizeof text, "%s defends the flag\n", name_of(t, attacker));
        else (void)snprintf(text, sizeof text, "%s defends the %s flag\n", name_of(t, attacker), team_name(team));
        broadcast(t, text);
      }
    }
    free(observations);
  }
}
void qa_q1mp_teams_drop_carried_flag(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  if (!ctf_of(t)) return;
  qa_q1_actor *state = state_of(t, actor);
  const int32_t carried = bits(num(state, "ctf_flags"));
  const char *classname = mode_of(t) == 5 && (carried & 1) != 0 ? "item_flag" : (carried & 1) != 0 ? "item_flag_team1"
                          : (carried & 2) != 0 ? "item_flag_team2" : "";
  size_t count;
  qa_q1_actor **flags = flags_of(t->game, &count);
  qa_q1_actor *flag = find_flag(flags, count, classname);
  free(flags);
  if (flag != NULL) { qa_q1mp_world_number(state, "ctf_flags", carried & ~3); drop_flag(t, flag); }
}
void qa_q1mp_teams_player_died(qa_q1mp_rogue_teams *t, qa_actor_id actor, const qa_actor_id *attacker) {
  qa_q1_actor *state = state_of(t, actor);
  qa_q1mp_world_number(state, "ctf_killed", num(state, "ctf_killed") == 2 ? 0 : 1);
  if (!ctf_of(t)) return;
  const int32_t carried = bits(num(state, "ctf_flags"));
  if (attacker != NULL && qa_q1_is_player(t->game, attacker) && !qa_same_actor(actor, *attacker)) assists(t, actor, *attacker);
  if ((carried & 3) != 0) {
    size_t count;
    qa_actor_id *players = players_of(t->game, &count);
    for (size_t i = 0; i < count; i++)
      if (mode_of(t) == 5 || ((carried & 1) != 0 && team_of(t, players[i]) == 5) || ((carried & 2) != 0 && team_of(t, players[i]) == 14))
        qa_q1mp_world_number(state_of(t, players[i]), "ctf_lasthurtcarrier", -10);
    free(players);
  }
  qa_q1mp_teams_drop_carried_flag(t, actor);
}

static void on_release(void *ctx, qa_owned_actor actor) {
  qa_q1mp_rogue_teams *t = ctx;
  for (size_t i = 0; i < t->state_count; i++) if (qa_same_owned_actor(t->states[i].actor, actor)) {
    qa_q1_actor *state = t->states[i].state;
    memmove(&t->states[i], &t->states[i + 1], (t->state_count - i - 1) * sizeof *t->states);
    t->state_count--;
    if (qa_q1_live(t->game, state)) qa_q1_remove(t->game, state);
    return;
  }
}
static bool capture_travel(void *ctx, qa_q1_services *game, qa_q1_player_state *player, uint8_t **out, size_t *out_len) {
  (void)game;
  uint8_t *bytes = malloc(1);
  if (bytes == NULL) return false;
  bytes[0] = (uint8_t)(bits(num(state_of(ctx, player->actor.id), "steam")) & 255);
  *out = bytes; *out_len = 1;
  return true;
}
static void restore_travel(void *ctx, qa_q1_services *game, qa_q1_player_state *player, const uint8_t *bytes, size_t len) {
  (void)game;
  qa_q1mp_rogue_teams *t = ctx;
  const int team = (int8_t)(len > 0 ? bytes[0] : 255); /* (bytes[0] ?? 255) << 24 >> 24 */
  if (ctf_of(t) && team >= 0 && legal(t, team)) qa_q1mp_world_number(state_of(t, player->actor.id), "steam", team);
}
static bool armor_allowed(void *ctx, qa_damage_request request, double damage, qa_combat_state target, const qa_combat_state *attacker) {
  double left;
  double right;
  (void)damage; (void)target; (void)attacker;
  qa_q1mp_rogue_teams *t = ctx;
  const qa_actor_id *source = request.attack.attacker;
  return !ctf_of(t) || (source != NULL && qa_same_actor(*source, request.target)) ||
         (left = qa_q1mp_teams_team(t, source), right = team_of(t, request.target), left != right) || (bits(gamecfg_of(t)) & 2) != 0;
}
static bool before_health(void *ctx, qa_damage_request request, double damage, qa_combat_state target, const qa_combat_state *attacker) {
  double left2;
  double right2;
  double left;
  double right;
  (void)damage; (void)target; (void)attacker;
  qa_q1mp_rogue_teams *t = ctx;
  const qa_actor_id *source = request.attack.attacker;
  if (mode_of(t) <= 0) return true;
  return (mode_of(t) != 1 || (left2 = qa_q1mp_teams_team(t, source), right2 = team_of(t, request.target), left2 != right2)) &&
         (!ctf_of(t) || (source != NULL && qa_same_actor(*source, request.target)) ||
          (left = qa_q1mp_teams_team(t, source), right = team_of(t, request.target), left != right) || (bits(gamecfg_of(t)) & 4) != 0);
}
static void flag_place(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  e->movement_flags = 256 | 131072; e->solid = "trigger"; e->movement = "toss";
  e->touch = qa_q1_callback_registry_touch(game->named, e, "rogue:flag_touch");
  e->mangle = qa_q1_get_body(game, e).angles;
  e->effects = qa_js_bit_or(e->effects, 8);
  qa_q1mp_world_number(e, "cnt", 0);
  if (!drop_floor(game, e)) { qa_q1_remove(game, e); return; }
  qa_q1mp_world_vector(e, "oldorigin", qa_q1_get_body(game, e).origin);
  qa_q1mp_world_later(game, e, 0.1, "rogue:flag_think");
}
static void flag_spawn(qa_q1mp_rogue_teams *t, qa_q1_services *game, qa_q1_actor *e, double team, double skin) {
  if (team == 0 ? mode_of(t) != 5 : game->options.deathmatch == 0 || !ctf_of(t)) { qa_q1_remove(game, e); return; }
  qa_q1mp_world_number(e, "team", team);
  e->skin = skin;
  flag_base(t, e, team == 0 ? "item_flagbase" : team == 5 ? "item_flagbase_team1" : "item_flagbase_team2");
  if (team != 0 && mode_of(t) == 5) { qa_q1_remove(game, e); return; }
  qa_q1mp_set_string(&e->model, "progs/ctfmodel.mdl");
  qa_q1mp_field_text(e, "noise", "misc/flagtk.wav");
  qa_q1mp_field_text(e, "noise1", "misc/flagret.wav");
  qa_q1_set_bounds(game, e, FLAG_BOUNDS);
  qa_q1mp_world_later(game, e, 0.2, "rogue:flag_place");
}
static void flag_team1_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) { flag_spawn(ctx, game, e, 5, 0); }
static void flag_team2_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) { flag_spawn(ctx, game, e, 14, 1); }
static void flag_neutral_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) { flag_spawn(ctx, game, e, 0, 2); }
static void team_start_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) { (void)ctx; (void)game; (void)e; }
static void ctf_wall_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  if (ctf_of(ctx)) qa_q1mp_world_brush(game, e); else qa_q1_remove(game, e);
}
static void teleport_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  if ((bits(e->spawnflags) & 4) != 0 && !ctf_of(ctx)) qa_q1_remove(game, e);
  else qa_q1_spawn_map_actor(game, e);
}
void qa_q1mp_rogue_teams_init(qa_q1mp_rogue_teams *t, qa_q1_services *game, const qa_q1mp_world_hooks *hooks) {
  memset(t, 0, sizeof *t);
  t->game = game; t->hooks = hooks;
  t->release_subscription = qa_session_actor_registry_on_release(game->host.actors, on_release, t);
  t->extension = (qa_q1_player_extension){.ctx = t, .id = "rogue:team", .capture_travel = capture_travel, .restore_travel = restore_travel};
  qa_q1_register_player_extension(game, &t->extension);
  qa_q1_set_base_team_health(game, false);
  qa_q1_damage_source_effects effects = {0};
  effects.ctx = t; effects.armor_allowed = armor_allowed; effects.before_health = before_health;
  qa_q1_register_damage_source_effects(game, "rogue:teams", effects);
  qa_q1_callback_registry_register(game->named, "rogue:flag_place", (qa_q1_callback_handlers){.ctx = t, .action = flag_place});
  qa_q1_callback_registry_register(game->named, "rogue:flag_think", (qa_q1_callback_handlers){.ctx = t, .action = flag_think});
  qa_q1_callback_registry_register(game->named, "rogue:flag_touch", (qa_q1_callback_handlers){.ctx = t, .touch = flag_touch});
  qa_q1_callback_registry_register(game->named, "rogue:flagbase_touch", (qa_q1_callback_handlers){.ctx = t, .touch = base_touch});
  qa_q1_register_spawn(game, "item_flag_team1", flag_team1_spawn, t);
  qa_q1_register_spawn(game, "item_flag_team2", flag_team2_spawn, t);
  qa_q1_register_spawn(game, "item_flag", flag_neutral_spawn, t);
  qa_q1_register_spawn(game, "info_player_team1", team_start_spawn, t);
  qa_q1_register_spawn(game, "info_player_team2", team_start_spawn, t);
  qa_q1_register_spawn(game, "func_ctf_wall", ctf_wall_spawn, t);
  qa_q1_register_spawn(game, "trigger_teleport", teleport_spawn, t);
}
void qa_q1mp_rogue_teams_destroy(qa_q1mp_rogue_teams *t) {
  qa_session_actor_registry_off_release(t->game->host.actors, t->release_subscription);
  free(t->states);
  t->states = NULL; t->state_count = t->state_capacity = 0;
}
