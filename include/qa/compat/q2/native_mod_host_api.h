/* TS NativeModHost / NativeModProjection (src/app/bootstrap/simulation/
 * native-mod-host.ts) as the fn+ctx table every native-mod-* class takes,
 * in contracts-world types (guest/abi headers conflict with
 * contracts/execution.h in one TU; layout.h precedent). Also the guest value
 * codec (guest/abi/values.ts encodeValue/decodeValue/storageBytes) over those
 * types. CPU and callback-table access goes through the shared runner mirror.
 * Recoverable throws use qa/compat/q2/native_throw.h; every other TS throw
 * stays fatal (qa_range_error), as docs/PORTING.md. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_HOST_API_H
#define QA_COMPAT_Q2_NATIVE_MOD_HOST_API_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/classic/layout.h"
#include "qa/compat/q2/native_mod_entries.h"
#include "qa/compat/q2/native_throw.h"
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/session.h"
#include "qa/core/commands/index.h"
#include "qa/persistence/value.h"
#include "qa/world/session/mods.h"

/* TS try { body } finally { cleanup } over native_throw.h. */
void qa_compat_q2_native_mod_finally(void (*body)(void *), void *body_ctx, void (*cleanup)(void *), void *cleanup_ctx);

/* --- Guest value codec. --- */
int qa_compat_q2_native_storage_bytes(const char *storage, int64_t pointer_bytes);
/* NativeModScalar width: TS field.encoding.endsWith("64"|"16"|"8") else 4. */
int qa_compat_q2_native_scalar_bytes(const char *encoding);
/* TS encodeValue for a scalar layout; returns the byte count (out holds 8). */
int qa_compat_q2_native_encode_scalar(const qa_compat_q2_memory *memory, const char *storage, const qa_guest_call_value *value, uint8_t *out);
/* TS decodeValue for a scalar layout. */
qa_guest_call_value qa_compat_q2_native_decode_scalar(const qa_compat_q2_memory *memory, const char *storage, const uint8_t *bytes);

/* --- NativeModHost. --- */
typedef struct qa_compat_q2_written_range { double byte_offset, byte_length; } qa_compat_q2_written_range;
typedef void (*qa_compat_q2_written_fn)(void *ctx, const qa_compat_q2_written_range *ranges, size_t count);
typedef struct qa_compat_q2_native_mod_entities { qa_guest_address base; double stride, count, capacity; } qa_compat_q2_native_mod_entities;
typedef struct qa_compat_q2_native_mod_game_entry { qa_guest_address address; qa_compat_q2_guest_call_signature signature; } qa_compat_q2_native_mod_game_entry;
typedef struct qa_compat_q2_native_mod_actor_slot { qa_actor_id actor; double slot; } qa_compat_q2_native_mod_actor_slot;
typedef struct qa_compat_q2_native_mod_configstring { double index; const char *value; } qa_compat_q2_native_mod_configstring;
/* TS NativeModSourceSave. Bytes and strings are borrowed from the producer. */
typedef struct qa_compat_q2_native_mod_source_save {
  bool rerelease;
  qa_provider_checkpoint original; /* classic */
  qa_byte_view game, level, cvars;  /* rerelease */
  const qa_compat_q2_native_mod_configstring *configstrings; size_t configstring_count;
} qa_compat_q2_native_mod_source_save;
/* TS NativeModPresentation. checkpoint values are owned qa_save_value trees. */
typedef struct qa_compat_q2_native_mod_presentation {
  void *ctx;
  void (*release)(void *ctx, qa_actor_id actor);
  void (*begin_frame)(void *ctx);
  void (*publish)(void *ctx, const qa_compat_q2_native_mod_actor_slot *entries, size_t count, bool events);
  const char *(*signature)(void *ctx, double slot);
  const qa_app_simulation_presentation *(*appearance)(void *ctx, qa_actor_id actor, double slot, size_t *out_count);
  double (*generation)(void *ctx);
  bool (*client_frame)(void *ctx, qa_actor_id actor, qa_mod_client_presentation_frame *out); /* false is TS null */
  qa_save_value *(*checkpoint)(void *ctx);
  void (*restore)(void *ctx, const qa_save_value *saved);
  void (*close)(void *ctx);
} qa_compat_q2_native_mod_presentation;
typedef struct qa_compat_q2_native_mod_host {
  void *ctx;
  qa_content_id content;
  qa_compat_q2_memory *memory;
  qa_guest_address image_base;
  qa_compat_q2_native_mod_presentation presentation;
  /* entries.cpu and entries.callbacks. */
  qa_compat_q2_runner *runner;
  qa_native_call_abi abi; /* declaration.target.abi, for bindInlineRegion */
  void (*set_instruction_pointer)(void *ctx, qa_guest_address address);
  /* memory.observeWrites with its ranges; offsets are relative to address. */
  uint64_t (*observe_writes)(void *ctx, qa_guest_address address, double byte_length, void *cb_ctx, qa_compat_q2_written_fn cb);
  void (*unobserve_writes)(void *ctx, uint64_t handle);
  void (*synchronize_frame)(void *ctx, double seconds, double frame);
  qa_raw_entity_view (*entity)(void *ctx, double slot);
  bool (*client)(void *ctx, double slot, qa_guest_address *out); /* false is TS null */
  bool (*active)(void *ctx, double slot);
  void (*clear_entity_event)(void *ctx, double slot);
  void (*project_player_view)(void *ctx, double slot, double view_height); /* optional */
  const qa_source_weapon_presentation_model *(*weapon_model)(void *ctx, double slot); /* NULL result is TS null */
  /* TS encodeTrace: a malloc'd byte buffer. */
  uint8_t *(*encode_trace)(void *ctx, const qa_trace_result *trace, size_t *out_length);
  bool (*invoke_command)(void *ctx, qa_command_invocation *command);
  void (*with_command)(void *ctx, qa_command_invocation *command, void (*invoke)(void *), void *invoke_ctx);
  qa_guest_address (*entry)(void *ctx, const char *name);
  qa_compat_q2_native_mod_game_entry (*game_entry)(void *ctx, const char *name);
  qa_compat_q2_native_mod_entities (*entities)(void *ctx);
  qa_guest_call_result (*invoke)(void *ctx, qa_guest_address entry, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *values, size_t count);
  void (*initialize)(void *ctx, bool restoring);
  qa_compat_q2_native_mod_source_save (*checkpoint)(void *ctx);
  void (*restore)(void *ctx, const qa_compat_q2_native_mod_source_save *save);
  void (*close)(void *ctx);
} qa_compat_q2_native_mod_host;

/* Host conveniences. */
/* TS host.bindInlineRegion; accepts may be NULL (always true). */
uint64_t qa_compat_q2_native_mod_bind_region(const qa_compat_q2_native_mod_host *host, qa_guest_address entry, qa_guest_address join, void *cb_ctx,
    void (*cb)(void *, qa_compat_q2_inline_continuation *), void *accepts_ctx, bool (*accepts)(void *));
void qa_compat_q2_native_mod_unbind_region(const qa_compat_q2_native_mod_host *host, uint64_t handle);
/* TS bindNativeModEntry on this host (native_mod_entries.h). */
qa_compat_q2_native_entry *qa_compat_q2_native_mod_bind_entry(const qa_compat_q2_native_mod_host *host, qa_guest_address address, const char *id,
    qa_compat_q2_guest_call_signature signature, void *ctx, qa_compat_q2_native_entry_execute_fn execute, void *accepts_ctx, bool (*accepts)(void *));
qa_guest_address qa_compat_q2_native_mod_image_rva(const qa_compat_q2_native_mod_host *host, double rva);
/* TS entry.kind === "export" ? host.entry(name) : image + rva. */
qa_guest_address qa_compat_q2_native_mod_entry_address(const qa_compat_q2_native_mod_host *host, qa_native_mod_entry entry);
uint64_t qa_compat_q2_native_mod_stack(const qa_compat_q2_native_mod_host *host);
/* TS (address - table.base) / stride; aborts with message outside the table. */
double qa_compat_q2_native_mod_table_slot(const qa_compat_q2_native_mod_host *host, qa_guest_address address, const char *message);
/* TS isDeepStrictEqual over armor states. */
bool qa_compat_q2_regular_armor_equal(qa_regular_armor_state a, qa_regular_armor_state b);
bool qa_compat_q2_powered_protection_equal(qa_powered_protection_state a, qa_powered_protection_state b);
/* String equality where either side may be null. */
bool qa_compat_q2_text_equal(const char *a, const char *b);
/* TS new Set-free append helper: grows *items and returns the new element. */
void *qa_compat_q2_grow(void **items, size_t *count, size_t *capacity, size_t element);
qa_source_time qa_compat_q2_services_time(const qa_mod_host_services *services);
double qa_compat_q2_time_seconds(qa_source_time time);
#endif
