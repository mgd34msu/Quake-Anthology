/* Port of src/content/q3/base/records.ts. GPL-2.0-or-later.
 * GameEntity/GameClient are the landed game/state mirror types: entity
 * health/takedamage/body accessors read the inline body mirror rather
 * than forwarding to the authority/body tables (the authority binding
 * below still forwards), and ammoTimerStored has no mirror slot hook.
 * damageInflictor's shared-actor origin ctx is a malloc'd cell owned
 * by the records (freed on destroy is not tracked; cells are small and
 * bounded by foreign-damage calls). */
#ifndef QA_CONTENT_Q3_BASE_RECORDS_H
#define QA_CONTENT_Q3_BASE_RECORDS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/q3/base/game/combat.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/world.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"

/* TS admitDamage result. */
typedef enum qa_q3_base_admission { QA_Q3_BASE_ADMIT_CONTINUE, QA_Q3_BASE_ADMIT_HANDLED } qa_q3_base_admission;

/* TS Q3RecordHost. schedule has_due false is TS null due time;
 * damage_call false is TS null; ammo_timer_stored/admit_damage NULL fn
 * is TS undefined. */
typedef struct qa_q3_base_record_host {
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  qa_actor_callback_table *callbacks;
  void *ctx;
  void (*ammo_timer_stored)(void *ctx, qa_actor_id actor, double weapon, double value);
  void (*schedule)(void *ctx, qa_owned_actor actor, bool has_due, double due_milliseconds);
  void (*run_think)(void *ctx, qa_owned_actor actor, double time_milliseconds);
  bool (*damage_call)(void *ctx, qa_q3_damage_call *out);
  qa_q3_base_admission (*admit_damage)(void *ctx, qa_q3_game_entity *entity, qa_damage_request request);
  qa_q3_game_entity *(*foreign)(void *ctx, qa_actor_id actor);
  bool (*is_player)(void *ctx, qa_actor_id actor);
} qa_q3_base_record_host;

/* TS captureOwnership() entry. has_actor false is TS null. */
typedef struct qa_q3_base_ownership_state {
  bool has_actor; qa_owned_actor actor; bool active, borrowed;
} qa_q3_base_ownership_state;

/* TS Q3EntityRecords. Arrays are heap-owned; init returns false on OOM. */
typedef struct qa_q3_base_entity_records qa_q3_base_entity_records;
bool qa_q3_base_entity_records_init(qa_q3_base_entity_records *records,
    qa_q3_base_record_host host, qa_provider_id provider, qa_q3_base_product product);
void qa_q3_base_entity_records_destroy(qa_q3_base_entity_records *records);
/* Heap convenience over init/destroy (NULL only on OOM). */
qa_q3_base_entity_records *qa_q3_base_entity_records_create(
    qa_q3_base_record_host host, qa_provider_id provider, qa_q3_base_product product);
void qa_q3_base_entity_records_free(qa_q3_base_entity_records *records);
void qa_q3_base_entity_records_close(qa_q3_base_entity_records *records);
const qa_q3_base_record_host *qa_q3_base_entity_records_host(const qa_q3_base_entity_records *records);
qa_provider_id qa_q3_base_entity_records_provider(const qa_q3_base_entity_records *records);
qa_q3_base_product qa_q3_base_entity_records_product(const qa_q3_base_entity_records *records);

/* Malloc'd array of MAX_GENTITIES entries (caller frees with free()). */
qa_q3_base_ownership_state *qa_q3_base_entity_records_capture_ownership(qa_q3_base_entity_records *records);
void qa_q3_base_entity_records_restore_ownership(qa_q3_base_entity_records *records,
    const qa_q3_base_ownership_state *states, size_t count);
void qa_q3_base_entity_records_restore_callbacks(qa_q3_base_entity_records *records);
/* out arrays hold 16 values. */
void qa_q3_base_entity_records_capture_client_backing(qa_q3_base_entity_records *records, double slot,
    double source_stats[16], double special_ammo[16]);
void qa_q3_base_entity_records_restore_client_backing(qa_q3_base_entity_records *records, double slot,
    const double *source_stats, size_t source_count, const double *special_ammo, size_t ammo_count);

/* TS get(): NULL is TS undefined. */
qa_q3_game_entity *qa_q3_base_entity_records_get(qa_q3_base_entity_records *records, double slot);
qa_q3_game_client *qa_q3_base_entity_records_client(qa_q3_base_entity_records *records, double slot);
qa_q3_game_entity *qa_q3_base_entity_records_adopt(qa_q3_base_entity_records *records, double slot, qa_owned_actor actor);
qa_q3_game_entity *qa_q3_base_entity_records_attach(qa_q3_base_entity_records *records, double slot,
    qa_owned_actor actor, bool player);
qa_q3_game_entity *qa_q3_base_entity_records_activate(qa_q3_base_entity_records *records, double slot);
void qa_q3_base_entity_records_deactivate_client(qa_q3_base_entity_records *records, double slot);
void qa_q3_base_entity_records_release(qa_q3_base_entity_records *records, qa_q3_game_entity *entity);
/* actor NULL (has_actor false) mirrors the TS null parameter. */
qa_q3_game_entity *qa_q3_base_entity_records_native_by_actor(qa_q3_base_entity_records *records,
    bool has_actor, qa_actor_id actor);
qa_q3_game_entity *qa_q3_base_entity_records_by_actor(qa_q3_base_entity_records *records,
    bool has_actor, qa_actor_id actor);
/* TS useParticipant(): returns false for TS null result. */
bool qa_q3_base_entity_records_use_participant(qa_q3_base_entity_records *records,
    bool has_actor, qa_actor_id actor, qa_q3_use_participant *out);
qa_q3_damage_inflictor qa_q3_base_entity_records_damage_inflictor(qa_q3_base_entity_records *records,
    bool has_actor, qa_actor_id actor);
/* TS private playerBinding(slot), exposed for the session wiring.
 * Returned binding borrows records-owned ctx storage. */
qa_q3_base_player_authority_binding qa_q3_base_entity_records_player_binding(
    qa_q3_base_entity_records *records, double slot);
/* EntityPool records table (qa_q3_entity_records) over these records. */
qa_q3_entity_records qa_q3_base_entity_records_pool_table(qa_q3_base_entity_records *records);
#endif
