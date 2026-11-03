/* Port of src/compat/q2/native-mod-weapon-stage.ts. The original dispatcher
 * retains firing, latch, animation and callback timing. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_WEAPON_STAGE_H
#define QA_COMPAT_Q2_NATIVE_MOD_WEAPON_STAGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/classic/layout.h"
#include "qa/compat/q2/native_mod_entries.h"
#include "qa/compat/q2/native_throw.h"
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/contracts/native_mod_items.h"
#include "qa/foundation/support.h"

/* TS Pick<NativeModHost, "memory"|"entries"|"invoke"|"imageBase"|"entry"|"bindInlineRegion">.
 * Field-compatible with qa_compat_q2_native_mod_item_host. */
typedef struct qa_compat_q2_native_weapon_host {
  qa_compat_q2_memory *memory;
  qa_compat_q2_runner *runner;
  qa_guest_address image_base;
  void *ctx;
  qa_guest_call_result (*invoke)(void *ctx, qa_guest_address address, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *values, size_t count);
  qa_guest_address (*entry)(void *ctx, const char *name);
} qa_compat_q2_native_weapon_host;
/* TS cancellation?(actor). */
typedef struct qa_compat_q2_native_weapon_cancellation {
  void *ctx;
  bool (*accepts)(void *ctx, const qa_compat_q2_native_thrown *error);
  bool (*source_current)(void *ctx);
} qa_compat_q2_native_weapon_cancellation;
/* TS NativeWeaponOperations. Optional members may be NULL. actor false is TS
 * null. The dispatcher alone reads actor/current/selected/read/write/completed/
 * cancellation/committed_input. */
typedef struct qa_compat_q2_native_weapon_operations {
  void *ctx;
  bool (*actor)(void *ctx, const char *record, qa_guest_address address, qa_actor_id *out);
  bool (*current)(void *ctx, qa_actor_id actor);
  bool (*selected)(void *ctx, qa_actor_id actor);
  bool (*committed_input)(void *ctx, qa_actor_id actor);
  qa_guest_address (*pointer)(void *ctx, qa_actor_id actor, qa_native_item_pointer field);
  qa_guest_address (*resolve)(void *ctx, qa_native_mod_address address);
  double (*read)(void *ctx, qa_actor_id actor, qa_native_item_field field);
  void (*write)(void *ctx, qa_actor_id actor, qa_native_item_field field, double value);
  void (*invoke)(void *ctx, qa_actor_id actor, const qa_native_mod_source_call *call);
  void (*completed)(void *ctx, qa_actor_id actor, bool reached_decision);
  bool (*cancellation)(void *ctx, qa_actor_id actor, qa_compat_q2_native_weapon_cancellation *out);
} qa_compat_q2_native_weapon_operations;

typedef struct qa_compat_q2_native_weapon_dispatcher qa_compat_q2_native_weapon_dispatcher;
/* The definition, id and operations table are retained (id is copied). provider is
 * the TS `${provider}:weapon-dispatcher` prefix. */
qa_compat_q2_native_weapon_dispatcher *qa_compat_q2_native_weapon_dispatcher_create(
    const qa_native_weapon_dispatcher *dispatcher, const qa_native_weapon_decision *decisions, size_t decision_count,
    qa_native_abi abi, qa_compat_q2_native_weapon_host host, qa_provider_id provider, qa_compat_q2_native_weapon_operations operations);
/* False is TS null. */
bool qa_compat_q2_native_weapon_dispatcher_current_actor(const qa_compat_q2_native_weapon_dispatcher *s, qa_actor_id *out);
void qa_compat_q2_native_weapon_dispatcher_close(qa_compat_q2_native_weapon_dispatcher *s);
void qa_compat_q2_native_weapon_dispatcher_destroy(qa_compat_q2_native_weapon_dispatcher *s);

typedef struct qa_compat_q2_native_mod_weapon_stage qa_compat_q2_native_mod_weapon_stage;
qa_compat_q2_native_mod_weapon_stage *qa_compat_q2_native_mod_weapon_stage_create(const qa_native_weapon_stage *definition, qa_native_abi abi,
    qa_compat_q2_native_weapon_host host, qa_provider_id provider, qa_compat_q2_native_weapon_operations operations);
bool qa_compat_q2_native_mod_weapon_stage_continuing(qa_compat_q2_native_mod_weapon_stage *s, qa_actor_id actor);
bool qa_compat_q2_native_mod_weapon_stage_settled(qa_compat_q2_native_mod_weapon_stage *s, qa_actor_id actor);
/* NULL is TS null. */
qa_item_id qa_compat_q2_native_mod_weapon_stage_active(qa_compat_q2_native_mod_weapon_stage *s, qa_actor_id actor);
qa_item_id qa_compat_q2_native_mod_weapon_stage_pending(qa_compat_q2_native_mod_weapon_stage *s, qa_actor_id actor);
bool qa_compat_q2_native_mod_weapon_stage_request(qa_compat_q2_native_mod_weapon_stage *s, qa_actor_id actor, qa_item_id item);
void qa_compat_q2_native_mod_weapon_stage_close(qa_compat_q2_native_mod_weapon_stage *s);
void qa_compat_q2_native_mod_weapon_stage_destroy(qa_compat_q2_native_mod_weapon_stage *s);
#endif
