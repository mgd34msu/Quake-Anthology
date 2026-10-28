/* Port of src/content/q3/presentation/weapons.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/weapons.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/ballistics_math.h"
#include "qa/content/q3/base/game/grapple.h"
#include "qa/content/q3/base/shared/trajectory.h"
#include "qa/content/q3/presentation/local_entities.h"
#include "qa/content/q3/presentation/model_access.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
static float f32(double v) { return (float)v; }
enum { CONTENTS_WATER = 32, SURF_NOIMPACT = 16, SURF_METALSTEPS = 4096 };
#define MASK_SHOT (1 | 0x2000000 | 0x4000000)
static qa_vec3 eval_pos(const qa_q3_wire_trajectory *wire, double at_time) {
  qa_q3_base_trajectory trajectory_value = {(qa_q3_base_trajectory_type)wire->type, wire->time, wire->duration, wire->base, wire->delta};
  return qa_q3_base_evaluate_trajectory(trajectory_value, at_time);
}
static qa_vec3 ma3(qa_vec3 origin, double scale, qa_vec3 direction) {
  return qa_add3(origin, qa_scale3(direction, scale));
}
static qa_vec3 transform3(qa_vec3 value, qa_axis axis) {
  qa_vec3 result = {qa_dot3(value, (qa_vec3){axis.vectors[0].x, axis.vectors[1].x, axis.vectors[2].x}),
                    qa_dot3(value, (qa_vec3){axis.vectors[0].y, axis.vectors[1].y, axis.vectors[2].y}),
                    qa_dot3(value, (qa_vec3){axis.vectors[0].z, axis.vectors[1].z, axis.vectors[2].z})};
  return result;
}
const char *qa_q3_pres_eject_brass_name(qa_q3_pres_eject_brass brass) {
  switch (brass) {
    case QA_Q3_PRES_EJECT_MACHINEGUN: return "machinegun";
    case QA_Q3_PRES_EJECT_SHOTGUN: return "shotgun";
    case QA_Q3_PRES_EJECT_NAILGUN: return "nailgun";
    default: return "none";
  }
}
void qa_q3_pres_emit_shotgun_presentation(qa_q3_pres_shotgun_presentation_host host, qa_q3_pres_shotgun_event shot) {
  if (host.smoke_enabled && ((int64_t)host.contents(host.ctx, shot.muzzle) & CONTENTS_WATER) == 0)
    host.smoke(host.ctx, ma3(shot.muzzle, 32, qa_normalize3(qa_sub3(shot.direction, shot.muzzle))));
  qa_q3_shotgun_endpoints endpoints = qa_q3_shotgun_endpoints_compute(shot.muzzle, shot.direction, shot.seed);
  for (int i = 0; i < QA_Q3_SHOTGUN_PELLET_COUNT; i++) {
    qa_vec3 end = endpoints.ends[i];
    qa_q3_pres_shotgun_trace trace = host.trace(host.ctx, shot.muzzle, end);
    double source_contents = host.contents(host.ctx, shot.muzzle);
    double destination_contents = host.contents(host.ctx, trace.end);
    if (source_contents == destination_contents) {
      if (((int64_t)source_contents & CONTENTS_WATER) != 0) host.bubbles(host.ctx, shot.muzzle, trace.end);
    } else if (((int64_t)source_contents & CONTENTS_WATER) != 0) {
      host.bubbles(host.ctx, shot.muzzle, host.water(host.ctx, end, shot.muzzle));
    } else if (((int64_t)destination_contents & CONTENTS_WATER) != 0) {
      host.bubbles(host.ctx, trace.end, host.water(host.ctx, shot.muzzle, end));
    }
    if (((int64_t)trace.surface_flags & SURF_NOIMPACT) != 0) continue;
    if (trace.has_target && host.is_player(host.ctx, trace.target))
      host.blood(host.ctx, trace.end, trace.normal, trace.target);
    else
      host.wall(host.ctx, trace.end, trace.normal,
                ((int64_t)trace.surface_flags & SURF_METALSTEPS) != 0 ? QA_Q3_PRES_IMPACT_SOUND_METAL
                                                                      : QA_Q3_PRES_IMPACT_SOUND_DEFAULT);
  }
}
/* ---- selection ---- */
void qa_q3_pres_client_weapon_selection_init(qa_q3_pres_client_weapon_selection *selection,
    qa_q3_pres_client_game_state *state, void (*selected)(void *ctx, double weapon), void *selected_ctx) {
  selection->state = state;
  selection->selected = selected;
  selection->selected_ctx = selected_ctx;
}
static bool selectable(qa_q3_pres_client_weapon_selection *selection, double number) {
  qa_q3_pres_client_game_state *state = selection->state;
  if (!state->snap) qa_range_error("CG_WeaponSelectable: cg.snap == NULL");
  double ammo = qa_q3_base_player_state_slots_get(&state->snap->player_state.ammo, number);
  double weapons = qa_q3_base_player_state_slots_get(&state->snap->player_state.stats,
      qa_q3_base_stat_schema_for(state->product)->weapons);
  return ammo != 0 && ((int64_t)weapons & (1LL << (int)number)) != 0;
}
static void cycle_weapon(qa_q3_pres_client_weapon_selection *selection, double direction) {
  qa_q3_pres_client_game_state *state = selection->state;
  if (!state->snap) return;
  if (((int64_t)state->snap->player_state.pm_flags & 8) != 0) return; /* MoveFlags.FOLLOW = PMF_FOLLOW (8) */
  state->weapon_select_time = state->time;
  double original = state->weapon_select;
  for (int i = 0; i < 16; i++) {
    state->weapon_select = fmod(state->weapon_select + direction + 16, 16);
    if (state->weapon_select != QA_Q3_WP_GAUNTLET && selectable(selection, state->weapon_select)) {
      if (selection->selected) selection->selected(selection->selected_ctx, state->weapon_select);
      return;
    }
  }
  state->weapon_select = original;
}
void qa_q3_pres_client_weapon_selection_next_weapon(qa_q3_pres_client_weapon_selection *s) { cycle_weapon(s, 1); }
void qa_q3_pres_client_weapon_selection_previous_weapon(qa_q3_pres_client_weapon_selection *s) { cycle_weapon(s, -1); }
void qa_q3_pres_client_weapon_selection_select_weapon(qa_q3_pres_client_weapon_selection *selection, double number) {
  qa_q3_pres_client_game_state *state = selection->state;
  if (!state->snap) return;
  if (((int64_t)state->snap->player_state.pm_flags & 8) != 0 || number < 1 || number > 15) return;
  state->weapon_select_time = state->time;
  double weapons = qa_q3_base_player_state_slots_get(&state->snap->player_state.stats,
      qa_q3_base_stat_schema_for(state->product)->weapons);
  if (((int64_t)weapons & (1LL << (int)number)) != 0) {
    state->weapon_select = number;
    if (selection->selected) selection->selected(selection->selected_ctx, number);
  }
}
void qa_q3_pres_client_weapon_selection_out_of_ammo_change(qa_q3_pres_client_weapon_selection *selection) {
  selection->state->weapon_select_time = selection->state->time;
  for (int i = 15; i > 0; i--) {
    if (selectable(selection, i)) {
      selection->state->weapon_select = i;
      return;
    }
  }
}
/* ---- registry ---- */
struct qa_q3_pres_client_weapon_media_registry {
  qa_q3_base_product product;
  qa_q3_pres_weapon_registration_host host;
  qa_q3_pres_client_weapon_info weapons[16];
  qa_q3_pres_packet_item_visual *items;
  size_t item_count;
  bool registered_weapons[16], ready_weapons[16];
  bool *registered_items;
  qa_q3_pres_registered_weapon_effects effects;
};
void qa_q3_pres_registered_weapon_effects_init(qa_q3_pres_registered_weapon_effects *effects) {
  memset(effects, 0, sizeof *effects);
  effects->lightning_explosion_model = qa_q3_pres_default_model;
}
static void empty_weapon(qa_q3_pres_client_weapon_info *weapon) {
  memset(weapon, 0, sizeof *weapon);
  weapon->base.weapon_model = qa_q3_pres_default_model;
  weapon->base.missile_model = qa_q3_pres_default_model;
  weapon->hands_model = qa_q3_pres_default_model;
  weapon->flash_model = qa_q3_pres_default_model;
  weapon->ammo_model = qa_q3_pres_default_model;
}
qa_q3_pres_client_weapon_media_registry *qa_q3_pres_client_weapon_media_registry_create(
    qa_q3_base_product product, qa_q3_pres_weapon_registration_host host) {
  qa_q3_pres_client_weapon_media_registry *registry = calloc(1, sizeof *registry);
  if (!registry) return NULL;
  registry->product = product;
  registry->host = host;
  for (int i = 0; i < 16; i++) empty_weapon(&registry->weapons[i]);
  size_t count = 0;
  (void)qa_q3_base_item_list(product, &count);
  registry->item_count = count;
  registry->items = count ? calloc(count, sizeof *registry->items) : NULL;
  registry->registered_items = count ? calloc(count, sizeof(bool)) : NULL;
  if (count && (!registry->items || !registry->registered_items)) {
    qa_q3_pres_client_weapon_media_registry_destroy(registry);
    return NULL;
  }
  for (size_t i = 0; i < count; i++) registry->items[i].models[0] = qa_q3_pres_default_model;
  qa_q3_pres_registered_weapon_effects_init(&registry->effects);
  return registry;
}
void qa_q3_pres_client_weapon_media_registry_destroy(qa_q3_pres_client_weapon_media_registry *registry) {
  if (!registry) return;
  free(registry->items);
  free(registry->registered_items);
  free(registry);
}
qa_q3_base_product qa_q3_pres_client_weapon_media_registry_product(qa_q3_pres_client_weapon_media_registry *r) {
  return r->product;
}
qa_q3_pres_registered_weapon_effects *qa_q3_pres_client_weapon_media_registry_effects(
    qa_q3_pres_client_weapon_media_registry *r) { return &r->effects; }
const qa_q3_pres_client_weapon_info *qa_q3_pres_client_weapon_media_registry_weapons(
    qa_q3_pres_client_weapon_media_registry *r, size_t *out_count) {
  *out_count = 16;
  return r->weapons;
}
const qa_q3_pres_packet_item_visual *qa_q3_pres_client_weapon_media_registry_items(
    qa_q3_pres_client_weapon_media_registry *r, size_t *out_count) {
  *out_count = r->item_count;
  return r->items;
}
qa_q3_pres_client_weapon_info *qa_q3_pres_client_weapon_media_registry_weapon(
    qa_q3_pres_client_weapon_media_registry *r, double number) {
  if (number != trunc(number) || number < 0 || number >= 16) {
    if (number == trunc(number)) qa_range_error("Invalid weapon media index %lld", (long long)number);
    qa_range_error("Invalid weapon media index %g", number);
  }
  return &r->weapons[(size_t)number];
}
qa_q3_pres_client_weapon_info *qa_q3_pres_client_weapon_media_registry_require_weapon(
    qa_q3_pres_client_weapon_media_registry *r, double number) {
  if (number != 0 && !(number == trunc(number) && number >= 0 && number < 16 && r->ready_weapons[(size_t)number])) {
    if (number == trunc(number))
      qa_range_error("Weapon %lld must finish registration before synchronous presentation", (long long)number);
    qa_range_error("Weapon %g must finish registration before synchronous presentation", number);
  }
  return qa_q3_pres_client_weapon_media_registry_weapon(r, number);
}
static bool reg_sound(qa_q3_pres_client_weapon_media_registry *r, const char *path, qa_pcm_sound **out) {
  return r->host.register_sound(r->host.ctx, path, false, out);
}
static bool reg_shader(qa_q3_pres_client_weapon_media_registry *r, const char *path, const qa_q3_pres_scene_shader **out) {
  return r->host.register_shader(r->host.ctx, path, out);
}
static bool reg_model(qa_q3_pres_client_weapon_media_registry *r, const char *path, qa_q3_pres_scene_model *out) {
  return r->host.register_model(r->host.ctx, path, out);
}
static bool set_flash(qa_q3_pres_client_weapon_media_registry *r, qa_q3_pres_client_weapon_info *w, const char *path) {
  qa_pcm_sound *sound = NULL;
  if (!reg_sound(r, path, &sound)) return false;
  w->flash_sounds[0] = sound;
  w->flash_sounds[1] = w->flash_sounds[2] = w->flash_sounds[3] = NULL;
  return true;
}
static bool register_weapon_specific(qa_q3_pres_client_weapon_media_registry *r, double number,
                                     qa_q3_pres_client_weapon_info *w) {
  qa_q3_pres_registered_weapon_effects *fx = &r->effects;
  qa_pcm_sound *sound = NULL;
  switch ((int)number) {
    case QA_Q3_WP_GAUNTLET:
      w->flash_dlight_color = (qa_vec3){0.6, 0.6, 1};
      if (!reg_sound(r, "sound/weapons/melee/fstrun.wav", &w->firing_sound)) return false;
      return set_flash(r, w, "sound/weapons/melee/fstatck.wav");
    case QA_Q3_WP_LIGHTNING:
      w->flash_dlight_color = (qa_vec3){0.6, 0.6, 1};
      if (!reg_sound(r, "sound/weapons/melee/fsthum.wav", &w->ready_sound)) return false;
      if (!reg_sound(r, "sound/weapons/lightning/lg_hum.wav", &w->firing_sound)) return false;
      if (!set_flash(r, w, "sound/weapons/lightning/lg_fire.wav")) return false;
      if (!reg_shader(r, "lightningBoltNew", &fx->lightning_shader)) return false;
      if (!reg_model(r, "models/weaphits/crackle.md3", &fx->lightning_explosion_model)) return false;
      if (!reg_sound(r, "sound/weapons/lightning/lg_hit.wav", &fx->lightning_hit_sounds[0])) return false;
      if (!reg_sound(r, "sound/weapons/lightning/lg_hit2.wav", &fx->lightning_hit_sounds[1])) return false;
      return reg_sound(r, "sound/weapons/lightning/lg_hit3.wav", &fx->lightning_hit_sounds[2]);
    case QA_Q3_WP_GRAPPLING_HOOK:
      if (!reg_shader(r, "lightningBoltNew", &fx->lightning_shader)) return false;
      w->flash_dlight_color = (qa_vec3){0.6, 0.6, 1};
      if (!reg_model(r, "models/ammo/rocket/rocket.md3", &w->base.missile_model)) return false;
      w->base.has_missile_trail = true;
      w->base.missile_trail = QA_Q3_PRES_TRAIL_GRAPPLE;
      w->base.missile_dlight = 200;
      w->base.trail_time = 2000;
      w->base.trail_radius = 64;
      w->base.missile_dlight_color = (qa_vec3){1, 0.75, 0};
      if (!reg_sound(r, "sound/weapons/melee/fsthum.wav", &w->ready_sound)) return false;
      return reg_sound(r, "sound/weapons/melee/fstrun.wav", &w->firing_sound);
    case QA_Q3_WP_CHAINGUN:
      if (!reg_sound(r, "sound/weapons/vulcan/wvulfire.wav", &w->firing_sound)) return false;
      w->loop_fire_sound = true;
      w->flash_dlight_color = (qa_vec3){1, 1, 0};
      {
        const char *paths[4] = {"sound/weapons/vulcan/vulcanf1b.wav", "sound/weapons/vulcan/vulcanf2b.wav",
                                "sound/weapons/vulcan/vulcanf3b.wav", "sound/weapons/vulcan/vulcanf4b.wav"};
        for (int i = 0; i < 4; i++) if (!reg_sound(r, paths[i], &w->flash_sounds[i])) return false;
      }
      w->eject_brass = QA_Q3_PRES_EJECT_MACHINEGUN;
      return reg_shader(r, "bulletExplosion", &fx->bullet_explosion_shader);
    case QA_Q3_WP_MACHINEGUN:
      w->flash_dlight_color = (qa_vec3){1, 1, 0};
      {
        const char *paths[4] = {"sound/weapons/machinegun/machgf1b.wav", "sound/weapons/machinegun/machgf2b.wav",
                                "sound/weapons/machinegun/machgf3b.wav", "sound/weapons/machinegun/machgf4b.wav"};
        for (int i = 0; i < 4; i++) if (!reg_sound(r, paths[i], &w->flash_sounds[i])) return false;
      }
      w->eject_brass = QA_Q3_PRES_EJECT_MACHINEGUN;
      return reg_shader(r, "bulletExplosion", &fx->bullet_explosion_shader);
    case QA_Q3_WP_SHOTGUN:
      w->flash_dlight_color = (qa_vec3){1, 1, 0};
      if (!set_flash(r, w, "sound/weapons/shotgun/sshotf1b.wav")) return false;
      w->eject_brass = QA_Q3_PRES_EJECT_SHOTGUN;
      return true;
    case QA_Q3_WP_ROCKET_LAUNCHER:
      if (!reg_model(r, "models/ammo/rocket/rocket.md3", &w->base.missile_model)) return false;
      if (!reg_sound(r, "sound/weapons/rocket/rockfly.wav", &w->base.missile_sound)) return false;
      w->base.has_missile_trail = true;
      w->base.missile_trail = QA_Q3_PRES_TRAIL_ROCKET;
      w->base.missile_dlight = 200;
      w->base.trail_time = 2000;
      w->base.trail_radius = 64;
      w->base.missile_dlight_color = (qa_vec3){1, 0.75, 0};
      w->flash_dlight_color = (qa_vec3){1, 0.75, 0};
      if (!set_flash(r, w, "sound/weapons/rocket/rocklf1a.wav")) return false;
      return reg_shader(r, "rocketExplosion", &fx->rocket_explosion_shader);
    case QA_Q3_WP_PROX_LAUNCHER:
    case QA_Q3_WP_GRENADE_LAUNCHER: {
      bool prox = (int)number == QA_Q3_WP_PROX_LAUNCHER;
      if (!reg_model(r, prox ? "models/weaphits/proxmine.md3" : "models/ammo/grenade1.md3", &w->base.missile_model))
        return false;
      w->base.has_missile_trail = true;
      w->base.missile_trail = QA_Q3_PRES_TRAIL_GRENADE;
      w->base.trail_time = 700;
      w->base.trail_radius = 32;
      w->flash_dlight_color = (qa_vec3){1, 0.7, 0};
      if (!set_flash(r, w, prox ? "sound/weapons/proxmine/wstbfire.wav" : "sound/weapons/grenade/grenlf1a.wav"))
        return false;
      return reg_shader(r, "grenadeExplosion", &fx->grenade_explosion_shader);
    }
    case QA_Q3_WP_NAILGUN:
      w->eject_brass = QA_Q3_PRES_EJECT_NAILGUN;
      w->base.has_missile_trail = true;
      w->base.missile_trail = QA_Q3_PRES_TRAIL_NAIL;
      w->base.trail_radius = 16;
      w->base.trail_time = 250;
      if (!reg_model(r, "models/weaphits/nail.md3", &w->base.missile_model)) return false;
      w->flash_dlight_color = (qa_vec3){1, 0.75, 0};
      return set_flash(r, w, "sound/weapons/nailgun/wnalfire.wav");
    case QA_Q3_WP_PLASMAGUN:
      w->base.has_missile_trail = true;
      w->base.missile_trail = QA_Q3_PRES_TRAIL_PLASMA;
      if (!reg_sound(r, "sound/weapons/plasma/lasfly.wav", &w->base.missile_sound)) return false;
      w->flash_dlight_color = (qa_vec3){0.6, 0.6, 1};
      if (!set_flash(r, w, "sound/weapons/plasma/hyprbf1a.wav")) return false;
      if (!reg_shader(r, "plasmaExplosion", &fx->plasma_explosion_shader)) return false;
      return reg_shader(r, "railDisc", &fx->rail_rings_shader);
    case QA_Q3_WP_RAILGUN:
      if (!reg_sound(r, "sound/weapons/railgun/rg_hum.wav", &w->ready_sound)) return false;
      w->flash_dlight_color = (qa_vec3){1, 0.5, 0};
      if (!set_flash(r, w, "sound/weapons/railgun/railgf1a.wav")) return false;
      if (!reg_shader(r, "railExplosion", &fx->rail_explosion_shader)) return false;
      if (!reg_shader(r, "railDisc", &fx->rail_rings_shader)) return false;
      return reg_shader(r, "railCore", &fx->rail_core_shader);
    case QA_Q3_WP_BFG:
      if (!reg_sound(r, "sound/weapons/bfg/bfg_hum.wav", &w->ready_sound)) return false;
      w->flash_dlight_color = (qa_vec3){1, 0.7, 1};
      if (!set_flash(r, w, "sound/weapons/bfg/bfg_fire.wav")) return false;
      if (!reg_shader(r, "bfgExplosion", &fx->bfg_explosion_shader)) return false;
      if (!reg_model(r, "models/weaphits/bfg.md3", &w->base.missile_model)) return false;
      return reg_sound(r, "sound/weapons/rocket/rockfly.wav", &w->base.missile_sound);
    default:
      (void)sound;
      w->flash_dlight_color = (qa_vec3){1, 1, 1};
      return set_flash(r, w, "sound/weapons/rocket/rocklf1a.wav");
  }
}
static bool register_weapon_now(qa_q3_pres_client_weapon_media_registry *r, double number);
static bool register_item_now(qa_q3_pres_client_weapon_media_registry *r, double number) {
  if (number < 0 || number >= (double)r->item_count) {
    if (number == trunc(number))
      qa_range_error("CG_RegisterItemVisuals: itemNum %lld out of range [0-%lld]", (long long)number,
                     (long long)r->item_count - 1);
    qa_range_error("CG_RegisterItemVisuals: itemNum %g out of range [0-%lld]", number, (long long)r->item_count - 1);
  }
  const qa_q3_base_item_definition *item = qa_q3_base_item_at(r->product, number);
  size_t index = (size_t)number;
  if (r->registered_items[index]) return true;
  r->registered_items[index] = true;
  qa_q3_pres_scene_model model = qa_q3_pres_default_model;
  if (item->world_models[0] && !reg_model(r, item->world_models[0], &model)) return false;
  const qa_q3_pres_scene_shader *icon = NULL;
  if (item->icon && !reg_shader(r, item->icon, &icon)) return false;
  r->items[index].models[0] = model;
  r->items[index].has_second_model = false;
  r->items[index].icon = icon;
  if (item->type == QA_Q3_BASE_IT_WEAPON && !register_weapon_now(r, item->tag)) return false;
  bool second = (item->type == 5 || item->type == 4 || item->type == QA_Q3_BASE_IT_ARMOR || item->type == 6);
  (void)second;
  /* TS: IT_POWERUP/HEALTH/ARMOR/HOLDABLE carry a second world model. */
  if ((item->type == QA_Q3_BASE_IT_ARMOR || item->type >= 4) && item->world_models[1]) {
    qa_q3_pres_scene_model model2;
    if (!reg_model(r, item->world_models[1], &model2)) return false;
    r->items[index].models[1] = model2;
    r->items[index].has_second_model = true;
  }
  return true;
}
static bool register_weapon_now(qa_q3_pres_client_weapon_media_registry *r, double number) {
  (void)qa_q3_pres_client_weapon_media_registry_weapon(r, number);
  if (number == 0 || r->registered_weapons[(size_t)number]) return true;
  r->registered_weapons[(size_t)number] = true;
  size_t count = 0;
  const qa_q3_base_item_definition *items = qa_q3_base_item_list(r->product, &count);
  int found = -1;
  for (size_t i = 0; i < count; i++) {
    if (items[i].type == QA_Q3_BASE_IT_WEAPON && items[i].tag == number) { found = (int)i; break; }
  }
  if (found < 0) qa_range_error("Couldn't find weapon %lld", (long long)number);
  const qa_q3_base_item_definition *item = qa_q3_base_item_at(r->product, found);
  qa_q3_pres_client_weapon_info *weapon = &r->weapons[(size_t)number];
  empty_weapon(weapon);
  weapon->item = item;
  if (!register_item_now(r, found)) return false;
  const char *path = item->world_models[0];
  if (!path || !item->icon) qa_range_error("Weapon %lld has no world model or icon", (long long)number);
  if (!reg_model(r, path, &weapon->base.weapon_model)) return false;
  if (weapon->base.weapon_model.kind == QA_Q3_PRES_SCENE_MODEL_LOADED ||
      weapon->base.weapon_model.kind == QA_Q3_PRES_SCENE_MODEL_INLINE) {
    qa_bounds bounds = qa_q3_pres_model_bounds(&weapon->base.weapon_model);
    weapon->base.weapon_midpoint = (qa_vec3){
        f32(bounds.min.x + f32(0.5 * f32(bounds.max.x - bounds.min.x))),
        f32(bounds.min.y + f32(0.5 * f32(bounds.max.y - bounds.min.y))),
        f32(bounds.min.z + f32(0.5 * f32(bounds.max.z - bounds.min.z)))};
  }
  if (!reg_shader(r, item->icon, &weapon->weapon_icon)) return false;
  if (!reg_shader(r, item->icon, &weapon->ammo_icon)) return false;
  for (size_t i = 0; i < count; i++) {
    if (items[i].type == QA_Q3_BASE_IT_AMMO && items[i].tag == number) {
      if (items[i].world_models[0] && !reg_model(r, items[i].world_models[0], &weapon->ammo_model)) return false;
      break;
    }
  }
  char stem[256];
  const char *dot = strchr(path, '.');
  size_t stem_length = dot ? (size_t)(dot - path) : strlen(path);
  if (stem_length >= sizeof stem) stem_length = sizeof stem - 1;
  memcpy(stem, path, stem_length);
  stem[stem_length] = '\0';
  char derived[320];
  snprintf(derived, sizeof derived, "%s_flash.md3", stem);
  if (!reg_model(r, derived, &weapon->flash_model)) return false;
  snprintf(derived, sizeof derived, "%s_barrel.md3", stem);
  qa_q3_pres_scene_model barrel;
  if (!reg_model(r, derived, &barrel)) return false;
  weapon->base.has_barrel_model = barrel.kind != QA_Q3_PRES_SCENE_MODEL_DEFAULT;
  if (weapon->base.has_barrel_model) weapon->base.barrel_model = barrel;
  snprintf(derived, sizeof derived, "%s_hand.md3", stem);
  if (!reg_model(r, derived, &weapon->hands_model)) return false;
  if (weapon->hands_model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT &&
      !reg_model(r, "models/weapons2/shotgun/shotgun_hand.md3", &weapon->hands_model)) return false;
  if (!register_weapon_specific(r, number, weapon)) return false;
  r->ready_weapons[(size_t)number] = true;
  return true;
}
bool qa_q3_pres_client_weapon_media_registry_register_weapon(qa_q3_pres_client_weapon_media_registry *r, double number) {
  return register_weapon_now(r, number);
}
bool qa_q3_pres_client_weapon_media_registry_register_item_visuals(qa_q3_pres_client_weapon_media_registry *r,
                                                                   double number) {
  return register_item_now(r, number);
}
static const qa_q3_pres_scene_shader *view_lightning(void *ctx) {
  return ((qa_q3_pres_client_weapon_media_registry *)ctx)->effects.lightning_shader;
}
static const qa_q3_pres_scene_shader *view_rocket(void *ctx) {
  return ((qa_q3_pres_client_weapon_media_registry *)ctx)->effects.rocket_explosion_shader;
}
static bool view_register_items(void *ctx, double number) {
  return qa_q3_pres_client_weapon_media_registry_register_item_visuals(ctx, number);
}
bool qa_q3_pres_client_weapon_media_registry_make_view(qa_q3_pres_client_weapon_media_registry *r,
    qa_q3_pres_packet_weapon_info **scratch, qa_q3_pres_client_weapon_media_registry_view *out) {
  *scratch = malloc(16 * sizeof **scratch);
  if (!*scratch) return false;
  for (int i = 0; i < 16; i++) (*scratch)[i] = r->weapons[i].base;
  memset(out, 0, sizeof *out);
  out->items = r->items;
  out->item_count = r->item_count;
  out->weapons = *scratch;
  out->weapon_count = 16;
  out->effects = (qa_q3_pres_registered_weapon_effects_view){r, view_lightning, view_rocket};
  out->ctx = r;
  out->register_item_visuals = view_register_items;
  return true;
}
/* ---- runtime ---- */
void qa_q3_pres_client_weapon_runtime_init(qa_q3_pres_client_weapon_runtime *runtime,
    qa_q3_pres_client_game_state *state, qa_q3_pres_client_weapon_media_registry *registry,
    qa_q3_pres_client_weapon_host host) {
  qa_q3_pres_client_weapon_selection_init(&runtime->selection, state, NULL, NULL);
  if (state->product != registry->product) qa_range_error("Weapon media product differs from cgame state");
  runtime->registry = registry;
  runtime->host = host;
}
static qa_q3_pres_client_game_state *rt_state(qa_q3_pres_client_weapon_runtime *rt) { return rt->selection.state; }
void qa_q3_pres_client_weapon_runtime_out_of_ammo_change(qa_q3_pres_client_weapon_runtime *rt) {
  qa_q3_pres_client_weapon_selection_out_of_ammo_change(&rt->selection);
}
void qa_q3_pres_client_weapon_runtime_next_weapon(qa_q3_pres_client_weapon_runtime *rt) {
  qa_q3_pres_client_weapon_selection_next_weapon(&rt->selection);
}
void qa_q3_pres_client_weapon_runtime_previous_weapon(qa_q3_pres_client_weapon_runtime *rt) {
  qa_q3_pres_client_weapon_selection_previous_weapon(&rt->selection);
}
void qa_q3_pres_client_weapon_runtime_select_weapon(qa_q3_pres_client_weapon_runtime *rt, double weapon) {
  qa_q3_pres_client_weapon_selection_select_weapon(&rt->selection, weapon);
}
static void eject_brass(qa_q3_pres_client_weapon_runtime *rt, qa_q3_pres_client_entity *cent,
                        qa_q3_pres_eject_brass kind);
void qa_q3_pres_client_weapon_runtime_fire_weapon(qa_q3_pres_client_weapon_runtime *rt,
                                                  qa_q3_pres_client_entity *cent) {
  qa_q3_base_entity_state *ent = &cent->current_state;
  if (ent->weapon == QA_Q3_WP_NONE) return;
  double limit = rt_state(rt)->product == QA_Q3_BASE_PRODUCT_BASEQ3 ? 11 : 14;
  if (ent->weapon >= limit) qa_range_error("CG_FireWeapon: ent->weapon >= WP_NUM_WEAPONS");
  qa_q3_pres_client_weapon_info *weapon =
      qa_q3_pres_client_weapon_media_registry_weapon(rt->registry, ent->weapon);
  cent->muzzle_flash_time = rt_state(rt)->time;
  if (ent->weapon == QA_Q3_WP_LIGHTNING && cent->player.lightning_firing != 0) return;
  if (((int64_t)ent->powerups & (1 << QA_Q3_PW_QUAD)) != 0)
    rt->host.start_sound(rt->host.ctx, NULL, ent->number, 4, rt->host.media.sounds.quad);
  int length = 4;
  for (int i = 0; i < 4; i++) {
    if (!weapon->flash_sounds[i]) { length = i; break; }
  }
  if (length > 0) {
    int index = (int)rt->host.rand(rt->host.random_ctx) % length;
    qa_pcm_sound *sound = weapon->flash_sounds[index];
    if (sound) rt->host.start_sound(rt->host.ctx, NULL, ent->number, 2, sound);
  }
  if (weapon->eject_brass != QA_Q3_PRES_EJECT_NONE && rt->host.settings(rt->host.ctx).brass_time > 0)
    eject_brass(rt, cent, weapon->eject_brass);
}
static void eject_brass(qa_q3_pres_client_weapon_runtime *rt, qa_q3_pres_client_entity *cent,
                        qa_q3_pres_eject_brass kind) {
  qa_axis axis = qa_qvm_angles_to_axis(cent->lerp_angles);
  double time = rt_state(rt)->time;
  if (kind == QA_Q3_PRES_EJECT_NAILGUN) {
    qa_q3_pres_smoke_puff_options options = {qa_add3(cent->lerp_origin, transform3((qa_vec3){0, -12, 24}, axis)),
        {0, 0, 64}, 32, {1, 1, 1, 0.33}, 700, time, 0, 0, rt->host.media.shaders.smoke_puff};
    qa_q3_pres_local_entity *smoke = rt->host.smoke_puff(rt->host.effects_ctx, options);
    if (smoke) smoke->le_type = QA_Q3_PRES_LE_SCALE_FADE;
    return;
  }
  double brass_time = rt->host.settings(rt->host.ctx).brass_time;
  if (brass_time <= 0) return;
  bool shotgun = kind == QA_Q3_PRES_EJECT_SHOTGUN;
  for (int i = 0; i < (shotgun ? 2 : 1); i++) {
    qa_q3_pres_scene_model model = shotgun ? rt->host.media.models.shotgun_brass : rt->host.media.models.machinegun_brass;
    qa_q3_pres_ref_entity ref = qa_q3_pres_ref_entity_from_model(qa_q3_pres_create_model_entity(model));
    qa_q3_pres_local_entity *le =
        qa_q3_pres_local_entity_pool_allocate(rt->host.local_entities, QA_Q3_PRES_LE_FRAGMENT, ref);
    qa_vec3 velocity = shotgun
        ? (qa_vec3){f32(60 + f32(60 * rt->host.crandom(rt->host.random_ctx))),
                    f32((i == 0 ? 40 : -40) + f32(10 * rt->host.crandom(rt->host.random_ctx))),
                    f32(100 + f32(50 * rt->host.crandom(rt->host.random_ctx)))}
        : (qa_vec3){0, f32(-50 + f32(40 * rt->host.crandom(rt->host.random_ctx))),
                    f32(100 + f32(50 * rt->host.crandom(rt->host.random_ctx)))};
    le->start_time = time;
    le->end_time = qa_qvm_float_to_int(f32(f32(qa_int32(time + (double)((int64_t)brass_time * (shotgun ? 3 : 1)))) +
        f32(f32(shotgun ? brass_time : trunc(brass_time / 4)) * rt->host.random(rt->host.random_ctx))));
    double pos_time = shotgun ? time : qa_int32(time - ((int32_t)rt->host.rand(rt->host.random_ctx) & 15));
    qa_vec3 origin = qa_add3(cent->lerp_origin, transform3((qa_vec3){8, shotgun ? 0 : -4, 24}, axis));
    double water = ((int64_t)rt->host.point_contents(rt->host.prediction_ctx, origin, -1) & CONTENTS_WATER) != 0
                       ? f32(0.1) : 1;
    le->pos.type = QA_Q3_BASE_TR_GRAVITY;
    le->pos.time = pos_time;
    le->pos.duration = 0;
    le->pos.base = origin;
    le->pos.delta = qa_scale3(transform3(velocity, axis), water);
    le->ref_entity.as.model.origin = origin;
    le->ref_entity.as.model.axis = (qa_axis){{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    le->bounce_factor = shotgun ? f32(0.3) : f32(f32(0.4) * water);
    le->angles.type = QA_Q3_BASE_TR_LINEAR;
    le->angles.time = time;
    le->angles.duration = 0;
    le->angles.base = (qa_vec3){(double)((int32_t)rt->host.rand(rt->host.random_ctx) & 31),
                                (double)((int32_t)rt->host.rand(rt->host.random_ctx) & 31),
                                (double)((int32_t)rt->host.rand(rt->host.random_ctx) & 31)};
    le->angles.delta = shotgun ? (qa_vec3){1, 0.5, 0} : (qa_vec3){2, 1, 0};
    le->le_flags = QA_Q3_PRES_LEF_TUMBLE;
    le->le_bounce_sound_type = QA_Q3_PRES_LE_SOUND_BRASS;
    le->le_mark_type = QA_Q3_PRES_LE_MARK_NONE;
  }
}
void qa_q3_pres_client_weapon_runtime_rail_trail(qa_q3_pres_client_weapon_runtime *rt, double client_num,
                                                 qa_vec3 *start, qa_vec3 end) {
  qa_q3_pres_emit_rail_trail(rt_state(rt)->time, rt->registry, &rt->host, client_num, start, end);
}
void qa_q3_pres_client_weapon_runtime_missile_trail(qa_q3_pres_client_weapon_runtime *rt,
    qa_q3_pres_missile_trail kind, qa_q3_pres_client_entity *cent, const qa_q3_pres_packet_weapon_info *weapon) {
  if (kind == QA_Q3_PRES_TRAIL_GRAPPLE) {
    qa_q3_pres_client_weapon_runtime_grapple_trail(rt, cent, weapon);
    return;
  }
  if (kind == QA_Q3_PRES_TRAIL_PLASMA) {
    qa_q3_pres_emit_plasma_trail(rt_state(rt)->time,
        eval_pos(&cent->current_state.pos, rt_state(rt)->time), cent->lerp_angles,
        cent->current_state.weapon, rt->registry, &rt->host);
    return;
  }
  if (rt->host.settings(rt->host.ctx).no_projectile_trail) return;
  qa_q3_base_entity_state *es = &cent->current_state;
  double time = rt_state(rt)->time;
  double t = (double)((int64_t)50 * (int64_t)trunc((double)qa_int32(cent->trail_time + 50) / 50));
  qa_vec3 origin = eval_pos(&es->pos, time);
  double contents = rt->host.point_contents(rt->host.prediction_ctx, origin, -1);
  if (es->pos.type == QA_Q3_BASE_TR_STATIONARY) {
    cent->trail_time = time;
    return;
  }
  qa_vec3 previous = eval_pos(&es->pos, cent->trail_time);
  double last_contents = rt->host.point_contents(rt->host.prediction_ctx, previous, -1);
  cent->trail_time = time;
  if (((int64_t)contents & (32 | 16 | 8)) != 0) {
    if (((int64_t)contents & (int64_t)last_contents & CONTENTS_WATER) != 0)
      rt->host.bubble_trail(rt->host.effects_ctx, previous, origin, 8);
    return;
  }
  for (; t <= time; t = qa_int32(t + 50)) {
    qa_q3_pres_smoke_puff_options options = {eval_pos(&es->pos, t), {0, 0, 0},
        weapon->trail_radius, {1, 1, 1, 0.33}, weapon->trail_time, t, 0, 0,
        kind == QA_Q3_PRES_TRAIL_NAIL ? rt->host.media.shaders.nail_puff : rt->host.media.shaders.smoke_puff};
    qa_q3_pres_local_entity *smoke = rt->host.smoke_puff(rt->host.effects_ctx, options);
    if (smoke) smoke->le_type = QA_Q3_PRES_LE_SCALE_FADE;
  }
}
void qa_q3_pres_client_weapon_runtime_grapple_trail(qa_q3_pres_client_weapon_runtime *rt,
    qa_q3_pres_client_entity *cent, const qa_q3_pres_packet_weapon_info *weapon) {
  (void)weapon;
  qa_vec3 origin = eval_pos(&cent->current_state.pos, rt_state(rt)->time);
  cent->trail_time = rt_state(rt)->time;
  qa_q3_pres_client_entity *owner =
      qa_q3_pres_client_game_state_entity_at(rt_state(rt), cent->current_state.other_entity_num);
  qa_q3_grapple_cable cable;
  /* Landed grapple cable takes an explicit view height (TS owner pose). */
  if (!qa_q3_get_grapple_cable(owner->lerp_origin, qa_qvm_angle_vectors(owner->lerp_angles).up, origin, 0, &cable))
    return;
  qa_q3_pres_ref_entity beam = qa_q3_pres_ref_entity_from_rail(QA_Q3_PRES_REF_LIGHTNING,
                                                               qa_q3_pres_create_lightning_entity());
  beam.as.rail.origin = cable.start;
  beam.as.rail.old_origin = cable.end;
  beam.as.rail.shading.custom_shader = rt->registry->effects.lightning_shader;
  beam.as.rail.shading.shader_rgba = (qa_vec4){255, 255, 255, 255};
  rt->host.add_ref_entity(rt->host.ctx, &beam);
}
void qa_q3_pres_client_weapon_runtime_missile_hit_wall(qa_q3_pres_client_weapon_runtime *rt, double weapon,
    double client_num, qa_vec3 origin, qa_vec3 direction, qa_q3_pres_impact_sound sound_type) {
  qa_q3_pres_emit_weapon_impact(rt_state(rt)->product, rt->registry, &rt->host, weapon, client_num, origin,
                                direction, sound_type);
}
void qa_q3_pres_client_weapon_runtime_missile_hit_player(qa_q3_pres_client_weapon_runtime *rt, double weapon,
    qa_vec3 origin, qa_vec3 direction, double entity_num) {
  rt->host.bleed(rt->host.effects_ctx, origin, entity_num);
  bool mission = rt_state(rt)->product == QA_Q3_BASE_PRODUCT_MISSIONPACK;
  if (weapon == QA_Q3_WP_GRENADE_LAUNCHER || weapon == QA_Q3_WP_ROCKET_LAUNCHER ||
      (mission && (weapon == QA_Q3_WP_NAILGUN || weapon == QA_Q3_WP_CHAINGUN || weapon == QA_Q3_WP_PROX_LAUNCHER)))
    qa_q3_pres_client_weapon_runtime_missile_hit_wall(rt, weapon, 0, origin, direction,
                                                      QA_Q3_PRES_IMPACT_SOUND_FLESH);
}
typedef struct { qa_q3_pres_client_weapon_runtime *rt; qa_q3_base_entity_state *es; } shotgun_rt;
static qa_q3_pres_shotgun_trace sg_trace(void *ctx, qa_vec3 start, qa_vec3 end) {
  shotgun_rt *sg = ctx;
  qa_bounds point = {{0, 0, 0}, {0, 0, 0}};
  qa_q3_pres_movement_trace trace = sg->rt->host.trace(sg->rt->host.prediction_ctx, start, end, point,
                                                        sg->es->other_entity_num, MASK_SHOT);
  qa_q3_pres_shotgun_trace out;
  out.end = trace.result.end;
  out.normal = trace.result.has_plane ? trace.result.plane.normal : (qa_vec3){0, 0, 0};
  out.surface_flags = trace.result.surface_flags;
  out.has_target = true;
  out.target = trace.entity_num;
  return out;
}
static qa_vec3 sg_water(void *ctx, qa_vec3 start, qa_vec3 end) {
  shotgun_rt *sg = ctx;
  return sg->rt->host.collision_trace_end(sg->rt->host.prediction_ctx, start, end, CONTENTS_WATER);
}
static double sg_contents(void *ctx, qa_vec3 point) {
  shotgun_rt *sg = ctx;
  return sg->rt->host.collision_point_contents(sg->rt->host.prediction_ctx, point);
}
static bool sg_is_player(void *ctx, double target) {
  shotgun_rt *sg = ctx;
  return qa_q3_pres_client_game_state_entity_at(rt_state(sg->rt), target)->current_state.e_type ==
         QA_Q3_BASE_ET_PLAYER;
}
static void sg_blood(void *ctx, qa_vec3 point, qa_vec3 normal, double target) {
  shotgun_rt *sg = ctx;
  qa_q3_pres_client_weapon_runtime_missile_hit_player(sg->rt, QA_Q3_WP_SHOTGUN, point, normal, target);
}
static void sg_wall(void *ctx, qa_vec3 point, qa_vec3 normal, qa_q3_pres_impact_sound sound) {
  shotgun_rt *sg = ctx;
  qa_q3_pres_client_weapon_runtime_missile_hit_wall(sg->rt, QA_Q3_WP_SHOTGUN, 0, point, normal, sound);
}
static void sg_bubbles(void *ctx, qa_vec3 start, qa_vec3 end) {
  shotgun_rt *sg = ctx;
  sg->rt->host.bubble_trail(sg->rt->host.effects_ctx, start, end, 32);
}
static void sg_smoke(void *ctx, qa_vec3 origin) {
  shotgun_rt *sg = ctx;
  qa_q3_pres_smoke_puff_options options = {origin, {0, 0, 8}, 32, {1, 1, 1, 0.33}, 900, rt_state(sg->rt)->time, 0,
                                           QA_Q3_PRES_LEF_PUFF_DONT_SCALE, sg->rt->host.media.shaders.shotgun_smoke_puff};
  (void)sg->rt->host.smoke_puff(sg->rt->host.effects_ctx, options);
}
void qa_q3_pres_client_weapon_runtime_shotgun_fire(qa_q3_pres_client_weapon_runtime *rt,
                                                   qa_q3_base_entity_state *es) {
  shotgun_rt sg = {rt, es};
  qa_q3_pres_shotgun_presentation_host host = {!rt->host.settings(rt->host.ctx).ragepro, &sg, sg_trace, sg_water,
                                               sg_contents, sg_is_player, sg_blood, sg_wall, sg_bubbles, sg_smoke};
  qa_q3_pres_shotgun_event shot = {es->pos.base, es->origin2, es->event_parm};
  qa_q3_pres_emit_shotgun_presentation(host, shot);
}
static bool muzzle_point(qa_q3_pres_client_weapon_runtime *rt, double entity_num, qa_vec3 *out) {
  qa_q3_pres_client_game_state *state = rt_state(rt);
  if (!state->snap) qa_range_error("CG_CalcMuzzlePoint: cg.snap == NULL");
  qa_q3_base_source_player_state *snap_ps = &state->snap->player_state;
  if (entity_num == snap_ps->client_num) {
    qa_vec3 origin = qa_q3_base_player_state_record_origin(snap_ps);
    *out = ma3(qa_add3(origin, (qa_vec3){0, 0, snap_ps->viewheight}), 14,
               qa_qvm_angle_vectors(snap_ps->viewangles).forward);
    return true;
  }
  qa_q3_pres_client_entity *cent = qa_q3_pres_client_game_state_entity_at(state, entity_num);
  if (!cent->current_valid) return false;
  qa_q3_base_entity_state *es = &cent->current_state;
  double anim = (double)((int64_t)es->legs_anim & ~128LL);
  double height = (anim == 9 || anim == 11) ? 12 : 26; /* LEGS_WALKCR / LEGS_IDLECR */
  *out = ma3(qa_add3(es->pos.base, (qa_vec3){0, 0, height}), 14, qa_qvm_angle_vectors(es->apos.base).forward);
  return true;
}
void qa_q3_pres_client_weapon_runtime_bullet(qa_q3_pres_client_weapon_runtime *rt, qa_vec3 end,
                                             double source, qa_q3_pres_bullet_hit hit) {
  if (source >= 0 && rt->host.settings(rt->host.ctx).tracer_chance > 0) {
    qa_vec3 start;
    if (muzzle_point(rt, source, &start)) {
      double a = rt->host.collision_point_contents(rt->host.prediction_ctx, start);
      double b = rt->host.collision_point_contents(rt->host.prediction_ctx, end);
      if (a == b && ((int64_t)a & CONTENTS_WATER) != 0) rt->host.bubble_trail(rt->host.effects_ctx, start, end, 32);
      else if (((int64_t)a & CONTENTS_WATER) != 0) {
        qa_vec3 water = rt->host.collision_trace_end(rt->host.prediction_ctx, end, start, CONTENTS_WATER);
        rt->host.bubble_trail(rt->host.effects_ctx, start, water, 32);
      } else if (((int64_t)b & CONTENTS_WATER) != 0) {
        qa_vec3 water = rt->host.collision_trace_end(rt->host.prediction_ctx, start, end, CONTENTS_WATER);
        rt->host.bubble_trail(rt->host.effects_ctx, water, end, 32);
      }
      if (rt->host.random(rt->host.random_ctx) < rt->host.settings(rt->host.ctx).tracer_chance)
        qa_q3_pres_client_weapon_runtime_tracer(rt, start, end);
    }
  }
  if (!hit.is_wall) rt->host.bleed(rt->host.effects_ctx, end, hit.entity_num);
  else
    qa_q3_pres_client_weapon_runtime_missile_hit_wall(rt, QA_Q3_WP_MACHINEGUN, 0, end, hit.normal,
                                                      QA_Q3_PRES_IMPACT_SOUND_DEFAULT);
}
void qa_q3_pres_client_weapon_runtime_tracer(qa_q3_pres_client_weapon_runtime *rt, qa_vec3 source,
                                             qa_vec3 destination) {
  qa_vec3 delta = qa_sub3(destination, source);
  double length = qa_length3(delta);
  qa_vec3 forward = qa_normalize3(delta);
  if (length < 100) return;
  qa_q3_pres_weapon_presentation_settings settings = rt->host.settings(rt->host.ctx);
  double begin = f32(50 + f32(rt->host.random(rt->host.random_ctx) * f32(length - 60)));
  double end = fmin(f32(begin + settings.tracer_length), length);
  qa_vec3 start = ma3(source, begin, forward), finish = ma3(source, end, forward);
  qa_axis axis = rt_state(rt)->refdef.view_axis;
  qa_vec3 right = qa_normalize3(ma3(qa_scale3(axis.vectors[1], qa_dot3(forward, axis.vectors[2])),
                                    -qa_dot3(forward, axis.vectors[1]), axis.vectors[2]));
  qa_q3_pres_ref_poly_vertex verts[4] = {
      {ma3(finish, settings.tracer_width, right), {0, 1}, {255, 255, 255, 255}},
      {ma3(finish, -settings.tracer_width, right), {1, 0}, {255, 255, 255, 255}},
      {ma3(start, -settings.tracer_width, right), {1, 1}, {255, 255, 255, 255}},
      {ma3(start, settings.tracer_width, right), {0, 0}, {255, 255, 255, 255}}};
  qa_q3_pres_ref_poly poly = {rt->host.media.shaders.tracer, verts, 4};
  rt->host.add_poly(rt->host.ctx, &poly);
  qa_vec3 midpoint = qa_scale3(qa_add3(start, finish), 0.5);
  rt->host.start_sound(rt->host.ctx, &midpoint, 1022, 0, rt->host.media.sounds.tracer);
}
void qa_q3_pres_client_weapon_runtime_add_player_weapon(qa_q3_pres_client_weapon_runtime *rt,
    const qa_q3_pres_ref_model_entity *parent, qa_q3_base_source_player_state *player_state,
    qa_q3_pres_client_entity *entity, qa_q3_base_team team) {
  (void)team;
  qa_q3_pres_client_weapon_info *weapon =
      qa_q3_pres_client_weapon_media_registry_require_weapon(rt->registry, player_state->weapon);
  qa_q3_pres_ref_model_entity gun = qa_q3_pres_create_model_entity(weapon->base.weapon_model);
  qa_q3_pres_position_entity_on_tag(&gun, parent, &parent->model, "tag_weapon");
  if (entity->muzzle_flash_time == rt_state(rt)->time) gun.shading.render_flags =
      (double)(qa_int32(gun.shading.render_flags) | QA_Q3_PRES_RF_MINLIGHT);
  qa_q3_pres_ref_entity entity_ref = qa_q3_pres_ref_entity_from_model(gun);
  rt->host.add_ref_entity(rt->host.ctx, &entity_ref);
}
void qa_q3_pres_client_weapon_runtime_add_view_weapon(qa_q3_pres_client_weapon_runtime *rt,
                                                      qa_q3_base_source_player_state *player_state) {
  qa_q3_pres_weapon_presentation_settings settings = rt->host.settings(rt->host.ctx);
  if (!settings.draw_gun) return;
  qa_q3_pres_client_weapon_info *weapon =
      qa_q3_pres_client_weapon_media_registry_require_weapon(rt->registry, player_state->weapon);
  qa_q3_pres_client_game_state *state = rt_state(rt);
  qa_q3_pres_ref_model_entity gun = qa_q3_pres_create_model_entity(weapon->base.weapon_model);
  gun.shading.render_flags = QA_Q3_PRES_RF_DEPTHHACK | QA_Q3_PRES_RF_FIRST_PERSON | QA_Q3_PRES_RF_MINLIGHT;
  gun.axis = state->refdef.view_axis;
  gun.origin = ma3(state->refdef.view_origin, f32(settings.gun_x), state->refdef.view_axis.vectors[0]);
  gun.origin = ma3(gun.origin, f32(settings.gun_y), state->refdef.view_axis.vectors[1]);
  gun.origin = ma3(gun.origin, f32(settings.gun_z), state->refdef.view_axis.vectors[2]);
  qa_q3_pres_ref_entity entity_ref = qa_q3_pres_ref_entity_from_model(gun);
  rt->host.add_ref_entity(rt->host.ctx, &entity_ref);
}
void qa_q3_pres_client_weapon_runtime_draw_weapon_select(qa_q3_pres_client_weapon_runtime *rt) {
  qa_q3_pres_client_game_state *state = rt_state(rt);
  qa_q3_pres_weapon_selection_drawing *drawing = &rt->host.drawing;
  qa_vec4 color;
  if (!drawing->fade_color(drawing->ctx, state->weapon_select_time, 2000, &color)) return;
  if (!state->snap) return;
  double weapons = qa_q3_base_player_state_slots_get(&state->snap->player_state.stats,
      qa_q3_base_stat_schema_for(state->product)->weapons);
  int count = 0;
  for (int i = 1; i < 16; i++) if (((int64_t)weapons & (1LL << i)) != 0) count++;
  double x = 320 - count * 20;
  for (int i = 1; i < 16; i++) {
    if (((int64_t)weapons & (1LL << i)) == 0) continue;
    qa_q3_pres_client_weapon_info *weapon = qa_q3_pres_client_weapon_media_registry_weapon(rt->registry, i);
    bool selected = i == (int)state->weapon_select;
    drawing->set_color(drawing->ctx, &color);
    drawing->draw_pic(drawing->ctx, x, 410, 32, 32, weapon->weapon_icon);
    if (selected) drawing->draw_pic(drawing->ctx, x - 4, 406, 40, 40, rt->host.media.shaders.select);
    if (weapon->item && weapon->item->pickup_name && selected) {
      double width = drawing->draw_string_length(drawing->ctx, weapon->item->pickup_name);
      drawing->draw_big_string_color(drawing->ctx, 320 - width * 8, 450, weapon->item->pickup_name, color);
    }
    x += 40;
  }
  drawing->set_color(drawing->ctx, NULL);
}
static void view_out_of_ammo(void *ctx) { qa_q3_pres_client_weapon_runtime_out_of_ammo_change(ctx); }
static void view_fire(void *ctx, qa_q3_pres_client_entity *e) { qa_q3_pres_client_weapon_runtime_fire_weapon(ctx, e); }
static void view_hit_player(void *ctx, double w, qa_vec3 o, qa_vec3 d, double e) {
  qa_q3_pres_client_weapon_runtime_missile_hit_player(ctx, w, o, d, e);
}
static void view_hit_wall(void *ctx, double w, double c, qa_vec3 o, qa_vec3 d, qa_q3_pres_impact_sound s) {
  qa_q3_pres_client_weapon_runtime_missile_hit_wall(ctx, w, c, o, d, s);
}
static void view_rail(void *ctx, double client_num, qa_vec3 start, qa_vec3 end) {
  qa_vec3 mutable_start = start;
  qa_q3_pres_client_weapon_runtime_rail_trail(ctx, client_num, &mutable_start, end);
}
static void view_bullet(void *ctx, qa_vec3 end, double source, qa_q3_pres_bullet_hit hit) {
  qa_q3_pres_client_weapon_runtime_bullet(ctx, end, source, hit);
}
static void view_shotgun(void *ctx, qa_q3_base_entity_state *state) {
  qa_q3_pres_client_weapon_runtime_shotgun_fire(ctx, state);
}
qa_q3_pres_client_weapon_runtime_view qa_q3_pres_client_weapon_runtime_as_view(
    qa_q3_pres_client_weapon_runtime *rt) {
  qa_q3_pres_client_weapon_runtime_view view = {rt, view_out_of_ammo, view_fire, view_hit_player, view_hit_wall,
                                                view_rail, view_bullet, view_shotgun};
  return view;
}
void qa_q3_pres_emit_weapon_impact(qa_q3_base_product product, qa_q3_pres_client_weapon_media_registry *registry,
    qa_q3_pres_client_weapon_host *host, double weapon, double client_num, qa_vec3 origin, qa_vec3 direction,
    qa_q3_pres_impact_sound sound_type) {
  qa_q3_pres_registered_weapon_effects *effects = &registry->effects;
  const qa_q3_pres_scene_shader *mark = NULL, *shader = NULL;
  qa_q3_pres_scene_model model = qa_q3_pres_default_model;
  qa_pcm_sound *sound = NULL;
  double radius = 32, light = 0, duration = 600;
  qa_vec3 light_color = {1, 1, 0};
  bool sprite = false;
  double impact_weapon = weapon;
  if (product == QA_Q3_BASE_PRODUCT_BASEQ3 &&
      (weapon == QA_Q3_WP_PROX_LAUNCHER || weapon == QA_Q3_WP_CHAINGUN)) impact_weapon = QA_Q3_WP_NONE;
  switch ((int)impact_weapon) {
    case QA_Q3_WP_NAILGUN:
      if (product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
        sound = sound_type == QA_Q3_PRES_IMPACT_SOUND_FLESH ? host->media.sounds.nail_hit_flesh
                : sound_type == QA_Q3_PRES_IMPACT_SOUND_METAL ? host->media.sounds.nail_hit_metal
                                                              : host->media.sounds.nail_hit;
        mark = host->media.shaders.hole_mark;
        radius = 12;
        break;
      }
      /* fall through */
    default:
    case QA_Q3_WP_LIGHTNING: {
      int32_t choice = (int32_t)host->rand(host->random_ctx) & 3;
      sound = effects->lightning_hit_sounds[choice < 2 ? 1 : choice == 2 ? 0 : 2];
      mark = host->media.shaders.hole_mark;
      radius = 12;
      break;
    }
    case QA_Q3_WP_PROX_LAUNCHER:
      model = host->media.models.dish_flash;
      shader = effects->grenade_explosion_shader;
      sound = host->media.sounds.prox_explosion;
      mark = host->media.shaders.burn_mark;
      radius = 64; light = 300; sprite = true;
      break;
    case QA_Q3_WP_GRENADE_LAUNCHER:
      model = host->media.models.dish_flash;
      shader = effects->grenade_explosion_shader;
      sound = host->media.sounds.rocket_explosion;
      mark = host->media.shaders.burn_mark;
      radius = 64; light = 300; sprite = true;
      break;
    case QA_Q3_WP_ROCKET_LAUNCHER:
      model = host->media.models.dish_flash;
      shader = effects->rocket_explosion_shader;
      sound = host->media.sounds.rocket_explosion;
      mark = host->media.shaders.burn_mark;
      radius = 64; light = 300; sprite = true; duration = 1000;
      light_color = (qa_vec3){1, 0.75, 0};
      if (!host->settings(host->ctx).old_rocket)
        host->explosion(host->particles_ctx, ma3(origin, 24, direction), qa_scale3(direction, 64), 1400, 20, 30);
      break;
    case QA_Q3_WP_RAILGUN:
      model = host->media.models.ring_flash;
      shader = effects->rail_explosion_shader;
      sound = host->media.sounds.plasma_explosion;
      mark = host->media.shaders.energy_mark;
      radius = 24;
      break;
    case QA_Q3_WP_PLASMAGUN:
      model = host->media.models.ring_flash;
      shader = effects->plasma_explosion_shader;
      sound = host->media.sounds.plasma_explosion;
      mark = host->media.shaders.energy_mark;
      radius = 16;
      break;
    case QA_Q3_WP_BFG:
      model = host->media.models.dish_flash;
      shader = effects->bfg_explosion_shader;
      sound = host->media.sounds.rocket_explosion;
      mark = host->media.shaders.burn_mark;
      radius = 32; sprite = true;
      break;
    case QA_Q3_WP_SHOTGUN:
      model = host->media.models.bullet_flash;
      shader = effects->bullet_explosion_shader;
      mark = host->media.shaders.bullet_mark;
      radius = 4;
      break;
    case QA_Q3_WP_CHAINGUN: {
      model = host->media.models.bullet_flash;
      sound = sound_type == QA_Q3_PRES_IMPACT_SOUND_FLESH ? host->media.sounds.chaingun_hit_flesh
              : sound_type == QA_Q3_PRES_IMPACT_SOUND_METAL ? host->media.sounds.chaingun_hit_metal
                                                            : host->media.sounds.chaingun_hit;
      mark = host->media.shaders.bullet_mark;
      int32_t choice = (int32_t)host->rand(host->random_ctx) & 3;
      sound = choice < 2 ? host->media.sounds.ricochet1 : choice == 2 ? host->media.sounds.ricochet2
                                                                        : host->media.sounds.ricochet3;
      radius = 8;
      break;
    }
    case QA_Q3_WP_MACHINEGUN: {
      model = host->media.models.bullet_flash;
      shader = effects->bullet_explosion_shader;
      mark = host->media.shaders.bullet_mark;
      int32_t choice = (int32_t)host->rand(host->random_ctx) & 3;
      sound = choice == 0 ? host->media.sounds.ricochet1 : choice == 1 ? host->media.sounds.ricochet2
                                                                        : host->media.sounds.ricochet3;
      radius = 8;
      break;
    }
  }
  if (sound) host->start_sound(host->ctx, &origin, 1022, 0, sound);
  if (model.kind != QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
    qa_q3_pres_explosion_options options = {origin, &direction, model, shader, duration, sprite};
    qa_q3_pres_local_entity *le = host->make_explosion(host->effects_ctx, options);
    if (le) {
      le->light = light;
      le->light_color = light_color;
      if (weapon == QA_Q3_WP_RAILGUN) {
        qa_vec3 color = host->client_info(host->ctx, client_num)->color1;
        le->color = (qa_vec4){color.x, color.y, color.z, le->color.w};
      }
    }
  }
  qa_vec3 color = weapon == QA_Q3_WP_RAILGUN ? host->client_info(host->ctx, client_num)->color2
                                              : (qa_vec3){1, 1, 1};
  qa_q3_pres_impact_mark_request request = {mark, origin, direction,
      f32(host->random(host->random_ctx) * 360), {color.x, color.y, color.z, 1},
      mark == host->media.shaders.energy_mark, radius, false};
  qa_q3_pres_ref_poly *polys = NULL;
  size_t poly_count = 0;
  if (host->impact_mark(host->marks_ctx, request, &polys, &poly_count)) {
    for (size_t i = 0; i < poly_count; i++) qa_q3_pres_ref_poly_destroy(&polys[i]);
    free(polys);
  }
}
void qa_q3_pres_emit_rail_trail(double time, qa_q3_pres_client_weapon_media_registry *registry,
    qa_q3_pres_client_weapon_host *host, double client_num, qa_vec3 *start, qa_vec3 end) {
  qa_q3_pres_client_info *info = host->client_info(host->ctx, client_num);
  qa_q3_pres_weapon_presentation_settings settings = host->settings(host->ctx);
  start->z = f32(start->z - 4);
  qa_q3_pres_ref_entity ref = qa_q3_pres_ref_entity_from_rail(QA_Q3_PRES_REF_RAIL_CORE,
                                                              qa_q3_pres_create_rail_core_entity());
  qa_q3_pres_local_entity *le =
      qa_q3_pres_local_entity_pool_allocate(host->local_entities, QA_Q3_PRES_LE_FADE_RGB, ref);
  le->start_time = time;
  le->end_time = qa_qvm_float_to_int(f32(f32(time) + settings.rail_trail_time));
  le->life_rate = f32(1.0 / f32(qa_int32(le->end_time - time)));
  le->ref_entity.as.rail.shading.shader_time = f32(f32(time) / 1000);
  le->ref_entity.as.rail.shading.custom_shader = registry->effects.rail_core_shader;
  le->ref_entity.as.rail.origin = *start;
  le->ref_entity.as.rail.old_origin = end;
  le->ref_entity.as.rail.shading.shader_rgba = (qa_vec4){
      (double)(qa_qvm_float_to_int(f32(info->color1.x * 255)) & 255),
      (double)(qa_qvm_float_to_int(f32(info->color1.y * 255)) & 255),
      (double)(qa_qvm_float_to_int(f32(info->color1.z * 255)) & 255), 255};
  le->color = (qa_vec4){f32(info->color1.x), f32(info->color1.y), f32(info->color1.z), 1};
  if (settings.old_rail) return;
  /* Spiral rings: one fade-rgb rings entity per 16 units (cg_weapons). */
  qa_vec3 delta = qa_sub3(end, *start);
  double length = qa_length3(delta);
  qa_vec3 direction = qa_normalize3(delta);
  qa_vec3 temp = qa_perpendicular_vector(direction);
  double distance = 0;
  int index = 0;
  while (distance < length) {
    qa_vec3 point = ma3(*start, distance, direction);
    qa_vec3 axis_point = qa_qvm_rotate_point_around_vector(direction, temp, index * 10.0);
    qa_q3_pres_ref_entity rings = qa_q3_pres_ref_entity_from_rail(QA_Q3_PRES_REF_RAIL_RINGS,
                                                                  qa_q3_pres_create_rail_rings_entity());
    qa_q3_pres_local_entity *ring =
        qa_q3_pres_local_entity_pool_allocate(host->local_entities, QA_Q3_PRES_LE_FADE_RGB, rings);
    ring->start_time = time;
    ring->end_time = le->end_time;
    ring->life_rate = le->life_rate;
    ring->ref_entity.as.rail.origin = ma3(point, 8, axis_point);
    ring->ref_entity.as.rail.old_origin = point;
    ring->ref_entity.as.rail.shading.custom_shader = registry->effects.rail_rings_shader;
    ring->ref_entity.as.rail.shading.shader_rgba = (qa_vec4){
        (double)(qa_qvm_float_to_int(f32(info->color2.x * 255)) & 255),
        (double)(qa_qvm_float_to_int(f32(info->color2.y * 255)) & 255),
        (double)(qa_qvm_float_to_int(f32(info->color2.z * 255)) & 255), 255};
    ring->color = (qa_vec4){f32(info->color2.x), f32(info->color2.y), f32(info->color2.z), 1};
    distance += 16;
    index = (index + 1) % 36;
  }
}
void qa_q3_pres_emit_plasma_trail(double time, qa_vec3 origin, qa_vec3 angles, double weapon,
    qa_q3_pres_client_weapon_media_registry *registry, qa_q3_pres_client_weapon_host *host) {
  (void)angles;
  (void)weapon;
  if (host->settings(host->ctx).old_plasma || host->settings(host->ctx).no_projectile_trail) return;
  qa_q3_pres_smoke_puff_options options = {origin, {0, 0, 0}, 16, {1, 1, 1, 0.33}, 350, time, 0, 0,
                                           registry->effects.plasma_explosion_shader};
  qa_q3_pres_local_entity *smoke = host->smoke_puff(host->effects_ctx, options);
  if (smoke) smoke->le_type = QA_Q3_PRES_LE_SCALE_FADE;
}
