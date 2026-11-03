/* Port of src/content/q1/addons/campaign.ts. quakec_mg1/items_runes.qc,
 * map_specific; quakec_mg3/items_runes.qc. Copyright (C) 1996-2026 id
 * Software LLC. GPL-2.0-or-later.
 * Callback, rule and selection names are `${program}:campaign:...`:
 * static literals per program, since slots and rules borrow the name. */
#include "qa/content/q1/addons/campaign.h"

#include <stdio.h>
#include <string.h>

#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/foundation/movers.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

double qa_q1ad_mg1_last_sigil(double flags) {
  return (double)(qa_js_to_int32(qa_js_ushr(flags, QA_Q1AD_MG1_LAST_SIGIL_SHIFT)) & QA_Q1AD_MG1_ALL_SIGILS);
}
double qa_q1ad_mg1_clear_last_sigil(double flags) {
  return (double)(qa_js_to_int32(flags) & ~(QA_Q1AD_MG1_ALL_SIGILS << QA_Q1AD_MG1_LAST_SIGIL_SHIFT));
}
int qa_q1ad_mg3_rune_count(double flags) {
  int count = 0;
  const int32_t bits[4] = {1, 2, 4, 8};
  for (int i = 0; i < 4; i++) if ((qa_js_to_int32(flags) & bits[i]) != 0) count++;
  return count;
}

#define NAMED(context, name) \
  ((const char *const[]){"dopa:campaign:" name, "mg1:campaign:" name, "mg3:campaign:" name, \
                         "ctf:campaign:" name}[(context)->program])

static bool same(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static const char *map_name(const qa_q1_services *game) { return game->map_name != NULL ? game->map_name : ""; }
static double read_flags(qa_q1ad_context *context) {
  qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
  return campaign.read_flags(campaign.ctx);
}
static void write_flags(qa_q1ad_context *context, double flags) {
  qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
  campaign.write_flags(campaign.ctx, flags);
}
static double cvar(qa_q1ad_context *context, const char *name) {
  return context->services.cvar(context->services.ctx, name);
}
static void emit_addon(qa_q1ad_context *context, qa_q1ad_event event) {
  context->services.emit(context->services.ctx, &event);
}
static void schedule_action(qa_q1ad_context *context, qa_q1_actor *entity, const char *name, double delay) {
  qa_q1_services *game = context->game;
  qa_q1_schedule(game, entity, delay, qa_q1_callback_registry_action(game->named, entity, name));
}
static const qa_actor_id *activator_of(const qa_q1_actor *entity) {
  return entity->has_activator ? &entity->activator : NULL;
}
static void achievement(qa_q1_services *game, const char *id) {
  qa_q1_event event = {.kind = "achievement", .id = id}; /* player: null */
  qa_q1_emit(game, event);
}
static int32_t spawnflags(const qa_q1_actor *entity) { return qa_js_to_int32(entity->spawnflags); }

static void gib_monster(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_actor *world = game->world;
  if (world == NULL) qa_range_error("Horde exit requires the source world actor");
  qa_actor_id id = world->actor.id;
  (void)qa_q1_damage(game, entity->actor.id, id, &id, 4000, NULL, NULL, NULL, NULL);
}

/* --- intermission rule --- */
static void rule_touch(void *ctx, qa_q1_actor *trigger, qa_actor_id player) {
  (void)player;
  qa_q1ad_context *context = ctx;
  qa_q1_services *game = context->game;
  if (game->world != NULL) (void)qa_q1_actor_field_set(game->world, "addon.intermissiontext", qa_q1_actor_text(trigger, "endtext"));
  if (same(game->map_name, "mgend")) write_flags(context, 0);
}

static void rule_begin(void *ctx, const char *map, const qa_actor_id *cause) {
  (void)cause;
  qa_q1ad_context *context = ctx;
  qa_q1_services *game = context->game;
  const qa_q1_base_options *options = qa_q1_base_options_of(context->base);
  if (!(options->has_official_campaign && !options->official_campaign)) {
    const char *name = map_name(game);
    const char *complete = strcmp(name, "e5end") == 0 ? "E5END"
        : strcmp(name, "mgend") == 0 ? "MGEND"
        : context->program == QA_Q1AD_MG3 && strcmp(name, "boss") == 0 && (strcmp(map, "start") == 0 || strcmp(map, "map1") == 0) ? "MG3"
        : NULL;
    if (complete != NULL) {
      char id[64];
      (void)snprintf(id, sizeof id, "ACH_COMPLETE_%s", complete);
      achievement(game, id);
      if (qa_q1_option_skill(game) == 3) {
        (void)snprintf(id, sizeof id, "ACH_COMPLETE_%s_NIGHTMARE", complete);
        achievement(game, id);
      }
    }
  }
  if (same(game->map_name, "e5m6") && strcmp(map, "e5sm2") == 0) achievement(game, "ACH_FIND_E5M8");
  if (same(game->map_name, "mge1m1") && strcmp(map, "mge1m3") == 0) achievement(game, "ACH_FIND_MGE1M3");
  if (context->program != QA_Q1AD_MG3) {
    qa_q1_actor *manager = NULL;
    for (size_t i = 0; i < game->entity_count && manager == NULL; i++)
      if (game->entities[i] != NULL && same(game->entities[i]->classname, "horde_manager")) manager = game->entities[i];
    if (manager != NULL) {
      qa_q1_cancel(game, manager);
      for (size_t i = 0; i < game->entity_count; i++) {
        qa_q1_actor *monster = game->entities[i];
        if (monster != NULL && strcmp(qa_q1_actor_text(monster, "category"), "monster") == 0)
          schedule_action(context, monster, NAMED(context, "gib_monster"), 0.2 + qa_q1_host_random(game) * 1.8);
      }
    }
  }
}

static void finale_text(qa_q1_intermission_result *out, const char *text, double track) {
  *out = (qa_q1_intermission_result){.kind = "finale", .track = track};
  (void)snprintf(out->text, sizeof out->text, "%s", text);
}

static int rule_finale(void *ctx, double stage, const char *next_map, qa_q1_intermission_result *out) {
  (void)next_map;
  qa_q1ad_context *context = ctx;
  qa_q1_services *game = context->game;
  if (stage == 2) {
    const char *map = map_name(game), *text;
    char held[4096];
    if (strcmp(map, "e1m7") == 0) text = qa_q1_base_registered(context->base) ? "$qc_finale_e1" : "$qc_finale_e1_shareware";
    else if (strcmp(map, "e2m6") == 0) text = "$qc_finale_e2";
    else if (strcmp(map, "e3m6") == 0) text = "$qc_finale_e3";
    else if (strcmp(map, "e4m7") == 0) text = "$qc_finale_e4";
    else {
      /* game.world?.text("endtext") || game.world?.text("addon.intermissiontext") || "" */
      text = game->world != NULL ? qa_q1_actor_text(game->world, "endtext") : "";
      if (text[0] == '\0' && game->world != NULL) text = qa_q1_actor_text(game->world, "addon.intermissiontext");
      (void)snprintf(held, sizeof held, "%s", text); /* the field is cleared below */
      text = held;
    }
    if (game->world != NULL) (void)qa_q1_actor_field_set(game->world, "addon.intermissiontext", "");
    if (text[0] == '\0') return 2;
    finale_text(out, text, 2);
    return 1;
  }
  if (stage == 3) {
    if (!qa_q1_base_registered(context->base)) { *out = (qa_q1_intermission_result){.kind = "sell-screen"}; return 1; }
    if (context->program != QA_Q1AD_MG3 &&
        (qa_js_to_int32(read_flags(context)) & QA_Q1AD_MG1_ALL_SIGILS) == QA_Q1AD_MG1_ALL_SIGILS) {
      finale_text(out, "$qc_mg1_endtext_all_runes", 2);
      return 1;
    }
  }
  return 2;
}

static bool rule_travel(void *ctx, const char *map, const qa_actor_id *cause) {
  qa_q1ad_context *context = ctx;
  qa_q1_services *game = context->game;
  const char *name = map_name(game);
  if (cvar(context, "samelevel") != 0) { qa_q1_travel(game, name, cause); return true; }
  if (context->program != QA_Q1AD_MG3 && cvar(context, "horde") != 0) {
    qa_q1_travel(game, strcmp(name, "horde1") == 0 ? "horde2" : strcmp(name, "horde2") == 0 ? "horde3"
                     : strcmp(name, "horde3") == 0 ? "horde4" : "horde1", cause);
    return true;
  }
  const int32_t nightmare = QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE | QA_Q1AD_BLOODY_NIGHTMARE_NEWGAME;
  if (context->program == QA_Q1AD_MG3 && strcmp(name, "hub") == 0 &&
      (qa_js_to_int32(read_flags(context)) & nightmare) == nightmare && strcmp(map, "secret2") == 0) {
    qa_q1_travel(game, "boss2", cause);
    return true;
  }
  if (strcmp(map, "start") == 0 && !game->options.coop && game->options.deathmatch == 0) {
    qa_q1_event event = {.kind = "server-command", .text = "menu_credits\ndisconnect\n"};
    qa_q1_emit(game, event);
    return true;
  }
  return false;
}

/* --- named callbacks --- */
static void sigil_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                        const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface;
  qa_q1ad_context *context = ctx;
  if (!same(entity->solid, "trigger") || !qa_q1_is_player(game, &other) || qa_q1_health(game, other) <= 0) return;
  qa_q1ad_broadcast(context, qa_q1_actor_text(entity, "netname"));
  qa_owned_actor player;
  if (qa_session_actor_registry_resolve_owned(game->host.actors, other, &player))
    qa_q1_sound(game, player.id, "misc/runekey.wav", "item", 1, 1);
  qa_q1_effect(game, "pickup", qa_q1_get_body(game, entity).origin, &other, 1);
  entity->solid = "none";
  qa_q1ad_set_text(&entity->model, "");
  qa_q1_link(game, entity);
  double bits = context->program == QA_Q1AD_MG3 ? qa_q1_actor_number(entity, "style")
                                                 : (double)(spawnflags(entity) & QA_Q1AD_MG1_ALL_SIGILS);
  int32_t flags = qa_js_to_int32(read_flags(context)) | qa_js_to_int32(bits) |
                  (context->program == QA_Q1AD_MG3 ? 0 : qa_js_to_int32(qa_js_shl(bits, QA_Q1AD_MG1_LAST_SIGIL_SHIFT)));
  write_flags(context, (double)flags);
  if (context->program != QA_Q1AD_MG3 && cvar(context, "horde") != 0 && (qa_js_to_int32(bits) & 2) != 0) {
    size_t count = 0;
    const qa_actor_id *players = game->host.players(game->host.ctx, &count);
    for (size_t i = 0; i < count; i++) qa_q1ad_set_player_number(context, players[i], "hunger_time", game->time + 10);
  }
  emit_addon(context, (qa_q1ad_event){.kind = QA_Q1AD_EVENT_RUNE_COLLECTED, .program = context->program,
                                      .has_player = true, .player = other, .bits = bits});
  qa_q1_use_targets(game, entity, &other);
}

static void drop_item(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_q1_body body = qa_q1_get_body(game, entity);
  qa_vec3 start = qa_q1_vadd(body.origin, (qa_vec3){0, 0, 6});
  qa_q1_trace floor = qa_q1_host_trace(game, (qa_q1_trace_request){
      .start = start, .end = qa_q1_vadd(start, (qa_vec3){0, 0, -256}), .bounds = body.bounds,
      .has_ignore = true, .ignore = entity->actor.id, .monsters = true});
  if (floor.all_solid || floor.fraction == 1) { qa_q1_remove(game, entity); return; }
  qa_q1_body_patch patch = {.has_origin = true, .origin = floor.end, .has_velocity = true, .velocity = QA_Q1_ZERO,
                            .has_ground = true, .ground = floor.has_actor ? &floor.actor : NULL};
  qa_q1_set_body(game, entity, &patch);
  qa_q1_link(game, entity);
  /* MG3 items.qc PlaceItem: flag 128 sigils start hidden until used. */
  if (context->program == QA_Q1AD_MG3 && (spawnflags(entity) & 128) != 0) {
    qa_q1ad_set_text(&entity->model, "");
    entity->solid = "none";
    qa_q1_link(game, entity);
  }
}

static void use_targets_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_use_targets(game, entity, activator_of(entity));
}

static void indicator_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                          const qa_actor_id *activator) {
  (void)other;
  qa_q1ad_alpha(ctx, entity, 1);
  qa_q1_use_targets(game, entity, activator);
}

static void hub_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                      const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface;
  qa_q1ad_context *context = ctx;
  if (!qa_q1_is_player(game, &other)) return;
  if (game->options.no_exit == 1 || (game->options.no_exit == 2 && !same(game->map_name, "start"))) {
    qa_actor_id self = entity->actor.id;
    (void)qa_q1_damage(game, other, self, &self, 50000, NULL, "direct", "exit", NULL);
    return;
  }
  qa_q1_level_rules *rules = qa_q1_base_level_rules(context->base);
  const qa_q1_base_options *options = qa_q1_base_options_of(context->base);
  qa_q1_level_rules_changelevel_touched(rules, entity, other);
  if (options->player_exited != NULL) options->player_exited(options->ctx, other);
  qa_q1_use_targets(game, entity, &other);
  bool same_level = options->same_level != NULL && options->same_level(options->ctx);
  const char *map = same_level ? map_name(game) : qa_q1_actor_text(entity, "map");
  if ((spawnflags(entity) & 1) != 0 && game->options.deathmatch == 0 && qa_q1_actor_text(entity, "endtext")[0] == '\0') {
    qa_q1_level_rules_travel_to(rules, map, &other);
    return;
  }
  entity->touch = (qa_q1_bound_touch){0};
  entity->has_activator = true;
  entity->activator = other;
  schedule_action(context, entity, NAMED(context, "hub_changelevel"), 0.1);
}

static void hub_changelevel(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_q1_level_rules *rules = qa_q1_base_level_rules(context->base);
  qa_q1_level_rules_begin(rules, qa_q1_actor_text(entity, "map"), activator_of(entity));
  if ((spawnflags(entity) & 1) != 0) {
    qa_q1_intermission_result result = qa_q1_level_rules_advance_finale(rules, game->time);
    if (same(result.kind, "finale")) {
      emit_addon(context, (qa_q1ad_event){.kind = QA_Q1AD_EVENT_MUSIC, .track = result.track, .loop_track = 3});
      qa_q1_event event = {.kind = "finale", .text = result.text, .stage = 2};
      qa_q1_emit(game, event);
    } else if (same(result.kind, "sell-screen")) {
      emit_addon(context, (qa_q1ad_event){.kind = QA_Q1AD_EVENT_SELL_SCREEN});
    }
  }
}

static void electrode_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                            const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)surface;
  if (!qa_q1_is_player(game, &other) || qa_q1_health(game, other) <= 0) return;
  qa_q1_call_touch(game, entity, qa_q1_callback_registry_touch(game->named, entity, "button_touch"), other, normal, NULL);
  entity->touch = (qa_q1_bound_touch){0};
  for (size_t i = 0; i < game->entity_count; i++) {
    double left;
    double right;
    qa_q1_actor *target = game->entities[i];
    if (target != NULL && same(target->classname, "mge2m2_electrode_target") &&
        (left = qa_q1_actor_number(target, "cnt"), right = qa_q1_actor_number(entity, "cnt"), left == right))
      qa_q1_schedule(game, target, 0.1, qa_q1_callback_registry_action(game->named, target, "SUB_Remove"));
  }
}

static void egg_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                    const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  qa_q1_actor *targets[256];
  size_t count = qa_q1_find(game, entity->target, targets, 256);
  for (size_t i = 0; i < count; i++) {
    qa_q1_actor *target = targets[i];
    if (!same(target->classname, "func_door")) continue;
    target->movedir = qa_q1_actor_field_get(target, "dest2") != NULL ? qa_q1_actor_vector(target, "dest2") : target->dest2;
    target->pos1 = qa_q1_get_body(game, target).origin;
    target->pos2 = qa_q1_vadd(target->pos1, target->movedir);
    target->state = "bottom";
    target->speed = 500;
    (void)qa_q1_actor_field_set(target, "noise1", "doors/drclos4.wav");
    (void)qa_q1_actor_field_set(target, "noise2", "doors/doormv1.wav");
    qa_q1_door_up(game, target);
  }
}

static void fix_pickup(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  bool enable = entity->activated;
  entity->activated = true;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *rune = game->entities[i];
    if (rune == NULL || !same(rune->classname, "item_sigil")) continue;
    rune->touch = enable ? qa_q1_callback_registry_touch(game->named, rune, NAMED(context, "sigil_touch"))
                         : (qa_q1_bound_touch){0};
  }
}
static void fix_pickup_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                           const qa_actor_id *activator) {
  (void)other; (void)activator;
  fix_pickup(ctx, game, entity);
}

/* --- spawns --- */
static int first_bit(int32_t flags, int count) {
  for (int i = 0; i < count; i++) if ((flags & (1 << i)) != 0) return i;
  return -1;
}

static void spawn_sigil(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  bool mission3 = context->program == QA_Q1AD_MG3;
  int32_t spawned = mission3 ? spawnflags(entity) & 128 : 0;
  if (entity->spawnflags == 0) entity->spawnflags = 1;
  int index = first_bit(spawnflags(entity), mission3 ? 4 : 6);
  if (index < 0) qa_range_error("%s item_sigil has no source rune model", qa_q1ad_program_name(context->program));
  int32_t bit = (int32_t)1 << index;
  entity->spawnflags = (double)(bit | spawned);
  qa_q1ad_set_number(context, entity, "style", bit);
  char text[64];
  (void)snprintf(text, sizeof text, mission3 ? "progs/end%d.mdl" : "progs/mg1_rune%d.mdl", index + 1);
  qa_q1ad_set_text(&entity->model, text);
  (void)snprintf(text, sizeof text, mission3 ? "$mg3_qc_rune%d" : "$qc_mg1_pickup_rune%d", index + 1);
  (void)qa_q1_actor_field_set(entity, "netname", text);
  entity->solid = "trigger";
  entity->movement = "toss";
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, NAMED(context, "sigil_touch"));
  qa_q1ad_set_text(&entity->original_model, entity->model);
  if (spawned != 0) entity->use = qa_q1_callback_registry_use(game->named, entity, "mg3:items:regenerate");
  qa_q1_set_bounds(game, entity, (qa_bounds){{-16, -16, -24}, {16, 16, 32}});
  schedule_action(context, entity, NAMED(context, "drop_item"), 0.2);
}

static void spawn_indicator(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  bool active = (spawnflags(entity) & 64) != 0;
  entity->spawnflags = (double)(spawnflags(entity) & ~64);
  if (entity->spawnflags == 0) entity->spawnflags = 1;
  int index = first_bit(spawnflags(entity), 6);
  if (index < 0) qa_range_error("misc_rune_indicator has no rune model");
  int32_t bit = (int32_t)1 << index;
  entity->spawnflags = bit;
  char model[64];
  (void)snprintf(model, sizeof model, "progs/mg1_rune%d.mdl", index + 1);
  qa_q1ad_set_text(&entity->model, model);
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "indicator_use"));
  if ((qa_js_to_int32(read_flags(context)) & bit) != 0 || active) {
    schedule_action(context, entity, NAMED(context, "use_targets"), 0.2);
    return;
  }
  qa_q1ad_alpha(context, entity, 0.2);
}

static void spawn_bossgate(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  bool inverse = (spawnflags(entity) & 64) != 0;
  entity->spawnflags = (double)(spawnflags(entity) & ~64);
  if (entity->spawnflags == 0) entity->spawnflags = 15;
  entity->spawnflags = (double)(spawnflags(entity) & QA_Q1AD_MG1_ALL_SIGILS);
  bool complete = (qa_js_to_int32(read_flags(context)) & spawnflags(entity)) == spawnflags(entity);
  if (complete != inverse) { qa_q1_remove(game, entity); return; }
  entity->solid = "bsp";
  entity->movement = "push";
  qa_q1_body_patch patch = {.has_angles = true, .angles = QA_Q1_ZERO};
  qa_q1_set_body(game, entity, &patch);
  entity->use = qa_q1_callback_registry_use(game->named, entity, "func_wall_use");
  if (entity->target[0] != '\0' || entity->killtarget[0] != '\0')
    schedule_action(context, entity, NAMED(context, "use_targets"), 0.2);
}

static void spawn_nothing(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; (void)game; (void)entity;
}

static void spawn_changelevel(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (qa_q1_actor_text(entity, "map")[0] == '\0') qa_range_error("changelevel trigger doesn't have map");
  qa_q1ad_init_trigger(context, entity);
  if (qa_q1_live(game, entity)) entity->touch = qa_q1_callback_registry_touch(game->named, entity, NAMED(context, "hub_touch"));
}

static void spawn_hub_changelevel(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if ((qa_js_to_int32(read_flags(context)) & QA_Q1AD_MG1_ALL_SIGILS) != QA_Q1AD_MG1_ALL_SIGILS) {
    qa_q1_remove(game, entity);
    return;
  }
  if (qa_q1_actor_text(entity, "map")[0] == '\0') qa_range_error("hub_trigger_changelevel is missing its map");
  qa_q1ad_init_trigger(context, entity);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, NAMED(context, "hub_touch"));
}

static void spawn_electrode_button(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_q1_spawn_button(game, entity);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, NAMED(context, "electrode_touch"));
}

static void spawn_egg_opener(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "egg_use"));
}

static void spawn_pickup_fixer(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "fix_pickup"));
  schedule_action(context, entity, NAMED(context, "fix_pickup"), 0.8);
}

/* --- spawn selections (0 = TS undefined, 1 = point) --- */
static bool any_classname(qa_q1_services *game, const char *classname) {
  for (size_t i = 0; i < game->entity_count; i++)
    if (game->entities[i] != NULL && same(game->entities[i]->classname, classname)) return true;
  return false;
}

static int select_mg3_start(void *ctx, bool force_spawn, qa_q1_actor **out) {
  (void)force_spawn;
  qa_q1ad_context *context = ctx;
  qa_q1_services *game = context->game;
  if (game->options.coop || game->options.deathmatch != 0 || any_classname(game, "testplayerstart")) return 0;
  for (size_t i = 0; i < game->entity_count; i++)
    if (game->entities[i] != NULL && same(game->entities[i]->classname, "info_player_start")) {
      *out = game->entities[i];
      return 1;
    }
  qa_range_error("PutClientInServer: no info_player_start on level");
}

static int select_hub(void *ctx, bool force_spawn, qa_q1_actor **out) {
  (void)force_spawn;
  qa_q1ad_context *context = ctx;
  qa_q1_services *game = context->game;
  if (game->options.coop || game->options.deathmatch != 0) return 0;
  if (any_classname(game, "testplayerstart")) return 0;
  double pickup = qa_q1ad_mg1_last_sigil(read_flags(context));
  if (pickup == 0) return 0;
  int index = first_bit(qa_js_to_int32(pickup), 6);
  char wanted[32];
  (void)snprintf(wanted, sizeof wanted, "start_%d", index + 1);
  qa_q1_actor *point = NULL;
  for (size_t i = 0; i < game->entity_count && point == NULL; i++)
    if (game->entities[i] != NULL && strcmp(qa_q1_actor_text(game->entities[i], "netname"), wanted) == 0)
      point = game->entities[i];
  if (point == NULL || !same(point->classname, "info_player_start_hub")) return 0;
  write_flags(context, qa_q1ad_mg1_clear_last_sigil(read_flags(context)));
  *out = point;
  return 1;
}

static void named(qa_q1_services *game, const char *name, qa_q1_callback_handlers handlers) {
  qa_q1_callback_registry_register(game->named, name, handlers);
}

void qa_q1ad_register_campaign_addons(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1_base *base = context->base;
  named(game, NAMED(context, "gib_monster"), (qa_q1_callback_handlers){.ctx = context, .action = gib_monster});
  static const char *const RULE_IDS[] = {"dopa:campaign", "mg1:campaign", "mg3:campaign", "ctf:campaign"};
  qa_q1_level_rules_register_rule(qa_q1_base_level_rules(base), (qa_q1_intermission_rule){
      .id = RULE_IDS[context->program], .ctx = context, .touch = rule_touch, .begin = rule_begin,
      .finale = rule_finale, .travel = rule_travel});
  named(game, NAMED(context, "sigil_touch"), (qa_q1_callback_handlers){.ctx = context, .touch = sigil_touch});
  named(game, NAMED(context, "drop_item"), (qa_q1_callback_handlers){.ctx = context, .action = drop_item});
  named(game, NAMED(context, "use_targets"), (qa_q1_callback_handlers){.ctx = context, .action = use_targets_action});
  named(game, NAMED(context, "indicator_use"), (qa_q1_callback_handlers){.ctx = context, .use = indicator_use});
  named(game, NAMED(context, "hub_touch"), (qa_q1_callback_handlers){.ctx = context, .touch = hub_touch});
  named(game, NAMED(context, "hub_changelevel"), (qa_q1_callback_handlers){.ctx = context, .action = hub_changelevel});
  named(game, NAMED(context, "electrode_touch"), (qa_q1_callback_handlers){.ctx = context, .touch = electrode_touch});
  named(game, NAMED(context, "egg_use"), (qa_q1_callback_handlers){.ctx = context, .use = egg_use});
  named(game, NAMED(context, "fix_pickup"),
        (qa_q1_callback_handlers){.ctx = context, .action = fix_pickup, .use = fix_pickup_use});
  qa_q1_replace_spawn(game, "item_sigil", spawn_sigil, context);
  qa_q1_register_spawn(game, "misc_rune_indicator", spawn_indicator, context);
  qa_q1_replace_spawn(game, "func_bossgate", spawn_bossgate, context);
  qa_q1_register_spawn(game, "info_player_start_hub", spawn_nothing, context);
  qa_q1_replace_spawn(game, "trigger_changelevel", spawn_changelevel, context);
  qa_q1_spawn_selector *selector = qa_q1_base_spawn_selector(base);
  if (context->program == QA_Q1AD_MG3) qa_q1_spawn_selector_register_selection(selector, "mg3:start", select_mg3_start, context);
  if (context->program != QA_Q1AD_MG3) {
    static const char *const HUB_IDS[] = {"dopa:hub", "mg1:hub", "mg3:hub", "ctf:hub"};
    qa_q1_spawn_selector_register_selection(selector, HUB_IDS[context->program], select_hub, context);
    qa_q1_register_spawn(game, "hub_trigger_changelevel", spawn_hub_changelevel, context);
    qa_q1_register_spawn(game, "mge2m2_electrode_target", spawn_nothing, context);
    qa_q1_register_spawn(game, "mge2m2_electrode_button", spawn_electrode_button, context);
    qa_q1_register_spawn(game, "mge2m2_rune_egg_opener", spawn_egg_opener, context);
    qa_q1_register_spawn(game, "mge2m2_rune_pickup_fixer", spawn_pickup_fixer, context);
  }
}
