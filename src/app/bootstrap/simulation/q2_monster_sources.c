/* Port of src/app/bootstrap/simulation/q2-monster-sources.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q2_monster_sources.h"
#include <string.h>
#include "qa/foundation/support.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
typedef struct noise_env { qa_app_sim_q2_monster_factory factory; const void *monsters; } noise_env;
static void noise_fn(void *ctx, qa_actor_id actor, qa_vec3 origin, bool secondary) {
  noise_env *e = ctx;
  e->factory.report_noise(e->factory.ctx, e->monsters, actor, origin, secondary);
}
qa_app_sim_selected_q2_monster_modules qa_app_sim_create_selected_q2_monster_modules(
    qa_app_sim_q2_monster_options options, qa_app_sim_q2_monster_factory factory) {
  static noise_env env; env.factory = factory; env.monsters = NULL;
  const void *ballistics = factory.create_ballistics(factory.ctx, options.weapons, noise_fn, &env);
  const void *monsters = factory.create_monsters(factory.ctx, ballistics, options.behavior);
  env.monsters = monsters;
  const void *base = factory.register_base(factory.ctx, monsters);
  const void *movers = factory.create_movers(factory.ctx, monsters);
  const void *projectiles = factory.create_projectiles(factory.ctx, ballistics, monsters);
  const char *selected[2]; size_t selected_count = 0;
  if (is(options.edition, "rerelease")) { selected[0] = "xatrix"; selected[1] = "rogue"; selected_count = 2; }
  else if (is(options.program, "xatrix")) { selected[0] = "xatrix"; selected_count = 1; }
  else if (is(options.program, "rogue")) { selected[0] = "rogue"; selected_count = 1; }
  qa_app_sim_selected_q2_monster_modules out; memset(&out, 0, sizeof out);
  const void *modules[4]; size_t module_count = 0;
  modules[module_count++] = base;
  for (size_t i = 0; i < selected_count; i++) {
    out.packs[i] = factory.register_pack(factory.ctx, monsters, selected[i], projectiles, options.edition);
    modules[module_count++] = out.packs[i].monsters;
  }
  out.pack_count = selected_count;
  if (is(options.edition, "rerelease")) {
    const void *source = NULL;
    for (size_t i = 0; i < out.pack_count; i++) if (is(out.packs[i].pack, "rogue")) source = out.packs[i].source;
    if (source == NULL) qa_range_error("Rerelease monster registration requires its Rogue state");
    const char *expansion = is(options.program, "baseq2") ? "base" : is(options.program, "mg2") ? "mg1" : options.program;
    modules[module_count++] = factory.register_rerelease(factory.ctx, monsters, source, projectiles, expansion);
  }
  const void *game = factory.create_game(factory.ctx, monsters, modules, module_count);
  factory.register_ballistics_callbacks(factory.ctx, ballistics, game);
  factory.register_source_callbacks(factory.ctx, game, factory.projectile_callbacks(factory.ctx, projectiles));
  factory.register_source_callbacks(factory.ctx, game, factory.mover_callbacks(factory.ctx, movers));
  out.game = game; out.monsters = monsters; out.ballistics = ballistics; out.movers = movers;
  return out;
}
