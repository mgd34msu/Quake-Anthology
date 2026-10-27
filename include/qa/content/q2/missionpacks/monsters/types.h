/* Port of src/content/q2/missionpacks/monsters/types.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_TYPES_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_TYPES_H

#include <stdbool.h>

#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"

/* TS Q2MonsterMissionPack: "xatrix" | "rogue". */
typedef enum qa_q2mpm_pack { QA_Q2MPM_PACK_XATRIX, QA_Q2MPM_PACK_ROGUE } qa_q2mpm_pack;
const char *qa_q2mpm_pack_name(qa_q2mpm_pack pack);

/* Mirrored unported surface (TS owner: src/content/q2/foundation/
 * movers.ts Q2MoverModule; the landed qa_q2_mover_module serves the
 * incompatible qa_q2_* host). Only linear.moveTo is used here. */
typedef struct qa_q2mpm_movers {
  void *ctx;
  void (*linear_move_to)(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_vec3 destination, qa_q2f_think on_arrive);
} qa_q2mpm_movers;

typedef struct qa_q2mpm_powerups {
  double quad_until, double_until, invulnerability_until;
} qa_q2mpm_powerups;

/* TS Q2MissionPackMonsterServices. bad_area_entity returns NULL for
 * TS null; origin NULL mirrors the omitted optional argument. */
typedef struct qa_q2mpm_services {
  void *ctx;
  qa_q2mpm_movers movers;
  double (*gravity)(void *ctx);
  bool (*bad_area)(void *ctx, qa_actor_id actor);
  qa_q2f_entity *(*bad_area_entity)(void *ctx, qa_actor_id actor, const qa_vec3 *origin);
  bool (*mark_tesla_area)(void *ctx, qa_q2f_entity *self, qa_q2f_entity *tesla);
  qa_q2mpm_powerups (*powerups)(void *ctx, qa_actor_id actor);
} qa_q2mpm_services;

/** TS Q2MissionPackMonsterWeapons. fire_heat_rocket's optional
 * turnFraction uses has_turn_fraction. */
typedef struct qa_q2mpm_weapons {
  void *ctx;
  qa_q2f_entity *(*fire_ion_ripper)(void *ctx, qa_q2f_entity *self, qa_q2f_game *game, qa_vec3 start,
                                    qa_vec3 direction, double damage, double speed, double effects);
  qa_q2f_entity *(*fire_blue_blaster)(void *ctx, qa_q2f_entity *self, qa_q2f_game *game, qa_vec3 start,
                                      qa_vec3 direction, double damage, double speed, double effects);
  qa_q2f_entity *(*fire_heat_rocket)(void *ctx, qa_q2f_entity *self, qa_q2f_game *game, qa_vec3 start,
                                     qa_vec3 direction, double damage, double speed, double radius,
                                     double radius_damage, bool has_turn_fraction, double turn_fraction);
  qa_q2f_entity *(*fire_plasma)(void *ctx, qa_q2f_entity *self, qa_q2f_game *game, qa_vec3 start,
                                qa_vec3 direction, double damage, double speed, double radius, double radius_damage);
  qa_q2f_entity *(*fire_blaster2)(void *ctx, qa_q2f_entity *self, qa_q2f_game *game, qa_vec3 start,
                                  qa_vec3 direction, double damage, double speed, double effects);
  qa_q2f_entity *(*fire_tracker)(void *ctx, qa_q2f_entity *self, qa_q2f_game *game, qa_vec3 start,
                                 qa_vec3 direction, double damage, double speed, const qa_actor_id *enemy);
  qa_q2f_entity *(*fire_flechette)(void *ctx, qa_q2f_entity *self, qa_q2f_game *game, qa_vec3 start,
                                   qa_vec3 direction, double damage, double speed, double kick);
  void (*fire_heat_beam)(void *ctx, qa_q2f_entity *self, qa_q2f_game *game, qa_vec3 start,
                         qa_vec3 direction, qa_vec3 offset, double damage, double kick);
} qa_q2mpm_weapons;

/* Mirrored unported surface (TS owner: src/content/q2/foundation/
 * host.ts Q2PresentationEvent "beam"; qa_q2f presentation events
 * carry no beam kind). The host wires a sink; emitting without one
 * aborts, mirroring qa_q2bm_emit_extended. */
typedef struct qa_q2mpm_beam_event {
  qa_actor_id actor;
  qa_vec3 start, end;
  double width, color;
  bool visible;
} qa_q2mpm_beam_event;
typedef struct qa_q2mpm_beam_sink {
  void *ctx;
  void (*emit)(void *ctx, qa_q2mpm_beam_event event);
} qa_q2mpm_beam_sink;
void qa_q2mpm_set_beam_sink(qa_q2mpm_beam_sink sink);
void qa_q2mpm_emit_beam(qa_q2mpm_beam_event event);

/* Mirrored unported surface (TS owner: src/content/q2/foundation/
 * callbacks.ts Q2SourceCallbacks use/touch resolve; the qa_q2f
 * mirror carries think resolve only). The host wires resolvers;
 * use without them aborts. A NULL-name result mirrors TS null via
 * a NULL fn. */
typedef struct qa_q2mpm_callback_resolvers {
  void *ctx;
  qa_q2f_use (*use_resolve)(void *ctx, const char *name);
  qa_q2f_touch (*touch_resolve)(void *ctx, const char *name);
} qa_q2mpm_callback_resolvers;
void qa_q2mpm_set_callback_resolvers(qa_q2mpm_callback_resolvers resolvers);
qa_q2f_use qa_q2mpm_use_resolve(const char *name);
bool qa_q2mpm_touch_resolve(const char *name, qa_q2f_touch *out); /* false = TS null */

#endif
