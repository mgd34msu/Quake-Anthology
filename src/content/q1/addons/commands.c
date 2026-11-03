/* Port of src/content/q1/addons/commands.ts (quakec_{mg1,mg3}/weapons.qc and
 * quakec_mg3/client.qc). GPL-2.0-or-later. */
#include "qa/content/q1/addons/commands.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/campaign.h"
#include "qa/content/q1/addons/monsters/startup.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

static bool cheat_arsenal(qa_q1ad_context *context, qa_actor_id actor, const char *category) {
  /* context.services.cheatArsenal?.(actor, category) ?? false */
  return context->services.cheat_arsenal != NULL && context->services.cheat_arsenal(context->services.ctx, actor, category);
}
static void emit_addon(qa_q1ad_context *context, qa_q1ad_event event) {
  context->services.emit(context->services.ctx, &event);
}
static double cvar(qa_q1ad_context *context, const char *name) {
  return context->services.cvar(context->services.ctx, name);
}

static void set_count(qa_q1_services *game, qa_q1_player_state *player, qa_actor_id actor, qa_item_id item, double count,
                      double capacity) {
  size_t entry_count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(game->host.inventory, actor, &entry_count);
  qa_inventory_entry entry = {.item = item, .count = count, .capacity = capacity};
  for (size_t i = 0; i < entry_count; i++)
    if (strcmp(entries[i].item, item) == 0) { entry = entries[i]; entry.count = count; break; } /* { ...previous, count } */
  qa_shared_inventory_table_configure(game->host.inventory, player->actor, entry, NULL, NULL);
}

static void no_keys(qa_q1ad_context *context, qa_actor_id actor) {
  qa_q1_services *game = context->game;
  qa_q1_player_state *player = qa_q1_player(game, actor);
  if (player == NULL) qa_range_error("Addon command requires an admitted Q1 arsenal");
  if ((game->options.deathmatch != 0 || game->options.coop) && cvar(context, "sv_cheats") == 0) return;
  bool weapons = cheat_arsenal(context, actor, "weapons");
  bool ammo = cheat_arsenal(context, actor, "ammo");
  if (!ammo) {
    set_count(game, player, actor, "q1:ammo/rockets", 100, 100);
    set_count(game, player, actor, "q1:ammo/nails", 200, 200);
    set_count(game, player, actor, "q1:ammo/shells", 100, 100);
    set_count(game, player, actor, "q1:ammo/cells", 200, 100);
  }
  if (!weapons) {
    for (size_t i = 0; i < 8; i++) set_count(game, player, actor, qa_q1_services_weapon_item(game, QA_Q1_WEAPONS[i]), 1, 1);
    (void)qa_q1_select_weapon(game, player->actor, "rocketlauncher");
  }
}

static bool present(const qa_q1_services *game, const qa_q1_actor *entity) {
  for (size_t i = 0; i < game->entity_count; i++) if (game->entities[i] == entity) return true;
  return false;
}
static bool listed(qa_q1_actor *const *list, size_t count, const qa_q1_actor *entity) {
  for (size_t i = 0; i < count; i++) if (list[i] == entity) return true;
  return false;
}

static void omnicide_entity(qa_q1ad_context *context, qa_q1_actor *entity, qa_actor_id actor) {
  qa_q1_services *game = context->game;
  if (!qa_q1_live(game, entity) || (qa_js_to_int32(entity->movement_flags) & (32 | 16384)) == 0) return;
  if (entity->target[0] != '\0' || entity->killtarget[0] != '\0') qa_q1_use_targets(game, entity, &actor);
  if (context->program == QA_Q1AD_MG3) {
    if (strcmp(entity->classname, "monster_oldone_new") == 0) {
      emit_addon(context, (qa_q1ad_event){.kind = QA_Q1AD_EVENT_MUSIC, .track = 3, .loop_track = 3});
      qa_q1_emit(game, (qa_q1_event){.kind = "lightstyle", .style = 0, .pattern = "m"});
      qa_q1_call_action(game, entity, qa_q1_callback_registry_action(game->named, entity, "mg3:bosses:oldnew_credits"));
    } else if (strcmp(entity->classname, "monster_boss") == 0) {
      qa_q1_call_action(game, entity, qa_q1_callback_registry_action(game->named, entity, "mg3:bosses:boss_end"));
    }
  }
  if (qa_q1_live(game, entity)) qa_q1_remove(game, entity);
}

void qa_q1ad_omnicide(qa_q1ad_context *context, qa_actor_id actor) {
  qa_q1_services *game = context->game;
  /* for (const entity of game.entities.values()): the Map iterator skips entries
   * deleted before it reaches them and visits entries appended meanwhile. The
   * array splices on release and appends on create, and released structs are
   * kept until the game is freed, so pointers stay unique. */
  qa_q1_actor **visited = NULL;
  size_t visited_count = 0, visited_capacity = 0, i = 0;
  while (i < game->entity_count) {
    qa_q1_actor *current = game->entities[i];
    if (visited_count == visited_capacity) {
      visited_capacity = visited_capacity == 0 ? 64 : visited_capacity * 2;
      qa_q1_actor **grown = realloc(visited, visited_capacity * sizeof *grown);
      if (grown == NULL) qa_range_error("out of memory");
      visited = grown;
    }
    visited[visited_count++] = current;
    omnicide_entity(context, current, actor);
    /* Visited entries still present precede every unvisited one. */
    size_t k = i < game->entity_count ? i + 1 : game->entity_count;
    bool found = false;
    while (k > 0 && !found) found = game->entities[--k] == current;
    if (found) { i = k + 1; continue; }
    size_t j = i < game->entity_count ? i : game->entity_count;
    while (j > 0 && !listed(visited, visited_count, game->entities[j - 1])) j--;
    i = j;
  }
  free(visited);
  game->killed_monsters = game->total_monsters;
  emit_addon(context, (qa_q1ad_event){.kind = QA_Q1AD_EVENT_MONSTER_COUNT, .count = game->killed_monsters});
}

static void cleanup_markers(qa_q1ad_context *context, const char *name) {
  qa_q1_services *game = context->game;
  size_t count = game->entity_count;
  qa_q1_actor **snapshot = malloc((count != 0 ? count : 1) * sizeof *snapshot);
  if (snapshot == NULL) qa_range_error("out of memory");
  if (count != 0) memcpy(snapshot, game->entities, count * sizeof *snapshot);
  for (size_t i = 0; i < count; i++)
    if (present(game, snapshot[i]) && strcmp(snapshot[i]->classname, name) == 0) qa_q1_remove(game, snapshot[i]);
  free(snapshot);
}

static double read_flags(qa_q1ad_context *context) {
  qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
  return campaign.read_flags(campaign.ctx);
}
static void write_flags(qa_q1ad_context *context, int32_t flags) {
  qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
  campaign.write_flags(campaign.ctx, (double)flags);
}

/* The MG1/MG3 impulses their ImpulseCommands leaves open to every client, which
 * `sv_gate_dev_impulses` puts behind cheats. */
bool qa_q1ad_is_developer_impulse(qa_q1ad_program program, double impulse) {
  if (impulse == 219) return program == QA_Q1AD_MG1 || program == QA_Q1AD_MG3;
  return program == QA_Q1AD_MG3 &&
         ((impulse >= 100 && impulse <= 105) || (impulse >= 111 && impulse <= 122) || impulse == 222 || impulse == 223 ||
          impulse == 227 || impulse == 228);
}

bool qa_q1ad_handle_impulse(qa_q1ad_context *context, qa_actor_id actor, double impulse) {
  if (context->program == QA_Q1AD_CTF) return false;
  if (impulse == 219) { qa_q1ad_omnicide(context, actor); return true; }
  if (context->program != QA_Q1AD_MG3) {
    if (impulse == 99) { no_keys(context, actor); return true; }
    return false;
  }
  qa_q1_services *game = context->game;
  double raw = read_flags(context);
  int32_t flags = qa_js_to_int32(raw);
  if (impulse == 11) {
    for (int32_t rune = 1; rune <= 8; rune *= 2)
      if ((flags & rune) == 0) { write_flags(context, flags | rune); return true; }
    emit_addon(context, (qa_q1ad_event){.kind = QA_Q1AD_EVENT_DEVELOPER_MESSAGE, .text = "already has all runes!\n"});
    return true;
  }
  if (impulse >= 101 && impulse <= 105) {
    write_flags(context, flags | qa_js_to_int32(impulse == 105 ? 15 : qa_js_pow(2, impulse - 101)));
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
    qa_q1ad_set_player_number(context, actor, field, qa_q1ad_player_number(context, actor, field) == 0 ? 1 : 0);
    return true;
  }
  if (impulse == 220) {
    qa_q1_actor *entity = qa_q1_get_entity(game, &actor);
    double effects = (double)(qa_js_to_int32(entity != NULL ? entity->effects : qa_q1ad_player_number(context, actor, "effects")) | 8);
    if (entity != NULL) entity->effects = effects;
    qa_q1ad_set_player_number(context, actor, "effects", effects);
    emit_addon(context, (qa_q1ad_event){.kind = QA_Q1AD_EVENT_ACTOR_EFFECTS, .has_actor = true, .actor = actor, .effects = effects});
    return true;
  }
  if (impulse == 222) { qa_q1_travel(game, game->map_name != NULL ? game->map_name : "", &actor); return true; }
  if (impulse == 223) {
    if ((flags & QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE) != 0) {
      write_flags(context, flags & ~QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE);
    } else {
      write_flags(context, flags | QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE | QA_Q1AD_BLOODY_NIGHTMARE_DISCOVERED);
      if (cvar(context, "skill") != 3) {
        context->services.set_cvar(context->services.ctx, "skill", "3");
        qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
        campaign.set_skill(campaign.ctx, 3);
      }
    }
    return true;
  }
  if (impulse == 224) { write_flags(context, flags ^ QA_Q1AD_BLOODY_NIGHTMARE_NEWGAME); return true; }
  return false;
}

void qa_q1ad_frame_player(qa_q1ad_context *context, qa_actor_id actor, qa_vec3 view_offset) {
  if (context->program != QA_Q1AD_MG3) return;
  qa_q1_services *game = context->game;
  qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor);
  if (!body.has_value) return;
  qa_vec3 eye = qa_q1_vadd(body.value.origin, view_offset);
  static const struct { const char *field, *classname, *marker; } HUNTERS[] = {
    {"secrethunter", "trigger_secret", "secret_marker"},
    {"exithunter", "trigger_changelevel", "exit_marker"},
  };
  for (size_t h = 0; h < 2; h++) {
    if (qa_q1ad_player_number(context, actor, HUNTERS[h].field) == 0) continue;
    cleanup_markers(context, HUNTERS[h].marker);
    /* Markers created here are appended and never match the classname; the
     * live array visits them like the Map iterator does. */
    for (size_t i = 0; i < game->entity_count; i++) {
      qa_q1_actor *entity = game->entities[i];
      if (entity == NULL || strcmp(entity->classname, HUNTERS[h].classname) != 0) continue;
      qa_q1_body target_body = qa_q1_get_body(game, entity);
      qa_bounds bounds = entity->has_trigger_bounds ? entity->trigger_bounds : target_body.bounds;
      qa_vec3 middle = qa_q1_vadd(target_body.origin, qa_q1_vscale(qa_q1_vadd(bounds.min, bounds.max), 0.5));
      qa_q1_actor *marker = qa_q1_create(game, HUNTERS[h].marker, NULL, NULL);
      qa_q1_trace trace = qa_q1_host_trace(game, (qa_q1_trace_request){.start = eye, .end = middle, .bounds = QA_Q1_POINT,
                                                                       .has_ignore = true, .ignore = actor, .monsters = false});
      qa_q1ad_set_text(&marker->model, "progs/s_bubble.spr");
      qa_q1_body_patch patch = {.has_origin = true,
                                .origin = qa_q1_vsub(trace.end, qa_q1_vscale(qa_q1_normalize(qa_q1_vsub(middle, eye)), 4))};
      qa_q1_set_body(game, marker, &patch);
      qa_q1_link(game, marker);
    }
  }
  if (qa_q1ad_player_number(context, actor, "monsterhunter") != 0) {
    for (size_t i = 0; i < game->entity_count; i++) {
      qa_q1_actor *entity = game->entities[i];
      if (entity == NULL) continue;
      qa_q1_body current = qa_q1_get_body(game, entity);
      if ((qa_js_to_int32(entity->movement_flags) & 32) != 0 && qa_q1_health(game, entity->actor.id) > 0)
        emit_addon(context, (qa_q1ad_event){.kind = QA_Q1AD_EVENT_DEBUG_BOUNDS, .min = qa_q1_vadd(current.origin, current.bounds.min),
                                            .max = qa_q1_vadd(current.origin, current.bounds.max), .color = 251, .lifetime = 0,
                                            .depth_test = false});
      else if (qa_q1am_waiting_mg3_monster(game, entity))
        emit_addon(context, (qa_q1ad_event){.kind = QA_Q1AD_EVENT_DEBUG_BOUNDS, .min = qa_q1_vsub(current.origin, (qa_vec3){16, 16, 16}),
                                            .max = qa_q1_vadd(current.origin, (qa_vec3){16, 16, 16}), .color = 244, .lifetime = 0,
                                            .depth_test = false});
    }
  }
}
