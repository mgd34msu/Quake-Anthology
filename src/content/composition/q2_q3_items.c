/* Port of src/content/composition/q2-q3-items.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q2_q3_items.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/composition/q3_q2_supply.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

#include "q2_q3_items_data.h"

/* One pickup closure: the services plus the Q3 row (tag, quantity, supply items). */
typedef struct foreign_pickup {
  const qa_q2_foreign_item_services *services;
  const q3_foreign_row *row;
} foreign_pickup;

static const qa_q2_item_definition *native_item(const qa_q2_foreign_item_services *s, qa_item_id destination) {
  return strncmp(destination, "q2:", 3) == 0 ? s->item(s->ctx, destination + 3) : NULL;
}

static bool ammo_pickup(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_owned_actor player) {
  const foreign_pickup *fc = ctx;
  const qa_q2_foreign_item_services *s = fc->services;
  qa_item_id source = fc->row->weapon_ammo, destination = NULL;
  for (size_t i = 0; i < QA_COMPOSITION_Q3_Q2_SUPPLY_PROFILE.ammo_count; i++)
    if (strcmp(QA_COMPOSITION_Q3_Q2_SUPPLY_PROFILE.ammo[i].source, source) == 0) {
      if (QA_COMPOSITION_Q3_Q2_SUPPLY_PROFILE.ammo[i].destination_count > 0) destination = QA_COMPOSITION_Q3_Q2_SUPPLY_PROFILE.ammo[i].destinations[0];
      break;
    }
  if (destination == NULL) return false;
  const qa_q2_item_definition *native = native_item(s, destination);
  s->ensure(s->ctx, player, g, destination, native != NULL && strcmp(native->kind, "ammo") == 0 ? native->capacity : 200);
  return qa_shared_inventory_table_give(g->host->inventory, player, destination, e->count != 0 ? e->count : fc->row->quantity) != 0;
}

static bool weapon_pickup(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_owned_actor player) {
  const foreign_pickup *fc = ctx;
  const qa_q2_foreign_item_services *s = fc->services;
  qa_item_id source = fc->row->weapon_item, destination = NULL;
  for (size_t i = 0; i < QA_COMPOSITION_Q3_Q2_SUPPLY_PROFILE.weapon_count; i++)
    if (strcmp(QA_COMPOSITION_Q3_Q2_SUPPLY_PROFILE.weapons[i].source, source) == 0) {
      if (QA_COMPOSITION_Q3_Q2_SUPPLY_PROFILE.weapons[i].destination_count > 0) destination = QA_COMPOSITION_Q3_Q2_SUPPLY_PROFILE.weapons[i].destinations[0];
      break;
    }
  if (destination == NULL) return false;
  const qa_q2_item_definition *native = native_item(s, destination);
  if (native == NULL || strcmp(native->kind, "weapon") != 0) return false;
  double previous = s->weapon_owned(s->ctx, g, player.id, native);
  s->ensure(s->ctx, player, g, destination, 32767);
  qa_shared_inventory_table_give(g->host->inventory, player, destination, 1);
  double ammo_given = 0;
  qa_pickup_ammo_grant grants[1];
  size_t grant_count = s->weapon_ammo(s->ctx, e, g, native, grants);
  for (size_t i = 0; i < grant_count; i++) {
    const qa_q2_item_definition *ammo = native_item(s, grants[i].item);
    if (ammo == NULL || strcmp(ammo->kind, "ammo") != 0) continue;
    s->ensure(s->ctx, player, g, grants[i].item, ammo->capacity);
    ammo_given += qa_shared_inventory_table_give(g->host->inventory, player, grants[i].item, e->count != 0 ? e->count : grants[i].amount);
  }
  if (previous == 0) s->weapon_picked(s->ctx, player.id, destination, true);
  return previous == 0 || ammo_given > 0;
}

/* Q3 Powerup tags: PW_QUAD 1, PW_BATTLESUIT 2, PW_HASTE 3, PW_INVIS 4, PW_REGEN 5,
 * PW_FLIGHT 6, PW_SCOUT 10, PW_GUARD 11, PW_DOUBLER 12, PW_AMMOREGEN 13,
 * PW_INVULNERABILITY 14; IT_PERSISTANT_POWERUP is item type 7. */
static bool powerup_pickup(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_owned_actor player) {
  (void)e;
  const foreign_pickup *fc = ctx;
  const qa_q2_foreign_item_services *s = fc->services;
  int tag = fc->row->tag;
  bool persistent = fc->row->type == 7;
  double now = g->host->now(g->host->ctx);
  qa_q2_player_powerups *state = s->powers(s->ctx, player.id);
  double duration = persistent ? 3600 : 30;
  switch (tag) {
    case 1: case 12: state->quad_until = qa_js_max(state->quad_until, now) + duration; return true;
    case 2: state->enviro_until = qa_js_max(state->enviro_until, now) + duration; return true;
    case 14: {
      state->invulnerability_until = qa_js_max(state->invulnerability_until, now) + duration;
      qa_combat_trait_changes tc = {0}; tc.has_invulnerable = true; tc.invulnerable = true;
      qa_gameplay_authority_set_traits(g->host->combat, player, tc);
      return true;
    }
    case 3: case 10: state->haste_until = qa_js_max(state->haste_until, now) + duration; return true;
    case 4: state->invis_until = qa_js_max(state->invis_until, now) + duration; return true;
    case 6: state->flight_until = qa_js_max(state->flight_until, now) + duration; return true;
    case 5: case 11: {
      state->regen_until = qa_js_max(state->regen_until, now) + duration;
      if (tag == 11) {
        qa_combat_state_result combat = qa_gameplay_authority_read(g->host->combat, player.id);
        if ((combat.has_value ? combat.value.health : 0) < 100) qa_gameplay_authority_set_health(g->host->combat, player, 100);
      }
      qa_q2_entity *timer = g->create(g->ctx, "regen_timer", NULL, 0);
      timer->has_owner = true; timer->owner = player.id;
      g->schedule(g->ctx, timer, 1, s->regen_think);
      return true;
    }
    case 13: {
      size_t count = 0;
      const qa_q2_item_definition *ammunition = s->ammunition(s->ctx, &count);
      for (size_t i = 0; i < count; i++) {
        if (strcmp(ammunition[i].kind, "ammo") != 0) continue;
        qa_item_id ammo = s->item_id(s->ctx, &ammunition[i]);
        s->ensure(s->ctx, player, g, ammo, ammunition[i].capacity);
        qa_shared_inventory_table_give(g->host->inventory, player, ammo, 999);
      }
      return true;
    }
    default: return false;
  }
}

/* Q3 Holdable tags: HI_TELEPORTER 1, HI_MEDKIT 2, HI_KAMIKAZE 3, HI_PORTAL 4, HI_INVULNERABILITY 5. */
static bool holdable_pickup(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_owned_actor player) {
  (void)e;
  const foreign_pickup *fc = ctx;
  const qa_q2_foreign_item_services *s = fc->services;
  qa_q2_entity *player_entity = g->entity(g->ctx, &player.id);
  switch (fc->row->tag) {
    case 2: {
      qa_combat_state_result combat = qa_gameplay_authority_read(g->host->combat, player.id);
      double health = combat.has_value ? combat.value.health : 0;
      double maximum = player_entity != NULL && player_entity->max_health != 0 ? player_entity->max_health : 100;
      if (health < 1 || health >= maximum) return false;
      qa_gameplay_authority_set_health(g->host->combat, player, maximum);
      return true;
    }
    case 5: {
      double now = g->host->now(g->host->ctx);
      qa_q2_player_powerups *state = s->powers(s->ctx, player.id);
      state->invulnerability_until = qa_js_max(state->invulnerability_until, now) + 30;
      qa_combat_trait_changes tc = {0}; tc.has_invulnerable = true; tc.invulnerable = true;
      qa_gameplay_authority_set_traits(g->host->combat, player, tc);
      return true;
    }
    case 3: {
      if (player_entity == NULL) return false;
      g->radius_damage(g->ctx, player_entity, &player.id, 300, NULL, 300, 27, 0, NULL);
      qa_body_state body = qa_q2_game_body(g, player_entity);
      g->damage(g->ctx, player.id, player_entity, player_entity->actor.id, &player.id, 10000, 0, QA_Q2_ZERO, body.origin, QA_Q2_ZERO, 0, 0, NULL);
      return true;
    }
    case 1: case 4: {
      size_t n = 0, spot_count = 0;
      qa_q2_entity *const *entities = g->entities(g->ctx, &n);
      for (size_t i = 0; i < n; i++)
        if (strcmp(entities[i]->classname, "info_player_deathmatch") == 0 || strcmp(entities[i]->classname, "info_player_start") == 0) spot_count++;
      /* spots[Math.floor(random() * spots.length)]: random() is drawn even with no spots. */
      double pick = floor(g->host->random(g->host->ctx) * (double)spot_count);
      qa_q2_entity *spot = NULL;
      if (spot_count > 0 && pick >= 0 && pick < (double)spot_count) {
        size_t index = (size_t)pick;
        for (size_t i = 0; i < n; i++) {
          if (strcmp(entities[i]->classname, "info_player_deathmatch") != 0 && strcmp(entities[i]->classname, "info_player_start") != 0) continue;
          if (index == 0) { spot = entities[i]; break; }
          index--;
        }
      }
      qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, player.id);
      if (spot == NULL || !body.has_value) return false;
      qa_shared_body_table_unlink(g->host->bodies, player);
      qa_body_state next = body.value;
      next.origin = qa_q2_game_body(g, spot).origin; next.velocity = QA_Q2_ZERO; next.ground = NULL;
      qa_shared_body_table_write(g->host->bodies, player, next);
      qa_shared_body_table_link(g->host->bodies, player, NULL);
      return true;
    }
    default: return false;
  }
}

static bool team_pickup(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_owned_actor player) {
  (void)ctx; (void)e; (void)g; (void)player;
  return false;
}

qa_q2_foreign_item_list qa_composition_q3_foreign_item_definitions(void *ctx, const qa_q2_foreign_item_services *services) {
  (void)ctx;
  qa_q2_foreign_item_list list = {0};
  list.items = calloc(Q3_FOREIGN_COUNT, sizeof *list.items);
  foreign_pickup *pickups = calloc(Q3_FOREIGN_COUNT, sizeof *pickups);
  if (list.items == NULL || pickups == NULL) qa_range_error("Out of memory");
  list.storage = pickups;
  /* Q3 item types: IT_WEAPON 1, IT_AMMO 2, IT_ARMOR 3, IT_HEALTH 4, IT_POWERUP 5,
   * IT_HOLDABLE 6, IT_PERSISTANT_POWERUP 7, IT_TEAM 8 (IT_BAD rows are not in the table). */
  for (size_t i = 0; i < Q3_FOREIGN_COUNT; i++) {
    const q3_foreign_row *row = &Q3_FOREIGN[i];
    pickups[i] = (foreign_pickup){services, row};
    qa_q2_item_definition visual = {0};
    visual.classname = row->classname; visual.model = row->model; visual.icon = "i_fixme";
    visual.name = row->name; visual.sound = row->sound; visual.rotate = true;
    double quantity = row->quantity;
    switch (row->type) {
      case 4:
        visual.kind = "health"; visual.respawn = 35; visual.amount = quantity;
        visual.ignore_maximum = strcmp(row->classname, "item_health_mega") == 0 || strcmp(row->classname, "item_health_small") == 0;
        visual.timed = strcmp(row->classname, "item_health_mega") == 0;
        break;
      case 3:
        if (strcmp(row->classname, "item_armor_shard") == 0) { visual.kind = "shard"; visual.respawn = 25; }
        else {
          visual.kind = "armor"; visual.respawn = 25; visual.points = quantity; visual.maximum = 200;
          visual.normal = quantity >= 100 ? 0.8 : 0.6; visual.energy = 0.3;
        }
        break;
      case 2:
        if (row->weapon_item == NULL || row->weapon_ammo == NULL) continue;
        visual.kind = "custom"; visual.respawn = 40; visual.capacity = 200; visual.quantity = quantity;
        visual.custom_ctx = &pickups[i]; visual.pickup = ammo_pickup;
        break;
      case 1:
        if (row->weapon_item == NULL) continue;
        visual.kind = "custom"; visual.respawn = 30; visual.capacity = 32767; visual.quantity = quantity;
        visual.has_coop_stay = true; visual.coop_stay = true;
        visual.custom_ctx = &pickups[i]; visual.pickup = weapon_pickup;
        break;
      case 5: case 7:
        visual.kind = "custom"; visual.respawn = row->type == 7 ? 0 : 120; visual.capacity = 32767; visual.quantity = 1;
        visual.custom_ctx = &pickups[i]; visual.pickup = powerup_pickup;
        break;
      case 6:
        visual.kind = "custom"; visual.respawn = 60; visual.capacity = 32767; visual.quantity = 1;
        visual.custom_ctx = &pickups[i]; visual.pickup = holdable_pickup;
        break;
      case 8:
        visual.kind = "custom"; visual.respawn = 0; visual.capacity = 32767; visual.quantity = 1;
        visual.custom_ctx = &pickups[i]; visual.pickup = team_pickup;
        break;
      default: continue;
    }
    list.items[list.count++] = visual;
  }
  return list;
}
