/* Port of src/content/q1/missionpacks/world/hipnotic-spawn.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/hipnotic_spawn.h"

#include "../missionpacks_internal.h"
#include "qa/content/q1/foundation/callbacks.h"
#include "qa/content/q1/foundation/spawns.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/content/q1/missionpacks/world/index.h"

static qa_q1_solid saved_solid(const char *value) {
  static const char *const VALID[] = {"none", "trigger", "bbox", "slidebox", "bsp", "corpse"};
  for (size_t i = 0; i < sizeof VALID / sizeof VALID[0]; i++) if (qa_q1mp_streq(value, VALID[i])) return VALID[i];
  qa_range_error("Invalid func_spawn saved solid %s", value);
}
static qa_q1_actor *make_template(qa_q1_services *game, qa_q1_actor *mold, const char *classname) {
  /* { properties: [...mold.fields] without classname, then { classname } } */
  qa_q1_entity_property *properties = malloc((mold->field_count + 1) * sizeof *properties);
  if (properties == NULL) qa_range_error("Out of memory");
  size_t count = 0;
  for (size_t i = 0; i < mold->field_count; i++) {
    if (qa_q1mp_streq(mold->fields[i].key, "classname")) continue;
    properties[count].key = mold->fields[i].key; properties[count].value = mold->fields[i].value; count++;
  }
  char classname_key[] = "classname";
  char *classname_value = malloc(strlen(classname) + 1);
  if (classname_value == NULL) qa_range_error("Out of memory");
  memcpy(classname_value, classname, strlen(classname) + 1);
  properties[count].key = classname_key; properties[count].value = classname_value; count++;
  const qa_q1_entity source = {properties, count};
  qa_q1_actor *entity = qa_q1_create(game, classname, &source, NULL);
  free(classname_value); free(properties);
  qa_q1_body mold_body = qa_q1_get_body(game, mold);
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = mold_body.origin;
  patch.has_angles = true; patch.angles = mold_body.angles;
  patch.has_velocity = true; patch.velocity = mold_body.velocity;
  patch.has_bounds = true; patch.bounds = mold_body.bounds;
  patch.has_ground = true; patch.ground = mold_body.has_ground ? &mold_body.ground : NULL;
  qa_q1_set_body(game, entity, &patch);
  const double single_player = 0;
  qa_q1_spawn_entity_context(game, entity, &single_player);
  if (!qa_q1_live(game, entity)) qa_range_error("func_spawn template %s removed itself", classname);
  const qa_q1_body body = qa_q1_get_body(game, entity);
  qa_q1mp_field_text(entity, "spawnmodel", entity->model);
  qa_q1mp_field_text(entity, "spawnsolidtype", entity->solid);
  const char *think = qa_q1_action_name(entity->think);
  if (think == NULL) (void)qa_q1_actor_field_delete(entity, "spawnthink");
  else qa_q1mp_field_text(entity, "spawnthink", think);
  qa_q1mp_world_vector(entity, "spawnmins", body.bounds.min);
  qa_q1mp_world_vector(entity, "spawnmaxs", body.bounds.max);
  qa_q1mp_set_string(&entity->model, ""); entity->solid = "none";
  qa_q1mp_world_later(game, entity, 1, "hip:spawn_think");
  qa_q1_link(game, entity);
  return entity;
}
static void spawn_think(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; qa_q1mp_world_later(game, entity, 1, "hip:spawn_think");
}
static void spawn_use(void *ctx, qa_q1_services *game, qa_q1_actor *e, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  const qa_q1mp_world_hooks *hooks = ctx;
  qa_actor_id master_id;
  qa_q1_actor *master = qa_q1_actor_reference_get(e, "spawnmaster", &master_id) ? qa_q1_get_entity(game, &master_id) : NULL;
  if (master == NULL) qa_range_error("func_spawn lost its initialized master");
  qa_actor_id charmer;
  const bool has_charmer = hooks->charmer != NULL && hooks->charmer(hooks->ctx, &charmer);
  qa_q1_actor *entity = qa_q1_actor_number(e, "spawnmulti") == 1 || has_charmer ? qa_q1_clone_entity(game, master) : master;
  qa_q1mp_set_string(&entity->model, qa_q1_actor_text(entity, "spawnmodel"));
  entity->solid = saved_solid(qa_q1_actor_text(entity, "spawnsolidtype"));
  const char *think = qa_q1_actor_text(entity, "spawnthink");
  entity->think = think[0] == '\0' ? (qa_q1_bound_action){0} : qa_q1_callback_registry_action(game->named, entity, think);
  qa_vec3 min = qa_q1_actor_vector(entity, "spawnmins");
  qa_vec3 max = qa_q1_actor_vector(entity, "spawnmaxs");
  qa_q1_set_bounds(game, entity, (qa_bounds){min, max});
  qa_q1_link(game, entity);
  if (qa_q1_actor_number(e, "spawnsilent") == 0) (void)qa_q1_spawn_teleport_fog(game, qa_q1_get_body(game, entity).origin);
  if (has_charmer) {
    if (hooks->charm == NULL) qa_range_error("Horn func_spawn requires the selected monster charm implementation");
    hooks->charm(hooks->ctx, entity, charmer);
  }
  if (qa_js_bit_and(entity->movement_flags, 32) != 0) {
    if (qa_q1_actor_number(e, "spawnmulti") != 0 && !has_charmer) game->total_monsters++;
    if (has_charmer) entity->effects = qa_js_bit_or(entity->effects, 8);
  }
  if (qa_q1_actor_number(e, "spawnmulti") == 0 && !has_charmer) qa_q1_remove(game, e);
}
static void func_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  const double count = game->total_monsters;
  qa_q1_actor *master;
  if (qa_q1_actor_text(e, "spawnfunction")[0] == '\0') {
    const double chance = game->host.random(game->host.ctx);
    qa_q1_actor *dog = make_template(game, e, "monster_dog");
    qa_q1_actor *ogre = make_template(game, e, "monster_ogre");
    qa_q1_actor *demon = make_template(game, e, "monster_demon1");
    qa_q1_actor *zombie = make_template(game, e, "monster_zombie");
    qa_q1_actor *shambler = make_template(game, e, "monster_shambler");
    master = chance < 0.5 ? dog : chance < 0.8 ? ogre : chance < 0.92 ? demon : chance < 0.97 ? zombie : shambler;
    game->total_monsters = count + 1;
  } else {
    const char *spawn_class = qa_q1_actor_text(e, "spawnclassname");
    if (spawn_class[0] == '\0') qa_range_error("No spawnclassname defined");
    master = make_template(game, e, spawn_class);
    if (qa_q1_actor_number(e, "spawnmulti") != 0) game->total_monsters = count;
  }
  e->solid = "none"; e->movement = "none"; qa_q1mp_set_string(&e->model, "");
  e->use = qa_q1_callback_registry_use(game->named, e, "hip:spawn_use");
  if (!qa_q1_actor_reference_set(e, "spawnmaster", &master->actor.id)) qa_range_error("Out of memory");
}
void qa_q1mp_register_hipnotic_spawn(qa_q1_services *game, const qa_q1mp_world_hooks *hooks) {
  /* hooks is borrowed for the life of the game (the world keeps it). */
  void *hooks_ctx = (void *)(uintptr_t)hooks;
  qa_q1_callback_registry_register(game->named, "hip:spawn_think", (qa_q1_callback_handlers){.action = spawn_think});
  qa_q1_callback_registry_register(game->named, "hip:spawn_use", (qa_q1_callback_handlers){.ctx = hooks_ctx, .use = spawn_use});
  qa_q1_register_spawn(game, "func_spawn", func_spawn, NULL);
  qa_q1_register_spawn(game, "func_spawn_small", func_spawn, NULL);
}
