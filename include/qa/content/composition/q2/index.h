/*
 * Port of src/content/composition/q2/index.ts: Q2ProductRuntime, one
 * source runtime whose phases the session calls from its scheduler.
 * (The TS file also re-exports types.ts and save.ts; see the sibling
 * headers.) All construction/operations of unported Q2 modules are
 * mirrored as fn+ctx hooks (qa_comp_q2_* names; TS owners under
 * src/content/q2/). The constructor's closure bindings are implemented
 * in index.c. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q2_INDEX_H
#define QA_CONTENT_COMPOSITION_Q2_INDEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/composition/q2/match.h"
#include "qa/content/composition/q2/types.h"
#include "qa/contracts/gameplay.h"

/* Bindings index.c builds for the module constructors. */
typedef struct qa_comp_q2_monster_bindings {
  void *ctx;
  void (*drop_item)(void *ctx, qa_actor_id actor, qa_comp_q2_game_services *game, const char *classname);
  /* platformState(actor): phase string or NULL. */
  const char *(*platform_state)(void *ctx, qa_actor_id actor);
} qa_comp_q2_monster_bindings;
typedef struct qa_comp_q2_mover_bindings {
  void *ctx;
  void (*path_corner)(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game, qa_comp_q2_entity *other);
  void (*combat_point)(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game, qa_comp_q2_entity *other);
} qa_comp_q2_mover_bindings;
typedef struct qa_comp_q2_base_entity_bindings {
  void *ctx;
  const void *entity_hooks; /* configuration.entityHooks spread */
  void *movers;
  void *weapons;
  void (*teleport_player)(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles);
  void (*turret_driver)(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  const void *(*monster_context)(void *ctx, qa_actor_id actor);
  void (*resume_monster)(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
} qa_comp_q2_base_entity_bindings;
typedef struct qa_comp_q2_item_bindings {
  void *ctx;
  const void *rest; /* configuration.itemHooks spread */
  bool has_random_respawn;
  /* Returns the respawned entity or NULL (TS null). */
  qa_comp_q2_entity *(*random_respawn)(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  bool (*weapon_picked)(void *ctx, qa_actor_id actor, qa_item_id item, bool first);
  void (*ammo_pack)(void *ctx, qa_actor_id actor, qa_comp_q2_game_services *game, bool full);
} qa_comp_q2_item_bindings;
typedef struct qa_comp_q2_pickup_policy {
  void *ctx;
  bool (*instanced_coop)(void *ctx, qa_comp_q2_game_services *game);
  bool (*can_pickup)(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game, qa_actor_id actor);
  bool (*before_pickup)(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game, qa_actor_id actor);
  void (*before_targets)(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game, qa_actor_id actor,
                         bool taken);
  void (*after_pickup)(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game, qa_actor_id actor,
                       bool taken);
  bool (*keep_after_pickup)(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game, qa_actor_id actor);
} qa_comp_q2_pickup_policy;
typedef struct qa_comp_q2_armory_bindings {
  void *ctx;
  void *weapons;
  void *items;
  const void *(*monster)(void *ctx, qa_actor_id actor);
  bool hunter_camera;
  bool strong_mines;
  double (*gravity)(void *ctx);
  bool (*intermission)(void *ctx);
  void (*player_effect)(void *ctx, const qa_comp_q2_missionpack_player_effect *event);
} qa_comp_q2_armory_bindings;
typedef struct qa_comp_q2_pack_entity_bindings {
  void *ctx;
  void *movers;
  void *weapons;
  void *projectiles;
  void (*teleport_player)(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles);
  void (*target_anger)(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_entity *target,
                       qa_comp_q2_game_services *game);
  void (*emit)(void *ctx, const qa_comp_q2_missionpack_entity_event *event);
  const char *pack; /* selects the sibling expansion for target_anger */
} qa_comp_q2_pack_entity_bindings;
typedef struct qa_comp_q2_pack_monster_bindings {
  void *ctx;
  void *movers;
  double (*gravity)(void *ctx);
  bool (*bad_area)(void *ctx, qa_actor_id actor, qa_comp_q2_game_services *game);
  bool (*bad_area_entity)(void *ctx, qa_actor_id actor, qa_comp_q2_game_services *game, qa_vec3 origin);
  void (*mark_tesla_area)(void *ctx, qa_actor_id self, qa_comp_q2_game_services *game, qa_actor_id tesla);
  qa_comp_q2_powerups (*powerups)(void *ctx, qa_actor_id actor);
} qa_comp_q2_pack_monster_bindings;
typedef struct qa_comp_q2_rerelease_monster_bindings {
  void *ctx;
  void *source;
  void *weapons; /* armory projectiles */
  void (*transfer_healthbar_target)(void *ctx, qa_actor_id old_actor, qa_actor_id new_actor,
                                    qa_comp_q2_game_services *game);
  bool is_n64;
  const char *expansion; /* "mg1" | "xatrix" | "rogue" | "base" */
} qa_comp_q2_rerelease_monster_bindings;

typedef struct qa_comp_q2_expansion {
  const char *pack; /* "xatrix" | "rogue" */
  void *entities;
  void *monsters;
} qa_comp_q2_expansion;

typedef struct qa_comp_q2_runtime_hooks {
  void *ctx;
  /* Module constructors (handles stay owned by the hook side). */
  void *(*create_monsters)(void *ctx, void *weapons, const qa_comp_q2_monster_bindings *bindings);
  void *(*register_classic_base_monsters)(void *ctx, void *monsters);
  void *(*create_movers)(void *ctx, const qa_comp_q2_mover_bindings *bindings);
  void *(*create_base_entities)(void *ctx, const qa_comp_q2_base_entity_bindings *bindings);
  void *(*create_items)(void *ctx, const qa_comp_q2_item_bindings *bindings);
  void *(*register_armory)(void *ctx, const char *pack, const qa_comp_q2_armory_bindings *bindings,
                           const char *edition);
  void *(*armory_items)(void *ctx, void *armory);
  void *(*armory_projectiles)(void *ctx, void *armory);
  void *(*create_pack_entities)(void *ctx, const char *pack, const qa_comp_q2_pack_entity_bindings *bindings);
  void *(*register_pack_monsters)(void *ctx, void *monsters, const char *pack, void *projectiles,
                                  const qa_comp_q2_pack_monster_bindings *bindings, const char *edition);
  void *(*create_players)(void *ctx, void *items, void *weapons, const qa_comp_q2_player_hooks *hooks,
                          const void *rules);
  void *(*create_rerelease_players)(void *ctx, void *items, void *weapons, const qa_comp_q2_player_hooks *hooks,
                                    const qa_comp_q2_rerelease_hooks *rerelease_hooks, const void *options,
                                    const void *rules);
  void *(*create_rerelease_module)(void *ctx, void *players, const qa_comp_q2_rerelease_hooks *hooks,
                                   bool has_campaign, const void *campaign);
  void *(*register_rerelease_monsters)(void *ctx, void *monsters,
                                       const qa_comp_q2_rerelease_monster_bindings *bindings);
  void *(*create_rogue_spawns)(void *ctx);
  void *(*create_target_module)(void *ctx);
  void *(*create_scenery_module)(void *ctx);
  /* Q2Foundation construction; flags read through the getter closure. */
  qa_comp_q2_game_services *(*create_foundation)(void *ctx, void *host, const qa_comp_q2_game_options *options,
                                                 const char *edition, double (*flags)(void *flags_ctx),
                                                 void *flags_ctx, void *const *modules, size_t module_count);
  const void *(*module_callbacks)(void *ctx, void *module); /* NULL when none */
  /* Foundation/game operations beyond qa_comp_q2_game_services. */
  qa_comp_q2_entity *(*attach_player)(void *ctx, qa_comp_q2_game_services *game, qa_owned_actor actor);
  void (*register_source_callbacks)(void *ctx, qa_comp_q2_game_services *game, const void *callbacks);
  double (*now)(void *ctx, qa_comp_q2_game_services *game);
  /* Weapons operations. */
  void (*weapons_bind)(void *ctx, void *weapons, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  const void *(*weapons_callbacks)(void *ctx, void *weapons);
  /* Monsters operations. */
  void (*touch_path_corner)(void *ctx, void *monsters, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game,
                            qa_comp_q2_entity *other);
  void (*touch_combat_point)(void *ctx, void *monsters, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game,
                             qa_comp_q2_entity *other);
  void (*spawn_infantry_driver)(void *ctx, void *monsters, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  const void *(*monster_context)(void *ctx, void *monsters, qa_actor_id actor);
  void (*resume_monster)(void *ctx, void *monsters, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  /* Items / base entities / armory operations. */
  void (*items_drop_monster)(void *ctx, void *items, qa_actor_id actor, qa_comp_q2_game_services *game,
                             const char *classname);
  /* phase string or NULL (movers/base platformState().phase) */
  const char *(*pack_movers_platform_state)(void *ctx, void *entities, qa_comp_q2_entity *entity);
  const char *(*base_platform_state)(void *ctx, void *base_entities, qa_comp_q2_entity *entity);
  qa_comp_q2_powerups (*items_player_powerups)(void *ctx, void *items, qa_actor_id actor);
  double (*items_haste_until)(void *ctx, void *items, qa_actor_id actor);
  void (*items_set_pickup_policy)(void *ctx, void *items, const qa_comp_q2_pickup_policy *policy);
  qa_comp_q2_entity *(*armory_random_respawn)(void *ctx, void *armory, qa_comp_q2_entity *entity,
                                              qa_comp_q2_game_services *game, const void *settings);
  void (*armory_ammo_pack)(void *ctx, void *armory, qa_actor_id actor, qa_comp_q2_game_services *game, bool full,
                           const char *pack);
  double (*armory_double_until)(void *ctx, void *armory, qa_actor_id actor);
  double (*armory_quad_fire_until)(void *ctx, void *armory, qa_actor_id actor);
  qa_comp_q2_weapon_input (*armory_input)(void *ctx, void *armory, qa_actor_id actor, qa_comp_q2_weapon_input input);
  void (*armory_items_reset)(void *ctx, void *armory, qa_actor_id actor);
  void (*spheres_owner_died)(void *ctx, void *armory, qa_actor_id actor, qa_comp_q2_game_services *game,
                             const qa_attack_provenance *attack);
  void (*spheres_owner_damaged)(void *ctx, void *armory, qa_actor_id actor, const qa_attack_provenance *attack,
                                qa_comp_q2_game_services *game);
  void (*spheres_disconnect)(void *ctx, void *armory, qa_actor_id actor, qa_comp_q2_game_services *game);
  const void *(*projectiles_callbacks)(void *ctx, void *armory);
  const void *(*spheres_callbacks)(void *ctx, void *armory);
  const void *(*doppleganger_callbacks)(void *ctx, void *armory);
  bool (*projectiles_bad_area)(void *ctx, void *armory, qa_actor_id actor, qa_comp_q2_game_services *game);
  bool (*projectiles_bad_area_entity)(void *ctx, void *armory, qa_actor_id actor, qa_comp_q2_game_services *game,
                                      qa_vec3 origin);
  void (*projectiles_mark_tesla_area)(void *ctx, void *armory, qa_actor_id self, qa_comp_q2_game_services *game,
                                      qa_actor_id tesla);
  /* Pack entities/monsters operations. */
  void (*pack_target_anger)(void *ctx, void *monsters, qa_comp_q2_entity *entity, qa_comp_q2_entity *target,
                            qa_comp_q2_game_services *game);
  bool (*pack_hints_finalize)(void *ctx, void *monsters, qa_comp_q2_game_services *game); /* false = no hints */
  void *(*pack_monsters_source)(void *ctx, void *monsters);
  const char *const *(*pack_original_source_fallbacks)(void *ctx, void *monsters, size_t *out_count);
  double (*pack_entities_capture_steam_id)(void *ctx, void *entities);
  void (*pack_entities_restore)(void *ctx, void *entities, double steam_id);
  /* Players operations. */
  void (*players_attach)(void *ctx, void *players, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game,
                         const void *admission);
  void (*players_put_in_server)(void *ctx, void *players, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game,
                                bool has_landmark_flag, const void *landmark);
  void (*players_teleport)(void *ctx, void *players, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game,
                           qa_vec3 origin, qa_vec3 angles);
  bool (*rerelease_weapon_picked)(void *ctx, void *players, qa_comp_q2_entity *entity,
                                  qa_comp_q2_game_services *game, qa_item_id item, bool first);
  const char *(*players_intermission_kind)(void *ctx, void *players);
  void (*players_chase)(void *ctx, void *players, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game,
                        double direction, bool flag);
  void (*players_end_deathmatch_level)(void *ctx, void *players, qa_comp_q2_game_services *game);
  void (*players_begin_intermission)(void *ctx, void *players, qa_comp_q2_game_services *game, const char *map);
  bool (*players_chat_allowed)(void *ctx, void *players, qa_actor_id actor, qa_comp_q2_game_services *game);
  void (*players_record_damage)(void *ctx, void *players, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game,
                                const qa_damage_decision *decision);
  void (*players_check_rules)(void *ctx, void *players, qa_comp_q2_game_services *game);
  const qa_actor_id *(*players_state_actors)(void *ctx, void *players, size_t *out_count);
  void (*rerelease_movement_impact)(void *ctx, void *players, qa_actor_id actor, double impact_delta, bool on_ladder);
  void (*rerelease_record_weapon_fire)(void *ctx, void *players, qa_actor_id actor, double now);
  /* Rerelease module operations. */
  void (*rerelease_transfer_healthbar)(void *ctx, void *entities, qa_actor_id old_actor, qa_actor_id new_actor,
                                       qa_comp_q2_game_services *game);
  bool (*rerelease_instanced_coop)(void *ctx, void *entities, qa_comp_q2_game_services *game);
  bool (*rerelease_can_pickup)(void *ctx, void *entities, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game,
                               qa_actor_id actor);
  bool (*rerelease_before_pickup)(void *ctx, void *entities, qa_comp_q2_entity *entity,
                                  qa_comp_q2_game_services *game, qa_actor_id actor);
  void (*rerelease_before_targets)(void *ctx, void *entities, qa_comp_q2_entity *entity,
                                   qa_comp_q2_game_services *game, qa_actor_id actor, bool taken);
  void (*rerelease_after_pickup)(void *ctx, void *entities, qa_comp_q2_entity *entity,
                                 qa_comp_q2_game_services *game, qa_actor_id actor, bool taken);
  bool (*rerelease_keep_after_pickup)(void *ctx, void *entities, qa_comp_q2_entity *entity,
                                      qa_comp_q2_game_services *game, qa_actor_id actor);
  void (*rerelease_after_player_frames)(void *ctx, void *entities, qa_comp_q2_game_services *game);
  /* Rogue spawns + DeathBall rules. */
  qa_comp_q2_entity *(*rogue_select_spawn)(void *ctx, void *spawns, qa_comp_q2_game_services *game);
  double (*deathball_rules_flags)(void *ctx, double flags);
} qa_comp_q2_runtime_hooks;

/* Q2PlayerAdmission surface read here (q2/base/player). */
typedef struct qa_comp_q2_player_admission {
  bool has_use_q2_weapons;
  bool use_q2_weapons;
  const void *rest;
} qa_comp_q2_player_admission;

#define QA_COMP_Q2_MAX_MODULES 24
#define QA_COMP_Q2_MAX_EXPANSIONS 2

/* TS Q2ProductRuntime. */
typedef struct qa_comp_q2_product_runtime {
  qa_comp_q2_options configuration;
  const qa_comp_q2_runtime_hooks *hooks;
  const qa_comp_q2_mode_hooks *mode_hooks;
  qa_comp_q2_game_services *game;
  void *weapons;
  void *monsters;
  void *movers;
  void *items;
  void *players;
  void *base_entities;
  void *armory;
  qa_comp_q2_expansion expansions[QA_COMP_Q2_MAX_EXPANSIONS];
  size_t expansion_count;
  bool has_rerelease;
  void *rerelease_players;
  void *rerelease_entities;
  void *rerelease_monsters;
  void *modules[QA_COMP_Q2_MAX_MODULES];
  size_t module_count;
  qa_comp_q2_product_match match;
  bool has_movement_stop_speed;
  double movement_stop_speed;
  void *rogue_spawns;
  uint32_t active_deathmatch_flags;
  /* Constructor bindings kept alive for the module constructors. */
  qa_comp_q2_monster_bindings monster_bindings;
  qa_comp_q2_mover_bindings mover_bindings;
  qa_comp_q2_base_entity_bindings base_entity_bindings;
  qa_comp_q2_item_bindings item_bindings;
  qa_comp_q2_armory_bindings armory_bindings;
  qa_comp_q2_pack_entity_bindings pack_entity_bindings[QA_COMP_Q2_MAX_EXPANSIONS];
  qa_comp_q2_pack_monster_bindings pack_monster_bindings;
  qa_comp_q2_player_hooks effective_player_hooks;
  qa_comp_q2_ctf_hooks ctf_hooks;
  qa_comp_q2_pickup_policy pickup_policy;
  qa_comp_q2_rerelease_hooks effective_rerelease_hooks;
  qa_comp_q2_rerelease_monster_bindings rerelease_monster_bindings;
} qa_comp_q2_product_runtime;

/** TS Q2ProductRuntime constructor. */
void qa_comp_q2_product_runtime_init(qa_comp_q2_product_runtime *runtime, const qa_comp_q2_options *configuration,
                                     const qa_comp_q2_runtime_hooks *hooks, const qa_comp_q2_mode_hooks *mode_hooks);
/** TS createQ2ProductRuntime(). */
void qa_comp_create_q2_product_runtime(qa_comp_q2_product_runtime *runtime, const qa_comp_q2_options *options,
                                       const qa_comp_q2_runtime_hooks *hooks, const qa_comp_q2_mode_hooks *mode_hooks);

void qa_comp_q2_product_runtime_set_deathmatch_flags(qa_comp_q2_product_runtime *runtime, double flags);
/* False when out cannot hold the full fallback list. */
bool qa_comp_q2_product_runtime_original_source_fallbacks(const qa_comp_q2_product_runtime *runtime,
                                                          const char **out, size_t capacity, size_t *out_count);
void qa_comp_q2_product_runtime_register_callbacks(qa_comp_q2_product_runtime *runtime);
qa_comp_q2_entity *qa_comp_q2_product_runtime_admit(qa_comp_q2_product_runtime *runtime, qa_owned_actor actor,
                                                    const qa_comp_q2_player_admission *admission,
                                                    const void *landmark);
qa_comp_q2_weapon_input qa_comp_q2_product_runtime_weapon_input(qa_comp_q2_product_runtime *runtime,
                                                                qa_actor_id actor, qa_comp_q2_weapon_input input);
qa_comp_q2_powerups qa_comp_q2_product_runtime_powerups(qa_comp_q2_product_runtime *runtime, qa_actor_id actor);
void qa_comp_q2_product_runtime_record_damage(qa_comp_q2_product_runtime *runtime, qa_comp_q2_entity *entity,
                                              const qa_damage_decision *decision);
void qa_comp_q2_product_runtime_before_reaction(qa_comp_q2_product_runtime *runtime, qa_actor_id actor,
                                                const qa_attack_provenance *attack);
void qa_comp_q2_product_runtime_movement_impact(qa_comp_q2_product_runtime *runtime, qa_actor_id actor,
                                                double impact_delta, bool on_ladder);
void qa_comp_q2_product_runtime_record_foreign_weapon_fire(qa_comp_q2_product_runtime *runtime, qa_actor_id actor);
void qa_comp_q2_product_runtime_after_player_frames(qa_comp_q2_product_runtime *runtime);
void qa_comp_q2_product_runtime_after_spawn(qa_comp_q2_product_runtime *runtime);
void qa_comp_q2_product_runtime_check_rules(qa_comp_q2_product_runtime *runtime);

#endif
