/* Port of src/content/q1/addons/commands.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/commands.h"

#include <string.h>

#include "qa/content/q1/addons/campaign.h"
#include "qa/foundation/support.h"

static bool cheat(qa_q1ad_context *context, qa_actor_id actor, const char *category) {
  return context->services.has_cheat_arsenal && context->services.cheat_arsenal != NULL &&
         context->services.cheat_arsenal(context->services.ctx, actor, category);
}
static void set_count(qa_q1ad_context *context, qa_q1ad_player *player, qa_item_id item,
                      double count, double capacity) {
  qa_q1ad_inventory_entry entries[32];
  size_t found = context->game.inventory_entries != NULL
      ? context->game.inventory_entries(context->game.ctx, player->actor.id, entries, 32) : 0;
  bool exists = false;
  for (size_t i = 0; i < found; i++) if (strcmp(entries[i].item, item) == 0) { exists = true; capacity = entries[i].capacity; break; }
  (void)exists;
  if (context->game.inventory_configure != NULL)
    context->game.inventory_configure(context->game.ctx, player->actor, item, count, capacity);
}
static void no_keys(qa_q1ad_context *context, qa_actor_id actor) {
  qa_q1ad_player *player = qa_q1ad_player_state(context, actor);
  if (player == NULL) qa_range_error("Addon command requires an admitted Q1 arsenal");
  if ((context->game.deathmatch != 0 || context->game.coop) && qa_q1ad_cvar(context, "sv_cheats") == 0) return;
  bool weapons = cheat(context, actor, "weapons");
  bool ammo = cheat(context, actor, "ammo");
  if (!ammo) {
    set_count(context, player, "q1:ammo/rockets", 100, 100);
    set_count(context, player, "q1:ammo/nails", 200, 200);
    set_count(context, player, "q1:ammo/shells", 100, 100);
    set_count(context, player, "q1:ammo/cells", 200, 100);
  }
  if (!weapons) {
    char buffer[64];
    for (int i = 0; i < QA_Q1AD_WEAPONS_COUNT; i++)
      set_count(context, player, qa_q1ad_weapon_item(QA_Q1AD_WEAPONS[i], buffer), 1, 1);
    if (context->game.select_weapon != NULL)
      (void)context->game.select_weapon(context->game.ctx, player->actor, "rocketlauncher");
  }
}

void qa_q1ad_omnicide(qa_q1ad_context *context, qa_actor_id actor) {
  qa_q1ad_actor *entities[512];
  size_t count = qa_q1ad_entities(context, entities, 512);
  for (size_t i = 0; i < count; i++) {
    qa_q1ad_actor *entity = entities[i];
    if (!qa_q1ad_live(context, entity) || (entity->movement_flags & (32 | 16384)) == 0) continue;
    const char *killtarget = entity->killtarget != NULL ? entity->killtarget : "";
    if ((entity->target != NULL && entity->target[0] != '\0') || killtarget[0] != '\0')
      qa_q1ad_use_targets(context, entity, &actor);
    if (context->program == QA_Q1AD_MG3 && entity->classname != NULL) {
      if (strcmp(entity->classname, "monster_oldone_new") == 0) {
        qa_q1ad_event music = {.kind = QA_Q1AD_EVENT_MUSIC, .track = 3, .loop_track = 3};
        qa_q1ad_emit(context, &music);
        qa_q1ad_host_event light = {.kind = "lightstyle", .style = 0, .pattern = "m"};
        qa_q1ad_host_emit(context, &light);
        qa_q1ad_do_action(context, entity, "mg3:bosses:oldnew_credits");
      } else if (strcmp(entity->classname, "monster_boss") == 0)
        qa_q1ad_do_action(context, entity, "mg3:bosses:boss_end");
    }
    if (qa_q1ad_live(context, entity)) qa_q1ad_remove(context, entity);
  }
  context->game.killed_monsters = context->game.total_monsters;
  qa_q1ad_event event = {.kind = QA_Q1AD_EVENT_MONSTER_COUNT, .count = context->game.killed_monsters};
  qa_q1ad_emit(context, &event);
}

static void cleanup_markers(qa_q1ad_context *context, const char *name) {
  qa_q1ad_actor *entities[512];
  size_t count = qa_q1ad_entities(context, entities, 512);
  for (size_t i = 0; i < count; i++)
    if (entities[i]->classname != NULL && strcmp(entities[i]->classname, name) == 0)
      qa_q1ad_remove(context, entities[i]);
}
static double read_flags(qa_q1ad_context *context) {
  return context->game.campaign_read_flags != NULL
      ? context->game.campaign_read_flags(context->game.ctx) : 0;
}
static void write_flags(qa_q1ad_context *context, double flags) {
  if (context->game.campaign_write_flags != NULL)
    context->game.campaign_write_flags(context->game.ctx, flags);
}

bool qa_q1ad_handle_impulse(qa_q1ad_context *context, qa_actor_id actor, double impulse) {
  if (context->program == QA_Q1AD_CTF) return false;
  if (impulse == 219) { qa_q1ad_omnicide(context, actor); return true; }
  if (context->program != QA_Q1AD_MG3) {
    if (impulse == 99) { no_keys(context, actor); return true; }
    return false;
  }
  int32_t flags = (int32_t)read_flags(context);
  if (impulse == 11) {
    const int runes[4] = {1, 2, 4, 8};
    for (int i = 0; i < 4; i++) if ((flags & runes[i]) == 0) { write_flags(context, flags | runes[i]); return true; }
    qa_q1ad_event event = {.kind = QA_Q1AD_EVENT_DEVELOPER_MESSAGE, .text = "already has all runes!\n"};
    qa_q1ad_emit(context, &event);
    return true;
  }
  if (impulse >= 101 && impulse <= 105) {
    write_flags(context, flags | (impulse == 105 ? 15 : 1 << (int)(impulse - 101)));
    return true;
  }
  if (impulse == 116 || impulse == 117) {
    const char *field = impulse == 116 ? "secrethunter" : "exithunter";
    bool enabled = qa_q1ad_player_number(context, actor, field) == 0;
    qa_q1ad_set_player_number(context, actor, field, enabled ? 1 : 0);
    if (!enabled) cleanup_markers(context, impulse == 116 ? "secret_marker" : "exit_marker");
    return true;
  }
  if (impulse == 119 || impulse == 121) {
    const char *field = impulse == 119 ? "monsterhunter" : "buddha";
    qa_q1ad_set_player_number(context, actor, field,
        qa_q1ad_player_number(context, actor, field) == 0 ? 1 : 0);
    return true;
  }
  if (impulse == 220) {
    qa_q1ad_actor *entity = qa_q1ad_entity(context, &actor);
    double effects = (double)((int32_t)(entity != NULL ? entity->effects
        : qa_q1ad_player_number(context, actor, "effects")) | 8);
    if (entity != NULL) entity->effects = effects;
    qa_q1ad_set_player_number(context, actor, "effects", effects);
    qa_q1ad_event event = {.kind = QA_Q1AD_EVENT_ACTOR_EFFECTS, .actor = actor,
                           .has_actor = true, .effects = effects};
    qa_q1ad_emit(context, &event);
    return true;
  }
  if (impulse == 222) {
    if (context->game.travel != NULL)
      context->game.travel(context->game.ctx, context->game.map_name, &actor);
    return true;
  }
  if (impulse == 223) {
    if ((flags & QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE) != 0)
      write_flags(context, flags & ~QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE);
    else {
      write_flags(context, flags | QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE | QA_Q1AD_BLOODY_NIGHTMARE_DISCOVERED);
      if (qa_q1ad_cvar(context, "skill") != 3) {
        if (context->services.set_cvar != NULL) context->services.set_cvar(context->services.ctx, "skill", "3");
        if (context->game.campaign_set_skill != NULL) context->game.campaign_set_skill(context->game.ctx, 3);
      }
    }
    return true;
  }
  if (impulse == 224) { write_flags(context, flags ^ QA_Q1AD_BLOODY_NIGHTMARE_NEWGAME); return true; }
  return false;
}

void qa_q1ad_frame_player(qa_q1ad_context *context, qa_actor_id actor, qa_vec3 view_offset) {
  if (context->program != QA_Q1AD_MG3) return;
  qa_q1ad_body body;
  if (!qa_q1ad_body_by_id(context, actor, &body)) return;
  qa_vec3 eye = qa_add3(body.origin, view_offset);
  static const struct { const char *field, *classname, *marker; } HUNTERS[] = {
    {"secrethunter", "trigger_secret", "secret_marker"},
    {"exithunter", "trigger_changelevel", "exit_marker"},
  };
  for (size_t h = 0; h < 2; h++) {
    if (qa_q1ad_player_number(context, actor, HUNTERS[h].field) == 0) continue;
    cleanup_markers(context, HUNTERS[h].marker);
    qa_q1ad_actor *entities[512];
    size_t count = qa_q1ad_entities(context, entities, 512);
    for (size_t i = 0; i < count; i++) {
      qa_q1ad_actor *entity = entities[i];
      if (entity->classname == NULL || strcmp(entity->classname, HUNTERS[h].classname) != 0) continue;
      qa_q1ad_body target_body = qa_q1ad_body_of(context, entity);
      qa_bounds bounds = entity->has_trigger_bounds ? entity->trigger_bounds : target_body.bounds;
      qa_vec3 middle = qa_add3(target_body.origin, qa_scale3(qa_add3(bounds.min, bounds.max), 0.5));
      if (context->game.create == NULL) continue;
      qa_q1ad_actor *marker = context->game.create(context->game.ctx, HUNTERS[h].marker);
      if (marker == NULL) continue;
      qa_q1ad_trace trace = {.fraction = 1, .end = middle};
      if (context->game.trace != NULL)
        (void)context->game.trace(context->game.ctx, eye, middle,
            (qa_bounds){{0, 0, 0}, {0, 0, 0}}, &actor, false, &trace);
      marker->model = "progs/s_bubble.spr";
      qa_vec3 direction = qa_normalize3_or_zero(qa_sub3(middle, eye));
      qa_q1ad_body_patch patch = {.has_origin = true,
          .origin = qa_sub3(trace.end, qa_scale3(direction, 4))};
      qa_q1ad_set_body(context, marker, &patch);
      qa_q1ad_link(context, marker);
    }
  }
  if (qa_q1ad_player_number(context, actor, "monsterhunter") != 0) {
    qa_q1ad_actor *entities[512];
    size_t count = qa_q1ad_entities(context, entities, 512);
    for (size_t i = 0; i < count; i++) {
      qa_q1ad_actor *entity = entities[i];
      qa_q1ad_body current = qa_q1ad_body_of(context, entity);
      if ((entity->movement_flags & 32) != 0 && qa_q1ad_health(context, entity->actor.id) > 0) {
        qa_q1ad_event event = {.kind = QA_Q1AD_EVENT_DEBUG_BOUNDS,
            .min = qa_add3(current.origin, current.bounds.min),
            .max = qa_add3(current.origin, current.bounds.max),
            .color = 251, .lifetime = 0, .depth_test = false};
        qa_q1ad_emit(context, &event);
      } else if (context->game.waiting_monster != NULL &&
                 context->game.waiting_monster(context->game.ctx, entity)) {
        qa_q1ad_event event = {.kind = QA_Q1AD_EVENT_DEBUG_BOUNDS,
            .min = qa_sub3(current.origin, (qa_vec3){16, 16, 16}),
            .max = qa_add3(current.origin, (qa_vec3){16, 16, 16}),
            .color = 244, .lifetime = 0, .depth_test = false};
        qa_q1ad_emit(context, &event);
      }
    }
  }
}
