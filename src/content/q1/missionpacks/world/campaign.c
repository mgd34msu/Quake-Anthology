/* Port of src/content/q1/missionpacks/world/campaign.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/campaign.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/content/q1/missionpacks/world/finale_text.h"
#include "qa/content/q1/missionpacks/world/index.h"

static void emit_server_command(qa_q1_services *game, const char *text) {
  qa_q1_event event = {0};
  event.kind = "server-command"; event.text = text;
  qa_q1_emit(game, event);
}
static void emit_achievement(qa_q1_services *game, const char *id) {
  qa_q1_event event = {0};
  event.kind = "achievement"; event.id = id;
  qa_q1_emit(game, event);
}
/* TS base.options.officialCampaign !== false / registered !== false. */
static bool official_campaign(qa_q1_base *base) {
  const qa_q1_base_options *options = qa_q1_base_options_of(base);
  return !(options->has_official_campaign && !options->official_campaign);
}
static bool registered(qa_q1_base *base) {
  const qa_q1_base_options *options = qa_q1_base_options_of(base);
  return !(options->has_registered && !options->registered);
}
void qa_q1mp_start_finale_timer(qa_q1_services *game) {
  if (qa_q1mp_streq(game->options.edition, "classic")) return;
  qa_q1mp_world_later(game, qa_q1_create(game, "mission_finale_timer", NULL, NULL), 1, "mission:finale_check");
}
static void finale_transition(void *ctx, qa_q1_services *g, qa_q1_actor *e) {
  (void)ctx;
  if (g->options.coop) qa_q1_travel(g, "start", NULL);
  else { emit_server_command(g, "menu_credits\n"); emit_server_command(g, "disconnect\n"); }
  qa_q1_remove(g, e);
}
static void finale_check(void *ctx, qa_q1_services *g, qa_q1_actor *e) {
  qa_q1_base *base = ctx;
  const bool finished = qa_q1_base_has_finished_finale(base);
  qa_q1mp_world_later(g, e, finished ? 5 : 0.1, finished ? "mission:finale_transition" : "mission:finale_check");
}
static int finale(qa_q1_services *game, const char *key, double track, qa_q1_intermission_result *out) {
  memset(out, 0, sizeof *out);
  out->kind = "finale";
  (void)snprintf(out->text, sizeof out->text, "%s", qa_q1mp_mission_finale_text(game->options.edition, key));
  out->track = track;
  return 1;
}
static int rogue_finale(void *ctx, double stage, const char *next_map, qa_q1_intermission_result *out) {
  (void)next_map;
  qa_q1_services *game = ctx;
  qa_q1_base *base = qa_q1_base_for(game);
  const char *map = game->map_name;
  if (stage == 2 && qa_q1mp_streq(map, "r1m7")) return finale(game, "$qc_finale_r1", 3, out);
  if (stage == 2 && qa_q1mp_streq(map, "r2m8") && game->options.coop && qa_q1mp_streq(game->options.edition, "rerelease")) {
    emit_server_command(game, "menu_credits\ndisconnect\n");
    qa_q1_level_rules_defer_exit(qa_q1_base_level_rules(base), game->time + 10000000);
    memset(out, 0, sizeof *out);
    out->kind = "finale"; out->track = 3;
    return 1;
  }
  return 0;
}
static int hipnotic_finale(void *ctx, double stage, const char *next_map, qa_q1_intermission_result *out) {
  (void)next_map;
  qa_q1_services *game = ctx;
  qa_q1_base *base = qa_q1_base_for(game);
  const char *map = game->map_name;
  if (stage == 2) {
    if (qa_q1mp_streq(map, "hip1m4")) return finale(game, "$qc_finale_hip1", 6, out);
    if (qa_q1mp_streq(map, "hip2m5")) return finale(game, "$qc_finale_hip2", 6, out);
    if (qa_q1mp_streq(map, "hipend")) {
      if (qa_q1mp_streq(game->options.edition, "rerelease") && official_campaign(base)) {
        emit_achievement(game, "ACH_COMPLETE_HIPEND");
        if (qa_q1_option_skill(game) == 3) emit_achievement(game, "ACH_COMPLETE_HIPEND_NIGHTMARE");
      }
      return finale(game, "$qc_finale_hipend", 2, out);
    }
  }
  if (stage == 3 && registered(base)) {
    const qa_q1_campaign_binding campaign = qa_q1_base_campaign(base);
    if ((qa_js_to_int32(campaign.read_flags(campaign.ctx)) & 15) != 15) {
      if (qa_q1mp_streq(map, "hip1m4")) return finale(game, "$qc_finale_hip1m4", 6, out);
      if (qa_q1mp_streq(map, "hip2m5")) return finale(game, "$qc_finale_hip2m5", 6, out);
      if (qa_q1mp_streq(map, "hipend")) {
        qa_q1_level_rules_defer_exit(qa_q1_base_level_rules(base), game->time + 10000000);
        qa_q1mp_start_finale_timer(game);
        return finale(game, "$qc_finale_hipend2", 2, out);
      }
    }
  }
  return 0;
}
static void end_text(const qa_q1mp_world_hooks *hooks, qa_q1_services *g) {
  qa_q1_level_rules *rules = qa_q1_base_level_rules(qa_q1_base_for(g));
  if (!g->has_intermission) qa_q1_level_rules_begin_cutscene(rules, g->map_name, NULL, g->time);
  const qa_q1_intermission_result result = qa_q1_level_rules_advance_finale(rules, g->time);
  if (qa_q1mp_streq(result.kind, "finale") || qa_q1mp_streq(result.kind, "sell-screen")) {
    if (hooks == NULL || hooks->present_finale == NULL) qa_range_error("Hipnotic end text requires the shared finale journal");
    hooks->present_finale(hooks->ctx, &result);
  }
}
static void end_text_action(void *ctx, qa_q1_services *g, qa_q1_actor *e) { (void)e; end_text(ctx, g); }
static void end_text_use(void *ctx, qa_q1_services *g, qa_q1_actor *e, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)e; (void)other; (void)activator; end_text(ctx, g);
}
static void effect_finale_use(void *ctx, qa_q1_services *g, qa_q1_actor *e, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  const qa_q1mp_world_hooks *hooks = ctx;
  if (qa_q1_actor_number(e, "finale_state") == 1) return;
  qa_q1mp_world_number(e, "finale_state", 1);
  qa_q1_actor *point = qa_q1mp_find_first(g, e->target);
  if (point == NULL) qa_range_error("no target in finale");
  qa_q1_event event = {0};
  event.kind = "finale"; event.text = ""; event.stage = 1;
  qa_q1_emit(g, event);
  if ((qa_js_to_int32(e->spawnflags) & 2) == 0) {
    qa_q1_actor *target = qa_q1mp_find_first(g, qa_q1_actor_text(e, "mdl"));
    if (target == NULL) qa_range_error("Hipnotic finale decoy path is missing");
    size_t count = 0;
    const qa_actor_id *players = g->host.players(g->host.ctx, &count);
    qa_vec3 origin;
    if ((qa_js_to_int32(e->spawnflags) & 1) != 0 && count != 0) {
      const qa_body_state_result body = qa_shared_body_table_read(g->host.bodies, players[0]);
      origin = body.has_value ? body.value.origin : QA_Q1_ZERO;
    } else origin = qa_q1_get_body(g, target).origin;
    if (hooks == NULL || hooks->become_decoy == NULL) qa_range_error("Hipnotic finale requires the source decoy controller");
    (void)hooks->become_decoy(hooks->ctx, target->target, origin);
  }
  size_t count = 0;
  const qa_actor_id *listed = g->host.players(g->host.ctx, &count);
  qa_actor_id *players = malloc((count != 0 ? count : 1) * sizeof *players);
  if (players == NULL) qa_range_error("Out of memory");
  if (count != 0) memcpy(players, listed, count * sizeof *players);
  for (size_t i = 0; i < count; i++) qa_q1_control_player(g, players[i], qa_q1_get_body(g, point).origin, point->mangle, QA_Q1_ZERO);
  free(players);
  const char *callback = qa_q1_actor_text(e, "spawnfunction");
  if (callback[0] != '\0') {
    qa_q1mp_field_text(e, "hip:finale_callback", callback);
    qa_q1mp_world_later(g, e, e->wait, "hip:finale_callback");
  }
}
static void finale_callback(void *ctx, qa_q1_services *g, qa_q1_actor *e) {
  const char *callback = qa_q1_actor_text(e, "hip:finale_callback");
  if (qa_q1mp_streq(callback, "info_startendtext_use")) { end_text(ctx, g); return; }
  if (qa_q1mp_streq(callback, "SUB_UseTargets")) { qa_q1_use_targets(g, e, e->has_activator ? &e->activator : NULL); return; }
  if (qa_q1mp_streq(callback, "SUB_Remove")) { qa_q1_remove(g, e); return; }
  if (qa_q1mp_streq(callback, "SUB_Null")) return;
  /* Quake map function fields also name spawn routines. */
  char *previous = e->classname;
  e->classname = NULL;
  qa_q1mp_set_string(&e->classname, callback);
  const qa_actor_id id = e->actor.id;
  qa_q1_spawn_entity(g, e);
  if (qa_q1_get_entity(g, &id) != NULL) { free(e->classname); e->classname = previous; }
  else free(previous);
}
static void effect_finale_spawn(void *ctx, qa_q1_services *g, qa_q1_actor *e) {
  (void)ctx;
  if (g->options.deathmatch != 0) { qa_q1_remove(g, e); return; }
  qa_q1_body_patch patch = {0};
  patch.has_angles = true; patch.angles = e->mangle;
  qa_q1_set_body(g, e, &patch);
  qa_q1mp_world_number(e, "finale_state", 0);
  e->use = qa_q1_callback_registry_use(g->named, e, "hip:effect_finale");
}
static void start_end_text_spawn(void *ctx, qa_q1_services *g, qa_q1_actor *e) {
  (void)ctx;
  e->use = qa_q1_callback_registry_use(g->named, e, "hip:start_end_text");
}
void qa_q1mp_register_mission_campaign(qa_q1_services *game, qa_q1mp_pack pack, const qa_q1mp_world_hooks *hooks) {
  qa_q1_base *base = qa_q1_base_for(game);
  void *hook_ctx = (void *)hooks; /* borrowed for the life of the world (TS closure) */
  qa_q1_callback_registry_register(game->named, "mission:finale_transition", (qa_q1_callback_handlers){.action = finale_transition});
  qa_q1_callback_registry_register(game->named, "mission:finale_check", (qa_q1_callback_handlers){.ctx = base, .action = finale_check});
  const bool rogue = qa_q1mp_streq(pack, "rogue");
  qa_q1_intermission_rule rule = {0};
  rule.id = rogue ? "q1:rogue:campaign" : "q1:hipnotic:campaign";
  rule.ctx = game;
  rule.finale = rogue ? rogue_finale : hipnotic_finale;
  qa_q1_level_rules_register_rule(qa_q1_base_level_rules(base), rule);
  if (!qa_q1mp_streq(pack, "hipnotic")) return;
  qa_q1_callback_registry_register(game->named, "hip:start_end_text",
                                   (qa_q1_callback_handlers){.ctx = hook_ctx, .action = end_text_action, .use = end_text_use});
  qa_q1_callback_registry_register(game->named, "hip:effect_finale", (qa_q1_callback_handlers){.ctx = hook_ctx, .use = effect_finale_use});
  qa_q1_callback_registry_register(game->named, "hip:finale_callback", (qa_q1_callback_handlers){.ctx = hook_ctx, .action = finale_callback});
  qa_q1_register_spawn(game, "effect_finale", effect_finale_spawn, NULL);
  qa_q1_register_spawn(game, "info_startendtext", start_end_text_spawn, NULL);
}
