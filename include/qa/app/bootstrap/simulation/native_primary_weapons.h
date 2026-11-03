/* Port of src/app/bootstrap/simulation/native-primary-weapons.ts.
 * The TS closures capture the world and actor registry; in C that state is
 * a caller-owned qa_app_sim_native_primary_weapon_context the host table
 * points at, which must outlive the weapons. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_PRIMARY_WEAPONS_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_PRIMARY_WEAPONS_H
#include <stdbool.h>
#include "qa/app/bootstrap/simulation/classic_guest_world.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_world.h"
#include "qa/compat/q2/native_primary_weapons.h"
#include "qa/world/actors/registry.h"

/* TS ClassicGuestWorld | RereleaseGuestWorld. */
typedef struct qa_app_sim_q2_native_world { bool rerelease; qa_app_sim_classic_guest_world *classic; qa_app_sim_rerelease_guest_world *rerelease_world; } qa_app_sim_q2_native_world;
typedef struct qa_app_sim_native_primary_weapon_context { qa_app_sim_q2_native_world world; qa_session_actor_registry *actors; } qa_app_sim_native_primary_weapon_context;
/* TS nativePrimaryWeaponHost(world, actors); context is filled and kept by the caller. */
qa_compat_q2_native_primary_weapon_host qa_app_sim_native_primary_weapon_host(qa_app_sim_native_primary_weapon_context *context,
                                                                              qa_app_sim_q2_native_world world, qa_session_actor_registry *actors);
qa_compat_q2_native_primary_weapons *qa_app_sim_bind_native_primary_weapons(qa_app_sim_native_primary_weapon_context *context,
    qa_app_sim_q2_native_world world, qa_session_actor_registry *actors,
    const qa_compat_q2_native_primary_weapon_profile *profile, qa_compat_q2_native_primary_weapon_hooks hooks);
#endif
