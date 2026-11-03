/* Port of src/content/composition/q1/runtime.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q1/runtime.h"
#include "qa/content/composition/q1/q3_pickups.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/composition/q1/commands.h"
#include "qa/content/q1/addons.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

typedef qa_comp_q1_source_composition comp_t;

static const char *const GRAPPLE = "q1:ctf/weapon/grapple";

static comp_t *of(void *ctx) { return (comp_t *)ctx; }
static qa_q1_level_rules *level_rules(comp_t *comp) { return qa_q1_base_level_rules(comp->base); }
static bool is_rerelease(const comp_t *comp) { return strcmp(comp->game->options.edition, "rerelease") == 0; }

/* --- services ------------------------------------------------------------ */
static double cvar(comp_t *comp, const char *name) { return comp->services->cvar(comp->services->ctx, name); }

/* createQ1SourceComposition passes the foundation option getters
 * `get skill()` (cvar "skill" clamped to 0..3), `get teamplay()` (cvar
 * "teamplay") and `get gravity()` (cvar "sv_gravity"); they become the
 * qa_q1_options.live functions, whose ctx is the composition services. */
static double skill_of(double value) { return value >= 3 ? 3 : value >= 2 ? 2 : value >= 1 ? 1 : 0; }
static double services_cvar(void *ctx, const char *name) {
  const qa_comp_q1_services *services = ctx;
  return services->cvar(services->ctx, name);
}
static double live_skill(void *ctx) { return skill_of(services_cvar(ctx, "skill")); }
static double live_teamplay(void *ctx) { return services_cvar(ctx, "teamplay"); }
static double live_gravity(void *ctx) { return services_cvar(ctx, "sv_gravity"); }
static void set_cvar(comp_t *comp, const char *name, const char *value) {
  comp->services->set_cvar(comp->services->ctx, name, value);
}
static void emit(comp_t *comp, const qa_comp_q1_event *event) { comp->services->emit(comp->services->ctx, event); }
static qa_comp_q1_selected_player selected_player(comp_t *comp, qa_actor_id actor) {
  return comp->services->selected_player(comp->services->ctx, actor);
}
static qa_item_id selected_weapon(comp_t *comp, qa_actor_id actor) {
  return comp->services->selected_weapon(comp->services->ctx, actor);
}
static void emit_developer_message(comp_t *comp, const char *text) {
  qa_comp_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_COMP_Q1_EVENT_DEVELOPER_MESSAGE;
  event.text = text;
  emit(comp, &event);
}
static void developer_message(void *ctx, const char *text) { emit_developer_message(of(ctx), text); }
static double read_flags(comp_t *comp) {
  return comp->selection.campaign.read_flags(comp->selection.campaign.ctx);
}

/* `client.impulse = 0` after a call that may have moved the client records. */
static void clear_impulse(comp_t *comp, qa_actor_id actor) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_get(&comp->clients, actor);
  if (client != NULL) client->impulse = 0;
}

/* --- private helpers ----------------------------------------------------- */
static bool is_monster(comp_t *comp, qa_actor_id actor) {
  const qa_q1_actor *entity = qa_q1_get_entity(comp->game, &actor);
  return entity != NULL && (qa_js_to_int32(entity->movement_flags) & 32) != 0;
}

static void broadcast(comp_t *comp, const char *text, const char *const *args, size_t arg_count) {
  qa_q1_msg_arg values[4];
  if (arg_count > sizeof values / sizeof values[0]) qa_range_error("Q1 broadcast has too many arguments");
  for (size_t i = 0; i < arg_count; i++) values[i] = (qa_q1_msg_arg){false, 0, args[i]};
  size_t count = 0;
  const qa_actor_id *listed = comp->game->host.players(comp->game->host.ctx, &count);
  qa_actor_id *players = malloc((count > 0 ? count : 1) * sizeof *players);
  if (players == NULL) qa_range_error("Out of memory");
  if (count > 0) memcpy(players, listed, count * sizeof *players);
  for (size_t i = 0; i < count; i++) qa_q1_message(comp->game, &players[i], text, false, values, arg_count);
  free(players);
}

/* TS logLine: log.qc dprint output, one tab-separated line per event. The pieces are joined in order
   (NULL-terminated list) into the developer-message text. */
static void log_line(comp_t *comp, const char *first, ...) {
  size_t length = 0;
  va_list args;
  va_start(args, first);
  for (const char *piece = first; piece != NULL; piece = va_arg(args, const char *)) length += strlen(piece);
  va_end(args);
  char *text = malloc(length + 1);
  if (text == NULL) qa_range_error("Out of memory");
  text[0] = '\0';
  va_start(args, first);
  for (const char *piece = first; piece != NULL; piece = va_arg(args, const char *)) strcat(text, piece);
  va_end(args);
  emit_developer_message(comp, text);
  free(text);
}
/* TS logDeath. */
static void log_death(comp_t *comp, qa_actor_id victim, const char *what, bool has_versus, qa_actor_id versus) {
  const qa_comp_q1_source_client *target = qa_comp_q1_source_clients_get(&comp->clients, victim);
  if (target == NULL) return;
  const qa_comp_q1_source_client *other = has_versus ? qa_comp_q1_source_clients_get(&comp->clients, versus) : NULL;
  char target_frags[40], other_frags[40];
  qa_net_js_number(trunc(target->frags), target_frags, sizeof target_frags);
  if (other != NULL) qa_net_js_number(trunc(other->frags), other_frags, sizeof other_frags);
  if (other == NULL)
    log_line(comp, "LOG:\tDEATH\t", qa_comp_q1_source_client_name(target), "\t", target_frags, "\t", what, "\n", NULL);
  else
    log_line(comp, "LOG:\tDEATH\t", qa_comp_q1_source_client_name(target), "\t", target_frags, "\t",
             qa_comp_q1_source_client_name(other), "\t", other_frags, "\t", what, "\n", NULL);
}

/* event is "connect" | "disconnect" | "suicide" | "exit". */
static void notice(comp_t *comp, qa_actor_id actor, const char *event) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&comp->clients, actor);
  qa_q1_client_notice value;
  qa_q1_client_notice_make(&value, comp->game->options.edition, event, qa_comp_q1_source_client_name(client),
                           client->frags);
  if (comp->ctf != NULL && (strcmp(event, "connect") == 0 || strcmp(event, "disconnect") == 0))
    log_line(comp, "LOG:\t", strcmp(event, "connect") == 0 ? "CONNECT" : "DISCONNECT", "\t",
             qa_comp_q1_source_client_name(client), "\n", NULL);
  broadcast(comp, value.text, value.arguments, value.argument_count);
  if (value.score_delta != 0) qa_comp_q1_source_clients_add_score(&comp->clients, actor, value.score_delta);
}

/* QuakeC issued `stuffcmd disconnect`, which took effect after the current think; release only between frames. */
static void request_disconnect(comp_t *comp, qa_actor_id actor) {
  for (size_t i = 0; i < comp->pending_disconnect_count; i++)
    if (qa_same_actor(comp->pending_disconnects[i], actor)) return;
  if (comp->pending_disconnect_count == comp->pending_disconnect_capacity) {
    size_t capacity = comp->pending_disconnect_capacity == 0 ? 4 : comp->pending_disconnect_capacity * 2;
    qa_actor_id *grown = realloc(comp->pending_disconnects, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    comp->pending_disconnects = grown;
    comp->pending_disconnect_capacity = capacity;
  }
  comp->pending_disconnects[comp->pending_disconnect_count++] = actor;
}

static void flush_disconnects(comp_t *comp) {
  /* pendingDisconnects.splice(0) */
  qa_actor_id *pending = comp->pending_disconnects;
  size_t count = comp->pending_disconnect_count;
  comp->pending_disconnects = NULL;
  comp->pending_disconnect_count = 0;
  comp->pending_disconnect_capacity = 0;
  for (size_t i = 0; i < count; i++)
    if (qa_comp_q1_source_clients_get(&comp->clients, pending[i]) != NULL)
      qa_comp_q1_source_composition_disconnect(comp, pending[i]);
  free(pending);
}

static void present_intermission(comp_t *comp, const qa_q1_intermission_result *result) {
  const bool finale = strcmp(result->kind, "finale") == 0;
  if (finale) {
    qa_q1_base_reset_finale(comp->base);
    qa_compat_qc_finale_ack_reset(&comp->finale_acknowledgement);
  }
  if (finale || strcmp(result->kind, "sell-screen") == 0) {
    qa_comp_q1_event event;
    memset(&event, 0, sizeof event);
    event.kind = QA_COMP_Q1_EVENT_LEVEL_PRESENTATION;
    event.finale = result;
    emit(comp, &event);
  }
}

/* WEAPONS.find(weapon => game.weaponItem(weapon) === services.selectedWeapon(actor)) ?? player?.weapon ?? null */
static const char *selected_base_weapon(comp_t *comp, qa_actor_id actor) {
  for (size_t i = 0; i < sizeof QA_Q1_WEAPONS / sizeof QA_Q1_WEAPONS[0]; i++) {
    qa_item_id item = qa_q1_services_weapon_item(comp->game, QA_Q1_WEAPONS[i]);
    qa_item_id selected = selected_weapon(comp, actor);
    if (selected != NULL && strcmp(item, selected) == 0) return QA_Q1_WEAPONS[i];
  }
  const qa_q1_player_state *player = qa_q1_player(comp->game, actor);
  return player != NULL ? player->weapon : NULL;
}

static void respawn_at(comp_t *comp, qa_actor_id actor, qa_q1_actor *source_spot, const qa_q1_travel_state *travel);

/* respawnAt(actor, spot, this.newTravel()): the fresh travel is released after the call. */
static void respawn_new_travel(comp_t *comp, qa_actor_id actor, qa_q1_actor *spot) {
  qa_q1_travel_state travel = qa_comp_q1_source_composition_new_travel(comp);
  respawn_at(comp, actor, spot, &travel);
  qa_q1_travel_state_destroy(&travel);
}

/* MG1 client.qc SelectSpawnPoint in Horde coop: the first checkpoint with no living player within 72 units,
 * falling back to the ordinary rotation where the source returns no point. */
static qa_q1_actor *horde_coop_spawn(comp_t *comp, qa_actor_id actor) {
  qa_q1_services *game = comp->game;
  size_t listed_count = 0;
  const qa_actor_id *listed = game->host.players(game->host.ctx, &listed_count);
  qa_actor_id *others = malloc((listed_count > 0 ? listed_count : 1) * sizeof *others);
  if (others == NULL) qa_range_error("Out of memory");
  size_t other_count = 0;
  for (size_t i = 0; i < listed_count; i++) {
    const qa_actor_id player = listed[i];
    if (!qa_same_actor(player, actor) && qa_q1_health(game, player) > 0) others[other_count++] = player;
  }
  qa_q1_actor *found = NULL;
  for (size_t i = 0; found == NULL && i < game->entity_count; i++) {
    qa_q1_actor *spot = game->entities[i];
    if (strcmp(spot->classname, "info_player_coop") != 0 || !spot->activated) continue;
    const qa_vec3 origin = qa_q1_get_body(game, spot).origin;
    bool occupied = false;
    for (size_t p = 0; !occupied && p < other_count; p++) {
      const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, others[p]);
      occupied = body.has_value && qa_q1_length(qa_q1_vsub(body.value.origin, origin)) < 72;
    }
    if (!occupied) found = spot;
  }
  free(others);
  return found;
}

static bool horde_has_manager(comp_t *comp) {
  return comp->horde != NULL && qa_q1ad_horde_manager(comp->horde) != NULL;
}

/* TS private obituaryActor(actor). */
static qa_q1_obituary_actor obituary_actor(comp_t *comp, qa_actor_id actor) {
  qa_q1_services *game = comp->game;
  const qa_q1_actor *entity = qa_q1_get_entity(game, &actor);
  const qa_comp_q1_source_client *client = qa_comp_q1_source_clients_get(&comp->clients, actor);
  const qa_q1_player_state *player = qa_q1_player(game, actor);
  qa_comp_q1_selected_player selected;
  memset(&selected, 0, sizeof selected);
  if (client != NULL) selected = selected_player(comp, actor);
  qa_q1_obituary_actor result;
  memset(&result, 0, sizeof result);
  result.actor = actor;
  result.name = client != NULL ? qa_comp_q1_source_client_name(client)
                : entity != NULL ? qa_q1_actor_text(entity, "netname") : "";
  result.classname = client == NULL ? game->host.classname(game->host.ctx, actor) : "player";
  result.is_player = client != NULL;
  result.is_monster = is_monster(comp, actor);
  result.team = client == NULL ? (entity != NULL ? qa_q1_actor_number(entity, "team") : 0) : client->team;
  result.health = qa_q1_health(game, actor);
  result.water_type = client != NULL ? selected.water_type : "empty";
  result.water_level = client != NULL ? (double)selected.water_level : entity != NULL ? entity->water_level : 0;
  result.weapon = player != NULL ? player->weapon : NULL;
  result.quad_expires = qa_js_max(0.0, (player != NULL ? qa_q1_player_powerup_get(player, "quad") : 0) - game->time);
  result.invulnerable_expires =
      qa_js_max(0.0, (player != NULL ? qa_q1_player_powerup_get(player, "invulnerability") : 0) - game->time);
  result.brush = entity != NULL && strcmp(entity->solid, "bsp") == 0;
  result.kill_string = entity != NULL ? qa_q1_actor_text(entity, "killstring") : "";
  result.has_aflag = true;
  result.aflag = entity != NULL ? qa_q1_actor_number(entity, "aflag") : 0;
  return result;
}
static double host_random(void *ctx) {
  qa_q1_services *game = of(ctx)->game;
  return game->host.random(game->host.ctx);
}

/* --- campaign binding (selection.campaign forwarding) --------------------- */
static double campaign_read_flags(void *ctx) { return read_flags(of(ctx)); }
static void campaign_write_flags(void *ctx, double flags) {
  comp_t *comp = of(ctx);
  comp->selection.campaign.write_flags(comp->selection.campaign.ctx, flags);
}
static void campaign_set_skill(void *ctx, int skill) {
  comp_t *comp = of(ctx);
  comp->selection.campaign.set_skill(comp->selection.campaign.ctx, skill);
  char text[16];
  (void)snprintf(text, sizeof text, "%d", skill); /* String(skill), skill 0..3 */
  set_cvar(comp, "skill", text);
}

/* --- Q1BaseOptions ---------------------------------------------------------- */
static bool base_same_level(void *ctx) { return cvar(of(ctx), "samelevel") != 0; }
static void base_player_exited(void *ctx, qa_actor_id actor) { notice(of(ctx), actor, "exit"); }
static void base_finish_campaign(void *ctx) {
  comp_t *comp = of(ctx);
  comp->services->finish_campaign(comp->services->ctx);
}
static bool base_finale_finished(void *ctx) {
  comp_t *comp = of(ctx);
  const size_t count = comp->clients.record_count;
  qa_actor_id *actors = malloc((count > 0 ? count : 1) * sizeof *actors);
  bool *held = malloc((count > 0 ? count : 1) * sizeof *held);
  if (actors == NULL || held == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    actors[i] = comp->clients.records[i].actor.id;
    const qa_q1_player_state *player = qa_q1_player(comp->game, actors[i]);
    held[i] = player != NULL && player->attack_held;
  }
  const bool result = qa_compat_qc_finale_ack_poll(&comp->finale_acknowledgement, comp->game->time, actors, held, count);
  free(actors);
  free(held);
  return result;
}

/* --- Q1AddonServices -------------------------------------------------------- */
static void addon_emit(void *ctx, const qa_q1ad_event *addon_event) {
  qa_comp_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_COMP_Q1_EVENT_ADDON;
  event.addon_event = addon_event;
  emit(of(ctx), &event);
}
static bool cheat_arsenal(void *ctx, qa_actor_id actor, const char *category) {
  const qa_comp_q1_services *services = of(ctx)->services;
  return services->cheat_arsenal != NULL && services->cheat_arsenal(services->ctx, actor, category);
}
static bool addon_is_monster(void *ctx, qa_actor_id actor) { return is_monster(of(ctx), actor); }
static double addon_cvar(void *ctx, const char *name) { return cvar(of(ctx), name); }
static void addon_set_cvar(void *ctx, const char *name, const char *value) { set_cvar(of(ctx), name, value); }

/* --- Q1HordeServices -------------------------------------------------------- */
static double horde_dead_flag(void *ctx, qa_actor_id actor) { return selected_player(of(ctx), actor).dead_flag; }
static bool horde_no_target(void *ctx, qa_actor_id actor) {
  return qa_comp_q1_source_composition_no_target(of(ctx), actor);
}
static bool horde_is_bot(void *ctx, qa_actor_id actor) { return selected_player(of(ctx), actor).is_bot; }
static void horde_add_score(void *ctx, qa_actor_id actor, double delta) {
  qa_comp_q1_source_clients_add_score(&of(ctx)->clients, actor, delta);
}
static void horde_respawn_teammate(void *ctx, qa_actor_id actor) { respawn_new_travel(of(ctx), actor, NULL); }
static void restart_session(void *ctx, const char *map, double flags) {
  const qa_comp_q1_services *services = of(ctx)->services;
  services->restart_session(services->ctx, map, flags);
}

/* --- Q1MissionPackOptions --------------------------------------------------- */
static double pack_gamecfg(void *ctx) { return cvar(of(ctx), "gamecfg"); }
static double pack_team_color(void *ctx, qa_actor_id actor) {
  return qa_comp_q1_source_clients_team_color(&of(ctx)->clients, actor);
}
static void pack_set_team_color(void *ctx, qa_actor_id actor, double color) {
  if (!qa_comp_q1_source_clients_colors(&of(ctx)->clients, actor, color - 1, color - 1))
    qa_range_error("Out of memory");
}
static double client_frags(void *ctx, qa_actor_id actor) {
  return qa_comp_q1_source_clients_require(&of(ctx)->clients, actor)->frags;
}
static void disconnect_request(void *ctx, qa_actor_id actor) { request_disconnect(of(ctx), actor); }
static double pack_player_frame(void *ctx, qa_actor_id actor) { return selected_player(of(ctx), actor).frame; }
static const char *client_name(void *ctx, qa_actor_id actor) {
  return qa_comp_q1_source_client_name(qa_comp_q1_source_clients_require(&of(ctx)->clients, actor));
}
static bool pack_cheats_allowed(void *ctx) { return cvar(of(ctx), "sv_cheats") != 0; }
static bool pack_footsteps(void *ctx) { return cvar(of(ctx), "footsteps") == 1; }

/* --- Q1CtfServices (ctfServices()) ----------------------------------------- */
static bool ctf_is_bot(void *ctx, qa_actor_id actor) { return selected_player(of(ctx), actor).is_bot; }
static double ctf_captures(void *ctx, const char *team) {
  comp_t *comp = of(ctx);
  return strcmp(team, "red") == 0 ? comp->clients.red_captures : comp->clients.blue_captures;
}
static void ctf_add_capture(void *ctx, const char *team) {
  comp_t *comp = of(ctx);
  const double total = strcmp(team, "red") == 0 ? ++comp->clients.red_captures : ++comp->clients.blue_captures;
  qa_comp_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_COMP_Q1_EVENT_CTF_CAPTURE;
  event.team = team;
  event.total = total;
  emit(comp, &event);
}
static qa_q1ad_ctf_input ctf_input(void *ctx, qa_actor_id actor) {
  comp_t *comp = of(ctx);
  const qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&comp->clients, actor);
  const qa_q1_player_state *player = qa_q1_player(comp->game, actor);
  const qa_comp_q1_selected_player selected = selected_player(comp, actor);
  qa_q1ad_ctf_input input;
  memset(&input, 0, sizeof input);
  input.attack = player != NULL && player->attack_held;
  input.jump = player != NULL && player->jump_held;
  input.impulse = client->impulse;
  qa_item_id weapon = selected_weapon(comp, actor);
  input.grapple_selected = weapon != NULL && strcmp(weapon, GRAPPLE) == 0;
  input.view_angles = selected.view_angles;
  input.teleport_until = selected.teleport_until;
  input.frame = selected.frame;
  return input;
}
static void ctf_consume_impulse(void *ctx, qa_actor_id actor) {
  qa_comp_q1_source_clients_require(&of(ctx)->clients, actor)->impulse = 0;
}
static bool ctf_observer(void *ctx, qa_actor_id actor) {
  return qa_comp_q1_source_clients_require(&of(ctx)->clients, actor)->observer;
}
static void ctf_set_observer(void *ctx, qa_actor_id actor, bool enabled) {
  qa_comp_q1_source_clients_set_observer(&of(ctx)->clients, actor, enabled);
}
static void ctf_respawn(void *ctx, qa_actor_id actor, qa_q1_actor *spot) { respawn_new_travel(of(ctx), actor, spot); }
static void ctf_colors(void *ctx, qa_actor_id actor, double shirt, double pants) {
  if (!qa_comp_q1_source_clients_colors(&of(ctx)->clients, actor, shirt, pants)) qa_range_error("Out of memory");
}
static bool ctf_prompt_supported(void *ctx, qa_actor_id actor) {
  const qa_comp_q1_services *services = of(ctx)->services;
  return services->prompt_supported(services->ctx, actor);
}
static void ctf_prompt(void *ctx, qa_actor_id actor, const char *title, const qa_q1ad_ctf_prompt_choice *choices,
                       size_t count) {
  qa_comp_q1_prompt_choice *copied = malloc((count > 0 ? count : 1) * sizeof *copied);
  if (copied == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) copied[i] = (qa_comp_q1_prompt_choice){choices[i].label, choices[i].impulse};
  qa_comp_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_COMP_Q1_EVENT_PROMPT;
  event.actor = actor;
  event.title = title;
  event.choices = copied;
  event.choice_count = count;
  emit(of(ctx), &event);
  free(copied);
}
static void ctf_clear_prompt(void *ctx, qa_actor_id actor) {
  qa_comp_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_COMP_Q1_EVENT_CLEAR_PROMPT;
  event.actor = actor;
  emit(of(ctx), &event);
}
static void ctf_teleport(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles, qa_vec3 velocity, double until) {
  const qa_comp_q1_services *services = of(ctx)->services;
  services->teleport(services->ctx, actor, origin, angles, velocity, until);
}
static void ctf_select_grapple(void *ctx, qa_actor_id actor) {
  const qa_comp_q1_services *services = of(ctx)->services;
  (void)services->select_weapon(services->ctx, actor, GRAPPLE);
}
static qa_item_id ctf_selected_weapon(void *ctx, qa_actor_id actor) { return selected_weapon(of(ctx), actor); }
static qa_item_id ctf_selected_ammo(void *ctx, qa_actor_id actor) {
  const qa_comp_q1_services *services = of(ctx)->services;
  return services->selected_ammo(services->ctx, actor);
}
static void ctf_weapon_changed(void *ctx, qa_actor_id actor, qa_item_id acquired) {
  const qa_comp_q1_services *services = of(ctx)->services;
  services->weapon_changed(services->ctx, actor, acquired);
}
/* haste: actor => services.weaponChanged(actor, null); the closure takes the actor only. */
static void ctf_haste(void *ctx, qa_actor_id actor, bool enabled) {
  (void)enabled;
  ctf_weapon_changed(ctx, actor, NULL);
}
static void ctf_status(void *ctx, qa_actor_id actor, qa_q1ad_ctf_status status) {
  qa_comp_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_COMP_Q1_EVENT_CTF_STATUS;
  event.actor = actor;
  event.status = &status;
  emit(of(ctx), &event);
}
static void ctf_log(void *ctx, qa_actor_id actor, const char *action) {
  comp_t *comp = of(ctx);
  const qa_comp_q1_source_client *client = qa_comp_q1_source_clients_get(&comp->clients, actor);
  log_line(comp, "LOG:\t", action, "\t", client != NULL ? qa_comp_q1_source_client_name(client) : "", "\n", NULL);
  qa_comp_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_COMP_Q1_EVENT_SOURCE_LOG;
  event.actor = actor;
  event.action = action;
  emit(of(ctx), &event);
}
static qa_q1ad_ctf_services ctf_services(comp_t *comp) {
  qa_q1ad_ctf_services services;
  memset(&services, 0, sizeof services);
  services.ctx = comp;
  services.name = client_name;
  services.score = client_frags;
  services.add_score = horde_add_score;
  services.is_bot = ctf_is_bot;
  services.captures = ctf_captures;
  services.add_capture = ctf_add_capture;
  services.input = ctf_input;
  services.consume_impulse = ctf_consume_impulse;
  services.observer = ctf_observer;
  services.set_observer = ctf_set_observer;
  services.respawn = ctf_respawn;
  services.disconnect = disconnect_request;
  services.colors = ctf_colors;
  services.prompt_supported = ctf_prompt_supported;
  services.prompt = ctf_prompt;
  services.clear_prompt = ctf_clear_prompt;
  services.teleport = ctf_teleport;
  services.select_grapple = ctf_select_grapple;
  services.selected_weapon = ctf_selected_weapon;
  services.selected_ammo = ctf_selected_ammo;
  services.weapon_changed = ctf_weapon_changed;
  services.haste = ctf_haste;
  services.status = ctf_status;
  services.log = ctf_log;
  return services;
}

/* --- damage source effects -------------------------------------------------- */
static bool coop_bots_before_quad(void *ctx, qa_damage_request request, double amount, qa_combat_state target,
                                  const qa_combat_state *attacker_state, qa_damage_preparation *out) {
  (void)target;
  (void)attacker_state;
  comp_t *comp = of(ctx);
  const qa_actor_id *attacker = request.attack.attacker;
  if (comp->game->options.coop && attacker != NULL && !qa_same_actor(request.target, *attacker) &&
      qa_comp_q1_source_clients_get(&comp->clients, request.target) != NULL &&
      qa_comp_q1_source_clients_get(&comp->clients, *attacker) != NULL && selected_player(comp, *attacker).is_bot &&
      !selected_player(comp, request.target).is_bot) {
    *out = (qa_damage_preparation){false, 0};
    return true;
  }
  *out = (qa_damage_preparation){true, amount};
  return true;
}
static qa_q1_lethal_health mg3_buddha_lethal_health(void *ctx, qa_damage_request request, double health,
                                                    qa_combat_state target, const qa_combat_state *attacker) {
  (void)target;
  (void)attacker;
  if (qa_q1ad_player_number(of(ctx)->addon, request.target, "buddha") != 0)
    return (qa_q1_lethal_health){true, 1, "none"};
  return (qa_q1_lethal_health){true, health, "death"};
}

/* --- "q1:source-clients" state extension ------------------------------------ */
static bool clients_capture(void *ctx, uint8_t **out, size_t *out_length) {
  *out = qa_comp_q1_source_clients_capture(&of(ctx)->clients, out_length);
  return *out != NULL;
}
static void clients_restore(void *ctx, const uint8_t *bytes, size_t length) {
  if (!qa_comp_q1_source_clients_restore(&of(ctx)->clients, bytes, length)) qa_range_error("Out of memory");
}

/* --- game.adjoin(() => ...) bodies ------------------------------------------ */
static void adjoin_mg1_addons(void *ctx, qa_q1_services *game) {
  (void)game;
  comp_t *comp = of(ctx);
  comp->horde_addon = qa_q1ad_register_q1_campaign_addons(comp->base, QA_Q1AD_MG1, comp->addon_services);
  if (comp->horde_addon == NULL) qa_range_error("Out of memory");
}
static void adjoin_horde(void *ctx, qa_q1_services *game) {
  (void)game;
  comp_t *comp = of(ctx);
  comp->horde = qa_q1ad_register_horde(comp->horde_addon, comp->horde_services);
  if (comp->horde == NULL) qa_range_error("Out of memory");
}

static qa_q1ad_program addon_program(qa_comp_q1_program program) {
  return program == QA_COMP_Q1_DOPA ? QA_Q1AD_DOPA : program == QA_COMP_Q1_MG1 ? QA_Q1AD_MG1 : QA_Q1AD_MG3;
}

/* --- constructor -------------------------------------------------------------- */
void qa_comp_q1_source_composition_init(comp_t *comp, qa_q1_services *game, const qa_comp_q1_source_selection *selection,
                                        const qa_comp_q1_services *services) {
  memset(comp, 0, sizeof *comp);
  comp->game = game;
  comp->selection = *selection;
  comp->services = services;
  const qa_comp_q1_program program = selection->program;
  qa_compat_qc_finale_ack_init(&comp->finale_acknowledgement);
  qa_comp_q1_register_q3_pickup_hook(game);
  qa_comp_q1_source_clients_init(&comp->clients, game, program, services);
  if (is_rerelease(comp) &&
      (program == QA_COMP_Q1_ID1 || program == QA_COMP_Q1_HIPNOTIC || program == QA_COMP_Q1_ROGUE)) {
    qa_q1_damage_source_effects effects;
    memset(&effects, 0, sizeof effects);
    effects.ctx = comp;
    effects.before_quad = coop_bots_before_quad;
    qa_q1_register_damage_source_effects(game, "q1:coop-bots", effects);
  }
  if (program == QA_COMP_Q1_ROGUE) qa_q1_set_base_team_health(game, false);
  comp->campaign = (qa_q1_campaign_binding){comp, campaign_read_flags, campaign_write_flags, campaign_set_skill};
  memset(&comp->base_options, 0, sizeof comp->base_options);
  comp->base_options.campaign = &comp->campaign;
  comp->base_options.has_registered = true;
  comp->base_options.registered = selection->registered;
  comp->base_options.has_official_campaign = true;
  comp->base_options.official_campaign = selection->official_campaign;
  comp->base_options.ctx = comp;
  comp->base_options.same_level = base_same_level;
  comp->base_options.player_exited = base_player_exited;
  comp->base_options.finish_campaign = base_finish_campaign;
  comp->base_options.finale_finished = base_finale_finished;
  comp->base = qa_q1_register_base(game, &comp->base_options);
  if (comp->base == NULL) qa_range_error("Out of memory");
  comp->addon_services = (qa_q1ad_services){comp, cheat_arsenal, addon_emit, addon_is_monster, addon_cvar, addon_set_cvar};
  comp->horde_services = (qa_q1ad_horde_services){comp,           horde_dead_flag,        horde_no_target, horde_is_bot,
                                                  horde_respawn_teammate, horde_add_score, restart_session};
  if (program == QA_COMP_Q1_HIPNOTIC || program == QA_COMP_Q1_ROGUE) {
    qa_q1mp_mission_pack_options *options = &comp->pack_options;
    memset(options, 0, sizeof *options);
    options->world.ctx = comp;
    options->world.gamecfg = pack_gamecfg;
    options->world.team_color = pack_team_color;
    options->world.set_team_color = pack_set_team_color;
    options->world.add_frags = horde_add_score;
    options->world.frags = client_frags;
    options->world.disconnect = disconnect_request;
    options->world.player_frame = pack_player_frame;
    options->world.player_name = client_name;
    options->command.ctx = comp;
    options->command.cheat_arsenal = cheat_arsenal;
    options->command.cheats_allowed = pack_cheats_allowed;
    options->command.developer_message = developer_message;
    options->footsteps_ctx = comp;
    options->footsteps = pack_footsteps;
    comp->packs = malloc(sizeof *comp->packs);
    if (comp->packs == NULL) qa_range_error("Out of memory");
    (void)qa_q1mp_register_q1_mission_pack(game, comp->base, qa_comp_q1_program_name(program), options, comp->packs);
  } else if (program == QA_COMP_Q1_CTF) {
    comp->addon = qa_q1ad_context_create(comp->base, QA_Q1AD_CTF, comp->addon_services);
    if (comp->addon == NULL) qa_range_error("Out of memory");
    comp->ctf = qa_q1ad_register_ctf(comp->addon, ctf_services(comp), services->shared_grapple);
    if (comp->ctf == NULL) qa_range_error("Out of memory");
  } else if (program == QA_COMP_Q1_DOPA || program == QA_COMP_Q1_MG1 || program == QA_COMP_Q1_MG3) {
    comp->addon = qa_q1ad_register_q1_campaign_addons(comp->base, addon_program(program), comp->addon_services);
    if (comp->addon == NULL) qa_range_error("Out of memory");
    if (program == QA_COMP_Q1_MG1 || program == QA_COMP_Q1_DOPA) {
      comp->horde = qa_q1ad_register_horde(comp->addon, comp->horde_services);
      if (comp->horde == NULL) qa_range_error("Out of memory");
    }
  }
  if (comp->horde == NULL && selection->has_horde && selection->horde) {
    /* Horde is a match mode, not a campaign: authored horde maps must work
     * on every Q1 program (including custom games). MG3 already registered
     * the MG1 monster roster; other programs pull the MG1 campaign addons
     * in alongside their own so horde squads and loot can spawn. */
    if (program == QA_COMP_Q1_MG3) {
      comp->horde_addon = qa_q1ad_context_create(comp->base, QA_Q1AD_MG1, comp->addon_services);
      if (comp->horde_addon == NULL) qa_range_error("Out of memory");
    } else {
      qa_q1_adjoin(game, adjoin_mg1_addons, comp);
    }
    qa_q1_adjoin(game, adjoin_horde, comp);
  }
  if (comp->addon != NULL && comp->addon->program == QA_Q1AD_MG3) {
    qa_q1_damage_source_effects effects;
    memset(&effects, 0, sizeof effects);
    effects.ctx = comp;
    effects.lethal_health = mg3_buddha_lethal_health;
    qa_q1_register_damage_source_effects(game, "q1:mg3:buddha", effects);
  }
  comp->clients_extension = (qa_q1_state_extension){comp, "q1:source-clients", clients_capture, clients_restore, NULL};
  qa_q1_register_state_extension(game, &comp->clients_extension);
}

void qa_comp_q1_source_composition_destroy(comp_t *comp) {
  qa_comp_q1_source_clients_destroy(&comp->clients);
  if (comp->horde != NULL) qa_q1ad_horde_destroy(comp->horde);
  if (comp->ctf != NULL) qa_q1ad_ctf_destroy(comp->ctf);
  if (comp->horde_addon != NULL && comp->horde_addon != comp->addon) qa_q1ad_context_destroy(comp->horde_addon);
  if (comp->addon != NULL) qa_q1ad_context_destroy(comp->addon);
  if (comp->packs != NULL) {
    qa_q1mp_mission_pack_runtime_destroy(comp->packs);
    free(comp->packs);
  }
  if (comp->base != NULL) qa_q1_base_destroy(comp->base);
  qa_compat_qc_finale_ack_destroy(&comp->finale_acknowledgement);
  free(comp->pending_disconnects);
  if (comp->owns_game && comp->game != NULL) {
    if (comp->game->named != NULL) qa_q1_services_free(comp->game);
    free(comp->game);
  }
  memset(comp, 0, sizeof *comp);
}

/* --- map admission ------------------------------------------------------------ */
/* map.source.replace(/^.*[/\\]/u, "").replace(/\.bsp$/u, "") === "e1m8" */
static bool source_is_e1m8(const char *source) {
  const char *name = source;
  for (const char *cursor = source; *cursor != '\0'; cursor++)
    if (*cursor == '/' || *cursor == '\\') name = cursor + 1;
  return strcmp(name, "e1m8") == 0 || strcmp(name, "e1m8.bsp") == 0;
}

bool qa_comp_q1_source_composition_spawn_map(comp_t *comp, const qa_q1_map *map, qa_q1_spawn_report *report) {
  set_cvar(comp, "sv_gravity", source_is_e1m8(map->source != NULL ? map->source : "") ? "100" : "800");
  qa_q1_spawn_report discarded;
  const bool ok = qa_q1_foundation_spawn_map(comp->game, map, report != NULL ? report : &discarded);
  if (report == NULL && ok) qa_q1_spawn_report_free(&discarded);
  return ok;
}

bool qa_comp_q1_source_composition_spawn_entities(comp_t *comp, const qa_q1_entity *entity_list, size_t entity_count,
                                                  const qa_bounds *model_bounds, size_t model_count,
                                                  const char *map_name, const double *keep, size_t keep_count,
                                                  qa_q1_spawn_report *report) {
  set_cvar(comp, "sv_gravity", strcmp(map_name, "e1m8") == 0 ? "100" : "800");
  qa_q1_spawn_report discarded;
  const bool ok = qa_q1_foundation_spawn_entities(comp->game, entity_list, entity_count, model_bounds, model_count,
                                                  map_name, keep, keep_count, report != NULL ? report : &discarded);
  if (report == NULL && ok) qa_q1_spawn_report_free(&discarded);
  return ok;
}

/* --- clients ------------------------------------------------------------------- */
bool qa_comp_q1_source_composition_attach(comp_t *comp, qa_owned_actor actor, const qa_comp_q1_client_admission *admission) {
  if (qa_comp_q1_source_clients_attach(&comp->clients, actor, admission) == NULL) return false;
  notice(comp, actor.id, "connect");
  const double time = comp->game->time;
  const bool same_level = cvar(comp, "samelevel") != 0;
  const qa_q1_intermission_result result = qa_q1_level_rules_client_connected(level_rules(comp), time, same_level);
  present_intermission(comp, &result);
  return true;
}

/* The outer session has already initialized the selected character, source arsenal and travel. */
void qa_comp_q1_source_composition_spawned(comp_t *comp, qa_actor_id actor, bool first_admission, qa_q1_actor *spot) {
  qa_q1_services *game = comp->game;
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&comp->clients, actor);
  client->death_recorded = false;
  client->impulse = 0;
  client->use = false;
  client->respawn_requested_at = -1;
  if (!first_admission) {
    qa_q1_level_rules_reset_player(level_rules(comp), client->actor);
    if (comp->horde != NULL) qa_q1ad_horde_restore_keys(comp->horde, actor);
  }
  if (spot != NULL && game->source_fog_push.fn != NULL &&
      !game->source_fog_push.fn(game->source_fog_push.ctx, actor, spot) && game->world != NULL)
    (void)game->source_fog_push.fn(game->source_fog_push.ctx, actor, game->world);
  qa_comp_q1_source_clients_spawned(&comp->clients, actor);
  if (comp->packs != NULL) qa_q1mp_runtime_player_spawned(comp->packs, actor);
  if (comp->ctf != NULL) qa_q1ad_ctf_spawn_player(comp->ctf, actor, first_admission);
}

bool qa_comp_q1_source_composition_userinfo(comp_t *comp, qa_actor_id actor, const qa_comp_userinfo_entry *values,
                                            size_t count) {
  return qa_comp_q1_source_clients_update(&comp->clients, actor, values, count);
}

bool qa_comp_q1_source_composition_no_target(comp_t *comp, qa_actor_id actor) {
  const qa_comp_q1_source_client *client = qa_comp_q1_source_clients_get(&comp->clients, actor);
  return client != NULL && client->no_target;
}

void qa_comp_q1_source_composition_set_no_target(comp_t *comp, qa_actor_id actor, bool enabled) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&comp->clients, actor);
  client->no_target = enabled;
  qa_comp_q1_source_clients_publish(&comp->clients, client);
}

void qa_comp_q1_source_composition_input(comp_t *comp, qa_actor_id actor, qa_comp_q1_source_input input) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&comp->clients, actor);
  const qa_comp_q1_selected_player selected = selected_player(comp, actor);
  if (input.impulse != 0) client->impulse = input.impulse;
  client->use = input.use;
  const double teleport_until = selected.teleport_until;
  qa_q1_services_player_input(comp->game, client->actor, input.attack, input.jump, &teleport_until);
}

/* --- frame phases ---------------------------------------------------------------- */
void qa_comp_q1_source_composition_pre_frame(comp_t *comp, double elapsed_seconds) {
  flush_disconnects(comp);
  if (comp->addon != NULL) qa_q1ad_frame(comp->addon, elapsed_seconds);
  if (comp->horde_addon != NULL && comp->horde_addon != comp->addon) qa_q1ad_frame(comp->horde_addon, elapsed_seconds);
  if (comp->game->options.deathmatch != 0 || comp->game->options.coop) {
    const double time = comp->game->time;
    const size_t count = comp->clients.record_count;
    double *frags = malloc((count > 0 ? count : 1) * sizeof *frags);
    if (frags == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < count; i++) frags[i] = comp->clients.records[i].frags;
    const double timelimit = cvar(comp, "timelimit");
    const double fraglimit = cvar(comp, "fraglimit");
    (void)qa_q1_level_rules_check_limits(level_rules(comp), time, frags, count, timelimit, fraglimit);
    free(frags);
  }
}

/* Before game.playerFrame: source observer/team/rune prethink precedes environment and movement. */
void qa_comp_q1_source_composition_player_pre_think(comp_t *comp, qa_actor_id actor) {
  if (comp->ctf != NULL) qa_q1ad_ctf_player_frame(comp->ctf, actor);
}

/* After selected movement and game.playerAfterPhysics, which already invokes pack and horde extensions. */
void qa_comp_q1_source_composition_player_post_think(comp_t *comp, qa_actor_id actor) {
  if (comp->ctf != NULL) qa_q1ad_ctf_after_physics(comp->ctf, actor);
  if (comp->addon != NULL) qa_q1ad_frame_player(comp->addon, actor, selected_player(comp, actor).view_offset);
}

/* q1CtfWeaponCycle's select closure. */
typedef struct cycle_select_ctx {
  comp_t *comp;
  qa_actor_id actor;
  qa_q1_services *weapons;
  qa_q1_player_state *weapon_player;
} cycle_select_ctx;
static bool cycle_select(void *ctx, qa_item_id item, qa_q1_base_weapon weapon) {
  cycle_select_ctx *select = ctx;
  if (weapon == NULL) {
    const qa_comp_q1_services *services = select->comp->services;
    (void)services->select_weapon(services->ctx, select->actor, item);
    return true;
  }
  return qa_q1_select_weapon(select->weapons, select->weapon_player->actor, weapon);
}

bool qa_comp_q1_source_composition_impulse(comp_t *comp, qa_actor_id actor) {
  const qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&comp->clients, actor);
  qa_q1_player_state *player = qa_q1_player(comp->game, actor);
  qa_q1_services *weapons = comp->services->weapon_services == NULL
                                ? comp->game
                                : comp->services->weapon_services(comp->services->ctx, actor);
  qa_q1_player_state *weapon_player = weapons != NULL ? qa_q1_player(weapons, actor) : NULL;
  if (client->impulse == 0 || (weapon_player != NULL && weapons != NULL && weapons->time < weapon_player->attack_finished))
    return false;
  if (comp->ctf != NULL && qa_q1ad_ctf_impulse(comp->ctf, actor)) return true;
  client = qa_comp_q1_source_clients_require(&comp->clients, actor);
  const double impulse = client->impulse;
  if (comp->ctf != NULL && (impulse == 10 || impulse == 12) && weapons != NULL && weapon_player != NULL) {
    cycle_select_ctx select = {comp, actor, weapons, weapon_player};
    (void)qa_comp_q1_ctf_weapon_cycle(weapons, weapon_player, selected_weapon(comp, actor), impulse == 12, GRAPPLE,
                                      cycle_select, &select);
    clear_impulse(comp, actor);
    return true;
  }
  if (comp->packs != NULL && qa_q1mp_runtime_impulse(comp->packs, actor, impulse)) {
    clear_impulse(comp, actor);
    return true;
  }
  /* sv_gate_dev_impulses: developer impulses are inert in multiplayer without sv_cheats. */
  if (comp->addon != NULL && qa_q1ad_is_developer_impulse(comp->addon->program, impulse) &&
      cvar(comp, "sv_gate_dev_impulses") != 0 &&
      (comp->game->options.deathmatch != 0 || comp->game->options.coop) && cvar(comp, "sv_cheats") == 0) {
    clear_impulse(comp, actor);
    return true;
  }
  if (comp->addon != NULL && qa_q1ad_handle_mg3_item_impulse(comp->addon, actor, impulse, developer_message, comp)) {
    clear_impulse(comp, actor);
    return true;
  }
  if (comp->addon != NULL && qa_q1ad_handle_impulse(comp->addon, actor, impulse)) {
    clear_impulse(comp, actor);
    return true;
  }
  if (weapons != NULL && weapon_player != NULL && qa_comp_q1_weapon_impulse(weapons, weapon_player, impulse)) {
    clear_impulse(comp, actor);
    return true;
  }
  if (player == NULL) return false;
  const qa_comp_q1_impulse_source source = {comp->game, comp->services, comp->selection.program,
                                            qa_q1_base_campaign(comp->base)};
  (void)qa_comp_base_q1_impulse(&source, player, impulse);
  clear_impulse(comp, actor);
  return true;
}

/* Invoke only after an accepted shot, including a foreign arsenal using source campaign rules. */
void qa_comp_q1_source_composition_fired(comp_t *comp, qa_actor_id actor, qa_item_id weapon) {
  const qa_comp_q1_source_client *client = qa_comp_q1_source_clients_get(&comp->clients, actor);
  if (client == NULL) return;
  qa_q1_level_rules_note_attack(level_rules(comp), client->actor, weapon != NULL && strcmp(weapon, "q1:weapon/axe") == 0);
}

/* Runs inside GameplayAuthority.beforeReaction, before character death callbacks and item drops. */
void qa_comp_q1_source_composition_before_reaction(comp_t *comp, qa_owned_actor actor, const qa_damage_decision *decision) {
  qa_q1_services *game = comp->game;
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_get(&comp->clients, actor.id);
  const bool death = strcmp(decision->reaction, "death") == 0;
  if (death && (client != NULL || qa_q1_get_entity(game, &actor.id) != NULL) && qa_q1_health(game, actor.id) < -99)
    qa_gameplay_authority_set_health(game->host.combat, actor, -99);
  /* Rogue combat.qc Killed: the buzzsaw, the Vengeance Sphere and the pendulum always gib. */
  {
    const qa_actor_id *killer = decision->request.attack.attacker;
    if (death && comp->packs != NULL && strcmp(comp->packs->pack, "rogue") == 0 && killer != NULL) {
      const char *classname = game->host.classname(game->host.ctx, *killer);
      if (strcmp(classname, "buzzsaw") == 0 || strcmp(classname, "Vengeance") == 0 || strcmp(classname, "pendulum") == 0)
        qa_gameplay_authority_set_health(game->host.combat, actor, -99);
    }
  }
  const qa_actor_id *attacker = decision->request.attack.attacker;
  if (client == NULL) {
    const qa_q1_actor *entity = qa_q1_get_entity(game, &actor.id);
    if (!death || entity == NULL || strcmp(entity->movement, "none") == 0 || strcmp(entity->movement, "push") == 0)
      return;
    if (is_rerelease(comp) && is_monster(comp, actor.id) && attacker != NULL &&
        qa_comp_q1_source_clients_get(&comp->clients, *attacker) != NULL &&
        strcmp(qa_q1_actor_text(entity, "horde.sourceDie"), "") == 0)
      qa_comp_q1_source_clients_add_score(&comp->clients, *attacker, 1);
    /* ClientObituary consumes its source rnum even when the victim is not a player. */
    (void)game->host.random(game->host.ctx);
    return;
  }
  qa_q1_level_rules_note_damage(level_rules(comp), actor, decision->applied_damage);
  if (decision->applied_damage > 0 && comp->packs != NULL)
    qa_q1mp_runtime_confirmed_damage(comp->packs, actor.id, attacker);
  client = qa_comp_q1_source_clients_require(&comp->clients, actor.id);
  if (!death || client->death_recorded) return;
  client->death_recorded = true;
  const qa_q1_actor *source_attacker = attacker != NULL ? qa_q1_get_entity(game, attacker) : NULL;
  const qa_attack_cause *cause = &decision->request.attack.cause;
  qa_q1_obituary_input input;
  memset(&input, 0, sizeof input);
  input.edition = game->options.edition;
  input.victim = obituary_actor(comp, actor.id);
  qa_q1_obituary_actor attacker_actor, telefrag_owner;
  if (attacker != NULL) {
    attacker_actor = obituary_actor(comp, *attacker);
    input.attacker = &attacker_actor;
  }
  if (source_attacker != NULL && source_attacker->has_owner) {
    telefrag_owner = obituary_actor(comp, source_attacker->owner);
    input.telefrag_owner = &telefrag_owner;
  }
  input.teamplay = cvar(comp, "teamplay");
  input.death_type = cause->kind == QA_ATTACK_CAUSE_Q1 ? (cause->death_type != NULL ? cause->death_type : "")
                     : cause->kind == QA_ATTACK_CAUSE_ENVIRONMENT && cause->hazard != NULL &&
                             strcmp(cause->hazard, "fall") == 0
                         ? "falling"
                         : "";
  input.random_ctx = comp;
  input.random = host_random;
  /* TS obituaryVariant. */
  input.variant = comp->ctf != NULL ? "ctf"
                  : comp->addon != NULL && (comp->addon->program == QA_Q1AD_MG1 || comp->addon->program == QA_Q1AD_MG3)
                      ? "mg"
                      : NULL;
  const qa_q1_obituary obituary = comp->packs != NULL
                                      ? qa_q1mp_runtime_obituary(comp->packs, &input, decision->request.attack.inflictor)
                                      : qa_q1_obituary_compute(&input);
  if (obituary.has_message) broadcast(comp, obituary.text, obituary.arguments, obituary.argument_count);
  if (comp->ctf != NULL) {
    qa_q1ad_ctf_death(comp->ctf, actor.id, attacker);
    if (obituary.has_log) log_death(comp, actor.id, obituary.log_what, obituary.log_has_versus, obituary.log_versus);
  } else {
    if (obituary.has_score && qa_comp_q1_source_clients_get(&comp->clients, obituary.score_actor) != NULL)
      qa_comp_q1_source_clients_add_score(&comp->clients, obituary.score_actor, obituary.score_delta);
    if (attacker != NULL && qa_comp_q1_source_clients_get(&comp->clients, *attacker) != NULL && comp->horde != NULL)
      qa_q1ad_horde_teammate_killed(comp->horde, *attacker);
  }
  if (obituary.has_achievement) {
    qa_q1_event event;
    memset(&event, 0, sizeof event);
    event.kind = "achievement";
    event.has_player = true;
    event.player = obituary.achievement_actor;
    event.id = obituary.achievement_id;
    qa_q1_emit(game, event);
  }
  if (comp->packs != NULL) qa_q1mp_runtime_player_died(comp->packs, actor.id, attacker);
}

void qa_comp_q1_source_composition_disconnect(comp_t *comp, qa_actor_id actor) {
  const int64_t slot = qa_comp_q1_source_clients_require(&comp->clients, actor)->slot;
  if (comp->ctf != NULL) qa_q1ad_ctf_disconnect_player(comp->ctf, actor);
  if (comp->packs != NULL) qa_q1mp_runtime_player_died(comp->packs, actor, NULL);
  notice(comp, actor, "disconnect");
  qa_comp_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_COMP_Q1_EVENT_CLIENT_LEFT;
  event.actor = actor;
  event.slot = slot;
  emit(comp, &event);
  comp->services->disconnect(comp->services->ctx, actor);
}

/* --- respawn and spawn selection ---------------------------------------------------- */
void qa_comp_q1_source_composition_request_respawn(comp_t *comp, qa_actor_id actor, const qa_q1_travel_state *coop_entry) {
  if (comp->horde != NULL && qa_q1ad_horde_request_respawn(comp->horde)) return;
  const qa_q1_options *options = &comp->game->options;
  if (!options->coop && options->deathmatch == 0) {
    const char *map = comp->game->map_name != NULL ? comp->game->map_name : "";
    comp->services->restart_session(comp->services->ctx, map, read_flags(comp));
    return;
  }
  if (options->coop && coop_entry != NULL) respawn_at(comp, actor, NULL, coop_entry);
  else respawn_new_travel(comp, actor, NULL);
}

static void respawn_at(comp_t *comp, qa_actor_id actor, qa_q1_actor *source_spot, const qa_q1_travel_state *travel) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&comp->clients, actor);
  if (client->respawn_requested_at < 0) client->respawn_requested_at = comp->game->time;
  qa_q1_actor *spot = source_spot != NULL
                          ? source_spot
                          : qa_comp_q1_source_composition_select_spawn(
                                comp, &actor, comp->game->time >= client->respawn_requested_at + 5);
  if (spot == NULL) return;
  const qa_owned_actor owned = qa_comp_q1_source_clients_require(&comp->clients, actor)->actor;
  comp->services->place_player(comp->services->ctx, owned, spot, travel);
  qa_comp_q1_source_composition_spawned(comp, actor, false, spot);
  if (comp->addon != NULL && (comp->addon->program == QA_Q1AD_MG1 || comp->addon->program == QA_Q1AD_MG3))
    qa_q1ad_restore_coop_weapons(comp->addon, actor, spot);
}

qa_q1_actor *qa_comp_q1_source_composition_select_spawn(comp_t *comp, const qa_actor_id *actor, bool force) {
  if (actor != NULL && comp->ctf != NULL) return qa_q1ad_ctf_select_spawn(comp->ctf, *actor);
  if (actor != NULL && comp->game->options.coop && horde_has_manager(comp)) {
    qa_q1_actor *free_spot = horde_coop_spawn(comp, *actor);
    if (free_spot != NULL) return free_spot;
  }
  if (actor != NULL && comp->packs != NULL) {
    qa_q1_actor *point = qa_q1mp_runtime_select_spawn(comp->packs, *actor);
    if (point != NULL) return point;
  }
  return qa_q1_spawn_selector_select(qa_q1_base_spawn_selector(comp->base), force);
}

/* --- travel ---------------------------------------------------------------------------- */
qa_q1_travel_state qa_comp_q1_source_composition_new_travel(comp_t *comp) {
  if (comp->ctf != NULL) return qa_q1ad_new_q1_ctf_travel(comp->ctf);
  if (comp->packs != NULL) return qa_q1mp_runtime_new_travel(comp->packs);
  if (comp->addon != NULL) return qa_q1ad_new_travel(comp->addon);
  return qa_q1_new_travel(&comp->game->options);
}

qa_q1_travel_state qa_comp_q1_source_composition_capture_travel(comp_t *comp, qa_owned_actor actor) {
  if (comp->ctf != NULL) return qa_q1ad_capture_q1_ctf_travel(comp->ctf, actor);
  if (comp->packs != NULL) return qa_q1mp_runtime_capture_travel(comp->packs, actor);
  if (comp->addon != NULL) return qa_q1ad_capture_travel(comp->addon, actor);
  const qa_q1_travel_policy policy = {false, false};
  return qa_q1_capture_travel(comp->game, actor, selected_base_weapon(comp, actor.id), NULL, policy);
}

/* qa_q1_decode_travel shares the input's extensions (TS returns them as is);
 * the composition returns an owned state, so it copies them. */
static void own_extensions(qa_q1_travel_state *state) {
  const qa_q1_travel_extension *shared = state->extensions;
  const size_t count = state->extension_count;
  state->extensions = NULL;
  state->extension_count = 0;
  if (count == 0) return;
  qa_q1_travel_extension *copies = calloc(count, sizeof *copies);
  if (copies == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    const size_t id_length = strlen(shared[i].id) + 1;
    copies[i].id = malloc(id_length);
    copies[i].bytes = malloc(shared[i].length > 0 ? shared[i].length : 1);
    if (copies[i].id == NULL || copies[i].bytes == NULL) qa_range_error("Out of memory");
    memcpy(copies[i].id, shared[i].id, id_length);
    if (shared[i].length > 0) memcpy(copies[i].bytes, shared[i].bytes, shared[i].length);
    copies[i].length = shared[i].length;
  }
  state->extensions = copies;
  state->extension_count = count;
}

qa_q1_travel_state qa_comp_q1_source_composition_decode_travel(comp_t *comp, const qa_q1_travel_state *state) {
  if (comp->ctf != NULL) return qa_q1ad_decode_q1_ctf_travel(comp->ctf, state);
  if (comp->packs != NULL) return qa_q1mp_runtime_decode_travel(comp->packs, state, read_flags(comp));
  if (comp->addon != NULL) return qa_q1ad_decode_travel(comp->addon, state);
  qa_q1_travel_state decoded = qa_q1_decode_travel(comp->game, state, read_flags(comp));
  own_extensions(&decoded);
  return decoded;
}

void qa_comp_q1_source_composition_admit_travel(comp_t *comp, qa_owned_actor actor, const qa_q1_travel_state *state) {
  if (comp->packs != NULL) {
    qa_q1_travel_state decoded = qa_q1mp_runtime_decode_travel(comp->packs, state, read_flags(comp));
    qa_q1mp_runtime_admit_travel(comp->packs, actor, &decoded);
    qa_q1_travel_state_destroy(&decoded);
    return;
  }
  if (comp->addon != NULL && comp->ctf == NULL) {
    qa_q1ad_admit_travel(comp->addon, actor, state);
    return;
  }
  qa_q1_travel_state decoded = qa_comp_q1_source_composition_decode_travel(comp, state);
  qa_q1_admit_travel(comp->game, actor, &decoded);
  qa_q1_travel_state_destroy(&decoded);
}

/* The selected inventory calls this for a Q1 source death drop exactly once. */
qa_q1_actor *qa_comp_q1_source_composition_drop_inventory(comp_t *comp, qa_owned_actor actor) {
  qa_q1_services *game = comp->game;
  if (!game->options.coop && game->options.deathmatch == 0) return NULL;
  if (horde_has_manager(comp)) return NULL;
  if (comp->packs != NULL) return qa_q1mp_runtime_drop_backpack(comp->packs, actor);
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor.id);
  if (!body.has_value) qa_range_error("Q1 death drop has no shared body");
  qa_q1_backpack_contents contents;
  memset(&contents, 0, sizeof contents);
  contents.weapon = selected_base_weapon(comp, actor.id);
  contents.shells = qa_shared_inventory_table_count(game->host.inventory, actor.id, "q1:ammo/shells");
  contents.nails = qa_shared_inventory_table_count(game->host.inventory, actor.id, "q1:ammo/nails");
  contents.rockets = qa_shared_inventory_table_count(game->host.inventory, actor.id, "q1:ammo/rockets");
  contents.cells = qa_shared_inventory_table_count(game->host.inventory, actor.id, "q1:ammo/cells");
  return qa_q1_drop_backpack(game, body.value.origin, &contents, NULL);
}

/* --- intermission and presentation ---------------------------------------------------- */
qa_q1_intermission_result qa_comp_q1_source_composition_request_intermission_exit(
    comp_t *comp, bool pressed, const qa_comp_q1_intermission_input *input) {
  if (input != NULL) {
    const qa_actor_id id = qa_comp_q1_source_clients_require(&comp->clients, input->actor)->actor.id;
    qa_q1_player_state *player = qa_q1_player(comp->game, id);
    if (player != NULL) player->attack_held = input->attack;
  }
  const double time = comp->game->time;
  const bool same_level = cvar(comp, "samelevel") != 0;
  const qa_q1_intermission_result result = qa_q1_level_rules_request_exit(level_rules(comp), time, pressed, same_level);
  present_intermission(comp, &result);
  return result;
}

void qa_comp_q1_source_composition_dismiss_finale(comp_t *comp) {
  qa_compat_qc_finale_ack_dismiss(&comp->finale_acknowledgement, comp->game->time);
  qa_q1_base_dismiss_finale(comp->base);
}

qa_q1_character_source_pose qa_comp_q1_source_composition_character_pose(comp_t *comp, qa_actor_id actor) {
  qa_q1_character_source_pose pose;
  memset(&pose, 0, sizeof pose);
  if (comp->ctf != NULL) {
    const qa_q1ad_ctf_character_pose ctf_pose = qa_q1ad_ctf_player_pose(comp->ctf, actor);
    pose.has_axe_pose = true;
    pose.axe_pose = ctf_pose.axe_pose;
    pose.has_frame = ctf_pose.has_frame;
    pose.frame = ctf_pose.frame;
    return pose;
  }
  qa_q1_player_state *player = qa_q1_player(comp->game, actor);
  if (comp->addon != NULL && comp->addon->program == QA_Q1AD_MG3 && player != NULL) {
    pose.has_frame = qa_q1ad_mg3_hammer_body_frame(comp->addon, player, &pose.frame);
    if (!pose.has_frame) pose.frame = 0;
    return pose;
  }
  if (comp->packs != NULL) return qa_q1mp_runtime_character_pose(comp->packs, actor);
  return pose;
}

bool qa_comp_q1_source_composition_fall_damage_allowed(comp_t *comp, qa_actor_id actor) {
  return comp->ctf != NULL ? qa_q1ad_ctf_fall_damage_allowed(comp->ctf, actor) : true;
}

void qa_comp_q1_source_composition_character_frame(comp_t *comp, qa_actor_id actor,
                                                   const qa_q1_character_presentation *presentation) {
  if (comp->packs != NULL) qa_q1mp_runtime_character_frame(comp->packs, actor, presentation);
}

void qa_comp_q1_source_composition_suicide(comp_t *comp, qa_actor_id actor) {
  if (comp->ctf != NULL) {
    qa_q1ad_ctf_suicide(comp->ctf, actor);
    return;
  }
  if (comp->packs != NULL && !qa_q1mp_runtime_suicide(comp->packs, actor)) return;
  notice(comp, actor, "suicide");
  /* ClientKill in Horde runs PlayerDie; the post-death respawn request owns the restart logic. */
  if (horde_has_manager(comp)) {
    (void)qa_q1_damage(comp->game, actor, actor, &actor, 100000, NULL, "direct", "suicide", "bypass");
    return;
  }
  qa_comp_q1_source_composition_request_respawn(comp, actor, NULL);
}

/* --- createQ1SourceComposition ------------------------------------------------------------ */
bool qa_comp_create_q1_source_composition(comp_t *comp, qa_q1_host host, const qa_q1_options *options,
                                          const qa_comp_q1_source_selection *selection,
                                          const qa_comp_q1_services *services) {
  qa_q1_services *game = calloc(1, sizeof *game);
  if (game == NULL) return false;
  qa_q1_options resolved = *options;
  resolved.precache_id1 = selection->program == QA_COMP_Q1_ID1;
  resolved.live.ctx = (void *)services;
  resolved.live.skill = live_skill;
  resolved.live.teamplay = live_teamplay;
  resolved.live.gravity = live_gravity;
  if (!qa_q1_services_init(game, host, resolved)) {
    if (game->named != NULL) qa_q1_services_free(game);
    free(game);
    return false;
  }
  qa_comp_q1_source_composition_init(comp, game, selection, services);
  comp->owns_game = true;
  return true;
}
