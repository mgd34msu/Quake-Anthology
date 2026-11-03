/*
 * Port of src/content/composition/q1/runtime.ts: registers one official
 * source program on the session's existing world and actors, over the one
 * Q1 game (qa_q1_services, TS Q1Foundation / Q1EntityServices).
 * Q1SourceComposition calls the real layers directly: foundation, base
 * (qa_q1_base), mission packs (qa_q1mp_mission_pack_runtime), addons
 * (qa_q1ad_context, qa_q1ad_ctf, qa_q1ad_horde) and the composition's
 * clients/commands. The closures the TS constructor hands to those layers
 * (campaign forwarding, Q1BaseOptions, Q1AddonServices, Q1HordeServices,
 * Q1MissionPackOptions, Q1CtfServices, damage source effects, the
 * "q1:source-clients" state extension) are static functions in runtime.c
 * whose ctx is the composition, so the composition must not move after
 * init. qa_comp_q1_services (TS Q1CompositionServices) is the host
 * interface. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q1_RUNTIME_H
#define QA_CONTENT_COMPOSITION_Q1_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qc/presentation_host.h"
#include "qa/content/composition/q1/clients.h"
#include "qa/content/composition/q1/types.h"
#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/addons/ctf/index.h"
#include "qa/content/q1/addons/horde/index.h"
#include "qa/content/q1/base/player.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/base/travel.h"
#include "qa/content/q1/foundation/runtime.h"
#include "qa/content/q1/missionpacks/runtime.h"
#include "qa/contracts/gameplay.h"

/* TS Q1SourceComposition. game is borrowed (the TS constructor) or owned
 * (qa_comp_create_q1_source_composition); every layer object below is
 * created and owned by the composition. NULL members are TS null. */
typedef struct qa_comp_q1_source_composition {
  qa_q1_services *game;
  bool owns_game;
  qa_comp_q1_source_selection selection;
  const qa_comp_q1_services *services;
  qa_compat_qc_finale_ack finale_acknowledgement; /* TS QcFinaleAcknowledgement */
  qa_q1_base *base;
  qa_comp_q1_source_clients clients;
  qa_q1mp_mission_pack_runtime *packs;
  qa_q1ad_context *addon;
  qa_q1ad_ctf *ctf;
  qa_q1ad_horde *horde;
  qa_q1ad_context *horde_addon;
  /* private pendingDisconnects (ActorId[]). */
  qa_actor_id *pending_disconnects;
  size_t pending_disconnect_count, pending_disconnect_capacity;
  /* Storage the game and the layers borrow (the TS closure objects). */
  qa_q1_campaign_binding campaign;
  qa_q1_base_options base_options;
  qa_q1ad_services addon_services;
  qa_q1ad_horde_services horde_services;
  qa_q1mp_mission_pack_options pack_options;
  qa_q1_state_extension clients_extension;
} qa_comp_q1_source_composition;

/** TS new Q1SourceComposition(game, selection, services). game and
 * services are borrowed and must outlive the composition. Aborts where TS
 * throws; OOM in a layer constructor aborts too. */
void qa_comp_q1_source_composition_init(qa_comp_q1_source_composition *composition, qa_q1_services *game,
                                        const qa_comp_q1_source_selection *selection,
                                        const qa_comp_q1_services *services);
/** Releases what the composition created: clients, horde, ctf, addon
 * contexts, mission packs, base, then the game when owned. */
void qa_comp_q1_source_composition_destroy(qa_comp_q1_source_composition *composition);

/** spawnMap(map). report NULL discards the spawn report. False only on OOM. */
bool qa_comp_q1_source_composition_spawn_map(qa_comp_q1_source_composition *composition, const qa_q1_map *map,
                                             qa_q1_spawn_report *report);
/** spawnEntities(entityList, modelBounds, mapName, keep = null): keep NULL
 * spawns every ordinal. report NULL discards it. False only on OOM. */
bool qa_comp_q1_source_composition_spawn_entities(qa_comp_q1_source_composition *composition,
                                                  const qa_q1_entity *entity_list, size_t entity_count,
                                                  const qa_bounds *model_bounds, size_t model_count,
                                                  const char *map_name, const double *keep, size_t keep_count,
                                                  qa_q1_spawn_report *report);
/** attach(actor, admission). False only on OOM. */
bool qa_comp_q1_source_composition_attach(qa_comp_q1_source_composition *composition, qa_owned_actor actor,
                                          const qa_comp_q1_client_admission *admission);
/** spawned(actor, firstAdmission, spot = null). */
void qa_comp_q1_source_composition_spawned(qa_comp_q1_source_composition *composition, qa_actor_id actor,
                                           bool first_admission, qa_q1_actor *spot);
/** userinfo(actor, values). False only on OOM. */
bool qa_comp_q1_source_composition_userinfo(qa_comp_q1_source_composition *composition, qa_actor_id actor,
                                            const qa_comp_userinfo_entry *values, size_t count);
bool qa_comp_q1_source_composition_no_target(qa_comp_q1_source_composition *composition, qa_actor_id actor);
void qa_comp_q1_source_composition_set_no_target(qa_comp_q1_source_composition *composition,
                                                 qa_actor_id actor, bool enabled);
void qa_comp_q1_source_composition_input(qa_comp_q1_source_composition *composition, qa_actor_id actor,
                                         qa_comp_q1_source_input input);
/** Called once after game.beginFrame and before source player prethink. */
void qa_comp_q1_source_composition_pre_frame(qa_comp_q1_source_composition *composition, double elapsed_seconds);
void qa_comp_q1_source_composition_player_pre_think(qa_comp_q1_source_composition *composition, qa_actor_id actor);
void qa_comp_q1_source_composition_player_post_think(qa_comp_q1_source_composition *composition, qa_actor_id actor);
/** Returns true only when this selected source program consumed its impulse. */
bool qa_comp_q1_source_composition_impulse(qa_comp_q1_source_composition *composition, qa_actor_id actor);
/** fired(actor, weapon): weapon NULL = TS null. */
void qa_comp_q1_source_composition_fired(qa_comp_q1_source_composition *composition, qa_actor_id actor,
                                         qa_item_id weapon);
/** Runs inside GameplayAuthority.beforeReaction. */
void qa_comp_q1_source_composition_before_reaction(qa_comp_q1_source_composition *composition,
                                                   qa_owned_actor actor, const qa_damage_decision *decision);
void qa_comp_q1_source_composition_disconnect(qa_comp_q1_source_composition *composition, qa_actor_id actor);
/** requestRespawn(actor, coopEntry = null): coop_entry is borrowed. */
void qa_comp_q1_source_composition_request_respawn(qa_comp_q1_source_composition *composition,
                                                   qa_actor_id actor, const qa_q1_travel_state *coop_entry);
/** selectSpawn(actor = null, force = false): actor NULL = TS null. */
qa_q1_actor *qa_comp_q1_source_composition_select_spawn(qa_comp_q1_source_composition *composition,
                                                        const qa_actor_id *actor, bool force);
/* Travel: every returned state is owned by the caller (qa_q1_travel_state_destroy). */
qa_q1_travel_state qa_comp_q1_source_composition_new_travel(qa_comp_q1_source_composition *composition);
qa_q1_travel_state qa_comp_q1_source_composition_capture_travel(qa_comp_q1_source_composition *composition,
                                                                qa_owned_actor actor);
qa_q1_travel_state qa_comp_q1_source_composition_decode_travel(qa_comp_q1_source_composition *composition,
                                                               const qa_q1_travel_state *state);
void qa_comp_q1_source_composition_admit_travel(qa_comp_q1_source_composition *composition,
                                                qa_owned_actor actor, const qa_q1_travel_state *state);
/** The selected inventory calls this for a Q1 source death drop exactly once. NULL = TS null. */
qa_q1_actor *qa_comp_q1_source_composition_drop_inventory(qa_comp_q1_source_composition *composition,
                                                          qa_owned_actor actor);
/** requestIntermissionExit(pressed, input?): input NULL = TS undefined. */
typedef struct qa_comp_q1_intermission_input {
  qa_actor_id actor;
  bool attack;
} qa_comp_q1_intermission_input;
qa_q1_intermission_result qa_comp_q1_source_composition_request_intermission_exit(
    qa_comp_q1_source_composition *composition, bool pressed, const qa_comp_q1_intermission_input *input);
void qa_comp_q1_source_composition_dismiss_finale(qa_comp_q1_source_composition *composition);
qa_q1_character_source_pose qa_comp_q1_source_composition_character_pose(qa_comp_q1_source_composition *composition,
                                                                         qa_actor_id actor);
bool qa_comp_q1_source_composition_fall_damage_allowed(qa_comp_q1_source_composition *composition,
                                                       qa_actor_id actor);
void qa_comp_q1_source_composition_character_frame(qa_comp_q1_source_composition *composition,
                                                   qa_actor_id actor,
                                                   const qa_q1_character_presentation *presentation);
void qa_comp_q1_source_composition_suicide(qa_comp_q1_source_composition *composition, qa_actor_id actor);

/** TS createQ1SourceComposition(host, options, selection, services): builds
 * the Q1Foundation (owned by the composition) with options plus
 * precacheProgram "id1" for the id1 program, and skill (cvar "skill"
 * clamped to 0..3), teamplay (cvar "teamplay") and gravity (cvar
 * "sv_gravity") read from services. options->precache_id1 is ignored (TS
 * Omit<Q1FoundationOptions, "precacheProgram">). False only on OOM. */
bool qa_comp_create_q1_source_composition(qa_comp_q1_source_composition *composition, qa_q1_host host,
                                          const qa_q1_options *options,
                                          const qa_comp_q1_source_selection *selection,
                                          const qa_comp_q1_services *services);

#endif
