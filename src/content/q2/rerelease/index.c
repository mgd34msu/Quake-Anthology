/* Port of src/content/q2/rerelease/index.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/index.h"
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/rerelease/debug_shapes.h"
#include "qa/content/q2/rerelease/spawns.h"
#include "rr_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static qa_q2rr_pickup_entry *picked_get(qa_q2_rerelease_module *m, qa_actor_id a) {
  for (size_t i = 0; i < m->picked_count; i++) if (qa_same_actor(m->picked_up_by[i].actor, a)) return &m->picked_up_by[i];
  return NULL;
}
static qa_q2rr_pickup_entry *picked_ensure(qa_q2_rerelease_module *m, qa_actor_id a) {
  qa_q2rr_pickup_entry *p = picked_get(m, a); if (p) return p;
  if (m->picked_count == m->picked_cap) { size_t nc = m->picked_cap ? m->picked_cap * 2 : 8; void *q = realloc(m->picked_up_by, nc * sizeof *m->picked_up_by); if (!q) return NULL; m->picked_up_by = q; m->picked_cap = nc; }
  qa_q2rr_pickup_entry *e = &m->picked_up_by[m->picked_count++];
  memset(e, 0, sizeof *e); e->actor = a; return e;
}
static bool slots_has(qa_q2rr_pickup_entry *p, double slot) {
  if (!p) return false;
  for (size_t i = 0; i < p->count; i++) if (p->slots[i] == slot) return true;
  return false;
}
static void slots_add(qa_q2rr_pickup_entry *p, double slot) {
  if (slots_has(p, slot)) return;
  if (p->count == p->cap) { size_t nc = p->cap ? p->cap * 2 : 4; void *q = realloc(p->slots, nc * sizeof *p->slots); if (!q) return; p->slots = q; p->cap = nc; }
  p->slots[p->count++] = slot;
}
static void picked_delete(qa_q2_rerelease_module *m, qa_actor_id a) {
  for (size_t i = 0; i < m->picked_count; i++) if (qa_same_actor(m->picked_up_by[i].actor, a)) {
    free(m->picked_up_by[i].slots);
    memmove(&m->picked_up_by[i], &m->picked_up_by[i + 1], (m->picked_count - i - 1) * sizeof *m->picked_up_by);
    m->picked_count--; return;
  }
}
static const char *msg_get(qa_q2_rerelease_module *m, qa_actor_id a) {
  for (size_t i = 0; i < m->msg_count; i++) if (qa_same_actor(m->pickup_messages[i].actor, a)) return m->pickup_messages[i].message;
  return NULL;
}
static void msg_set(qa_q2_rerelease_module *m, qa_actor_id a, const char *s) {
  for (size_t i = 0; i < m->msg_count; i++) if (qa_same_actor(m->pickup_messages[i].actor, a)) {
    free(m->pickup_messages[i].message); m->pickup_messages[i].message = qa_q2rr_dup(s ? s : ""); return;
  }
  if (m->msg_count == m->msg_cap) { size_t nc = m->msg_cap ? m->msg_cap * 2 : 8; void *q = realloc(m->pickup_messages, nc * sizeof *m->pickup_messages); if (!q) return; m->pickup_messages = q; m->msg_cap = nc; }
  m->pickup_messages[m->msg_count].actor = a;
  m->pickup_messages[m->msg_count].message = qa_q2rr_dup(s ? s : "");
  m->msg_count++;
}
static void msg_delete(qa_q2_rerelease_module *m, qa_actor_id a) {
  for (size_t i = 0; i < m->msg_count; i++) if (qa_same_actor(m->pickup_messages[i].actor, a)) {
    free(m->pickup_messages[i].message);
    memmove(&m->pickup_messages[i], &m->pickup_messages[i + 1], (m->msg_count - i - 1) * sizeof *m->pickup_messages);
    m->msg_count--; return;
  }
}

static void ext_admitted(void *c, qa_q2_entity *e, qa_q2_game_services *g) { qa_q2_rerelease_module_admitted(c, e, g); }
static void ext_end_frame(void *c, qa_q2_entity *e, qa_q2_game_services *g) { qa_q2_rerelease_module_end_player_frame(c, e, g); }
static void ext_spawned(void *c, qa_q2_entity *e, qa_q2_game_services *g) { qa_q2_rerelease_module_spawned(c, e, g); }
static void ext_before_change(void *c, qa_q2_game_services *g) { qa_q2_rerelease_module_before_level_change(c, g); }
static void ext_end_unit(void *c, qa_q2_game_services *g) { qa_q2_rerelease_module_end_of_unit(c, g); }
static void ext_leave(void *c) { qa_q2_rerelease_module_leave_unit(c); }
static void ext_begin_frame(void *c, qa_q2_entity *e, qa_q2_game_services *g) { qa_q2_rerelease_module_begin_player_frame(c, e, g); }
static void ext_help(void *c, qa_q2_entity *e, qa_q2_game_services *g) { qa_q2_rerelease_module_help(c, e, g); }
static void q64_end_unit(void *c, qa_q2_game_services *g) { qa_q2_rerelease_module_end_of_unit(c, g); }

static bool flashlight_pickup(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_owned_actor player) {
  (void)c; (void)e;
  return qa_shared_inventory_table_count(g->host->inventory, player.id, "q2:item_flashlight") == 0 &&
         qa_shared_inventory_table_give(g->host->inventory, player, "q2:item_flashlight", 1) > 0;
}
static bool flashlight_use(void *c, qa_owned_actor player, qa_q2_game_services *g) {
  qa_q2_rerelease_module *m = c;
  qa_q2_rerelease_entities_toggle_flashlight(&m->base, player.id, g,
      !qa_q2_rerelease_players_get_extra(m->base.players, player.id)->flashlight);
  return true;
}
static bool compass_pickup(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_owned_actor player) {
  (void)c; (void)e; (void)g; (void)player; return false;
}
static bool compass_use(void *c, qa_owned_actor player, qa_q2_game_services *g) {
  qa_q2_rerelease_module_use_compass(c, player.id, g); return true;
}

bool qa_q2_create_rerelease_module(qa_q2_rerelease_module *m, qa_q2_rerelease_module_options o) {
  memset(m, 0, sizeof *m);
  if (!qa_q2_rerelease_entities_init(&m->base, o.players, o.hooks, o.campaign)) return false;
  qa_q2_rerelease_triggers_init(&m->triggers, o.players, o.hooks);
  qa_q2_rerelease_q64_init(&m->q64, o.players, o.hooks, m, q64_end_unit);
  qa_q2_rerelease_lights_init(&m->lights, o.hooks);
  qa_q2_rerelease_goals_init(&m->goals, o.players, &m->base.campaign, o.hooks, m->base.poi_use);
  o.players->has_extension = true;
  o.players->extension = (qa_q2_rerelease_player_extension){m, ext_admitted, ext_end_frame, ext_spawned,
      ext_before_change, ext_end_unit, ext_leave, ext_begin_frame, ext_help};
  qa_q2_item_module *items = qa_q2_players_items(o.players->base);
  qa_q2_item_module_set_pickup_policy(items, qa_q2_rerelease_module_as_pickup_policy(m));
  qa_q2_item_definition invis = {0};
  invis.kind = "power"; invis.classname = "item_invisibility"; invis.model = "models/items/cloaker/tris.md2";
  invis.icon = "p_cloaker"; invis.name = "Invisibility"; invis.sound = "items/pkup.wav";
  invis.rotate = true; invis.respawn = 300; invis.has_coop_stay = true; invis.coop_stay = false;
  qa_q2_item_module_register(items, invis);
  qa_q2_item_definition torch = {0};
  torch.kind = "custom"; torch.classname = "item_flashlight"; torch.model = "models/items/flashlight/tris.md2";
  torch.icon = "p_torch"; torch.name = "Flashlight"; torch.sound = "items/pkup.wav";
  torch.rotate = true; torch.respawn = 0; torch.capacity = 1; torch.quantity = 1;
  torch.has_coop_stay = true; torch.coop_stay = true; torch.droppable = false;
  torch.custom_ctx = m; torch.pickup = flashlight_pickup; torch.use = flashlight_use; torch.has_use = true;
  qa_q2_item_module_register(items, torch);
  qa_q2_item_definition compass = {0};
  compass.kind = "custom"; compass.console_give = "inventory-only"; compass.classname = "item_compass";
  compass.model = ""; compass.icon = "p_compass"; compass.name = "Compass"; compass.sound = "";
  compass.rotate = false; compass.respawn = 0; compass.capacity = 1; compass.quantity = 0;
  compass.has_coop_stay = true; compass.coop_stay = true; compass.droppable = false;
  compass.custom_ctx = m; compass.pickup = compass_pickup; compass.use = compass_use; compass.has_use = true;
  qa_q2_item_module_register(items, compass);
  return true;
}
static void free_capture_scratch(qa_q2_rerelease_module *m) {
  free(m->cp_bars); free(m->cp_ht); free(m->cp_picked); free(m->cp_sounds); free(m->cp_lights);
  if (m->cp_slot_arrays) { for (size_t i = 0; i < m->picked_count; i++) free(m->cp_slot_arrays[i]); free(m->cp_slot_arrays); }
  if (m->cp_levels) qa_q2_rerelease_unit_report_free(m->cp_levels, m->base.campaign.level_count);
  free(m->cp_visited);
  m->cp_bars = NULL; m->cp_ht = NULL; m->cp_picked = NULL; m->cp_slot_arrays = NULL;
  m->cp_sounds = NULL; m->cp_lights = NULL; m->cp_levels = NULL; m->cp_visited = NULL;
}
void qa_q2_rerelease_module_destroy(qa_q2_rerelease_module *m) {
  if (m->release_bound) qa_session_actor_registry_off_release(m->release_registry, m->release_subscription);
  m->release_bound = false;
  free_capture_scratch(m);
  for (size_t i = 0; i < m->picked_count; i++) free(m->picked_up_by[i].slots);
  free(m->picked_up_by);
  for (size_t i = 0; i < m->msg_count; i++) free(m->pickup_messages[i].message);
  free(m->pickup_messages);
  qa_q2_rerelease_triggers_destroy(&m->triggers);
  qa_q2_rerelease_q64_destroy(&m->q64);
  qa_q2_rerelease_lights_destroy(&m->lights);
  qa_q2_rerelease_goals_destroy(&m->goals);
  qa_q2_rerelease_entities_destroy(&m->base);
  memset(m, 0, sizeof *m);
}
void qa_q2_rerelease_module_draw_debug_shape(qa_q2_rerelease_module *m, qa_debug_shape shape, qa_vec4 color,
                                             double lifetime_seconds, bool depth_test,
                                             qa_debug_line *lines, size_t capacity) {
  qa_q2_rerelease_event ev;
  qa_q2_debug_shape(shape, color, lifetime_seconds, depth_test, &ev, lines, capacity);
  rr_emit_rr(&m->base.hooks, ev);
}
bool qa_q2_rerelease_module_use_powerup(qa_q2_rerelease_module *m, qa_owned_actor player, qa_item_id item, qa_q2_game_services *g) {
  if (item && strcmp(item, "q2:item_invisibility") == 0) {
    qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(m->base.players, player.id);
    double base = extra->invisibility_until > rr_now(g) ? extra->invisibility_until : rr_now(g);
    extra->invisibility_until = base + 30;
    qa_q2_entity *e = rr_ent(g, &player.id);
    if (e) rr_sound_ent(g, e, "items/protect.wav", 3, 1, 1);
    return true;
  }
  if (item && strcmp(item, "q2:item_adrenaline") == 0) {
    qa_q2_entity *e = rr_ent(g, &player.id);
    if (!e) qa_q2rr_error("Rerelease adrenaline requires a source player");
    if (g->options.mode != QA_Q2_MODE_DEATHMATCH) e->max_health++;
    if (rr_health(g, player.id) < e->max_health) qa_gameplay_authority_set_health(g->host->combat, player, e->max_health);
    rr_sound_ent(g, e, "items/n_health.wav", 3, 1, 1);
    return true;
  }
  return false;
}
static void module_on_release(void *c, qa_owned_actor actor) {
  qa_q2_rerelease_module *m = c;
  picked_delete(m, actor.id); msg_delete(m, actor.id);
  qa_q2rr_health_target_delete(&m->base, actor.id);
  qa_q2rr_sound_time_delete(&m->triggers, actor.id);
  qa_q2_rerelease_q64_release(&m->q64, actor.id);
  qa_q2_rerelease_lights_delete(&m->lights, actor.id);
  if (m->base.poi.has && m->base.poi.has_dynamic && qa_same_actor(m->base.poi.dynamic, actor.id))
    m->base.poi.has_dynamic = false;
}
bool qa_q2_rerelease_module_spawn(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g) {
  if (!m->release_bound) {
    m->release_bound = true;
    m->release_registry = g->host->actors;
    m->release_subscription = qa_session_actor_registry_on_release(g->host->actors, module_on_release, m);
  }
  const char *color = qa_q2_spawn_get(&e->spawn, "rgba");
  if (!color) color = qa_q2_spawn_get(&e->spawn, "_color");
  if (color) e->skin = qa_q2_rerelease_color(color);
  return qa_q2_rerelease_q64_spawn(&m->q64, e, g) || qa_q2_rerelease_lights_spawn(&m->lights, e, g) ||
         qa_q2_rerelease_goals_spawn(&m->goals, e, g) || qa_q2_rerelease_triggers_spawn(&m->triggers, e, g) ||
         qa_q2_rerelease_entities_spawn(&m->base, e, g);
}
qa_q2_callback_definitions qa_q2_rerelease_module_callbacks(qa_q2_rerelease_module *m) {
  qa_q2_callback_definitions parts[5] = {
    qa_q2_rerelease_entities_callbacks(&m->base),
    qa_q2_rerelease_triggers_callbacks(&m->triggers),
    qa_q2_rerelease_q64_callbacks(&m->q64),
    qa_q2_rerelease_lights_callbacks(&m->lights),
    qa_q2_rerelease_goals_callbacks(&m->goals),
  };
  size_t tn = 0, un = 0, cn = 0;
  for (int p = 0; p < 5; p++) {
    for (size_t i = 0; i < parts[p].think_count && tn < 64; i++) m->cb_thinks[tn++] = parts[p].think[i];
    for (size_t i = 0; i < parts[p].use_count && un < 64; i++) m->cb_uses[un++] = parts[p].use[i];
  }
  /* touch: base + triggers only (TS merge) */
  for (size_t i = 0; i < parts[0].touch_count && cn < 64; i++) m->cb_touches[cn++] = parts[0].touch[i];
  for (size_t i = 0; i < parts[1].touch_count && cn < 64; i++) m->cb_touches[cn++] = parts[1].touch[i];
  qa_q2_callback_definitions d = {0};
  d.think = m->cb_thinks; d.think_count = tn; d.touch = m->cb_touches; d.touch_count = cn; d.use = m->cb_uses; d.use_count = un;
  return d;
}
static qa_saved_actor_id saved_of(qa_actor_id a) { return (qa_saved_actor_id){(double)a.slot, (double)a.generation}; }
qa_q2_rerelease_module_checkpoint qa_q2_rerelease_module_capture(qa_q2_rerelease_module *m) {
  free_capture_scratch(m);
  qa_q2_rerelease_module_checkpoint cp; memset(&cp, 0, sizeof cp);
  cp.version = 1; cp.world_fog = m->base.world_fog; cp.story = m->base.story;
  cp.sky = (qa_q2rr_sky){m->base.sky.name, m->base.sky.rotation, m->base.sky.auto_rotate, m->base.sky.axis};
  cp.has_poi = m->base.poi.has;
  if (m->base.poi.has) {
    cp.poi.actor = saved_of(m->base.poi.actor); cp.poi.origin = m->base.poi.origin; cp.poi.image = m->base.poi.image;
    if (m->base.poi.has_dynamic) { qa_saved_actor_id s; cp.poi.has_dynamic = qa_q2_save_actor(&m->base.poi.dynamic, &s); cp.poi.dynamic = s; }
  }
  cp.poi_stage = m->base.poi_stage; cp.last_auto_save = m->base.last_auto_save;
  cp.campaign.cross_unit_flags = m->base.campaign.cross_unit_flags;
  m->cp_visited = m->base.campaign.visited_count ? calloc(m->base.campaign.visited_count, sizeof(char *)) : NULL;
  for (size_t i = 0; i < m->base.campaign.visited_count; i++) m->cp_visited[i] = m->base.campaign.visited_maps[i];
  cp.campaign.visited_maps = m->cp_visited; cp.campaign.visited_count = m->base.campaign.visited_count;
  m->cp_levels = m->base.campaign.level_count ? calloc(m->base.campaign.level_count, sizeof *m->cp_levels) : NULL;
  for (size_t i = 0; i < m->base.campaign.level_count; i++) {
    m->cp_levels[i] = m->base.campaign.levels[i];
    m->cp_levels[i].map = qa_q2rr_dup(m->base.campaign.levels[i].map);
    m->cp_levels[i].name = qa_q2rr_dup(m->base.campaign.levels[i].name);
  }
  cp.campaign.levels = m->cp_levels; cp.campaign.level_count = m->base.campaign.level_count;
  cp.campaign.mission = m->base.campaign.mission; /* borrowed strings */
  m->cp_bars = calloc(2, sizeof *m->cp_bars);
  for (int i = 0; i < 2; i++) if (m->base.health_bars[i].has) {
    m->cp_bars[i].has = true;
    m->cp_bars[i].controller = saved_of(m->base.health_bars[i].controller);
    m->cp_bars[i].target = saved_of(m->base.health_bars[i].target);
    m->cp_bars[i].has_dead_until = m->base.health_bars[i].has_dead_until;
    m->cp_bars[i].dead_until = m->base.health_bars[i].dead_until;
  }
  cp.health_bars = m->cp_bars; cp.health_bar_count = 2;
  m->cp_ht = m->base.ht_count ? malloc(m->base.ht_count * sizeof *m->cp_ht) : NULL;
  for (size_t i = 0; i < m->base.ht_count; i++) {
    m->cp_ht[i].controller = saved_of(m->base.health_targets[i].key);
    m->cp_ht[i].target = saved_of(m->base.health_targets[i].value);
  }
  cp.health_targets = m->cp_ht; cp.health_target_count = m->base.ht_count;
  m->cp_picked = m->picked_count ? calloc(m->picked_count, sizeof *m->cp_picked) : NULL;
  m->cp_slot_arrays = m->picked_count ? calloc(m->picked_count, sizeof(double *)) : NULL;
  for (size_t i = 0; i < m->picked_count; i++) {
    m->cp_picked[i].actor = saved_of(m->picked_up_by[i].actor);
    if (m->picked_up_by[i].count) {
      m->cp_slot_arrays[i] = malloc(m->picked_up_by[i].count * sizeof(double));
      memcpy(m->cp_slot_arrays[i], m->picked_up_by[i].slots, m->picked_up_by[i].count * sizeof(double));
    }
    m->cp_picked[i].slots = m->cp_slot_arrays[i]; m->cp_picked[i].slot_count = m->picked_up_by[i].count;
  }
  cp.picked_up_by = m->cp_picked; cp.picked_count = m->picked_count;
  m->cp_sounds = m->triggers.count ? malloc(m->triggers.count * sizeof *m->cp_sounds) : NULL;
  for (size_t i = 0; i < m->triggers.count; i++) {
    m->cp_sounds[i].actor = saved_of(m->triggers.sound_times[i].actor);
    m->cp_sounds[i].time = m->triggers.sound_times[i].time;
  }
  cp.trigger_sound_times = m->cp_sounds; cp.trigger_sound_count = m->triggers.count;
  cp.q64 = qa_q2_rerelease_q64_capture(&m->q64);
  m->cp_lights = m->lights.count ? malloc(m->lights.count * sizeof *m->cp_lights) : NULL;
  for (size_t i = 0; i < m->lights.count; i++) {
    m->cp_lights[i].actor = saved_of(m->lights.active[i].actor);
    m->cp_lights[i].active = m->lights.active[i].active;
  }
  cp.lights = m->cp_lights; cp.light_count = m->lights.count;
  cp.goals = qa_q2_rerelease_goals_capture(&m->goals);
  return cp;
}
void qa_q2_rerelease_module_restore(qa_q2_rerelease_module *m, qa_q2_game_services *g, qa_q2_rerelease_module_checkpoint cp) {
  qa_q2_rerelease_entities * e2;
  qa_actor_id k;
  qa_actor_id v;
  m->base.world_fog = cp.world_fog;
  free(m->base.story); m->base.story = qa_q2rr_dup(cp.story ? cp.story : "");
  free(m->base.sky.name); m->base.sky.name = qa_q2rr_dup(cp.sky.name ? cp.sky.name : "");
  m->base.sky.rotation = cp.sky.rotation; m->base.sky.auto_rotate = cp.sky.auto_rotate; m->base.sky.axis = cp.sky.axis;
  free(m->base.poi.image); m->base.poi.image = NULL;
  m->base.poi.has = cp.has_poi;
  if (cp.has_poi) {
    m->base.poi.actor = qa_session_actor_registry_reference_saved(g->host->actors, cp.poi.actor, "checkpoint");
    m->base.poi.origin = cp.poi.origin; m->base.poi.image = qa_q2rr_dup(cp.poi.image ? cp.poi.image : "");
    m->base.poi.has_dynamic = cp.poi.has_dynamic;
    if (cp.poi.has_dynamic) m->base.poi.dynamic = qa_session_actor_registry_reference_saved(g->host->actors, cp.poi.dynamic, "checkpoint");
  }
  m->base.poi_stage = cp.poi_stage; m->base.last_auto_save = cp.last_auto_save;
  qa_q2_rerelease_campaign_state *camp = &m->base.campaign;
  camp->cross_unit_flags = cp.campaign.cross_unit_flags;
  for (size_t i = 0; i < camp->visited_count; i++) free(camp->visited_maps[i]);
  camp->visited_count = 0;
  for (size_t i = 0; i < cp.campaign.visited_count; i++) {
    if (camp->visited_count == camp->visited_cap) { size_t nc = camp->visited_cap ? camp->visited_cap * 2 : 8; void *q = realloc(camp->visited_maps, nc * sizeof(char *)); if (!q) break; camp->visited_maps = q; camp->visited_cap = nc; }
    camp->visited_maps[camp->visited_count++] = qa_q2rr_dup(cp.campaign.visited_maps[i]);
  }
  for (size_t i = 0; i < camp->level_count; i++) { free(camp->levels[i].map); free(camp->levels[i].name); }
  camp->level_count = 0;
  for (size_t i = 0; i < cp.campaign.level_count; i++) {
    if (camp->level_count == camp->level_cap) { size_t nc = camp->level_cap ? camp->level_cap * 2 : 8; void *q = realloc(camp->levels, nc * sizeof *camp->levels); if (!q) break; camp->levels = q; camp->level_cap = nc; }
    camp->levels[camp->level_count] = cp.campaign.levels[i];
    camp->levels[camp->level_count].map = qa_q2rr_dup(cp.campaign.levels[i].map);
    camp->levels[camp->level_count].name = qa_q2rr_dup(cp.campaign.levels[i].name);
    camp->level_count++;
  }
  free(camp->mission.primary); free(camp->mission.secondary);
  camp->mission = cp.campaign.mission;
  camp->mission.primary = qa_q2rr_dup(cp.campaign.mission.primary ? cp.campaign.mission.primary : "");
  camp->mission.secondary = qa_q2rr_dup(cp.campaign.mission.secondary ? cp.campaign.mission.secondary : "");
  for (int i = 0; i < 2; i++) {
    m->base.health_bars[i].has = false;
    if ((size_t)i < cp.health_bar_count && cp.health_bars[i].has) {
      m->base.health_bars[i].has = true;
      m->base.health_bars[i].controller = qa_session_actor_registry_reference_saved(g->host->actors, cp.health_bars[i].controller, "checkpoint");
      m->base.health_bars[i].target = qa_session_actor_registry_reference_saved(g->host->actors, cp.health_bars[i].target, "checkpoint");
      m->base.health_bars[i].has_dead_until = cp.health_bars[i].has_dead_until;
      m->base.health_bars[i].dead_until = cp.health_bars[i].dead_until;
    }
  }
  qa_q2rr_health_target_clear(&m->base);
  for (size_t i = 0; i < m->picked_count; i++) free(m->picked_up_by[i].slots);
  m->picked_count = 0;
  for (size_t i = 0; i < m->msg_count; i++) free(m->pickup_messages[i].message);
  m->msg_count = 0;
  for (size_t i = 0; i < cp.health_target_count; i++)
    (e2 = &m->base, k = qa_q2_restore_actor(g, cp.health_targets[i].controller).id, v = qa_session_actor_registry_reference_saved(g->host->actors, cp.health_targets[i].target, "checkpoint"), qa_q2rr_health_target_set(e2, k,
                              v));
  for (size_t i = 0; i < cp.picked_count; i++) {
    qa_q2rr_pickup_entry *e = picked_ensure(m, qa_q2_restore_actor(g, cp.picked_up_by[i].actor).id);
    if (e) for (size_t j = 0; j < cp.picked_up_by[i].slot_count; j++) slots_add(e, cp.picked_up_by[i].slots[j]);
  }
  qa_q2rr_sound_time_clear(&m->triggers);
  for (size_t i = 0; i < cp.trigger_sound_count; i++)
    qa_q2rr_sound_time_set(&m->triggers, qa_q2_restore_actor(g, cp.trigger_sound_times[i].actor).id, cp.trigger_sound_times[i].time);
  qa_q2_rerelease_q64_restore(&m->q64, g, cp.q64);
  qa_q2_rerelease_lights_clear(&m->lights);
  for (size_t i = 0; i < cp.light_count; i++)
    qa_q2_rerelease_lights_set_active(&m->lights, qa_q2_restore_actor(g, cp.lights[i].actor).id, cp.lights[i].active);
  qa_q2_rerelease_goals_restore(&m->goals, cp.goals);
  m->goals.campaign = &m->base.campaign;
  for (size_t i = 0; i < qa_q2_players_state_count(m->base.players->base); i++) {
    qa_actor_id actor; qa_q2_base_player_state *state;
    if (!qa_q2_players_state_at(m->base.players->base, i, &actor, &state)) break;
    double slot = state->slot;
    if (!(g->options.mode == QA_Q2_MODE_COOP && qa_q2_uses_instanced_items(m->base.players->rerelease_options))) continue;
    for (size_t j = 0; j < m->picked_count; j++) if (slots_has(&m->picked_up_by[j], slot)) {
      qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
      ev.kind = "item-visibility"; ev.has_actor = true; ev.actor = actor;
      ev.has_item = true; ev.item = m->picked_up_by[j].actor; ev.visible = false;
      rr_emit_rr(&m->base.hooks, ev);
    }
  }
}
void qa_q2_rerelease_module_restore_campaign(qa_q2_rerelease_module *m, const qa_q2_rerelease_campaign_state *camp) {
  qa_q2_rerelease_campaign_state copy;
  if (!qa_q2_rerelease_campaign_clone(&copy, camp)) return;
  qa_q2_rerelease_campaign_state *cur = &m->base.campaign;
  cur->cross_unit_flags = copy.cross_unit_flags;
  for (size_t i = 0; i < cur->visited_count; i++) free(cur->visited_maps[i]);
  free(cur->visited_maps);
  cur->visited_maps = copy.visited_maps; cur->visited_count = copy.visited_count; cur->visited_cap = copy.visited_cap;
  copy.visited_maps = NULL; copy.visited_count = copy.visited_cap = 0;
  for (size_t i = 0; i < cur->level_count; i++) { free(cur->levels[i].map); free(cur->levels[i].name); }
  free(cur->levels);
  cur->levels = copy.levels; cur->level_count = copy.level_count; cur->level_cap = copy.level_cap;
  copy.levels = NULL; copy.level_count = copy.level_cap = 0;
  free(cur->mission.primary); free(cur->mission.secondary);
  cur->mission = copy.mission;
  copy.mission.primary = NULL; copy.mission.secondary = NULL;
  qa_q2_rerelease_campaign_destroy(&copy);
  m->goals.campaign = &m->base.campaign;
}
static bool module_instanced(qa_q2_rerelease_module *m, qa_q2_game_services *g) {
  return g->options.mode == QA_Q2_MODE_COOP && qa_q2_uses_instanced_items(m->base.players->rerelease_options);
}
void qa_q2_rerelease_module_admitted(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_enter_rerelease_level(g, m->base.players, &m->base.campaign);
  qa_q2_base_player_state *st = qa_q2_players_context(m->base.players->base, e, g).state;
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(m->base.players, e->actor.id);
  if (module_instanced(m, g))
    for (size_t i = 0; i < m->picked_count; i++) if (slots_has(&m->picked_up_by[i], st->slot)) {
      qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
      ev.kind = "item-visibility"; ev.has_actor = true; ev.actor = e->actor.id;
      ev.has_item = true; ev.item = m->picked_up_by[i].actor; ev.visible = false;
      rr_emit_rr(&m->base.hooks, ev);
    }
  extra->spawned = !extra->awaiting_respawn;
  extra->wanted_fog = m->base.world_fog;
  qa_q2_rerelease_entities_force_fog(&m->base, e->actor.id, true);
  if (st->use_q2_inventory && g->options.mode != QA_Q2_MODE_DEATHMATCH) {
    qa_inventory_entry en; memset(&en, 0, sizeof en); en.item = "q2:item_compass"; en.count = 1; en.capacity = 1;
    qa_shared_inventory_table_configure(g->host->inventory, e->actor, en, NULL, NULL);
  }
  if (g->options.mode == QA_Q2_MODE_COOP && st->use_q2_inventory) {
    size_t pn = 0; const qa_actor_id *pls = g->host->players(g->host->ctx, &pn);
    for (size_t i = 0; i < pn; i++) {
      if (qa_same_actor(pls[i], e->actor.id)) continue;
      qa_q2_base_player_state *other = qa_q2_players_get_state(m->base.players->base, pls[i]);
      qa_q2_entity *source = rr_ent(g, &pls[i]);
      if (!other || !source || other->spectator || other->noclip) continue;
      size_t n = 0; const qa_inventory_entry *entries = qa_shared_inventory_table_entries(g->host->inventory, pls[i], &n);
      qa_inventory_entry *copy = NULL;
      if (n) { copy = malloc(n * sizeof *copy); memcpy(copy, entries, n * sizeof *copy); }
      for (size_t j = 0; j < n; j++) qa_shared_inventory_table_configure(g->host->inventory, e->actor, copy[j], NULL, NULL);
      free(copy);
      e->power_cubes = source->power_cubes;
      break;
    }
  }
}
void qa_q2_rerelease_module_after_player_frames(qa_q2_rerelease_module *m, qa_q2_game_services *g) {
  qa_q2_rerelease_level_entry *entry = qa_q2_rerelease_campaign_get_level(&m->base.campaign, g->options.map_name);
  qa_q2_base_player_state *host = NULL;
  for (size_t i = 0; i < qa_q2_players_state_count(m->base.players->base); i++) {
    qa_actor_id actor; qa_q2_base_player_state *state;
    if (qa_q2_players_state_at(m->base.players->base, i, &actor, &state) && state->slot == 0) { host = state; break; }
  }
  if (entry && strcmp(qa_q2_players_intermission(m->base.players->base).kind, "playing") == 0 && host && host->connected) entry->time += rr_frame(g);
}
void qa_q2_rerelease_module_before_level_change(qa_q2_rerelease_module *m, qa_q2_game_services *g) {
  qa_q2_update_rerelease_level(g, &m->base.campaign);
}
void qa_q2_rerelease_module_leave_unit(qa_q2_rerelease_module *m) {
  for (size_t i = 0; i < m->base.campaign.level_count; i++) { free(m->base.campaign.levels[i].map); free(m->base.campaign.levels[i].name); }
  m->base.campaign.level_count = 0;
  if (m->base.players->rerelease_options.coop_lives)
    for (size_t i = 0; i < m->base.players->extra_count; i++)
      m->base.players->rerelease_states[i].state->lives = m->base.players->rerelease_options.coop_num_lives + 1;
}
void qa_q2_rerelease_module_spawned(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(m->base.players, e->actor.id);
  extra->wanted_fog = m->base.world_fog;
  extra->spawned = !extra->awaiting_respawn;
  qa_q2_rerelease_entities_force_fog(&m->base, e->actor.id, true);
  qa_q2_rerelease_players_emit_flashlight(m->base.players, e->actor.id, g);
}
void qa_q2_rerelease_module_end_of_unit(qa_q2_rerelease_module *m, qa_q2_game_services *g) {
  qa_q2_rerelease_entities_end_of_unit(&m->base, g);
}
bool qa_q2_rerelease_module_instanced_coop(qa_q2_rerelease_module *m, qa_q2_game_services *g) { return module_instanced(m, g); }
bool qa_q2_rerelease_module_can_pickup(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id player) {
  qa_q2_base_player_state *st = qa_q2_players_get_state(m->base.players->base, player);
  if (!st) return false;
  return !module_instanced(m, g) || !slots_has(picked_get(m, e->actor.id), st->slot);
}
bool qa_q2_rerelease_module_before_pickup(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id player) {
  if (!qa_q2_rerelease_module_can_pickup(m, e, g, player)) return false;
  if (module_instanced(m, g) || g->options.mode == QA_Q2_MODE_DEATHMATCH) {
    msg_set(m, e->actor.id, e->message);
    e->message = "";
  }
  return true;
}
void qa_q2_rerelease_module_after_pickup(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id player, bool taken) {
  (void)g; (void)player; (void)taken;
  const char *msg = msg_get(m, e->actor.id);
  if (msg) {
    /* restore borrowed message: leak-free copy retained in map until delete */
    static char ring[16][512]; static size_t ri;
    char *slot = ring[ri++ % 16]; snprintf(slot, 512, "%s", msg);
    e->message = slot;
    msg_delete(m, e->actor.id);
  }
}
void qa_q2_rerelease_module_before_targets(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id player, bool taken) {
  qa_q2_base_player_state *st = qa_q2_players_get_state(m->base.players->base, player);
  if (!st || !taken) return;
  st->bonus_alpha = 0.25;
  qa_q2_inventory_item desc;
  if (qa_q2_item_module_lookup(qa_q2_players_items(m->base.players->base), e->classname ? e->classname : "", &desc) && desc.usable &&
      qa_shared_inventory_table_count(g->host->inventory, player, desc.id) != 0)
    st->selected_item = desc.id;
  if (module_instanced(m, g)) {
    qa_q2rr_pickup_entry *picked = picked_ensure(m, e->actor.id);
    if (picked) slots_add(picked, st->slot);
    qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
    ev.kind = "item-visibility"; ev.has_actor = true; ev.actor = player;
    ev.has_item = true; ev.item = e->actor.id; ev.visible = false;
    rr_emit_rr(&m->base.hooks, ev);
    const char *msg = msg_get(m, e->actor.id);
    if (!msg) msg = e->message;
    if (msg && msg[0]) {
      qa_q2_presentation_event pev; memset(&pev, 0, sizeof pev);
      pev.kind = "centerprint"; pev.has_actor = true; pev.actor = player; pev.text = msg;
      rr_emit_host(g, pev);
    }
  }
}
bool qa_q2_rerelease_module_keep_after_pickup(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id player) {
  (void)player;
  return module_instanced(m, g) && rr_band(e->spawnflags, 0x20000) == 0;
}
static bool pol_instanced(void *c, qa_q2_game_services *g) { return qa_q2_rerelease_module_instanced_coop(c, g); }
static bool pol_can(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id p) { return qa_q2_rerelease_module_can_pickup(c, e, g, p); }
static bool pol_before(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id p) { return qa_q2_rerelease_module_before_pickup(c, e, g, p); }
static void pol_before_t(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id p, bool t) { qa_q2_rerelease_module_before_targets(c, e, g, p, t); }
static void pol_after(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id p, bool t) { qa_q2_rerelease_module_after_pickup(c, e, g, p, t); }
static bool pol_keep(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id p) { return qa_q2_rerelease_module_keep_after_pickup(c, e, g, p); }
qa_q2_pickup_policy qa_q2_rerelease_module_as_pickup_policy(qa_q2_rerelease_module *m) {
  qa_q2_pickup_policy p = {m, pol_instanced, pol_can, pol_before, pol_before_t, pol_after, pol_keep};
  return p;
}
static bool mod_spawn_cb(void *c, qa_q2_entity *e, qa_q2_game_services *g) { return qa_q2_rerelease_module_spawn(c, e, g); }
qa_q2_spawn_module qa_q2_rerelease_module_as_spawn(qa_q2_rerelease_module *m) {
  qa_q2_spawn_module mod = {0}; mod.ctx = m; mod.spawn = mod_spawn_cb; return mod;
}
void qa_q2_rerelease_module_resume_presentation(qa_q2_rerelease_module *m, qa_q2_game_services *g, const qa_actor_id *only) {
  double now = rr_now(g);
  qa_q2rr_mission *mission = &m->base.campaign.mission;
  for (size_t i = 0; i < m->base.players->extra_count; i++) {
    qa_actor_id actor = m->base.players->rerelease_states[i].actor;
    qa_q2rr_player_extra_state *extra = m->base.players->rerelease_states[i].state;
    if (only && !qa_same_actor(actor, *only)) continue;
    qa_q2_base_player_state *st = qa_q2_players_get_state(m->base.players->base, actor);
    if (!st || !st->connected) continue;
    qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
    ev.kind = "help-computer"; ev.has_actor = true; ev.actor = actor; ev.visible = st->show_help;
    ev.primary = mission->primary; ev.secondary = mission->secondary; ev.slow_time = st->show_help;
    rr_emit_rr(&m->base.hooks, ev);
    if (extra->help_marker_until > now) {
      qa_q2_rerelease_event pev; memset(&pev, 0, sizeof pev);
      pev.kind = "poi"; pev.has_actor = true; pev.actor = actor; pev.position = extra->help_location;
      pev.image = extra->help_image; pev.duration = (extra->help_marker_until - now) * 1000; pev.color = 208;
      rr_emit_rr(&m->base.hooks, pev);
      long index = (long)extra->help_index - 1;
      if (index >= 0 && (size_t)index < extra->help_point_count) {
        qa_vec3 point = extra->help_points[index];
        qa_vec3 nxt = (size_t)(index + 1) < extra->help_point_count ? extra->help_points[index + 1] : extra->help_location;
        qa_q2_rerelease_event hev; memset(&hev, 0, sizeof hev);
        hev.kind = "help-path"; hev.has_actor = true; hev.actor = actor; hev.first = true;
        hev.position = point; hev.direction = qa_q2_normalize(qa_q2_subtract(nxt, point));
        rr_emit_rr(&m->base.hooks, hev);
      }
    }
  }
  qa_q2_intermission intermission = qa_q2_players_intermission(m->base.players->base);
  if (strcmp(intermission.kind, "playing") != 0 && intermission.map && strchr(intermission.map, '*') &&
      rr_band(m->base.players->intermission_flags, 16) == 0) {
    size_t n = 0; qa_q2_rerelease_level_entry *levels = qa_q2_rerelease_unit_report(&m->base.campaign, &n);
    qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
    ev.kind = "end-of-unit"; ev.levels = levels; ev.level_count = n;
    ev.button_time = intermission.started + 5;
    rr_emit_rr(&m->base.hooks, ev);
    qa_q2_rerelease_unit_report_free(levels, n);
  }
}
void qa_q2_rerelease_module_send_poi(qa_q2_rerelease_module *m, qa_actor_id a, qa_q2_game_services *g) {
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(m->base.players, a);
  extra->help_marker_until = rr_now(g) + 10;
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "poi"; ev.has_actor = true; ev.actor = a; ev.position = extra->help_location;
  ev.image = extra->help_image; ev.duration = 10000; ev.color = 208;
  rr_emit_rr(&m->base.hooks, ev);
}
void qa_q2_rerelease_module_use_compass(qa_q2_rerelease_module *m, qa_actor_id a, qa_q2_game_services *g) {
  if (!m->base.poi.has) {
    qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev);
    ev.kind = "print"; ev.has_actor = true; ev.actor = a; ev.level = "high"; ev.text = "$no_valid_poi";
    rr_emit_host(g, ev); return;
  }
  qa_q2_entity *dynamic = m->base.poi.has_dynamic ? rr_ent(g, &m->base.poi.dynamic) : NULL;
  if (dynamic && dynamic->use.fn) dynamic->use.fn(dynamic->use.ctx, dynamic, g, &a, &a);
  qa_body_state_result br = qa_shared_body_table_read(g->host->bodies, a);
  if (!br.has_value) return;
  qa_body_state body = br.value;
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(m->base.players, a);
  qa_q2rr_poi *poi = &m->base.poi;
  extra->help_location = poi->origin;
  snprintf(extra->help_image, sizeof extra->help_image, "%s", poi->image ? poi->image : "");
  qa_q2rr_navigation path = m->base.hooks.navigation(m->base.hooks.ctx, body.origin, poi->origin, &a);
  if (path.kind && strcmp(path.kind, "path") == 0 && path.point_count != 0) {
    qa_vec3 points[160]; size_t pn = path.point_count < 128 ? path.point_count : 128;
    for (size_t i = 0; i < pn; i++) points[i] = path.points[i];
    size_t index = 0;
    while (index + 1 < pn && qa_q2_length(qa_q2_subtract(points[index], body.origin)) <= 192) index++;
    qa_vec3 first = pn ? points[index] : poi->origin;
    qa_q2_player_hooks *hooks = qa_q2_players_hooks(m->base.players->base);
    qa_vec3 forward = qa_q2_movedir(hooks->movement(hooks->ctx, a).view_angles);
    if (qa_q2_dot(qa_q2_normalize(qa_q2_subtract(first, body.origin)), forward) < 0.3) {
      qa_q2_entity *ent = rr_ent(g, &a);
      double height = ent ? ent->view_height : 22;
      qa_q2_trace_request r; memset(&r, 0, sizeof r);
      r.start = qa_q2_add(body.origin, rr_v3(0, 0, height));
      r.end = qa_q2_add(body.origin, qa_q2_scale(forward, 64));
      r.bounds = NULL; r.ignore = NULL; r.mask = 1;
      qa_trace_result tr = g->host->trace(g->host->ctx, r);
      qa_vec3 point = (tr.fraction < 1 && tr.contact.has_plane)
          ? qa_q2_add(tr.end, qa_q2_scale(tr.contact.plane.normal, 8)) : tr.end;
      if (pn < 160) { memmove(&points[index + 1], &points[index], (pn - index) * sizeof *points); points[index] = point; pn++; }
    }
    free(extra->help_points);
    extra->help_points = pn ? malloc(pn * sizeof(qa_vec3)) : NULL;
    if (pn) memcpy(extra->help_points, points, pn * sizeof(qa_vec3));
    extra->help_point_count = pn; extra->help_point_cap = pn;
    extra->help_index = (double)index; extra->help_draw_time = 0;
    qa_q2_rerelease_module_compass_update(m, a, g, true);
  } else {
    qa_q2_rerelease_module_send_poi(m, a, g);
    qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev);
    ev.kind = "sound"; ev.has_actor = true; ev.actor = a; ev.origin = body.origin;
    ev.path = "misc/help_marker.wav"; ev.channel = 0; ev.volume = 1; ev.attenuation = 1; ev.loop = "once";
    rr_emit_host(g, ev);
  }
}
void qa_q2_rerelease_module_compass_update(qa_q2_rerelease_module *m, qa_actor_id a, qa_q2_game_services *g, bool first) {
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(m->base.players, a);
  size_t idx = extra->help_index >= 0 ? (size_t)extra->help_index : 0;
  qa_body_state_result br = qa_shared_body_table_read(g->host->bodies, a);
  if (idx >= extra->help_point_count || !br.has_value || extra->help_draw_time >= rr_now(g)) return;
  qa_vec3 point = extra->help_points[idx];
  if (qa_q2_length(qa_q2_subtract(point, br.value.origin)) > 4096 || !g->host->in_phs(g->host->ctx, br.value.origin, point)) {
    free(extra->help_points); extra->help_points = NULL; extra->help_point_count = extra->help_point_cap = 0;
    return;
  }
  qa_vec3 nxt = idx + 1 < extra->help_point_count ? extra->help_points[idx + 1] : extra->help_location;
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "help-path"; ev.has_actor = true; ev.actor = a; ev.first = first;
  ev.position = point; ev.direction = qa_q2_normalize(qa_q2_subtract(nxt, point));
  rr_emit_rr(&m->base.hooks, ev);
  qa_q2_rerelease_module_send_poi(m, a, g);
  qa_q2_presentation_event sev; memset(&sev, 0, sizeof sev);
  sev.kind = "sound"; sev.has_actor = true; sev.actor = a; sev.origin = point;
  sev.path = "misc/help_marker.wav"; sev.channel = 0; sev.volume = 1; sev.attenuation = 1; sev.loop = "once";
  rr_emit_host(g, sev);
  extra->help_index++; extra->help_draw_time = rr_now(g) + 0.2;
}
void qa_q2_rerelease_module_begin_player_frame(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_entities_force_fog(&m->base, e->actor.id, false);
  qa_q2_rerelease_goals_notify(&m->goals, e, g);
}
void qa_q2_rerelease_module_help(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_goals_help(&m->goals, e, g);
}
void qa_q2_rerelease_module_end_player_frame(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_entities_end_player_frame(&m->base, e, g);
  if (strcmp(qa_q2_players_intermission(m->base.players->base).kind, "playing") == 0) qa_q2_rerelease_goals_end_player_frame(&m->goals, e, g);
  qa_q2_rerelease_module_compass_update(m, e->actor.id, g, false);
}
