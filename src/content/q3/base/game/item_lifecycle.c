/* Port of src/content/q3/base/game/item-lifecycle.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/item_lifecycle.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/game/ground.h"
#include "qa/content/q3/base/game/utilities.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"

enum {
  CONTENTS_SOLID = 0x1,
  ITEM_RADIUS = 15,
  EF_NODRAW = 0x80,
};
#define CONTENTS_TRIGGER 0x40000000

void qa_q3_item_registry_init(qa_q3_item_registry *registry, qa_q3_base_product product) {
  size_t count = 0;
  registry->product = product;
  registry->items = qa_q3_base_item_list(product, &count); registry->count = count;
  free(registry->registered);
  registry->registered = calloc(count ? count : 1, 1);
  if (registry->registered == NULL) qa_range_error("out of memory");
}
void qa_q3_item_registry_destroy(qa_q3_item_registry *registry) {
  free(registry->registered);
  registry->registered = NULL; registry->items = NULL; registry->count = 0;
}
/* TS constructor always allocates; struct copies before init stay zeroed. */
static uint8_t *registry_bytes(qa_q3_item_registry *registry) {
  if (registry->registered == NULL) {
    registry->registered = calloc(registry->count ? registry->count : 1, 1);
    if (registry->registered == NULL) qa_range_error("out of memory");
  }
  return registry->registered;
}
static long registry_index(qa_q3_item_registry *registry,
                           const qa_q3_base_item_definition *item) {
  for (size_t i = 0; i < registry->count; i++)
    if (&registry->items[i] == item) return (long)i;
  return -1;
}
void qa_q3_item_registry_register(qa_q3_item_registry *registry,
                                  const qa_q3_base_item_definition *item) {
  long index = registry_index(registry, item);
  if (index < 0)
    qa_range_error("Registered item does not belong to %s",
                   qa_q3_base_product_name(registry->product));
  registry_bytes(registry)[(size_t)index] = 1;
}
bool qa_q3_item_registry_is_registered(qa_q3_item_registry *registry,
                                       const qa_q3_base_item_definition *item) {
  long index = registry_index(registry, item);
  if (index < 0)
    qa_range_error("Registered item does not belong to %s",
                   qa_q3_base_product_name(registry->product));
  return registry_bytes(registry)[(size_t)index] != 0;
}
static bool folded_equal(const char *a, const char *b) {
  for (;; a++, b++) {
    unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
    if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
    if (ca != cb) return false;
    if (ca == 0) return true;
  }
}
static const qa_q3_base_item_definition *registry_find_for_weapon(
    qa_q3_item_registry *registry, double weapon) {
  for (size_t i = 0; i < registry->count; i++)
    if (registry->items[i].type == QA_Q3_BASE_IT_WEAPON && registry->items[i].tag == weapon)
      return &registry->items[i];
  qa_range_error("Couldn't find item for weapon %d", qa_js_to_int32(weapon));
}
static const qa_q3_base_item_definition *registry_find_item(
    qa_q3_item_registry *registry, const char *pickup_name) {
  for (size_t i = 0; i < registry->count; i++)
    if (registry->items[i].pickup_name != NULL
        && folded_equal(registry->items[i].pickup_name, pickup_name))
      return &registry->items[i];
  return NULL;
}
void qa_q3_item_registry_clear(qa_q3_item_registry *registry, double game_type) {
  memset(registry_bytes(registry), 0, registry->count);
  qa_q3_item_registry_register(registry, registry_find_for_weapon(registry, QA_Q3_WP_MACHINEGUN));
  qa_q3_item_registry_register(registry, registry_find_for_weapon(registry, QA_Q3_WP_GAUNTLET));
  if (registry->product != QA_Q3_BASE_PRODUCT_MISSIONPACK
      || game_type != QA_Q3_BASE_GT_HARVESTER) return;
  static const char *const cubes[] = {"Red Cube", "Blue Cube"};
  for (size_t i = 0; i < sizeof cubes / sizeof *cubes; i++) {
    const qa_q3_base_item_definition *item = registry_find_item(registry, cubes[i]);
    if (item == NULL) qa_range_error("Required item %s is missing", cubes[i]);
    qa_q3_item_registry_register(registry, item);
  }
}
double qa_q3_item_registry_save(qa_q3_item_registry *registry, void *ctx,
    void (*set_configstring)(void *ctx, double index, const char *value),
    void (*log)(void *ctx, const char *message)) {
  char *value = malloc(registry->count + 1);
  if (value == NULL) qa_range_error("out of memory");
  const uint8_t *bytes = registry_bytes(registry);
  double count = 0;
  for (size_t i = 0; i < registry->count; i++) {
    if (bytes[i] != 0) count++;
    value[i] = bytes[i] != 0 ? '1' : '0';
  }
  value[registry->count] = '\0';
  char message[64];
  (void)snprintf(message, sizeof message, "%lld items registered\n", (long long)count);
  log(ctx, message);
  set_configstring(ctx, 27, value);
  free(value);
  return count;
}
qa_save_value *qa_q3_item_registry_capture_save_state(qa_q3_item_registry *registry) {
  return qa_save_value_bytes(registry_bytes(registry), registry->count);
}
void qa_q3_item_registry_restore_save_state(qa_q3_item_registry *registry,
                                            const qa_save_value *value) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "q3.items")) qa_range_error("out of memory");
  size_t length = 0;
  const uint8_t *bytes = qa_save_reader_bytes(&reader, &length);
  bool valid = length == registry->count;
  for (size_t i = 0; valid && i < length; i++) valid = bytes[i] == 0 || bytes[i] == 1;
  if (!valid) qa_save_reader_fail(&reader, "invalid registered item table");
  memcpy(registry_bytes(registry), bytes, length);
  qa_save_reader_destroy(&reader);
}

static float f32(double v) { return (float)v; }

static double game_time(qa_q3_item_lifecycle_context context) {
  double time = context.entities->options.time(context.entities->options.ctx);
  if (time != trunc(time) || time < -2147483648 || time > 2147483647)
    qa_range_error("Item lifecycle time must be a signed 32-bit millisecond value");
  return time;
}

static void check_context(qa_q3_item_lifecycle_context context) {
  if (context.entities == NULL || context.entities->options.product != context.product
      || context.registry.product != context.product)
    qa_range_error("Item lifecycle product does not match its entity pool and registry");
}

static void require_owned(qa_q3_item_lifecycle_context context, qa_q3_game_entity *entity) {
  if (qa_q3_entity_pool_get(context.entities, entity->slot) != entity)
    qa_range_error("Item lifecycle entity does not belong to its entity pool or was replaced");
}

static double find_index(qa_q3_item_lifecycle_context context,
                         const qa_q3_base_item_definition *item) {
  for (size_t i = 0; i < context.registry.count; i++)
    if (&context.registry.items[i] == item) return (double)i;
  return -1;
}

static const qa_q3_base_item_definition *require_item(
    qa_q3_item_lifecycle_context context, qa_q3_game_entity *entity) {
  const qa_q3_base_item_definition *item = entity->item;
  if (item == NULL || find_index(context, item) < 1)
    qa_range_error("Item entity does not contain a %s item definition",
                   qa_q3_base_product_name(context.product));
  return item;
}

static double table_index(qa_q3_item_lifecycle_context context,
                          const qa_q3_base_item_definition *item) {
  double index = find_index(context, item);
  if (index < 1)
    qa_range_error("Item does not belong to the %s item table",
                   qa_q3_base_product_name(context.product));
  return index;
}

static const qa_q3_base_item_definition *require_published_item(
    qa_q3_item_lifecycle_context context, qa_q3_game_entity *entity) {
  const qa_q3_base_item_definition *item = require_item(context, entity);
  if (entity->s.modelindex != table_index(context, item))
    qa_range_error("Item entity model index does not match its item definition");
  return item;
}

static void pool_link(qa_q3_item_lifecycle_context context, qa_q3_game_entity *entity) {
  if (context.entities->options.link)
    context.entities->options.link(context.entities->options.ctx, entity);
}

static double unit_random(qa_q3_item_lifecycle_context context) {
  double value = context.random(context.ctx);
  if (!isfinite(value) || value < 0 || value > 1)
    qa_range_error("Game random() must return a value within [0, 1]");
  return f32(value);
}

static double crandom(qa_q3_item_lifecycle_context context) {
  return f32(2 * (double)f32(unit_random(context) - 0.5));
}

static double random_integer(qa_q3_item_lifecycle_context context) {
  double value = context.rand_value(context.ctx);
  if (value != trunc(value) || !isfinite(value) || value < 0 || value > 9007199254740991.0)
    qa_range_error("Game rand() must return a nonnegative safe integer");
  return value;
}

static qa_q3_game_entity *team_member(qa_q3_item_lifecycle_context context,
                                      qa_q3_game_entity *entity) {
  if (entity->team == NULL) return entity;
  qa_q3_game_entity *master = entity->teammaster;
  if (master == NULL) qa_range_error("RespawnItem: bad teammaster");
  double count = 0;
  qa_q3_game_entity *cursor = master;
  while (cursor != NULL) {
    require_owned(context, cursor);
    count++;
    if (count > context.entities->num_entities) qa_range_error("RespawnItem: cyclic teamchain");
    cursor = cursor->teamchain;
  }
  double choice = (double)((int64_t)random_integer(context) % (int64_t)count);
  cursor = master;
  for (double index = 0; index < choice; index++) {
    if (cursor == NULL) qa_range_error("RespawnItem: broken teamchain");
    cursor = cursor->teamchain;
  }
  if (cursor == NULL) qa_range_error("RespawnItem: broken teamchain");
  return cursor;
}

static void spawn_respawn_sound(qa_q3_item_lifecycle_context context,
                                qa_q3_game_entity *entity, const char *path) {
  double event = entity->speed != 0 ? QA_Q3_EV_GENERAL_SOUND : QA_Q3_EV_GLOBAL_SOUND;
  qa_q3_game_entity *temporary =
      qa_q3_entity_pool_temp_entity(context.entities, entity->s.pos.base, event);
  temporary->s.event_parm = context.sound_index(context.ctx, path);
  temporary->r.sv_flags = (double)(qa_int32(temporary->r.sv_flags) | QA_Q3_BASE_SVF_BROADCAST);
}

void qa_q3_respawn_item(qa_q3_game_entity *entity, qa_q3_item_lifecycle_context context) {
  check_context(context);
  require_owned(context, entity);
  qa_q3_game_entity *selected = team_member(context, entity);
  const qa_q3_base_item_definition *item = require_published_item(context, selected);
  selected->r.contents = CONTENTS_TRIGGER;
  selected->s.e_flags = (double)(qa_int32(selected->s.e_flags) & ~EF_NODRAW);
  selected->r.sv_flags = (double)(qa_int32(selected->r.sv_flags) & ~QA_Q3_BASE_SVF_NOCLIENT);
  pool_link(context, selected);
  if (item->type == QA_Q3_BASE_IT_POWERUP)
    spawn_respawn_sound(context, selected, "sound/items/poweruprespawn.wav");
  if (item->type == QA_Q3_BASE_IT_HOLDABLE && item->tag == QA_Q3_HI_KAMIKAZE)
    spawn_respawn_sound(context, selected, "sound/items/kamikazerespawn.wav");
  qa_q3_entity_pool_add_event(context.entities, selected, QA_Q3_EV_ITEM_RESPAWN, 0);
  qa_q3_game_entity_set_nextthink(selected, 0);
}

static qa_q3_source_pickup_descriptor pickup_descriptor(
    qa_q3_game_entity *entity, qa_q3_game_entity *recipient,
    const qa_q3_base_item_definition *item, qa_q3_item_lifecycle_context context) {
  qa_q3_source_pickup_descriptor descriptor;
  descriptor.item_actor = qa_q3_game_entity_actor(entity).id;
  descriptor.player_actor = qa_q3_game_entity_actor(recipient).id;
  descriptor.item = item;
  descriptor.count = entity->count;
  descriptor.generic1 = entity->s.generic1;
  descriptor.dropped = (qa_int32(entity->flags) & QA_Q3_GAME_FLAG_DROPPED_ITEM) != 0;
  descriptor.game_type = context.game_type;
  descriptor.weapon_respawn_seconds = context.weapon_respawn_seconds;
  descriptor.team_weapon_respawn_seconds = context.team_weapon_respawn_seconds;
  return descriptor;
}

static bool callback_equal(qa_q3_entity_callback a, qa_q3_entity_callback b) {
  return a.fn == b.fn && a.ctx == b.ctx;
}

bool qa_q3_observe_q3_supply(qa_q3_game_entity *entity, qa_q3_game_entity *recipient,
    qa_q3_item_lifecycle_context context, qa_q3_supply_observation *out) {
  if (context.product != QA_Q3_BASE_PRODUCT_BASEQ3
      || !qa_q3_game_entity_inuse(entity) || !qa_q3_game_entity_inuse(recipient)
      || qa_q3_entity_pool_get(context.entities, entity->slot) != entity
      || qa_q3_entity_pool_get(context.entities, recipient->slot) != recipient
      || recipient->client == NULL || !context.has_callbacks
      || !callback_equal(entity->touch, context.touch_callback)
      || entity->item == NULL
      || (entity->item->type != QA_Q3_BASE_IT_WEAPON
          && entity->item->type != QA_Q3_BASE_IT_AMMO))
    return false;
  const qa_q3_base_item_definition *item = require_published_item(context, entity);
  if (context.preview_pickup == NULL) return false;
  qa_q3_source_pickup_preview supplied =
      context.preview_pickup(context.ctx, pickup_descriptor(entity, recipient, item, context));
  if (supplied.kind != QA_Q3_SOURCE_PREVIEW_SELECTED) return false;
  bool ready = (qa_int32(entity->r.contents) & (int32_t)CONTENTS_TRIGGER) != 0
      && (qa_int32(entity->s.e_flags) & EF_NODRAW) == 0
      && (qa_int32(entity->r.sv_flags) & QA_Q3_BASE_SVF_NOCLIENT) == 0
      && !entity->free_after_event && !entity->unlink_after_event;
  bool respawning = entity->team == NULL && entity->teammaster == NULL
      && entity->teamchain == NULL
      && (qa_int32(entity->flags) & (QA_Q3_GAME_FLAG_DROPPED_ITEM | QA_Q3_GAME_FLAG_TEAMSLAVE)) == 0
      && !entity->free_after_event && !entity->unlink_after_event
      && callback_equal(entity->think, context.respawn_callback)
      && entity->nextthink > 0;
  memset(out, 0, sizeof *out);
  out->observation.actor = qa_q3_game_entity_actor(entity).id;
  out->observation.offer = supplied.offer;
  if (ready) {
    out->observation.availability_kind = "ready";
    out->observation.eligible = qa_q3_game_entity_health(recipient) >= 1;
  } else if (respawning) {
    out->observation.availability_kind = "respawning";
    out->observation.at_seconds = entity->nextthink / 1000;
  } else {
    out->observation.availability_kind = "inactive";
  }
  out->preview = supplied.preview;
  return true;
}

static qa_q3_powerup_sight_trace trace_solid_line(void *ctx, qa_vec3 start, qa_vec3 end) {
  qa_q3_item_lifecycle_context *context = ctx;
  qa_q3_base_server_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = start; query.end = end;
  query.shape.kind = QA_Q3_BASE_SHAPE_POINT;
  query.mask = CONTENTS_SOLID;
  query.pass_entity_num = QA_Q3_BASE_ENTITYNUM_NONE;
  qa_q3_base_server_trace_result result = context->trace(context->ctx, query);
  qa_q3_powerup_sight_trace trace = {result.fraction};
  return trace;
}

static double inventory_ammo(void *ctx, qa_q3_weapon weapon) {
  qa_q3_base_player_state *ps = ctx;
  return qa_q3_base_player_state_slots_get(&ps->ammo, (size_t)weapon);
}
static double inventory_powerup(void *ctx, qa_q3_powerup powerup) {
  qa_q3_base_player_state *ps = ctx;
  return qa_q3_base_player_state_slots_get(&ps->powerups, (size_t)powerup);
}

/* TS q3ItemInventory. */
qa_q3_base_player_inventory qa_q3_item_inventory(qa_q3_game_client *client) {
  qa_q3_base_player_state *ps = &client->ps;
  qa_q3_base_product product =
      ps->state_product == QA_Q3_BASE_PRODUCT_MISSIONPACK ? QA_Q3_BASE_PRODUCT_MISSIONPACK
                                                    : QA_Q3_BASE_PRODUCT_BASEQ3;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(product);
  qa_q3_base_player_inventory inventory;
  memset(&inventory, 0, sizeof inventory);
  inventory.ctx = ps;
  inventory.product = product;
  inventory.health = qa_q3_base_player_state_slots_get(&ps->stats, (size_t)schema->health);
  inventory.armor = qa_q3_base_player_state_slots_get(&ps->stats, (size_t)schema->armor);
  inventory.max_health = qa_q3_base_player_state_slots_get(&ps->stats, (size_t)schema->max_health);
  inventory.holdable_item =
      qa_q3_base_player_state_slots_get(&ps->stats, (size_t)schema->holdable_item);
  inventory.team =
      qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_TEAM);
  inventory.ammo = inventory_ammo;
  inventory.powerup = inventory_powerup;
  if (product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    inventory.has_persistent_powerup_index = true;
    inventory.persistent_powerup_index = qa_q3_base_player_state_slots_get(
        &ps->stats, QA_Q3_BASE_MP_STAT_PERSISTANT_POWERUP);
  }
  return inventory;
}

/* TS touchItem's original/complete closures over this stack state. */
typedef struct touch_state {
  qa_q3_item_lifecycle_context context;
  qa_q3_game_entity *entity, *other;
  qa_q3_game_client *client;
  const qa_q3_base_item_definition *item;
  qa_q3_base_pickup_entity pickup_state;
  const char *class_name;
  qa_actor_id item_actor, player_actor;
  double now, respawn;
  bool predict, original_ran;
} touch_state;

static bool actor_equal(qa_actor_id a, qa_actor_id b) {
  return a.slot == b.slot && a.generation == b.generation && a.token == b.token
      && a.session.token == b.session.token;
}

static bool touch_live(touch_state *state) {
  return qa_q3_game_entity_inuse(state->entity) && qa_q3_game_entity_inuse(state->other)
      && qa_q3_entity_pool_get(state->context.entities, state->entity->slot) == state->entity
      && qa_q3_entity_pool_get(state->context.entities, state->other->slot) == state->other
      && actor_equal(qa_q3_game_entity_actor(state->entity).id, state->item_actor)
      && actor_equal(qa_q3_game_entity_actor(state->other).id, state->player_actor);
}

static void touch_log(touch_state *state) {
  qa_q3_game_format_arg args[2] = {qa_q3_game_format_number(state->other->s.number),
                                   qa_q3_game_format_string(state->class_name)};
  char *message = qa_q3_game_format("Item: %i %s\n", args, 2, 32000);
  if (message == NULL) qa_range_error("out of memory");
  state->context.log(state->context.ctx, message);
  free(message);
}

static bool touch_original(void *ctx) {
  touch_state *state = ctx;
  qa_q3_item_lifecycle_context context = state->context;
  qa_q3_game_entity *entity = state->entity, *other = state->other;
  const qa_q3_base_item_definition *item = state->item;
  state->original_ran = true;
  qa_q3_source_pickup_admission admission = {QA_Q3_SOURCE_PICKUP_NATIVE, 0};
  if (context.admit_pickup != NULL)
    admission = context.admit_pickup(context.ctx,
                                     pickup_descriptor(entity, other, item, context));
  if (!touch_live(state) || admission.kind == QA_Q3_SOURCE_PICKUP_REJECTED) return false;
  if (admission.kind == QA_Q3_SOURCE_PICKUP_NATIVE) {
    qa_q3_base_player_inventory inventory = qa_q3_item_inventory(state->client);
    if (!qa_q3_base_can_item_be_grabbed(context.game_type, state->pickup_state, &inventory))
      return false;
  }
  touch_log(state);
  if (!touch_live(state)) return false;
  if (admission.kind == QA_Q3_SOURCE_PICKUP_PICKED) {
    state->respawn = admission.respawn_seconds;
  } else if (item->type == QA_Q3_BASE_IT_TEAM) {
    state->respawn = context.team_pickup(context.ctx, entity, other);
  } else {
    qa_q3_item_pickup_context pickup;
    memset(&pickup, 0, sizeof pickup);
    pickup.weapon.game_type = context.game_type;
    pickup.weapon.weapon_respawn_seconds = context.weapon_respawn_seconds;
    pickup.weapon.team_weapon_respawn_seconds = context.team_weapon_respawn_seconds;
    pickup.powerup.time = state->now;
    pickup.powerup.game_type = context.game_type;
    pickup.powerup.clients = (qa_q3_game_client *const *)context.entities->clients;
    pickup.powerup.client_count = (size_t)context.entities->max_clients;
    pickup.powerup.trace_ctx = &state->context;
    pickup.powerup.trace_solid_line = trace_solid_line;
    pickup.handicap_ctx = context.ctx;
    pickup.handicap_for_client = context.handicap_for_client;
    state->respawn = qa_q3_pickup_item(entity, other, pickup);
    if (item->type == QA_Q3_BASE_IT_POWERUP) state->predict = false;
  }
  if (state->respawn == 0 || !touch_live(state)) return false;
  if (admission.kind == QA_Q3_SOURCE_PICKUP_NATIVE) {
    qa_q3_ranking_reports *reports = &context.entities->rankings;
    double quantity = entity->count != 0 ? entity->count : item->quantity;
    if (item->type == QA_Q3_BASE_IT_WEAPON)
      qa_q3_ranking_reports_pickup_weapon(reports, other->slot, item->tag);
    else if (item->type == QA_Q3_BASE_IT_AMMO)
      qa_q3_ranking_reports_pickup_ammo(reports, other->slot, item->tag, quantity);
    else if (item->type == QA_Q3_BASE_IT_HEALTH)
      qa_q3_ranking_reports_pickup_health(reports, other->slot, quantity);
    else if (item->type == QA_Q3_BASE_IT_ARMOR)
      qa_q3_ranking_reports_pickup_armor(reports, other->slot, item->quantity);
    else if (item->type == QA_Q3_BASE_IT_POWERUP)
      qa_q3_ranking_reports_pickup_powerup(reports, other->slot, item->tag);
    else if (item->type == QA_Q3_BASE_IT_HOLDABLE)
      qa_q3_ranking_reports_pickup_holdable(reports, other->slot, item->tag);
  }
  return true;
}

static void touch_complete(void *ctx, bool taken) {
  touch_state *state = ctx;
  qa_q3_item_lifecycle_context context = state->context;
  qa_q3_game_entity *entity = state->entity, *other = state->other;
  const qa_q3_base_item_definition *item = state->item;
  if (!taken || !touch_live(state)) return;
  if (!state->original_ran) {
    qa_q3_weapon_pickup_context weapon = {context.game_type, context.weapon_respawn_seconds,
                                          context.team_weapon_respawn_seconds};
    state->respawn = qa_q3_item_respawn_seconds(item, weapon);
    if (state->respawn == 0) return;
    if (item->type == QA_Q3_BASE_IT_POWERUP) state->predict = false;
    touch_log(state);
    if (!touch_live(state)) return;
  }
  if (state->predict)
    qa_q3_entity_pool_add_predictable_event(context.entities, other, QA_Q3_EV_ITEM_PICKUP,
                                            entity->s.modelindex, NULL);
  else
    qa_q3_entity_pool_add_event(context.entities, other, QA_Q3_EV_ITEM_PICKUP,
                                entity->s.modelindex);
  if (!touch_live(state)) return;
  if (item->type == QA_Q3_BASE_IT_POWERUP || item->type == QA_Q3_BASE_IT_TEAM) {
    qa_q3_game_entity *temporary = qa_q3_entity_pool_temp_entity(
        context.entities, entity->s.pos.base, QA_Q3_EV_GLOBAL_ITEM_PICKUP);
    if (!touch_live(state)) return;
    temporary->s.event_parm = entity->s.modelindex;
    if (entity->speed == 0) {
      temporary->r.sv_flags =
          (double)(qa_int32(temporary->r.sv_flags) | QA_Q3_BASE_SVF_BROADCAST);
    } else {
      temporary->r.sv_flags =
          (double)(qa_int32(temporary->r.sv_flags) | QA_Q3_BASE_SVF_SINGLECLIENT);
      temporary->r.single_client = other->s.number;
    }
  }
  context.use_targets(context.ctx, entity, other);
  if (!touch_live(state)) return;
  if (entity->wait == -1) {
    entity->r.sv_flags = (double)(qa_int32(entity->r.sv_flags) | QA_Q3_BASE_SVF_NOCLIENT);
    entity->s.e_flags = (double)(qa_int32(entity->s.e_flags) | EF_NODRAW);
    entity->r.contents = 0;
    entity->unlink_after_event = true;
    return;
  }
  double respawn = state->respawn;
  if (entity->wait != 0) respawn = qa_qvm_float_to_int(entity->wait);
  if (entity->random != 0) {
    respawn = qa_qvm_float_to_int(
        f32((double)f32(respawn) + (double)f32(crandom(context) * entity->random)));
    if (!touch_live(state)) return;
    if (respawn < 1) respawn = 1;
  }
  if ((qa_int32(entity->flags) & QA_Q3_GAME_FLAG_DROPPED_ITEM) != 0)
    entity->free_after_event = true;
  entity->r.sv_flags = (double)(qa_int32(entity->r.sv_flags) | QA_Q3_BASE_SVF_NOCLIENT);
  entity->s.e_flags = (double)(qa_int32(entity->s.e_flags) | EF_NODRAW);
  entity->r.contents = 0;
  if (respawn <= 0) {
    qa_q3_game_entity_set_nextthink(entity, 0);
    entity->think.fn = NULL; entity->think.ctx = NULL;
  } else {
    int32_t scaled = (int32_t)((uint32_t)qa_int32(respawn) * 1000u);
    qa_q3_game_entity_set_nextthink(entity, qa_int32(state->now + (double)scaled));
    entity->think = context.has_callbacks
        ? context.respawn_callback
        : qa_q3_callback_family_resolve(&context.entities->callbacks.think,
                                        "q3.base.game.item-lifecycle.touchItem.think");
  }
  pool_link(context, entity);
}

void qa_q3_touch_item(qa_q3_game_entity *entity, qa_q3_damage_participant other,
                      qa_touch_contact contact, qa_q3_item_lifecycle_context context) {
  (void)contact;
  qa_q3_bind_item_save_callbacks(context);
  check_context(context);
  require_owned(context, entity);
  if (other.entity == NULL) return;
  qa_q3_game_entity *other_entity = other.entity;
  require_owned(context, other_entity);
  qa_q3_game_client *client = other_entity->client;
  if (client == NULL || qa_q3_game_entity_health(other_entity) < 1) return;
  if (client->ps.state_product != context.product)
    qa_range_error("Item touch player product does not match lifecycle product");
  const qa_q3_base_item_definition *item = require_published_item(context, entity);
  touch_state state;
  memset(&state, 0, sizeof state);
  state.context = context;
  state.entity = entity; state.other = other_entity; state.client = client; state.item = item;
  state.pickup_state.model_index = entity->s.modelindex;
  state.pickup_state.model_index2 = entity->s.modelindex2;
  state.pickup_state.generic1 = entity->s.generic1;
  state.item_actor = qa_q3_game_entity_actor(entity).id;
  state.player_actor = qa_q3_game_entity_actor(other_entity).id;
  state.class_name = item->class_name;
  if (state.class_name == NULL) qa_range_error("Pickup item has no classname");
  state.now = game_time(context);
  state.predict = client->pers.predict_item_pickup;
  if (!context.has_original_pickups) {
    touch_complete(&state, touch_original(&state));
    return;
  }
  const qa_q3_weapon_item *weapon =
      item->type == QA_Q3_BASE_IT_WEAPON || item->type == QA_Q3_BASE_IT_AMMO
          ? qa_q3_get_weapon_item(item->tag) : NULL;
  char fallback[512];
  int written = snprintf(fallback, sizeof fallback, "q3:%s", state.class_name);
  if (written < 0 || (size_t)written >= sizeof fallback) qa_range_error("out of memory");
  qa_item_id offered = fallback;
  if (item->type == QA_Q3_BASE_IT_WEAPON && weapon != NULL && weapon->item != NULL)
    offered = weapon->item;
  else if (item->type == QA_Q3_BASE_IT_AMMO && weapon != NULL && weapon->ammo != NULL)
    offered = weapon->ammo;
  qa_pickup_resource resource;
  const qa_pickup_resource *default_resource = NULL;
  if (item->type == QA_Q3_BASE_IT_ARMOR) {
    resource.kind = QA_PICKUP_RESOURCE_PROTECTION;
    resource.channel = QA_PROTECTION_REGULAR;
    resource.item = NULL;
    default_resource = &resource;
  } else if (item->type == QA_Q3_BASE_IT_WEAPON || item->type == QA_Q3_BASE_IT_AMMO) {
    resource.kind = QA_PICKUP_RESOURCE_INVENTORY;
    resource.channel = QA_PROTECTION_REGULAR;
    resource.item = offered;
    default_resource = &resource;
  }
  qa_original_pickup_offer offer;
  memset(&offer, 0, sizeof offer);
  offer.recipient = state.player_actor;
  offer.pickup = state.item_actor;
  offer.source = qa_q3_game_entity_actor(entity).owner;
  offer.item = offered;
  offer.default_resource = default_resource;
  offer.count.is_override = entity->count != 0;
  offer.count.amount = entity->count;
  offer.dropped = (qa_int32(entity->flags) & QA_Q3_GAME_FLAG_DROPPED_ITEM) != 0;
  offer.time.kind = QA_SOURCE_TIME_MILLISECONDS;
  offer.time.value = state.now;
  if (item->type == QA_Q3_BASE_IT_TEAM) offer.grant = "map-coupled";
  qa_original_pickup_continuation continuation;
  continuation.ctx = &state;
  continuation.eligible = NULL;
  continuation.original = touch_original;
  continuation.complete = touch_complete;
  (void)context.original_pickups.touch(context.original_pickups.ctx, offer, continuation);
}

static double source_float_schedule(double time, double seconds) {
  float milliseconds = f32((double)f32(seconds) * 1000);
  return qa_qvm_float_to_int(f32((double)f32(time) + milliseconds));
}

void qa_q3_finish_spawning_item(qa_q3_game_entity *entity,
                                qa_q3_item_lifecycle_context context) {
  qa_q3_bind_item_save_callbacks(context);
  check_context(context);
  require_owned(context, entity);
  const qa_q3_base_item_definition *item = require_item(context, entity);
  qa_q3_base_entity_shared_set_mins(&entity->r, (qa_vec3){-ITEM_RADIUS, -ITEM_RADIUS, -ITEM_RADIUS});
  qa_q3_base_entity_shared_set_maxs(&entity->r, (qa_vec3){ITEM_RADIUS, ITEM_RADIUS, ITEM_RADIUS});
  entity->s.e_type = QA_Q3_BASE_ET_ITEM;
  entity->s.modelindex = table_index(context, item);
  entity->s.modelindex2 = 0;
  entity->r.contents = CONTENTS_TRIGGER;
  entity->touch = context.has_callbacks
      ? context.touch_callback
      : qa_q3_callback_family_resolve(&context.entities->callbacks.touch,
                                      "q3.base.game.item-lifecycle.finishSpawningItem.touch");
  entity->use = qa_q3_callback_family_resolve(
      &context.entities->callbacks.use, "q3.base.game.item-lifecycle.finishSpawningItem.use");
  if ((qa_int32(entity->spawnflags) & 1) != 0) {
    qa_q3_set_origin(entity, entity->s.origin);
  } else {
    qa_vec3 destination = {entity->s.origin.x, entity->s.origin.y,
                           entity->s.origin.z - 4096};
    qa_actor_id pass_actor = qa_q3_game_entity_actor(entity).id;
    qa_q3_base_actor_trace_query query;
    memset(&query, 0, sizeof query);
    query.start = entity->s.origin;
    query.end = destination;
    query.shape.kind = QA_Q3_BASE_SHAPE_BOX;
    query.shape.mins = qa_q3_base_entity_shared_mins(&entity->r);
    query.shape.maxs = qa_q3_base_entity_shared_maxs(&entity->r);
    query.pass_actor = &pass_actor;
    query.mask = CONTENTS_SOLID;
    qa_q3_base_actor_trace_result trace = context.trace_actor(context.ctx, query);
    if (trace.solidity != QA_Q3_BASE_SOLID_CLEAR) {
      const char *classname = qa_q3_game_entity_classname(entity);
      const char *origin_text =
          (const char *)qa_q3_game_utility_scratch_vtos(context.entities->utilities,
                                                        entity->s.origin);
      qa_q3_game_format_arg args[2] = {
          classname != NULL ? qa_q3_game_format_string(classname) : qa_q3_game_format_null(),
          qa_q3_game_format_string(origin_text)};
      char *message =
          qa_q3_game_format("FinishSpawningItem: %s startsolid at %s\n", args, 2, 32000);
      if (message == NULL) qa_range_error("out of memory");
      context.warn(context.ctx, message);
      free(message);
      qa_q3_entity_pool_free(context.entities, entity);
      return;
    }
    qa_q3_trace_ground(entity, trace.hit_kind, trace.hit_actor, context.entities);
    qa_q3_set_origin(entity, trace.end);
  }
  if ((qa_int32(entity->flags) & QA_Q3_GAME_FLAG_TEAMSLAVE) != 0
      || entity->targetname != NULL) {
    entity->s.e_flags = (double)(qa_int32(entity->s.e_flags) | EF_NODRAW);
    entity->r.contents = 0;
    return;
  }
  if (item->type == QA_Q3_BASE_IT_POWERUP) {
    double delay = f32(45 + (double)f32(crandom(context) * 15));
    entity->s.e_flags = (double)(qa_int32(entity->s.e_flags) | EF_NODRAW);
    entity->r.contents = 0;
    qa_q3_game_entity_set_nextthink(entity, source_float_schedule(game_time(context), delay));
    entity->think = context.has_callbacks
        ? context.respawn_callback
        : qa_q3_callback_family_resolve(&context.entities->callbacks.think,
                                        "q3.base.game.item-lifecycle.touchItem.think");
    return;
  }
  pool_link(context, entity);
}

enum { FRAME_TIME = 100 };

void qa_q3_spawn_item(qa_q3_game_entity *entity, const qa_q3_base_item_definition *item,
    const qa_q3_spawn_variables *variables, bool (*is_disabled)(void *ctx), void *is_disabled_ctx,
    qa_q3_item_lifecycle_context context) {
  qa_q3_bind_item_save_callbacks(context);
  check_context(context);
  require_owned(context, entity);
  (void)table_index(context, item);
  entity->random = qa_q3_spawn_variables_float(variables, "random", "0").value;
  entity->wait = qa_q3_spawn_variables_float(variables, "wait", "0").value;
  qa_q3_item_registry_register(&context.registry, item);
  if (is_disabled != NULL && is_disabled(is_disabled_ctx)) return;
  entity->item = item;
  qa_q3_game_entity_set_nextthink(entity, qa_int32(game_time(context) + FRAME_TIME * 2));
  entity->think = qa_q3_callback_family_resolve(
      &context.entities->callbacks.think, "q3.base.game.item-lifecycle.spawnItem.think");
  entity->physics_bounce = f32(0.5);
  if (item->type == QA_Q3_BASE_IT_POWERUP) {
    (void)context.sound_index(context.ctx, "sound/items/poweruprespawn.wav");
    entity->speed = qa_q3_spawn_variables_float(variables, "noglobalsound", "0").value;
  }
  if (context.product == QA_Q3_BASE_PRODUCT_MISSIONPACK
      && item->type == QA_Q3_BASE_IT_PERSISTANT_POWERUP)
    entity->s.generic1 = entity->spawnflags;
}

static void cb_respawn(void *ctx, qa_q3_game_entity *self) {
  qa_q3_respawn_item(self, *(qa_q3_item_lifecycle_context *)ctx);
}
static void cb_touch(void *ctx, qa_q3_game_entity *self, qa_q3_damage_participant other,
                     qa_touch_contact contact) {
  qa_q3_touch_item(self, other, contact, *(qa_q3_item_lifecycle_context *)ctx);
}
static void cb_use(void *ctx, qa_q3_game_entity *self, const qa_q3_use_participant *other,
                   const qa_q3_use_participant *activator) {
  (void)other; (void)activator;
  qa_q3_respawn_item(self, *(qa_q3_item_lifecycle_context *)ctx);
}
static void cb_finish(void *ctx, qa_q3_game_entity *self) {
  qa_q3_finish_spawning_item(self, *(qa_q3_item_lifecycle_context *)ctx);
}
static qa_q3_entity_callback mkcb(const void *fn, void *ctx) {
  qa_q3_entity_callback cb; memcpy(&cb.fn, fn, sizeof cb.fn); cb.ctx = ctx; return cb;
}

void qa_q3_bind_item_save_callbacks(qa_q3_item_lifecycle_context context) {
  if (context.entities == NULL) return;
  qa_q3_item_lifecycle_context *retained = malloc(sizeof *retained);
  if (retained == NULL) qa_range_error("out of memory");
  *retained = context;
  qa_q3_entity_think_fn respawn_fn = cb_respawn, finish_fn = cb_finish;
  qa_q3_entity_touch_fn touch_fn = cb_touch;
  qa_q3_entity_use_fn use_fn = cb_use;
  qa_q3_callback_catalog *catalog = &context.entities->callbacks;
  qa_q3_entity_callback bound = qa_q3_callback_family_intern(
      &catalog->think, "q3.base.game.item-lifecycle.touchItem.think",
      mkcb(&respawn_fn, retained));
  if (bound.ctx != retained) { free(retained); return; }
  qa_q3_callback_catalog_retain(catalog, retained);
  qa_q3_callback_family_intern(&catalog->touch,
                               "q3.base.game.item-lifecycle.finishSpawningItem.touch",
                               mkcb(&touch_fn, retained));
  qa_q3_callback_family_intern(&catalog->use,
                               "q3.base.game.item-lifecycle.finishSpawningItem.use",
                               mkcb(&use_fn, retained));
  qa_q3_callback_family_intern(&catalog->think,
                               "q3.base.game.item-lifecycle.spawnItem.think",
                               mkcb(&finish_fn, retained));
}
