/* Port of src/content/q1/addons/campaign.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/campaign.h"

#include <stdio.h>
#include <string.h>

#include "qa/foundation/support.h"

double qa_q1ad_mg1_last_sigil(double flags) {
  return (double)(((uint32_t)flags >> QA_Q1AD_MG1_LAST_SIGIL_SHIFT) & QA_Q1AD_MG1_ALL_SIGILS);
}
double qa_q1ad_mg1_clear_last_sigil(double flags) {
  return (double)(int32_t)((int32_t)flags & ~(QA_Q1AD_MG1_ALL_SIGILS << QA_Q1AD_MG1_LAST_SIGIL_SHIFT));
}
int qa_q1ad_mg3_rune_count(double flags) {
  int count = 0;
  const int bits[4] = {1, 2, 4, 8};
  for (int i = 0; i < 4; i++) if (((int32_t)flags & bits[i]) != 0) count++;
  return count;
}

static void prefix_of(qa_q1ad_context *context, char out[QA_Q1AD_NAME]) {
  (void)snprintf(out, QA_Q1AD_NAME, "%s:campaign:", qa_q1ad_program_name(context->program));
}
static double read_flags(qa_q1ad_context *context) {
  return context->game.campaign_read_flags != NULL
      ? context->game.campaign_read_flags(context->game.ctx) : 0;
}
static void write_flags(qa_q1ad_context *context, double flags) {
  if (context->game.campaign_write_flags != NULL)
    context->game.campaign_write_flags(context->game.ctx, flags);
}
static void action(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name, double delay) {
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_schedule_named(context, entity, delay, prefix, name);
}
static void use_targets(qa_q1ad_context *context, qa_q1ad_actor *entity, const qa_actor_id *activator) {
  qa_q1ad_use_targets(context, entity, activator);
}
static void host_emit_simple(qa_q1ad_context *context, const char *kind, const char *text) {
  qa_q1ad_host_event event = {.kind = kind, .player_null = true, .text = text};
  qa_q1ad_host_emit(context, &event);
}

static void gib_monster(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_actor *world = qa_q1ad_world(context);
  if (world == NULL) qa_range_error("Horde exit requires the source world actor");
  qa_actor_id wid = world->actor.id;
  qa_q1ad_damage(context, entity->actor.id, &wid, &wid, 4000, NULL, NULL, NULL);
}

void qa_q1ad_campaign_intermission_touch(qa_q1ad_context *context, qa_q1ad_actor *trigger, qa_actor_id player) {
  (void)player;
  qa_q1ad_actor *world = qa_q1ad_world(context);
  if (world != NULL) qa_q1ad_actor_set(world, "addon.intermissiontext", qa_q1ad_actor_text(trigger, "endtext"));
  if (context->game.map_name != NULL && strcmp(context->game.map_name, "mgend") == 0) write_flags(context, 0);
}

void qa_q1ad_campaign_intermission_begin(qa_q1ad_context *context, const char *map, const qa_actor_id *cause) {
  (void)cause;
  const char *map_name = context->game.map_name != NULL ? context->game.map_name : "";
  bool official = !context->game.has_official_campaign || context->game.official_campaign;
  if (official) {
    const char *complete = NULL;
    char id[64];
    if (strcmp(map_name, "e5end") == 0) complete = "E5END";
    else if (strcmp(map_name, "mgend") == 0) complete = "MGEND";
    else if (context->program == QA_Q1AD_MG3 && strcmp(map_name, "boss") == 0 &&
             (strcmp(map, "start") == 0 || strcmp(map, "map1") == 0)) complete = "MG3";
    if (complete != NULL) {
      (void)snprintf(id, sizeof id, "ACH_COMPLETE_%s", complete);
      host_emit_simple(context, "achievement", id);
      if (context->game.skill == 3) {
        (void)snprintf(id, sizeof id, "ACH_COMPLETE_%s_NIGHTMARE", complete);
        host_emit_simple(context, "achievement", id);
      }
    }
  }
  if (strcmp(map_name, "e5m6") == 0 && strcmp(map, "e5sm2") == 0)
    host_emit_simple(context, "achievement", "ACH_FIND_E5M8");
  if (strcmp(map_name, "mge1m1") == 0 && strcmp(map, "mge1m3") == 0)
    host_emit_simple(context, "achievement", "ACH_FIND_MGE1M3");
  if (context->program != QA_Q1AD_MG3) {
    qa_q1ad_actor *entities[256];
    size_t count = qa_q1ad_entities(context, entities, 256);
    qa_q1ad_actor *manager = NULL;
    for (size_t i = 0; i < count; i++)
      if (entities[i]->classname != NULL && strcmp(entities[i]->classname, "horde_manager") == 0) { manager = entities[i]; break; }
    if (manager != NULL) {
      qa_q1ad_cancel(context, manager);
      for (size_t i = 0; i < count; i++)
        if (strcmp(qa_q1ad_actor_text(entities[i], "category"), "monster") == 0)
          action(context, entities[i], "gib_monster", 0.2 + qa_q1ad_random(context) * 1.8);
    }
  }
}

bool qa_q1ad_campaign_finale(qa_q1ad_context *context, double stage, qa_q1ad_finale *out) {
  const char *map = context->game.map_name != NULL ? context->game.map_name : "";
  if (stage == 2) {
    const char *text;
    qa_q1ad_actor *world = qa_q1ad_world(context);
    if (strcmp(map, "e1m7") == 0) text = context->game.registered ? "$qc_finale_e1" : "$qc_finale_e1_shareware";
    else if (strcmp(map, "e2m6") == 0) text = "$qc_finale_e2";
    else if (strcmp(map, "e3m6") == 0) text = "$qc_finale_e3";
    else if (strcmp(map, "e4m7") == 0) text = "$qc_finale_e4";
    else {
      text = world != NULL ? qa_q1ad_actor_text(world, "endtext") : "";
      if (text[0] == '\0' && world != NULL) text = qa_q1ad_actor_text(world, "addon.intermissiontext");
    }
    if (world != NULL) qa_q1ad_actor_set(world, "addon.intermissiontext", "");
    if (text[0] == '\0') return false;
    *out = (qa_q1ad_finale){.kind = "finale", .text = text, .track = 2};
    return true;
  }
  if (stage == 3) {
    if (!context->game.registered) { *out = (qa_q1ad_finale){.kind = "sell-screen"}; return true; }
    if (context->program != QA_Q1AD_MG3 &&
        ((int32_t)read_flags(context) & QA_Q1AD_MG1_ALL_SIGILS) == QA_Q1AD_MG1_ALL_SIGILS) {
      *out = (qa_q1ad_finale){.kind = "finale", .text = "$qc_mg1_endtext_all_runes", .track = 2};
      return true;
    }
  }
  return false;
}

bool qa_q1ad_campaign_travel_rule(qa_q1ad_context *context, const char *map, const qa_actor_id *cause) {
  const char *map_name = context->game.map_name != NULL ? context->game.map_name : "";
  if (qa_q1ad_cvar(context, "samelevel") != 0) {
    if (context->game.travel != NULL) context->game.travel(context->game.ctx, map_name, cause);
    return true;
  }
  if (context->program != QA_Q1AD_MG3 && qa_q1ad_cvar(context, "horde") != 0) {
    const char *next = strcmp(map_name, "horde1") == 0 ? "horde2" : strcmp(map_name, "horde2") == 0 ? "horde3"
        : strcmp(map_name, "horde3") == 0 ? "horde4" : "horde1";
    if (context->game.travel != NULL) context->game.travel(context->game.ctx, next, cause);
    return true;
  }
  if (context->program == QA_Q1AD_MG3 && strcmp(map_name, "hub") == 0 &&
      ((int32_t)read_flags(context) & (QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE | QA_Q1AD_BLOODY_NIGHTMARE_NEWGAME)) ==
          (QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE | QA_Q1AD_BLOODY_NIGHTMARE_NEWGAME) && strcmp(map, "secret2") == 0) {
    if (context->game.travel != NULL) context->game.travel(context->game.ctx, "boss2", cause);
    return true;
  }
  if (strcmp(map, "start") == 0 && !context->game.coop && context->game.deathmatch == 0) {
    host_emit_simple(context, "server-command", "menu_credits\ndisconnect\n");
    return true;
  }
  return false;
}

static void touch_sigil(qa_q1ad_context *context, qa_q1ad_actor *entity,
                        const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (other == NULL) return;
  if (entity->solid == NULL || strcmp(entity->solid, "trigger") != 0 ||
      !qa_q1ad_is_player(context, other) || qa_q1ad_health(context, *other) <= 0) return;
  qa_q1ad_broadcast(context, qa_q1ad_actor_text(entity, "netname"));
  qa_owned_actor player;
  if (qa_q1ad_resolve_owned(context, *other, &player))
    qa_q1ad_sound_actor(context, player.id, "misc/runekey.wav", "item");
  qa_q1ad_body body = qa_q1ad_body_of(context, entity);
  qa_q1ad_effect(context, "pickup", body.origin, other, 0);
  entity->solid = "none";
  entity->model = "";
  qa_q1ad_link(context, entity);
  int32_t bits = context->program == QA_Q1AD_MG3
      ? (int32_t)qa_q1ad_actor_number(entity, "style")
      : (int32_t)entity->spawnflags & QA_Q1AD_MG1_ALL_SIGILS;
  write_flags(context, (double)(int32_t)((int32_t)read_flags(context) | bits |
      (context->program == QA_Q1AD_MG3 ? 0 : bits << QA_Q1AD_MG1_LAST_SIGIL_SHIFT)));
  if (context->program != QA_Q1AD_MG3 && qa_q1ad_cvar(context, "horde") != 0 && (bits & 2) != 0) {
    qa_actor_id players[32];
    size_t count = qa_q1ad_players(context, players, 32);
    for (size_t i = 0; i < count; i++)
      qa_q1ad_set_player_number(context, players[i], "hunger_time", context->game.time + 10);
  }
  qa_q1ad_event event = {.kind = QA_Q1AD_EVENT_RUNE_COLLECTED, .program = context->program,
                         .player = *other, .has_player = true, .bits = (double)bits};
  qa_q1ad_emit(context, &event);
  use_targets(context, entity, other);
}

static void drop_item(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_body body = qa_q1ad_body_of(context, entity);
  qa_vec3 start = qa_add3(body.origin, (qa_vec3){0, 0, 6});
  qa_q1ad_trace floor_trace = {0};
  qa_actor_id self = entity->actor.id;
  if (context->game.trace != NULL)
    (void)context->game.trace(context->game.ctx, start,
        qa_add3(start, (qa_vec3){0, 0, -256}), body.bounds, &self, true, &floor_trace);
  if (floor_trace.all_solid || floor_trace.fraction == 1) { qa_q1ad_remove(context, entity); return; }
  qa_q1ad_body_patch patch = {.has_origin = true, .origin = floor_trace.end,
      .has_velocity = true, .velocity = {0, 0, 0}, .has_ground = true,
      .ground = floor_trace.actor, .ground_null = !floor_trace.has_actor};
  qa_q1ad_set_body(context, entity, &patch);
  qa_q1ad_link(context, entity);
}

static void use_targets_action(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  use_targets(context, entity, entity->has_activator ? &entity->activator : NULL);
}
static void indicator_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                          const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  qa_q1ad_alpha(context, entity, 1);
  use_targets(context, entity, activator);
}
static void hub_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                      const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (!qa_q1ad_is_player(context, other)) return;
  const char *map_name = context->game.map_name != NULL ? context->game.map_name : "";
  if (context->game.no_exit == 1 || (context->game.no_exit == 2 && strcmp(map_name, "start") != 0)) {
    qa_actor_id self = entity->actor.id;
    qa_q1ad_damage(context, *other, &self, &self, 50000, NULL, "direct", "exit");
    return;
  }
  if (context->game.level_changelevel_touched != NULL)
    context->game.level_changelevel_touched(context->game.ctx, entity, *other);
  if (context->game.has_player_exited && context->game.player_exited != NULL)
    context->game.player_exited(context->game.ctx, *other);
  use_targets(context, entity, other);
  bool same = context->game.has_same_level && context->game.same_level != NULL &&
              context->game.same_level(context->game.ctx);
  const char *map = same ? map_name : qa_q1ad_actor_text(entity, "map");
  if (((int32_t)entity->spawnflags & 1) != 0 && context->game.deathmatch == 0 &&
      qa_q1ad_actor_text(entity, "endtext")[0] == '\0') {
    if (context->game.level_travel_to != NULL) context->game.level_travel_to(context->game.ctx, map, *other);
    return;
  }
  entity->touch[0] = '\0';
  entity->activator = *other;
  entity->has_activator = true;
  action(context, entity, "hub_changelevel", 0.1);
}
static void hub_changelevel(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (context->game.level_begin != NULL)
    context->game.level_begin(context->game.ctx, qa_q1ad_actor_text(entity, "map"),
                              entity->has_activator ? &entity->activator : NULL);
  if (((int32_t)entity->spawnflags & 1) != 0) {
    qa_q1ad_finale result = {0};
    if (context->game.level_advance_finale != NULL &&
        context->game.level_advance_finale(context->game.ctx, context->game.time, &result)) {
      if (result.kind != NULL && strcmp(result.kind, "finale") == 0) {
        qa_q1ad_event music = {.kind = QA_Q1AD_EVENT_MUSIC, .track = result.track, .loop_track = 3};
        qa_q1ad_emit(context, &music);
        qa_q1ad_host_event finale = {.kind = "finale", .text = result.text, .style = 2};
        qa_q1ad_host_emit(context, &finale);
      } else if (result.kind != NULL && strcmp(result.kind, "sell-screen") == 0) {
        qa_q1ad_event sell = {.kind = QA_Q1AD_EVENT_SELL_SCREEN};
        qa_q1ad_emit(context, &sell);
      }
    }
  }
}
static void electrode_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                            const qa_actor_id *other, const qa_vec3 *normal) {
  if (!qa_q1ad_is_player(context, other) || qa_q1ad_health(context, *other) <= 0) return;
  qa_q1ad_do_touch(context, entity, "button_touch", other, normal);
  entity->touch[0] = '\0';
  qa_q1ad_actor *entities[256];
  size_t count = qa_q1ad_entities(context, entities, 256);
  for (size_t i = 0; i < count; i++)
    if (entities[i]->classname != NULL && strcmp(entities[i]->classname, "mge2m2_electrode_target") == 0 &&
        qa_q1ad_actor_number(entities[i], "cnt") == qa_q1ad_actor_number(entity, "cnt"))
      qa_q1ad_schedule(context, entities[i], 0.1, "SUB_Remove");
}
static void egg_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                    const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q1ad_actor *targets[64];
  size_t count = qa_q1ad_find(context, entity->target != NULL ? entity->target : "", targets, 64);
  for (size_t i = 0; i < count; i++) {
    qa_q1ad_actor *target = targets[i];
    if (target->classname == NULL || strcmp(target->classname, "func_door") != 0) continue;
    target->movedir = qa_q1ad_actor_has(target, "dest2") ? qa_q1ad_actor_vector(target, "dest2") : target->dest2;
    target->pos1 = qa_q1ad_body_of(context, target).origin;
    target->pos2 = qa_add3(target->pos1, target->movedir);
    target->state = "bottom";
    target->speed = 500;
    qa_q1ad_actor_set(target, "noise1", "doors/drclos4.wav");
    qa_q1ad_actor_set(target, "noise2", "doors/doormv1.wav");
    if (context->game.door_up != NULL) context->game.door_up(context->game.ctx, target);
  }
}
static void fix_pickup(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  bool enable = entity->activated;
  entity->activated = true;
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_actor *entities[256];
  size_t count = qa_q1ad_entities(context, entities, 256);
  for (size_t i = 0; i < count; i++)
    if (entities[i]->classname != NULL && strcmp(entities[i]->classname, "item_sigil") == 0) {
      if (enable) qa_q1ad_bind_prefix(entities[i]->touch, prefix, "sigil_touch");
      else entities[i]->touch[0] = '\0';
    }
}
static void fix_pickup_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                           const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  fix_pickup(context, entity);
}

static void spawn_sigil(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  bool mission3 = context->program == QA_Q1AD_MG3;
  int32_t spawned = mission3 ? (int32_t)entity->spawnflags & 128 : 0;
  if (entity->spawnflags == 0) entity->spawnflags = 1;
  const int bits[6] = {1, 2, 4, 8, 16, 32};
  int count = mission3 ? 4 : 6, index = -1;
  for (int i = 0; i < count; i++) if (((int32_t)entity->spawnflags & bits[i]) != 0) { index = i; break; }
  if (index < 0)
    qa_range_error("%s item_sigil has no source rune model", qa_q1ad_program_name(context->program));
  int bit = bits[index];
  entity->spawnflags = (double)(bit | spawned);
  qa_q1ad_set_number(context, entity, "style", bit);
  static const char *const MG3_MODELS[] = {"progs/end1.mdl", "progs/end2.mdl", "progs/end3.mdl", "progs/end4.mdl"};
  static const char *const MG1_MODELS[] = {"progs/mg1_rune1.mdl", "progs/mg1_rune2.mdl", "progs/mg1_rune3.mdl",
      "progs/mg1_rune4.mdl", "progs/mg1_rune5.mdl", "progs/mg1_rune6.mdl"};
  static const char *const MG3_NAMES[] = {"$mg3_qc_rune1", "$mg3_qc_rune2", "$mg3_qc_rune3", "$mg3_qc_rune4"};
  static const char *const MG1_NAMES[] = {"$qc_mg1_pickup_rune1", "$qc_mg1_pickup_rune2", "$qc_mg1_pickup_rune3",
      "$qc_mg1_pickup_rune4", "$qc_mg1_pickup_rune5", "$qc_mg1_pickup_rune6"};
  entity->model = mission3 ? MG3_MODELS[index] : MG1_MODELS[index];
  qa_q1ad_actor_set(entity, "netname", mission3 ? MG3_NAMES[index] : MG1_NAMES[index]);
  entity->solid = "trigger";
  entity->movement = "toss";
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->touch, prefix, "sigil_touch");
  qa_q1ad_set_bounds(context, entity, (qa_bounds){{-16, -16, -24}, {16, 16, 32}});
  action(context, entity, "drop_item", 0.2);
}
static void spawn_indicator(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  bool active = ((int32_t)entity->spawnflags & 64) != 0;
  entity->spawnflags = (double)((int32_t)entity->spawnflags & ~64);
  if (entity->spawnflags == 0) entity->spawnflags = 1;
  const int bits[6] = {1, 2, 4, 8, 16, 32};
  int index = -1;
  for (int i = 0; i < 6; i++) if (((int32_t)entity->spawnflags & bits[i]) != 0) { index = i; break; }
  if (index < 0) qa_range_error("misc_rune_indicator has no rune model");
  int bit = bits[index];
  entity->spawnflags = bit;
  static const char *const MODELS[] = {"progs/mg1_rune1.mdl", "progs/mg1_rune2.mdl", "progs/mg1_rune3.mdl",
      "progs/mg1_rune4.mdl", "progs/mg1_rune5.mdl", "progs/mg1_rune6.mdl"};
  entity->model = MODELS[index];
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "indicator_use");
  if (((int32_t)read_flags(context) & bit) != 0 || active) { action(context, entity, "use_targets", 0.2); return; }
  qa_q1ad_alpha(context, entity, 0.2);
}
static void spawn_bossgate(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  bool inverse = ((int32_t)entity->spawnflags & 64) != 0;
  entity->spawnflags = (double)((int32_t)entity->spawnflags & ~64);
  if (entity->spawnflags == 0) entity->spawnflags = 15;
  entity->spawnflags = (double)((int32_t)entity->spawnflags & QA_Q1AD_MG1_ALL_SIGILS);
  bool complete = ((int32_t)read_flags(context) & (int32_t)entity->spawnflags) == (int32_t)entity->spawnflags;
  if (complete != inverse) { qa_q1ad_remove(context, entity); return; }
  entity->solid = "bsp";
  entity->movement = "push";
  qa_q1ad_body_patch patch = {.has_angles = true, .angles = {0, 0, 0}};
  qa_q1ad_set_body(context, entity, &patch);
  qa_q1ad_bind(entity->use, "func_wall_use");
  const char *killtarget = entity->killtarget != NULL ? entity->killtarget : "";
  if ((entity->target != NULL && entity->target[0] != '\0') || killtarget[0] != '\0')
    action(context, entity, "use_targets", 0.2);
}
static void spawn_nothing(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  (void)context; (void)entity;
}
static void spawn_changelevel(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_actor_text(entity, "map")[0] == '\0')
    qa_range_error("changelevel trigger doesn't have map");
  qa_q1ad_init_trigger(context, entity);
  if (qa_q1ad_live(context, entity)) {
    char prefix[QA_Q1AD_NAME];
    prefix_of(context, prefix);
    qa_q1ad_bind_prefix(entity->touch, prefix, "hub_touch");
  }
}
static void spawn_hub_changelevel(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (((int32_t)read_flags(context) & QA_Q1AD_MG1_ALL_SIGILS) != QA_Q1AD_MG1_ALL_SIGILS) {
    qa_q1ad_remove(context, entity);
    return;
  }
  if (qa_q1ad_actor_text(entity, "map")[0] == '\0')
    qa_range_error("hub_trigger_changelevel is missing its map");
  qa_q1ad_init_trigger(context, entity);
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->touch, prefix, "hub_touch");
}
static void spawn_electrode_button(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (context->game.spawn_button != NULL) context->game.spawn_button(context->game.ctx, entity);
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->touch, prefix, "electrode_touch");
}
static void spawn_egg_opener(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "egg_use");
}
static void spawn_pickup_fixer(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "fix_pickup");
  action(context, entity, "fix_pickup", 0.8);
}

qa_q1ad_actor *qa_q1ad_campaign_spawn_selection(qa_q1ad_context *context) {
  if (context->program == QA_Q1AD_MG3) {
    if (context->game.coop || context->game.deathmatch != 0) return NULL;
    qa_q1ad_actor *entities[256];
    size_t count = qa_q1ad_entities(context, entities, 256);
    for (size_t i = 0; i < count; i++)
      if (entities[i]->classname != NULL && strcmp(entities[i]->classname, "testplayerstart") == 0) return NULL;
    for (size_t i = 0; i < count; i++)
      if (entities[i]->classname != NULL && strcmp(entities[i]->classname, "info_player_start") == 0) return entities[i];
    qa_range_error("PutClientInServer: no info_player_start on level");
  }
  if (context->game.coop || context->game.deathmatch != 0) return NULL;
  qa_q1ad_actor *entities[256];
  size_t count = qa_q1ad_entities(context, entities, 256);
  for (size_t i = 0; i < count; i++)
    if (entities[i]->classname != NULL && strcmp(entities[i]->classname, "testplayerstart") == 0) return NULL;
  double pickup = qa_q1ad_mg1_last_sigil(read_flags(context));
  if (pickup == 0) return NULL;
  const int bits[6] = {1, 2, 4, 8, 16, 32};
  int index = -1;
  for (int i = 0; i < 6; i++) if (((int32_t)pickup & bits[i]) != 0) { index = i; break; }
  char wanted[32];
  (void)snprintf(wanted, sizeof wanted, "start_%d", index + 1);
  qa_q1ad_actor *point = NULL;
  for (size_t i = 0; i < count; i++)
    if (strcmp(qa_q1ad_actor_text(entities[i], "netname"), wanted) == 0) { point = entities[i]; break; }
  if (point == NULL || point->classname == NULL || strcmp(point->classname, "info_player_start_hub") != 0) return NULL;
  write_flags(context, qa_q1ad_mg1_clear_last_sigil(read_flags(context)));
  return point;
}

void qa_q1ad_register_campaign_addons(qa_q1ad_context *context) {
  char prefix[QA_Q1AD_NAME], full[QA_Q1AD_NAME];
  prefix_of(context, prefix);
#define REG(name, handlers) do { qa_q1ad_named_name(full, prefix, name); \
  qa_q1ad_register_named(context, full, handlers); } while (0)
  REG("gib_monster", ((qa_q1ad_handlers){.action = gib_monster}));
  if (context->game.register_rule != NULL) {
    char rule[QA_Q1AD_NAME];
    (void)snprintf(rule, sizeof rule, "%s:campaign", qa_q1ad_program_name(context->program));
    context->game.register_rule(context->game.ctx, rule);
  }
  REG("sigil_touch", ((qa_q1ad_handlers){.touch = touch_sigil}));
  REG("drop_item", ((qa_q1ad_handlers){.action = drop_item}));
  REG("use_targets", ((qa_q1ad_handlers){.action = use_targets_action}));
  REG("indicator_use", ((qa_q1ad_handlers){.use = indicator_use}));
  REG("hub_touch", ((qa_q1ad_handlers){.touch = hub_touch}));
  REG("hub_changelevel", ((qa_q1ad_handlers){.action = hub_changelevel}));
  REG("electrode_touch", ((qa_q1ad_handlers){.touch = electrode_touch}));
  REG("egg_use", ((qa_q1ad_handlers){.use = egg_use}));
  REG("fix_pickup", ((qa_q1ad_handlers){.action = fix_pickup, .use = fix_pickup_use}));
#undef REG
  qa_q1ad_register_spawn(context, "item_sigil", true, spawn_sigil);
  qa_q1ad_register_spawn(context, "misc_rune_indicator", false, spawn_indicator);
  qa_q1ad_register_spawn(context, "func_bossgate", true, spawn_bossgate);
  qa_q1ad_register_spawn(context, "info_player_start_hub", false, spawn_nothing);
  qa_q1ad_register_spawn(context, "trigger_changelevel", true, spawn_changelevel);
  if (context->program != QA_Q1AD_MG3) {
    qa_q1ad_register_spawn(context, "hub_trigger_changelevel", false, spawn_hub_changelevel);
    qa_q1ad_register_spawn(context, "mge2m2_electrode_target", false, spawn_nothing);
    qa_q1ad_register_spawn(context, "mge2m2_electrode_button", false, spawn_electrode_button);
    qa_q1ad_register_spawn(context, "mge2m2_rune_egg_opener", false, spawn_egg_opener);
    qa_q1ad_register_spawn(context, "mge2m2_rune_pickup_fixer", false, spawn_pickup_fixer);
  }
}

