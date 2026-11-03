/* Port of src/compat/q2/native-mod-items.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_ITEMS_H
#define QA_COMPAT_Q2_NATIVE_MOD_ITEMS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/classic/layout.h"
#include "qa/compat/q2/native_throw.h"
#include "qa/compat/q2/native_mod_weapon_stage.h"
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/contracts/native_mod_items.h"
#include "qa/contracts/original_pickups.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
#include "qa/world/session/mods.h"

/* TS status: "pending"|"accepted"|"refused". item NULL = TS null. */
typedef struct qa_compat_q2_native_item_checkpoint_request { qa_saved_actor_id actor; double id; qa_item_id item; const char *status; } qa_compat_q2_native_item_checkpoint_request;
/** TS NativeItemCheckpoint (version is the literal 1). requests is malloc'd. */
typedef struct qa_compat_q2_native_item_checkpoint { double next_request; const qa_compat_q2_native_item_checkpoint_request *requests; size_t request_count; } qa_compat_q2_native_item_checkpoint;
void qa_compat_q2_native_item_checkpoint_free(qa_compat_q2_native_item_checkpoint *checkpoint);
qa_compat_q2_native_item_checkpoint qa_compat_q2_read_native_item_checkpoint(const qa_save_reader *reader, const qa_native_mod_items *definition);
void qa_compat_q2_validate_native_mod_items(const qa_native_mod_declaration *declaration);

/** TS Operations. pointer: guest address of record for actor. model NULL = TS null. pickup NULL = TS undefined. */
typedef struct qa_compat_q2_native_mod_item_operations {
  void *ctx;
  qa_guest_address (*pointer)(void *ctx, qa_actor_id actor, const char *record);
  qa_guest_address (*resolve)(void *ctx, qa_native_mod_address address);
  bool (*live)(void *ctx, qa_actor_id actor);
  double (*read)(void *ctx, qa_guest_address address, qa_native_mod_scalar encoding);
  void (*write)(void *ctx, qa_guest_address address, qa_native_mod_scalar encoding, double value);
  void (*invoke)(void *ctx, qa_actor_id actor, const qa_native_mod_source_call *call);
  const qa_source_weapon_presentation_model *(*model)(void *ctx, qa_actor_id actor);
  const qa_original_pickup_execution *(*pickup)(void *ctx);
} qa_compat_q2_native_mod_item_operations;
/** TS NativeModHost pick the items and their weapon stage read; field-compatible with qa_compat_q2_native_weapon_host. */
typedef struct qa_compat_q2_native_mod_item_host {
  qa_compat_q2_memory *memory;
  qa_compat_q2_runner *runner;
  qa_guest_address image_base;
  void *ctx;
  qa_guest_call_result (*invoke)(void *ctx, qa_guest_address address, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *values, size_t count);
  qa_guest_address (*entry)(void *ctx, const char *name);
} qa_compat_q2_native_mod_item_host;
/** TS cancellation(actor). Carried by value; accepts compares the caught error to
 * the actor's admitted entry (SourceItemRetired of its lease, actor and owner). */
typedef struct qa_compat_q2_native_item_cancellation {
  void *items; const void *entry; qa_actor_id actor; qa_owned_actor owned;
} qa_compat_q2_native_item_cancellation;
bool qa_compat_q2_native_item_cancellation_accepts(const qa_compat_q2_native_item_cancellation *c, const qa_compat_q2_native_thrown *error);
bool qa_compat_q2_native_item_cancellation_source_current(const qa_compat_q2_native_item_cancellation *c);

typedef struct qa_compat_q2_native_mod_items qa_compat_q2_native_mod_items;
qa_compat_q2_native_mod_items *qa_compat_q2_native_mod_items_create(const qa_native_mod_items *definition, qa_native_abi abi, qa_compat_q2_native_mod_item_host host, const qa_mod_host_services *services, qa_provider_id provider, qa_content_id content, qa_compat_q2_native_mod_item_operations operations);
void qa_compat_q2_native_mod_items_admit(qa_compat_q2_native_mod_items *s, qa_actor_id actor);
qa_compat_q2_native_item_cancellation qa_compat_q2_native_mod_items_cancellation(qa_compat_q2_native_mod_items *s, qa_actor_id actor);
bool qa_compat_q2_native_mod_items_accepts_attack(qa_compat_q2_native_mod_items *s, qa_actor_id actor);
/* Caller frees the result with qa_compat_q2_native_item_checkpoint_free. */
qa_compat_q2_native_item_checkpoint qa_compat_q2_native_mod_items_checkpoint(const qa_compat_q2_native_mod_items *s);
void qa_compat_q2_native_mod_items_restore(qa_compat_q2_native_mod_items *s, const qa_compat_q2_native_item_checkpoint *value);
void qa_compat_q2_native_mod_items_release(qa_compat_q2_native_mod_items *s, qa_actor_id actor);
void qa_compat_q2_native_mod_items_clear(qa_compat_q2_native_mod_items *s);
void qa_compat_q2_native_mod_items_close(qa_compat_q2_native_mod_items *s);
void qa_compat_q2_native_mod_items_destroy(qa_compat_q2_native_mod_items *s);
#endif
