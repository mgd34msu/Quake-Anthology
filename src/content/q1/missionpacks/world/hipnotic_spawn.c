/* Port of src/content/q1/missionpacks/world/hipnotic-spawn.ts.
 * Template creation with map properties (game.create options) and
 * spawnTeleportFog (TS owner: foundation/spawns.ts) are ext below
 * where the q1b mirror lacks them. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/hipnotic_spawn.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/content/q1/missionpacks/world/index.h"
#include "qa/foundation/support.h"
#include <stdlib.h>

qa_q1mp_actor *qa_q1mp_ext_spawn_teleport_fog(qa_q1mp_game *game, qa_vec3 origin);
typedef struct hs_ctx { qa_q1mp_game *game; qa_q1mp_world_hooks hooks; } hs_ctx;
static const char *solid_checked(const char *value) {
  static const char *const valid[] = {"none","trigger","bbox","slidebox","bsp","corpse"};
  for (size_t i = 0; i < 6; i++) if (qa_q1mp_streq(value, valid[i])) return valid[i];
  qa_range_error("Invalid func_spawn saved solid %s", value ? value : "");
}
static qa_q1mp_actor *make_template(hs_ctx *c, qa_q1b_game *g, qa_q1b_actor *mold, const char *classname) {
  qa_q1mp_actor *entity = g->create(g, classname);
  /* Copy mold fields (except classname) as spawn properties. */
  for (size_t i = 0; i < mold->field_count; i++)
    if (!qa_q1mp_streq(mold->fields[i].key, "classname")) qa_q1b_fields_set(entity, mold->fields[i].key, mold->fields[i].value);
  qa_q1b_body body = g->body(g, mold);
  qa_q1b_body_patch p = {0};
  p.has_origin = true; p.origin = body.origin; p.has_angles = true; p.angles = body.angles;
  p.has_velocity = true; p.velocity = body.velocity; p.has_bounds = true; p.bounds = body.bounds;
  p.has_ground = true; p.ground = body.ground; qa_q1mp_patch(c->game, entity, &p);
  g->spawn_entity(g, entity);
  if (!g->live(g, entity)) qa_range_error("func_spawn template %s removed itself", classname);
  qa_q1b_body body2 = g->body(g, entity);
  qa_q1b_fields_set(entity, "spawnmodel", entity->model);
  qa_q1b_fields_set(entity, "spawnsolidtype", entity->solid);
  if (entity->think.has && entity->think.name) qa_q1b_fields_set(entity, "spawnthink", entity->think.name);
  else qa_q1b_fields_set(entity, "spawnthink", "");
  qa_q1mp_world_vector(entity, "spawnmins", body2.bounds.min);
  qa_q1mp_world_vector(entity, "spawnmaxs", body2.bounds.max);
  entity->model[0] = '\0'; entity->solid = "none";
  qa_q1mp_world_later(c->game, entity, 1, "hip:spawn_think"); g->link(g, entity);
  return entity;
}
static void spawn_think_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { (void)g; qa_q1mp_world_later(((hs_ctx *)vctx)->game, e, 1, "hip:spawn_think"); }
static void spawn_use_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) {
  (void)o; (void)a; hs_ctx *c = vctx;
  qa_q1mp_opt_actor master_ref = qa_q1b_references_get(e, "spawnmaster");
  qa_q1mp_actor *master = master_ref.has ? g->entity(g, master_ref) : NULL;
  if (!master) qa_range_error("func_spawn lost its initialized master");
  qa_q1mp_opt_actor charmer = c->hooks.charmer ? c->hooks.charmer(c->hooks.ctx) : QA_Q1MP_NONE;
  qa_q1mp_actor *entity = (qa_q1mp_number(e, "spawnmulti") == 1 || charmer.has) ? g->clone_entity(g, master) : master;
  qa_q1mp_set_model(entity, qa_q1mp_text(entity, "spawnmodel"));
  entity->solid = solid_checked(qa_q1mp_text(entity, "spawnsolidtype"));
  const char *think = qa_q1mp_text(entity, "spawnthink");
  if (!think[0]) entity->think.has = false;
  else entity->think = g->named.action(g->named.ctx, entity, think);
  qa_bounds b = {qa_q1mp_vector_of(entity, "spawnmins"), qa_q1mp_vector_of(entity, "spawnmaxs")};
  g->set_bounds(g, entity, b); g->link(g, entity);
  if (qa_q1mp_number(e, "spawnsilent") == 0) (void)qa_q1mp_ext_spawn_teleport_fog(c->game, g->body(g, entity).origin);
  if (charmer.has) {
    if (!c->hooks.charm) qa_range_error("Horn func_spawn requires the selected monster charm implementation");
    c->hooks.charm(c->hooks.ctx, entity, charmer.id);
  }
  if ((entity->movement_flags & 32) != 0) {
    if (qa_q1mp_number(e, "spawnmulti") != 0 && !charmer.has) g->total_monsters++;
    if (charmer.has) entity->effects |= 8;
  }
  if (qa_q1mp_number(e, "spawnmulti") == 0 && !charmer.has) g->remove(g, e);
}
static void func_spawn_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  hs_ctx *c = vctx; double count = g->total_monsters; qa_q1mp_actor *master = NULL;
  if (!qa_q1mp_text(e, "spawnfunction")[0]) {
    double chance = qa_q1mp_random(c->game);
    qa_q1mp_actor *dog = make_template(c, g, e, "monster_dog");
    qa_q1mp_actor *ogre = make_template(c, g, e, "monster_ogre");
    qa_q1mp_actor *demon = make_template(c, g, e, "monster_demon1");
    qa_q1mp_actor *zombie = make_template(c, g, e, "monster_zombie");
    qa_q1mp_actor *shambler = make_template(c, g, e, "monster_shambler");
    master = chance < 0.5 ? dog : chance < 0.8 ? ogre : chance < 0.92 ? demon : chance < 0.97 ? zombie : shambler;
    g->total_monsters = count + 1;
  } else {
    const char *spawn_class = qa_q1mp_text(e, "spawnclassname");
    if (!spawn_class[0]) qa_range_error("No spawnclassname defined");
    master = make_template(c, g, e, spawn_class);
    if (qa_q1mp_number(e, "spawnmulti") != 0) g->total_monsters = count;
  }
  e->solid = "none"; e->movement = "none"; e->model[0] = '\0';
  e->use = g->named.use(g->named.ctx, e, "hip:spawn_use");
  qa_q1b_references_set(e, "spawnmaster", QA_Q1MP_SOME(master->actor.id));
}
void qa_q1mp_register_hipnotic_spawn(qa_q1mp_game *game, const qa_q1mp_world_hooks *hooks) {
  hs_ctx *c = malloc(sizeof *c); c->game = game;
  if (hooks) c->hooks = *hooks; else memset(&c->hooks, 0, sizeof c->hooks);
  qa_q1b_game *g = game->base;
  qa_q1b_handlers h = {0}; h.ctx = c; h.action = spawn_think_cb;
  g->named.register_named(g->named.ctx, "hip:spawn_think", &h);
  qa_q1b_handlers hu = {0}; hu.ctx = c; hu.use = spawn_use_cb;
  g->named.register_named(g->named.ctx, "hip:spawn_use", &hu);
  g->register_spawn(g, "func_spawn", func_spawn_spawn, c);
  g->register_spawn(g, "func_spawn_small", func_spawn_spawn, c);
}
