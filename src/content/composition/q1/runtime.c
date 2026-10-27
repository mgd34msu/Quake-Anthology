/* Port of src/content/composition/q1/runtime.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q1/runtime.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/composition/q1/give.h"
#include "qa/foundation/support.h"

#define H(comp) ((comp)->hooks)
#define HC(comp) ((comp)->hooks->ctx)
#define S(comp) ((comp)->services)
#define SC(comp) ((comp)->services->ctx)

static void emit_developer(qa_comp_q1_source_composition *comp, const char *text) {
  qa_comp_q1_event event = {0};
  event.kind = QA_COMP_Q1_EVENT_DEVELOPER_MESSAGE;
  event.text = text;
  S(comp)->emit(SC(comp), &event);
}
static void developer_text(void *ctx, const char *text) { emit_developer((qa_comp_q1_source_composition *)ctx, text); }
static double random_next(void *ctx) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return H(comp)->random(HC(comp));
}

/* --- binding closures ---------------------------------------------------- */
static double campaign_read(void *ctx) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return comp->selection.campaign.read_flags(comp->selection.campaign.ctx);
}
static void campaign_write(void *ctx, double flags) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  comp->selection.campaign.write_flags(comp->selection.campaign.ctx, flags);
}
static void campaign_skill(void *ctx, double skill) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  comp->selection.campaign.set_skill(comp->selection.campaign.ctx, skill);
  /* TS: services.setCvar("skill", String(skill)) */
  char text[32];
  if (skill == trunc(skill) && fabs(skill) < 9.0e15) (void)snprintf(text, sizeof text, "%lld", (long long)skill);
  else (void)snprintf(text, sizeof text, "%g", skill);
  S(comp)->set_cvar(SC(comp), "skill", text);
}
static bool base_same_level(void *ctx) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return S(comp)->cvar(SC(comp), "samelevel") != 0;
}
static void notice(qa_comp_q1_source_composition *comp, qa_actor_id actor, const char *event);
static void base_player_exited(void *ctx, qa_actor_id actor) { notice((qa_comp_q1_source_composition *)ctx, actor, "exit"); }
static void base_finish_campaign(void *ctx) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  S(comp)->finish_campaign(SC(comp));
}
static bool base_finale_finished(void *ctx) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  size_t count = comp->clients.record_count;
  qa_actor_id *actors = (qa_actor_id *)malloc((count > 0 ? count : 1) * sizeof *actors);
  bool *held = (bool *)malloc((count > 0 ? count : 1) * sizeof *held);
  if (actors == NULL || held == NULL) { free(actors); free(held); return false; }
  for (size_t i = 0; i < count; i++) {
    actors[i] = comp->clients.records[i].actor.id;
    qa_comp_q1_player_state *player = comp->game->player(comp->game->ctx, actors[i]);
    held[i] = player != NULL && player->attack_held;
  }
  bool result = H(comp)->finale_poll(HC(comp), H(comp)->time(HC(comp)), actors, held, count);
  free(actors);
  free(held);
  return result;
}

static bool is_monster(qa_comp_q1_source_composition *comp, qa_actor_id actor) {
  qa_comp_q1_actor *entity = H(comp)->entity(HC(comp), actor);
  return entity != NULL && ((int64_t)H(comp)->entity_movement_flags(HC(comp), entity) & 32) != 0;
}
static bool addon_is_monster(void *ctx, qa_actor_id actor) { return is_monster((qa_comp_q1_source_composition *)ctx, actor); }
static void addon_emit(void *ctx, const qa_comp_q1_addon_event *addon_event) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  qa_comp_q1_event event = {0};
  event.kind = QA_COMP_Q1_EVENT_ADDON;
  event.addon_event = addon_event;
  S(comp)->emit(SC(comp), &event);
}
static bool addon_cheat(void *ctx, qa_actor_id actor, const char *category) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return S(comp)->cheat_arsenal != NULL && S(comp)->cheat_arsenal(SC(comp), actor, category);
}
static double addon_cvar(void *ctx, const char *name) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return S(comp)->cvar(SC(comp), name);
}
static void addon_set_cvar(void *ctx, const char *name, const char *value) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  S(comp)->set_cvar(SC(comp), name, value);
}

static int horde_dead_flag(void *ctx, qa_actor_id actor) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return S(comp)->selected_player(SC(comp), actor).dead_flag;
}
static bool horde_no_target(void *ctx, qa_actor_id actor) {
  return qa_comp_q1_source_composition_no_target((qa_comp_q1_source_composition *)ctx, actor);
}
static bool horde_is_bot(void *ctx, qa_actor_id actor) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return S(comp)->selected_player(SC(comp), actor).is_bot;
}
static void horde_add_score(void *ctx, qa_actor_id actor, double delta) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  qa_comp_q1_source_clients_add_score(&comp->clients, actor, delta);
}
static void respawn_at(qa_comp_q1_source_composition *comp, qa_actor_id actor, qa_comp_q1_actor *spot,
                       qa_comp_q1_travel *travel);
static void horde_respawn_teammate(void *ctx, qa_actor_id actor) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  respawn_at(comp, actor, NULL, qa_comp_q1_source_composition_new_travel(comp));
}
static void horde_restart(void *ctx, const char *map, double flags) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  S(comp)->restart_session(SC(comp), map, flags);
}

static double pack_gamecfg(void *ctx) { return addon_cvar(ctx, "gamecfg"); }
static double pack_team_color(void *ctx, qa_actor_id actor) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return qa_comp_q1_source_clients_team_color(&comp->clients, actor);
}
static void pack_set_team_color(void *ctx, qa_actor_id actor, double color) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  (void)qa_comp_q1_source_clients_colors(&comp->clients, actor, color - 1, color - 1);
}
static double pack_frags(void *ctx, qa_actor_id actor) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return qa_comp_q1_source_clients_require(&comp->clients, actor)->frags;
}
static void pack_disconnect(void *ctx, qa_actor_id actor) {
  qa_comp_q1_source_composition_disconnect((qa_comp_q1_source_composition *)ctx, actor);
}
static double pack_player_frame(void *ctx, qa_actor_id actor) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return S(comp)->selected_player(SC(comp), actor).frame;
}
static const char *pack_player_name(void *ctx, qa_actor_id actor) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return qa_comp_q1_source_client_name(qa_comp_q1_source_clients_require(&comp->clients, actor));
}
static bool pack_cheats_allowed(void *ctx) { return addon_cvar(ctx, "sv_cheats") != 0; }
static bool pack_footsteps(void *ctx) { return addon_cvar(ctx, "footsteps") == 1; }

static const char *ctf_name(void *ctx, qa_actor_id actor) { return pack_player_name(ctx, actor); }
static double ctf_captures(void *ctx, const char *team) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return strcmp(team, "red") == 0 ? comp->clients.red_captures : comp->clients.blue_captures;
}
static void ctf_add_capture(void *ctx, const char *team) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  double total = strcmp(team, "red") == 0 ? ++comp->clients.red_captures : ++comp->clients.blue_captures;
  qa_comp_q1_event event = {0};
  event.kind = QA_COMP_Q1_EVENT_CTF_CAPTURE;
  event.team = team;
  event.total = total;
  S(comp)->emit(SC(comp), &event);
}
static void ctf_input(void *ctx, qa_actor_id actor, bool *attack, bool *jump, double *impulse,
                      bool *grapple_selected, qa_vec3 *view_angles, double *teleport_until, double *frame) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&comp->clients, actor);
  qa_comp_q1_player_state *player = comp->game->player(comp->game->ctx, actor);
  qa_comp_q1_selected_player selected = S(comp)->selected_player(SC(comp), actor);
  qa_item_id weapon = S(comp)->selected_weapon(SC(comp), actor);
  *attack = player != NULL && player->attack_held;
  *jump = player != NULL && player->jump_held;
  *impulse = client->impulse;
  *grapple_selected = weapon != NULL && strcmp(weapon, "q1:ctf/weapon/grapple") == 0;
  *view_angles = selected.view_angles;
  *teleport_until = selected.teleport_until;
  *frame = selected.frame;
}
static void ctf_consume_impulse(void *ctx, qa_actor_id actor) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  qa_comp_q1_source_clients_require(&comp->clients, actor)->impulse = 0;
}
static bool ctf_observer(void *ctx, qa_actor_id actor) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return qa_comp_q1_source_clients_require(&comp->clients, actor)->observer;
}
static void ctf_set_observer(void *ctx, qa_actor_id actor, bool enabled) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  qa_comp_q1_source_clients_set_observer(&comp->clients, actor, enabled);
}
static void ctf_respawn(void *ctx, qa_actor_id actor, qa_comp_q1_actor *spot) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  respawn_at(comp, actor, spot, qa_comp_q1_source_composition_new_travel(comp));
}
static void ctf_colors(void *ctx, qa_actor_id actor, double shirt, double pants) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  (void)qa_comp_q1_source_clients_colors(&comp->clients, actor, shirt, pants);
}
static bool ctf_prompt_supported(void *ctx, qa_actor_id actor) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return S(comp)->prompt_supported(SC(comp), actor);
}
static void ctf_prompt(void *ctx, qa_actor_id actor, const char *title,
                       const qa_comp_q1_prompt_choice *choices, size_t choice_count) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  qa_comp_q1_event event = {0};
  event.kind = QA_COMP_Q1_EVENT_PROMPT;
  event.actor = actor;
  event.title = title;
  event.choices = choices;
  event.choice_count = choice_count;
  S(comp)->emit(SC(comp), &event);
}
static void ctf_clear_prompt(void *ctx, qa_actor_id actor) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  qa_comp_q1_event event = {0};
  event.kind = QA_COMP_Q1_EVENT_CLEAR_PROMPT;
  event.actor = actor;
  S(comp)->emit(SC(comp), &event);
}
static void ctf_teleport(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles, qa_vec3 velocity, double until) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  S(comp)->teleport(SC(comp), actor, origin, angles, velocity, until);
}
static void ctf_select_grapple(void *ctx, qa_actor_id actor) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  S(comp)->select_weapon(SC(comp), actor, "q1:ctf/weapon/grapple");
}
static qa_item_id ctf_selected_weapon(void *ctx, qa_actor_id actor) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return S(comp)->selected_weapon(SC(comp), actor);
}
static qa_item_id ctf_selected_ammo(void *ctx, qa_actor_id actor) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return S(comp)->selected_ammo(SC(comp), actor);
}
static void ctf_weapon_changed(void *ctx, qa_actor_id actor, qa_item_id acquired) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  S(comp)->weapon_changed(SC(comp), actor, acquired);
}
static void ctf_haste(void *ctx, qa_actor_id actor) { ctf_weapon_changed(ctx, actor, NULL); }
static void ctf_status(void *ctx, qa_actor_id actor, const qa_comp_ctf_status *status) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  qa_comp_q1_event event = {0};
  event.kind = QA_COMP_Q1_EVENT_CTF_STATUS;
  event.actor = actor;
  event.status = status;
  S(comp)->emit(SC(comp), &event);
}
static void ctf_log(void *ctx, qa_actor_id actor, const char *action) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  qa_comp_q1_event event = {0};
  event.kind = QA_COMP_Q1_EVENT_SOURCE_LOG;
  event.actor = actor;
  event.action = action;
  S(comp)->emit(SC(comp), &event);
}

static uint8_t *clients_capture(void *ctx, size_t *out_length) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return qa_comp_q1_source_clients_capture(&comp->clients, out_length);
}
static bool clients_restore(void *ctx, const uint8_t *bytes, size_t length) {
  qa_comp_q1_source_composition *comp = (qa_comp_q1_source_composition *)ctx;
  return qa_comp_q1_source_clients_restore(&comp->clients, bytes, length);
}

/* --- constructor --------------------------------------------------------- */
bool qa_comp_q1_source_composition_init(qa_comp_q1_source_composition *comp, qa_comp_q1_game *game,
                                        const qa_comp_q1_source_selection *selection,
                                        const qa_comp_q1_services *services,
                                        const qa_comp_q1_runtime_hooks *hooks) {
  memset(comp, 0, sizeof *comp);
  comp->game = game;
  comp->selection = *selection;
  comp->services = services;
  comp->hooks = hooks;
  qa_comp_q1_program program = selection->program;
  comp->base_campaign = (qa_comp_q1_campaign_binding){comp, campaign_read, campaign_write, campaign_skill};
  comp->base_bindings = (qa_comp_q1_base_bindings){comp, comp->base_campaign, selection->registered,
                                                   selection->official_campaign, base_same_level,
                                                   base_player_exited, base_finish_campaign, base_finale_finished};
  comp->addon_bindings = (qa_comp_q1_addon_bindings){comp, addon_emit, addon_cheat, addon_is_monster,
                                                     addon_cvar, addon_set_cvar};
  comp->horde_bindings = (qa_comp_q1_horde_bindings){comp, horde_dead_flag, horde_no_target, horde_is_bot,
                                                     horde_add_score, horde_respawn_teammate, horde_restart};
  comp->pack_bindings = (qa_comp_q1_pack_bindings){comp, addon_cheat, pack_gamecfg, pack_team_color,
                                                   pack_set_team_color, horde_add_score, pack_frags, pack_disconnect,
                                                   pack_player_frame, pack_player_name, pack_cheats_allowed,
                                                   developer_text, pack_footsteps};
  comp->ctf_bindings = (qa_comp_q1_ctf_bindings){comp, ctf_name, pack_frags, horde_add_score, horde_is_bot,
                                                 ctf_captures, ctf_add_capture, ctf_input, ctf_consume_impulse,
                                                 ctf_observer, ctf_set_observer, ctf_respawn, pack_disconnect,
                                                 ctf_colors, ctf_prompt_supported, ctf_prompt, ctf_clear_prompt,
                                                 ctf_teleport, ctf_select_grapple, ctf_selected_weapon,
                                                 ctf_selected_ammo, ctf_weapon_changed, ctf_haste, ctf_status, ctf_log};
  qa_comp_q1_source_clients_init(&comp->clients, hooks->client_game, program, services);
  if (strcmp(game->edition, "rerelease") == 0 &&
      (program == QA_COMP_Q1_ID1 || program == QA_COMP_Q1_HIPNOTIC || program == QA_COMP_Q1_ROGUE))
    hooks->register_coop_bots_effect(hooks->ctx);
  if (program == QA_COMP_Q1_ROGUE) hooks->set_base_team_health(hooks->ctx, false);
  comp->base = hooks->register_base(hooks->ctx, &comp->base_bindings);
  if (program == QA_COMP_Q1_HIPNOTIC || program == QA_COMP_Q1_ROGUE) {
    comp->packs = hooks->register_mission_pack(hooks->ctx, comp->base, program, &comp->pack_bindings);
  } else if (program == QA_COMP_Q1_CTF) {
    comp->addon = hooks->new_addon_context(hooks->ctx, comp->base, "ctf", &comp->addon_bindings);
    comp->ctf = hooks->register_ctf(hooks->ctx, comp->addon, &comp->ctf_bindings, services->shared_grapple);
  } else if (program == QA_COMP_Q1_DOPA || program == QA_COMP_Q1_MG1 || program == QA_COMP_Q1_MG3) {
    comp->addon = hooks->register_campaign_addons(hooks->ctx, comp->base, qa_comp_q1_program_name(program),
                                                  &comp->addon_bindings);
    if (program == QA_COMP_Q1_MG1 || program == QA_COMP_Q1_DOPA)
      comp->horde = hooks->register_horde(hooks->ctx, comp->addon, &comp->horde_bindings);
  }
  if (comp->horde == NULL && selection->has_horde && selection->horde) {
    /* Horde is a match mode on every Q1 program (see TS comment). */
    comp->horde_addon = program == QA_COMP_Q1_MG3
                            ? hooks->new_addon_context(hooks->ctx, comp->base, "mg1", &comp->addon_bindings)
                            : hooks->register_campaign_addons(hooks->ctx, comp->base, "mg1", &comp->addon_bindings);
    comp->horde = hooks->register_horde(hooks->ctx, comp->horde_addon, &comp->horde_bindings);
  }
  if (comp->addon != NULL && strcmp(hooks->addon_program(hooks->ctx, comp->addon), "mg3") == 0)
    hooks->register_mg3_buddha_effect(hooks->ctx);
  hooks->register_state_extension(hooks->ctx, "q1:source-clients", clients_capture, clients_restore, comp);
  return true;
}

void qa_comp_q1_source_composition_destroy(qa_comp_q1_source_composition *comp) {
  qa_comp_q1_source_clients_destroy(&comp->clients);
}

qa_comp_q1_effect_result qa_comp_q1_coop_bots_before_quad(qa_comp_q1_source_composition *comp,
                                                          const qa_damage_request *request, double amount) {
  const qa_actor_id *attacker = request->attack.attacker;
  if (comp->game->coop && attacker != NULL && !qa_same_actor(request->target, *attacker) &&
      qa_comp_q1_source_clients_get(&comp->clients, request->target) != NULL &&
      qa_comp_q1_source_clients_get(&comp->clients, *attacker) != NULL &&
      S(comp)->selected_player(SC(comp), *attacker).is_bot &&
      !S(comp)->selected_player(SC(comp), request->target).is_bot)
    return (qa_comp_q1_effect_result){true, amount};
  return (qa_comp_q1_effect_result){false, amount};
}

qa_comp_q1_lethal_result qa_comp_q1_mg3_buddha_lethal_health(qa_comp_q1_source_composition *comp,
                                                             const qa_damage_request *request, double health) {
  if (H(comp)->addon_player_number(HC(comp), comp->addon, request->target, "buddha") != 0)
    return (qa_comp_q1_lethal_result){1, "none"};
  return (qa_comp_q1_lethal_result){health, "death"};
}

/* map.source basename minus ".bsp" (TS /^.*[/\\]/ then /\.bsp$/). */
static void map_basename(const char *source, char *out, size_t cap) {
  const char *base = source;
  for (const char *cursor = source; *cursor != '\0'; cursor++)
    if (*cursor == '/' || *cursor == '\\') base = cursor + 1;
  size_t length = strlen(base);
  if (length >= 4 && strcmp(base + length - 4, ".bsp") == 0) length -= 4;
  if (length >= cap) length = cap - 1;
  memcpy(out, base, length);
  out[length] = '\0';
}

void qa_comp_q1_source_composition_spawn_map(qa_comp_q1_source_composition *comp, qa_comp_q1_map *map) {
  char name[256];
  map_basename(H(comp)->map_source(HC(comp), map), name, sizeof name);
  S(comp)->set_cvar(SC(comp), "sv_gravity", strcmp(name, "e1m8") == 0 ? "100" : "800");
  H(comp)->spawn_map(HC(comp), map);
}

void qa_comp_q1_source_composition_spawn_entities(qa_comp_q1_source_composition *comp,
                                                  const void *entity_list, const void *model_bounds,
                                                  const char *map_name, const int64_t *keep,
                                                  size_t keep_count, bool has_keep) {
  S(comp)->set_cvar(SC(comp), "sv_gravity", strcmp(map_name, "e1m8") == 0 ? "100" : "800");
  H(comp)->spawn_entities(HC(comp), entity_list, model_bounds, map_name, keep, keep_count, has_keep);
}

static void present_intermission(qa_comp_q1_source_composition *comp, qa_comp_q1_intermission result) {
  if (strcmp(result.kind, "finale") == 0) {
    H(comp)->base_reset_finale(HC(comp), comp->base);
    H(comp)->finale_reset(HC(comp));
  }
  if (strcmp(result.kind, "finale") == 0 || strcmp(result.kind, "sell-screen") == 0) {
    qa_comp_q1_event event = {0};
    event.kind = QA_COMP_Q1_EVENT_LEVEL_PRESENTATION;
    event.finale = result.finale;
    S(comp)->emit(SC(comp), &event);
  }
}

bool qa_comp_q1_source_composition_attach(qa_comp_q1_source_composition *comp, qa_owned_actor actor,
                                          const qa_comp_q1_client_admission *admission) {
  if (qa_comp_q1_source_clients_attach(&comp->clients, actor, admission) == NULL) return false;
  notice(comp, actor.id, "connect");
  qa_comp_q1_intermission result =
      H(comp)->level_client_connected(HC(comp), comp->base, H(comp)->time(HC(comp)),
                                      S(comp)->cvar(SC(comp), "samelevel") != 0);
  present_intermission(comp, result);
  return true;
}

void qa_comp_q1_source_composition_spawned(qa_comp_q1_source_composition *comp, qa_actor_id actor,
                                           bool first_admission) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&comp->clients, actor);
  client->death_recorded = false;
  client->impulse = 0;
  client->use = false;
  client->respawn_requested_at = -1;
  if (!first_admission) {
    H(comp)->level_reset_player(HC(comp), comp->base, client->actor);
    if (comp->horde != NULL) H(comp)->horde_restore_keys(HC(comp), comp->horde, actor);
  }
  qa_comp_q1_source_clients_spawned(&comp->clients, actor);
  if (comp->packs != NULL) H(comp)->packs_player_spawned(HC(comp), comp->packs, actor);
  if (comp->ctf != NULL) H(comp)->ctf_spawn_player(HC(comp), comp->ctf, actor, first_admission);
}

bool qa_comp_q1_source_composition_userinfo(qa_comp_q1_source_composition *comp, qa_actor_id actor,
                                            const qa_comp_userinfo_entry *values, size_t count) {
  return qa_comp_q1_source_clients_update(&comp->clients, actor, values, count);
}

bool qa_comp_q1_source_composition_no_target(qa_comp_q1_source_composition *comp, qa_actor_id actor) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_get(&comp->clients, actor);
  return client != NULL && client->no_target;
}

void qa_comp_q1_source_composition_set_no_target(qa_comp_q1_source_composition *comp,
                                                 qa_actor_id actor, bool enabled) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&comp->clients, actor);
  client->no_target = enabled;
  qa_comp_q1_source_clients_publish(&comp->clients, client);
}

void qa_comp_q1_source_composition_input(qa_comp_q1_source_composition *comp, qa_actor_id actor,
                                         qa_comp_q1_source_input input) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&comp->clients, actor);
  qa_comp_q1_selected_player selected = S(comp)->selected_player(SC(comp), actor);
  if (input.impulse != 0) client->impulse = input.impulse;
  client->use = input.use;
  H(comp)->player_input(HC(comp), client->actor, input.attack, input.jump, selected.teleport_until);
}

void qa_comp_q1_source_composition_pre_frame(qa_comp_q1_source_composition *comp, double elapsed_seconds) {
  if (comp->addon != NULL) H(comp)->addon_frame(HC(comp), comp->addon, elapsed_seconds);
  if (comp->horde_addon != NULL && comp->horde_addon != comp->addon)
    H(comp)->addon_frame(HC(comp), comp->horde_addon, elapsed_seconds);
  if (comp->game->deathmatch != 0) {
    size_t count = comp->clients.record_count;
    double *frags = (double *)malloc((count > 0 ? count : 1) * sizeof *frags);
    if (frags != NULL) {
      for (size_t i = 0; i < count; i++) frags[i] = comp->clients.records[i].frags;
      H(comp)->level_check_limits(HC(comp), comp->base, H(comp)->time(HC(comp)), frags, count,
                                  S(comp)->cvar(SC(comp), "timelimit"), S(comp)->cvar(SC(comp), "fraglimit"));
      free(frags);
    }
  }
}

void qa_comp_q1_source_composition_player_pre_think(qa_comp_q1_source_composition *comp, qa_actor_id actor) {
  if (comp->ctf != NULL) H(comp)->ctf_player_frame(HC(comp), comp->ctf, actor);
}

void qa_comp_q1_source_composition_player_post_think(qa_comp_q1_source_composition *comp, qa_actor_id actor) {
  if (comp->ctf != NULL) H(comp)->ctf_after_physics(HC(comp), comp->ctf, actor);
  if (comp->addon != NULL)
    H(comp)->frame_addon_player(HC(comp), comp->addon, actor, S(comp)->selected_player(SC(comp), actor).view_offset);
}

bool qa_comp_q1_source_composition_impulse(qa_comp_q1_source_composition *comp, qa_actor_id actor) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&comp->clients, actor);
  qa_comp_q1_player_state *player = comp->game->player(comp->game->ctx, actor);
  /* TS weapons = services.weaponServices?.(actor) ?? game (mirrored to the
   * same game table; weaponPlayer is that table's player). */
  qa_comp_q1_player_state *weapon_player = player;
  if (S(comp)->weapon_services != NULL) (void)S(comp)->weapon_services(SC(comp), actor);
  if (client->impulse == 0 || (weapon_player != NULL && H(comp)->time(HC(comp)) < weapon_player->attack_finished))
    return false;
  if (comp->ctf != NULL && H(comp)->ctf_impulse(HC(comp), comp->ctf, actor)) return true;
  if (comp->packs != NULL && H(comp)->packs_impulse(HC(comp), comp->packs, actor, client->impulse)) {
    client->impulse = 0;
    return true;
  }
  if (comp->addon != NULL &&
      H(comp)->mg3_item_impulse(HC(comp), comp->addon, actor, client->impulse, developer_text, comp)) {
    client->impulse = 0;
    return true;
  }
  if (comp->addon != NULL && H(comp)->addon_impulse(HC(comp), comp->addon, actor, client->impulse)) {
    client->impulse = 0;
    return true;
  }
  if (weapon_player != NULL && qa_comp_q1_weapon_impulse(comp->game, weapon_player, client->impulse)) {
    client->impulse = 0;
    return true;
  }
  if (player == NULL) return false;
  qa_comp_q1_impulse_source source = {comp->game, comp->services, comp->selection.program, comp->base_campaign};
  if (qa_comp_base_q1_impulse(&source, player, client->impulse)) {
    client->impulse = 0;
    return true;
  }
  client->impulse = 0;
  return true;
}

void qa_comp_q1_source_composition_fired(qa_comp_q1_source_composition *comp, qa_actor_id actor,
                                         qa_item_id weapon) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_get(&comp->clients, actor);
  if (client == NULL) return;
  H(comp)->level_note_attack(HC(comp), comp->base, client->actor,
                             weapon != NULL && strcmp(weapon, "q1:weapon/axe") == 0);
}

static qa_comp_q1_obituary_actor obituary_actor(qa_comp_q1_source_composition *comp, qa_actor_id actor) {
  qa_comp_q1_actor *entity = H(comp)->entity(HC(comp), actor);
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_get(&comp->clients, actor);
  qa_comp_q1_player_state *player = comp->game->player(comp->game->ctx, actor);
  bool has_selected = client != NULL;
  qa_comp_q1_selected_player selected = {0};
  if (has_selected) selected = S(comp)->selected_player(SC(comp), actor);
  double time = H(comp)->time(HC(comp));
  double quad_until = player != NULL ? H(comp)->powerup_until(HC(comp), actor, "quad") : 0;
  double inv_until = player != NULL ? H(comp)->powerup_until(HC(comp), actor, "invulnerability") : 0;
  qa_comp_q1_obituary_actor result;
  result.actor = actor;
  result.name = client != NULL ? qa_comp_q1_source_client_name(client)
                : entity != NULL ? H(comp)->entity_text(HC(comp), entity, "netname") : "";
  result.classname = client == NULL ? H(comp)->classname(HC(comp), actor) : "player";
  result.is_player = client != NULL;
  result.is_monster = is_monster(comp, actor);
  result.team = client == NULL ? (entity != NULL ? H(comp)->entity_number(HC(comp), entity, "team") : 0) : client->team;
  result.health = H(comp)->health(HC(comp), actor);
  result.water_type = has_selected ? selected.water_type : "empty";
  result.water_level = has_selected ? (double)selected.water_level
                                    : entity != NULL ? H(comp)->entity_water_level(HC(comp), entity) : 0;
  result.weapon = player != NULL ? player->weapon : NULL;
  result.quad_expires = fmax(0.0, quad_until - time);
  result.invulnerable_expires = fmax(0.0, inv_until - time);
  result.brush = entity != NULL && strcmp(H(comp)->entity_solid(HC(comp), entity), "bsp") == 0;
  result.kill_string = entity != NULL ? H(comp)->entity_text(HC(comp), entity, "kill_string") : "";
  return result;
}

static void broadcast(qa_comp_q1_source_composition *comp, const char *text, const char *const *args, size_t count) {
  size_t player_count = 0;
  const qa_actor_id *players = H(comp)->players(HC(comp), &player_count);
  for (size_t i = 0; i < player_count; i++)
    comp->game->message(comp->game->ctx, players[i], text, false, args, count);
}

static void notice(qa_comp_q1_source_composition *comp, qa_actor_id actor, const char *event) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&comp->clients, actor);
  qa_comp_q1_notice value = H(comp)->client_notice(HC(comp), comp->game->edition, event,
                                                   qa_comp_q1_source_client_name(client), client->frags);
  broadcast(comp, value.text, value.args, value.arg_count);
  if (value.score_delta != 0) qa_comp_q1_source_clients_add_score(&comp->clients, actor, value.score_delta);
}

void qa_comp_q1_source_composition_before_reaction(qa_comp_q1_source_composition *comp,
                                                   qa_owned_actor actor, const qa_damage_decision *decision) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_get(&comp->clients, actor.id);
  bool death = strcmp(decision->reaction, "death") == 0;
  if (death && (client != NULL || H(comp)->entity(HC(comp), actor.id) != NULL) &&
      H(comp)->health(HC(comp), actor.id) < -99)
    comp->game->set_health(comp->game->ctx, actor, -99);
  const qa_actor_id *attacker = decision->request.attack.attacker;
  if (client == NULL) {
    qa_comp_q1_actor *entity = H(comp)->entity(HC(comp), actor.id);
    if (!death || entity == NULL) return;
    const char *movement = H(comp)->entity_movement(HC(comp), entity);
    if (strcmp(movement, "none") == 0 || strcmp(movement, "push") == 0) return;
    if (strcmp(comp->game->edition, "rerelease") == 0 && is_monster(comp, actor.id) && attacker != NULL &&
        qa_comp_q1_source_clients_get(&comp->clients, *attacker) != NULL &&
        strcmp(H(comp)->entity_text(HC(comp), entity, "horde.sourceDie"), "") == 0)
      qa_comp_q1_source_clients_add_score(&comp->clients, *attacker, 1);
    /* ClientObituary consumes its source rnum even for non-players. */
    (void)H(comp)->random(HC(comp));
    return;
  }
  H(comp)->level_note_damage(HC(comp), comp->base, actor, decision->applied_damage);
  if (decision->applied_damage > 0 && comp->packs != NULL)
    H(comp)->packs_confirmed_damage(HC(comp), comp->packs, actor.id, attacker);
  if (!death || client->death_recorded) return;
  client->death_recorded = true;
  qa_comp_q1_actor *source_attacker = attacker != NULL ? H(comp)->entity(HC(comp), *attacker) : NULL;
  qa_actor_id telefrag_owner;
  bool has_telefrag = source_attacker != NULL && H(comp)->entity_owner(HC(comp), source_attacker, &telefrag_owner);
  const qa_attack_cause *cause = &decision->request.attack.cause;
  const char *death_type = cause->kind == QA_ATTACK_CAUSE_Q1 ? cause->death_type
                           : cause->kind == QA_ATTACK_CAUSE_ENVIRONMENT && cause->hazard != NULL &&
                                     strcmp(cause->hazard, "fall") == 0
                                 ? "falling"
                                 : "";
  qa_comp_q1_obituary_input input;
  memset(&input, 0, sizeof input);
  input.edition = comp->game->edition;
  input.victim = obituary_actor(comp, actor.id);
  input.has_attacker = attacker != NULL;
  if (attacker != NULL) input.attacker = obituary_actor(comp, *attacker);
  input.has_telefrag_owner = has_telefrag;
  if (has_telefrag) input.telefrag_owner = obituary_actor(comp, telefrag_owner);
  input.teamplay = S(comp)->cvar(SC(comp), "teamplay");
  input.death_type = death_type;
  qa_comp_q1_obituary obituary = comp->packs != NULL
                                     ? H(comp)->packs_obituary(HC(comp), comp->packs, &input,
                                                               decision->request.attack.inflictor, random_next, comp)
                                     : H(comp)->obituary(HC(comp), &input, random_next, comp);
  if (obituary.has_message) broadcast(comp, obituary.message_text, obituary.message_args, obituary.message_arg_count);
  if (comp->ctf != NULL) {
    H(comp)->ctf_death(HC(comp), comp->ctf, actor.id, attacker);
  } else {
    if (obituary.has_score && qa_comp_q1_source_clients_get(&comp->clients, obituary.score_actor) != NULL)
      qa_comp_q1_source_clients_add_score(&comp->clients, obituary.score_actor, obituary.score_delta);
    if (attacker != NULL && qa_comp_q1_source_clients_get(&comp->clients, *attacker) != NULL && comp->horde != NULL)
      H(comp)->horde_teammate_killed(HC(comp), comp->horde, *attacker);
  }
  if (obituary.has_achievement)
    H(comp)->emit_achievement(HC(comp), obituary.achievement_actor, obituary.achievement_id);
  if (comp->packs != NULL) H(comp)->packs_player_died(HC(comp), comp->packs, actor.id, attacker);
}

void qa_comp_q1_source_composition_disconnect(qa_comp_q1_source_composition *comp, qa_actor_id actor) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&comp->clients, actor);
  int64_t slot = client->slot;
  if (comp->ctf != NULL) H(comp)->ctf_disconnect_player(HC(comp), comp->ctf, actor);
  if (comp->packs != NULL) H(comp)->packs_player_died(HC(comp), comp->packs, actor, NULL);
  notice(comp, actor, "disconnect");
  qa_comp_q1_event event = {0};
  event.kind = QA_COMP_Q1_EVENT_CLIENT_LEFT;
  event.actor = actor;
  event.slot = slot;
  S(comp)->emit(SC(comp), &event);
  S(comp)->disconnect(SC(comp), actor);
}

static void respawn_at(qa_comp_q1_source_composition *comp, qa_actor_id actor, qa_comp_q1_actor *source_spot,
                       qa_comp_q1_travel *travel) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&comp->clients, actor);
  double time = H(comp)->time(HC(comp));
  if (client->respawn_requested_at < 0) client->respawn_requested_at = time;
  qa_comp_q1_actor *spot = source_spot != NULL ? source_spot
                                               : qa_comp_q1_source_composition_select_spawn(
                                                     comp, true, actor, time >= client->respawn_requested_at + 5);
  if (spot == NULL) return;
  S(comp)->place_player(SC(comp), client->actor, spot, travel);
  qa_comp_q1_source_composition_spawned(comp, actor, false);
}

void qa_comp_q1_source_composition_request_respawn(qa_comp_q1_source_composition *comp,
                                                   qa_actor_id actor, qa_comp_q1_travel *coop_entry) {
  if (comp->horde != NULL && H(comp)->horde_request_respawn(HC(comp), comp->horde)) return;
  if (!comp->game->coop && comp->game->deathmatch == 0) {
    S(comp)->restart_session(SC(comp), H(comp)->map_name(HC(comp)),
                             comp->selection.campaign.read_flags(comp->selection.campaign.ctx));
    return;
  }
  respawn_at(comp, actor, NULL,
             comp->game->coop && coop_entry != NULL ? coop_entry
                                                    : qa_comp_q1_source_composition_new_travel(comp));
}

qa_comp_q1_actor *qa_comp_q1_source_composition_select_spawn(qa_comp_q1_source_composition *comp,
                                                             bool has_actor, qa_actor_id actor, bool force) {
  if (has_actor && comp->ctf != NULL) return H(comp)->ctf_select_spawn(HC(comp), comp->ctf, actor);
  if (has_actor && comp->packs != NULL) {
    qa_comp_q1_actor *point = NULL;
    if (H(comp)->packs_select_spawn(HC(comp), comp->packs, actor, &point)) return point;
  }
  return H(comp)->spawn_select(HC(comp), comp->base, force);
}

qa_comp_q1_travel *qa_comp_q1_source_composition_new_travel(qa_comp_q1_source_composition *comp) {
  if (comp->ctf != NULL) return H(comp)->new_ctf_travel(HC(comp), comp->ctf);
  if (comp->packs != NULL) return H(comp)->packs_new_travel(HC(comp), comp->packs);
  if (comp->addon != NULL) return H(comp)->new_addon_travel(HC(comp), comp->addon);
  return H(comp)->new_base_travel(HC(comp));
}

static const char *selected_base_weapon(qa_comp_q1_source_composition *comp, qa_actor_id actor) {
  qa_item_id selected = S(comp)->selected_weapon(SC(comp), actor);
  if (selected != NULL)
    for (int i = 0; i < QA_COMP_Q1_WEAPON_COUNT; i++)
      if (strcmp(comp->game->weapon_item(comp->game->ctx, QA_COMP_Q1_WEAPONS[i]), selected) == 0)
        return QA_COMP_Q1_WEAPONS[i];
  return NULL;
}

qa_comp_q1_travel *qa_comp_q1_source_composition_capture_travel(qa_comp_q1_source_composition *comp,
                                                                qa_owned_actor actor) {
  if (comp->ctf != NULL) return H(comp)->capture_ctf_travel(HC(comp), comp->ctf, actor);
  if (comp->packs != NULL) return H(comp)->packs_capture_travel(HC(comp), comp->packs, actor);
  if (comp->addon != NULL) return H(comp)->capture_addon_travel(HC(comp), comp->addon, actor);
  const char *weapon = selected_base_weapon(comp, actor.id);
  if (weapon == NULL) {
    qa_comp_q1_player_state *player = comp->game->player(comp->game->ctx, actor.id);
    weapon = player != NULL ? player->weapon : NULL;
  }
  return H(comp)->capture_base_travel(HC(comp), actor, weapon);
}

qa_comp_q1_travel *qa_comp_q1_source_composition_decode_travel(qa_comp_q1_source_composition *comp,
                                                               qa_comp_q1_travel *state) {
  if (comp->ctf != NULL) return H(comp)->decode_ctf_travel(HC(comp), comp->ctf, state);
  if (comp->packs != NULL)
    return H(comp)->packs_decode_travel(HC(comp), comp->packs, state,
                                        comp->selection.campaign.read_flags(comp->selection.campaign.ctx));
  if (comp->addon != NULL) return H(comp)->decode_addon_travel(HC(comp), comp->addon, state);
  return H(comp)->decode_base_travel(HC(comp), state,
                                     comp->selection.campaign.read_flags(comp->selection.campaign.ctx));
}

void qa_comp_q1_source_composition_admit_travel(qa_comp_q1_source_composition *comp,
                                                qa_owned_actor actor, qa_comp_q1_travel *state) {
  if (comp->packs != NULL) {
    H(comp)->packs_admit_travel(HC(comp), comp->packs, actor,
                                H(comp)->packs_decode_travel(
                                    HC(comp), comp->packs, state,
                                    comp->selection.campaign.read_flags(comp->selection.campaign.ctx)));
    return;
  }
  if (comp->addon != NULL && comp->ctf == NULL) {
    H(comp)->admit_addon_travel(HC(comp), comp->addon, actor, state);
    return;
  }
  H(comp)->admit_base_travel(HC(comp), actor, qa_comp_q1_source_composition_decode_travel(comp, state));
}

qa_comp_q1_actor *qa_comp_q1_source_composition_drop_inventory(qa_comp_q1_source_composition *comp,
                                                               qa_owned_actor actor) {
  if (!comp->game->coop && comp->game->deathmatch == 0) return NULL;
  if (comp->packs != NULL) return H(comp)->packs_drop_backpack(HC(comp), comp->packs, actor);
  qa_vec3 origin;
  if (!H(comp)->body_origin(HC(comp), actor.id, &origin)) qa_range_error("Q1 death drop has no shared body");
  const char *weapon = selected_base_weapon(comp, actor.id);
  if (weapon == NULL) {
    qa_comp_q1_player_state *player = comp->game->player(comp->game->ctx, actor.id);
    weapon = player != NULL ? player->weapon : NULL;
  }
  void *ctx = comp->game->ctx;
  return H(comp)->drop_backpack(HC(comp), origin, weapon, comp->game->inventory_count(ctx, actor.id, "q1:ammo/shells"),
                                comp->game->inventory_count(ctx, actor.id, "q1:ammo/nails"),
                                comp->game->inventory_count(ctx, actor.id, "q1:ammo/rockets"),
                                comp->game->inventory_count(ctx, actor.id, "q1:ammo/cells"));
}

qa_comp_q1_intermission qa_comp_q1_source_composition_request_intermission_exit(
    qa_comp_q1_source_composition *comp, bool pressed, bool has_input, qa_actor_id input_actor, bool input_attack) {
  if (has_input) {
    qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&comp->clients, input_actor);
    qa_comp_q1_player_state *player = comp->game->player(comp->game->ctx, client->actor.id);
    if (player != NULL) player->attack_held = input_attack;
  }
  qa_comp_q1_intermission result = H(comp)->level_request_exit(
      HC(comp), comp->base, H(comp)->time(HC(comp)), pressed, S(comp)->cvar(SC(comp), "samelevel") != 0);
  present_intermission(comp, result);
  return result;
}

void qa_comp_q1_source_composition_dismiss_finale(qa_comp_q1_source_composition *comp) {
  H(comp)->finale_dismiss(HC(comp), H(comp)->time(HC(comp)));
  H(comp)->base_dismiss_finale(HC(comp), comp->base);
}

qa_comp_q1_pose qa_comp_q1_source_composition_character_pose(qa_comp_q1_source_composition *comp, qa_actor_id actor) {
  if (comp->ctf != NULL) return H(comp)->ctf_character_pose(HC(comp), comp->ctf, actor);
  qa_comp_q1_player_state *player = comp->game->player(comp->game->ctx, actor);
  if (comp->addon != NULL && strcmp(H(comp)->addon_program(HC(comp), comp->addon), "mg3") == 0 && player != NULL)
    return (qa_comp_q1_pose){true, H(comp)->mg3_hammer_body_frame(HC(comp), comp->addon, player)};
  if (comp->packs != NULL) return H(comp)->packs_character_pose(HC(comp), comp->packs, actor);
  return (qa_comp_q1_pose){false, 0};
}

bool qa_comp_q1_source_composition_fall_damage_allowed(qa_comp_q1_source_composition *comp, qa_actor_id actor) {
  return comp->ctf != NULL ? H(comp)->ctf_fall_damage_allowed(HC(comp), comp->ctf, actor) : true;
}

void qa_comp_q1_source_composition_character_frame(qa_comp_q1_source_composition *comp, qa_actor_id actor,
                                                   const void *presentation) {
  if (comp->packs != NULL) H(comp)->packs_character_frame(HC(comp), comp->packs, actor, presentation);
}

void qa_comp_q1_source_composition_suicide(qa_comp_q1_source_composition *comp, qa_actor_id actor) {
  if (comp->ctf != NULL) { H(comp)->ctf_suicide(HC(comp), comp->ctf, actor); return; }
  notice(comp, actor, "suicide");
  qa_comp_q1_source_composition_request_respawn(comp, actor, NULL);
}

int qa_comp_q1_skill_from_cvar(double value) { return value >= 3 ? 3 : value >= 2 ? 2 : value >= 1 ? 1 : 0; }

bool qa_comp_create_q1_source_composition(
    qa_comp_q1_source_composition *composition,
    qa_comp_q1_game *(*create_game)(void *ctx, const qa_comp_q1_foundation_options *options,
                                    const qa_comp_q1_services *services),
    void *create_ctx, const qa_comp_q1_foundation_options *options,
    const qa_comp_q1_source_selection *selection, const qa_comp_q1_services *services,
    const qa_comp_q1_runtime_hooks *hooks, qa_comp_q1_game **out_game) {
  qa_comp_q1_foundation_options resolved = *options;
  resolved.precache_program = selection->program == QA_COMP_Q1_ID1 ? "id1" : NULL;
  qa_comp_q1_game *game = create_game(create_ctx, &resolved, services);
  *out_game = game;
  return qa_comp_q1_source_composition_init(composition, game, selection, services, hooks);
}
