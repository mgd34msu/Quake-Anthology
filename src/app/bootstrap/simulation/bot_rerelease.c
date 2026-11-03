/* Port of src/app/bootstrap/simulation/bot-rerelease.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_rerelease.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/bot_commands.h"
#include "qa/bots/behavior/rerelease/chat_text.h"
#include "qa/bots/behavior/rerelease/checkpoint.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"

typedef struct connection {
  struct qa_app_sim_rerelease_bots *owner;
  qa_app_sim_bot_client client; /* client, reliable (owned), userinfo (owned) */
  qa_owned_actor actor;
  qa_rerelease_bot_behavior behavior;
  qa_app_sim_rerelease_bot_world *world;
  char *name, *skill;
  int64_t slot;
  struct { double number; } selection;
  double sequence;
} connection;
typedef struct observation { double number; qa_actor_id actor; } observation;
struct qa_app_sim_rerelease_bots {
  qa_app_sim_bots_options options;
  const qa_app_sim_application_bot_assets *assets;
  qa_cvar_registry *configuration;
  qa_app_sim_bot_navigation *navigation;
  connection **connections; size_t connection_count, connection_capacity; /* TS Map insertion order */
  observation *observations; size_t observation_count, observation_capacity; /* both TS maps, one row per actor */
  double next_observation, elapsed_milliseconds;
  qa_bot_sound *heard; size_t heard_count, heard_capacity;
  bool closed;
  qa_app_sim_rerelease_frame_snapshot snapshot;
  qa_app_sim_rerelease_bot_objectives_state *objectives;
  int64_t release_subscription;
  qa_app_sim_bot_service service;
  qa_actor_command *commands; qa_arsenal_intent *arsenals; size_t command_capacity;
  qa_app_sim_bot_client *client_views; size_t client_view_capacity;
};
typedef struct qa_app_sim_rerelease_bots bots_t;

static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static bool starts(const char *a, const char *prefix) { return a != NULL && strncmp(a, prefix, strlen(prefix)) == 0; }
static noreturn void fail(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
static void *grow(void *p, size_t *capacity, size_t count, size_t size) {
  if (count < *capacity) return p;
  *capacity = *capacity != 0 ? *capacity * 2 : 8;
  void *q = realloc(p, *capacity * size);
  if (q == NULL) qa_range_error("Out of memory");
  return q;
}
static char *xstrdup(const char *s) { char *p = strdup(s != NULL ? s : ""); if (p == NULL) qa_range_error("Out of memory"); return p; }
static const qa_app_sim_bot_simulation *sim_of(bots_t *b) { return b->options.simulation; }
static bool ascii_equal_ignore_case(const char *a, const char *b) {
  for (; *a != '\0' && *b != '\0'; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
  return *a == *b;
}
static connection *connection_at(bots_t *b, int64_t slot) {
  for (size_t i = 0; i < b->connection_count; i++) if (b->connections[i]->slot == slot) return b->connections[i];
  return NULL;
}
static connection *connection_of_actor(bots_t *b, qa_actor_id actor) {
  for (size_t i = 0; i < b->connection_count; i++) if (qa_same_actor(b->connections[i]->actor.id, actor)) return b->connections[i];
  return NULL;
}
static void connection_free(connection *c) {
  qa_rerelease_bot_behavior_destroy(&c->behavior);
  qa_app_sim_rerelease_bot_world_destroy(c->world);
  if (c->client.reliable != NULL) qa_q3_reliable_ring_destroy(c->client.reliable);
  free((char *)c->client.userinfo); free(c->name); free(c->skill); free(c);
}
static void remove_connection(bots_t *b, int64_t slot) {
  for (size_t i = 0; i < b->connection_count; i++) {
    if (b->connections[i]->slot != slot) continue;
    connection_free(b->connections[i]);
    memmove(&b->connections[i], &b->connections[i + 1], (b->connection_count - i - 1) * sizeof *b->connections);
    b->connection_count--; return;
  }
}
static const qa_character_entry *characters(bots_t *b, size_t *out_count) {
  *out_count = b->assets->knowledge->chars.entry_count; return b->assets->knowledge->chars.entries;
}
static const char *default_skill(bots_t *b) {
  const qa_bot_settings_result *settings = &b->assets->knowledge->settings_r;
  double index = qa_js_min((double)settings->skill_count - 1, sim_of(b)->skill + 1);
  const qa_bot_skill_settings *selected = index >= 0 && index < (double)settings->skill_count && index == trunc(index) ? &settings->skills[(size_t)index]
      : settings->skill_count != 0 ? &settings->skills[0] : NULL;
  if (selected == NULL) fail("Native bot assets contain no source skill settings");
  return selected->skill;
}
/* TS name(saved): the userinfo "name" field, else the first character. */
static char *name_of(bots_t *b, const char *userinfo) {
  const char *text = userinfo != NULL ? userinfo : "";
  char *copy = xstrdup(text);
  /* fields = text.split("\\"); index = fields.indexOf("name") */
  size_t field_count = 1;
  for (const char *p = copy; *p != '\0'; p++) if (*p == '\\') field_count++;
  char **fields = calloc(field_count, sizeof *fields);
  if (fields == NULL) qa_range_error("Out of memory");
  size_t n = 0; fields[n++] = copy;
  for (char *p = copy; *p != '\0'; p++) if (*p == '\\') { *p = '\0'; fields[n++] = p + 1; }
  long index = -1;
  for (size_t i = 0; i < n && index < 0; i++) if (strcmp(fields[i], "name") == 0) index = (long)i;
  char *result;
  if (index < 0) { size_t count; const qa_character_entry *c = characters(b, &count); result = xstrdup(count != 0 ? c[0].name : "Bot"); }
  else result = xstrdup((size_t)index + 1 < n ? fields[index + 1] : "Bot");
  free(fields); free(copy);
  return result;
}
static int64_t identify(void *ctx, qa_actor_id actor) {
  bots_t *b = ctx;
  for (size_t i = 0; i < b->observation_count; i++)
    if (b->observations[i].actor.slot == actor.slot && b->observations[i].actor.generation == actor.generation) return (int64_t)b->observations[i].number;
  double number = b->next_observation++;
  b->observations = grow(b->observations, &b->observation_capacity, b->observation_count, sizeof *b->observations);
  b->observations[b->observation_count].number = number; b->observations[b->observation_count].actor = actor; b->observation_count++;
  return (int64_t)number;
}
/* The table otherwise grows with every actor ever observed on the map. */
static void on_release(void *ctx, qa_owned_actor released) {
  bots_t *b = ctx;
  for (size_t i = 0; i < b->observation_count; i++) {
    if (b->observations[i].actor.slot != released.id.slot || b->observations[i].actor.generation != released.id.generation) continue;
    memmove(&b->observations[i], &b->observations[i + 1], (b->observation_count - i - 1) * sizeof *b->observations);
    b->observation_count--; return;
  }
}
static bool is_bot(void *ctx, qa_actor_id actor) { return connection_of_actor(ctx, actor) != NULL; }
static double elapsed_of(void *ctx) { bots_t *b = ctx; return b->elapsed_milliseconds; }
static const qa_bot_sound *sounds_of(void *ctx, size_t *out_count) { bots_t *b = ctx; *out_count = b->heard_count; return b->heard; }

/* ---- behavior callbacks (ctx: the connection) ---- */
static double cb_time(void *ctx) { connection *c = ctx; return sim_of(c->owner)->time_seconds(sim_of(c->owner)->ctx); }
static void cb_pre_think(void *ctx, const char *name, qa_rerelease_orders orders) { (void)ctx; (void)name; (void)orders; } /* TS () => {} */
static void cb_post_think(void *ctx, const char *name, qa_bot_usercmd command) { (void)ctx; (void)name; (void)command; } /* TS () => {} */
typedef struct lookup_ctx { bots_t *owner; char *last; } lookup_ctx;
static const char *localization_lookup(void *ctx, const char *key) {
  lookup_ctx *l = ctx;
  free(l->last);
  l->last = qa_localization_table_lookup((qa_localization_table *)&l->owner->assets->localization, key, NULL, 0);
  return l->last;
}
static void cb_chat(void *ctx, qa_bot_chat_event event) {
  connection *self = ctx;
  bots_t *b = self->owner;
  connection *c = connection_at(b, self->slot);
  if (c == NULL) fail("Native bot chat lost its admitted client");
  lookup_ctx lookup = {b, NULL};
  char *text;
  if (is(b->assets->source, "q1-rerelease")) {
    text = qa_q1_bot_chat_text(event.locstring, localization_lookup, &lookup, qa_xorshift32_as_random(&c->behavior.random));
    if (text == NULL) qa_range_error("Out of memory");
  } else {
    const char *found = localization_lookup(&lookup, event.locstring);
    text = xstrdup(found != NULL ? found : event.locstring);
  }
  free(lookup.last);
  const char *args[1] = {text};
  sim_of(b)->player_command(sim_of(b)->ctx, self->actor.id, event.team_only ? "say_team" : "say", args, 1);
  free(text);
}
static void cb_select_weapon(void *ctx, double number) { connection *c = ctx; c->selection.number = number; }
static double cb_weapon_impulse(void *ctx, double number) { connection *c = ctx; c->selection.number = number; return 0; }
static bool cb_human_teammate_near(void *ctx) {
  connection *self = ctx;
  bots_t *b = self->owner;
  const qa_app_sim_bot_simulation *sim = sim_of(b);
  const qa_app_sim_rerelease_bot_objectives *o = &b->objectives->objectives;
  size_t count = 0;
  const qa_actor_id *players = sim->players(sim->ctx, &count);
  qa_actor_id *copy = calloc(count != 0 ? count : 1, sizeof *copy);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, players, count * sizeof *copy);
  bool near = false;
  for (size_t i = 0; i < count && !near; i++) {
    qa_actor_id other = copy[i];
    if (is_bot(b, other) || o->team(o->ctx, other) != o->team(o->ctx, self->actor.id)) continue;
    qa_body_state_result body = qa_shared_body_table_read(sim->bodies, other), own = qa_shared_body_table_read(sim->bodies, self->actor.id);
    near = body.has_value && own.has_value && qa_js_hypot3(body.value.origin.x - own.value.origin.x, body.value.origin.y - own.value.origin.y,
        body.value.origin.z - own.value.origin.z) < 256;
  }
  free(copy);
  return near;
}

static connection *connect_client(bots_t *b, qa_session_client *client, qa_q3_server_reliable_commands **reliable, const char *userinfo,
    const char *name, const char *skill, const qa_owned_actor *restored_actor) {
  const qa_app_sim_bot_simulation *sim = sim_of(b);
  qa_client_id id = qa_session_client_id(client);
  if (qa_session_client_is_closed(client) || connection_at(b, id.slot) != NULL) fail("Native bot client is closed or already admitted");
  size_t player_count = 0;
  const qa_actor_id *players = sim->players(sim->ctx, &player_count);
  bool has_existing = false; qa_actor_id existing = {0};
  for (size_t i = 0; i < player_count && !has_existing; i++) {
    qa_app_sim_movement_player *movement = sim->movement_player(sim->ctx, players[i]);
    if (movement != NULL && qa_same_client(movement->client, id)) { existing = players[i]; has_existing = true; }
  }
  /* Fresh admission goes through prepareBotClient so source bot identity
   * (e.g. Q2 SVF_BOT) is applied exactly as for the shared transport. */
  qa_owned_actor actor;
  if (restored_actor != NULL) actor = *restored_actor;
  else if (!has_existing) actor = sim->prepare_bot_client(sim->ctx, id);
  else if (!qa_session_actor_registry_resolve_owned(sim->actors, existing, &actor)) fail("Native bot admission has no shared actor");
  qa_app_sim_movement_player *player = sim->movement_player(sim->ctx, actor.id);
  if (player == NULL) fail("Native bot admission lost selected movement");
  size_t character_count = 0;
  const qa_character_entry *all = characters(b, &character_count), *character = NULL;
  for (size_t i = 0; i < character_count && character == NULL; i++) if (ascii_equal_ignore_case(all[i].name, name)) character = &all[i];
  const char *definition = b->assets->files.provenance != NULL ? b->assets->files.provenance(b->assets->files.files.ctx) : NULL;
  if (definition == NULL) fail("Native bot assets require saved provenance");
  connection *c = calloc(1, sizeof *c);
  if (c == NULL) qa_range_error("Out of memory");
  c->owner = b; c->slot = id.slot; c->actor = actor;
  c->client.client = client; c->client.reliable = *reliable; *reliable = NULL; /* owned by the connection */
  if (userinfo != NULL) c->client.userinfo = xstrdup(userinfo);
  else {
    size_t length = strlen(name) + 7;
    char *text = malloc(length);
    if (text == NULL) qa_range_error("Out of memory");
    snprintf(text, length, "\\name\\%s", name);
    c->client.userinfo = text;
  }
  c->name = xstrdup(name); c->skill = xstrdup(skill);
  qa_rerelease_profile_options profile;
  memset(&profile, 0, sizeof profile);
  profile.definition = definition;
  profile.source = is(b->assets->source, "q1-rerelease") ? QA_RERELEASE_Q1 : QA_RERELEASE_Q2;
  profile.knowledge = b->assets->knowledge; profile.skill = skill;
  profile.seed = restored_actor == NULL ? (int64_t)qa_app_sim_source_random_next_integer(sim->random) : 0;
  profile.game_mode = b->objectives->objectives.mode(b->objectives);
  profile.character = character;
  profile.max_health = 100; profile.run_speed = starts(player->profile.kind, "q2") ? 300 : 320; profile.walk_speed = 160;
  double gravity = qa_app_sim_shared_physics_gravity(sim->physics);
  profile.movement.gravity = gravity; profile.movement.jump_velocity = 270; profile.movement.jump_air_seconds = 540 / gravity;
  profile.movement.maximum_landing_rise = 18; profile.movement.start_above = 56;
  profile.movement.body_mins = player->standing_bounds.min; profile.movement.body_maxs = player->standing_bounds.max;
  profile.callbacks.ctx = c;
  profile.callbacks.time = cb_time; profile.callbacks.pre_think = cb_pre_think; profile.callbacks.post_think = cb_post_think;
  profile.callbacks.chat = cb_chat; profile.callbacks.select_weapon = cb_select_weapon; profile.callbacks.weapon_impulse = cb_weapon_impulse;
  profile.callbacks.human_teammate_near = cb_human_teammate_near;
  bool ok = qa_rerelease_bot_behavior_init(&c->behavior, profile);
  qa_bot_game_mode_destroy(&profile.game_mode);
  if (!ok) qa_range_error("Out of memory");
  qa_app_sim_rerelease_world_options world;
  world.simulation = sim; world.navigation = b->navigation; world.knowledge = b->assets->knowledge;
  world.objectives = &b->objectives->objectives; world.ctx = b;
  world.is_bot = is_bot; world.identify = identify; world.elapsed = elapsed_of; world.sounds = sounds_of;
  world.snapshot = &b->snapshot;
  c->world = qa_app_sim_create_rerelease_bot_world(&world, actor.id);
  if (c->world == NULL) qa_range_error("Out of memory");
  b->connections = grow(b->connections, &b->connection_capacity, b->connection_count, sizeof *b->connections);
  b->connections[b->connection_count++] = c;
  if (restored_actor == NULL) {
    qa_bot_game_mode mode = b->objectives->objectives.mode(b->objectives);
    bool cosmetic = !(mode.has_has_teams && mode.has_teams) && character != NULL;
    qa_bot_game_mode_destroy(&mode);
    qa_app_sim_player_identity identity;
    memset(&identity, 0, sizeof identity);
    identity.name = name;
    if (cosmetic && is(b->assets->source, "q2-rerelease") && character->skin != NULL && character->skin[0] != '\0') identity.skin = character->skin;
    if (cosmetic && is(b->assets->source, "q1-rerelease")) {
      identity.has_shirt = true; identity.shirt = character->shirt_color;
      identity.has_pants = true; identity.pants = character->pants_color;
    }
    sim->set_source_player_identity(sim->ctx, actor.id, &identity);
    b->objectives->objectives.admit(b->objectives, actor.id);
  }
  return c;
}

typedef struct add_attempt { bots_t *b; qa_session_client *client; qa_q3_server_reliable_commands *reliable; const char *name, *skill; } add_attempt;
static void add_attempt_run(void *ctx) {
  add_attempt *a = ctx;
  connect_client(a->b, a->client, &a->reliable, NULL, a->name, a->skill, NULL);
}
typedef struct cleanup_attempt { bots_t *b; qa_client_id id; int step; } cleanup_attempt;
static void cleanup_disconnect(void *ctx) {
  cleanup_attempt *a = ctx;
  const qa_app_sim_bot_simulation *sim = sim_of(a->b);
  size_t count = 0;
  const qa_actor_id *players = sim->players(sim->ctx, &count);
  for (size_t i = 0; i < count; i++) {
    qa_app_sim_movement_player *movement = sim->movement_player(sim->ctx, players[i]);
    if (movement != NULL && qa_same_client(movement->client, a->id)) { sim->disconnect_player(sim->ctx, players[i]); return; }
  }
}
static void cleanup_close(void *ctx) { cleanup_attempt *a = ctx; qa_engine_session_close_client(a->b->options.session, a->id); }
static void add(bots_t *b, const char *name, const char *skill) {
  const qa_app_sim_bot_simulation *sim = sim_of(b);
  /* A session client need not currently be a simulation player (e.g. a
   * disconnected human), so session occupancy decides free slots. */
  size_t identity_count = 0;
  const qa_client_id *identities = sim->client_identities(sim->ctx, &identity_count);
  for (int64_t slot = 0; slot < (int64_t)sim->max_clients; slot++) {
    bool occupied = false;
    for (size_t i = 0; i < identity_count && !occupied; i++) occupied = identities[i].slot == slot;
    if (occupied || qa_engine_session_client_at(b->options.session, slot) != NULL) continue;
    qa_session_client *client = qa_engine_session_create_client(b->options.session, slot);
    add_attempt attempt = {b, client, NULL, name, skill};
    if (!qa_q3_server_reliable_commands_init(&attempt.reliable)) qa_range_error("Out of memory");
    qa_caught_error error;
    if (qa_try(add_attempt_run, &attempt, &error)) return;
    if (attempt.reliable != NULL) qa_q3_reliable_ring_destroy(attempt.reliable);
    qa_client_id id = qa_session_client_id(client);
    cleanup_attempt cleanup = {b, id, 0};
    qa_caught_error first, second;
    bool disconnect_failed = !qa_try(cleanup_disconnect, &cleanup, &first);
    connection *partial = connection_at(b, id.slot);
    if (partial != NULL) remove_connection(b, id.slot);
    bool close_failed = !qa_try(cleanup_close, &cleanup, &second);
    if (disconnect_failed || close_failed)
      qa_raise("AggregateError", 0, 0, "Native bot admission and cleanup failed: %s%s%s%s%s", error.message,
          disconnect_failed ? "; " : "", disconnect_failed ? first.message : "", close_failed ? "; " : "", close_failed ? second.message : "");
    qa_rethrow(&error);
  }
  b->options.print(b->options.ctx, "Unable to add bot: server is full\n");
}

static void console_command(void *ctx, const char *const *argv, size_t argc) {
  bots_t *b = ctx;
  char command[64] = "undefined";
  if (argc > 0) {
    size_t n = 0;
    for (; argv[0][n] != '\0' && n + 1 < sizeof command; n++) command[n] = (char)tolower((unsigned char)argv[0][n]);
    command[n] = '\0';
  }
  size_t character_count = 0;
  const qa_character_entry *names = characters(b, &character_count);
  if (argc > 0 && strcmp(command, "addbot") == 0) {
    size_t index = b->connection_count % (character_count > 1 ? character_count : 1);
    const char *name = argc > 1 ? argv[1] : index < character_count ? names[index].name : "Bot";
    const char *raw = argc > 2 ? argv[2] : NULL;
    double numeric = raw == NULL ? NAN : qa_js_string_to_number(raw);
    const qa_bot_settings_result *settings = &b->assets->knowledge->settings_r;
    const char *skill;
    if (isfinite(numeric)) {
      double at = qa_js_max(0, qa_js_min((double)settings->skill_count - 1, trunc(numeric)));
      skill = at >= 0 && at < (double)settings->skill_count ? settings->skills[(size_t)at].skill : default_skill(b);
    } else skill = raw != NULL ? raw : default_skill(b);
    char *name_copy = xstrdup(name), *skill_copy = xstrdup(skill);
    add(b, name_copy, skill_copy);
    free(name_copy); free(skill_copy);
  } else if (argc > 0 && strcmp(command, "botlist") == 0) {
    for (size_t i = 0; i < character_count; i++) {
      size_t length = strlen(names[i].name) + 2;
      char *line = malloc(length);
      if (line == NULL) qa_range_error("Out of memory");
      snprintf(line, length, "%s\n", names[i].name);
      b->options.print(b->options.ctx, line);
      free(line);
    }
  } else qa_raise("Error", 0, 0, "Unknown native bot command %s", argc > 0 ? command : "undefined");
}
static qa_user_command command_of(bots_t *b, qa_bot_usercmd source, connection *c) {
  const qa_app_sim_bot_simulation *sim = sim_of(b);
  qa_app_sim_movement_player *player = sim->movement_player(sim->ctx, c->actor.id);
  if (player == NULL) fail("Native bot lost command player");
  return qa_app_sim_rerelease_bot_command(source, player->profile.kind, b->elapsed_milliseconds, trunc(sim->time_seconds(sim->ctx) * 1000));
}
typedef struct frame_pass { bots_t *b; size_t n; } frame_pass;
static void frame_commands(void *ctx) {
  frame_pass *pass = ctx;
  bots_t *b = pass->b;
  const qa_app_sim_bot_simulation *sim = sim_of(b);
  size_t n = 0;
  for (size_t i = 0; i < b->connection_count; i++) {
    connection *c = b->connections[i];
    qa_app_sim_movement_player *player = sim->movement_player(sim->ctx, c->actor.id);
    if (player == NULL || !qa_session_actor_registry_is_live(sim->actors, c->actor.id)) fail("Native bot command targets a retired actor");
    qa_bot_game_mode mode = b->objectives->objectives.mode(b->objectives);
    bool ok = qa_bot_brain_set_game_mode(&c->behavior.brain, mode);
    qa_bot_game_mode_destroy(&mode);
    if (!ok) qa_range_error("Out of memory");
    qa_vec3 objective;
    bool has_objective = b->objectives->objectives.goal(b->objectives, c->actor.id, &objective);
    qa_rerelease_bot_behavior_set_objective_goal(&c->behavior, has_objective ? &objective : NULL);
    qa_bot_usercmd source = qa_rerelease_bot_behavior_think(&c->behavior, &c->world->world);
    qa_actor_command *out = &b->commands[n];
    memset(out, 0, sizeof *out);
    out->command = command_of(b, source, c);
    out->actor = c->actor.id;
    out->source.kind = QA_COMMAND_SOURCE_BOT;
    out->source.provider = is(b->assets->source, "q1-rerelease") ? "q1:bot" : "q2:bot";
    out->sequence = c->sequence++;
    qa_arsenal_intent *arsenal = &b->arsenals[n];
    memset(arsenal, 0, sizeof *arsenal);
    arsenal->provider = player->arsenal.provider;
    arsenal->weapon = qa_app_sim_native_weapon_item(sim, c->actor.id, b->assets->knowledge, c->selection.number);
    arsenal->use_holdable = false;
    out->arsenal = arsenal;
    n++;
  }
  pass->n = n;
}
static const qa_actor_command *frame(void *ctx, double milliseconds, double elapsed, size_t *out_count) {
  (void)milliseconds; /* TS frame(_milliseconds, elapsed) */
  bots_t *b = ctx;
  if (b->closed) fail("Native bot transport is closed");
  b->elapsed_milliseconds = elapsed;
  const qa_app_sim_bot_simulation *sim = sim_of(b);
  double minimum = qa_js_min(sim->max_clients, qa_js_max(0, trunc(qa_cvar_registry_variable_value(b->configuration, "bot_minplayers"))));
  size_t identity_count = 0;
  sim->client_identities(sim->ctx, &identity_count);
  if (qa_cvar_registry_variable_value(b->configuration, "bot_enable") != 0 && (double)identity_count < minimum) {
    size_t character_count = 0;
    const qa_character_entry *names = characters(b, &character_count);
    size_t index = b->connection_count % (character_count > 1 ? character_count : 1);
    char *name = xstrdup(index < character_count ? names[index].name : "Bot"), *skill = xstrdup(default_skill(b));
    add(b, name, skill);
    free(name); free(skill);
  }
  if (b->command_capacity < b->connection_count) {
    b->command_capacity = b->connection_count;
    qa_actor_command *commands = realloc(b->commands, b->command_capacity * sizeof *commands);
    qa_arsenal_intent *arsenals = realloc(b->arsenals, b->command_capacity * sizeof *arsenals);
    if (commands == NULL || arsenals == NULL) qa_range_error("Out of memory");
    b->commands = commands; b->arsenals = arsenals;
  }
  /* TS snapshot.begin(); try { ... } finally { snapshot.end(); } */
  frame_pass pass = {b, 0};
  qa_caught_error error;
  qa_app_sim_rerelease_frame_snapshot_begin(&b->snapshot);
  bool completed = qa_try(frame_commands, &pass, &error);
  qa_app_sim_rerelease_frame_snapshot_end(&b->snapshot);
  if (!completed) qa_rethrow(&error);
  size_t n = pass.n;
  b->heard_count = 0;
  *out_count = n;
  return b->commands;
}
static void receive(void *ctx, const qa_app_sim_presentation_event *events, size_t count) {
  bots_t *b = ctx;
  const qa_app_sim_bot_simulation *sim = sim_of(b);
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_presentation_event *event = &events[i];
    bool q1 = is(event->kind, "q1") && event->q1 != NULL, q2 = is(event->kind, "q2") && event->q2 != NULL;
    if (!(q1 && is(event->q1->kind, "sound")) && !(q2 && is(event->q2->kind, "sound"))) continue;
    bool has_origin = false, has_actor = false;
    qa_vec3 origin = {0, 0, 0};
    qa_actor_id actor = {0};
    double volume = 0;
    if (q1) {
      const qa_q1_event *source = event->q1;
      has_origin = source->has_origin; origin = source->origin;
      has_actor = source->has_actor; actor = source->actor; volume = source->volume;
    } else {
      const qa_q2_presentation_event *source = event->q2;
      has_origin = true; origin = source->origin; /* Q2PresentationEvent carries its sound origin */
      has_actor = source->has_actor; actor = source->actor; volume = source->volume;
    }
    if (!has_origin && has_actor) {
      qa_body_state_result body = qa_shared_body_table_read(sim->bodies, actor);
      if (body.has_value) { has_origin = true; origin = body.value.origin; }
    }
    if (!has_origin) continue;
    b->heard = grow(b->heard, &b->heard_capacity, b->heard_count, sizeof *b->heard);
    qa_bot_sound *sound = &b->heard[b->heard_count++];
    sound->origin = origin; sound->source_id = has_actor ? identify(b, actor) : -1;
    sound->time = event->seconds; sound->loudness = volume;
  }
}
static const qa_app_sim_bot_client *clients(void *ctx, size_t *out_count) {
  bots_t *b = ctx;
  b->client_views = grow(b->client_views, &b->client_view_capacity, b->connection_count, sizeof *b->client_views);
  for (size_t i = 0; i < b->connection_count; i++) b->client_views[i] = b->connections[i]->client;
  *out_count = b->connection_count;
  return b->client_views;
}
static int32_t goal_result(int64_t before, int64_t status) { return status == 0 ? 0 : status == 1 ? 3 : before == 0 ? 1 : 2; }
static int32_t move_to_point(void *ctx, qa_actor_id actor, qa_vec3 point, double tolerance) {
  bots_t *b = ctx;
  connection *c = connection_of_actor(b, actor);
  qa_body_state_result body = qa_shared_body_table_read(sim_of(b)->bodies, actor);
  if (c == NULL || !body.has_value) return 0;
  int64_t before = qa_rerelease_bot_behavior_goal_status(&c->behavior);
  qa_rerelease_bot_behavior_request_move_to_point(&c->behavior, point);
  if (qa_js_hypot3(body.value.origin.x - point.x, body.value.origin.y - point.y, body.value.origin.z - point.z) <= tolerance) return 3;
  return goal_result(before, qa_rerelease_bot_behavior_goal_status(&c->behavior));
}
static int32_t follow_actor(void *ctx, qa_actor_id actor, qa_actor_id target) {
  bots_t *b = ctx;
  connection *c = connection_of_actor(b, actor);
  qa_body_state_result body = qa_shared_body_table_read(sim_of(b)->bodies, target);
  if (c == NULL || !body.has_value) return 0;
  int64_t before = qa_rerelease_bot_behavior_goal_status(&c->behavior);
  qa_rerelease_bot_behavior_request_follow_entity(&c->behavior, (double)identify(b, target), body.value.origin);
  return goal_result(before, qa_rerelease_bot_behavior_goal_status(&c->behavior));
}
static bool actor_of(void *ctx, qa_client_id client, qa_actor_id *out) {
  connection *c = connection_at(ctx, client.slot);
  if (c == NULL || !qa_same_client(qa_session_client_id(c->client.client), client)) return false;
  *out = c->actor.id; return true;
}
static bool disconnect(void *ctx, int64_t slot) {
  bots_t *b = ctx;
  connection *c = connection_at(b, slot);
  if (c == NULL) return false;
  qa_client_id id = qa_session_client_id(c->client.client);
  sim_of(b)->disconnect_player(sim_of(b)->ctx, c->actor.id);
  remove_connection(b, slot);
  qa_engine_session_close_client(b->options.session, id);
  return true;
}
static void close_transport(void *ctx, bool restart) {
  (void)restart; /* TS close(_restart = false) */
  bots_t *b = ctx;
  if (b->closed) return;
  qa_app_sim_bot_services_detach_transport(sim_of(b)->bot_services, &b->service);
  while (b->connection_count != 0) remove_connection(b, b->connections[0]->slot);
  b->closed = true;
}
static qa_cvar_registry *configuration_of(void *ctx) { bots_t *b = ctx; return b->configuration; }
static const qa_app_sim_bot_client *begin_round_restart(void *ctx, size_t *out_count) {
  (void)ctx; (void)out_count;
  fail("Native rerelease bots use shared map replacement, not Q3 fast restart");
}
static void bind_restarted_round(void *ctx) { (void)ctx; fail("Native rerelease bots cannot bind a Q3 round"); }
static bool reconnect_restarted_client(void *ctx, qa_client_id client) { (void)ctx; (void)client; fail("Native rerelease bots cannot reconnect a Q3 round"); }
static void resume_round_bots(void *ctx) { (void)ctx; fail("Native rerelease bots cannot resume a Q3 round"); }

/* ---- checkpoint: the TS RereleaseBehaviorCheckpoint object, as
 * readRereleaseBehavior (bots/behavior/rerelease/checkpoint.ts) reads it ---- */
static qa_save_value *o_new(void) { qa_save_value *v = qa_save_value_object(); if (v == NULL) qa_range_error("Out of memory"); return v; }
static qa_save_value *a_new(void) { qa_save_value *v = qa_save_value_array(); if (v == NULL) qa_range_error("Out of memory"); return v; }
static void o_put(qa_save_value *o, const char *key, qa_save_value *v) { if (v == NULL || !qa_save_value_set(o, key, v)) qa_range_error("Out of memory"); }
static void a_push(qa_save_value *a, qa_save_value *v) { if (v == NULL || !qa_save_value_push(a, v)) qa_range_error("Out of memory"); }
static qa_save_value *n_new(double n) { return qa_save_value_number(n); }
static qa_save_value *b_new(bool v) { return qa_save_value_boolean(v); }
static qa_save_value *s_new(const char *s) { return qa_save_value_string(s != NULL ? s : ""); }
static qa_save_value *vec(qa_vec3 v) { qa_save_value *o = o_new(); o_put(o, "x", n_new(v.x)); o_put(o, "y", n_new(v.y)); o_put(o, "z", n_new(v.z)); return o; }
static qa_save_value *nullable_vec(bool has, qa_vec3 v) { return has ? vec(v) : qa_save_value_null(); }
static qa_save_value *encode_mode(const qa_bot_game_mode *mode) {
  qa_save_value *o = o_new();
  o_put(o, "gameType", s_new(mode->game_type)); o_put(o, "weaponStay", b_new(mode->weapon_stay));
  if (mode->has_has_teams) o_put(o, "hasTeams", b_new(mode->has_teams));
  if (mode->has_team_damage) o_put(o, "teamDamage", b_new(mode->team_damage));
  return o;
}
static qa_save_value *encode_path(const qa_bot_brain_checkpoint_memory *m) {
  if (!m->has_path) return qa_save_value_null();
  const qa_nav_path *path = &m->path;
  qa_save_value *o = o_new(), *nodes = a_new(), *points = a_new(), *links = a_new();
  for (size_t i = 0; i < path->node_count; i++) a_push(nodes, n_new((double)path->nodes[i]));
  for (size_t i = 0; i < path->point_count; i++) a_push(points, vec(path->points[i]));
  for (size_t i = 0; i < path->link_count; i++) {
    if (!path->has_link[i]) { a_push(links, qa_save_value_null()); continue; }
    const qa_nav_graph_link *link = &path->links[i];
    qa_save_value *l = o_new();
    o_put(l, "from", n_new((double)link->from)); o_put(l, "to", n_new((double)link->to)); o_put(l, "type", n_new((double)link->type));
    if (link->has_traversal) {
      qa_save_value *t = o_new();
      o_put(t, "funnel", vec(link->traversal.funnel)); o_put(t, "start", vec(link->traversal.start)); o_put(t, "end", vec(link->traversal.end));
      o_put(l, "traversal", t);
    } else o_put(l, "traversal", qa_save_value_null());
    if (link->has_entity_bounds) {
      qa_save_value *bounds = o_new();
      o_put(bounds, "mins", vec(link->entity_mins)); o_put(bounds, "maxs", vec(link->entity_maxs));
      o_put(l, "entityBounds", bounds);
    } else o_put(l, "entityBounds", qa_save_value_null());
    a_push(links, l);
  }
  o_put(o, "nodes", nodes); o_put(o, "points", points); o_put(o, "links", links);
  o_put(o, "cost", n_new(path->cost)); o_put(o, "generation", n_new((double)path->generation)); o_put(o, "mapDigest", s_new(path->map_digest));
  return o;
}
/* One BotBrainCheckpointMemory property by its TS name. */
static qa_save_value *memory_value(const qa_bot_brain_checkpoint_memory *m, const char *key) {
#define K(name) (strcmp(key, name) == 0)
  if (K("triggerWeapon")) return n_new(m->trigger_weapon);
  if (K("triggerHeldSince")) return n_new(m->trigger_held_since);
  if (K("triggerReadyAt")) return n_new(m->trigger_ready_at);
  if (K("targetId")) return n_new(m->target_id);
  if (K("goalPoint")) return nullable_vec(m->has_goal_point, m->goal_point);
  if (K("goalEntityId")) return n_new(m->goal_entity_id);
  if (K("stuckTrips")) return n_new(m->stuck_trips);
  if (K("goalIsLive")) return b_new(m->goal_is_live);
  if (K("unstickUntil")) return n_new(m->unstick_until);
  if (K("pressUntil")) return n_new(m->press_until);
  if (K("unstickSide")) return n_new(m->unstick_side);
  if (K("explicitGoalDone")) return b_new(m->explicit_goal_done);
  if (K("explicitGoalFailed")) return b_new(m->explicit_goal_failed);
  if (K("wedgeOrigin")) return nullable_vec(m->has_wedge_origin, m->wedge_origin);
  if (K("wedgeSince")) return n_new(m->wedge_since);
  if (K("lastSafeOrigin")) return nullable_vec(m->has_last_safe_origin, m->last_safe_origin);
  if (K("restPoint")) return nullable_vec(m->has_rest_point, m->rest_point);
  if (K("restUntil")) return n_new(m->rest_until);
  if (K("guardRefusals")) return n_new(m->guard_refusals);
  if (K("lastGuardRefused")) return b_new(m->last_guard_refused);
  if (K("gapJumps")) return n_new(m->gap_jumps);
  if (K("hazardFrames")) return n_new(m->hazard_frames);
  if (K("ownObjectiveHome")) return nullable_vec(m->has_own_objective_home, m->own_objective_home);
  if (K("enemyObjectiveHome")) return nullable_vec(m->has_enemy_objective_home, m->enemy_objective_home);
  if (K("objectiveRole")) return s_new(m->objective_role);
  if (K("touchGoal")) return b_new(m->touch_goal);
  if (K("holdPosition")) return b_new(m->hold_position);
  if (K("gateShootAt")) return nullable_vec(m->has_gate_shoot_at, m->gate_shoot_at);
  if (K("gateFiredAt")) return n_new(m->gate_fired_at);
  if (K("coopRegrouping")) return b_new(m->coop_regrouping);
  if (K("coopRegroupAt")) return n_new(m->coop_regroup_at);
  if (K("coopRegroupUntil")) return n_new(m->coop_regroup_until);
  if (K("levelStarted")) return b_new(m->level_started);
  if (K("checkSixUntil")) return n_new(m->check_six_until);
  if (K("checkSixYaw")) return n_new(m->check_six_yaw);
  if (K("checkSixNextAt")) return n_new(m->check_six_next_at);
  if (K("roamPoint")) return nullable_vec(m->has_roam_point, m->roam_point);
  if (K("roamUntil")) return n_new(m->roam_until);
  if (K("spawnedOnce")) return b_new(m->spawned_once);
  if (K("lastWeaponNumber")) return n_new(m->last_weapon_number);
  if (K("deadSince")) return n_new(m->dead_since);
  if (K("respawnWait")) return n_new(m->respawn_wait);
  if (K("respawnPress")) return b_new(m->respawn_press);
  if (K("aim")) {
    qa_save_value *aim = o_new();
    o_put(aim, "pitch", n_new(m->aim.pitch)); o_put(aim, "yaw", n_new(m->aim.yaw)); o_put(aim, "pitchVelocity", n_new(m->aim.pitch_velocity));
    o_put(aim, "yawVelocity", n_new(m->aim.yaw_velocity)); o_put(aim, "modifierUntil", n_new(m->aim.modifier_until));
    return aim;
  }
  if (K("pathState")) {
    /* newPathState order, or the reader's for a read or restored layout */
    qa_save_value *follow = o_new();
    o_put(follow, "path", encode_path(m)); o_put(follow, "index", n_new((double)m->path_index)); o_put(follow, "stuckOrigin", vec(m->stuck_origin));
    o_put(follow, "stuckSince", n_new(m->stuck_since)); o_put(follow, "stuckCount", n_new((double)m->stuck_count));
    if (m->layout == QA_BOT_BRAIN_LAYOUT_FRESH) {
      o_put(follow, "jumpReadyAt", n_new(m->jump_ready_at)); o_put(follow, "plannedAt", n_new(m->planned_at));
      o_put(follow, "liftWaitSince", n_new(m->lift_wait_since)); o_put(follow, "liftWaitZ", n_new(m->lift_wait_z));
    } else {
      o_put(follow, "liftWaitSince", n_new(m->lift_wait_since)); o_put(follow, "liftWaitZ", n_new(m->lift_wait_z));
      o_put(follow, "jumpReadyAt", n_new(m->jump_ready_at)); o_put(follow, "plannedAt", n_new(m->planned_at));
    }
    return follow;
  }
  if (K("awareness")) {
    qa_save_value *awareness = a_new();
    for (size_t i = 0; i < m->awareness_count; i++) {
      const qa_bot_awareness *a = &m->awareness[i].value;
      qa_save_value *entry = o_new(), *value = o_new();
      o_put(value, "id", n_new((double)a->id)); o_put(value, "sight", n_new(a->sight)); o_put(value, "weapon", n_new(a->weapon));
      o_put(value, "lastContact", n_new(a->last_contact)); o_put(value, "lastSeen", n_new(a->last_seen)); o_put(value, "lastHeard", n_new(a->last_heard));
      o_put(value, "lastKnownOrigin", vec(a->last_known_origin));
      o_put(entry, "entity", n_new((double)m->awareness[i].entity)); o_put(entry, "value", value);
      a_push(awareness, entry);
    }
    return awareness;
  }
  if (K("unreachableUntil")) {
    qa_save_value *unreachable = a_new();
    for (size_t i = 0; i < m->unreachable_count; i++) {
      qa_save_value *entry = o_new();
      o_put(entry, "entity", n_new((double)m->unreachable_until[i].entity)); o_put(entry, "time", n_new(m->unreachable_until[i].time));
      a_push(unreachable, entry);
    }
    return unreachable;
  }
  if (K("objectiveHome")) {
    qa_save_value *homes = a_new();
    for (size_t i = 0; i < m->objective_home_count; i++) {
      qa_save_value *entry = o_new();
      o_put(entry, "entity", n_new((double)m->objective_home[i].entity)); o_put(entry, "origin", vec(m->objective_home[i].origin));
      a_push(homes, entry);
    }
    return homes;
  }
  if (K("saidThisLevel")) {
    qa_save_value *said = a_new();
    for (size_t i = 0; i < m->said_count; i++) a_push(said, s_new(m->said_this_level[i]));
    return said;
  }
  if (K("explicitGoal")) {
    if (!m->has_explicit_goal) return qa_save_value_null();
    qa_save_value *goal = o_new();
    o_put(goal, "owner", s_new(m->explicit_goal.objective_owner ? "objective" : "external"));
    o_put(goal, "kind", s_new(m->explicit_goal.is_entity ? "entity" : "point"));
    o_put(goal, "point", vec(m->explicit_goal.point)); o_put(goal, "entityId", n_new((double)m->explicit_goal.entity_id));
    return goal;
  }
  if (K("lastCmd")) {
    qa_save_value *cmd = o_new();
    o_put(cmd, "forwardmove", n_new(m->last_cmd.forwardmove)); o_put(cmd, "sidemove", n_new(m->last_cmd.sidemove));
    o_put(cmd, "upmove", n_new(m->last_cmd.upmove)); o_put(cmd, "buttons", n_new((double)m->last_cmd.buttons));
    o_put(cmd, "impulse", n_new((double)m->last_cmd.impulse)); o_put(cmd, "viewAngles", vec(m->last_cmd.view_angles));
    return cmd;
  }
#undef K
  fail("Unknown rerelease brain memory field");
}
/* TS BotBrain.checkpoint() memory: JSON keeps the object's property order, which
 * is newBrainMemory's for a brain never restored, else the order of the
 * checkpoint it was restored from (see qa_bot_brain_layout). */
static const char *const fresh_memory_order[] = {
    "triggerWeapon", "triggerHeldSince", "triggerReadyAt", "aim", "pathState", "targetId", "goalPoint", "goalEntityId",
    "stuckTrips", "goalIsLive", "unstickUntil", "pressUntil", "unstickSide", "explicitGoal", "explicitGoalDone",
    "explicitGoalFailed", "wedgeOrigin", "wedgeSince", "lastSafeOrigin", "restPoint", "restUntil", "guardRefusals",
    "lastGuardRefused", "gapJumps", "hazardFrames", "ownObjectiveHome", "enemyObjectiveHome", "objectiveRole", "touchGoal",
    "holdPosition", "gateShootAt", "gateFiredAt", "coopRegrouping", "coopRegroupAt", "coopRegroupUntil", "levelStarted",
    "checkSixUntil", "checkSixYaw", "checkSixNextAt", "roamPoint", "roamUntil", "lastCmd", "spawnedOnce", "lastWeaponNumber",
    "deadSince", "respawnWait", "respawnPress", "awareness", "unreachableUntil", "objectiveHome", "saidThisLevel"};
static const char *const read_memory_order[] = {
    "triggerWeapon", "triggerHeldSince", "triggerReadyAt", "targetId", "goalPoint", "goalEntityId", "stuckTrips", "goalIsLive",
    "unstickUntil", "pressUntil", "unstickSide", "explicitGoalDone", "explicitGoalFailed", "wedgeOrigin", "wedgeSince",
    "lastSafeOrigin", "restPoint", "restUntil", "guardRefusals", "lastGuardRefused", "gapJumps", "hazardFrames",
    "ownObjectiveHome", "enemyObjectiveHome", "objectiveRole", "touchGoal", "holdPosition", "gateShootAt", "gateFiredAt",
    "coopRegrouping", "coopRegroupAt", "coopRegroupUntil", "levelStarted", "checkSixUntil", "checkSixYaw", "checkSixNextAt",
    "roamPoint", "roamUntil", "spawnedOnce", "lastWeaponNumber", "deadSince", "respawnWait", "respawnPress", "aim", "pathState",
    "awareness", "unreachableUntil", "objectiveHome", "saidThisLevel", "explicitGoal", "lastCmd"};
static const char *const restored_memory_order[] = {
    "triggerWeapon", "triggerHeldSince", "triggerReadyAt", "targetId", "goalPoint", "goalEntityId", "stuckTrips", "goalIsLive",
    "unstickUntil", "pressUntil", "unstickSide", "explicitGoalDone", "explicitGoalFailed", "wedgeOrigin", "wedgeSince",
    "lastSafeOrigin", "restPoint", "restUntil", "guardRefusals", "lastGuardRefused", "gapJumps", "hazardFrames",
    "ownObjectiveHome", "enemyObjectiveHome", "objectiveRole", "touchGoal", "holdPosition", "gateShootAt", "gateFiredAt",
    "coopRegrouping", "coopRegroupAt", "coopRegroupUntil", "levelStarted", "checkSixUntil", "checkSixYaw", "checkSixNextAt",
    "roamPoint", "roamUntil", "spawnedOnce", "lastWeaponNumber", "deadSince", "respawnWait", "respawnPress", "aim", "pathState",
    "explicitGoal", "lastCmd", "awareness", "unreachableUntil", "objectiveHome", "saidThisLevel"};
static qa_save_value *encode_memory(const qa_bot_brain_checkpoint_memory *m) {
  const char *const *order = m->layout == QA_BOT_BRAIN_LAYOUT_FRESH ? fresh_memory_order
      : m->layout == QA_BOT_BRAIN_LAYOUT_READ ? read_memory_order : restored_memory_order;
  qa_save_value *o = o_new();
  for (size_t i = 0; i < sizeof fresh_memory_order / sizeof fresh_memory_order[0]; i++) o_put(o, order[i], memory_value(m, order[i]));
  return o;
}
qa_save_value *qa_app_sim_encode_rerelease_behavior(const qa_rerelease_behavior_checkpoint *checkpoint) {
  qa_save_value *o = o_new(), *brain = o_new(), *chats = a_new();
  o_put(o, "version", n_new((double)checkpoint->version)); o_put(o, "source", s_new(qa_rerelease_source_name(checkpoint->source)));
  o_put(o, "definition", s_new(checkpoint->definition)); o_put(o, "rng", n_new((double)checkpoint->rng));
  o_put(brain, "version", n_new((double)checkpoint->brain.version)); o_put(brain, "skill", s_new(checkpoint->brain.skill));
  o_put(brain, "gameMode", encode_mode(&checkpoint->brain.game_mode)); o_put(brain, "memory", encode_memory(&checkpoint->brain.memory));
  o_put(o, "brain", brain);
  for (size_t i = 0; i < checkpoint->pending_count; i++) {
    const qa_rerelease_pending_chat *chat = &checkpoint->pending_chats[i];
    qa_save_value *entry = o_new(), *event = o_new();
    o_put(event, "locstring", s_new(chat->locstring)); o_put(event, "type", s_new(chat->type));
    o_put(event, "delayMs", n_new(chat->delay_ms)); o_put(event, "teamOnly", b_new(chat->team_only));
    o_put(entry, "time", n_new(chat->time)); o_put(entry, "event", event);
    a_push(chats, entry);
  }
  o_put(o, "pendingChats", chats);
  return o;
}
static qa_save_value *checkpoint(void *ctx) {
  bots_t *b = ctx;
  const char *assets = b->assets->files.provenance != NULL ? b->assets->files.provenance(b->assets->files.files.ctx) : NULL;
  if (assets == NULL) fail("Native bot assets lack save provenance");
  qa_save_value *root = o_new(), *transport = o_new(), *connections = a_new(), *director = o_new(), *heard = a_new(),
      *client_list = a_new(), *observations = a_new();
  o_put(root, "version", n_new(1));
  o_put(transport, "version", n_new(1)); o_put(transport, "elapsedMilliseconds", n_new(b->elapsed_milliseconds)); o_put(transport, "snapshots", a_new());
  for (size_t i = 0; i < b->connection_count; i++) {
    connection *c = b->connections[i];
    a_push(connections, qa_app_sim_encode_bot_transport_connection(c->client.client, c->actor.id, c->client.reliable));
  }
  o_put(transport, "connections", connections);
  o_put(root, "transport", transport);
  o_put(director, "version", n_new(1)); o_put(director, "kind", s_new("rerelease")); o_put(director, "assets", s_new(assets));
  o_put(director, "nextObservation", n_new(b->next_observation));
  for (size_t i = 0; i < b->heard_count; i++) {
    qa_save_value *sound = o_new();
    o_put(sound, "origin", vec(b->heard[i].origin)); o_put(sound, "sourceId", n_new((double)b->heard[i].source_id));
    o_put(sound, "time", n_new(b->heard[i].time)); o_put(sound, "loudness", n_new(b->heard[i].loudness));
    a_push(heard, sound);
  }
  o_put(director, "heard", heard);
  for (size_t i = 0; i < b->connection_count; i++) {
    connection *c = b->connections[i];
    qa_rerelease_behavior_checkpoint behavior;
    if (!qa_rerelease_bot_behavior_checkpoint_make(&c->behavior, &behavior)) qa_range_error("Out of memory");
    qa_save_value *entry = o_new();
    o_put(entry, "slot", n_new((double)c->slot)); o_put(entry, "name", s_new(c->name)); o_put(entry, "skill", s_new(c->skill));
    o_put(entry, "sequence", n_new(c->sequence)); o_put(entry, "selection", n_new(c->selection.number));
    o_put(entry, "behavior", qa_app_sim_encode_rerelease_behavior(&behavior));
    qa_rerelease_behavior_checkpoint_destroy(&behavior);
    a_push(client_list, entry);
  }
  o_put(director, "clients", client_list);
  o_put(root, "director", director);
  o_put(root, "navigation", b->navigation->checkpoint(b->navigation->ctx));
  o_put(root, "knowledge", qa_save_value_null()); o_put(root, "sharedWorld", qa_save_value_null());
  for (size_t i = 0; i < b->observation_count; i++) {
    qa_saved_actor_id saved = qa_saved_actor_id_of(b->observations[i].actor);
    qa_save_value *entry = o_new(), *actor = o_new();
    o_put(actor, "slot", n_new(saved.slot)); o_put(actor, "generation", n_new(saved.generation));
    o_put(entry, "number", n_new(b->observations[i].number)); o_put(entry, "actor", actor);
    a_push(observations, entry);
  }
  o_put(root, "observations", observations);
  return root;
}

typedef struct saved_client { double slot; char *name, *skill; double sequence, selection; qa_rerelease_behavior_checkpoint behavior; } saved_client;
static void rf(const qa_save_reader *r, const char *name, qa_save_reader *out) { if (!qa_save_reader_field(r, name, out)) qa_range_error("Out of memory"); }
static double read_number(const qa_save_reader *r, const char *name, double (*read)(const qa_save_reader *)) {
  qa_save_reader f; rf(r, name, &f); double v = read(&f); qa_save_reader_destroy(&f); return v;
}
static double read_integer0(const qa_save_reader *r, const char *name) {
  qa_save_reader f; rf(r, name, &f); double v = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f); return v;
}
static char *read_string(const qa_save_reader *r, const char *name) {
  qa_save_reader f; rf(r, name, &f); char *v = xstrdup(qa_save_reader_string(&f)); qa_save_reader_destroy(&f); return v;
}
static void restore(bots_t *b) {
  const qa_app_sim_decoded_bots_checkpoint *image = b->options.restore;
  const qa_app_sim_bot_simulation *sim = sim_of(b);
  qa_save_reader r, f;
  if (!qa_save_reader_init(&r, image->director, "bot.rerelease.population")) qa_range_error("Out of memory");
  rf(&r, "version", &f); qa_save_reader_literal_number(&f, 1); qa_save_reader_destroy(&f);
  rf(&r, "kind", &f); qa_save_reader_literal_string(&f, "rerelease"); qa_save_reader_destroy(&f);
  rf(&r, "assets", &f);
  const char *saved_assets = qa_save_reader_string(&f);
  const char *assets = b->assets->files.provenance != NULL ? b->assets->files.provenance(b->assets->files.files.ctx) : NULL;
  if (assets == NULL || strcmp(saved_assets, assets) != 0) fail("Saved native bot assets differ from mounted definitions");
  qa_save_reader_destroy(&f);
  b->navigation->restore_checkpoint(b->navigation->ctx, image->navigation);
  rf(&r, "nextObservation", &f); b->next_observation = qa_save_reader_integer_min(&f, 1); qa_save_reader_destroy(&f);
  qa_save_reader list;
  rf(&r, "heard", &list);
  size_t heard_count = qa_save_reader_list_count(&list);
  b->heard_count = 0;
  for (size_t i = 0; i < heard_count; i++) {
    qa_save_reader sound, origin;
    if (!qa_save_reader_item(&list, i, &sound)) qa_range_error("Out of memory");
    rf(&sound, "origin", &origin);
    qa_bot_sound value;
    value.origin.x = read_number(&origin, "x", qa_save_reader_finite);
    value.origin.y = read_number(&origin, "y", qa_save_reader_finite);
    value.origin.z = read_number(&origin, "z", qa_save_reader_finite);
    qa_save_reader_destroy(&origin);
    value.source_id = (int64_t)read_number(&sound, "sourceId", qa_save_reader_integer);
    value.time = read_number(&sound, "time", qa_save_reader_finite);
    value.loudness = read_number(&sound, "loudness", qa_save_reader_finite);
    qa_save_reader_destroy(&sound);
    b->heard = grow(b->heard, &b->heard_capacity, b->heard_count, sizeof *b->heard);
    b->heard[b->heard_count++] = value;
  }
  qa_save_reader_destroy(&list);
  for (size_t i = 0; i < image->observation_count; i++) {
    const qa_app_sim_bot_observation *entry = &image->observations[i];
    bool known = false;
    for (size_t j = 0; j < b->observation_count && !known; j++) known = b->observations[j].number == entry->number;
    if (entry->number < 1 || entry->number >= b->next_observation || known) fail("Invalid native bot observation sequence");
    qa_actor_id restored = qa_session_actor_registry_reference_saved(sim->actors, entry->actor, NULL);
    b->observations = grow(b->observations, &b->observation_capacity, b->observation_count, sizeof *b->observations);
    b->observations[b->observation_count].number = entry->number; b->observations[b->observation_count].actor = restored; b->observation_count++;
  }
  rf(&r, "clients", &list);
  size_t client_count = qa_save_reader_list_count(&list);
  saved_client *saved = calloc(client_count != 0 ? client_count : 1, sizeof *saved);
  if (saved == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < client_count; i++) {
    qa_save_reader entry, behavior;
    if (!qa_save_reader_item(&list, i, &entry)) qa_range_error("Out of memory");
    saved[i].slot = read_integer0(&entry, "slot");
    saved[i].name = read_string(&entry, "name"); saved[i].skill = read_string(&entry, "skill");
    saved[i].sequence = read_integer0(&entry, "sequence"); saved[i].selection = read_integer0(&entry, "selection");
    rf(&entry, "behavior", &behavior);
    if (!qa_read_rerelease_behavior(&saved[i].behavior, behavior.value)) qa_range_error("Out of memory");
    qa_save_reader_destroy(&behavior); qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  qa_save_reader_destroy(&r);
  bool distinct = true;
  for (size_t i = 0; i < client_count; i++) for (size_t j = i + 1; j < client_count; j++) if (saved[i].slot == saved[j].slot) distinct = false;
  if (client_count != image->transport.connection_count || !distinct) fail("Saved native bot decisions and transport clients differ");
  for (size_t i = 0; i < image->transport.connection_count; i++) {
    const qa_app_sim_bot_transport_connection *entry = &image->transport.connections[i];
    qa_session_client *client = b->options.resolve_client(b->options.ctx, entry->client_slot, entry->client_generation);
    qa_owned_actor actor;
    bool has_actor = qa_session_actor_registry_resolve_saved(sim->actors, entry->actor, &actor);
    saved_client *state = NULL;
    for (size_t j = 0; j < client_count && state == NULL; j++) if (saved[j].slot == entry->client_slot) state = &saved[j];
    if (client == NULL || !has_actor || state == NULL) fail("Saved native bot lacks its original client/actor");
    qa_app_sim_movement_player *movement = sim->movement_player(sim->ctx, actor.id);
    if (movement == NULL || !qa_same_client(movement->client, qa_session_client_id(client))) fail("Saved native bot actor belongs to a different client");
    qa_q3_server_reliable_commands *reliable = qa_app_sim_restored_bot_reliable_commands(entry);
    if (reliable == NULL) qa_range_error("Out of memory");
    connection *c = connect_client(b, client, &reliable, NULL, state->name, state->skill, &actor);
    c->sequence = state->sequence; c->selection.number = state->selection;
    if (!qa_rerelease_bot_behavior_restore(&c->behavior, &state->behavior)) qa_range_error("Out of memory");
  }
  for (size_t i = 0; i < client_count; i++) { free(saved[i].name); free(saved[i].skill); qa_rerelease_behavior_checkpoint_destroy(&saved[i].behavior); }
  free(saved);
  b->elapsed_milliseconds = image->transport.elapsed_milliseconds;
}

qa_app_sim_rerelease_bots *qa_app_sim_rerelease_bots_create(const qa_app_sim_bots_options *options,
    const qa_app_sim_application_bot_assets *assets) {
  const qa_app_sim_bot_simulation *sim = options->simulation;
  qa_cvar_registry *configuration = options->configuration;
  if (configuration == NULL) {
    const qa_app_sim_bot_q1_source *q1 = sim->q1_source(sim->ctx);
    configuration = q1 != NULL ? q1->cvars : sim->q2_server_cvars(sim->ctx);
  }
  if (configuration == NULL) fail("Native bot configuration unavailable");
  if (options->navigation->checkpoint == NULL) fail("Native bot navigation requires shared checkpoint lifetime");
  bots_t *b = calloc(1, sizeof *b);
  if (b == NULL) return NULL;
  b->options = *options; b->assets = assets; b->configuration = configuration; b->navigation = options->navigation;
  b->next_observation = 1;
  b->service.ctx = b; b->service.options = &b->options;
  b->service.configuration = configuration_of; b->service.move_to_point = move_to_point; b->service.follow_actor = follow_actor;
  b->service.is_bot = is_bot; b->service.actor = actor_of; b->service.frame = frame; b->service.receive = receive;
  b->service.clients = clients; b->service.console_command = console_command; b->service.disconnect = disconnect;
  b->service.close = close_transport; b->service.checkpoint = checkpoint; b->service.begin_round_restart = begin_round_restart;
  b->service.bind_restarted_round = bind_restarted_round; b->service.reconnect_restarted_client = reconnect_restarted_client;
  b->service.resume_round_bots = resume_round_bots;
  b->objectives = qa_app_sim_rerelease_bot_objectives_create(sim, assets->knowledge);
  qa_cvar_snapshot snapshot;
  qa_cvar_registry_register(configuration, "bot_minplayers", "0", 0, &snapshot);
  qa_cvar_read_free(&snapshot);
  qa_cvar_registry_register(configuration, "bot_enable", "1", 0, &snapshot);
  qa_cvar_read_free(&snapshot);
  if (options->restore != NULL) restore(b);
  else for (size_t i = 0; i < options->client_count; i++) {
    const qa_app_sim_bot_client *saved = &options->clients[i];
    char *name = name_of(b, saved->userinfo);
    qa_q3_server_reliable_commands *reliable = saved->reliable;
    connect_client(b, saved->client, &reliable, saved->userinfo, name, default_skill(b), NULL);
    free(name);
  }
  qa_app_sim_bot_services_attach_transport(sim->bot_services, &b->service);
  b->release_subscription = qa_session_actor_registry_on_release(sim->actors, on_release, b);
  return b;
}
void qa_app_sim_rerelease_bots_destroy(qa_app_sim_rerelease_bots *b) {
  if (b == NULL) return;
  qa_session_actor_registry_off_release(sim_of(b)->actors, b->release_subscription);
  while (b->connection_count != 0) remove_connection(b, b->connections[0]->slot);
  qa_app_sim_rerelease_bot_objectives_destroy(b->objectives);
  free(b->connections); free(b->observations); free(b->heard); free(b->commands); free(b->arsenals); free(b->client_views); free(b);
}
const qa_app_sim_bot_service *qa_app_sim_rerelease_bots_service(qa_app_sim_rerelease_bots *b) { return &b->service; }
const qa_app_sim_bot_service *qa_app_sim_create_application_bots(const qa_app_sim_bots_options *options,
    const qa_app_sim_application_bot_assets *assets, qa_app_sim_rerelease_bots **out_rerelease,
    qa_app_sim_application_bots **out_q3) {
  *out_rerelease = NULL; *out_q3 = NULL;
  if (is(assets->kind, "rerelease")) {
    *out_rerelease = qa_app_sim_rerelease_bots_create(options, assets);
    if (*out_rerelease == NULL) qa_range_error("Out of memory");
    return &(*out_rerelease)->service;
  }
  *out_q3 = qa_app_sim_application_bots_create(options);
  if (*out_q3 == NULL) qa_range_error("Out of memory");
  return qa_app_sim_application_bots_service(*out_q3);
}
